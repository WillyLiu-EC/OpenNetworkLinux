/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AS1817-64O FPGA PCI Core Driver with MFD Support
 *
 * Copyright (C) 2025 Accton Technology Corporation
 *
 * This driver binds to the fixed FPGA PCIe function on the AS1817-64O
 * platform and exposes it as a multi-function device (MFD).
 *
 * Main responsibilities:
 *  - Claim and map the FPGA PCIe BAR0 MMIO region.
 *  - Provide a serialized MMIO access API (struct as1817_64o_fpga_ops)
 *    for child platform drivers.
 *  - Expose minimal identification and debug information via sysfs:
 *      fpga_version   - FPGA image version (major.minor).
 *      cpld0_version  - CPLD0 version (major.minor, window at 0x2000).
 *      cpld1_version  - CPLD1 version (major.minor, window at 0x3000).
 *      fpga_debug_reg - Simple helper for ad-hoc 8-bit register access.
 *  - Register MFD child devices that implement the individual FPGA
 *    functions (for example, transceiver control).
 *  - Participate in PCIe AER error-recovery callbacks so that the
 *    non-hotpluggable FPGA can recover from link errors or function
 *    level resets when possible.
 *
 * High level architecture:
 *
 *   +---------------------------+
 *   |   Linux PCI Subsystem    |
 *   +---------------------------+
 *                |
 *                v
 *   +--------------------------------------+
 *   | x86-64-accton-as1817-64o-fpga.c      |  (this file, MFD parent)
 *   +--------------------------------------+
 *        |             |
 *        |             +--> MFD child platform devices
 *        |
 *        +--> BAR0 MMIO: FPGA registers and CPLD windows
 *
 * Child drivers must use the as1817_64o_fpga_ops callbacks provided by
 * this core instead of accessing the MMIO space directly.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/hwmon-sysfs.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>

#include "x86-64-accton-as1817-64o-fpga.h"

/*
 * PCI BAR (Base Address Register) Definitions
 */
#define BAR0_NUM            0    /* Primary control register BAR */


/*
 * FPGA Register Map Definitions
 */

/*
 * FPGA_PCIE_START_OFFSET - Base offset for FPGA control registers
 *
 * This defines the starting offset within BAR0 where FPGA-specific
 * control registers begin. All FPGA register offsets are relative
 * to this base address.
 */
#define FPGA_PCIE_START_OFFSET        0x0000

/*
 * FPGA Board / PCB version register - hardware identification
 *
 * Offset 0x00 layout:
 *   bits[7:4] : BOARD_ID[3:0]
 *   bits[3:2] : PCB_version[1:0]
 *   bit1      : RESERVED
 *   bit0      : RESERVED_ID (TBD)
 */
#define FPGA_BOARD_ID_REG		  (FPGA_PCIE_START_OFFSET + 0x00)
#define FPGA_BOARD_ID_MASK		  GENMASK(7, 4)
#define FPGA_BOARD_ID_SHIFT       4
#define FPGA_PCB_VERSION_MASK     GENMASK(3, 2)
#define FPGA_PCB_VERSION_SHIFT    2

/*
 * FPGA version registers - Firmware identification
 *
 * These registers contain the major and minor version numbers of the
 * FPGA firmware, allowing software to identify firmware capabilities
 * and compatibility.
 */
#define FPGA_MAJOR_VER_REG        (FPGA_PCIE_START_OFFSET + 0x01)
#define FPGA_MINOR_VER_REG        (FPGA_PCIE_START_OFFSET + 0x02)

/*
 * CPLD*_PCIE_START_OFFSET - Base offsets for CPLD register windows
 *
 * The CPLD blocks are memory-mapped into the FPGA BAR0 space. Each CPLD
 * has its own MMIO window:
 *   - CPLD0 at 0x2000
 *   - CPLD1 at 0x3000
 *
 * All CPLD register offsets for each instance are relative
 * to this base address.
 */
#define CPLD0_PCIE_START_OFFSET        0x2000
#define CPLD1_PCIE_START_OFFSET        0x3000

/*
 * CPLD version registers - CPLD firmware identification
 *
 * Offset 0x00 (CPLD_VER_MAJOR):
 *   bit7   : 0 = Primary image, 1 = Golden image
 *   bit6:5 : Reserved
 *   bit4   : 0 = EVT & DVT status, 1 = MP status
 *   bit3:0 : Revision code
 *            If bit4 = 0: 4'h1~4'hf => R0A~R0O (EVT & DVT)
 *            If bit4 = 1: 4'h1~4'hf => R01~R15 (MP)
 *
 * Offset 0x01 (CPLD_VER_MINOR):
 *   bit7:0 : CPLD minor version
 *
 * For now, this driver only exposes the raw <major>.<minor> version
 * numbers through sysfs. The bitfield layout is documented here for
 * future decoding if needed.
 */
#define CPLD0_MAJOR_VER_REG        (CPLD0_PCIE_START_OFFSET + 0x00)
#define CPLD0_MINOR_VER_REG        (CPLD0_PCIE_START_OFFSET + 0x01)
#define CPLD1_MAJOR_VER_REG        (CPLD1_PCIE_START_OFFSET + 0x00)
#define CPLD1_MINOR_VER_REG        (CPLD1_PCIE_START_OFFSET + 0x01)


/* CPLD_VER_MAJOR bit definitions */
#define CPLD_VER_MAJOR_IMAGE_BIT      BIT(7)   /* 0: Primary, 1: Golden */
/* Bits 6:5 are reserved */
#define CPLD_VER_MAJOR_STAGE_BIT      BIT(4)   /* 0: EVT&DVT, 1: MP */
#define CPLD_VER_MAJOR_REV_MASK       GENMASK(3, 0) /* 4'h1~4'hf */

enum cpld_stage {
    CPLD_STAGE_EVT_DVT = 0,
    CPLD_STAGE_MP,
};

/*
 * struct as1817_64o_fpga_mfd_data - MFD device private data
 * @dev: Device pointer
 * @pdev: PCI device pointer
 * @base_addr: Mapped BAR0 virtual address
 * @phys_addr: Physical BAR0 address
 * @mem_size: BAR0 memory region size
 * @fpga_id: FPGA device identifier (board-specific enum, for example FPGA_CB)
 * @pdata: Platform data instance passed to children
 */
struct as1817_64o_fpga_mfd_data {
    struct device *dev;
    struct pci_dev *pdev;
    void __iomem *base_addr;
    resource_size_t phys_addr;
    resource_size_t mem_size;
    fpga_device_id_t fpga_id;
    struct as1817_64o_fpga_platform_data pdata;
    struct platform_device *fpga_pdev;
};

/*
 * PCI Device Identification
 */
#define FPGA_PCI_VENDOR_ID          0x10EE    /* Xilinx PCI vendor identifier */
#define FPGA_PCI_DEVICE_ID          0x7021    /* FPGA PCI device ID */

/*
 * PCI Device ID Table
 */

/*
 * as1817_64o_fpga_pci_table - Supported PCI device identifiers
 *
 * This table lists the PCI vendor and device ID pairs that this driver
 * claims. The kernel's PCI subsystem uses this table to match detected
 * hardware with this driver.
 */
static const struct pci_device_id as1817_64o_fpga_pci_table[] = {
    { PCI_DEVICE(FPGA_PCI_VENDOR_ID, FPGA_PCI_DEVICE_ID) },
    { 0, }  /* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, as1817_64o_fpga_pci_table);

/*
 * MFD Operations Implementation
 *
 * These functions provide a thread-safe abstraction layer for accessing
 * the FPGA's I/O memory. All child drivers must use these operations
 * via the platform data to ensure synchronized access.
 */

static u8 as1817_64o_fpga_mfd_read8(struct as1817_64o_fpga_platform_data *pdata, u16 reg)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = pdata->mfd_private;
    u8 val;

    mutex_lock(&pdata->lock);
    val = ioread8(mfd_data->base_addr + reg);
    mutex_unlock(&pdata->lock);

    return val;
}

static void as1817_64o_fpga_mfd_write8(struct as1817_64o_fpga_platform_data *pdata, u16 reg, u8 val)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = pdata->mfd_private;

    mutex_lock(&pdata->lock);
    iowrite8(val, mfd_data->base_addr + reg);
    mutex_unlock(&pdata->lock);
}

static u32 as1817_64o_fpga_mfd_read32(struct as1817_64o_fpga_platform_data *pdata, u16 reg)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = pdata->mfd_private;
    u32 val;

    mutex_lock(&pdata->lock);
    val = ioread32(mfd_data->base_addr + reg);
    mutex_unlock(&pdata->lock);

    return val;
}

static void as1817_64o_fpga_mfd_write32(struct as1817_64o_fpga_platform_data *pdata, u16 reg, u32 val)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = pdata->mfd_private;

    mutex_lock(&pdata->lock);
    iowrite32(val, mfd_data->base_addr + reg);
    mutex_unlock(&pdata->lock);
}

static const struct as1817_64o_fpga_ops as1817_64o_fpga_mfd_ops = {
    .read8 = as1817_64o_fpga_mfd_read8,
    .write8 = as1817_64o_fpga_mfd_write8,
    .read32 = as1817_64o_fpga_mfd_read32,
    .write32 = as1817_64o_fpga_mfd_write32,
};

/*
 * Sysfs Attributes
 *
 * This section implements sysfs entries for the core device, allowing
 * userspace to interact with the FPGA for diagnostics and debugging.
 * - fpga_version: Provides the firmware version.
 * - fpga_debug_reg: Allows direct read/write access to 8-bit registers
 * for low-level debugging.
 */

/*
 * pcb_version_show - Sysfs callback for reading PCB major revision field
 * @dev:  Device structure pointer (fpga platform device)
 * @attr: Device attribute structure pointer
 * @buf:  Buffer to store the formatted PCB version string
 *
 * The PCB version is encoded in bits [3:2] of the BOARD/PCB version
 * register at FPGA_BOARD_ID_REG:
 *   0b00: R0A
 *   0b01: R0B
 *   0b10: R0C
 *   0b11: R01
 *
 * This helper decodes the field and prints it as:
 *   "<hex> (<string>)\n"
 * for example: "0x0 (R0A)".
 *
 * Return: Number of bytes written to buffer on success, negative errno on error.
 */
static const char * const as1817_64o_pcb_version_names[] = {
	"R0A",
	"R0B",
	"R0C",
	"R01",
};

static ssize_t pcb_version_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct as1817_64o_fpga_mfd_data *mfd_data = dev_get_drvdata(dev);
	u8 reg_val;
	u8 pcb;
	const char *name;

	if (!mfd_data || !mfd_data->pdata.ops)
		return -ENODEV;

	reg_val = mfd_data->pdata.ops->read8(&mfd_data->pdata,
					     FPGA_BOARD_ID_REG);
	pcb = (reg_val & FPGA_PCB_VERSION_MASK) >> FPGA_PCB_VERSION_SHIFT;

	if (pcb < ARRAY_SIZE(as1817_64o_pcb_version_names))
		name = as1817_64o_pcb_version_names[pcb];
	else
		name = "unknown";

	return scnprintf(buf, PAGE_SIZE, "0x%02x (%s)\n", pcb, name);
}
static DEVICE_ATTR_RO(pcb_version);

/*
 * fpga_version_read - Sysfs callback for reading FPGA firmware version
 * @dev: Device structure pointer
 * @attr: Device attribute structure pointer
 * @buf: Buffer to store the formatted version string
 *
 * Reads the major and minor version registers from the FPGA hardware
 * and formats them as "major.minor" for display in sysfs.
 *
 * Return: Number of bytes written to buffer on success, negative errno on error.
 */
static ssize_t fpga_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = dev_get_drvdata(dev);
    u8 major, minor;

    if (!mfd_data || !mfd_data->pdata.ops)
        return -ENODEV;

    /* Use the ops table for reading, even from the core driver itself */
    major = mfd_data->pdata.ops->read8(&mfd_data->pdata, FPGA_MAJOR_VER_REG);
    minor = mfd_data->pdata.ops->read8(&mfd_data->pdata, FPGA_MINOR_VER_REG);

    return scnprintf(buf, PAGE_SIZE, "%u.%u\n", major, minor);
}
static DEVICE_ATTR_RO(fpga_version);

/*
 * cpld0_version_read - Sysfs callback for reading CPLD0 firmware version
 * @dev: Device structure pointer
 * @attr: Device attribute structure pointer
 * @buf: Buffer to store the formatted version string
 *
 * Reads the CPLD0 major and minor version registers from the MMIO window
 * and formats them as "major.minor" for display in sysfs.
 *
 * Return: Number of bytes written to buffer on success, negative errno on error.
 */
static ssize_t cpld0_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = dev_get_drvdata(dev);
    u8 major, minor;

    if (!mfd_data || !mfd_data->pdata.ops)
        return -ENODEV;

    /* Use the ops table for reading, even from the core driver itself */
    major = mfd_data->pdata.ops->read8(&mfd_data->pdata, CPLD0_MAJOR_VER_REG);
    minor = mfd_data->pdata.ops->read8(&mfd_data->pdata, CPLD0_MINOR_VER_REG);

    /* Keep only the revision bits [3:0] when reporting the major version. */
    major &= CPLD_VER_MAJOR_REV_MASK;

    return scnprintf(buf, PAGE_SIZE, "%u.%u\n", major, minor);
}
static DEVICE_ATTR_RO(cpld0_version);

/*
 * cpld1_version_read - Sysfs callback for reading CPLD1 firmware version
 * @dev: Device structure pointer
 * @attr: Device attribute structure pointer
 * @buf: Buffer to store the formatted version string
 *
 * Reads the CPLD1 major and minor version registers from the MMIO window
 * and formats them as "major.minor" for display in sysfs.
 *
 * Return: Number of bytes written to buffer on success, negative errno on error.
 */
static ssize_t cpld1_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = dev_get_drvdata(dev);
    u8 major, minor;

    if (!mfd_data || !mfd_data->pdata.ops)
        return -ENODEV;

    /* Use the ops table for reading, even from the core driver itself */
    major = mfd_data->pdata.ops->read8(&mfd_data->pdata, CPLD1_MAJOR_VER_REG);
    minor = mfd_data->pdata.ops->read8(&mfd_data->pdata, CPLD1_MINOR_VER_REG);

    /* Keep only the revision bits [3:0] when reporting the major version. */
    major &= CPLD_VER_MAJOR_REV_MASK;

    return scnprintf(buf, PAGE_SIZE, "%u.%u\n", major, minor);
}
static DEVICE_ATTR_RO(cpld1_version);

/**
 * fpga_debug_reg_show() - Display usage instructions for the debug attribute.
 */
static ssize_t fpga_debug_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE,
                   "Usage:\n"
                   "  Read:  echo <reg_offset> > fpga_debug_reg\n"
                   "  Write: echo <reg_offset> <value> > fpga_debug_reg\n"
                   "  -> <reg_offset> and <value> can be hex (0x) or decimal.\n"
                   "  -> View results with 'dmesg' or 'journalctl -k'\n");
}

/**
 * fpga_debug_reg_store() - Read or write an 8-bit FPGA register via sysfs.
 */
static ssize_t fpga_debug_reg_store(struct device *dev, struct device_attribute *attr,
                                    const char *buf, size_t count)
{
    struct as1817_64o_fpga_mfd_data *mfd_data = dev_get_drvdata(dev);
    unsigned int reg_u, val_u;
    u8 reg, val;
    int args;

    if (!mfd_data || !mfd_data->pdata.ops)
        return -ENODEV;

    /*
     * Parse one (read) or two (write) arguments from the buffer.
     * "%i" accepts either decimal ("16") or hexadecimal ("0x10").
     */
    args = sscanf(buf, "%i %i", &reg_u, &val_u);

    /* Validate input arguments */
    if (args < 1) {
        return -EINVAL; /* Must have at least one argument */
    }

    if (reg_u > 0xFF) {
        return -EINVAL; /* Register offset must be an 8-bit value */
    }

    if (args == 2 && val_u > 0xFF) {
        return -EINVAL; /* Value must be an 8-bit value */
    }

    reg = (u8)reg_u;
    val = (u8)val_u;

    if (args == 1) { /* Read operation */
        u8 read_val = mfd_data->pdata.ops->read8(&mfd_data->pdata, reg);
        dev_info(dev, "DEBUG_REG: Read reg 0x%02x = 0x%02x\n", reg, read_val);
    } else { /* Write operation (args == 2) */
        dev_info(dev, "DEBUG_REG: Write reg 0x%02x <== 0x%02x\n", reg, val);
        mfd_data->pdata.ops->write8(&mfd_data->pdata, reg, val);
    }

    return count;
}
static DEVICE_ATTR_RW(fpga_debug_reg);


static struct attribute *fpga_core_attributes[] = {
    &dev_attr_pcb_version.attr,
    &dev_attr_fpga_version.attr,
    &dev_attr_cpld0_version.attr,
    &dev_attr_cpld1_version.attr,
    &dev_attr_fpga_debug_reg.attr,
    NULL
};

static const struct attribute_group fpga_core_group = {
    .attrs = fpga_core_attributes,
};

/*
 * MFD Cell Definitions
 *
 * These definitions describe the child platform devices that will be
 * instantiated by the MFD core for a given FPGA instance. Each 'cell'
 * corresponds to a logical function within the FPGA (for example,
 * transceiver control) and is bound to its own driver.
 */
static const struct mfd_cell as1817_64o_fpga_cells[] = {
    { .name = XCVR_DRVNAME },
};

/**
 * as1817_64o_fpga_register_mfd_cells - Register MFD child devices
 * @dev:      The parent device structure.
 * @mfd_data: The private driver data containing FPGA-specific information.
 *
 * This function selects the appropriate MFD cell definitions based on the
 * FPGA ID and registers them as platform devices
 * using mfd_add_devices().
 *
 *
 * Return: 0 on success, or a negative errno code on failure.
 */
static int as1817_64o_fpga_register_mfd_cells(struct device *dev,
                                           struct as1817_64o_fpga_mfd_data *mfd_data)
{
    const struct mfd_cell *source_cells;
    struct mfd_cell *local_cells;
    int num_cells;
    int i, ret;

    dev_info(dev, "Selecting child device definitions for FPGA_CB\n");
    source_cells = as1817_64o_fpga_cells;
    num_cells = ARRAY_SIZE(as1817_64o_fpga_cells);

    if (num_cells == 0)
        return 0; /* No child devices to register. */

    /*
     * Use a temporary kcalloc()/kfree()-managed array here instead of
     * devm_kcalloc(). During PCIe AER error recovery the driver may call
     * mfd_remove_devices() from ->error_detected() and then invoke this
     * helper again from ->resume() to re-register the child devices.
     * Using devm_kcalloc() would allocate a new cell array on every
     * recovery attempt and keep it pinned in the parent's devres list
     * until final device removal. mfd_add_devices() copies the relevant
     * descriptors into the child devices, so the temporary array can be
     * freed immediately after registration.
     */
    local_cells = kcalloc(num_cells, sizeof(struct mfd_cell), GFP_KERNEL);
    if (!local_cells)
        return -ENOMEM;

    for (i = 0; i < num_cells; i++) {
        memcpy(&local_cells[i], &source_cells[i], sizeof(struct mfd_cell));
        local_cells[i].platform_data = &mfd_data->pdata;
        local_cells[i].pdata_size = sizeof(mfd_data->pdata);
    }

    ret = mfd_add_devices(dev, PLATFORM_DEVID_NONE,
                          local_cells, num_cells,
                          NULL, 0, NULL);

    kfree(local_cells);

    return ret;
}

/*
 * PCI Driver Implementation
 */
static int as1817_64o_fpga_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct device *dev = &pdev->dev;
    struct as1817_64o_fpga_mfd_data *mfd_data;
    u8 board_id_val;
    struct resource *res;
    int err;

	dev_info(dev, "Probing AS1817-64O FPGA MFD Core: %04x:%04x at %s\n",
		     pdev->vendor, pdev->device, pci_name(pdev));

    mfd_data = devm_kzalloc(dev, sizeof(*mfd_data), GFP_KERNEL);
    if (!mfd_data)
        return -ENOMEM;

    mfd_data->dev = dev;
    mfd_data->pdev = pdev;

    pci_set_drvdata(pdev, mfd_data);

    /* Step 1: Enable PCI device and request resources */
    err = pci_enable_device(pdev);
    if (err) {
        dev_err(dev, "Failed to enable PCI device: %d\n", err);
        return err;
    }

    /* Request PCI BAR resources */
    err = pci_request_regions(pdev, CORE_DRVNAME);
    if (err) {
        dev_err(dev, "Failed to request PCI regions: %d\n", err);
        pci_disable_device(pdev);
        return err;
    }

    /* Enable bus mastering */
    pci_set_master(pdev);

    /* Step 2: Map BAR0 into kernel virtual address space */
    mfd_data->base_addr = pci_iomap(pdev, BAR0_NUM, 0);
    if (!mfd_data->base_addr) {
        dev_err(dev, "Failed to map BAR0 memory region\n");
        err = -EIO;
        goto err_release_regions;
    }

    res = &pdev->resource[BAR0_NUM];
    mfd_data->phys_addr = res->start;
    mfd_data->mem_size = resource_size(res);

	/*
	 * Step 3: Read the hardware BOARD_ID register.
	 * The value is logged for debug and can be used in the future to
	 * distinguish between FPGA instances or board variants. This must be
	 * done AFTER iomap but BEFORE the MFD cells are registered so that
	 * the MMIO window is valid.
	 */
	board_id_val = ioread8(mfd_data->base_addr + FPGA_BOARD_ID_REG);
	board_id_val = (board_id_val & FPGA_BOARD_ID_MASK) >> FPGA_BOARD_ID_SHIFT;

	dev_info(dev, "Identifying FPGA by BOARD_ID register(0x%02x): 0x%x\n",
		 FPGA_BOARD_ID_REG, board_id_val);

    /*
     * The current AS1817-64O design exposes a single logical FPGA
     * instance, so we always tag it as FPGA_CB here. The BOARD_ID value
     * above is logged for debug and for potential future multi-FPGA
     * variants.
     */
    mfd_data->fpga_id = FPGA_CB;

    /* Step 4: Populate the shared platform data for child devices */
    mfd_data->pdata.fpga_id = mfd_data->fpga_id;
    mfd_data->pdata.base_addr = mfd_data->base_addr;
    mfd_data->pdata.phys_addr = mfd_data->phys_addr;
    mfd_data->pdata.mem_size = mfd_data->mem_size;
    mfd_data->pdata.ops = &as1817_64o_fpga_mfd_ops;
    mfd_data->pdata.mfd_private = mfd_data;
    mutex_init(&mfd_data->pdata.lock);

	/* Step 5: Register a platform device to host core sysfs attributes */
	mfd_data->fpga_pdev = platform_device_alloc(CORE_DRVNAME, PLATFORM_DEVID_NONE);
	if (!mfd_data->fpga_pdev) {
		err = -ENOMEM;
		dev_err(dev, "Failed to allocate fpga platform device: %d\n", err);
		goto err_unmap_bar0;
	}

	mfd_data->fpga_pdev->dev.parent = dev;

	err = platform_device_add(mfd_data->fpga_pdev);
	if (err) {
		dev_err(dev, "Failed to add fpga platform device: %d\n", err);
		platform_device_put(mfd_data->fpga_pdev);
		mfd_data->fpga_pdev = NULL;
		goto err_unmap_bar0;
	}

    /*
     * Allow core sysfs callbacks (fpga_version, cpld*_version, etc.)
     * to retrieve the MFD private data from the fpga platform device.
     */
    dev_set_drvdata(&mfd_data->fpga_pdev->dev, mfd_data);
	/* Step 6: Create sysfs attributes for diagnostics on fpga platform device */
	err = sysfs_create_group(&mfd_data->fpga_pdev->dev.kobj, &fpga_core_group);
	if (err) {
		dev_err(dev, "Failed to create fpga sysfs attributes: %d\n", err);
		goto err_unregister_fpga_pdev;
	}

	/* Step 7: Register child devices based on FPGA ID */
	err = as1817_64o_fpga_register_mfd_cells(dev, mfd_data);
	if (err) {
		dev_err(dev, "Failed to add MFD child devices: %d\n", err);
		goto err_remove_sysfs;
	}

	dev_info(dev, "AS1817-64O FPGA MFD Core initialized\n");

	return 0;

err_remove_sysfs:
	sysfs_remove_group(&mfd_data->fpga_pdev->dev.kobj, &fpga_core_group);
err_unregister_fpga_pdev:
	platform_device_unregister(mfd_data->fpga_pdev);
	mfd_data->fpga_pdev = NULL;
err_unmap_bar0:
    pci_iounmap(pdev, mfd_data->base_addr);
err_release_regions:
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    return err;
}

/**
 * as1817_64o_fpga_quiesce - Stop child devices and quiesce the FPGA core
 * @pdev: PCI device backing the FPGA
 *
 * This helper is shared between the PCI remove() and shutdown() paths.
 * It removes all MFD child devices and tears down the fpga platform
 * device so that no further MMIO or interrupts are generated. It does
 * not release the PCI BAR mapping or regions; those are only released
 * from the remove() callback when the device is actually unbound.
 */
static void as1817_64o_fpga_quiesce(struct pci_dev *pdev)
{
    struct device *dev = &pdev->dev;
    struct as1817_64o_fpga_mfd_data *mfd_data = pci_get_drvdata(pdev);

    if (!mfd_data) {
        dev_warn(dev,
                 "Quiesce requested but no driver data is available\n");
        return;
    }

    /* Step 1: Remove MFD child devices */
    mfd_remove_devices(dev);

    /* Step 2: Remove fpga platform device and its sysfs attributes */
    if (mfd_data->fpga_pdev) {
        sysfs_remove_group(&mfd_data->fpga_pdev->dev.kobj, &fpga_core_group);
        platform_device_unregister(mfd_data->fpga_pdev);
        mfd_data->fpga_pdev = NULL;
    }
}

static void as1817_64o_fpga_pci_remove(struct pci_dev *pdev)
{
    struct device *dev = &pdev->dev;
    struct as1817_64o_fpga_mfd_data *mfd_data = pci_get_drvdata(pdev);

    dev_info(dev, "Removing AS1817-64O FPGA MFD Core\n");

    /* Stop child devices and fpga platform device first */
    as1817_64o_fpga_quiesce(pdev);

    if (mfd_data) {
        mutex_destroy(&mfd_data->pdata.lock);
        /* Unmap BAR0 MMIO window */
        pci_iounmap(pdev, mfd_data->base_addr);
        pci_set_drvdata(pdev, NULL);
    }

    /* Release PCI resources */
    pci_release_regions(pdev);
    pci_disable_device(pdev);
}

static void as1817_64o_fpga_pci_shutdown(struct pci_dev *pdev)
{
    struct device *dev = &pdev->dev;

    dev_info(dev, "Shutting down AS1817-64O FPGA PCI device\n");

    /*
     * Put the FPGA core into a quiescent state so that it does not
     * generate further MMIO or interrupts across a reboot or kexec
     * into a new kernel. The PCI resources themselves are left for
     * the PCI core and the next kernel to re-initialize.
     */
    as1817_64o_fpga_quiesce(pdev);
    pci_disable_device(pdev);
}

/*
 * as1817_64o_fpga_error_detected - Handle PCI errors for fixed FPGA
 * @pdev: PCI device that encountered the error
 * @state: Current error state of the PCI channel
 *
 * For non-hotplug FPGA devices, we focus on recoverable errors:
 * - Transient PCIe link errors
 * - Correctable AER (Advanced Error Reporting) events
 * - Frozen I/O channels that may be recovered with a reset
 *
 * Return: PCI error recovery result
 */
static pci_ers_result_t as1817_64o_fpga_error_detected(struct pci_dev *pdev,
                            pci_channel_state_t state)
{
    struct device *dev = &pdev->dev;
    struct as1817_64o_fpga_mfd_data *mfd_data = pci_get_drvdata(pdev);

	if (!mfd_data) {
		dev_err(dev, "PCI error detected but no driver data is available\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

    dev_err(dev, "PCI error detected on fixed FPGA device: state=%d\n", state);

    switch (state) {
    case pci_channel_io_normal:
        /*
         * Minor/correctable error - often AER correctable errors.
         * Per PCI error recovery guidelines we must not issue new MMIO
         * from this callback. For a normal channel state we simply log
         * the condition and allow the PCI core to continue without
         * forcing a reset; the hardware will later be validated from
         * the ->slot_reset() (as1817_64o_fpga_verify_reset()) path
         * if a reset is performed.
         */
        dev_info(dev, "Correctable PCI error detected; no reset requested\n");
        return PCI_ERS_RESULT_CAN_RECOVER;

    case pci_channel_io_frozen:
        /*
         * I/O operations are blocked - more serious error
         * Could be PCIe link issues or power supply problems
         */
        dev_warn(dev, "PCI I/O channel frozen - attempting recovery\n");

        /*
         * Stop child device operations to prevent further errors.
         *
         * The ->error_detected() callback is invoked in task context
         * (see Documentation/PCI/pci-error-recovery.rst), so it is
         * legal to call into the driver core here even though
         * mfd_remove_devices() may sleep.  We must, however, avoid
         * issuing any new MMIO to the frozen device from this path.
         */
        dev_info(dev, "Temporarily disabling child devices\n");
        mfd_remove_devices(dev);

        return PCI_ERS_RESULT_NEED_RESET;

    case pci_channel_io_perm_failure:
        /*
         * Permanent failure - unusual for fixed FPGA
         * System may need reboot for recovery
         */
        dev_err(dev, "Permanent failure on fixed FPGA - system reboot may be required\n");
        return PCI_ERS_RESULT_DISCONNECT;

    default:
        dev_err(dev, "Unknown PCI error state: %d\n", state);
        return PCI_ERS_RESULT_DISCONNECT;
    }
}

/*
 * as1817_64o_fpga_verify_reset - Verify that PCIe reset was successful
 * @pdev: PCI device that was just reset by kernel
 *
 * Called after kernel completes PCIe Function Level Reset (FLR).
 * This function re-enables the device and performs a health check to
 * verify the FPGA is responsive. A retry loop is used because the FPGA
 * may require some time to re-initialize after a bus-level reset.
 *
 * Return: PCI error recovery result
 */
static pci_ers_result_t as1817_64o_fpga_verify_reset(struct pci_dev *pdev)
{
    struct device *dev = &pdev->dev;
    struct as1817_64o_fpga_mfd_data *mfd_data = pci_get_drvdata(pdev);
    int err;

    dev_info(dev, "Verifying PCIe reset was successful\n");

    /* Re-enable the PCI device after reset */
    err = pci_enable_device(pdev);
    if (err) {
        dev_err(dev, "Failed to re-enable device after reset: %d\n", err);
        return PCI_ERS_RESULT_DISCONNECT;
    }

    pci_set_master(pdev);

    /*
     * Verify FPGA functionality after reset
     */
    if (mfd_data && mfd_data->base_addr) {
        u8 major, minor;
        int retry_count = 0;
        const int max_retries = 5;

        /* Give FPGA time to complete reset sequence */
        if (msleep_interruptible(100)) {
            dev_warn(dev, "Reset wait interrupted\n");
        }

        /* Retry reading version registers */
        do {
            major = mfd_data->pdata.ops->read8(&mfd_data->pdata, FPGA_MAJOR_VER_REG);
            minor = mfd_data->pdata.ops->read8(&mfd_data->pdata, FPGA_MINOR_VER_REG);

            if (major != 0xFF || minor != 0xFF) {
                dev_info(dev, "FPGA reset successful: v%d.%d\n", major, minor);
                break;
            }

            retry_count++;

            /* Sleep between retries, but not after the last attempt */
            if (retry_count < max_retries) {
                if (msleep_interruptible(50)) {
                    dev_info(dev, "Retry wait interrupted, stopping verification\n");
                    retry_count = max_retries;
                    break;
                }
            }
        } while (retry_count < max_retries);

        if (retry_count >= max_retries) {
            dev_err(dev, "FPGA not responding after reset\n");
            return PCI_ERS_RESULT_DISCONNECT;
        }
    }

    dev_info(dev, "Reset verification completed successfully\n");
    return PCI_ERS_RESULT_RECOVERED;
}

/*
 * as1817_64o_fpga_resume_normal - Resume normal operation after recovery
 * @pdev: PCI device resuming operation
 *
 * This function is called after a successful recovery (e.g., after
 * slot_reset). It restores the driver to a fully operational state by
 * re-registering the MFD child devices.
 */
static void as1817_64o_fpga_resume_normal(struct pci_dev *pdev)
{
    struct device *dev = &pdev->dev;
    struct as1817_64o_fpga_mfd_data *mfd_data = pci_get_drvdata(pdev);
    int err;

    dev_info(dev, "Resuming normal operation on fixed FPGA\n");

    if (!mfd_data) {
        dev_err(dev, "No device data available for recovery\n");
        return;
    }

    err = as1817_64o_fpga_register_mfd_cells(dev, mfd_data);
    if (err) {
        dev_err(dev, "Failed to restore child devices after reset: %d\n", err);
        dev_err(dev, "System may be in an inconsistent state.\n");
    } else {
        dev_info(dev, "Fixed FPGA device recovery completed successfully\n");
    }
}

static struct pci_error_handlers as1817_64o_fpga_err_handler = {
    .error_detected = as1817_64o_fpga_error_detected,
    .slot_reset     = as1817_64o_fpga_verify_reset,
    .resume         = as1817_64o_fpga_resume_normal,
};

static struct pci_driver as1817_64o_fpga_pci_driver = {
    .name           = CORE_DRVNAME,
    /*
     *.id_table       = as1817_64o_fpga_pci_table
     *
     * In the ONL environment multiple platforms may share the same
     * FPGA PCI vendor/device IDs (e.g. 0x10ee:0x7021) while using different
     * per-platform FPGA drivers. If this driver exposed a normal id_table,
     * the PCI core would automatically bind it to any matching device and
     * could end up taking the FPGA away from another platform driver that
     * uses the same IDs.
     *
     * Setting .id_table = NULL disables automatic matching and requires
     * userspace to bind this driver explicitly only on the AS1817-64O
     * platform (for example via
     *   /sys/bus/pci/drivers/<driver_name>/new_id
     * or
     *   /sys/bus/pci/devices/0000:bb:dd.f/driver_override).
     *
     */
    .id_table       = NULL,
    .probe          = as1817_64o_fpga_pci_probe,
    .remove         = as1817_64o_fpga_pci_remove,
    .shutdown       = as1817_64o_fpga_pci_shutdown,
    .err_handler    = &as1817_64o_fpga_err_handler,
};

/*
 * Module Initialization and Cleanup
 */

static int __init as1817_64o_fpga_core_init(void)
{
    pr_info("Loading AS1817-64O FPGA Driver (Multi-Function Core Driver support)\n");
    return pci_register_driver(&as1817_64o_fpga_pci_driver);
}

static void __exit as1817_64o_fpga_core_exit(void)
{
    pr_info("Unloading AS1817-64O FPGA Driver\n");
    pci_unregister_driver(&as1817_64o_fpga_pci_driver);
}

module_init(as1817_64o_fpga_core_init);
module_exit(as1817_64o_fpga_core_exit);

MODULE_AUTHOR("Roger Ho <roger530_ho@accton.com>");
MODULE_DESCRIPTION("AS1817-64O FPGA Driver with Multi-Function Core Driver Support");
MODULE_LICENSE("GPL");

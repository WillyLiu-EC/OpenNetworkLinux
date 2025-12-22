/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AS1817-64O FPGA XCVR driver (MFD child)
 *
 * This driver is instantiated as a child device of the AS1817-64O FPGA
 * MFD core. It provides per-port management for the OSFP and SFP
 * transceiver modules that are connected behind the FPGA.
 *
 * The hardware groups ports into 8-bit FPGA registers, where each bit
 * represents a single logical port. The mapping between logical ports,
 * FPGA registers and I2C controller offsets is described by the static
 * configuration tables in this file.
 *
 * The driver:
 *  - Exposes per-port status and control through sysfs attributes:
 *      * module presence
 *      * module reset
 *      * low power mode
 *      * interrupt status
 *      * power-good status
 *      * RX loss-of-signal (SFP)
 *      * TX disable (SFP)
 *      * TX fault (SFP)
 *  - Creates one OpenCores I2C controller platform device per port so
 *    that higher-level transceiver/I2C drivers can access the modules
 *    over the FPGA-internal I2C buses.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/hwmon-sysfs.h>
#include <linux/string.h>
#include <linux/platform_data/i2c-ocores.h>

#include "x86-64-accton-as1817-64o-fpga.h"

#define PORT_NUM                       (66)  /* Total number of 64 x 800G OSFP + 2 x SFP28 */

#define OCORES_I2C_DRVNAME             "as1817-ocores-i2c"
#define I2C_BUS_CLK_400K

/*
 * XCVR (Transceiver) Register Map
 * These offsets are private to this driver and relative to the FPGA's base address.
 */

/* Module Presence Detection Registers (Active-low) */
#define XCVR_P08_P01_PRESENT_REG       (0x88)
#define XCVR_P16_P09_PRESENT_REG       (0x89)
#define XCVR_P24_P17_PRESENT_REG       (0x8A)
#define XCVR_P32_P25_PRESENT_REG       (0x8B)

#define XCVR_P40_P33_PRESENT_REG       (0x88)
#define XCVR_P48_P41_PRESENT_REG       (0x89)
#define XCVR_P56_P49_PRESENT_REG       (0x8A)
#define XCVR_P64_P57_PRESENT_REG       (0x8B)

/* Module Reset Control Registers (Active-low) */
#define XCVR_P08_P01_RESET_REG         (0x78)
#define XCVR_P16_P09_RESET_REG         (0x79)
#define XCVR_P24_P17_RESET_REG         (0x7A)
#define XCVR_P32_P25_RESET_REG         (0x7B)

#define XCVR_P40_P33_RESET_REG         (0x78)
#define XCVR_P48_P41_RESET_REG         (0x79)
#define XCVR_P56_P49_RESET_REG         (0x7A)
#define XCVR_P64_P57_RESET_REG         (0x7B)

/* Module Interrupt Status Registers (Active-high) */
#define XCVR_P08_P01_INT_REG           (0x80)
#define XCVR_P16_P09_INT_REG           (0x81)
#define XCVR_P24_P17_INT_REG           (0x82)
#define XCVR_P32_P25_INT_REG           (0x83)

#define XCVR_P40_P33_INT_REG           (0x80)
#define XCVR_P48_P41_INT_REG           (0x81)
#define XCVR_P56_P49_INT_REG           (0x82)
#define XCVR_P64_P57_INT_REG           (0x83)

/* Module Low Power Mode Control Registers (Active-low) */
#define XCVR_P08_P01_LPMODE_REG        (0x70)
#define XCVR_P16_P09_LPMODE_REG        (0x71)
#define XCVR_P24_P17_LPMODE_REG        (0x72)
#define XCVR_P32_P25_LPMODE_REG        (0x73)

#define XCVR_P40_P33_LPMODE_REG        (0x70)
#define XCVR_P48_P41_LPMODE_REG        (0x71)
#define XCVR_P56_P49_LPMODE_REG        (0x72)
#define XCVR_P64_P57_LPMODE_REG        (0x73)

/* Module Power Good Status Registers (Active-high) */
#define XCVR_P08_P01_PG_REG            (0x90)
#define XCVR_P16_P09_PG_REG            (0x91)
#define XCVR_P24_P17_PG_REG            (0x92)
#define XCVR_P32_P25_PG_REG            (0x93)

#define XCVR_P40_P33_PG_REG            (0x90)
#define XCVR_P48_P41_PG_REG            (0x91)
#define XCVR_P56_P49_PG_REG            (0x92)
#define XCVR_P64_P57_PG_REG            (0x93)

#define SFP_P65_P64_PRESENT_REG        (0x09) /* Active-low */
#define SFP_P65_P64_RX_LOS_REG         (0x08) /* Active-high */
#define SFP_P65_P64_TX_DISABLE_REG     (0x07) /* Active-high */
#define SFP_P65_P64_TX_FAULT_REG       (0x06) /* Active-high */

/*
 * Attribute Framework
 */

/**
 * enum attr_type - Defines the types of XCVR attributes supported.
 * @ATTR_PRESENT:     Module presence status.
 * @ATTR_RESET:       Controls the module reset line.
 * @ATTR_LPMODE:      Controls the module low power mode.
 * @ATTR_INT:         Module interrupt status.
 * @ATTR_PG:          Module power good status.
 * @ATTR_RX_LOS:      Module RX loss-of-signal status (SFP only).
 * @ATTR_TX_DISABLE:  Module TX disable control (SFP only).
 * @ATTR_TX_FAULT:    Module TX fault status (SFP only).
 * @ATTR_TYPE_MAX:    Boundary marker for the number of attribute types.
 */
enum attr_type {
    ATTR_PRESENT = 0,
    ATTR_RESET,
    ATTR_LPMODE,
    ATTR_INT,
    ATTR_PG,
    ATTR_RX_LOS,
    ATTR_TX_DISABLE,
    ATTR_TX_FAULT,
    ATTR_TYPE_MAX
};

/**
 * struct attr_type_info - Defines metadata for each attribute type.
 * @name_pattern:   Printf-style pattern for generating sysfs attribute names.
 * @name_prefix:    Static prefix used for parsing attribute names.
 * @mode:           File permissions for the sysfs attribute.
 * @writable:       True if the attribute is user-writable (e.g., reset, lpmode).
 * @description:    Human-readable description of the attribute.
 */
struct attr_type_info {
    const char *name_pattern;
    const char *name_prefix;
    umode_t mode;
    bool writable;
    const char *description;
};

/**
 * struct xcvr_attribute - Custom wrapper for device attribute.
 * @dev_attr: The standard device attribute structure.
 * @port_num: The 0-based port index associated with this attribute.
 * @type:     The attribute type enum.
 *
 * This structure allows us to store the port index and type directly with
 * the attribute, avoiding the need to parse the sysfs filename string
 * on every access.
 */
struct xcvr_attribute {
	struct device_attribute dev_attr;
	int port_num;
	enum attr_type type;
};

#define to_xcvr_attr(_attr) container_of(_attr, struct xcvr_attribute, dev_attr)


/**
 * struct port_reg_config - Maps a group of ports to its corresponding hardware
 *                          registers on a specific FPGA. The hardware design
 *                          groups ports into 8-bit registers where each bit
 *                          controls a single port.
 * @port_group_start: First port number in this group (0-indexed).
 * @port_group_size:  Number of consecutive ports in this group.
 * @fpga_id:          FPGA device identifier.
 * @bank_base:        Base offset of the register bank for this port group
 *                    (e.g. 0x2000 or 0x3000).
 * @reg_base:         Array of base register offsets for each attribute type.
 * @invert_mask:      Bitmask specifying inversion logic for each attribute type.
 *                    A '1' indicates the raw hardware bit is inverted so that
 *                    sysfs always reports a logical "1" for an asserted/true
 *                    condition from the OS point of view.
 */
struct port_reg_config {
    u8 port_group_start;
    u8 port_group_size;
    u16 bank_base;
    fpga_device_id_t fpga_id;
    u16 reg_base[ATTR_TYPE_MAX];
    u8 invert_mask[ATTR_TYPE_MAX];
};

/**
 * struct i2c_port_config - Defines the I2C controller mapping for a range of
 *                          ports. Each I2C controller provides an access bus
 *                          to a single XCVR module.
 * @port_start:         First port number in this I2C range (0-indexed).
 * @port_count:         Number of consecutive ports in this range.
 * @fpga_id:            FPGA identifier where the I2C controllers reside.
 * @base_offset:        Register offset for the first I2C controller in this range.
 * @offset_increment:   Address increment between consecutive I2C controllers.
 */
struct i2c_port_config {
    u8 port_start;
    u8 port_count;
    fpga_device_id_t fpga_id;
    u16 base_offset;
    u16 offset_increment;
};

/**
 * struct as1817_64o_xcvr_data - Holds all per-instance data for the driver,
 *                            including references to created platform devices
 *                            and dynamically allocated sysfs attributes.
  * @fpga_i2c:     Array of platform device pointers for the created I2C
 *                controllers, indexed by the logical port number.
 * @port_attrs:   Array of dynamically created device_attribute structures.
 * @attr_list:    NULL-terminated array of attribute pointers for sysfs group.
 * @total_attrs:  Total number of attributes created for this instance.
 * @dyn_group:    The attribute group for sysfs, shared between probe and remove.
 * @pdata:        Handle to the parent MFD core's platform data and access ops.
 */
struct as1817_64o_xcvr_data {
    struct platform_device *fpga_i2c[PORT_NUM];
    struct xcvr_attribute *port_attrs;
    struct attribute **attr_list;
    int total_attrs;
    struct attribute_group dyn_group;
    struct as1817_64o_fpga_platform_data *pdata;
    struct mutex xcvr_lock;
};

/*
 * Configuration Tables
 */

/**
 * attr_types - Defines the properties for each attribute type, such as its
 * sysfs name pattern, permissions, and basic description.
 */
static const struct attr_type_info attr_types[ATTR_TYPE_MAX] = {
    [ATTR_PRESENT] = {
        .name_pattern = "module_present_%d",
        .name_prefix = "module_present_",
        .mode = S_IRUGO,
        .writable = false,
        .description = "Module presence status (1 = module present, 0 = not present)"
    },
    [ATTR_RESET] = {
        .name_pattern = "module_reset_%d",
        .name_prefix = "module_reset_",
        .mode = S_IRUGO | S_IWUSR,
        .writable = true,
        .description = "Module reset control (1 = reset asserted, 0 = normal operation)"
    },
    [ATTR_LPMODE] = {
        .name_pattern = "module_lp_mode_%d",
        .name_prefix = "module_lp_mode_",
        .mode = S_IRUGO | S_IWUSR,
        .writable = true,
        .description = "Module low power mode control (1 = low power, 0 = normal)"
    },
    [ATTR_INT] = {
        .name_pattern = "module_interrupt_%d",
        .name_prefix = "module_interrupt_",
        .mode = S_IRUGO,
        .writable = false,
        .description = "Module interrupt status (1 = interrupt asserted, 0 = inactive)"
    },
    [ATTR_PG] = {
        .name_pattern = "module_power_good_%d",
        .name_prefix = "module_power_good_",
        .mode = S_IRUGO,
        .writable = false,
        .description = "Module power good status (1 = power good, 0 = fault)"
    },
    [ATTR_RX_LOS] = {
        .name_pattern = "module_rx_los_%d",
        .name_prefix = "module_rx_los_",
        .mode = S_IRUGO,
        .writable = false,
        .description = "Module RX LOS"
    },
    [ATTR_TX_DISABLE] = {
        .name_pattern = "module_tx_disable_%d",
        .name_prefix = "module_tx_disable_",
        .mode = S_IRUGO | S_IWUSR,
        .writable = true,
        .description = "Module TX disable (1 = disabled, 0 = enabled)"
    },
    [ATTR_TX_FAULT] = {
        .name_pattern = "module_tx_fault_%d",
        .name_prefix = "module_tx_fault_",
        .mode = S_IRUGO,
        .writable = false,
        .description = "Module TX fault"
    }
};

/**
 * port_configs - Port-to-register mapping configuration table.
 *
 * This table maps port ranges to their corresponding FPGA registers.
 * Each entry defines a group of 8 consecutive ports and their register layout.
 * The hardware groups ports in 8-bit registers where each bit represents one port.
 */
static const struct port_reg_config port_configs[] = {
    {
        .port_group_start = 0,
        .port_group_size = 8,
        .bank_base = 0x2000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P08_P01_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P08_P01_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P08_P01_LPMODE_REG,
            [ATTR_INT]     = XCVR_P08_P01_INT_REG,
            [ATTR_PG]      = XCVR_P08_P01_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* Ports 8-15 */
    {
        .port_group_start = 8,
        .port_group_size = 8,
        .bank_base = 0x2000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P16_P09_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P16_P09_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P16_P09_LPMODE_REG,
            [ATTR_INT]     = XCVR_P16_P09_INT_REG,
            [ATTR_PG]      = XCVR_P16_P09_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* Ports 16-23 */
    {
        .port_group_start = 16,
        .port_group_size = 8,
        .bank_base = 0x2000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P24_P17_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P24_P17_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P24_P17_LPMODE_REG,
            [ATTR_INT]     = XCVR_P24_P17_INT_REG,
            [ATTR_PG]      = XCVR_P24_P17_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* Ports 24-31 */
    {
        .port_group_start = 24,
        .port_group_size = 8,
        .bank_base = 0x2000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P32_P25_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P32_P25_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P32_P25_LPMODE_REG,
            [ATTR_INT]     = XCVR_P32_P25_INT_REG,
            [ATTR_PG]      = XCVR_P32_P25_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* Ports 32-39 */
    {
        .port_group_start = 32,
        .port_group_size = 8,
        .bank_base = 0x3000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P40_P33_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P40_P33_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P40_P33_LPMODE_REG,
            [ATTR_INT]     = XCVR_P40_P33_INT_REG,
            [ATTR_PG]      = XCVR_P40_P33_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* Ports 40-47 */
    {
        .port_group_start = 40,
        .port_group_size = 8,
        .bank_base = 0x3000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P48_P41_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P48_P41_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P48_P41_LPMODE_REG,
            [ATTR_INT]     = XCVR_P48_P41_INT_REG,
            [ATTR_PG]      = XCVR_P48_P41_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* Ports 48-55 */
    {
        .port_group_start = 48,
        .port_group_size = 8,
        .bank_base = 0x3000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P56_P49_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P56_P49_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P56_P49_LPMODE_REG,
            [ATTR_INT]     = XCVR_P56_P49_INT_REG,
            [ATTR_PG]      = XCVR_P56_P49_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* Ports 56-63 */
    {
        .port_group_start = 56,
        .port_group_size = 8,
        .bank_base = 0x3000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = XCVR_P64_P57_PRESENT_REG,
            [ATTR_RESET]   = XCVR_P64_P57_RESET_REG,
            [ATTR_LPMODE]  = XCVR_P64_P57_LPMODE_REG,
            [ATTR_INT]     = XCVR_P64_P57_INT_REG,
            [ATTR_PG]      = XCVR_P64_P57_PG_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1,
            [ATTR_RESET]   = 1,
            [ATTR_LPMODE]  = 1,
            [ATTR_INT]     = 0,
            [ATTR_PG]      = 0
        }
    },
    /* SFP Ports 64-65 (2 x SFP28) */
    {
        .port_group_start = 64,
        .port_group_size = 2,
        .bank_base = 0x2000,
        .fpga_id = FPGA_CB,
        .reg_base = {
            [ATTR_PRESENT] = SFP_P65_P64_PRESENT_REG,
            [ATTR_RX_LOS]  = SFP_P65_P64_RX_LOS_REG,
            [ATTR_TX_DISABLE]  = SFP_P65_P64_TX_DISABLE_REG,
            [ATTR_TX_FAULT]    = SFP_P65_P64_TX_FAULT_REG
        },
        .invert_mask = {
            [ATTR_PRESENT] = 1, 
            [ATTR_RX_LOS]  = 0,
            [ATTR_TX_DISABLE] = 0,
            [ATTR_TX_FAULT]   = 0
        }
    }
};

/**
 * i2c_configs - I2C controller configuration table.
 *
 * This table defines the mapping between ports and their I2C controllers.
 * Each I2C controller is used for communication with the XCVR module on that
 * port. The controllers are distributed across two FPGAs with specific address
 * ranges.
 */
static const struct i2c_port_config i2c_configs[] = {
    /* Ports 0-15, I2C controllers at 0x2100-0x22E0 */
    {0,  8, FPGA_CB, 0x2100, 0x20},  /* Ports 0-7:   0x2100, 0x2120, ..., 0x20E0 */
    {8,  8, FPGA_CB, 0x2200, 0x20},  /* Ports 8-15:  0x2200, 0x2220, ..., 0x22E0 */

    /* Ports 16-31, I2C controllers at 0x2300-0x24E0 */
    {16, 8, FPGA_CB, 0x2300, 0x20},  /* Ports 16-23: 0x2300, 0x2320, ..., 0x23E0 */
    {24, 8, FPGA_CB, 0x2400, 0x20},  /* Ports 24-31: 0x2400, 0x2420, ..., 0x24E0 */

    /* Ports 32-47, I2C controllers at 0x3100-0x32E0 */
    {32, 8, FPGA_CB, 0x3100, 0x20},  /* Ports 32-39: 0x3100, 0x3120, ..., 0x31E0 */
    {40, 8, FPGA_CB, 0x3200, 0x20},  /* Ports 40-47: 0x3200, 0x3220, ..., 0x32E0 */

    /* Ports 48-63, I2C controllers at 0x3300-0x34E0 */
    {48, 8, FPGA_CB, 0x3300, 0x20},  /* Ports 48-55: 0x3300, 0x3320, ..., 0x33E0 */
    {56, 8, FPGA_CB, 0x3400, 0x20},  /* Ports 56-63: 0x3400, 0x3420, ..., 0x34E0 */

    {64, 2, FPGA_CB, 0x2500, 0x20}  /* SFP Ports 64-65: 0x2500, 0x2520 */
};

/*
 * Framework Core Functions
 */

/**
 * find_port_config() - Finds the register configuration for a given port number
 * by searching the static port_configs table.
 * @port_num: The port number to look up (0-indexed).
 *
 * Return: A pointer to the matching port_reg_config structure, or NULL if
 * no configuration is found for the given port number.
 */
static const struct port_reg_config *find_port_config(int port_num)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(port_configs); i++) {
        const struct port_reg_config *config = &port_configs[i];
        if (port_num >= config->port_group_start &&
            port_num < (config->port_group_start + config->port_group_size)) {
            return config;
        }
    }
    return NULL;
}

/**
 * generate_attr_name() - Creates an attribute name string for a given port and type.
 * @dev:       Device pointer for managed memory allocation.
 * @port_num:  The port number (0-indexed).
 * @attr_type: The attribute type enum.
 *
 * Dynamically generates a sysfs attribute name using the predefined pattern.
 * Memory is allocated using devm_kasprintf(), a resource-managed function that
 * ensures the allocated memory is automatically freed when the driver detaches.
 *
 * Return: A pointer to the allocated attribute name string, or NULL on failure.
 */
static char *generate_attr_name(struct device *dev, int port_num, enum attr_type attr_type)
{
    if (attr_type >= ATTR_TYPE_MAX) {
        return NULL;
    }

    /* Generate name using 1-based port numbering for user interface */
    return devm_kasprintf(dev, GFP_KERNEL, attr_types[attr_type].name_pattern, port_num + 1);
}

/*
 * Attribute Access Functions
 */

/**
 * status_read() - Sysfs read callback for XCVR port attributes.
 * @dev:  Device pointer.
 * @attr: Device attribute being read.
 * @buf:  Buffer to store the formatted output.
 *
 * Reads the hardware register corresponding to the requested attribute, extracts
 * the relevant bit for the specific port, applies any configured logic inversion,
 * and formats the result as a string for userspace. All values are reported in
 * logical form where '1' represents an asserted/true condition from the OS
 * point of view, regardless of the underlying hardware polarity.
 */
static ssize_t status_read(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct as1817_64o_xcvr_data *xcvr_data = dev_get_drvdata(dev);
    struct xcvr_attribute *xcvr_attr = to_xcvr_attr(attr);
    int port_num;
    enum attr_type attr_type;
    const struct port_reg_config *config;
    u16 reg_base, reg_offset;
    u8 reg_val, bit_shift, bit_val;

    if (!xcvr_data || !xcvr_data->pdata)
        return -ENODEV;

    /* Retrieve port number and type directly from the container struct */
    port_num = xcvr_attr->port_num;
    attr_type = xcvr_attr->type;

    /* Find the register configuration for this port */
    config = find_port_config(port_num);
    if (!config) {
        dev_err(dev, "No configuration found for port %d\n", port_num);
        return -EINVAL;
    }

    /* Check whether this attribute is implemented for the port group */
    reg_base = config->reg_base[attr_type];
    if (!reg_base) {
        dev_err(dev,
                "Attribute %s not implemented on port %d\n",
                attr->attr.name, port_num + 1);
        return -EOPNOTSUPP;
    }

    /* Calculate register offset and bit position within the register */
    reg_offset = config->bank_base + reg_base;
    bit_shift = port_num - config->port_group_start;

    /* Read the register value from the appropriate FPGA using ops */
    reg_val = xcvr_data->pdata->ops->read8(xcvr_data->pdata, reg_offset);

    dev_dbg(dev, "attr_name=%s reg_offset=0x%02x, reg_val=0x%02x bit_shift=%u", 
            attr->attr.name, reg_offset, reg_val, bit_shift);

    /* Extract the bit corresponding to this port */
    bit_val = (reg_val >> bit_shift) & 0x01;

    /* Apply logic inversion if configured for this attribute type */
    if (config->invert_mask[attr_type]) {
        bit_val = !bit_val;
    }

    return scnprintf(buf, PAGE_SIZE, "%u\n", bit_val);
}

/**
 * status_write() - Sysfs write callback for XCVR port attributes.
 * @dev:   Device pointer.
 * @attr:  Device attribute being written.
 * @buf:   Buffer containing the input value from userspace.
 * @count: Number of bytes in the input buffer.
 *
 * Parses the input value, applies any configured logic inversion, and performs
 * a read-modify-write operation on the hardware register to update the specific
 * bit corresponding to the port. Userspace always writes logical values where
 * '1' means "asserted/true" for the given attribute, independent of the
 * underlying hardware polarity.
 */
static ssize_t status_write(struct device *dev, struct device_attribute *attr,
                            const char *buf, size_t count)
{
    struct as1817_64o_xcvr_data *xcvr_data = dev_get_drvdata(dev);
    struct xcvr_attribute *xcvr_attr = to_xcvr_attr(attr);
    int port_num;
    enum attr_type attr_type;
    const struct port_reg_config *config;
    u16 reg_base, reg_offset;
    u8 input, reg_val, bit_mask, bit_shift;
    int status;

    if (!xcvr_data || !xcvr_data->pdata)
        return -ENODEV;

    /* Parse and validate the input value */
    status = kstrtou8(buf, 10, &input);
    if (status) {
        return status;
    }
    if (input > 1) {
        dev_err(dev, "Invalid value for %s: %u (expected 0 or 1)\n",
                attr->attr.name, input);
        return -EINVAL;
    }

    /* Retrieve port number and type directly from the container struct */
    port_num = xcvr_attr->port_num;
    attr_type = xcvr_attr->type;

    /* Verify that this attribute type supports write operations */
    if (!attr_types[attr_type].writable) {
        dev_err(dev, "Attribute %s is read-only\n", attr->attr.name);
        return -EACCES;
    }

    /* Find the register configuration for this port */
    config = find_port_config(port_num);
    if (!config) {
        dev_err(dev, "No configuration found for port %d\n", port_num);
        return -EINVAL;
    }

    /* Check whether this attribute is implemented for the port group */
    reg_base = config->reg_base[attr_type];
    if (!reg_base) {
        dev_err(dev,
                "Attribute %s not implemented on port %d\n",
                attr->attr.name, port_num + 1);
        return -EOPNOTSUPP;
    }

    /* Calculate register offset and bit position */
    reg_offset = config->bank_base + reg_base;
    bit_shift = port_num - config->port_group_start;
    bit_mask = 0x01 << bit_shift;

    /* Apply logic inversion if configured for this attribute type */
    if (config->invert_mask[attr_type]) {
        input = !input;
    }

    mutex_lock(&xcvr_data->xcvr_lock);
    /*
     * Perform a read-modify-write operation on the shared 8-bit register.
     * The xcvr_lock ensures that concurrent writers from this driver do not
     * lose each other's bit updates, while the parent MFD core serializes
     * the underlying MMIO accesses inside the ops->read8/write8 helpers.
     */
    reg_val = xcvr_data->pdata->ops->read8(xcvr_data->pdata, reg_offset);
    if (input) {
        reg_val |= bit_mask;   /* Set the bit */
    } else {
        reg_val &= ~bit_mask;  /* Clear the bit */
    }
    xcvr_data->pdata->ops->write8(xcvr_data->pdata, reg_offset, reg_val);
    mutex_unlock(&xcvr_data->xcvr_lock);

    return count;
}

/**
 * xcvr_bitmap_show() - Generic bitmap show function for aggregated attributes.
 * @dev:       The device being read.
 * @attr_type: The attribute type to aggregate.
 * @buf:       The buffer to write the result into.
 *
 * Aggregates the status of all managed ports for a given attribute type
 * into a compact hexadecimal bitmap.
 *
 * Bitmap Semantics:
 * Bit 'i' corresponds to the port at index 'i' in the driver's port
 * configuration table. A value of '1' indicates the condition is asserted,
 * after applying the per-port inversion logic. The output prints bytes in
 * ascending order (byte 0 first), each as a two-digit hex value with no
 * prefix, followed by a newline (e.g., "3f00\n").
 *
 * Return: The number of bytes written to the buffer, or a negative errno on
 * failure.
 */
static ssize_t xcvr_bitmap_show(struct device *dev, enum attr_type attr_type,
                                char *buf)
{
    struct as1817_64o_xcvr_data *xcvr_data = dev_get_drvdata(dev);
    int i, port, byte_index, bit_index;
    int bitmap_bytes = (PORT_NUM + 7) / 8;
    u8 bitmap[(PORT_NUM + 7) / 8];
    ssize_t len = 0;

    if (!xcvr_data || !xcvr_data->pdata) {
        return -ENODEV;
    }

    memset(bitmap, 0, sizeof(bitmap));

    /*
     * Iterate over port groups instead of individual ports. Each group
     * corresponds to a single 8-bit register where each bit controls one
     * port in the group.
     */
    for (i = 0; i < ARRAY_SIZE(port_configs); i++) {
        const struct port_reg_config *config = &port_configs[i];
        u16 reg_offset;
        u8 reg_val;
        int p;

        /* Skip port groups that belong to a different FPGA instance */
        if (config->fpga_id != xcvr_data->pdata->fpga_id) {
            continue;
        }

        if (!config->reg_base[attr_type]) {
            /* Attribute type not implemented for this port group */
            continue;
        }

        /* Read the register once for the entire port group */
        reg_offset = config->bank_base + config->reg_base[attr_type];
        reg_val = xcvr_data->pdata->ops->read8(xcvr_data->pdata, reg_offset);

        /*
         * Apply inversion so that a logical "1" in the aggregated bitmap
         * always means "asserted" from the user's point of view.
         */
        if (config->invert_mask[attr_type]) {
            reg_val = (u8)~reg_val;
        }

        /* Map the bits in this register to the global port bitmap */
        for (p = 0; p < config->port_group_size; p++) {
            port = config->port_group_start + p;

            if (port >= PORT_NUM) {
                continue;
            }

            if ((reg_val >> p) & 0x01) {
                byte_index = port / 8;
                bit_index = port % 8;
                if (byte_index < bitmap_bytes) {
                    bitmap[byte_index] |= (1U << bit_index);
                }
            }
        }
    }

    /* Print bytes in ascending order, two hex digits per byte */
    for (byte_index = 0; byte_index < bitmap_bytes; byte_index++) {
        len += scnprintf((buf + len), (PAGE_SIZE - len), "%02x", bitmap[byte_index]);
    }
    len += scnprintf((buf + len), (PAGE_SIZE - len), "\n");

    return len;
}

/**
 * module_present_all_show() - Show presence for all modules as a hex bitmap.
 * @dev:   The device being read.
 * @attr:  The specific device_attribute being read.
 * @buf:   The buffer to write the result into.
 *
 * Aggregates the presence status of all managed ports into a compact
 * hexadecimal bitmap.
 *
 * Return: The number of bytes written to the buffer, or a negative errno.
 */
static ssize_t module_present_all_show(struct device *dev,
                                       struct device_attribute *attr,
                                       char *buf)
{
    return xcvr_bitmap_show(dev, ATTR_PRESENT, buf);
}

/**
 * module_rx_los_all_show() - Show RX LOS status for all modules as a hex bitmap.
 * @dev:   The device being read.
 * @attr:  The specific device_attribute being read.
 * @buf:   The buffer to write the result into.
 *
 * Aggregates the RX Loss of Signal (LOS) status of all managed ports into a
 * compact hexadecimal bitmap. Only ports that implement ATTR_RX_LOS (SFP ports
 * 64-65) will contribute non-zero bits; for all other ports the bitmap
 * remains 0.
 *
 * Return: The number of bytes written to the buffer, or a negative errno.
 */
static ssize_t module_rx_los_all_show(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
    return xcvr_bitmap_show(dev, ATTR_RX_LOS, buf);
}

/* Read-only aggregated sysfs attributes (hex bitmap) */
static DEVICE_ATTR_RO(module_present_all);
static DEVICE_ATTR_RO(module_rx_los_all);

/*
 * Dynamic Attribute Creation Framework
 */


/**
 * create_port_attributes() - Allocates and initializes all sysfs attributes
 *                           for the ports managed by this specific FPGA instance.
 *
 * This function implements the core of the data-driven sysfs creation. It iterates
 * through the `port_configs` table, creating attributes only for the port groups
 * associated with the current FPGA instance (determined by `fpga_id`).
 * @dev:       Device pointer for managed memory allocation.
 * @xcvr_data: Driver private data structure.
 *
 * Return: 0 on success, or -ENOMEM on memory allocation failure.
 */
static int create_port_attributes(struct device *dev, struct as1817_64o_xcvr_data *xcvr_data)
{
    int i, port, attr_type, attr_idx = 0;
    int total_attrs_for_this_fpga = 0;
    int extra_attrs = 2; /* module_present_all and module_rx_los_all */
    char *attr_name;

    /*
     * First, count how many attributes this FPGA instance is responsible for.
     * Only attribute types with a non-zero base register are counted so that
     * we do not over-allocate sysfs attribute structures.
     */
    for (i = 0; i < ARRAY_SIZE(port_configs); i++) {
        if (port_configs[i].fpga_id != xcvr_data->pdata->fpga_id) {
            continue;
        }

        for (attr_type = 0; attr_type < ATTR_TYPE_MAX; attr_type++) {
            if (port_configs[i].reg_base[attr_type]) {
                total_attrs_for_this_fpga += port_configs[i].port_group_size;
            }
        }
    }

    /*
     * If there are no per-port attributes for this FPGA instance, we still
     * create the aggregated bitmap attributes so that userspace has a
     * consistent view of the overall module state.
     */
    if (total_attrs_for_this_fpga == 0) {
        xcvr_data->attr_list = devm_kzalloc(dev,
                                            sizeof(struct attribute *) *
                                            (extra_attrs + 1),
                                            GFP_KERNEL);
        if (!xcvr_data->attr_list) {
            return -ENOMEM;
        }

        attr_idx = 0;
        xcvr_data->attr_list[attr_idx++] = &dev_attr_module_present_all.attr;
        xcvr_data->attr_list[attr_idx++] = &dev_attr_module_rx_los_all.attr;
        xcvr_data->attr_list[attr_idx] = NULL;
        xcvr_data->total_attrs = attr_idx;

        return 0;
    }

    /* Allocate memory for the device attribute structures */
    xcvr_data->port_attrs = devm_kzalloc(dev,
        sizeof(struct xcvr_attribute) * total_attrs_for_this_fpga, GFP_KERNEL);
    if (!xcvr_data->port_attrs) {
        return -ENOMEM;
    }

    /* Allocate memory for the attribute pointer array (plus NULL terminator) */
    xcvr_data->attr_list = devm_kzalloc(dev,
        sizeof(struct attribute *) * (total_attrs_for_this_fpga + extra_attrs + 1), GFP_KERNEL);
    if (!xcvr_data->attr_list) {
        return -ENOMEM;
    }

    /* Create attributes only for ports managed by this FPGA instance */
    for (i = 0; i < ARRAY_SIZE(port_configs); i++) {
        const struct port_reg_config *config = &port_configs[i];
        if (config->fpga_id != xcvr_data->pdata->fpga_id)
            continue;

        for (port = config->port_group_start; port < (config->port_group_start + config->port_group_size); port++) {
            for (attr_type = 0; attr_type < ATTR_TYPE_MAX; attr_type++) {
                if (!config->reg_base[attr_type]) {
                    continue;
                }

                /* Generate the sysfs attribute name */
                attr_name = generate_attr_name(dev, port, attr_type);
                if (!attr_name) {
                    dev_err(dev, "Failed to generate name for port %d, type %d\n",
                            port, attr_type);
                    return -ENOMEM;
                }
                /* Initialize the custom wrapper fields */
                xcvr_data->port_attrs[attr_idx].port_num = port;
                xcvr_data->port_attrs[attr_idx].type = attr_type;

                /* Initialize the device attribute structure */
                xcvr_data->port_attrs[attr_idx].dev_attr.attr.name = attr_name;
                xcvr_data->port_attrs[attr_idx].dev_attr.attr.mode = attr_types[attr_type].mode;
                xcvr_data->port_attrs[attr_idx].dev_attr.show = status_read;
                xcvr_data->port_attrs[attr_idx].dev_attr.store = attr_types[attr_type].writable ?
                                                      status_write : NULL;

                /* Add to the attribute list for sysfs group creation */
                xcvr_data->attr_list[attr_idx] = &xcvr_data->port_attrs[attr_idx].dev_attr.attr;
                attr_idx++;
            }
        }
    }

    /* Append aggregated bitmap attributes for this FPGA instance */
    xcvr_data->attr_list[attr_idx++] = &dev_attr_module_present_all.attr;
    xcvr_data->attr_list[attr_idx++] = &dev_attr_module_rx_los_all.attr;

    /* NULL-terminate the attribute list */
    xcvr_data->attr_list[attr_idx] = NULL;
    xcvr_data->total_attrs = attr_idx;

    return 0;
}

/*
 * I2C Device Creation Framework
 */

/**
 * as1817_64o_platform_data - Platform data for ocores-i2c controllers.
 *
 * Provides configuration data for the OpenCores I2C controller instances
 * created for each XCVR port. The configuration is optimized for the FPGA's
 * internal clock frequency (24MHz) and desired I2C bus speed (100KHz).
 */
static const struct ocores_i2c_platform_data as1817_64o_platform_data = {
    .reg_io_width = 1,      /* 8-bit register access */
    .reg_shift = 2,         /* Registers are 4-byte aligned */
#ifdef I2C_BUS_CLK_400K
    .clock_khz = 24000,     /* FPGA clock frequency in KHz */
    .bus_khz = 400,         /* Desired I2C bus frequency in KHz */
#elif defined(I2C_BUS_CLK_100K)
    .clock_khz = 25500,     /* FPGA clock frequency in KHz */
    .bus_khz = 100,         /* Desired I2C bus frequency in KHz */
#else
    /* Default to 100KHz for safety */
    .clock_khz = 25500,
    .bus_khz = 100,
#endif
};

/**
 * create_i2c_device() - Creates and registers an ocores-i2c platform device.
 * @dev:      The parent device for logging and context.
 * @port_id:  Logical port ID, used for the I2C device's ID to ensure uniqueness.
 * @bar_base: Physical base address of the FPGA's memory region.
 * @offset:   Register offset of the I2C controller within the memory region.
 *
 * Creates a platform device for the OpenCores I2C controller that will handle
 * communication with the XCVR module on the specified port.
 *
 * Return: A pointer to the created platform_device on success, or NULL on failure.
 */
static struct platform_device *create_i2c_device(struct device *dev,
                                                 unsigned int port_id,
                                                 resource_size_t bar_base,
                                                 unsigned int offset)
{
    struct resource res = DEFINE_RES_MEM(bar_base + offset, 0x20);
    struct platform_device *pdev;
    int err;

    /* Allocate the platform device structure */
    pdev = platform_device_alloc(OCORES_I2C_DRVNAME, port_id);
    if (!pdev) {
        err = -ENOMEM;
        dev_err(dev, "Port%u device allocation failed (%d)\n", (port_id & 0xFF), err);
        goto exit;
    }

    /* Add memory resource for the I2C controller registers */
    err = platform_device_add_resources(pdev, &res, 1);
    if (err) {
        dev_err(dev, "Port%u device resource addition failed (%d)\n", (port_id & 0xFF), err);
        goto exit_device_put;
    }

    /* Add platform-specific configuration data */
    err = platform_device_add_data(pdev, &as1817_64o_platform_data,
                                  sizeof(as1817_64o_platform_data));
    if (err) {
        dev_err(dev, "Port%u platform data allocation failed (%d)\n", (port_id & 0xFF), err);
        goto exit_device_put;
    }

    /* Register the platform device with the kernel */
    err = platform_device_add(pdev);
    if (err) {
        dev_err(dev, "Port%u device registration failed (%d)\n", (port_id & 0xFF), err);
        goto exit_device_put;
    }

    return pdev;

exit_device_put:
    platform_device_put(pdev);
exit:
    return NULL;
}

/**
 * create_fpga_i2c_devices() - Creates I2C platform devices for this FPGA instance.
 * @dev:       Device pointer for logging.
 * @xcvr_data: Driver private data to store I2C device references.
 *
 * Iterates through I2C configurations and creates the I2C platform devices
 * for the ports belonging to this specific FPGA.
 *
 * Return: 0 on success, or a negative errno if any device creation fails.
 */
static int create_fpga_i2c_devices(struct device *dev, struct as1817_64o_xcvr_data *xcvr_data)
{
    int i, port, i2c_offset;

    /* Iterate through I2C configs and only create devices for this FPGA instance */
    for (i = 0; i < ARRAY_SIZE(i2c_configs); i++) {
        const struct i2c_port_config *config = &i2c_configs[i];

        if (config->fpga_id != xcvr_data->pdata->fpga_id)
            continue;

        for (port = config->port_start; port < (config->port_start + config->port_count); port++) {
            i2c_offset = config->base_offset + (port - config->port_start) * config->offset_increment;

            xcvr_data->fpga_i2c[port] = create_i2c_device(dev, port, xcvr_data->pdata->phys_addr, i2c_offset);
            if (IS_ERR_OR_NULL(xcvr_data->fpga_i2c[port])) {
                dev_err(dev, "Failed to create I2C device for port %d\n", port);
                goto cleanup_i2c;
            }
        }
    }
    return 0;

cleanup_i2c:
    /* Cleanup already created I2C devices */
    for (i = 0; i < PORT_NUM; i++) {
        if (xcvr_data->fpga_i2c[i]) {
            platform_device_unregister(xcvr_data->fpga_i2c[i]);
            xcvr_data->fpga_i2c[i] = NULL;
        }
    }
    return -ENODEV;
}

/*
 * Platform Driver Implementation
 */

/**
 * as1817_64o_xcvr_probe() - Platform driver probe function.
 * @pdev: Platform device being probed.
 *
 * Called by the MFD core for each FPGA instance. This function initializes all
 * resources (sysfs attributes, I2C devices) for the set of ports managed by
 * this specific FPGA.
 *
 * Return: 0 on successful initialization, or a negative errno on failure.
 */
static int as1817_64o_xcvr_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct as1817_64o_fpga_platform_data *pdata = dev_get_platdata(dev);
    struct as1817_64o_xcvr_data *xcvr_data;
    int i, status = 0;

    if (!pdata) {
        dev_err(dev, "Missing platform data\n");
        return -ENODEV;
    }

    /* Allocate driver private data structure */
    xcvr_data = devm_kzalloc(dev, sizeof(struct as1817_64o_xcvr_data), GFP_KERNEL);
    if (!xcvr_data)
        return -ENOMEM;

    mutex_init(&xcvr_data->xcvr_lock);
    xcvr_data->pdata = pdata;
    platform_set_drvdata(pdev, xcvr_data);

    /* Create I2C platform devices for ports on this FPGA */
    status = create_fpga_i2c_devices(dev, xcvr_data);
    if (status) {
        dev_err(dev, "Failed to create I2C devices: %d\n", status);
        goto cleanup_devices;
    }

    /* Dynamically create sysfs attributes for ports on this FPGA */
    status = create_port_attributes(dev, xcvr_data);
    if (status) {
        dev_err(dev, "Failed to create sysfs attributes: %d\n", status);
        goto cleanup_devices;
    }

    /* Create and register the sysfs attribute group if there are attributes */
    if (xcvr_data->total_attrs > 0) {
        xcvr_data->dyn_group.attrs = xcvr_data->attr_list;
        status = sysfs_create_group(&pdev->dev.kobj, &xcvr_data->dyn_group);
        if (status) {
            dev_err(dev, "Failed to create sysfs group: %d\n", status);
            goto cleanup_devices;
        }
    }

    dev_info(dev, "XCVR driver initialized successfully for FPGA_CB\n");

    return 0;

cleanup_devices:
    /* Error path: unregister any I2C devices that were successfully created */
    for (i = 0; i < PORT_NUM; i++) {
        if (xcvr_data->fpga_i2c[i]) {
            platform_device_unregister(xcvr_data->fpga_i2c[i]);
        }
    }
    mutex_destroy(&xcvr_data->xcvr_lock);
    return status;
}

/**
 * as1817_64o_xcvr_remove() - Platform driver remove function.
 * @pdev: Platform device being removed.
 *
 * Unregisters I2C devices and removes the sysfs group for this FPGA instance.
 * All device-managed memory is freed automatically by the driver core.
 *
 * Return: Always returns 0.
 */
static int as1817_64o_xcvr_remove(struct platform_device *pdev)
{
    struct as1817_64o_xcvr_data *xcvr_data = platform_get_drvdata(pdev);
    int i;

    dev_info(&pdev->dev, "Removing XCVR driver for FPGA_CB\n");

    /* Remove the sysfs attribute group */
    if (xcvr_data->total_attrs > 0) {
        sysfs_remove_group(&pdev->dev.kobj, &xcvr_data->dyn_group);
    }

    /* Unregister all I2C platform devices created by this instance */
    for (i = 0; i < PORT_NUM; i++) {
        if (xcvr_data->fpga_i2c[i]) {
            platform_device_unregister(xcvr_data->fpga_i2c[i]);
        }
    }

    mutex_destroy(&xcvr_data->xcvr_lock);

    return 0;
}

static const struct platform_device_id xcvr_id_table[] = {
	{ .name = XCVR_DRVNAME },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, xcvr_id_table);

/**
 * xcvr_driver - The platform driver definition for the TTV-OWS XCVR
 * MFD child device.
 */
static struct platform_driver xcvr_driver = {
    .probe      = as1817_64o_xcvr_probe,
    .remove     = as1817_64o_xcvr_remove,
    .driver     = {
        .name  = XCVR_DRVNAME,
    },
    .id_table   = xcvr_id_table,
};

/*
 * Module Initialization and Cleanup
 */

/**
 * as1817_64o_xcvr_init() - Module initialization function.
 *
 * Registers the XCVR platform driver. The MFD core will subsequently call the
 * probe function for each matching MFD child device it creates.
 *
 * Return: 0 on successful registration, or a negative errno on failure.
 */
static int __init as1817_64o_xcvr_init(void)
{
    int status;
    pr_info("AS1817-64O XCVR Driver Module Loading\n");

    status = platform_driver_register(&xcvr_driver);
    if (status < 0)
        pr_err("AS1817-64O XCVR Driver: Failed to register platform driver: %d\n", status);

    return status;
}

/**
 * as1817_64o_xcvr_exit() - Module cleanup function.
 *
 * Unregisters the XCVR platform driver.
 */
static void __exit as1817_64o_xcvr_exit(void)
{
    pr_info("AS1817-64O XCVR Driver Module Unloading\n");
    platform_driver_unregister(&xcvr_driver);
}

module_init(as1817_64o_xcvr_init);
module_exit(as1817_64o_xcvr_exit);

MODULE_AUTHOR("Roger Ho <roger530_ho@accton.com>");
MODULE_DESCRIPTION("AS1817-64O FPGA XCVR Driver (MFD Child)");
MODULE_LICENSE("GPL");
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Common Header for AS1817-64O FPGA Driver Subsystem
 *
 * Copyright (C) 2025 Accton Technology Corporation
 *
 * This header file provides shared definitions, data structures, and function
 * declarations for the AS1817-64O FPGA driver subsystem. It defines the interface
 * between the FPGA core driver and other FPGA-related drivers, enabling
 * coordination across multiple FPGA devices in the platform.
 *
 */

#ifndef _AS1817_64O_FPGA_H_
#define _AS1817_64O_FPGA_H_
#include <linux/mutex.h>

#define CORE_DRVNAME                        "as1817_64o_fpga"
#define XCVR_DRVNAME                        "as1817_64o_xcvr"


/*
 * Data Structure Definitions
 */

/**
 * enum fpga_device_id - Logical FPGA device identifiers
 * @FPGA_CB: Single FPGA instance on AS1817-64O
 * @FPGA_MAX: Maximum number of FPGA devices (boundary marker)
 *
 * These logical identifiers are used throughout the driver subsystem to
 * tag resources that belong to a given FPGA instance. The current hardware
 * only uses a single FPGA (FPGA_CB), but the enum leaves room for future
 * expansion if additional devices are added.
 */

typedef enum {
    FPGA_CB = 0,
    FPGA_MAX
} fpga_device_id_t;

/*
 * MFD Platform Data and Operations
 */

/* Forward declaration */
struct as1817_64o_fpga_platform_data;

/*
 * struct as1817_64o_fpga_ops - Register access operations structure
 *
 * Defines the API interface provided by the MFD core driver to its
 * child function drivers. This avoids using global EXPORT_SYMBOL.
 */
struct as1817_64o_fpga_ops {
	u8 (*read8)(struct as1817_64o_fpga_platform_data *pdata, u16 reg);
	void (*write8)(struct as1817_64o_fpga_platform_data *pdata, u16 reg, u8 val);
	u32 (*read32)(struct as1817_64o_fpga_platform_data *pdata, u16 reg);
	void (*write32)(struct as1817_64o_fpga_platform_data *pdata, u16 reg, u32 val);
};

/* MFD platform data passed from core to child drivers */
struct as1817_64o_fpga_platform_data {
	fpga_device_id_t fpga_id;
	void __iomem *base_addr;
	resource_size_t phys_addr;
	resource_size_t mem_size;
	/* Expanded register access interface */
	const struct as1817_64o_fpga_ops *ops;
	/* Private data (handle to parent's private data) */
	void *mfd_private;
	/* Shared mutex protecting MMIO accesses for this FPGA instance */
	struct mutex lock;
};

#endif /* _AS1817_64O_FPGA_H_ */

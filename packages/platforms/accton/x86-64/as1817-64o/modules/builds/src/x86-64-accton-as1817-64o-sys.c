// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)  Roger Ho <roger530_ho@accton.com>
 *
 * Based on:
 *	pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *	pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 *	i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 *	pca9540.c from Jean Delvare <khali@linux-fr.org>.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/platform_device.h>
#include "x86-64-accton_ipmi_intf.h"

#define DRVNAME "as1817_64o_sys"

#define IPMI_READ_MAX_LEN 128

#define EEPROM_NAME "eeprom"
#define EEPROM_SIZE 256	/*	256 byte eeprom */

#define IPMI_SYSEEPROM_READ_CMD 0x18
#define IPMI_CPLD_READ_CMD 0x20
#define CPLD_COUNT (8)

static int as1817_64o_sys_probe(struct platform_device *pdev);
static int as1817_64o_sys_remove(struct platform_device *pdev);
static ssize_t show_version(struct device *dev,
				struct device_attribute *da, char *buf);

struct as1817_64o_sys_data {
	struct platform_device *pdev;
	struct mutex update_lock;
	char valid; /* != 0 if registers are valid */
	unsigned long last_updated;	/* In jiffies */
	struct ipmi_data ipmi;
	unsigned char ipmi_resp_eeprom[EEPROM_SIZE];
	unsigned char ipmi_resp_cpld[CPLD_COUNT][4];
	unsigned char ipmi_tx_data[2];
	struct bin_attribute eeprom; /* eeprom data */
};

struct as1817_64o_sys_data *data = NULL;

static struct platform_driver as1817_64o_sys_driver = {
	.probe = as1817_64o_sys_probe,
	.remove = as1817_64o_sys_remove,
	.driver = {
		.name = DRVNAME,
		.owner = THIS_MODULE,
	},
};

enum as1817_64o_sys_sysfs_attrs {
	DCSCM_CPLD_VER = 0,
	EC_VER,   /* EC version */
	FPGA_VER, /* FPGA version */
	SYS_CPLD_VER, /* SYS CPLD version */
	FAN_CPLD0_VER, /* FAN CPLD0 version */
	FAN_CPLD1_VER, /* FAN CPLD1 version */
	PORT_CPLD0_VER, /* PORT CPLD0 version */
	PORT_CPLD1_VER, /* PORT CPLD1 version */
};


static SENSOR_DEVICE_ATTR(dcscm_cpld_version, S_IRUGO, show_version, NULL, DCSCM_CPLD_VER);
static SENSOR_DEVICE_ATTR(ec_version, S_IRUGO, show_version, NULL, EC_VER);
static SENSOR_DEVICE_ATTR(sys_cpld_version, S_IRUGO, show_version, NULL, SYS_CPLD_VER);
static SENSOR_DEVICE_ATTR(fpga_version, S_IRUGO, show_version, NULL, FPGA_VER);
static SENSOR_DEVICE_ATTR(fan_cpld0_version, S_IRUGO, show_version, NULL, FAN_CPLD0_VER);
static SENSOR_DEVICE_ATTR(fan_cpld1_version, S_IRUGO, show_version, NULL, FAN_CPLD1_VER);
static SENSOR_DEVICE_ATTR(port_cpld0_version, S_IRUGO, show_version, NULL, PORT_CPLD0_VER);
static SENSOR_DEVICE_ATTR(port_cpld1_version, S_IRUGO, show_version, NULL, PORT_CPLD1_VER);

static struct attribute *as1817_64o_sys_attributes[] = {
	&sensor_dev_attr_dcscm_cpld_version.dev_attr.attr,
	&sensor_dev_attr_ec_version.dev_attr.attr,
	&sensor_dev_attr_fpga_version.dev_attr.attr,
	&sensor_dev_attr_sys_cpld_version.dev_attr.attr,
	&sensor_dev_attr_fan_cpld0_version.dev_attr.attr,
	&sensor_dev_attr_fan_cpld1_version.dev_attr.attr,
	&sensor_dev_attr_port_cpld0_version.dev_attr.attr,
	&sensor_dev_attr_port_cpld1_version.dev_attr.attr,
	NULL
};

static const struct attribute_group as1817_64o_sys_group = {
	.attrs = as1817_64o_sys_attributes,
};

/**
 * sys_eeprom_read - Read data from system EEPROM
 * @off: Offset within the EEPROM to start reading
 * @buf: Buffer to store the read data
 * @count: Number of bytes to read
 *
 * This function reads data from the system EEPROM. When SYS_DUMMY is defined,
 * it returns data from a static dummy array instead of communicating with
 * the BMC via IPMI.
 *
 * Return: Number of bytes read on success, negative error code on failure.
 */
static ssize_t sys_eeprom_read(loff_t off, char *buf, size_t count)
{
	int status = 0;
	unsigned char length = 0;

	if ((off + count) > EEPROM_SIZE)
		return -EINVAL;

	length = (count >= IPMI_READ_MAX_LEN) ? IPMI_READ_MAX_LEN : count;
	data->ipmi_tx_data[0] = (off & 0xff);
	data->ipmi_tx_data[1] = length;
	status = ipmi_send_message(&data->ipmi, IPMI_SYSEEPROM_READ_CMD,
								data->ipmi_tx_data, sizeof(data->ipmi_tx_data),
								data->ipmi_resp_eeprom + off, length);
	if (unlikely(status != 0))
		goto exit;

	if (unlikely(data->ipmi.rx_result != 0)) {
		status = -EIO;
		goto exit;
	}

	status = length; /* Read length */
	memcpy(buf, data->ipmi_resp_eeprom + off, length);

exit:
	return status;
}

static ssize_t sysfs_bin_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;

	if (unlikely(!count))
		return count;

	/*
	 * Read data from chip, protecting against concurrent updates
	 * from this host
	 */
	mutex_lock(&data->update_lock);

	while (count) {
		ssize_t status;

		status = sys_eeprom_read(off, buf, count);
		if (status <= 0) {
			if (retval == 0)
				retval = status;
			break;
		}

		buf += status;
		off += status;
		count -= status;
		retval += status;
	}

	mutex_unlock(&data->update_lock);
	return retval;
}

static int sysfs_eeprom_init(struct kobject *kobj, struct bin_attribute *eeprom)
{
	sysfs_bin_attr_init(eeprom);
	eeprom->attr.name = EEPROM_NAME;
	eeprom->attr.mode = S_IRUGO;
	eeprom->read = sysfs_bin_read;
	eeprom->size = EEPROM_SIZE;
	eeprom->write = NULL;

	/* Create eeprom file */
	return sysfs_create_bin_file(kobj, eeprom);
}

static int sysfs_eeprom_cleanup(struct kobject *kobj,
				struct bin_attribute *eeprom)
{
	sysfs_remove_bin_file(kobj, eeprom);
	return 0;
}

/**
 * as1817_64o_sys_update_version - Update CPLD version information
 * @cpld_id: The CPLD identifier to query
 *
 * This function retrieves the version information for a specific CPLD.
 * When SYS_DUMMY is defined, it uses static dummy version data instead
 * of querying the BMC via IPMI.
 *
 * Return: Pointer to the as1817_64o_sys_data structure.
 */
static struct as1817_64o_sys_data *as1817_64o_sys_update_version(int cpld_id)
{
	int status = 0;

	data->valid = 0;

	data->ipmi_tx_data[0] = cpld_id;
	status = ipmi_send_message(&data->ipmi, IPMI_CPLD_READ_CMD,
					data->ipmi_tx_data, 1,
					&data->ipmi_resp_cpld[cpld_id][0], 4);
	if (unlikely(status != 0))
		goto exit;

	if (unlikely(data->ipmi.rx_result != 0)) {
		status = -EIO;
		goto exit;
	}

	data->last_updated = jiffies;
	data->valid = 1;

exit:
	return data;
}

static ssize_t show_version(struct device *dev,
				struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	unsigned char major = 0, minor = 0, patch = 0, build = 0;
	int error = 0;

	mutex_lock(&data->update_lock);

	data = as1817_64o_sys_update_version(attr->index);
	if (!data->valid) {
		error = -EIO;
		goto exit;
	}

	major = data->ipmi_resp_cpld[attr->index - DCSCM_CPLD_VER][0];
	minor = data->ipmi_resp_cpld[attr->index - DCSCM_CPLD_VER][1];
	patch = data->ipmi_resp_cpld[attr->index - DCSCM_CPLD_VER][2];
	build = data->ipmi_resp_cpld[attr->index - DCSCM_CPLD_VER][3];

	mutex_unlock(&data->update_lock);

	switch (attr->index) {
		case DCSCM_CPLD_VER:
			return sprintf(buf, "%02x\n", major);
		case EC_VER:
			return sprintf(buf, "%02x.%02x.%02x.%02x\n", major, minor, patch, build);
		default:
			return sprintf(buf, "%02x.%02x\n", major, minor);
	}
exit:
	mutex_unlock(&data->update_lock);
	return sprintf(buf, "0.0\n");
}

static int as1817_64o_sys_probe(struct platform_device *pdev)
{
	int status = -1;

	/* Register sysfs hooks */
	status = sysfs_eeprom_init(&pdev->dev.kobj, &data->eeprom);
	if (status)
		goto exit;

	/* Register sysfs hooks */
	status = sysfs_create_group(&pdev->dev.kobj, &as1817_64o_sys_group);
	if (status)
		goto exit;

	dev_info(&pdev->dev, "device created\n");

	return 0;

exit:
	return status;
}

static int as1817_64o_sys_remove(struct platform_device *pdev)
{
	sysfs_eeprom_cleanup(&pdev->dev.kobj, &data->eeprom);
	sysfs_remove_group(&pdev->dev.kobj, &as1817_64o_sys_group);

	return 0;
}

static int __init as1817_64o_sys_init(void)
{
	int ret;

	data = kzalloc(sizeof(struct as1817_64o_sys_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_init(&data->update_lock);

	ret = platform_driver_register(&as1817_64o_sys_driver);
	if (ret < 0)
		goto dri_reg_err;

	data->pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(data->pdev)) {
		ret = PTR_ERR(data->pdev);
		goto dev_reg_err;
	}

	/* Set up IPMI interface */
	ret = init_ipmi_data(&data->ipmi, 0, &data->pdev->dev);
	if (ret)
		goto ipmi_err;

	return 0;

ipmi_err:
	platform_device_unregister(data->pdev);
dev_reg_err:
	platform_driver_unregister(&as1817_64o_sys_driver);
dri_reg_err:
	kfree(data);
alloc_err:
	return ret;
}

static void __exit as1817_64o_sys_exit(void)
{
	ipmi_destroy_user(data->ipmi.user);
	platform_device_unregister(data->pdev);
	platform_driver_unregister(&as1817_64o_sys_driver);
	kfree(data);
}

MODULE_AUTHOR("Roger Ho <roger530_ho@accton.com>");
MODULE_DESCRIPTION("AS1817-64O SYS Driver");
MODULE_LICENSE("GPL");

module_init(as1817_64o_sys_init);
module_exit(as1817_64o_sys_exit);

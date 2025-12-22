// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A hwmon driver for the as9936_128d_fan
 *
 * Copyright (C) 2019 Accton Technology Corporation.
 * Brandon Chuang <brandon_chuang@accton.com>
 *
 * Based on:
 *  pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *  pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 *  i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 *  pca9540.c from Jean Delvare <khali@linux-fr.org>.
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

#define DRVNAME "as1817_64o_fan"

#define IPMI_FAN_READ_CMD   0x14
#define IPMI_FAN_WRITE_CMD  0x15
#define IPMI_FAN_REG_READ_CMD  0x20
#define IPMI_FAN_MODE_READ_CMD  0x10
#define IPMI_FAN_SERIAL_READ_CMD  0x11
#define IPMI_STRING_LEN   32

#define MAX_FAN_FRONT_SPEED 20500
#define MAX_FAN_REAR_SPEED  21800

static ssize_t set_fan(struct device *dev, struct device_attribute *da,
               const char *buf, size_t count);
static ssize_t show_fan(struct device *dev, struct device_attribute *attr,
            char *buf);
static ssize_t show_string(struct device *dev, struct device_attribute *attr,
            char *buf);
static ssize_t show_version(struct device *dev, struct device_attribute *da,
                char *buf);
static ssize_t show_dir(struct device *dev, struct device_attribute *da,
            char *buf);
static int as1817_64o_fan_probe(struct platform_device *pdev);
static int as1817_64o_fan_remove(struct platform_device *pdev);

enum fan_id {
    FAN_1,
    FAN_2,
    FAN_3,
    FAN_4,
    FAN_5,
    FAN_6,
    FAN_7,
    FAN_8,
    FAN_9,
    FAN_10,
    FAN_11,
    FAN_12,
    FAN_13,
    FAN_14,
    FAN_15,
    FAN_16,
    NUM_OF_FAN,
    NUM_OF_FAN_PER_MODULE = 2,
    NUM_OF_FAN_MODULE = NUM_OF_FAN / NUM_OF_FAN_PER_MODULE,
    NUM_OF_FAN_BOARD = 2,
    NUM_OF_FAN_PER_FAN_BOARD = NUM_OF_FAN / NUM_OF_FAN_BOARD,
    NUM_OF_FAN_MODULE_PER_FAN_BOARD = NUM_OF_FAN_PER_FAN_BOARD / NUM_OF_FAN_PER_MODULE
};

enum fan_data_index {
    FAN_PRESENT,
    FAN_PWM,
    FAN_SPEED0,
    FAN_SPEED1,
    FAN_DATA_COUNT,

    FAN_DIR0 = (NUM_OF_FAN * FAN_DATA_COUNT),
    FAN_DIR1
};

struct as1817_64o_fan_data {
    struct platform_device *pdev;
    struct mutex update_lock;

    char valid[NUM_OF_FAN];        /* != 0 if registers are valid */
    unsigned long last_updated[NUM_OF_FAN];    /* In jiffies */
    /* 4 bytes for each fan, the last 2 bytes is fan dir */
    unsigned char ipmi_resp[NUM_OF_FAN * FAN_DATA_COUNT + 2];
    char ipmi_resp_model[NUM_OF_FAN][IPMI_STRING_LEN + 1];
    char ipmi_resp_serial[NUM_OF_FAN][IPMI_STRING_LEN + 1];

    char board_valid[NUM_OF_FAN_BOARD];        /* != 0 if registers are valid */
    unsigned long board_last_updated[NUM_OF_FAN_BOARD];    /* In jiffies */
    unsigned char ipmi_resp_cpld[NUM_OF_FAN_BOARD][2];

    struct ipmi_data ipmi;
    unsigned char ipmi_tx_data[2];    /* 0: FAN id, 1: PWM */
};

struct as1817_64o_fan_data *data = NULL;

static struct platform_driver as1817_64o_fan_driver = {
    .probe = as1817_64o_fan_probe,
    .remove = as1817_64o_fan_remove,
    .driver = {
           .name = DRVNAME,
           .owner = THIS_MODULE,
           },
};

#define FAN_PRESENT_ATTR_ID(index) FAN##index##_PRESENT
#define FAN_PWM_ATTR_ID(index) FAN##index##_PWM
#define FAN_RPM_ATTR_ID(index) FAN##index##_INPUT
#define FAN_RPM_MAX_ATTR_ID(index) FAN##index##_MAX
#define FAN_DIR_ATTR_ID(index) FAN##index##_DIR
#define FAN_SERIAL_ATTR_ID(index) FAN##index##_SERIAL
#define FAN_MODEL_ATTR_ID(index) FAN##index##_MODEL

#define FAN_ATTR(fan_id) \
    FAN_PRESENT_ATTR_ID(fan_id), \
    FAN_PWM_ATTR_ID(fan_id), \
    FAN_RPM_ATTR_ID(fan_id), \
    FAN_RPM_MAX_ATTR_ID(fan_id), \
    FAN_DIR_ATTR_ID(fan_id), \
    FAN_SERIAL_ATTR_ID(fan_id), \
    FAN_MODEL_ATTR_ID(fan_id)

enum as1817_64o_fan_sysfs_attrs {
    FAN_ATTR(1),
    FAN_ATTR(2),
    FAN_ATTR(3),
    FAN_ATTR(4),
    FAN_ATTR(5),
    FAN_ATTR(6),
    FAN_ATTR(7),
    FAN_ATTR(8),
    FAN_ATTR(9),
    FAN_ATTR(10),
    FAN_ATTR(11),
    FAN_ATTR(12),
    FAN_ATTR(13),
    FAN_ATTR(14),
    FAN_ATTR(15),
    FAN_ATTR(16),
    NUM_OF_FAN_ATTR,
    NUM_OF_PER_FAN_ATTR = (NUM_OF_FAN_ATTR / NUM_OF_FAN),
    FAN_BOARD1_VERSION,
    FAN_BOARD2_VERSION
};

/* fan attributes */
#define DECLARE_FAN_VER_SENSOR_DEVICE_ATTR() \
    static SENSOR_DEVICE_ATTR(fan_board1_version, S_IRUGO, show_version, NULL, FAN_BOARD1_VERSION); \
    static SENSOR_DEVICE_ATTR(fan_board2_version, S_IRUGO, show_version, NULL, FAN_BOARD2_VERSION)
#define DECLARE_FAN_VER_ATTR() \
    &sensor_dev_attr_fan_board1_version.dev_attr.attr, \
    &sensor_dev_attr_fan_board2_version.dev_attr.attr

#define DECLARE_FAN_SENSOR_DEVICE_ATTR(index) \
    static SENSOR_DEVICE_ATTR(fan##index##_present, S_IRUGO, show_fan, NULL, \
                                FAN##index##_PRESENT); \
    static SENSOR_DEVICE_ATTR(fan##index##_pwm, S_IWUSR | S_IRUGO, show_fan, \
                                set_fan, FAN##index##_PWM); \
    static SENSOR_DEVICE_ATTR(fan##index##_input, S_IRUGO, show_fan, NULL, \
                                FAN##index##_INPUT); \
    static SENSOR_DEVICE_ATTR(fan##index##_max, S_IRUGO, show_fan, NULL, \
                                FAN##index##_MAX); \
    static SENSOR_DEVICE_ATTR(fan##index##_dir, S_IRUGO, show_dir, NULL, \
                                FAN##index##_DIR); \
    static SENSOR_DEVICE_ATTR(fan##index##_serial, S_IRUGO, show_string, NULL, \
                                FAN##index##_SERIAL); \
    static SENSOR_DEVICE_ATTR(fan##index##_model, S_IRUGO, show_string, NULL, \
                                FAN##index##_MODEL);

#define DECLARE_FAN_ATTR(index) \
    &sensor_dev_attr_fan##index##_present.dev_attr.attr, \
    &sensor_dev_attr_fan##index##_pwm.dev_attr.attr, \
    &sensor_dev_attr_fan##index##_input.dev_attr.attr, \
    &sensor_dev_attr_fan##index##_max.dev_attr.attr, \
    &sensor_dev_attr_fan##index##_dir.dev_attr.attr, \
    &sensor_dev_attr_fan##index##_serial.dev_attr.attr, \
    &sensor_dev_attr_fan##index##_model.dev_attr.attr

DECLARE_FAN_SENSOR_DEVICE_ATTR(1);
DECLARE_FAN_SENSOR_DEVICE_ATTR(2);
DECLARE_FAN_SENSOR_DEVICE_ATTR(3);
DECLARE_FAN_SENSOR_DEVICE_ATTR(4);
DECLARE_FAN_SENSOR_DEVICE_ATTR(5);
DECLARE_FAN_SENSOR_DEVICE_ATTR(6);
DECLARE_FAN_SENSOR_DEVICE_ATTR(7);
DECLARE_FAN_SENSOR_DEVICE_ATTR(8);
DECLARE_FAN_SENSOR_DEVICE_ATTR(9);
DECLARE_FAN_SENSOR_DEVICE_ATTR(10);
DECLARE_FAN_SENSOR_DEVICE_ATTR(11);
DECLARE_FAN_SENSOR_DEVICE_ATTR(12);
DECLARE_FAN_SENSOR_DEVICE_ATTR(13);
DECLARE_FAN_SENSOR_DEVICE_ATTR(14);
DECLARE_FAN_SENSOR_DEVICE_ATTR(15);
DECLARE_FAN_SENSOR_DEVICE_ATTR(16);
DECLARE_FAN_VER_SENSOR_DEVICE_ATTR();

static struct attribute *as1817_64o_fan_attributes[] = {
    /* fan attributes */
    DECLARE_FAN_ATTR(1),
    DECLARE_FAN_ATTR(2),
    DECLARE_FAN_ATTR(3),
    DECLARE_FAN_ATTR(4),
    DECLARE_FAN_ATTR(5),
    DECLARE_FAN_ATTR(6),
    DECLARE_FAN_ATTR(7),
    DECLARE_FAN_ATTR(8),
    DECLARE_FAN_ATTR(9),
    DECLARE_FAN_ATTR(10),
    DECLARE_FAN_ATTR(11),
    DECLARE_FAN_ATTR(12),
    DECLARE_FAN_ATTR(13),
    DECLARE_FAN_ATTR(14),
    DECLARE_FAN_ATTR(15),
    DECLARE_FAN_ATTR(16),
    DECLARE_FAN_VER_ATTR(),
    NULL
};

static const struct attribute_group as1817_64o_fan_group = {
    .attrs = as1817_64o_fan_attributes,
};

static struct as1817_64o_fan_data *as1817_64o_fan_update_device(struct
    device_attribute
    *da)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    unsigned char fid = attr->index / NUM_OF_PER_FAN_ATTR;
    int status = 0;

    if (time_before(jiffies, data->last_updated[fid] + HZ * 5) && data->valid[fid])
        return data;

    data->valid[fid] = 0;
    status = ipmi_send_message(&data->ipmi, IPMI_FAN_READ_CMD, 
                    NULL, 0,
                    data->ipmi_resp, sizeof(data->ipmi_resp));
    if (unlikely(status != 0))
        goto exit;

    if (unlikely(data->ipmi.rx_result != 0)) {
        status = -EIO;
        goto exit;
    }

    // FAN ID: 1~8
    data->ipmi_tx_data[0] = IPMI_FAN_MODE_READ_CMD;
    data->ipmi_tx_data[1] = (fid % NUM_OF_FAN_MODULE) + 1;
    status = ipmi_send_message(&data->ipmi, IPMI_FAN_READ_CMD, 
                    data->ipmi_tx_data, 2,
                    data->ipmi_resp_model[fid], 
                    sizeof(data->ipmi_resp_model[fid]));
    if (unlikely(status != 0))
        goto exit;

    if (unlikely(data->ipmi.rx_result != 0)) {
        status = -EIO;
        goto exit;
    }

    // FAN ID: 1~8
    data->ipmi_tx_data[0] = IPMI_FAN_SERIAL_READ_CMD;
    data->ipmi_tx_data[1] = (fid % NUM_OF_FAN_MODULE) + 1;
    status = ipmi_send_message(&data->ipmi, IPMI_FAN_READ_CMD, 
                   data->ipmi_tx_data, 2,
                   data->ipmi_resp_serial[fid], 
                   sizeof(data->ipmi_resp_serial[fid]));
    if (unlikely(status != 0))
        goto exit;

    if (unlikely(data->ipmi.rx_result != 0)) {
        status = -EIO;
        goto exit;
    }

    data->last_updated[fid] = jiffies;
    data->valid[fid] = 1;

 exit:
    return data;
}

static ssize_t show_fan(struct device *dev, struct device_attribute *da,
            char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    unsigned char fid = attr->index / NUM_OF_PER_FAN_ATTR;
    int value = 0;
    int index = 0;
    int present = 0;
    int error = 0;

    mutex_lock(&data->update_lock);

    data = as1817_64o_fan_update_device(da);
    if (!data->valid[fid]) {
        error = -EIO;
        goto exit;
    }

    index = fid * FAN_DATA_COUNT;    /* base index */
    present = !!data->ipmi_resp[index + FAN_PRESENT];

    switch (attr->index) {
    case FAN1_PRESENT:
    case FAN2_PRESENT:
    case FAN3_PRESENT:
    case FAN4_PRESENT:
    case FAN5_PRESENT:
    case FAN6_PRESENT:
    case FAN7_PRESENT:
    case FAN8_PRESENT:
    case FAN9_PRESENT:
    case FAN10_PRESENT:
    case FAN11_PRESENT:
    case FAN12_PRESENT:
    case FAN13_PRESENT:
    case FAN14_PRESENT:
    case FAN15_PRESENT:
    case FAN16_PRESENT:
        value = present;
        break;
    case FAN1_PWM:
    case FAN2_PWM:
    case FAN3_PWM:
    case FAN4_PWM:
    case FAN5_PWM:
    case FAN6_PWM:
    case FAN7_PWM:
    case FAN8_PWM:
    case FAN9_PWM:
    case FAN10_PWM:
    case FAN11_PWM:
    case FAN12_PWM:
    case FAN13_PWM:
    case FAN14_PWM:
    case FAN15_PWM:
    case FAN16_PWM:
        value = DIV_ROUND_CLOSEST(data->ipmi_resp[index + FAN_PWM] * 666, 100);
        break;
    case FAN1_INPUT:
    case FAN2_INPUT:
    case FAN3_INPUT:
    case FAN4_INPUT:
    case FAN5_INPUT:
    case FAN6_INPUT:
    case FAN7_INPUT:
    case FAN8_INPUT:
    case FAN9_INPUT:
    case FAN10_INPUT:
    case FAN11_INPUT:
    case FAN12_INPUT:
    case FAN13_INPUT:
    case FAN14_INPUT:
    case FAN15_INPUT:
    case FAN16_INPUT:
        value = (int)data->ipmi_resp[index + FAN_SPEED0] |
            (int)data->ipmi_resp[index + FAN_SPEED1] << 8;
        break;
    case FAN1_MAX:
    case FAN2_MAX:
    case FAN3_MAX:
    case FAN4_MAX:
    case FAN5_MAX:
    case FAN6_MAX:
    case FAN7_MAX:
    case FAN8_MAX:
        value = MAX_FAN_FRONT_SPEED;
        break;
    case FAN9_MAX:
    case FAN10_MAX:
    case FAN11_MAX:
    case FAN12_MAX:
    case FAN13_MAX:
    case FAN14_MAX:
    case FAN15_MAX:
    case FAN16_MAX:
        value = MAX_FAN_REAR_SPEED;
        break;
    default:
        error = -EINVAL;
        goto exit;
    }

    mutex_unlock(&data->update_lock);
    return sprintf(buf, "%d\n", present ? value : 0);

 exit:
    mutex_unlock(&data->update_lock);
    return error;
}

static ssize_t show_string(struct device *dev, struct device_attribute *da,
            char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    unsigned char fid = attr->index / NUM_OF_PER_FAN_ATTR;
    char *str = NULL;
    int error = 0;

    mutex_lock(&data->update_lock);

    data = as1817_64o_fan_update_device(da);
    if (!data->valid[fid]) {
        error = -EIO;
        goto exit;
    }

    switch (attr->index) {
    case FAN1_SERIAL:
    case FAN2_SERIAL:
    case FAN3_SERIAL:
    case FAN4_SERIAL:
    case FAN5_SERIAL:
    case FAN6_SERIAL:
    case FAN7_SERIAL:
    case FAN8_SERIAL:
    case FAN9_SERIAL:
    case FAN10_SERIAL:
    case FAN11_SERIAL:
    case FAN12_SERIAL:
    case FAN13_SERIAL:
    case FAN14_SERIAL:
    case FAN15_SERIAL:
    case FAN16_SERIAL:
        str = data->ipmi_resp_serial[fid];
        break;
    case FAN1_MODEL:
    case FAN2_MODEL:
    case FAN3_MODEL:
    case FAN4_MODEL:
    case FAN5_MODEL:
    case FAN6_MODEL:
    case FAN7_MODEL:
    case FAN8_MODEL:
    case FAN9_MODEL:
    case FAN10_MODEL:
    case FAN11_MODEL:
    case FAN12_MODEL:
    case FAN13_MODEL:
    case FAN14_MODEL:
    case FAN15_MODEL:
    case FAN16_MODEL:
        str = data->ipmi_resp_model[fid];
        break;
    default:
        error = -EINVAL;
        goto exit;
    }

    mutex_unlock(&data->update_lock);
    return sprintf(buf, "%s\n", str);

 exit:
    mutex_unlock(&data->update_lock);
    return error;
}

/* ipmitool raw 0x34 0x15 <FANCPLD ID> <FAN ID> <FAN PWM> */
static ssize_t set_fan(struct device *dev, struct device_attribute *da,
               const char *buf, size_t count)
{
    int pwm;
    int status;
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    unsigned char fid = attr->index / NUM_OF_PER_FAN_ATTR;

    status = kstrtoint(buf, 10, &pwm);
    if (status)
        return status;

    pwm = (pwm > 100) ? 15 : DIV_ROUND_CLOSEST((pwm * 100), 666);

    mutex_lock(&data->update_lock);

    /* Send IPMI write command */
    data->ipmi_tx_data[0] = fid + 1;
    data->ipmi_tx_data[1] = pwm;
    status = ipmi_send_message(&data->ipmi, IPMI_FAN_WRITE_CMD,
                   data->ipmi_tx_data,
                   sizeof(data->ipmi_tx_data), NULL, 0);
    if (unlikely(status != 0))
        goto exit;

    if (unlikely(data->ipmi.rx_result != 0)) {
        status = -EIO;
        goto exit;
    }

    /* Data changed, trigger update */
    data->valid[fid] = 0;
    status = count;

 exit:
    mutex_unlock(&data->update_lock);
    return status;
}

static struct as1817_64o_fan_data *as1817_64o_fan_update_cpld_ver(struct
    device_attribute
    *da)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    unsigned char fid = attr->index - FAN_BOARD1_VERSION;
    int status = 0;

    if (time_before(jiffies, data->board_last_updated[fid] + HZ * 5) && data->board_valid[fid])
        return data;

    data->board_valid[fid] = 0;
    data->ipmi_tx_data[0] = 0x4 + fid;
    status = ipmi_send_message(&data->ipmi, IPMI_FAN_REG_READ_CMD,
                    data->ipmi_tx_data, 1,
                    data->ipmi_resp_cpld[fid],
                    sizeof(data->ipmi_resp_cpld[fid]));
    if (unlikely(status != 0))
        goto exit;

    if (unlikely(data->ipmi.rx_result != 0)) {
        status = -EIO;
        goto exit;
    }

    data->board_last_updated[fid] = jiffies;
    data->board_valid[fid] = 1;

 exit:
    return data;
}

static ssize_t show_version(struct device *dev, struct device_attribute *da,
                char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    unsigned char fid = attr->index - FAN_BOARD1_VERSION;
    unsigned char major = 0, minor = 0;
    int error = 0;

    mutex_lock(&data->update_lock);

    data = as1817_64o_fan_update_cpld_ver(da);
    if (!data->valid) {
        error = -EIO;
        goto exit;
    }

	major = data->ipmi_resp_cpld[fid][0];
	minor = data->ipmi_resp_cpld[fid][1];
	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%02x.%02x\n", major, minor);

 exit:
	mutex_unlock(&data->update_lock);
	return error;
}

static ssize_t show_dir(struct device *dev, struct device_attribute *da,
            char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    unsigned char fid = (attr->index / NUM_OF_PER_FAN_ATTR);
    int value = 0;
    int index = 0;
    int present = 0;
    int error = 0;

    mutex_lock(&data->update_lock);

    data = as1817_64o_fan_update_device(da);
    if (!data->valid[fid]) {
        error = -EIO;
        goto exit;
    }

    index = fid * FAN_DATA_COUNT;    /* base index */
    present = !!data->ipmi_resp[index + FAN_PRESENT];

    value = data->ipmi_resp[FAN_DIR0] | (data->ipmi_resp[FAN_DIR1] << 8);
    mutex_unlock(&data->update_lock);

    if (!present)
        return sprintf(buf, "0\n");
    else
        return sprintf(buf, "%s\n", (value & BIT(fid)) ? "B2F" : "F2B");

 exit:
    mutex_unlock(&data->update_lock);
    return error;
}

static int as1817_64o_fan_probe(struct platform_device *pdev)
{
    int status = -1;

    /* Register sysfs hooks */
    status = sysfs_create_group(&pdev->dev.kobj, &as1817_64o_fan_group);
    if (status)
        goto exit;

    dev_info(&pdev->dev, "device created\n");

    return 0;

 exit:
    return status;
}

static int as1817_64o_fan_remove(struct platform_device *pdev)
{
    sysfs_remove_group(&pdev->dev.kobj, &as1817_64o_fan_group);

    return 0;
}

static int __init as1817_64o_fan_init(void)
{
    int ret;

    data = kzalloc(sizeof(struct as1817_64o_fan_data), GFP_KERNEL);
    if (!data) {
        ret = -ENOMEM;
        goto alloc_err;
    }

    mutex_init(&data->update_lock);

    ret = platform_driver_register(&as1817_64o_fan_driver);
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
    platform_driver_unregister(&as1817_64o_fan_driver);
 dri_reg_err:
    kfree(data);
 alloc_err:
    return ret;
}

static void __exit as1817_64o_fan_exit(void)
{
    ipmi_destroy_user(data->ipmi.user);
    platform_device_unregister(data->pdev);
    platform_driver_unregister(&as1817_64o_fan_driver);
    kfree(data);
}

MODULE_AUTHOR("Richard KUO <richard_kuo@accton.com>");
MODULE_DESCRIPTION("as1817_64o thermal driver");
MODULE_LICENSE("GPL");

module_init(as1817_64o_fan_init);
module_exit(as1817_64o_fan_exit);

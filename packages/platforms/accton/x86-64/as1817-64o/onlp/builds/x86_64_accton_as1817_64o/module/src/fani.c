/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2014 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 * Fan Platform Implementation Defaults.
 *
 ***********************************************************/
#include <onlplib/file.h>
#include <onlp/platformi/fani.h>
#include "platform_lib.h"

#define MAX_PSU_FAN_SPEED 35000
#define PSU_FAN_INFO(pid, fid) \
    { \
        { ONLP_FAN_ID_CREATE(FAN_##fid##_ON_PSU_##pid), "PSU "#pid" - Fan "#fid, 0, {0} },\
        0x0,\
        ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,\
        0,\
        0,\
        ONLP_FAN_MODE_INVALID,\
    }

/* Static fan information */
/* Top front fan id : 1, 2, 3, 4
   Bottom front fan id : 5, 6, 7, 8
   Top rear fan id : 9, 10, 11, 12
   Bottom rear fan id : 13, 14, 15, 16 */

onlp_fan_info_t finfo[] = {
    { }, /* Not used */
    {
        { ONLP_FAN_ID_CREATE(FAN_1_ON_FAN_BOARD), "Chassis Fan - 1 Top Front Fan 1", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_2_ON_FAN_BOARD), "Chassis Fan - 2 Top Front Fan 2", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_3_ON_FAN_BOARD), "Chassis Fan - 3 Top Front Fan 3", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_4_ON_FAN_BOARD), "Chassis Fan - 4 Top Front Fan 4", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_5_ON_FAN_BOARD), "Chassis Fan - 5 Bottom Front Fan 1", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_6_ON_FAN_BOARD), "Chassis Fan - 6 Bottom Front Fan 2", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_7_ON_FAN_BOARD), "Chassis Fan - 7 Bottom Front Fan 3", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_8_ON_FAN_BOARD), "Chassis Fan - 8 Bottom Front Fan 4", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_9_ON_FAN_BOARD), "Chassis Fan - 9 Top Rear Fan 1", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_10_ON_FAN_BOARD), "Chassis Fan - 10 Top Rear Fan 2", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_11_ON_FAN_BOARD), "Chassis Fan - 11 Top Rear Fan 3", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_12_ON_FAN_BOARD), "Chassis Fan - 12 Top Rear Fan 4", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_13_ON_FAN_BOARD), "Chassis Fan - 13 Bottom Rear Fan 1", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_14_ON_FAN_BOARD), "Chassis Fan - 14 Bottom Rear Fan 2", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_15_ON_FAN_BOARD), "Chassis Fan - 15 Bottom Rear Fan 3", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    {
        { ONLP_FAN_ID_CREATE(FAN_16_ON_FAN_BOARD), "Chassis Fan - 16 Bottom Rear Fan 4", 0, {0} },
        0x0,
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,
        0,
        0,
        ONLP_FAN_MODE_INVALID,
    },
    PSU_FAN_INFO(1,1),
    PSU_FAN_INFO(2,1),
    PSU_FAN_INFO(3,1),
    PSU_FAN_INFO(4,1)
};

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_FAN(_id)) {             \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

static int
_onlp_fani_info_get_fan(int fid, onlp_fan_info_t* info)
{
    int value, ret;
    char file[32];
    char *str = NULL;
    int   len = 0;

    if (fid < FAN_1_ON_FAN_BOARD || fid > FAN_16_ON_FAN_BOARD) {
        return ONLP_STATUS_E_UNSUPPORTED;
    }

    /* get fan present status */
    ret = onlp_file_read_int(&value, "%s""fan%d_present", FAN_SYSFS_PATH, fid);
    if (ret < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    if (value == 0) {
        return ONLP_STATUS_OK;
    }
    info->status |= ONLP_FAN_STATUS_PRESENT;

    /* get fan dir */
    snprintf(file, sizeof(file), "fan%d_dir", fid);
    len = onlp_file_read_str(&str, FAN_SYSFS_PATH, file);
     if (str && len >= 3) {
         if (strncmp(str, "B2F", strlen("B2F")) == 0) {
             info->status |= ONLP_FAN_STATUS_B2F;
         }
         else {
             info->status |= ONLP_FAN_STATUS_F2B;
         }
     }
     AIM_FREE_IF_PTR(str);
    /* get fan speed */
    ret = onlp_file_read_int(&value, "%s""fan%d_input", FAN_SYSFS_PATH, fid);
    if (ret < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }
    info->rpm = value;
    /* get fan speed from rpm and max speed */
    ret = onlp_file_read_int(&value, "%s""fan%d_max", FAN_SYSFS_PATH, fid);
    if (ret < 0) {
        return ONLP_STATUS_E_INTERNAL;
    }

    info->percentage = info->rpm*100/value;

    if (info->percentage > 100)
        info->percentage = 100;

    return ONLP_STATUS_OK;
}

static int
_onlp_fani_info_get_fan_on_psu(int pid, onlp_fan_info_t* info)
{
    int ret = 0, val = 0;
    char file[32];
    char *str = NULL;
    int   len = 0;

    info->status |= ONLP_FAN_STATUS_PRESENT;

    /* get fan direction
     */
    snprintf(file, sizeof(file), "psu%d_fan_dir", pid);
    len = onlp_file_read_str(&str, PSU_SYSFS_PATH, file);
    if (str && len >= 3) {
        if (strncmp(str, "B2F", strlen("B2F")) == 0) {
            info->status |= ONLP_FAN_STATUS_B2F;
        }
        else {
            info->status |= ONLP_FAN_STATUS_F2B;
        }
    }
    AIM_FREE_IF_PTR(str);
    /* get fan speed
     */
    ret = onlp_file_read_int(&val, "%s""psu%d_fan1_input", PSU_SYSFS_PATH, pid);
    if (ret < 0) {
        AIM_LOG_ERROR("Unable to read status from PSU(%d)\r\n", pid);
        return ONLP_STATUS_E_INTERNAL;
    }
    info->rpm = val;
    info->percentage = (info->rpm * 100)/MAX_PSU_FAN_SPEED;

    return ONLP_STATUS_OK;
}

/*
 * This function will be called prior to all of onlp_fani_* functions.
 */
int
onlp_fani_init(void)
{
    return ONLP_STATUS_OK;
}

int
onlp_fani_info_get(onlp_oid_t id, onlp_fan_info_t* info)
{
    int ret = 0;
    int fid;
    VALIDATE(id);

    fid = ONLP_OID_ID_GET(id);
    *info = finfo[fid];
    switch (fid) {
        case FAN_1_ON_FAN_BOARD:
        case FAN_2_ON_FAN_BOARD:
        case FAN_3_ON_FAN_BOARD:
        case FAN_4_ON_FAN_BOARD:
        case FAN_5_ON_FAN_BOARD:
        case FAN_6_ON_FAN_BOARD:
        case FAN_7_ON_FAN_BOARD:
        case FAN_8_ON_FAN_BOARD:
        case FAN_9_ON_FAN_BOARD:
        case FAN_10_ON_FAN_BOARD:
        case FAN_11_ON_FAN_BOARD:
        case FAN_12_ON_FAN_BOARD:
        case FAN_13_ON_FAN_BOARD:
        case FAN_14_ON_FAN_BOARD:
        case FAN_15_ON_FAN_BOARD:
        case FAN_16_ON_FAN_BOARD:
            ret= _onlp_fani_info_get_fan(fid, info);
            break;
        case FAN_1_ON_PSU_1:
        case FAN_1_ON_PSU_2:
        case FAN_1_ON_PSU_3:
        case FAN_1_ON_PSU_4:
            ret = _onlp_fani_info_get_fan_on_psu(fid - FAN_1_ON_PSU_1 + 1, info);
            break;
        default:
            ret = ONLP_STATUS_E_INVALID;
            break;
    }

    return ret;
}

/*
 * This function sets the fan speed of the given OID as a percentage.
 *
 * This will only be called if the OID has the PERCENTAGE_SET
 * capability.
 *
 * It is optional if you have no fans at all with this feature.
 */
int
onlp_fani_percentage_set(onlp_oid_t id, int p)
{
    int  fid;
    VALIDATE(id);

    fid = ONLP_OID_ID_GET(id);

    if (fid < FAN_1_ON_FAN_BOARD || fid > FAN_16_ON_FAN_BOARD) {
        return ONLP_STATUS_E_UNSUPPORTED;
    }

    if (onlp_file_write_int(p, "%s""psu%d_pwm", PSU_SYSFS_PATH, fid) != 0) {
        AIM_LOG_ERROR("Unable to change duty cycle of fid (%d)\r\n", fid);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

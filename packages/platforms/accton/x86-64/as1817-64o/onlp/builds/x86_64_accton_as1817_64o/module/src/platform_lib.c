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
 *
 *
 ***********************************************************/
#include <onlp/onlp.h>
#include <onlplib/file.h>
#include "platform_lib.h"

#if 0
char* psu_get_pmbus_dir(int id)
{
    char *path[] = { PSU1_PMBUS_SYSFS_FORMAT, PSU2_PMBUS_SYSFS_FORMAT, PSU3_PMBUS_SYSFS_FORMAT, PSU4_PMBUS_SYSFS_FORMAT };
    return path[id-1];
}

int onlp_get_psu_hwmon_idx(int pid)
{
    /* find hwmon index */
    char* file = NULL;
    char* dir = NULL;
    char path[64];
    int ret, hwmon_idx, max_hwmon_idx = 20;

    dir = psu_get_pmbus_dir(pid);
    if (dir == NULL)
        return ONLP_STATUS_E_INTERNAL;

    for (hwmon_idx = 0; hwmon_idx <= max_hwmon_idx; hwmon_idx++) {
        snprintf(path, sizeof(path), "%s/hwmon/hwmon%d/", dir, hwmon_idx);

        ret = onlp_file_find(path, "name", &file);
        AIM_FREE_IF_PTR(file);

        if (ONLP_STATUS_OK == ret)
            return hwmon_idx;
    }

    return -1;
}

int onlp_get_fan_hwmon_idx(int fan_board_idx)
{
    /* find hwmon index */
    char* file = NULL;
    char path[64];
    int ret, hwmon_idx, max_hwmon_idx = 20;
    const char *dir;

    if (fan_board_idx == FAN_BOARD1)
        dir = "/sys/bus/i2c/devices/92-0033/hwmon/hwmon%d/";
    else if (fan_board_idx == FAN_BOARD2)
        dir = "/sys/bus/i2c/devices/93-0033/hwmon/hwmon%d/";
    else
        return -1;

    for (hwmon_idx = 0; hwmon_idx <= max_hwmon_idx; hwmon_idx++) {
        snprintf(path, sizeof(path), dir, hwmon_idx);

        ret = onlp_file_find(path, "name", &file);
        AIM_FREE_IF_PTR(file);

        if (ONLP_STATUS_OK == ret)
            return hwmon_idx;
    }

    return -1;
}

int psu_pmbus_info_get(int id, char *node, int *value)
{
    char *path;
    *value = 0;

    path = psu_get_pmbus_dir(id);
    if (path == NULL)
        return ONLP_STATUS_E_INTERNAL;

    return onlp_file_read_int(value, "%s*%s", path, node);
}

int psu_pmbus_str_get(int id, char *data_buf, int data_len, char *data_name)
{
    char *path;
    int   len    = 0;
    char *str = NULL;

    path = psu_get_pmbus_dir(id);
    if (path == NULL)
        return ONLP_STATUS_E_INTERNAL;

    /* Read attribute */
    len = onlp_file_read_str(&str, "%s/%s", path, data_name);
    if (!str || len <= 0) {
        AIM_FREE_IF_PTR(str);
        return ONLP_STATUS_E_INTERNAL;
    }

    if (len > data_len) {
        AIM_FREE_IF_PTR(str);
        return ONLP_STATUS_E_INVALID;
    }

    aim_strlcpy(data_buf, str, len+1);
    AIM_FREE_IF_PTR(str);
    return ONLP_STATUS_OK;
}
#endif

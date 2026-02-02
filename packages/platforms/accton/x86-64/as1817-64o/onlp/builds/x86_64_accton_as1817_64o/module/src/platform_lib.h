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
#ifndef __PLATFORM_LIB_H__
#define __PLATFORM_LIB_H__

#include "x86_64_accton_as1817_64o_log.h"

#define CHASSIS_FAN_COUNT     16
#define CHASSIS_THERMAL_COUNT 15
#define CHASSIS_LED_COUNT      5
#define CHASSIS_PSU_COUNT      4
#define NUM_OF_THERMAL_PER_PSU 3

#define PSU1_ID 1
#define PSU2_ID 2
#define PSU3_ID 3
#define PSU4_ID 4

#define PSU_SYSFS_PATH "/sys/bus/platform/devices/as1817_64o_psu/"
#define FAN_SYSFS_PATH "/sys/bus/platform/devices/as1817_64o_fan/"
#define IDPROM_PATH    "/sys/bus/platform/devices/as1817_64o_sys/eeprom"

enum onlp_thermal_id {
    THERMAL_RESERVED = 0,
    THERMAL_CPU_CORE,
    THERMAL_1_ON_CARRIER_BOARD,
    THERMAL_2_ON_CARRIER_BOARD,
    THERMAL_1_ON_MAIN_BOARD,
    THERMAL_2_ON_MAIN_BOARD,
    THERMAL_3_ON_MAIN_BOARD,
    THERMAL_4_ON_MAIN_BOARD,
    THERMAL_5_ON_MAIN_BOARD,
    THERMAL_1_ON_RJ45,
    THERMAL_1_ON_FCM_BOARD,
    THERMAL_2_ON_FCM_BOARD,
    THERMAL_1_ON_MAC,
    THERMAL_2_ON_MAC,
    THERMAL_3_ON_MAC,
    THERMAL_4_ON_MAC,
    THERMAL_1_ON_PSU1,
    THERMAL_2_ON_PSU1,
    THERMAL_3_ON_PSU1,
    THERMAL_1_ON_PSU2,
    THERMAL_2_ON_PSU2,
    THERMAL_3_ON_PSU2,
    THERMAL_1_ON_PSU3,
    THERMAL_2_ON_PSU3,
    THERMAL_3_ON_PSU3,
    THERMAL_1_ON_PSU4,
    THERMAL_2_ON_PSU4,
    THERMAL_3_ON_PSU4,
    THERMAL_COUNT
};

enum onlp_led_id {
    LED_LOC = 1,
    LED_DIAG,
    LED_ALARM,
    LED_FAN,
    LED_PSU
};

enum onlp_fan_dir {
    FAN_DIR_F2B,
    FAN_DIR_B2F,
    FAN_DIR_COUNT
};

enum fan_id { 
    FAN_1_ON_FAN_BOARD = 1,
    FAN_2_ON_FAN_BOARD,
    FAN_3_ON_FAN_BOARD,
    FAN_4_ON_FAN_BOARD,
    FAN_5_ON_FAN_BOARD,
    FAN_6_ON_FAN_BOARD,
    FAN_7_ON_FAN_BOARD,
    FAN_8_ON_FAN_BOARD,
    FAN_9_ON_FAN_BOARD,
    FAN_10_ON_FAN_BOARD,
    FAN_11_ON_FAN_BOARD,
    FAN_12_ON_FAN_BOARD,
    FAN_13_ON_FAN_BOARD,
    FAN_14_ON_FAN_BOARD,
    FAN_15_ON_FAN_BOARD,
    FAN_16_ON_FAN_BOARD,
    FAN_1_ON_PSU_1,
    FAN_1_ON_PSU_2,
    FAN_1_ON_PSU_3,
    FAN_1_ON_PSU_4
};

#define AIM_FREE_IF_PTR(p) \
    do \
    { \
        if (p) { \
            aim_free(p); \
            p = NULL; \
        } \
    } while (0)

#endif  /* __PLATFORM_LIB_H__ */

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

#include "x86_64_accton_wedge100bf_65x_log.h"
#include <curl/curl.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <onlplib/file.h>
#include <onlp/onlp.h>

#define DEBUG_MODE 0

#if (DEBUG_MODE == 1)
    #define DEBUG_PRINT(fmt, args...)                                        \
        printf("%s:%s[%d]: " fmt "\r\n", __FILE__, __FUNCTION__, __LINE__, ##args)
#else
    #define DEBUG_PRINT(fmt, args...)
#endif

#define CHASSIS_FAN_COUNT     10
#define CHASSIS_THERMAL_COUNT 19
#define CHASSIS_LED_COUNT     2
#define CHASSIS_PSU_COUNT     2

#define PSU1_ID 1
#define PSU2_ID 2
/* MAX PSU FAN need to check */
#define MAX_PSU_FAN_SPEED 23000

#define IDPROM_PATH "/sys/class/i2c-adapter/i2c-41/41-0050/eeprom"
#define BMC_CURL_PREFIX "https://[fe80::ff:fe00:1%usb0]:443/api/sys/bmc/"
#define BMC_CPLD_CURL_PREFIX "https://[fe80::1%usb0]:443/api/sys/cpldget/"
#define CURL_IGNORE_OFFSET              17
/* CURL data status */
#define curl_data_normal                0
#define curl_data_fan_present           0
#define curl_data_fan_absent            1
/* CURL data location */
#define curl_data_loc_status            0
/* CURL data location - Thermal bus_addr_temp */
#define curl_data_loc_thermal_3_48              1
#define curl_data_loc_thermal_3_49              2
#define curl_data_loc_thermal_3_4a              3
#define curl_data_loc_thermal_3_4b              4
#define curl_data_loc_thermal_3_4c              5
#define curl_data_loc_thermal_8_48              6
#define curl_data_loc_thermal_8_49              7
#define curl_data_loc_thermal_9_48              8
#define curl_data_loc_thermal_9_49              9
#define curl_data_loc_thermal_max6658_4c_1      10
#define curl_data_loc_thermal_max6658_4c_2      11
#define curl_data_loc_thermal_9_4a              12
#define curl_data_loc_thermal_9_4b              13
#define curl_data_loc_thermal_9_4d              14
#define curl_data_loc_thermal_9_4e              15
#define curl_data_loc_thermal_ir35215_9_1       16
#define curl_data_loc_thermal_ir35215_9_2       17
#define curl_data_loc_thermal_psu1_1            18
#define curl_data_loc_thermal_psu1_2            19
#define curl_data_loc_thermal_psu2_1            20
#define curl_data_loc_thermal_psu2_2            21
#define curl_data_loc_thermal_memory            22
#define curl_data_loc_thermal_cpu               23
/* CURL data location - PSU */
#define curl_data_loc_psu_vin                   1
#define curl_data_loc_psu_iin                   3
#define curl_data_loc_psu_pin                   4
#define curl_data_loc_psu_vout                  2
#define curl_data_loc_psu_iout                  14
#define curl_data_loc_psu_pout                  13
#define curl_data_loc_psu_poower_good           12
#define curl_data_loc_psu_model_name            9
#define curl_data_loc_psu_mode_serial           10
#define curl_data_loc_psu_model_ver             11
#define curl_data_loc_psu_fan                   5
/* CURL data location - FAN */
#define curl_data_loc_fan_front_rpm             2
#define curl_data_loc_fan_rear_rpm              3
#define curl_data_loc_fan_pwm                   4
#define curl_data_loc_fan_present               5

/* CURL data location - CPLD */
#define version_number                      4
#define curl_data_loc_cpld_ver_first        50
#define curl_data_loc_cpld_ver_second       51
#define curl_data_loc_cpld_subver_first     68
#define curl_data_loc_cpld_subver_second    69

enum onlp_thermal_id
{
    THERMAL_RESERVED = 0,
    THERMAL_CPU_CORE,
    THERMAL_RAM,
    THERMAL_1_ON_MAIN_BROAD,
    THERMAL_2_ON_MAIN_BROAD,
    THERMAL_3_ON_MAIN_BROAD,
    THERMAL_4_ON_MAIN_BROAD,
    THERMAL_5_ON_MAIN_BROAD,
    THERMAL_6_ON_MAIN_BROAD,
    THERMAL_7_ON_MAIN_BROAD,
    THERMAL_8_ON_MAIN_BROAD,
    THERMAL_9_ON_MAIN_BROAD,
    THERMAL_10_ON_MAIN_BROAD,
    THERMAL_11_ON_MAIN_BROAD,
    THERMAL_12_ON_MAIN_BROAD,
    THERMAL_13_ON_MAIN_BROAD,
    THERMAL_14_ON_MAIN_BROAD,
    THERMAL_15_ON_MAIN_BROAD,
    THERMAL_16_ON_MAIN_BROAD,
    THERMAL_17_ON_MAIN_BROAD,
    THERMAL_1_ON_PSU1,
    THERMAL_2_ON_PSU1,
    THERMAL_1_ON_PSU2,
    THERMAL_2_ON_PSU2
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
    FAN_ON_PSU_1,
    FAN_ON_PSU_2
};

#define HANDLECOUNT 19
enum curl_id
{
    CURL_THERMAL = 0,
    CURL_PSU_PRESENT_1,
    CURL_PSU_PRESENT_2,
    CURL_PSU_STATUS_1,
    CURL_PSU_STATUS_2,
    CURL_FAN_STATUS_1,
    CURL_FAN_STATUS_2,
    CURL_FAN_STATUS_3,
    CURL_FAN_STATUS_4,
    CURL_FAN_STATUS_5,
    CURL_FAN_STATUS_6,
    CURL_FAN_STATUS_7,
    CURL_FAN_STATUS_8,
    CURL_FAN_STATUS_9,
    CURL_FAN_STATUS_10,
    CURL_PSU_1_FAN,
    CURL_PSU_2_FAN,
    CURL_CPLD
};

CURL *curl[HANDLECOUNT];
CURLM *multi_curl;
CURLMsg *msg;

int bmc_curl_init(void);
int bmc_curl_deinit(void);
#endif  /* __PLATFORM_LIB_H__ */


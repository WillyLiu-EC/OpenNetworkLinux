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

#include "x86_64_accton_wedge100bf_32qs_log.h"
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

#define CHASSIS_FAN_COUNT     5
#define CHASSIS_THERMAL_COUNT 7
#define CHASSIS_LED_COUNT     2
#define CHASSIS_PSU_COUNT     2

#define PSU1_ID 1
#define PSU2_ID 2
#define MAX_PSU_FAN_SPEED 23000

#define IDPROM_PATH "/sys/class/i2c-adapter/i2c-40/40-0050/eeprom"
#define BMC_CURL_PREFIX "https://[fe80::ff:fe00:1%usb0]:443/api/sys/bmc/"
#define BMC_CPLD_CURL_PREFIX "https://[fe80::1%usb0]:443/api/sys/cpldget/"

enum onlp_thermal_id
{
    THERMAL_RESERVED = 0,
    THERMAL_CPU_CORE,
    THERMAL_1_ON_MAIN_BROAD,
    THERMAL_2_ON_MAIN_BROAD,
    THERMAL_3_ON_MAIN_BROAD,
    THERMAL_4_ON_MAIN_BROAD,
    THERMAL_5_ON_MAIN_BROAD,
    THERMAL_6_ON_MAIN_BROAD,
    THERMAL_ON_PSU1,
    THERMAL_ON_PSU2
};

enum fan_id {
    FAN_1_ON_FAN_BOARD = 1,
    FAN_2_ON_FAN_BOARD,
    FAN_3_ON_FAN_BOARD,
    FAN_4_ON_FAN_BOARD,
    FAN_5_ON_FAN_BOARD,
    FAN_ON_PSU_1,
    FAN_ON_PSU_2
};

#define HANDLECOUNT 16
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
    CURL_PSU_1_THERMAL,
    CURL_PSU_2_THERMAL,
    CURL_PSU_1_FAN,
    CURL_PSU_2_FAN,
    CURL_SYS_CPLD,
    CURL_FAN_CPLD
};
CURL *curl[HANDLECOUNT];
CURLM *multi_curl;
CURLMsg *msg;

int bmc_curl_init(void);
int bmc_curl_deinit(void);

#endif  /* __PLATFORM_LIB_H__ */


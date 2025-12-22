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
#include <onlplib/file.h>
#include <onlp/platformi/sysi.h>
#include "platform_lib.h"

#include "x86_64_accton_as1817_64o_int.h"
#include "x86_64_accton_as1817_64o_log.h"

#define NUM_OF_CPLD_VER 8
#define BMC_VER1_PATH  "/sys/devices/platform/ipmi_bmc.0/firmware_revision"
#define BMC_VER2_PATH  "/sys/devices/platform/ipmi_bmc.0/aux_firmware_revision"
#define BIOS_VER_PATH  "/sys/devices/virtual/dmi/id/bios_version"

static char* cpld_ver_path[NUM_OF_CPLD_VER] = {
    "/sys/bus/platform/devices/as1817_64o_sys/dcscm_cpld_version", /* DCSCM CPLD */
    "/sys/bus/platform/devices/as1817_64o_sys/ec_version",         /* CPU EC FW */
    "/sys/bus/platform/devices/as1817_64o_sys/fpga_version",       /* FPGA */
    "/sys/bus/platform/devices/as1817_64o_sys/sys_cpld_version",   /* SYS CPLD */
    "/sys/bus/platform/devices/as1817_64o_sys/fan_cpld0_version",  /* TOP Fan CPLD */
    "/sys/bus/platform/devices/as1817_64o_sys/fan_cpld1_version",   /* BOTTON Fan CPLD */
    "/sys/bus/platform/devices/as1817_64o_sys/port_cpld0_version", /* Port CPLD-0 */
    "/sys/bus/platform/devices/as1817_64o_sys/port_cpld1_version"  /* Port CPLD-1 */
};

const char*
onlp_sysi_platform_get(void)
{
    return "x86-64-accton-as1817-64o-r0";
}

int
onlp_sysi_onie_data_get(uint8_t** data, int* size)
{

    uint8_t* rdata = aim_zmalloc(256);
    if (onlp_file_read(rdata, 256, size, IDPROM_PATH) == ONLP_STATUS_OK) {
        if(*size == 256) {
            *data = rdata;
            return ONLP_STATUS_OK;
        }
    }

    aim_free(rdata);
    *size = 0;

    return ONLP_STATUS_E_INTERNAL;
}

int
onlp_sysi_oids_get(onlp_oid_t* table, int max)
{
    int i;
    onlp_oid_t* e = table;
    memset(table, 0, max*sizeof(onlp_oid_t));

    /* 10 Thermal sensors on the chassis */
    for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++) {
        *e++ = ONLP_THERMAL_ID_CREATE(i);
    }

    /* 4 LEDs on the chassis */
    for (i = 1; i <= CHASSIS_LED_COUNT; i++) {
        *e++ = ONLP_LED_ID_CREATE(i);
    }


    /* 4 PSUs on the chassis */
    for (i = 1; i <= CHASSIS_PSU_COUNT; i++) {
        *e++ = ONLP_PSU_ID_CREATE(i);
    }

    /* 16 Fans on the chassis */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++) {
        *e++ = ONLP_FAN_ID_CREATE(i);
    }

    return 0;
}

int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int i;
    char *v[NUM_OF_CPLD_VER] = {NULL};
    onlp_onie_info_t onie;
    char *bios_ver = NULL;
    char *bmc_buf = NULL;
    char *aux_buf = NULL;
    int bmc_major = 0, bmc_minor = 0;
    unsigned int bmc_aux[4] = {0};
    char bmc_ver[16] = "";
    const char *bios = "";
    const char *onie_ver = "";

    for (i = 0; i < AIM_ARRAYSIZE(cpld_ver_path); i++)
        onlp_file_read_str(&v[i], cpld_ver_path[i]);

    pi->cpld_versions = aim_fstrdup("\r\n\t   DCSCM(0x6):%s"
                                    "\r\n\t   Sys(0x60):%s"
                                    "\r\n\t   Fan(0x33):%s" /* top fan */
                                    "\r\n\t   Fan(0x34):%s" /* bottom fan */
                                    "\r\n\t   Port(0x64):%s"
                                    "\r\n\t   Port(0x65):%s"
                                    , v[0], v[3], v[4], v[5], v[6], v[7]);

    if ((onlp_file_read_str(&bmc_buf, BMC_VER1_PATH) >= 0) &&
        (onlp_file_read_str(&aux_buf, BMC_VER2_PATH) >= 0))
    {
        bmc_buf[strcspn(bmc_buf, "\n")] = '\0';
        aux_buf[strcspn(aux_buf, "\n")] = '\0';

        if (sscanf(bmc_buf, "%u.%x", &bmc_major, &bmc_minor) == 2 &&
            sscanf(aux_buf, "0x%x 0x%x 0x%x 0x%x", &bmc_aux[0], &bmc_aux[1], &bmc_aux[2], &bmc_aux[3]) == 4)
        {
            snprintf(bmc_ver, sizeof(bmc_ver), "%02X.%02X.%02X", bmc_major, bmc_minor, bmc_aux[3]);
        }
    }

    if (onlp_file_read_str(&bios_ver, BIOS_VER_PATH) > 0) {
        bios = bios_ver;
    }
    if (onlp_onie_decode_file(&onie, IDPROM_PATH) >= 0) {
        onie_ver = onie.onie_version;
    }

    pi->other_versions = aim_fstrdup("\r\n\t   CPU EC(0x21):%s"
                                     "\r\n\t   FPGA(0x21):%s"
                                     "\r\n\t   BIOS: %s"
                                     "\r\n\t   ONIE: %s"
                                     "\r\n\t   BMC: %s"
                                     ,v[1], v[2], bios, onie_ver, bmc_ver);

    for (i = 0; i < AIM_ARRAYSIZE(v); i++) {
        AIM_FREE_IF_PTR(v[i]);
    }

    AIM_FREE_IF_PTR(bmc_buf);
    AIM_FREE_IF_PTR(aux_buf);
    AIM_FREE_IF_PTR(bios_ver);
    onlp_onie_info_free(&onie);

    return ONLP_STATUS_OK;
}

void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
#if 0
    aim_free(pi->cpld_versions);
    aim_free(pi->other_versions);
    #endif
}


int onlp_sysi_platform_manage_fans(void)
{
    return ONLP_STATUS_OK;
}

int
onlp_sysi_platform_manage_leds(void)
{
#if 0
    int i, ret = ONLP_STATUS_OK;
    int fan_led = ONLP_LED_MODE_GREEN;

    /* Get each fan status
     */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++)
    {
        onlp_fan_info_t fan_info;

        ret = onlp_fani_info_get(ONLP_FAN_ID_CREATE(i), &fan_info);
        if (ret != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to get fan(%d) status\r\n", i);
            fan_led = ONLP_LED_MODE_ORANGE;
            break;
        }

        if (!(fan_info.status & ONLP_FAN_STATUS_PRESENT)) {
            AIM_LOG_ERROR("Fan(%d) is not present\r\n", i);
            fan_led = ONLP_LED_MODE_ORANGE;
            break;
        }

        if (fan_info.status & ONLP_FAN_STATUS_FAILED) {
            AIM_LOG_ERROR("Fan(%d) is not working\r\n", i);
            fan_led = ONLP_LED_MODE_ORANGE;
            break;
        }
    }

        onlp_ledi_mode_set(ONLP_LED_ID_CREATE(LED_FAN), fan_led);

    #endif
    return ONLP_STATUS_OK; 
}

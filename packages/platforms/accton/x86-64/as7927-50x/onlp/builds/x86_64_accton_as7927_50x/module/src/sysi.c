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
#include <unistd.h>
#include <fcntl.h>

#include <onlplib/file.h>
#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"

#include "x86_64_accton_as7927_50x_int.h"
#include "x86_64_accton_as7927_50x_log.h"


#define NUM_OF_CPLD_VER                   7

static char* cpld_ver_path[NUM_OF_CPLD_VER] = {
    "/sys/bus/platform/devices/as7927_50x_sys/come_e_cpld_ver", /* CPU CPLD */
    "/sys/bus/platform/devices/as7927_50x_sys/sys_cpld_ver", /* System CPLD */
    "/sys/bus/platform/devices/as7927_50x_fpga/cpld1_version", /* Port CPLD1 */
    "/sys/bus/platform/devices/as7927_50x_fpga/cpld2_version", /* Port CPLD2 */
    "/sys/bus/platform/devices/as7927_50x_sys/dcscm_cpld_ver", /* DC-SCM CPLD */
    "/sys/bus/platform/devices/as7927_50x_sys/fan_cpld_ver", /* Fan CPLD */
    "/sys/bus/platform/devices/as7927_50x_sys/fpga_cpld_ver", /* FPGA */
};

const char*
onlp_sysi_platform_get(void)
{
    return "x86-64-accton-as7927-50x-r0";
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

    /* 9 Thermal sensors on the chassis */
    for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++) {
        *e++ = ONLP_THERMAL_ID_CREATE(i);
    }

    /* 5 LEDs on the chassis */
    for (i = 1; i <= CHASSIS_LED_COUNT; i++) {
        *e++ = ONLP_LED_ID_CREATE(i);
    }

    /* 2 PSUs on the chassis */
    for (i = 1; i <= CHASSIS_PSU_COUNT; i++) {
        *e++ = ONLP_PSU_ID_CREATE(i);
    }

    /* 8 Fans on the chassis */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++) {
        *e++ = ONLP_FAN_ID_CREATE(i);
    }

    return 0;
}


int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int i, len, ret = ONLP_STATUS_OK;
    char *v[NUM_OF_CPLD_VER] = {NULL};
    char *bmc_buf = NULL;
    char *aux_buf = NULL;
    int bmc_major = 0, bmc_minor = 0;
    unsigned int bmc_aux[4] = {0};
    char bmc_ver[16] = ""; 
    onlp_onie_info_t onie;
    char *bios_ver = NULL;

    for (i = 0; i < AIM_ARRAYSIZE(cpld_ver_path); i++) {

        len = onlp_file_read_str(&v[i], cpld_ver_path[i]);

        if (v[i] == NULL || len <= 0)
            return ONLP_STATUS_E_INTERNAL;
    }

    onlp_file_read_str(&bios_ver, BIOS_VER_PATH);
    onlp_onie_decode_file(&onie, IDPROM_PATH);

    if ((onlp_file_read_str(&bmc_buf, BMC_VER1_PATH) >= 0) &&
        (onlp_file_read_str(&aux_buf, BMC_VER2_PATH) >= 0))
    {
        bmc_buf[strcspn(bmc_buf, "\n")] = '\0';
        aux_buf[strcspn(aux_buf, "\n")] = '\0';

        /*
         * NOTE: The value in /sys/devices/platform/ipmi_bmc.0/firmware_revision is formatted
         * using "%u.%x" in the kernel driver (see ipmi_msghandler.c::firmware_revision_show).
         * The second field (after the dot) is output in hexadecimal format and must be parsed
         * using "%x" from user-space.
         */
        if (sscanf(bmc_buf, "%u.%x", &bmc_major, &bmc_minor) == 2 &&
            sscanf(aux_buf, "0x%x 0x%x 0x%x 0x%x", &bmc_aux[0], &bmc_aux[1], &bmc_aux[2], &bmc_aux[3]) == 4)
        {
            snprintf(bmc_ver, sizeof(bmc_ver), "%02X.%02X.%02X",
                     bmc_major, bmc_minor, bmc_aux[3]);
        }
    }

    pi->cpld_versions = aim_fstrdup("\r\n\t   CPU(0x21):%s"
                                    "\r\n\t   Main(0x61):%s"
                                    "\r\n\t   Main(0x62):%s"
                                    "\r\n\t   Main(0x63):%s"
                                    "\r\n\t   Carrier(0x60):%s"
                                    "\r\n\t   Fan(0x33):%s"
                                    , v[0], v[1], v[2], v[3], v[4], v[5]);
                                    
                                    
                                    
pi->other_versions = aim_fstrdup("\r\n\t   FPGA(0x60):%s"
                                 "\r\n\t   BIOS: %s"
                                 "\r\n\t   ONIE: %s"
                                 "\r\n\t   BMC: %s",
                                 v[6], bios_ver, onie.onie_version, bmc_ver);

    for (i = 0; i < AIM_ARRAYSIZE(v); i++) {
        AIM_FREE_IF_PTR(v[i]);
    }

    AIM_FREE_IF_PTR(bmc_buf);
    AIM_FREE_IF_PTR(aux_buf);
    AIM_FREE_IF_PTR(bios_ver);
    onlp_onie_info_free(&onie);

    return ret;
}

void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
    aim_free(pi->other_versions);
}

int
onlp_sysi_platform_manage_fans(void)
{
    return ONLP_STATUS_OK;
}

int
onlp_sysi_platform_manage_leds(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}


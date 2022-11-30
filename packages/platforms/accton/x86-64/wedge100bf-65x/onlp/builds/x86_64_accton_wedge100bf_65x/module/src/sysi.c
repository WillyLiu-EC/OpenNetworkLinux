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

#include "x86_64_accton_wedge100bf_65x_int.h"
#include "x86_64_accton_wedge100bf_65x_log.h"

/* 4 version fwith main version and subversion */
static int ver[version_number][2];

typedef struct cpld_version
{
    char *attr_name;
    int   major_version;
    int   sub_version;
    char *description;
} cpld_version_t;

const char*
onlp_sysi_platform_get(void)
{
    return "x86-64-accton-wedge100bf-65x-r0";
}

int
onlp_sysi_onie_data_get(uint8_t** data, int* size)
{
    uint8_t* rdata = aim_zmalloc(256);
    if(onlp_file_read(rdata, 256, size, IDPROM_PATH) == ONLP_STATUS_OK) {
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
    
    /* 19 Thermal sensors on the chassis */
    for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++) {
        *e++ = ONLP_THERMAL_ID_CREATE(i);
    }

    /* 2 LEDs on the chassis */
    for (i = 1; i <= CHASSIS_LED_COUNT; i++) {
        *e++ = ONLP_LED_ID_CREATE(i);
    }

    /* 2 PSUs on the chassis */
    for (i = 1; i <= CHASSIS_PSU_COUNT; i++) {
        *e++ = ONLP_PSU_ID_CREATE(i);
    }

    /* 10 Fans on the chassis */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++) {
        *e++ = ONLP_FAN_ID_CREATE(i);
    }

    bmc_curl_init();

    return 0;
}

static void cpld_ver_call_back(void *p)
{
    int ver_data[2];
    int  i, j, k;
    char* loc;
    char str[2];
    int cpld_loc[4];
    int ver_data_loc[2][2] = { {curl_data_loc_cpld_ver_first, curl_data_loc_cpld_ver_second},
                                 {curl_data_loc_cpld_subver_first, curl_data_loc_cpld_subver_second}};

    const char *ptr = (const char *)p;

    if (ptr == NULL)
    {
        AIM_LOG_ERROR("NULL POINTER PASSED to call back function\n");

        return;
    }
    /* fan_lower_cpld */
    loc = strstr(ptr, "fan_lower_cpld");
    cpld_loc[0] = loc-ptr;
    /* fan_upper_cpld */
    loc = strstr(ptr, "fan_upper_cpld");
    cpld_loc[1] = loc-ptr;
    /* sys_lower_cpld */
    loc = strstr(ptr, "sys_lower_cpld");
    cpld_loc[2] = loc-ptr;
    /* sys_upper_cpld */
    loc = strstr(ptr, "sys_upper_cpld");
    cpld_loc[3]  = loc-ptr;

    for(i = 0 ; i < version_number ; i++){
        for(j = 0 ; j < 2 ; j++)
        {
            for(k = 0 ; k < 2 ; k++)
            {
                str[0] = ptr[cpld_loc[i]+ver_data_loc[j][0]];
                str[1] = ptr[cpld_loc[i]+ver_data_loc[j][1]];
                str[2] = '\0';

                ver_data[k] = (int)(str[k]);
                /* ASCII */
                if (ver_data[k] == 97)
                    ver_data[k] = 10;
                else if(ver_data[k] == 98)
                    ver_data[k] = 11;
                else if (ver_data[k] == 99)
                    ver_data[k] = 12;
                else if(ver_data[k] == 100)
                    ver_data[k] = 13;
                else if (ver_data[k] == 101)
                    ver_data[k] = 14;
                else if(ver_data[k] == 102)
                    ver_data[k] = 15;
                else
                    ver_data[k] = (int)(str[k]) - 48;
            }

            ver[i][j] = ver_data[0]*16 + ver_data[1];
        }
    }

    return;
}

int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{

    int index;
    char url[256] = {0};
    cpld_version_t cplds[] = { { "fan_lower_cpld_ver", 0, 0, "FAN LOWER CPLD"},
                               { "fan_upper_cpld_ver", 0, 0, "FAN UPPER CPLD"},
                               { "sys_lower_cpld_ver", 0, 0, "SYSTEM LOWER CPLD"},
                               { "sys_upper_cpld_ver", 0, 0, "SYSTEM UPPER CPLD"}};
    int still_running = 1;
    CURLMcode mc;

    snprintf(url, sizeof(url),"%s""*/*", BMC_CPLD_CURL_PREFIX);

    curl_easy_setopt(curl[CURL_CPLD], CURLOPT_URL, url);
    curl_easy_setopt(curl[CURL_CPLD], CURLOPT_WRITEFUNCTION, cpld_ver_call_back);
    curl_multi_add_handle(multi_curl, curl[CURL_CPLD]);

    while(still_running) {
         mc = curl_multi_perform(multi_curl, &still_running);
        if(mc != CURLM_OK)
        {
            AIM_LOG_ERROR("multi_curl failed, code %d.\n", mc);
        }
    }


    /*  Read CPLD version
        index = 0, FAN LOWER CPLD
        index = 1, FAN UPPER CPLD
        index = 2, SYSTEM LOWER CPLD
        index = 3, SYSTEM UPPER CPLD
    */
    for(index = 0 ; index < version_number ; index++)
    {
        cplds[index].major_version=ver[index][0];
        cplds[index].sub_version=ver[index][1];
    }

    pi->cpld_versions = aim_fstrdup("%s:%x.%x, %s:%x.%x, %s:%x.%x, %s:%x.%x",
                                    cplds[0].description, cplds[0].major_version, cplds[0].sub_version,
                                    cplds[1].description, cplds[1].major_version, cplds[1].sub_version,
                                    cplds[2].description, cplds[2].major_version, cplds[2].sub_version,
                                    cplds[3].description, cplds[3].major_version, cplds[3].sub_version);

    return ONLP_STATUS_OK;
}
void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
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


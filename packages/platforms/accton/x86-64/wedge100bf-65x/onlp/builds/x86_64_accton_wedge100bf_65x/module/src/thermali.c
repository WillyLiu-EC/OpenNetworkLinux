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
 * Thermal Sensor Platform Implementation.
 *
 ***********************************************************/
#include <onlplib/file.h>
#include <onlp/platformi/thermali.h>
#include "platform_lib.h"

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_THERMAL(_id)) {         \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

#define TMP_NUM_ELE 24
static int tmp[TMP_NUM_ELE] = {0};

static int curl_thermal_data_loc[23] = 
{   
    curl_data_loc_thermal_cpu,
    curl_data_loc_thermal_memory,
    curl_data_loc_thermal_3_48,
    curl_data_loc_thermal_3_49,
    curl_data_loc_thermal_3_4a,
    curl_data_loc_thermal_3_4b,
    curl_data_loc_thermal_3_4c,
    curl_data_loc_thermal_8_48,
    curl_data_loc_thermal_8_49,
    curl_data_loc_thermal_9_48,
    curl_data_loc_thermal_9_49,
    curl_data_loc_thermal_max6658_4c_1,
    curl_data_loc_thermal_max6658_4c_2,
    curl_data_loc_thermal_9_4a,
    curl_data_loc_thermal_9_4b,
    curl_data_loc_thermal_9_4d,
    curl_data_loc_thermal_9_4e,
    curl_data_loc_thermal_ir35215_9_1,
    curl_data_loc_thermal_ir35215_9_2,
    curl_data_loc_thermal_psu1_1,
    curl_data_loc_thermal_psu1_2,
    curl_data_loc_thermal_psu2_1,
    curl_data_loc_thermal_psu2_2
};

/* Static values */
static onlp_thermal_info_t linfo[] = {
    { }, /* Not used */
    { { ONLP_THERMAL_ID_CREATE(THERMAL_CPU_CORE), "com_e_driver-i2c-4-33 CPU", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {97200, 102600, 108000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_RAM), "com_e_driver-i2c-4-33 Memory", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {76500, 80750, 85000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_MAIN_BROAD), "tmp75-i2c-3-48", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {72000, 76000, 80000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_MAIN_BROAD), "tmp75-i2c-3-49", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_MAIN_BROAD), "tmp75-i2c-3-4a", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_4_ON_MAIN_BROAD), "tmp75-i2c-3-4b", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {72000, 76000, 80000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_5_ON_MAIN_BROAD), "tmp75-i2c-3-4c", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {72000, 76000, 80000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_6_ON_MAIN_BROAD), "tmp75-i2c-8-48", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_7_ON_MAIN_BROAD), "tmp75-i2c-8-49", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_8_ON_MAIN_BROAD), "tmp75-i2c-9-48", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_9_ON_MAIN_BROAD), "tmp75-i2c-9-49", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_10_ON_MAIN_BROAD), "max6658-i2c-9-4c Max6658", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {92000, 96000, 100000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_11_ON_MAIN_BROAD), "max6658-i2c-9-4c Tofino", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {97000, 101000, 105000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_12_ON_MAIN_BROAD), "tmp75-i2c-28-4a", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_13_ON_MAIN_BROAD), "tmp75-i2c-28-4b", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {72000, 76000, 80000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_14_ON_MAIN_BROAD), "tmp75-i2c-28-4d", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_15_ON_MAIN_BROAD), "tmp75-i2c-28-4e", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_16_ON_MAIN_BROAD), "ir35215-i2c-24-40 Temperature1", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {72000, 76000, 80000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_17_ON_MAIN_BROAD), "ir35215-i2c-24-40 Temperature2", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {72000, 76000, 80000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU1), "PSU1 Ambient Temperature", ONLP_PSU_ID_CREATE(PSU1_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {45000, 47000, 50000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU1), "PSU1 Hotspot Temperature", ONLP_PSU_ID_CREATE(PSU1_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {70000, 73000, 76000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU2), "PSU2 Ambient Temperature", ONLP_PSU_ID_CREATE(PSU2_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {45000, 47000, 50000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU2), "PSU2 Hotspot Temperature", ONLP_PSU_ID_CREATE(PSU2_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {70000, 73000, 75000}
    },
};

void tmp_call_back(void *p) 
{
    int i = 0;
    int j = 0;
    int k = 0;
    char str[40];
    char *ptr = p;

    if (ptr == NULL)
    {
        AIM_LOG_ERROR("NULL POINTER PASSED to call back function\n");
        return;
    }

    for (i = 0; i < TMP_NUM_ELE; i++)
        tmp[i] = -1;

    i = CURL_IGNORE_OFFSET;

    while (ptr[i] && ptr[i] != '[')
    {
        i++;
    }

    if (!ptr[i])
    {
        AIM_LOG_ERROR("NULL POINTER PASSED to call back function\n");
        return;
    }

    i++;
    while (ptr[i] && ptr[i] != ']')
    {
        j = 0;
        while (ptr[i] != ',' && ptr[i] != ']')
        {
            str[j] = ptr[i];
            j++;
            i++;
        }
        str[j] = '\0';
        tmp[k] = atoi(str);
        k++;
        if (ptr[i] == ']') break;
        i++;
    }

    return;
}

/*
 * This will be called to intiialize the thermali subsystem.
 */
int
onlp_thermali_init(void)
{
    return ONLP_STATUS_OK;
}

/*
 * Retrieve the information structure for the given thermal OID.
 *
 * If the OID is invalid, return ONLP_E_STATUS_INVALID.
 * If an unexpected error occurs, return ONLP_E_STATUS_INTERNAL.
 * Otherwise, return ONLP_STATUS_OK with the OID's information.
 *
 * Note -- it is expected that you fill out the information
 * structure even if the sensor described by the OID is not present.
 */
int
onlp_thermali_info_get(onlp_oid_t id, onlp_thermal_info_t* info)
{
    int   tid;
    VALIDATE(id);
    char url[256] = {0};
    CURLMcode mc;
    int still_running = 1;

    tid = ONLP_OID_ID_GET(id);
    
    /* Set the onlp_oid_hdr_t and capabilities */        
    *info = linfo[tid];
    

    /* just need to do curl once */
    if (THERMAL_CPU_CORE == tid)
    {
        snprintf(url, sizeof(url),"%s""tmp/mavericks", BMC_CURL_PREFIX);
        /* Just need to do curl once for Thermal 1-6 */
        curl_easy_setopt(curl[CURL_THERMAL], CURLOPT_URL, url);
        curl_easy_setopt(curl[CURL_THERMAL], CURLOPT_WRITEFUNCTION, tmp_call_back);
        curl_multi_add_handle(multi_curl, curl[CURL_THERMAL]);
        while(still_running) {
            mc = curl_multi_perform(multi_curl, &still_running);
            if(mc != CURLM_OK)
            {
                AIM_LOG_ERROR("multi_curl failed, code %d.\n", mc);
            }
        }
    }
    /* In case of error */
    if (tmp[curl_data_loc_status] != curl_data_normal) {
        info->mcelsius = 0;
        AIM_LOG_ERROR("Error returned from bmc with status %d \n", tmp[curl_data_loc_status]);
        return ONLP_STATUS_E_INTERNAL;
    }

    info->mcelsius = tmp[curl_thermal_data_loc[tid-1]]*100;

    return ONLP_STATUS_OK;
}
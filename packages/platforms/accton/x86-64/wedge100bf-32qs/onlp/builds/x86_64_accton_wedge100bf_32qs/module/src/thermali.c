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
#include <curl/curl.h>

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_THERMAL(_id)) {         \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

#define TMP_NUM_ELE 9
static int tmp[TMP_NUM_ELE] = {0};

#define PS_NUM_ELE 16
#define PS_DESC_LEN 32
static uint32_t ps[PS_NUM_ELE];

static int curl_thermal_data_loc[9] = 
{   
    /* CPU temp */
    curl_data_loc_thermal_4_33_cpu,
    /* memory temp */
    curl_data_loc_thermal_4_33_memory,
    /* 3-0048 tmp75_3_48_temp */
    curl_data_loc_thermal_3_48,
    /* 3-0049 tmp75_3_49_temp */
    curl_data_loc_thermal_3_49,
    /* 3-004a tmp75_3_4a_temp */
    curl_data_loc_thermal_3_4a,
    /* 3-004b tmp75_3_4b_temp */
    curl_data_loc_thermal_3_4b,
    /* 3-004c tmp75_3_4c_temp Max6658 */
    curl_data_loc_thermal_3_4c_max6658,
    /* 3-004c tmp75_3_4c_temp Tofino Fan Ctrl */
    curl_data_loc_thermal_3_4c_tofino,
    /* 3-004d tmp75_3_4d_temp */
    curl_data_loc_thermal_3_4d
};

/* Static values */
static onlp_thermal_info_t linfo[] = {
    { }, /* Not used */
    { { ONLP_THERMAL_ID_CREATE(THERMAL_CPU_CORE), "com_e_driver-i2c-4-33 CPU", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {97200, 102600, 108000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_MEMORY), "com_e_driver-i2c-4-33 Memory", 0}, 
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
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_5_ON_MAIN_BROAD), "max6658-i2c-3-4c Max6658", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {90000, 95000, 100000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_6_ON_MAIN_BROAD), "max6658-i2c-3-4c Tofino", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {94500, 99750, 105000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_7_ON_MAIN_BROAD), "tmp75-i2c-3-4d", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL1_ON_PSU1), "PSU1 Ambient", ONLP_PSU_ID_CREATE(PSU1_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {100000, 105000, 115000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL2_ON_PSU1), "PSU1 Hotspot", ONLP_PSU_ID_CREATE(PSU1_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {100000, 105000, 115000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL1_ON_PSU2), "PSU2 Ambient", ONLP_PSU_ID_CREATE(PSU2_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {100000, 105000, 115000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL2_ON_PSU2), "PSU2 Hotspot", ONLP_PSU_ID_CREATE(PSU2_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {100000, 105000, 115000}
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

static void ps_call_back(void *p)
{
    int i = 0;
    int j = 0;
    int k = 0;
    char str[32];
    const char *ptr = (const char *)p;

    if (ptr == NULL)
    {
        AIM_LOG_ERROR("NULL POINTER PASSED to call back function\n");
        return;
    }

    for (i = 0; i < PS_NUM_ELE; i++)
        ps[i] = 0;

    i = CURL_IGNORE_OFFSET;
    while (ptr[i] && ptr[i] != '[') {
        i++;
    }

    if (!ptr[i])
    {
        AIM_LOG_ERROR("FAILED : NULL POINTER\n");
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
        if (j >= PS_DESC_LEN) 
        { /* sanity check */
            AIM_LOG_ERROR("bad string len from BMC i %d j %d k %d 0x%02x\n", i, j, k, ptr[i]);
            return;
        }
        str[j] = '\0';
        if ((k < 9) || (k > 11))
        {
            ps[k] = atoi(str);
        } 

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
    int curlid = 0;
    char url[256] = {0};
    CURLMcode mc;
    int still_running = 1;

    tid = ONLP_OID_ID_GET(id);
    
    /* Set the onlp_oid_hdr_t and capabilities */        
    *info = linfo[tid];
    
    /* get path */
    if ((THERMAL1_ON_PSU1 == tid) || (THERMAL2_ON_PSU1 == tid) ||
                (THERMAL1_ON_PSU2 == tid) || (THERMAL2_ON_PSU2 == tid))
    {
        /* Get psu status by curl */
        if ((THERMAL1_ON_PSU1 == tid) || (THERMAL2_ON_PSU1 == tid))
        {
            snprintf(url, sizeof(url),"%s""ps/1", BMC_CURL_PREFIX);
            curlid = CURL_PSU_1_THERMAL;
        }
        else
        {
            snprintf(url, sizeof(url),"%s""ps/2", BMC_CURL_PREFIX);
            curlid = CURL_PSU_2_THERMAL;
        }

        curl_easy_setopt(curl[curlid], CURLOPT_URL, url);
        curl_easy_setopt(curl[curlid], CURLOPT_WRITEFUNCTION, ps_call_back);
        curl_multi_add_handle(multi_curl, curl[curlid]);

        while(still_running) {
            mc = curl_multi_perform(multi_curl, &still_running);
            if(mc != CURLM_OK)
            {
                AIM_LOG_ERROR("multi_curl failed, code %d.\n", mc);
            }
        }
        if ((THERMAL1_ON_PSU1 == tid) || (THERMAL1_ON_PSU2 == tid))
            info->mcelsius = ps[curl_data_loc_psu_thermal_1] * 1000;
        else
            info->mcelsius = ps[curl_data_loc_psu_thermal_2] * 1000;
    }
    else 
    {
        /* just need to do curl once */
        if (THERMAL_CPU_CORE == tid)
        {
            snprintf(url, sizeof(url),"%s""tmp/montara", BMC_CURL_PREFIX);
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
        if (tmp[0] != 0) {
            info->mcelsius = 0;
            AIM_LOG_ERROR("Error returned from bmc with status %d \n", tmp[0]);
            return ONLP_STATUS_E_INTERNAL;
        }

        info->mcelsius = tmp[curl_thermal_data_loc[tid-1]]*100;
    }

    return ONLP_STATUS_OK;
}


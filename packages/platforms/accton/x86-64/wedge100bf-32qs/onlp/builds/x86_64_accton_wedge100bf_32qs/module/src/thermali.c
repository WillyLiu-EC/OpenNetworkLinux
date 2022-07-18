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

#define TMP_NUM_ELE 7
static int tmp[TMP_NUM_ELE] = {0};

static char* cpu_coretemp_files[] =
{
        "/sys/devices/platform/coretemp.0*temp1_input",
        "/sys/devices/platform/coretemp.0*temp2_input",
        "/sys/devices/platform/coretemp.0*temp3_input",
        "/sys/devices/platform/coretemp.0*temp4_input",
        "/sys/devices/platform/coretemp.0*temp5_input",
        NULL,
};
/* Static values */
static onlp_thermal_info_t linfo[] = {
    { }, /* Not used */
    { { ONLP_THERMAL_ID_CREATE(THERMAL_CPU_CORE), "CPU Core", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {97200, 102600, 108000}
    },    
    { { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_MAIN_BROAD), "TMP75-1", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {72000, 76000, 80000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_MAIN_BROAD), "TMP75-2", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_MAIN_BROAD), "TMP75-3", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_4_ON_MAIN_BROAD), "TMP75-4", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_5_ON_MAIN_BROAD), "TMP75-5", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {945000, 997500, 105000}
    },
    { { ONLP_THERMAL_ID_CREATE(THERMAL_6_ON_MAIN_BROAD), "TMP75-6", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, {54000, 57000, 60000}
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

    i = 0;

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
    
    tid = ONLP_OID_ID_GET(id);
    
    /* Set the onlp_oid_hdr_t and capabilities */        
    *info = linfo[tid];
    
    /* get path */
    if (THERMAL_CPU_CORE == tid)
    {
        return onlp_file_read_int_max(&info->mcelsius, cpu_coretemp_files);
    }
    else 
    {
        /* just need to do curl once */
        if (THERMAL_1_ON_MAIN_BROAD == tid)
        {
            char url[256] = {0};
            CURLMcode mc;
            int still_running = 1;

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

        info->mcelsius = tmp[tid-1]*100;
    }

    return ONLP_STATUS_OK;
}

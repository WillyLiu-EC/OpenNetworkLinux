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
#include <onlplib/i2c.h>
#include <unistd.h>
#include <onlplib/file.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"
#include <curl/curl.h>

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_PSU(_id)) {             \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

#define MODEL_LEN 32
#define SERIAL_LEN 18
#define STRING_STRAT_LOC 8
static uint32_t ps[PS_NUM_ELE];
static char ps_model[PS_DESC_LEN];
static char ps_serial[PS_DESC_LEN];
static char ps_rev[PS_DESC_LEN];
static bool ps_presence;
/*
 * Get all information about the given PSU oid.
 */
static onlp_psu_info_t pinfo[] =
{
    { }, /* Not used */
    {
        { ONLP_PSU_ID_CREATE(PSU1_ID), "PSU-1", 0 },
    },
    {
        { ONLP_PSU_ID_CREATE(PSU2_ID), "PSU-2", 0 },
    }
};

#if 0
static void ps_presence_call_back(void *p)
{
    int i = 0;
    char *ptr = p;
    ps_presence = PSU_ABSCENT;

    if (!ptr[i])
    {
        AIM_LOG_ERROR("FAILED : NULL POINTER\n");
        return;
    }

    if (ptr[PSU_PRESENT_LOCATION] == '0')
    {
        ps_presence = PSU_PRESENT;
    }
    else
    {
        ps_presence = PSU_ABSCENT;
    }

    return;
}
#endif
static void ps_call_back(void *p)
{
    int i = 0;
    int j = 0;
    int k = 0;
    char str[32];
    char *ret;
    ps_presence = PSU_ABSCENT;
    const char *ptr = (const char *)p;

    if (ptr == NULL)
    {
        AIM_LOG_ERROR("NULL POINTER PASSED to call back function\n");
        return;
    }
    /* init ps var value */
    memset(ps_model, 0, sizeof(ps_model));
    memset(ps_serial, 0, sizeof(ps_serial));
    memset(ps_rev, 0, sizeof(ps_rev));

    ret = strstr(ptr, "absent");
    if (ret)
    {
        ps_presence = PSU_ABSCENT;
        return;
    }
    else
    {
        ps_presence = PSU_PRESENT;
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
        else 
        {
            switch (k) 
            {
                case 9:
                    strncpy(ps_model, str, sizeof(ps_model));
                    break;
                case 10:
                    strncpy(ps_serial, str, sizeof(ps_serial));
                    break;
                case 11:
                    strncpy(ps_rev, str, sizeof(ps_rev));
                    break;
            }
        }
        k++;
        if (ptr[i] == ']') break;
        i++;
    }

    return;
}

int
onlp_psui_init(void)
{
    return ONLP_STATUS_OK;
}

int
onlp_psui_info_get(onlp_oid_t id, onlp_psu_info_t* info)
{
    int pid;
    char url[256] = {0};
    bool power_good = 0 ;
    int curlid = 0;
    int still_running = 1;
    CURLMcode mc;

    VALIDATE(id);

    pid  = ONLP_OID_ID_GET(id);
    *info = pinfo[pid]; /* Set the onlp_oid_hdr_t */

    /* Get psu status by curl */
    snprintf(url, sizeof(url),"%s""ps/%d", BMC_CURL_PREFIX, pid);

    curlid = CURL_PSU_STATUS_1 + pid -1;

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

    if (PSU_PRESENT != ps_presence)
    {
        info->status &= ~ONLP_PSU_STATUS_PRESENT;

        return ONLP_STATUS_OK;
    }
    info->status |= ONLP_PSU_STATUS_PRESENT;
    /* only support AC type */
    info->caps = ONLP_PSU_CAPS_AC;
    /* Get power good status */
    power_good = ps[curl_data_loc_psu_poower_good];
    if (PSU_STATUS_POWER_GOOD != power_good) 
    {
        info->status |= ONLP_PSU_STATUS_FAILED;
    }
    else
    {
        char model[MODEL_LEN+1];
        char serial[SERIAL_LEN+1];
        const char* delims =  "\"";
        char *model_sting;

        memset(model, 0, sizeof(model));
        memset(serial, 0, sizeof(serial));
        /* Get the string */
        memcpy(model, ps_model + STRING_STRAT_LOC, MODEL_LEN);
        model_sting = strtok(model, delims);
        model_sting[MODEL_LEN+1] = '\0';

        memcpy(serial, ps_serial + STRING_STRAT_LOC, SERIAL_LEN);
        serial[SERIAL_LEN+1] = '\0';
        /* Get model name */
        strncpy(info->model, model_sting, sizeof(info->model));
        /* Get serial number */
        strncpy(info->serial, serial, sizeof(info->serial));
        /* Read vin */
        info->mvin = ps[curl_data_loc_psu_vin] * 1000;
        info->caps |= ONLP_PSU_CAPS_VIN;
        /* Read iin */
        info->miin = ps[curl_data_loc_psu_iin];
        info->caps |= ONLP_PSU_CAPS_IIN;
        /* Get pin */
        info->mpin = ps[curl_data_loc_psu_pin];
        info->caps |= ONLP_PSU_CAPS_PIN;
        /* Read iout */
        info->miout = ps[curl_data_loc_psu_iout];
        info->caps |= ONLP_PSU_CAPS_IOUT;
        /* Read pout */
        info->mpout = ps[curl_data_loc_psu_pout];
        info->caps |= ONLP_PSU_CAPS_POUT;
        /* Get vout */
        info->mvout = ps[curl_data_loc_psu_vout] * 1000;
        info->caps |= ONLP_PSU_CAPS_VOUT;

        info->hdr.coids[0] = ONLP_FAN_ID_CREATE(pid + CHASSIS_FAN_COUNT);
        info->hdr.coids[1] = ONLP_THERMAL_ID_CREATE((pid*2 - 1) + CHASSIS_THERMAL_COUNT);
        info->hdr.coids[2] = ONLP_THERMAL_ID_CREATE((pid*2) + CHASSIS_THERMAL_COUNT);
    }

    return ONLP_STATUS_OK;
}

int
onlp_psui_ioctl(onlp_oid_t pid, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

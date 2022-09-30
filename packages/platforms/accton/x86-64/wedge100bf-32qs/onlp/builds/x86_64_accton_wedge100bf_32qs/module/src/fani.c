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
 * Fan Platform Implementation Defaults.
 *
 ***********************************************************/
#include <onlplib/file.h>
#include <onlp/platformi/fani.h>
#include "platform_lib.h"
#include <curl/curl.h>

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_FAN(_id)) {             \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

#define CHASSIS_FAN_INFO(fid)        \
    { \
        { ONLP_FAN_ID_CREATE(FAN_##fid##_ON_FAN_BOARD), "Chassis Fan - "#fid, 0 },\
        0x0,\
        ONLP_FAN_CAPS_SET_PERCENTAGE | ONLP_FAN_CAPS_GET_RPM | ONLP_FAN_CAPS_GET_PERCENTAGE,\
        0,\
        0,\
        ONLP_FAN_MODE_INVALID,\
    }

#define PSU_FAN_INFO(pid)      \
    { \
        { ONLP_FAN_ID_CREATE(FAN_ON_PSU_##pid), "PSU "#pid" - Fan ", 0 },\
        0x0,\
        ONLP_FAN_CAPS_GET_RPM,\
        0,\
        0,\
        ONLP_FAN_MODE_INVALID,\
    }

/* Static fan information */
onlp_fan_info_t finfo[] = {
    { }, /* Not used */
    CHASSIS_FAN_INFO(1),
    CHASSIS_FAN_INFO(2),
    CHASSIS_FAN_INFO(3),
    CHASSIS_FAN_INFO(4),
    CHASSIS_FAN_INFO(5),
    PSU_FAN_INFO(1),
    PSU_FAN_INFO(2)
};

#define fan_NUM_ELE 12
static int farr[fan_NUM_ELE];

#define PS_NUM_ELE 16
#define PS_DESC_LEN 32
static uint32_t ps[PS_NUM_ELE];
/*
 * This function will be called prior to all of onlp_fani_* functions.
 */
void fan_call_back(void *p)
{
    uint8_t i = 0;
    uint8_t j = 0;
    uint8_t k = 0;
    char str[20];
    const char *ptr = (const char *)p;

    if (ptr == NULL) {
        AIM_LOG_ERROR("NULL POINTER PASSED to call back function\n");
        return;
    }

    for (i = 0; i < fan_NUM_ELE; i++) {
        farr[i] = 0;
    }

    i = 0;
    while (ptr[i] && ptr[i] != '[') {
        i++;
    }

    if (!ptr[i]) {
        AIM_LOG_ERROR("FAILED : NULL POINTER\n");
        return;
    }

    i++;
    while (ptr[i] && ptr[i] != ']') {
        j = 0;
        while (ptr[i] != ',' && ptr[i] != ']') {
            str[j] = ptr[i];
            j++;
            i++;
        }
        str[j] = '\0';
        farr[k] = atoi(str);
        k++;
        if (ptr[i] == ']')
            break;
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

    i = 0;
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
        if ((k < 11) || (k > 13))
        {
            ps[k] = atoi(str);
        } 

        k++;
        if (ptr[i] == ']') break;
        i++;
    }

    return;
}

int
onlp_fani_init(void)
{
    return ONLP_STATUS_OK;
}


int
onlp_fani_info_get(onlp_oid_t id, onlp_fan_info_t* info)
{
    int fid;
    int front_fan_speed = 0, read_fan_speed = 0;
    int fan_present = 0;
    char url[256] = {0};
    int curlid = 0;
    int still_running = 1;
    CURLMcode mc;

    VALIDATE(id);
    fid = ONLP_OID_ID_GET(id);
    *info = finfo[fid];

    if ((FAN_ON_PSU_1 == fid) || (FAN_ON_PSU_2 == fid))
    {
        info->status |= ONLP_FAN_STATUS_PRESENT;
        /* get fan direction */
        info->status |= ONLP_FAN_STATUS_F2B;
        /* Get psu fan speed by curl */
        if (FAN_ON_PSU_1 == fid)
        {
            snprintf(url, sizeof(url),"%s""ps/1", BMC_CURL_PREFIX);
            curlid = CURL_PSU_1_FAN;
        }
        else
        {
            snprintf(url, sizeof(url),"%s""ps/2", BMC_CURL_PREFIX);
            curlid = CURL_PSU_2_FAN;
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
        info->rpm = ps[5];
        /* get fan fault status */
        if (!info->rpm) {
            info->status |= ONLP_FAN_STATUS_FAILED;
        }
    }
    else
    {
        snprintf(url, sizeof(url),"%s""fan/get/%d", BMC_CURL_PREFIX, fid);

        curlid = CURL_FAN_STATUS_1 + fid -1;

        curl_easy_setopt(curl[curlid], CURLOPT_URL, url);
        curl_easy_setopt(curl[curlid], CURLOPT_WRITEFUNCTION, fan_call_back);
        curl_multi_add_handle(multi_curl, curl[curlid]);
        while(still_running) {
            mc = curl_multi_perform(multi_curl, &still_running);
            if(mc != CURLM_OK)
            {
                AIM_LOG_ERROR("multi_curl failed, code %d.\n", mc);
            }
        }

        /* In case of error */
        if (farr[0] != 0)
        {
            AIM_LOG_ERROR("Error returned from REST API with status %d \n", farr[0]);
            return ONLP_STATUS_E_INTERNAL;
        }

        fan_present = farr[5];
        if (1 == fan_present) {
            return ONLP_STATUS_OK; /* fan is not present */
        }
        info->status |= ONLP_FAN_STATUS_PRESENT;
        /* get front fan rpm */
        front_fan_speed = farr[2];
        /* get rear fan rpm */
        read_fan_speed = farr[3];
        /* take the min value from front/rear fan speed */
        if(front_fan_speed >= read_fan_speed)
        {
            info->rpm = read_fan_speed;
        }
        else
        {
            info->rpm = front_fan_speed;
        }
        /* set fan status based on rpm*/
        if (!info->rpm) {
            info->status |= ONLP_FAN_STATUS_FAILED;
            return ONLP_STATUS_OK;
        }
        /* get speed percentage */
        info->percentage = farr[4];
        /* set fan direction */
        info->status |= ONLP_FAN_STATUS_F2B;
    }

    return ONLP_STATUS_OK;
}

/*
 * This function sets the speed of the given fan in RPM.
 *
 * This function will only be called if the fan supprots the RPM_SET
 * capability.
 *
 * It is optional if you have no fans at all with this feature.
 */
int
onlp_fani_rpm_set(onlp_oid_t id, int rpm)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

/*
 * This function sets the fan speed of the given OID as a percentage.
 *
 * This will only be called if the OID has the PERCENTAGE_SET
 * capability.
 *
 * It is optional if you have no fans at all with this feature.
 */
int
onlp_fani_percentage_set(onlp_oid_t id, int p)
{

    return ONLP_STATUS_OK;
}

/*
 * This function sets the fan speed of the given OID as per
 * the predefined ONLP fan speed modes: off, slow, normal, fast, max.
 *
 * Interpretation of these modes is up to the platform.
 *
 */
int
onlp_fani_mode_set(onlp_oid_t id, onlp_fan_mode_t mode)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

/*
 * This function sets the fan direction of the given OID.
 *
 * This function is only relevant if the fan OID supports both direction
 * capabilities.
 *
 * This function is optional unless the functionality is available.
 */
int
onlp_fani_dir_set(onlp_oid_t id, onlp_fan_dir_t dir)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

/*
 * Generic fan ioctl. Optional.
 */
int
onlp_fani_ioctl(onlp_oid_t id, va_list vargs)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}


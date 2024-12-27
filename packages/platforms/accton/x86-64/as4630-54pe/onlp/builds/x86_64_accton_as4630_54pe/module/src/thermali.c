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
//#include <unistd.h>
#include <onlplib/file.h>
#include <onlp/platformi/thermali.h>
#include "platform_lib.h"

#define NUM_OF_CPU_CORES 4
#define THERMAL_PATH_FORMAT 	"/sys/bus/i2c/devices/%s/*temp1_input"
#define PSU_THERMAL_PATH_FORMAT "/sys/bus/i2c/devices/%s/*psu_temp1_input"
#define CPU_CORE_ID_PATH_FORMAT  "/sys/devices/system/cpu/cpu%d/topology/core_id"
#define CPU_CORETEMP_PATH_FORMAT "/sys/devices/platform/coretemp.0*temp%d_input"

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_THERMAL(_id)) {         \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

enum onlp_thermal_id
{
    THERMAL_RESERVED = 0,
    THERMAL_CPU_CORE,
    THERMAL_1_ON_MAIN_BROAD,
    THERMAL_2_ON_MAIN_BROAD,
    THERMAL_3_ON_MAIN_BROAD,   
    THERMAL_1_ON_PSU1,
    THERMAL_1_ON_PSU2,
};

static char* directory[] =  /* must map with onlp_thermal_id */
{
    NULL,
    NULL,                  /* CPU_CORE files */
    "24-004b",
    "25-004a",
    "14-0048",
    "10-0058",
    "11-0059",
};

/* Static values */
static onlp_thermal_info_t linfo[] = {
	{ }, /* Not used */
	{ { ONLP_THERMAL_ID_CREATE(THERMAL_CPU_CORE), "CPU Core", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
        },	
	{ { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_MAIN_BROAD), "LM75-1-4B", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
        },
	{ { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_MAIN_BROAD), "LM75-2-4A", 0}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
        },
	{ { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_MAIN_BROAD), "LM75-3-48", 0}, 
           ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
        },
	
	{ { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU1), "PSU-1 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU1_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
        },
	{ { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU2), "PSU-2 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU2_ID)}, 
            ONLP_THERMAL_STATUS_PRESENT,
            ONLP_THERMAL_CAPS_ALL, 0, ONLP_THERMAL_THRESHOLD_INIT_DEFAULTS
        }
};

/*
 * This will be called to intiialize the thermali subsystem.
 */
int
onlp_thermali_init(void)
{
    return ONLP_STATUS_OK;
}

int get_max_cpu_coretemp(int *max_cpu_coretemp)
{
    int core_id = 0;
    int cpu_coretemp = 0;
    int i = 0;
    for(i = 0; i < NUM_OF_CPU_CORES; i++){
        if(onlp_file_read_int(&core_id, CPU_CORE_ID_PATH_FORMAT, i) < 0) {
            AIM_LOG_ERROR("Unable to read cpu core id from "CPU_CORE_ID_PATH_FORMAT"\r\n",
                i);
            *max_cpu_coretemp = 0;
            return ONLP_STATUS_E_INTERNAL;
        }
        if(onlp_file_read_int(&cpu_coretemp, CPU_CORETEMP_PATH_FORMAT, core_id+2) < 0) {
            AIM_LOG_ERROR("Unable to read cpu coretemp from "CPU_CORETEMP_PATH_FORMAT"\r\n",
                core_id+2);
            *max_cpu_coretemp = 0;
            return ONLP_STATUS_E_INTERNAL;
        }
        if(cpu_coretemp > *max_cpu_coretemp)
            *max_cpu_coretemp = cpu_coretemp;
    }
    // read package temp, which is always in temp1_input
    if(onlp_file_read_int(&cpu_coretemp, CPU_CORETEMP_PATH_FORMAT, 1) < 0) {
            AIM_LOG_ERROR("Unable to read cpu package temp from "CPU_CORETEMP_PATH_FORMAT"\r\n",
                1);
            *max_cpu_coretemp = 0;
            return ONLP_STATUS_E_INTERNAL;
    }
    *max_cpu_coretemp = (*max_cpu_coretemp > cpu_coretemp) ?
                            *max_cpu_coretemp : cpu_coretemp;
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
	char *format   = NULL;
	char  path[64] = {0};
    VALIDATE(id);
	
    tid = ONLP_OID_ID_GET(id);
	
    /* Set the onlp_oid_hdr_t and capabilities */		
    *info = linfo[tid];
    if(tid == THERMAL_CPU_CORE) {
        return  get_max_cpu_coretemp(&info->mcelsius);
    }

	switch (tid) {
    	case THERMAL_1_ON_MAIN_BROAD:
    	case THERMAL_2_ON_MAIN_BROAD:
    	case THERMAL_3_ON_MAIN_BROAD:
			format = THERMAL_PATH_FORMAT;
			break;
    	case THERMAL_1_ON_PSU1:
    	case THERMAL_1_ON_PSU2:
			format = PSU_THERMAL_PATH_FORMAT;
			break;
		default:
			return ONLP_STATUS_E_INVALID;
	};
	
    /* get path */
    sprintf(path, format, directory[tid], tid);
    if (onlp_file_read_int(&info->mcelsius, path) < 0) {
        AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;								
}



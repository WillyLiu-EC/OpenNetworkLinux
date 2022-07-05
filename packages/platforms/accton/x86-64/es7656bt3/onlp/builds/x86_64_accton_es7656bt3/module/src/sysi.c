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
#include <limits.h>

#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"
#include "x86_64_accton_es7656bt3_int.h"
#include "x86_64_accton_es7656bt3_log.h"

#define PREFIX_PATH_ON_CPLD_DEV     "/sys/bus/i2c/devices/"
#define NUM_OF_CPLD                 3
#define FAN_DUTY_MAX               (100)
#define FAN_DUTY_MIN               (39)


static char arr_cplddev_name[NUM_OF_CPLD][10] =
{
    "19-0060",
    "13-0062",
    "20-0064"
};

const char*
onlp_sysi_platform_get(void)
{
    return "x86-64-accton-es7656bt3-r0";
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

    /* 5 Thermal sensors on the chassis */
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

    /* 6 Fans on the chassis */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++) {
        *e++ = ONLP_FAN_ID_CREATE(i);
    }

    return 0;
}

int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
    int   i, v[NUM_OF_CPLD]={0};

    for (i = 0; i < NUM_OF_CPLD; i++) {
        v[i] = 0;

        if(onlp_file_read_int(v+i, "%s%s/version", PREFIX_PATH_ON_CPLD_DEV, arr_cplddev_name[i]) < 0) {
            return ONLP_STATUS_E_INTERNAL;
        }
    }
    pi->cpld_versions = aim_fstrdup("%d.%d.%d", v[0], v[1], v[2]);

    return 0;
}

void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
}

/* THERMAL POLICY */
enum fan_level {
    FAN_LEVEL0 = 0,
    FAN_LEVEL1,
    FAN_LEVEL2,
    FAN_LEVELF,
    NUM_FAN_LEVEL,
    FAN_LEVEL_INVALID = NUM_FAN_LEVEL
};

enum onlp_fan_f2b_up_duty_cycle_percentage
{
    FAN_DUTY_F2B_LEVEL_0 = 39,
    FAN_DUTY_F2B_LEVEL_1 = 46,
    FAN_DUTY_F2B_LEVEL_2 = 60,
};

enum onlp_fan_b2f_duty_cycle_percentage
{
    FAN_DUTY_B2F_LEVEL_0 = 39,
    FAN_DUTY_B2F_LEVEL_1 = 46,
    FAN_DUTY_B2F_LEVEL_2 = 67,
};

static const int f2b_fanduty_by_level[] = {
    FAN_DUTY_F2B_LEVEL_0, FAN_DUTY_F2B_LEVEL_1, FAN_DUTY_F2B_LEVEL_2, FAN_DUTY_MAX
};

static const int b2f_fanduty_by_level[] = {
    FAN_DUTY_B2F_LEVEL_0, FAN_DUTY_B2F_LEVEL_1, FAN_DUTY_B2F_LEVEL_2, FAN_DUTY_MAX
};

static unsigned int get_fan_level_by_duty_f2b(int fanduty)
{
    switch(fanduty) {
        case 39: return FAN_LEVEL0;
        case 46: return FAN_LEVEL1;
        case 60: return FAN_LEVEL2;
        case FAN_DUTY_MAX: return FAN_LEVELF;
        default: return FAN_LEVEL_INVALID;
    }
}

static unsigned int get_fan_level_by_duty_b2f(int fanduty)
{
    switch(fanduty) {
        case 39: return FAN_LEVEL0;
        case 46: return FAN_LEVEL1;
        case 67: return FAN_LEVEL2;
        case FAN_DUTY_MAX: return FAN_LEVELF;
        default: return FAN_LEVEL_INVALID;
    }
}

#define THERMAL_COUNT (THERMAL_4_ON_MAIN_BROAD - THERMAL_CPU_CORE + 1)
#define NUM_FAN_LEVEL 4
typedef struct _threshold
{
    int val[THERMAL_COUNT];
    unsigned int next_level;
} threshold_t;
/*
    THERMAL_CPU_CORE
    THERMAL_1_ON_MAIN_BROAD : 0x48
    THERMAL_2_ON_MAIN_BROAD : 0x49
    THERMAL_3_ON_MAIN_BROAD : 0x4a
    THERMAL_4_ON_MAIN_BROAD : 0x4b
*/
/* F2B Thermal Policy */
#define NA_L 0
#define NA_H 999999
threshold_t up_th_f2b[NUM_FAN_LEVEL] = {
    /* CPU Core, THERMAL_1, THERMAL_2, THERMAL_3, THERMAL_4 */
    {{ 52000,    38000,     32000,     27000,     30000 }, FAN_LEVEL1},
    {{ 60000,    45000,     41000,     33000,     36000 }, FAN_LEVEL2},
    {{ 68000,    56000,     51000,     45000,     47000 }, FAN_LEVELF},
    {{ NA_H,     NA_H,      NA_H,      NA_H,      NA_H  }, FAN_LEVELF}  //LEVELF, Won't go any higher.
};

threshold_t down_th_f2b[NUM_FAN_LEVEL] = {
    /* CPU Core, THERMAL_1, THERMAL_2, THERMAL_3, THERMAL_4 */
    {{ NA_L,     NA_L,      NA_L,      NA_L,      NA_L  }, FAN_LEVEL0}, //LEVEL0, Won't go any lower.
    {{ 47000,    33000,     27000,     23000,     25000 }, FAN_LEVEL0},
    {{ 55000,    40000,     36000,     28000,     31000 }, FAN_LEVEL1},
    {{ 63000,    51000,     46000,     39000,     42000 }, FAN_LEVEL2}
};

/* B2F Thermal Policy */
threshold_t up_th_b2f[NUM_FAN_LEVEL] = {
    /* CPU Core, THERMAL_1, THERMAL_2, THERMAL_3, THERMAL_4 */
    {{ 30000,    35000,     43000,     25000,     25000 }, FAN_LEVEL1},
    {{ 38000,    43000,     49000,     34000,     33000 }, FAN_LEVEL2},
    {{ 50000,    53000,     58000,     47000,     46000 }, FAN_LEVELF},
    {{ NA_H,     NA_H,      NA_H,      NA_H,      NA_H  }, FAN_LEVELF}  //LEVELF, Won't go any higher.
};

threshold_t down_th_b2f[NUM_FAN_LEVEL] = {
    /* CPU Core, THERMAL_1, THERMAL_2, THERMAL_3, THERMAL_4 */
    {{ NA_L,     NA_L,      NA_L,      NA_L,      NA_L  }, FAN_LEVEL0}, //LEVEL5, Won't go any lower.
    {{ 25000,    30000,     38000,     20000,     20000 }, FAN_LEVEL0},
    {{ 33000,    38000,     44000,     29000,     27000 }, FAN_LEVEL1},
    {{ 45000,    48000,     53000,     42000,     41000 }, FAN_LEVEL2}
};

static int
sysi_fanctrl_overall_thermal_sensor_policy(onlp_fan_info_t fi[CHASSIS_FAN_COUNT],
                                           onlp_thermal_info_t ti[CHASSIS_THERMAL_COUNT],
                                           int *adjusted)
{
    int i, fanduty, fanlevel, next_up_level, next_down_level;
    bool can_level_up = false; //prioritze up more than down
    bool can_level_down = true;
    int high, low;
    threshold_t* up_th = (fi[0].status & ONLP_FAN_STATUS_F2B)? up_th_f2b : up_th_b2f;
    threshold_t* down_th = (fi[0].status & ONLP_FAN_STATUS_F2B)? down_th_f2b : down_th_b2f;

    *adjusted = 1;
    // decide fanlevel by fanduty
    if(onlp_file_read_int(&fanduty, FAN_NODE(fan_duty_cycle_percentage)) < 0)
        fanduty = FAN_DUTY_MAX;

    if(fi[0].status & ONLP_FAN_STATUS_F2B)
    {
        fanlevel = get_fan_level_by_duty_f2b(fanduty);
    }
    else
    {
        fanlevel = get_fan_level_by_duty_b2f(fanduty);
    }
    fanlevel = (fanlevel==FAN_LEVEL_INVALID)? FAN_LEVELF : fanlevel; //maximum while invalid.

    // decide target level by thermal sensor input.
    next_up_level = up_th[fanlevel].next_level;
    next_down_level = down_th[fanlevel].next_level;
    for (i=THERMAL_CPU_CORE ; i<=THERMAL_4_ON_MAIN_BROAD; i++){
        high = up_th[fanlevel].val[i-THERMAL_CPU_CORE];
        low = down_th[fanlevel].val[i-THERMAL_CPU_CORE];
        // perform level up if anyone is higher than high_th.
        if(ti[i-1].mcelsius >= high) {
            can_level_up = true;
            break;
        }
        // cancel level down if anyone is higher than low_th.
        if(ti[i-1].mcelsius > low)
        {
            can_level_down = false;
        }
    }
    fanlevel = (can_level_up)? next_up_level : ((can_level_down)? next_down_level : fanlevel);
    if(fi[0].status & ONLP_FAN_STATUS_F2B)
    {
        return onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1),
                                        f2b_fanduty_by_level[fanlevel]);
    }
    else
    {
        return onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1),
                                        b2f_fanduty_by_level[fanlevel]);
    }
}

static int
sysi_fanctrl_fan_fault_policy(onlp_fan_info_t fi[CHASSIS_FAN_COUNT],
                              onlp_thermal_info_t ti[CHASSIS_THERMAL_COUNT],
                              int *adjusted)
{
    int i;
    *adjusted = 0;

    /* Bring fan speed to FAN_DUTY_MAX if any fan is not operational */
    for (i = 0; i < CHASSIS_FAN_COUNT; i++) {
        if (!(fi[i].status & ONLP_FAN_STATUS_FAILED)) {
            continue;
        }

        *adjusted = 1;
        return onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_MAX);
    }

    return ONLP_STATUS_OK;
}

static int
sysi_fanctrl_fan_absent_policy(onlp_fan_info_t fi[CHASSIS_FAN_COUNT],
                               onlp_thermal_info_t ti[CHASSIS_THERMAL_COUNT],
                               int *adjusted)
{
    int i;
    *adjusted = 0;

    /* Bring fan speed to FAN_DUTY_MAX if fan is not present */
    for (i = 0; i < CHASSIS_FAN_COUNT; i++) {
        if (fi[i].status & ONLP_FAN_STATUS_PRESENT) {
            continue;
        }

        *adjusted = 1;
        return onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_MAX);
    }

    return ONLP_STATUS_OK;
}

typedef int (*fan_control_policy)(onlp_fan_info_t fi[CHASSIS_FAN_COUNT],
                                  onlp_thermal_info_t ti[CHASSIS_THERMAL_COUNT],
                                  int *adjusted);

fan_control_policy fan_control_policies[] =
{
    sysi_fanctrl_fan_fault_policy,
    sysi_fanctrl_fan_absent_policy,
    sysi_fanctrl_overall_thermal_sensor_policy,
};

int
onlp_sysi_platform_manage_fans(void)
{
    int i, rc;
    onlp_fan_info_t fi[CHASSIS_FAN_COUNT];
    onlp_thermal_info_t ti[CHASSIS_THERMAL_COUNT];

    memset(fi, 0, sizeof(fi));
    memset(ti, 0, sizeof(ti));
    /* Get fan status
     */
    for (i = 0; i < CHASSIS_FAN_COUNT; i++) {
        rc = onlp_fani_info_get(ONLP_FAN_ID_CREATE(i+1), &fi[i]);

        if (rc != ONLP_STATUS_OK) {
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_MAX);
            AIM_LOG_ERROR("Unable to get fan(%d) status\r\n", i+1);
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    /* Get thermal sensor status
     */
    for (i = 0; i < CHASSIS_THERMAL_COUNT; i++) {
        rc = onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(i+1), &ti[i]);
        
        if (rc != ONLP_STATUS_OK) {
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_MAX);
            AIM_LOG_ERROR("Unable to get thermal(%d) status\r\n", i+1);
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    /* Apply thermal policy according the policy list,
     * If fan duty is adjusted by one of the policies, skip the others
     */
    for (i = 0; i < AIM_ARRAYSIZE(fan_control_policies); i++) {
        int adjusted = 0;

        rc = fan_control_policies[i](fi, ti, &adjusted);
        if (!adjusted) {
            continue;
        }

        return rc;
    }

    return ONLP_STATUS_OK;
}

int
onlp_sysi_platform_manage_leds(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

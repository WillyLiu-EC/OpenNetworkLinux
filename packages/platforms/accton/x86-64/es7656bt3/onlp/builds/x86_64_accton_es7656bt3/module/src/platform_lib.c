#include <unistd.h>
#include <fcntl.h>
#include "platform_lib.h"
#include <onlp/platformi/sfpi.h>
#include "x86_64_accton_es7656bt3_log.h"

/* To avoid memory leak */
#define AIM_FREE_IF_PTR(p) \
    do \
    { \
        if (p) { \
            aim_free(p); \
            p = NULL; \
        } \
    } while (0)

#define PSU_NODE_MAX_PATH_LEN 64
#define PSU_FAN_DIR_LEN         3
#define PSU_MODEL_NAME_LEN      11
#define PSU_SERIAL_NUMBER_LEN   18

int get_psu_serial_number(int id, char *serial, int serial_len)
{
    int   ret  = 0;
    char *node = NULL;
    char *sn = NULL;

    if (!sn) {
        return ONLP_STATUS_E_INTERNAL;
    }
    
    /* Read AC serial number */
    node = (id == PSU1_ID) ? PSU1_AC_PMBUS_NODE(psu_mfr_serial) : PSU1_AC_PMBUS_NODE(psu_mfr_serial);
    ret = onlp_file_read_str(&sn, node);
    if (ret <= 0 || ret > PSU_SERIAL_NUMBER_LEN || sn == NULL) {
        AIM_FREE_IF_PTR(sn);
        return ONLP_STATUS_E_INVALID;
    }

    if (serial) {
        strncpy(serial, sn, PSU_SERIAL_NUMBER_LEN+1);
    }

    AIM_FREE_IF_PTR(sn);

    return ONLP_STATUS_OK;
}

psu_type_t get_psu_type(int id, char* modelname, int modelname_len)
{
    int ret = 0;
    char *node = NULL;
    char  *model_name = NULL;
    char  *fan_dir = NULL;


    /* Check AC model name */
    node = (id == PSU1_ID) ? PSU1_AC_PMBUS_NODE(psu_mfr_model) : PSU2_AC_PMBUS_NODE(psu_mfr_model);
    ret = onlp_file_read_str(&model_name, node);
    if (ret <= 0 || ret > PSU_MODEL_NAME_LEN || model_name == NULL) {
        AIM_FREE_IF_PTR(model_name);
        return PSU_TYPE_UNKNOWN;
    }

    if (strncmp(model_name, "YM-2651Y", strlen("YM-2651Y")) != 0 &&
        strncmp(model_name, "FSF019", strlen("FSF019")) != 0) {
        return PSU_TYPE_UNKNOWN;
    }

    if (modelname) {
        strncpy(modelname, model_name, PSU_MODEL_NAME_LEN + 1);
    }


    node = (id == PSU1_ID) ? PSU1_AC_PMBUS_NODE(psu_fan_dir) : PSU2_AC_PMBUS_NODE(psu_fan_dir);
    ret = onlp_file_read_str(&fan_dir, node);
    if (ret <= 0 || ret > PSU_FAN_DIR_LEN || fan_dir == NULL) {
        AIM_FREE_IF_PTR(fan_dir);
        return PSU_TYPE_UNKNOWN;
    }

    if (strncmp(fan_dir, "F2B", strlen("F2B")) == 0) {
        return PSU_TYPE_AC_F2B;
    }

    if (strncmp(fan_dir, "B2F", strlen("B2F")) == 0) {
        return PSU_TYPE_AC_B2F;
    }

    AIM_FREE_IF_PTR(fan_dir);
    AIM_FREE_IF_PTR(model_name);

    return PSU_TYPE_UNKNOWN;
}

int psu_ym2651y_pmbus_info_get(int id, char *node, int *value)
{
    int  ret = 0;
    char path[PSU_NODE_MAX_PATH_LEN] = {0};

    *value = 0;

    if (PSU1_ID == id) {
        sprintf(path, "%s%s", PSU1_AC_PMBUS_PREFIX, node);
    }
    else {
        sprintf(path, "%s%s", PSU2_AC_PMBUS_PREFIX, node);
    }

    if (onlp_file_read_int(value, path) < 0) {
        AIM_LOG_ERROR("Unable to read status from file(%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ret;
}

int psu_ym2651y_pmbus_info_set(int id, char *node, int value)
{
    char path[PSU_NODE_MAX_PATH_LEN] = {0};

	switch (id) {
	case PSU1_ID:
		sprintf(path, "%s%s", PSU1_AC_PMBUS_PREFIX, node);
		break;
	case PSU2_ID:
		sprintf(path, "%s%s", PSU2_AC_PMBUS_PREFIX, node);
		break;
	default:
		return ONLP_STATUS_E_UNSUPPORTED;
	};

    if (onlp_file_write_int(value, path) < 0) {
        AIM_LOG_ERROR("Unable to write data to file (%s)\r\n", path);
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}
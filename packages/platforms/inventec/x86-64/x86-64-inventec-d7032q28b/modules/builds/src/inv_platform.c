#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <linux/i2c/pca954x.h>
#include <linux/platform_data/pca953x.h>
#include <linux/platform_data/at24.h>

#define IO_EXPAND_BASE    64
#define IO_EXPAND_NGPIO   16

struct inv_i2c_board_info {
    int ch;
    int size;
    struct i2c_board_info *board_info;
};

#define bus_id(id)  (id)
static struct pca954x_platform_mode mux_modes_0[] = {
    {.adap_id = bus_id(2),},    {.adap_id = bus_id(3),},
    {.adap_id = bus_id(4),},    {.adap_id = bus_id(5),},
};
static struct pca954x_platform_mode mux_modes_0_0[] = {
    {.adap_id = bus_id(6),},    {.adap_id = bus_id(7),},
    {.adap_id = bus_id(8),},    {.adap_id = bus_id(9),},
    {.adap_id = bus_id(10),},    {.adap_id = bus_id(11),},
    {.adap_id = bus_id(12),},    {.adap_id = bus_id(13),},
};

static struct pca954x_platform_mode mux_modes_0_1[] = {
    {.adap_id = bus_id(14),},    {.adap_id = bus_id(15),},
    {.adap_id = bus_id(16),},    {.adap_id = bus_id(17),},
    {.adap_id = bus_id(18),},    {.adap_id = bus_id(19),},
    {.adap_id = bus_id(20),},    {.adap_id = bus_id(21),},
};

static struct pca954x_platform_mode mux_modes_0_2[] = {
    {.adap_id = bus_id(22),},    {.adap_id = bus_id(23),},
    {.adap_id = bus_id(24),},    {.adap_id = bus_id(25),},
    {.adap_id = bus_id(26),},    {.adap_id = bus_id(27),},
    {.adap_id = bus_id(28),},    {.adap_id = bus_id(29),},
};

static struct pca954x_platform_mode mux_modes_0_3[] = {
    {.adap_id = bus_id(30),},    {.adap_id = bus_id(31),},
    {.adap_id = bus_id(32),},    {.adap_id = bus_id(33),},
    {.adap_id = bus_id(34),},    {.adap_id = bus_id(35),},
    {.adap_id = bus_id(36),},    {.adap_id = bus_id(37),},
};

static struct pca954x_platform_data mux_data_0 = {
        .modes          = mux_modes_0,
        .num_modes      = 4,
};
static struct pca954x_platform_data mux_data_0_0 = {
        .modes          = mux_modes_0_0,
        .num_modes      = 8,
};
static struct pca954x_platform_data mux_data_0_1 = {
        .modes          = mux_modes_0_1,
        .num_modes      = 8,
};
static struct pca954x_platform_data mux_data_0_2 = {
        .modes          = mux_modes_0_2,
        .num_modes      = 8,
};
static struct pca954x_platform_data mux_data_0_3 = {
        .modes          = mux_modes_0_3,
        .num_modes      = 8,
};

static struct i2c_board_info i2c_device_info0[] __initdata = {
        {"inv_psoc",         0, 0x66, 0, 0, 0},//psoc
        {"inv_cpld",         0, 0x55, 0, 0, 0},//cpld
        {"pca9545",          0, 0x70, &mux_data_0, 0, 0},	
};

static struct i2c_board_info i2c_device_info1[] __initdata = {
        {"pca9545",          0, 0x70, &mux_data_0, 0, 0},	
};

static struct i2c_board_info i2c_device_info2[] __initdata = {
        {"pca9548",         0, 0x72, &mux_data_0_0, 0, 0},	
};

static struct i2c_board_info i2c_device_info3[] __initdata = {
        {"pca9548",         0, 0x72, &mux_data_0_1, 0, 0},	
};

static struct i2c_board_info i2c_device_info4[] __initdata = {
        {"pca9548",         0, 0x72, &mux_data_0_2, 0, 0},	
};

static struct i2c_board_info i2c_device_info5[] __initdata = {
        {"pca9548",         0, 0x72, &mux_data_0_3, 0, 0},	
};


static struct inv_i2c_board_info i2cdev_list[] = {
    {0, ARRAY_SIZE(i2c_device_info0),  i2c_device_info0 },  //smbus 0
    {1, ARRAY_SIZE(i2c_device_info1),  i2c_device_info1 },  //smbus 1 or gpio11+12
    
    {bus_id(2), ARRAY_SIZE(i2c_device_info2),  i2c_device_info2 },  //mux 0
    {bus_id(3), ARRAY_SIZE(i2c_device_info3),  i2c_device_info3 },  //mux 1
    {bus_id(4), ARRAY_SIZE(i2c_device_info4),  i2c_device_info4 },  //mux 2
    {bus_id(5), ARRAY_SIZE(i2c_device_info5),  i2c_device_info5 },  //mux 3
};

static int __init plat_redwood_x86_init(void)
{
    struct i2c_adapter *adap = NULL;
    struct i2c_client *e = NULL;
    int ret = 0;
    int i,j;

    printk("el6661 plat_redwood_x86_init  \n");

    for(i=0; i<ARRAY_SIZE(i2cdev_list); i++) {
        
        adap = i2c_get_adapter( i2cdev_list[i].ch );
        if (adap == NULL) {
            printk("redwood_x86 get channel %d adapter fail\n", i);
            continue;
            return -ENODEV;
        }
    
        i2c_put_adapter(adap);
        for(j=0; j<i2cdev_list[i].size; j++) {
            e = i2c_new_device(adap, &i2cdev_list[i].board_info[j] );
        }
    }

    return ret;    
}

module_init(plat_redwood_x86_init);

MODULE_AUTHOR("Inventec");
MODULE_DESCRIPTION("Redwood_x86 Platform devices");
MODULE_LICENSE("GPL");
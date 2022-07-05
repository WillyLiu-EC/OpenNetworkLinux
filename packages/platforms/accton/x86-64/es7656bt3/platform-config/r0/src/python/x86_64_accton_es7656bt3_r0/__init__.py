from onl.platform.base import *
from onl.platform.accton import *

import commands


class OnlPlatform_x86_64_accton_es7656bt3_r0(OnlPlatformAccton,
                                              OnlPlatformPortConfig_48x25_8x100):

    PLATFORM='x86-64-accton-es7656bt3-r0'
    MODEL="ES7656BT3"
    SYS_OBJECT_ID=".7220.58"


    def baseconfig(self):
        os.system("modprobe i2c-ismt")
        os.system("modprobe pmbus_core")
        self.insmod('optoe')
        self.insmod('ym2651y')
        for m in [ 'cpld', 'fan', 'psu', 'leds' ]:
            self.insmod("x86-64-accton-es7656bt3-%s.ko" % m)

        self.new_i2c_devices([
                ('pca9548', 0x77, 1), #2-9
                ('pca9548', 0x70, 2), #10-17
                ('pca9548', 0x71, 2), #18-25
                ('pca9548', 0x72, 25), #34-41

                # initiate cpld
                ('es7656bt3_fan', 0x66, 12),
                ('es7656bt3_cpld1', 0x60, 19),
                ('es7656bt3_cpld2', 0x62, 13),
                ('es7656bt3_cpld3', 0x64, 20),

                # inititate LM75
                ('lm75', 0x48, 16),
                ('lm75', 0x49, 16),
                ('lm75', 0x4a, 16),
                ('lm75', 0x4b, 16),

                # initiate PSU-1
                ('es7656bt3_psu1', 0x51, 18),
                ('ym2651', 0x59, 18),

                # initiate PSU-2
                ('es7656bt3_psu2', 0x53, 14),
                ('ym2651', 0x5b, 14),
           ])
        ########### initialize I2C bus 1 ###########
        # initiate multiplexer (PCA9548)
        self.new_i2c_devices(
            [
                ('pca9548', 0x70, 3), # 26-33
                # initiate multiplexer (PCA9548)
                ('pca9548', 0x71, 34), #42-49
                ('pca9548', 0x72, 35), #50-57
                ('pca9548', 0x73, 36), #58-65
                ('pca9548', 0x74, 37), #66-73
                ('pca9548', 0x75, 38), #74-81
                ('pca9548', 0x76, 39), #82-89
                ]
            )

        sfp_map =  [
        43,42,45,44,48,46,47,51,
        49,50,53,52,54,57,56,55,
        59,58,61,60,62,64,63,65,
        67,69,66,68,70,72,73,71,
        75,74,77,76,78,80,79,81,
        82,83,85,86,84,88,89,87,    #port 41~48
        26,27,28,29,30,31,32,33,    #port 49~56 QSFP
        23,24]                      #port 57~58 SFP+ from CPU NIF.

        # initialize SFP+ port 1~56 and 57+58.
        for port in range(1, 49):
            bus = sfp_map[port-1]
            self.new_i2c_device('optoe2', 0x50, bus)

        self.new_i2c_device('optoe2', 0x50, sfp_map[57-1])
        self.new_i2c_device('optoe2', 0x50, sfp_map[58-1])

        # initialize QSFP port 49~56
        for port in range(49, 57):
            bus = sfp_map[port-1]
            self.new_i2c_device('optoe1', 0x50, bus)

        for port in range(1, len(sfp_map)):
            bus = sfp_map[port-1]
            subprocess.call('echo port%d > /sys/bus/i2c/devices/%d-0050/port_name' % (port, bus), shell=True)

        # initiate IDPROM
        self.new_i2c_device('24c02', 0x57, 1)

        return True

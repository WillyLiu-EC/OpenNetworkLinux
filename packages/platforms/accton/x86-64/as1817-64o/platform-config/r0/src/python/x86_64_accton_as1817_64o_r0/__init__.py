import commands
import time
from itertools import chain
from onl.platform.base import *
from onl.platform.accton import *

init_ipmi_dev = [
    'echo "remove,kcs,i/o,0xca2" > /sys/module/ipmi_si/parameters/hotmod',
    'echo "add,kcs,i/o,0xca2" > /sys/module/ipmi_si/parameters/hotmod']

ATTEMPTS = 5
INTERVAL = 3

def init_ipmi_dev_intf():
    attempts = ATTEMPTS
    interval = INTERVAL

    while attempts:
        if os.path.exists('/dev/ipmi0') or os.path.exists('/dev/ipmidev/0'):
            return (True, (ATTEMPTS - attempts) * interval)

        for i in range(0, len(init_ipmi_dev)):
            commands.getstatusoutput(init_ipmi_dev[i])

        attempts -= 1
        time.sleep(interval)

    return (False, ATTEMPTS * interval)

def init_ipmi_oem_cmd():
    attempts = ATTEMPTS
    interval = INTERVAL

    while attempts:
        status, output = commands.getstatusoutput('ipmitool raw 0x34 0x95')
        if status:
            attempts -= 1
            time.sleep(interval)
            continue

        return (True, (ATTEMPTS - attempts) * interval)

    return (False, ATTEMPTS * interval)

def init_ipmi():
    attempts = ATTEMPTS
    interval = 60

    while attempts:
        attempts -= 1

        (status, elapsed_dev) = init_ipmi_dev_intf()
        if status is not True:
            time.sleep(interval - elapsed_dev)
            continue

        (status, elapsed_oem) = init_ipmi_oem_cmd()
        if status is not True:
            time.sleep(interval - elapsed_dev - elapsed_oem)
            continue

        print('IPMI dev interface is ready.')
        return True

    print('Failed to initialize IPMI dev interface')
    return False

class OnlPlatform_x86_64_accton_as1817_64o_r0(OnlPlatformAccton,
                                              OnlPlatformPortConfig_64x800_2x25):
    PLATFORM='x86-64-accton-as1817-64o-r0'
    MODEL="AS1817-64O"
    SYS_OBJECT_ID=".1817.64.1"

    def baseconfig(self):
        if init_ipmi() is not True:
            return False

        self.insmod('kernel/drivers/i2c/busses/i2c-ismt')
        self.insmod('optoe')
        self.insmod('x86-64-accton_ipmi_intf')
        self.insmod('x86-64-accton-as1817-64o-i2c-ocores')
        self.insmod('x86-64-accton-as1817-64o-fpga')
        self.insmod('x86-64-accton-as1817-64o-xcvr')
        subprocess.call('echo "10ee 7021" > /sys/bus/pci/drivers/as1817_64o_fpga/new_id',  shell=True)
        for m in [ 'fan', 'psu', 'leds', 'thermal', 'sys']:
            self.insmod("x86-64-accton-as1817-64o-%s.ko" % m)

        for port in range(2, 66):
            self.new_i2c_device('optoe3', 0x50, port)
            subprocess.call('echo port%d > /sys/bus/i2c/devices/%d-0050/port_name' % (port - 1, port), shell=True)
        for port in range(66, 68):
            self.new_i2c_device('optoe2', 0x50, port)
            subprocess.call('echo port%d > /sys/bus/i2c/devices/%d-0050/port_name' % (port - 1, port), shell=True)

        return True

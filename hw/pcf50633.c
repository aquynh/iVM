/*
 * PCF50633 emulation for S5L8900
 * written by cmw
 */

#include "sysemu.h"
#include "sysbus.h"
#include "smbus.h"
#include "s5l8900.h"
#include "qemu-timer.h"


typedef struct pcf50633State {
    SMBusDevice smbusdev;
	uint32_t cmd;
} pcf50633State;

static void pcf50633_reset(void *opaque)
{
	return;

}

static void pcf50633_write_data(SMBusDevice *dev, uint8_t cmd,
                                uint8_t *buf, int len)
{
    //fprintf(stderr, "%s: cmd 0x%08x len 0x%08x\n", __func__, cmd, len);

    switch (cmd) {
    default:
       // hw_error("pcf50633: bad write offset 0x%x\n", cmd);
		break;
    }

}

static uint8_t pcf50633_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
	//fprintf(stderr, "%s: cmd 0x%08x n 0x%08x\n", __func__, cmd, n);
    switch (cmd) {
    default:
        //hw_error("pcf50633: bad read offset 0x%x\n", cmd);
		break;
    }

    return 0;
}

static void pcf50633_quick_cmd(SMBusDevice *dev, uint8_t read)
{
    fprintf(stderr, "%s: addr=0x%02x read=%d\n", __func__, dev->i2c.address, read);
}

static void pcf50633_send_byte(SMBusDevice *dev, uint8_t val)
{
	pcf50633State *s = (pcf50633State *)dev;

    //fprintf(stderr, "%s: addr=%02x val=%02x\n",__func__, dev->i2c.address, val);

	s->cmd=val;
	
}

static uint8_t pcf50633_receive_byte(SMBusDevice *dev)
{
	pcf50633State *s = (pcf50633State *)dev;

    //fprintf(stderr, "%s: addr=0x%02x cmd=0x%02x\n",__func__, dev->i2c.address, s->cmd);

	switch(s->cmd) {
		case BUTTONS_IIC_STATE:
			return 0xff;
	  default:
		return 0;
	}
}

DeviceState *pcf50633_init(i2c_bus *bus, int addr)
{
    DeviceState *dev = qdev_create((BusState *)bus, "pcf50633");

	fprintf(stderr, "Registering pcf50633\n");
    qdev_init_nofail(dev);
    i2c_set_slave_address((i2c_slave *)dev, addr);
    return dev;
}

static int pcf50633_init1(SMBusDevice *dev)
{
    pcf50633State *s = (pcf50633State *) dev;
    pcf50633_reset(s);
    qemu_register_reset(pcf50633_reset, s);

    return 0;
}

static SMBusDeviceInfo pcf50633_info = {
    .i2c.qdev.name = "pcf50633",
    .i2c.qdev.size = sizeof(pcf50633State),
    .init = pcf50633_init1,
    .quick_cmd = pcf50633_quick_cmd,
	.send_byte = pcf50633_send_byte,
	.receive_byte = pcf50633_receive_byte,
    .write_data = pcf50633_write_data,
    .read_data = pcf50633_read_data
};

static void s5l8900_pmu_register_devices(void)
{
    smbus_register_device(&pcf50633_info);
}

device_init(s5l8900_pmu_register_devices)

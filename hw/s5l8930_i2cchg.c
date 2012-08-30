/*
 * iPad charger i2c emulation for S5L8930
 * written by cmw
 */

#include "sysemu.h"
#include "sysbus.h"
#include "smbus.h"
#include "s5l8930.h"
#include "qemu-timer.h"


typedef struct ipadchgState {
    SMBusDevice smbusdev;
	uint32_t cmd;
} ipadchgState;

static void ipadchg_reset(void *opaque)
{
	return;

}

static void ipadchg_write_data(SMBusDevice *dev, uint8_t cmd,
                                uint8_t *buf, int len)
{
    fprintf(stderr, "%s: cmd 0x%08x len 0x%08x\n", __func__, cmd, len);

    switch (cmd) {
    default:
       // hw_error("ipadchg: bad write offset 0x%x\n", cmd);
		break;
    }

}

static uint8_t ipadchg_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
	fprintf(stderr, "%s: cmd 0x%08x n 0x%08x\n", __func__, cmd, n);
    switch (cmd) {
    default:
        //hw_error("ipadchg: bad read offset 0x%x\n", cmd);
		break;
    }

    return 0;
}

static void ipadchg_quick_cmd(SMBusDevice *dev, uint8_t read)
{
    fprintf(stderr, "%s: addr=0x%02x read=%d\n", __func__, dev->i2c.address, read);
}

static void ipadchg_send_byte(SMBusDevice *dev, uint8_t val)
{
	ipadchgState *s = (ipadchgState *)dev;

    fprintf(stderr, "%s: addr=%02x val=%02x\n",__func__, dev->i2c.address, val);

	s->cmd=val;
	
}

static uint8_t ipadchg_receive_byte(SMBusDevice *dev)
{
	ipadchgState *s = (ipadchgState *)dev;

    fprintf(stderr, "%s: addr=0x%02x cmd=0x%02x\n",__func__, dev->i2c.address, s->cmd);

	return 0xff;

	switch(s->cmd) {
	  default:
		return 0;
	}
}

DeviceState *ipadchg_init(i2c_bus *bus, int addr)
{
    DeviceState *dev = qdev_create((BusState *)bus, "ipadchg");

	fprintf(stderr, "Registering ipadchg\n");
    qdev_init_nofail(dev);
    i2c_set_slave_address((i2c_slave *)dev, addr);
    return dev;
}

static int ipadchg_init1(SMBusDevice *dev)
{
    ipadchgState *s = (ipadchgState *) dev;
    ipadchg_reset(s);
    qemu_register_reset(ipadchg_reset, s);

    return 0;
}

static SMBusDeviceInfo ipadchg_info = {
    .i2c.qdev.name = "ipadchg",
    .i2c.qdev.size = sizeof(ipadchgState),
    .init = ipadchg_init1,
    .quick_cmd = ipadchg_quick_cmd,
	.send_byte = ipadchg_send_byte,
	.receive_byte = ipadchg_receive_byte,
    .write_data = ipadchg_write_data,
    .read_data = ipadchg_read_data
};

static void s5l8930_ipadchg_register_devices(void)
{
    smbus_register_device(&ipadchg_info);
}

device_init(s5l8930_ipadchg_register_devices)

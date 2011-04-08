/* 
 * iPhone2g Radio Emulation
 *
 */

#include "hw.h"
#include "qemu-char.h"
#include "pc.h"
#include "sysemu.h"

#include "iphone2g.h"

typedef struct iphone2g_radio {

} iphone2g_radio_s;

static void serial_init_core(SerialState *s)
{
    if (!s->chr) {
        fprintf(stderr, "Can't create radio device, empty char device\n");
    	exit(1);
    }

    qemu_register_reset(radio_reset, s);

    qemu_chr_add_handlers(s->chr, serial_can_receive1, serial_receive1,
                          serial_event, s);
}

static void iphone2g_radio_init()
{
    DeviceState *dev = qdev_create(NULL, "s5l8900.uart");
    char str[] = "s5l8900.uart.00";
    S5L8900UartState *s = FROM_SYSBUS(S5L8900UartState, dev);

    s->base = base;

    if (!chr) {
        fprintf(stderr, "openning char device");
        snprintf(str, strlen(str) + 1, "s5l8900.uart.%02d", instance % 100);
        chr = qemu_chr_open(str, "null", NULL);
    }
    qdev_prop_set_chr(dev, "chr", chr);
    qdev_prop_set_uint32(dev, "queue-size", queue_size);
    qdev_prop_set_uint32(dev, "instance", instance);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

static ISADeviceInfo serial_isa_info = {
    .qdev.name  = "iphone2g_radio",
    .qdev.size  = sizeof(iphone2g_radio_s),
    .init       = iphone2g_radio_initfn,
};

static void radio_register_devices(void)
{
}

device_init(radio_register_devices)


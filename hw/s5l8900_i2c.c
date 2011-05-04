/*
 * I2C controller for S5l8900
 * 
 * Hacked to bits by cmw
 * Based on worked by
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Vladimir Monakhov <vladimir.monakhov@ispras.ru>
 *                Alexey Merkulov <steelart@ispras.ru>
 *
 * Based on SMDK6400 I2C (hw/smdk6400/smdk_i2c.c)
 */

#include "i2c.h"
#include "sysbus.h"


#define I2CCON        0x00      /* I2C Control register */
#define I2CSTAT       0x04      /* I2C Status register */
#define I2CADD        0x08      /* I2C Slave Address register */
#define I2CDS         0x0c      /* I2C Data Shift register */
#define I2CLC         0x10      /* I2C Line Control register */

#define IICREG20	  0x20

#define SR_MODE       0x0       /* Slave Receive Mode */
#define ST_MODE       0x1       /* Slave Transmit Mode */
#define MR_MODE       0x2       /* Master Receive Mode */
#define MT_MODE       0x3       /* Master Transmit Mode */


#define S5L8900_IICCON_ACKEN        (1<<7)
#define S5L8900_IICCON_TXDIV_16     (0<<6)
#define S5L8900_IICCON_TXDIV_512    (1<<6)
#define S5L8900_IICCON_IRQEN        (1<<5)
#define S5L8900_IICCON_IRQPEND      (1<<4)

#define S5L8900_IICSTAT_START       (1<<5)
#define S5L8900_IICSTAT_BUSBUSY     (1<<5)
#define S5L8900_IICSTAT_TXRXEN      (1<<4)
#define S5L8900_IICSTAT_ARBITR      (1<<3)
#define S5L8900_IICSTAT_ASSLAVE     (1<<2)
#define S5L8900_IICSTAT_ADDR0       (1<<1)
#define S5L8900_IICSTAT_LASTBIT     (1<<0)

#define S5L8900_I2C_REG_MEM_SIZE    0x1000


/* I2C Interface */
typedef struct S5L8900I2CState {
    SysBusDevice busdev;

    i2c_bus *bus;
    qemu_irq irq;

    uint8_t control;
    uint8_t status;
    uint8_t address;
    uint8_t datashift;
    uint8_t line_ctrl;
	uint32_t iicreg20;

	uint8_t active;

    uint8_t ibmr;
    uint8_t data;
} S5L8900I2CState;


static void s5l8900_i2c_update(S5L8900I2CState *s)
{
    uint16_t level;
    level = (s->status & S5L8900_IICSTAT_START) &&
            (s->control & S5L8900_IICCON_IRQEN);

    if (s->control & S5L8900_IICCON_IRQPEND)
        level = 0;
    //qemu_set_irq(s->irq, !!level);
}

static int s5l8900_i2c_receive(S5L8900I2CState *s)
{
    int r;
    r = i2c_recv(s->bus);
    s5l8900_i2c_update(s);
    return r;
}

static int s5l8900_i2c_send(S5L8900I2CState *s, uint8_t data)
{
    if (!(s->status & S5L8900_IICSTAT_LASTBIT)) {
        s->status |= S5L8900_IICCON_ACKEN;
        s->data = data;
		s->iicreg20 |= 0x100;
        i2c_send(s->bus, s->data);
    }
    s5l8900_i2c_update(s);
    return 1;
}

/* I2C read function */
static uint32_t s5l8900_i2c_read(void *opaque, target_phys_addr_t offset)
{
    S5L8900I2CState *s = (S5L8900I2CState *)opaque;

    //fprintf(stderr, "s5l8900_i2c_read(): offset = 0x%08x\n", offset);


    switch (offset) {
    case I2CCON:
        return s->control;
    case I2CSTAT:
        return s->status;
    case I2CADD:
        return s->address;
    case I2CDS:
		s->iicreg20 |= 0x100;
        s->data = s5l8900_i2c_receive(s);
        return s->data;
    case I2CLC:
        return s->line_ctrl;
    case IICREG20:
		{
     		uint32_t tmp_reg20 = s->iicreg20; 
      		s->iicreg20 &= ~0x100; 
      		s->iicreg20 &= ~0x2000; 
			return tmp_reg20; 
		}
    default:
        //hw_error("s5l8900.i2c: bad read offset 0x" TARGET_FMT_plx "\n", offset);
		fprintf(stderr, "%s: bad read offset 0x%08x\n", __func__, offset);
    }
    return 0;
}

/* I2C write function */
static void s5l8900_i2c_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5L8900I2CState *s = (S5L8900I2CState *)opaque;
    int mode;

    //fprintf(stderr, "s5l8900_i2c_write: offset = 0x%08x, val = 0x%08x\n", offset, value);

    qemu_irq_lower(s->irq);

    switch (offset) {
    case I2CCON:
		if(value & ~(S5L8900_IICCON_ACKEN)) {
			s->iicreg20 |= 0x100;
		}
		if((value & 0x10) && (s->status == 0x90))  {
			s->iicreg20 |= 0x2000;
		}
		s->control = value & 0xff;
        if (value & S5L8900_IICCON_IRQEN)
            s5l8900_i2c_update(s);
        break;

    case I2CSTAT:
		/* We have to make sure we don't miss an end transfer */
		if((!s->active) && ((s->status >> 6) != ((value >> 6)))) {
        	s->status = value & 0xff;
		/* If they toggle the tx bit then we have to force an end transfer before mode update */
		} else if((s->active) && ((s->status >> 6) != ((value >> 6)))) {
                  	i2c_end_transfer(s->bus);
                    s->active=0;
                    s->status = value & 0xff;
                    s->status |= S5L8900_IICSTAT_TXRXEN;
					break;
		}
        mode = (s->status >> 6) & 0x3;
        if (value & S5L8900_IICSTAT_TXRXEN) {
            /* IIC-bus data output enable/disable bit */
            switch(mode) {
            case SR_MODE:
                s->data = s5l8900_i2c_receive(s);
                break;
            case ST_MODE:
                s->data = s5l8900_i2c_receive(s);
                break;
            case MR_MODE:
                if (value & S5L8900_IICSTAT_START) {
                    /* START condition */
                    s->status &= ~S5L8900_IICSTAT_LASTBIT;

                    s->iicreg20 |= 0x100;
					s->active = 1;
                    i2c_start_transfer(s->bus, s->data >> 1, 1);
                } else {
                    i2c_end_transfer(s->bus);
					s->active = 0;
                    s->status |= S5L8900_IICSTAT_TXRXEN;
                }
                break;
            case MT_MODE:
				if (value & S5L8900_IICSTAT_START) {
                    /* START condition */
                    s->status &= ~S5L8900_IICSTAT_LASTBIT;
						
                    s->iicreg20 |= 0x100;
					s->active = 1;
                    i2c_start_transfer(s->bus, s->data >> 1, 0);
                } else {
                    i2c_end_transfer(s->bus);
					s->active = 0;
                    s->status |= S5L8900_IICSTAT_TXRXEN;
                }
                break;
            default:
                break;
            }
        }
        s5l8900_i2c_update(s);
        break;

    case I2CADD:
        s->address = value & 0xff;
        break;

    case I2CDS:
        s5l8900_i2c_send(s, value & 0xff);
        break;

    case I2CLC:
        s->line_ctrl = value & 0xff;
        break;

	case IICREG20:
		//s->iicreg20 &= ~value;
		break;
    default:
        //hw_error("s5l8900.i2c: bad write offset 0x" TARGET_FMT_plx "\n", offset);
		fprintf(stderr, "%s: bad write offset 0x%08x\n", __func__, offset);
    }
}

static CPUReadMemoryFunc * const s5l8900_i2c_readfn[] = {
    s5l8900_i2c_read,
    s5l8900_i2c_read,
    s5l8900_i2c_read
};

static CPUWriteMemoryFunc * const s5l8900_i2c_writefn[] = {
    s5l8900_i2c_write,
    s5l8900_i2c_write,
    s5l8900_i2c_write
};

static void s5l8900_i2c_save(QEMUFile *f, void *opaque)
{
    S5L8900I2CState *s = (S5L8900I2CState *)opaque;

    qemu_put_8s(f, &s->control);
    qemu_put_8s(f, &s->status);
    qemu_put_8s(f, &s->address);
    qemu_put_8s(f, &s->datashift);
    qemu_put_8s(f, &s->line_ctrl);
    qemu_put_8s(f, &s->ibmr);
    qemu_put_8s(f, &s->data);
}

static int s5l8900_i2c_load(QEMUFile *f, void *opaque, int version_id)
{
    S5L8900I2CState *s = (S5L8900I2CState *)opaque;

    if (version_id != 1) {
        return -EINVAL;
    }

    qemu_get_8s(f, &s->control);
    qemu_get_8s(f, &s->status);
    qemu_get_8s(f, &s->address);
    qemu_get_8s(f, &s->datashift);
    qemu_get_8s(f, &s->line_ctrl);
    qemu_get_8s(f, &s->ibmr);
    qemu_get_8s(f, &s->data);

    return 0;
}

/* I2C init */
static int s5l8900_i2c_init(SysBusDevice *dev)
{
    int iomemtype;
    S5L8900I2CState *s = FROM_SYSBUS(S5L8900I2CState, dev);

    sysbus_init_irq(dev, &s->irq);
    s->bus = i2c_init_bus(&dev->qdev, "i2c");

    iomemtype =
        cpu_register_io_memory(s5l8900_i2c_readfn, s5l8900_i2c_writefn, s, DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, S5L8900_I2C_REG_MEM_SIZE, iomemtype);

    register_savevm(&dev->qdev, "s5l8900.i2c", -1, 1,
                    s5l8900_i2c_save, s5l8900_i2c_load, s);

    return 0;
}

static void s5l8900_i2c_register(void)
{
    sysbus_register_dev("s5l8900.i2c", sizeof(S5L8900I2CState),
                        s5l8900_i2c_init);
}

device_init(s5l8900_i2c_register)

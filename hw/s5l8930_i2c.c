/*
 * I2C controller for S5l8930
 * by cmw
 * 
 */

#include "i2c.h"
#include "sysbus.h"
#include "qemu-common.h"


#define I2CADD        0x00      /* I2C Slave Address register */
#define I2CSTAT       0x0c      /* I2C Status register */
#define I2CEP         0x10      /* I2C EP register */
#define I2CBL		  0x18      /* I2C Data Shift len register */
#define I2CDS         0x20      /* I2C Data Shift register */
#define I2CCON        0x24      /* I2C Control register */


#define IICREG8		  0x8
#define IICREG14      0x14

#define SR_MODE       0x0       /* Slave Receive Mode */
#define ST_MODE       0x1       /* Slave Transmit Mode */
#define MR_MODE       0x2       /* Master Receive Mode */
#define MT_MODE       0x3       /* Master Transmit Mode */


#define S5L8930_IICCON_ACKEN        (1<<7)
#define S5L8930_IICCON_TXDIV_16     (0<<6)
#define S5L8930_IICCON_TXDIV_512    (1<<6)
#define S5L8930_IICCON_IRQEN        (1<<5)
#define S5L8930_IICCON_IRQPEND      (1<<4)

#define S5L8930_IICSTAT_START       (1<<5)
#define S5L8930_IICSTAT_BUSBUSY     (1<<5)
#define S5L8930_IICSTAT_TXRXEN      (1<<4)
#define S5L8930_IICSTAT_ARBITR      (1<<3)
#define S5L8930_IICSTAT_ASSLAVE     (1<<2)
#define S5L8930_IICSTAT_ADDR0       (1<<1)
#define S5L8930_IICSTAT_LASTBIT     (1<<0)

#define S5L8930_I2C_REG_MEM_SIZE    0x1000


/* I2C Interface */
typedef struct S5L8930I2CState {
    SysBusDevice busdev;

    i2c_bus *bus;
    qemu_irq irq;

    uint8_t control;
    uint8_t status;	
	uint8_t epr;
    uint8_t address;
    uint8_t datashift;
    uint8_t line_ctrl;
	uint32_t iicreg20;

	uint32_t i2cnum;
	uint8_t active;

    uint8_t ibmr;
    uint8_t data;
} S5L8930I2CState;


static void s5l8930_i2c_update(S5L8930I2CState *s)
{
	/*
    uint16_t level;
    level = (s->status & S5L8930_IICSTAT_START) &&
            (s->control & S5L8930_IICCON_IRQEN);

    if (s->control & S5L8930_IICCON_IRQPEND)
        level = 0;
    //qemu_set_irq(s->irq, !!level);
	*/
	s->status = 0x1;
	fprintf(stderr, "s5l8930_i2c_update: irq triggered i2cnum %d", s->i2cnum);

	// for now always raise irq but we should fix irqen
	qemu_irq_raise(s->irq);
}

static int s5l8930_i2c_receive(S5L8930I2CState *s)
{
    int r;
    r = i2c_recv(s->bus);
    s5l8930_i2c_update(s);
    return r;
}

static int s5l8930_i2c_send(S5L8930I2CState *s, uint8_t data)
{
/*
    if (!(s->status & S5L8930_IICSTAT_LASTBIT)) {
        s->status |= S5L8930_IICCON_ACKEN;
        s->data = data;
		s->iicreg20 |= 0x100;
        i2c_send(s->bus, s->data);
    }
*/
	// should figure out which is IIC stat bit :XXX fix me
	i2c_send(s->bus, s->data);
    s5l8930_i2c_update(s);
    return 1;
}

/* I2C read function */
static uint32_t s5l8930_i2c_read(void *opaque, target_phys_addr_t offset)
{
    S5L8930I2CState *s = (S5L8930I2CState *)opaque;

    fprintf(stderr, "s5l8930_i2c_read(): offset = 0x%08x\n", offset);

	// We shouldnt lower every time but for now this works :XXX fix me
    qemu_irq_lower(s->irq);


    switch (offset) {
    case I2CCON:
        return s->control;
    case I2CSTAT:
        return s->status;
    case I2CADD:
        return s->address;
    case I2CDS:
        return s->data;
	case I2CEP:
		return s->epr;
	case IICREG14:
		return 0;
    default:
        fprintf(stderr, "s5l8930.i2c: bad read offset 0x%08x\n", offset);
        //hw_error("s5l8930.i2c: bad read offset 0x" TARGET_FMT_plx "\n", offset);
    }
    return 0;
}

/* I2C write function */
static void s5l8930_i2c_write(void *opaque, target_phys_addr_t offset,
                              uint32_t value)
{
    S5L8930I2CState *s = (S5L8930I2CState *)opaque;
    int mode;

    fprintf(stderr, "s5l8930_i2c_write: offset = 0x%08x, val = 0x%08x\n", offset, value);

    switch (offset) {
    case I2CCON:
		if(value & 0x1) 
		{
			//fprintf(stderr, "I2C: got write transfer\n");
			i2c_start_transfer(s->bus, s->address, 0);
			s5l8930_i2c_send(s, s->data);
		} else {
			//fprintf(stderr, "I2C: got read transfer from \n");
			i2c_start_transfer(s->bus, s->address, 1);
			s->data = s5l8930_i2c_receive(s);
		}
		i2c_end_transfer(s->bus);
        break;
    case I2CSTAT:
		qemu_irq_lower(s->irq);
		s->status = 0;
		break;
    case I2CADD:
        s->address = value & 0xff;
        break;
    case I2CDS:
		s->data = value;
        break;
	case IICREG14:
		break;
	case I2CEP:
		break;
    default:
		fprintf(stderr, "s5l8930.i2c: bad write offset 0x%08x\n", offset);
        //hw_error("s5l8930.i2c: bad write offset 0x" TARGET_FMT_plx "\n", offset);
    }
}

static CPUReadMemoryFunc * const s5l8930_i2c_readfn[] = {
    s5l8930_i2c_read,
    s5l8930_i2c_read,
    s5l8930_i2c_read
};

static CPUWriteMemoryFunc * const s5l8930_i2c_writefn[] = {
    s5l8930_i2c_write,
    s5l8930_i2c_write,
    s5l8930_i2c_write
};

static void s5l8930_i2c_save(QEMUFile *f, void *opaque)
{
    S5L8930I2CState *s = (S5L8930I2CState *)opaque;

    qemu_put_8s(f, &s->control);
    qemu_put_8s(f, &s->status);
    qemu_put_8s(f, &s->address);
    qemu_put_8s(f, &s->datashift);
    qemu_put_8s(f, &s->line_ctrl);
    qemu_put_8s(f, &s->ibmr);
    qemu_put_8s(f, &s->data);
}

static int s5l8930_i2c_load(QEMUFile *f, void *opaque, int version_id)
{
    S5L8930I2CState *s = (S5L8930I2CState *)opaque;

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

static int i2cnum;
void seti2cnum(int num)
{
	i2cnum = num;
}

/* I2C init */
static int s5l8930_i2c_init(SysBusDevice *dev)
{
    int iomemtype;
    S5L8930I2CState *s = FROM_SYSBUS(S5L8930I2CState, dev);

    sysbus_init_irq(dev, &s->irq);
    s->bus = i2c_init_bus(&dev->qdev, "i2c");
	s->i2cnum = i2cnum;
    iomemtype =
        cpu_register_io_memory(s5l8930_i2c_readfn, s5l8930_i2c_writefn, s, DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, S5L8930_I2C_REG_MEM_SIZE, iomemtype);

    register_savevm(&dev->qdev, "s5l8930.i2c", -1, 1,
                    s5l8930_i2c_save, s5l8930_i2c_load, s);

    return 0;
}

static void s5l8930_i2c_register(void)
{
    sysbus_register_dev("s5l8930.i2c", sizeof(S5L8930I2CState),
                        s5l8930_i2c_init);
}

device_init(s5l8930_i2c_register)

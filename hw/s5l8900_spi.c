/*
 * S5L8900 SPI Emulation
 *
 * by cmw
 */

#include "sysbus.h"
#include "s5l8900.h"

#define S5L8900_WDT_REG_MEM_SIZE 0x30

#define SPI_CONTROL 0
#define SPI_SETUP 0x4 
#define SPI_STATUS 0x8
#define SPI_PIN 0xc 
#define SPI_TXDATA 0x10
#define SPI_RXDATA 0x20
#define SPI_CLKDIV 0x30
#define SPI_CNT 0x34
#define SPI_IDD 0x38


typedef struct S5L8900SPIState {
    SysBusDevice busdev;

	uint32_t cmd;
	uint32_t base;
	uint32_t ctrl;
	uint32_t setup;
	uint32_t status;
	uint32_t pin;
	uint32_t tx_data;
	uint32_t rx_data;
	uint32_t clkdiv;
	uint32_t cnt;
	uint32_t idd;

    qemu_irq irq;
} S5L8900SPIState;


static uint32_t s5l8900_spi_mm_read(void *opaque, target_phys_addr_t offset)
{
    S5L8900SPIState *s = (S5L8900SPIState *)opaque;

	//fprintf(stderr, "%s: base 0x%08x offset 0x%08x\n", __func__, s->base, offset);

    switch (offset) {
    case SPI_CONTROL:
		return s->ctrl;
    case SPI_SETUP:
        return s->setup;
    case SPI_STATUS:
        return s->status;
    case SPI_PIN:
        return s->pin;
    case SPI_TXDATA:
        return s->tx_data;
    case SPI_RXDATA:
		//fprintf(stderr, "%s: s->cmd 0x%08x\n", __func__, s->cmd);
		switch(s->cmd) {
			case 0x95:
				return 1;
			case 0xDA:
				return 0x71;
			case 0xDB:
				return 0xC2;
			case 0xDC:
				return 0x00;
		  default:
			return 0;
		}
        return s->rx_data;
    case SPI_CLKDIV:
        return s->clkdiv;
    case SPI_CNT:
        return s->cnt;
    case SPI_IDD:
        return s->idd;
    default:
        hw_error("s5l8900_spi: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static void s5l8900_spi_mm_write(void *opaque, target_phys_addr_t offset,
                                 uint32_t val)
{
    S5L8900SPIState *s = (S5L8900SPIState *)opaque;

    //fprintf(stderr, "%s: base 0x%08x offset 0x%08x value 0x%08x\n", __func__, s->base, offset, val);

    switch (offset) {
    case SPI_CONTROL:
		if(val & 0x1) {
				s->status |= 0xff2;
				s->cmd = s->tx_data;
	    		qemu_irq_raise(s->irq);
		}
        break;
    case SPI_SETUP:
        s->setup = val;
        break;
    case SPI_STATUS:
		qemu_irq_lower(s->irq); 
        s->status = 0;
        break;
    case SPI_PIN:
        s->pin = val;
        break;
    case SPI_TXDATA:
        s->tx_data = val;
        break;
    case SPI_RXDATA:
        s->rx_data = val;
        break;
    case SPI_CLKDIV:
        s->clkdiv = val;
        break;
    case SPI_CNT:
        s->cnt = val;
        break;
    case SPI_IDD:
        s->idd = val;
        break;
    default:
        hw_error("s5l8900_spi: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

CPUReadMemoryFunc * const s5l8900_spi_readfn[] = {
    s5l8900_spi_mm_read,
    s5l8900_spi_mm_read,
    s5l8900_spi_mm_read
};

CPUWriteMemoryFunc * const s5l8900_spi_writefn[] = {
    s5l8900_spi_mm_write,
    s5l8900_spi_mm_write,
    s5l8900_spi_mm_write
};

static void s5l8900_spi_reset(void *opaque)
{
    S5L8900SPIState *s = (S5L8900SPIState *)opaque;

	s->cmd = 0;
	s->ctrl = 0;
	s->setup = 0;
	s->status = 0;
	s->pin = 0;
	s->tx_data = 0;
	s->rx_data = 0;
	s->clkdiv = 0;
	s->cnt = 0;
	s->idd = 0;
}

static uint32_t base_addr = 0;

void set_spi_base(uint32_t base)
{
	base_addr = base;
}

static int s5l8900_spi_init(SysBusDevice *dev)
{
    int iomemtype;
    S5L8900SPIState *s = FROM_SYSBUS(S5L8900SPIState, dev);

    sysbus_init_irq(dev, &s->irq);

    iomemtype = cpu_register_io_memory(s5l8900_spi_readfn, s5l8900_spi_writefn, s, DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, S5L8900_WDT_REG_MEM_SIZE, iomemtype);

	s->base = base_addr;
    s5l8900_spi_reset(s);

    qemu_register_reset(s5l8900_spi_reset, s);

    return 0;
}

static void s5l8900_spi_register_devices(void)
{
    sysbus_register_dev("s5l8900.spi", sizeof(S5L8900SPIState),
                        s5l8900_spi_init);
}

device_init(s5l8900_spi_register_devices)

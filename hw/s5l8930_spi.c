/*
 * S5L8930 SPI Emulation
 *
 * by cmw
 */

#include "sysbus.h"
#include "block.h"
#include "blockdev.h"
#include "flash.h"
#include "qemu-timer.h"

#include "s5l8930.h"

#define S5L8930_WDT_REG_MEM_SIZE 0x38

#define SPI_CONTROL 0
#define SPI_SETUP 0x4 
#define SPI_STATUS 0x8
#define SPI_PIN 0xc 
#define SPI_TXDATA 0x10
#define SPI_RXDATA 0x20
#define SPI_CLKDIV 0x30
#define SPI_CNT 0x34
#define SPI_IDD 0x38


typedef struct S5L8930SPIState {
    SysBusDevice busdev;
    qemu_irq irq;

	uint32_t cmd;
	uint32_t base;
	uint32_t ctrl;
	uint32_t setup;
	uint32_t status;
	uint32_t pin;
	uint32_t tx_data[10];
	uint32_t rx_data;
	uint32_t clkdiv;
	uint32_t cnt;
	uint32_t idd;
	
	// needs to be cleaner :(
	uint32_t txBuffer;
	uint32_t rxBuffer;
	uint32_t rxFifoCnt;
    QEMUTimer *timer;
	void *pflash;

} S5L8930SPIState;


static void spi_timer (void *opaque)
{
    S5L8930SPIState *s = (S5L8930SPIState *)opaque;

	if(s->rxFifoCnt == 0x1e)
	{
		s->rxFifoCnt = 0;
		s->status |= 1;
		//fprintf(stderr, "%s: base 0x%08x fifo queue empty.. triggering irq\n", __FUNCTION__, s->base);
		qemu_irq_lower(s->irq);
		qemu_irq_raise(s->irq);
	}

}

#define RX_BUFFER_LEFT(x) (((x) >> 11) & 0x1f)
#define TX_BUFFER_LEFT(x) (((x) >> 6) & 0x1f)

static uint32_t s5l8930_spi_mm_read(void *opaque, target_phys_addr_t offset)
{
    S5L8930SPIState *s = (S5L8930SPIState *)opaque;

	fprintf(stderr, "%s: base 0x%08x offset 0x%08x\n", __func__, s->base, offset);

    switch (offset) {
    case SPI_CONTROL:
		return s->ctrl;
    case SPI_SETUP:
        return s->setup;
    case SPI_STATUS:
		/* Strange that on fifo empty irq handler doesnt clear irq */
        qemu_irq_lower(s->irq);
        return s->status;
    case SPI_PIN:
        return s->pin;
    case SPI_TXDATA:
        return s->tx_data[0];
    case SPI_RXDATA:
		s->rx_data = pflash_cmd_parse(s->pflash);
        s->status |= pflash_cmd_len(s->pflash) << 11;
		//fprintf(stderr, "%s: rxBuf 0x%08x\n", __func__, s->rx_data);

		/* Queue fifo empty irq */
		if(s->rxFifoCnt == 0x1e) {
			qemu_irq_lower(s->irq);
			qemu_mod_timer(s->timer, qemu_get_clock_ns(vm_clock) + (get_ticks_per_sec() / 1000));
		} else 
			s->rxFifoCnt++;
		return s->rx_data;
    case SPI_CLKDIV:
        return s->clkdiv;
    case SPI_CNT:
        return s->cnt;
    case SPI_IDD:
        return s->idd;
    default:
		;
    	//fprintf(stderr, "%s: base 0x%08x offset 0x%08x\n", __func__, s->base, offset);
        //hw_error("s5l8930_spi: bad read offset 0x" TARGET_FMT_plx "\n", offset);
    }
	return 0;
}

extern void spi_irq_trigger(int status, qemu_irq spi);

static void s5l8930_spi_mm_write(void *opaque, target_phys_addr_t offset,
                                 uint32_t val)
{
    S5L8930SPIState *s = (S5L8930SPIState *)opaque;

    fprintf(stderr, "%s: base 0x%08x offset 0x%08x value 0x%08x\n", __func__, s->base, offset, val);

    switch (offset) {
    case SPI_CONTROL:
		if(val & 0x1) {
				if(s->setup & 0x200000) {
					// tx
					s->status = 0x400002;
                	s->cmd = s->tx_data[0];
					pflash_cmd_set(s->pflash, s->tx_data);
					//fprintf(stderr, "%s: base: %d txCmd: %d\n", __FUNCTION__, s->base, s->cmd);
				} else {
                    // rx
					s->rxFifoCnt = 0;
                    s->status |= 1;
					s->status |= pflash_cmd_len(s->pflash) << 11;
                	//fprintf(stderr, "%s: base: %d rxBuf: %d\n", __FUNCTION__, s->base, RX_BUFFER_LEFT(s->status));
				}
	    		qemu_irq_raise(s->irq);
		}
        break;
    case SPI_SETUP:
        s->setup = val;
        break;
    case SPI_STATUS:
		//fprintf(stderr, "%s: clearing irq on base %d\n", __FUNCTION__, s->base);
		qemu_irq_lower(s->irq); 
        s->status = 0;
		s->txBuffer = 0;
        break;
    case SPI_PIN:
        s->pin = val;
        break;
    case SPI_TXDATA:
        s->tx_data[s->txBuffer++] = val;
        break;
    case SPI_RXDATA:
        s->rx_data = val;
        break;
    case SPI_CLKDIV:
        s->clkdiv = val;
        break;
    case SPI_CNT:
		pflash_set_rxlen(s->pflash, val);
        s->cnt = val;
        break;
    case SPI_IDD:
        s->idd = val;
        break;
    default:
    	fprintf(stderr, "%s: BAD Write base 0x%08x offset 0x%08x value 0x%08x\n", __func__, s->base, offset, val);
        //hw_error("s5l8930_spi: bad write offset 0x" TARGET_FMT_plx "\n", offset);
    }
}

CPUReadMemoryFunc * const s5l8930_spi_readfn[] = {
    s5l8930_spi_mm_read,
    s5l8930_spi_mm_read,
    s5l8930_spi_mm_read
};

CPUWriteMemoryFunc * const s5l8930_spi_writefn[] = {
    s5l8930_spi_mm_write,
    s5l8930_spi_mm_write,
    s5l8930_spi_mm_write
};

#if 0
static void s5l8930_spi_reset(void *opaque)
{
    S5L8930SPIState *s = (S5L8930SPIState *)opaque;

	s->cmd = 0;
	s->ctrl = 0;
	s->setup = 0;
	s->status = 0;
	s->pin = 0;
	s->rx_data = 0;
	s->clkdiv = 0;
	s->cnt = 0;
	s->idd = 0;
}
#endif 
static uint32_t base_addr = 0;

void s5l8930_set_spi_base(uint32_t base)
{
	base_addr = base;
}
/*
static int s5l8930_spi_init(SysBusDevice *dev)
{
    int iomemtype;
    S5L8930SPIState *s = FROM_SYSBUS(S5L8930SPIState, dev);

    sysbus_init_irq(dev, &s->irq);

    iomemtype = cpu_register_io_memory(s5l8930_spi_readfn, s5l8930_spi_writefn, s, DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, S5L8930_WDT_REG_MEM_SIZE, iomemtype);

	s->base = base_addr;
	//fprintf(stderr, "%s: irq %x base %d", __FUNCTION__, print_irq(s->irq), s->base);
    s5l8930_spi_reset(s);

    qemu_register_reset(s5l8930_spi_reset, s);

    return 0;
}

static void s5l8930_spi_register_devices(void)
{
    sysbus_register_dev("s5l8930.spi", sizeof(S5L8930SPIState),
                        s5l8930_spi_init);
}

device_init(s5l8930_spi_register_devices)
*/

DeviceState *s5l8930_spi_init(target_phys_addr_t base, qemu_irq irq)
{
    DeviceState *dev = qdev_create(NULL, "s5l8930.spi");

	//fprintf(stderr, "%s: irq %x base 0x%08x\n", __FUNCTION__, print_irq(irq), base);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);

    return dev;
}

static int intnum = 0;
static int s5l8930_spi_init1(SysBusDevice *dev)
{
    int iomemtype;
	DriveInfo *dinfo;
    S5L8930SPIState *s = FROM_SYSBUS(S5L8930SPIState, dev);

    sysbus_init_irq(dev, &s->irq);

    iomemtype = cpu_register_io_memory(s5l8930_spi_readfn, s5l8930_spi_writefn, s, DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, 0x3c, iomemtype);

	if(!intnum) {
   		dinfo = drive_get(IF_PFLASH, 0, 0);

    	if (!dinfo) {
        	fprintf(stderr, "A NOR image must be given with the -pflash parameter\n");
        	exit(1);
    	}

    	s->timer = qemu_new_timer_ns(vm_clock, spi_timer, s);

		s->pflash = (void *)pflash_spi_register(qemu_ram_alloc(NULL, "ipad1g.xor", 1024*1024),
                                dinfo->bdrv, 4096, 
                                256, 1, 2,
                                0x1f, 0x45, 0x02);
		intnum++;
	}
    return 0;
}

static SysBusDeviceInfo s5l8930_spi_info = {
    .init = s5l8930_spi_init1,
    .qdev.name  = "s5l8930.spi",
    .qdev.size  = sizeof(S5L8930SPIState)
};

static void s5l8930_spi_register(void)
{
    sysbus_register_withprop(&s5l8930_spi_info);
}

device_init(s5l8930_spi_register)


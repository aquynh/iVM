/*
 * Samsung S5L8900 processor support
 *
 * Written by cmw 
 *
 * Uses code from Samsung S5pc1xx port
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "arm-misc.h"
#include "sysemu.h"
#include "qemu-char.h"
#include "qemu-timer.h"
#include "sysbus.h"
#include "primecell.h"
#include "devices.h"
#include "console.h"
#include "block.h"
#include "boards.h"
#include "s5l8900.h"
#include "net.h"
#include "i2c.h"

typedef struct s5l8900_clk1_s
{
	uint32_t	clk1_config0;
    uint32_t    clk1_config1;
    uint32_t    clk1_config2;
	uint32_t    clk1_pll1con;
    uint32_t    clk1_pll2con;
    uint32_t    clk1_pll3con;
	uint32_t    clk1_plllock;
	uint32_t	clk1_pllmode;

} s5l8900_clk1_s;


typedef struct s5l8900_timer_s
{
    uint32_t    ticks_high;
	uint32_t	ticks_low;

} s5l8900_timer_s;

static uint32_t s5l8900_timer1_read(void *opaque, target_phys_addr_t addr)
{
    s5l8900_timer_s *s = (struct s5l8900_timer_s *) opaque;
	uint64_t ticks;

    S5L8900_DEBUG(S5L8900_DEBUG_CLK, S5L8900_DLVL_ERR, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

    switch (addr) {
		case TIMER_TICKSHIGH:	 // needs to be fixed so that read from low first works as well
			ticks = qemu_get_clock_ns(vm_clock);
			s->ticks_high = (ticks >> 32);
			s->ticks_low = (ticks & 0xFFFFFFFF);
			return s->ticks_high;
		case TIMER_TICKSLOW:
			return s->ticks_low;
      default:
        S5L8900_DEBUG(S5L8900_DEBUG_CLK, S5L8900_DLVL_ERR, "%s: UNMAPPED offset = 0x%02x\n", __FUNCTION__, (int)addr);
    }
    return 0;
}

static void s5l8900_timer1_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{

    S5L8900_DEBUG(S5L8900_DEBUG_CLK, S5L8900_DLVL_ERR, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

}

static CPUReadMemoryFunc *s5l8900_timer1_readfn[] = {
    s5l8900_timer1_read,
    s5l8900_timer1_read,
    s5l8900_timer1_read,
};

static CPUWriteMemoryFunc *s5l8900_timer1_writefn[] = {
    s5l8900_timer1_write,
    s5l8900_timer1_write,
    s5l8900_timer1_write,
};

static void s5l8900_timer_init(target_phys_addr_t base)
{
    struct s5l8900_timer_s *timer1 = (struct s5l8900_timer_s *) qemu_mallocz(sizeof(struct s5l8900_timer_s));

    int iomemtype = cpu_register_io_memory(s5l8900_timer1_readfn,
                                           s5l8900_timer1_writefn, timer1, DEVICE_LITTLE_ENDIAN);
    S5L8900_OPAQUE("TIMER1", timer1);
	timer1->ticks_high = 0;
	timer1->ticks_low = 0;
    cpu_register_physical_memory(base, 0xFF, iomemtype);
}

static uint32_t s5l8900_clk1_read(void *opaque, target_phys_addr_t addr)
{
    s5l8900_clk1_s *s = (struct s5l8900_clk1_s *) opaque;

    S5L8900_DEBUG(S5L8900_DEBUG_CLK, S5L8900_DLVL_ERR, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

    switch (addr) {
    	case CLOCK1_CONFIG0:
    		return s->clk1_config0;
		case CLOCK1_CONFIG1:
			return s->clk1_config1;
		case CLOCK1_CONFIG2:
			return s->clk1_config2;
		case CLOCK1_PLLLOCK:
			return 1;
		case CLOCK1_PLLMODE:
			return s->clk1_pllmode;
	
      default:
        S5L8900_DEBUG(S5L8900_DEBUG_CLK, S5L8900_DLVL_ERR, "%s: UNMAPPED offset = 0x%02x\n", __FUNCTION__, (int)addr);
    }
    return 0;
}

static void s5l8900_clk1_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{

	S5L8900_DEBUG(S5L8900_DEBUG_CLK, S5L8900_DLVL_ERR, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

}

static CPUReadMemoryFunc *s5l8900_clk1_readfn[] = {
    s5l8900_clk1_read,
    s5l8900_clk1_read,
    s5l8900_clk1_read,
};

static CPUWriteMemoryFunc *s5l8900_clk1_writefn[] = {
    s5l8900_clk1_write,
    s5l8900_clk1_write,
    s5l8900_clk1_write,
};


static uint32_t s5l8900_miu_read(void *opaque, target_phys_addr_t addr)
{
    S5L8900_DEBUG(S5L8900_DEBUG_MIU, S5L8900_DLVL_ERR, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

    switch (addr) {
		case POWER_ID:
				return (3 << 24); //for older iboots
				//return (5 << 0x18); // new iboots
      default:
        S5L8900_DEBUG(S5L8900_DEBUG_MIU, S5L8900_DLVL_ERR, "%s: UNMAPPED offset = 0x%02x\n", __FUNCTION__, (int)addr);
    }
    return 0;
}

static void s5l8900_miu_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{

    S5L8900_DEBUG(S5L8900_DEBUG_MIU, S5L8900_DLVL_ERR, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

}

static CPUReadMemoryFunc *s5l8900_miu_readfn[] = {
    s5l8900_miu_read,
    s5l8900_miu_read,
    s5l8900_miu_read,
};

static CPUWriteMemoryFunc *s5l8900_miu_writefn[] = {
    s5l8900_miu_write,
    s5l8900_miu_write,
    s5l8900_miu_write,
};

static void s5l8900_miu_init(target_phys_addr_t base)
{
    int iomemtype = cpu_register_io_memory(s5l8900_miu_readfn,
                                           s5l8900_miu_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0x50, iomemtype);
}


static void s5l8900_clk_init(target_phys_addr_t base)
{
    struct s5l8900_clk1_s *clk1 = (struct s5l8900_clk1_s *) qemu_mallocz(sizeof(struct s5l8900_clk1_s));

    int iomemtype = cpu_register_io_memory(s5l8900_clk1_readfn,
                                           s5l8900_clk1_writefn, clk1, DEVICE_LITTLE_ENDIAN);
    S5L8900_OPAQUE("clk1", clk1);

    cpu_register_physical_memory(base, 0xFF, iomemtype);
}

static uint32_t s5l8900_chipid_read(void *opaque, target_phys_addr_t addr)
{

	S5L8900_DEBUG(S5L8900_DEBUG_CHIPID, S5L8900_DLVL_WARN, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

	switch(addr) {
			case 0x04:
				return 0xfffffffc;
		default:
			 S5L8900_DEBUG(S5L8900_DEBUG_CHIPID, S5L8900_DLVL_ERR, "%s: UNMAPPED offset = 0x%02x\n", __FUNCTION__, (int)addr);
	}

	return 0;
}

static CPUReadMemoryFunc *s5l8900_chipid_readfn[] = {
    s5l8900_chipid_read,
    s5l8900_chipid_read,
    s5l8900_chipid_read,
};

static CPUWriteMemoryFunc *s5l8900_chipid_writefn[] = {
    NULL,
    NULL,
    NULL,
};

static void s5l8900_chipid_init(target_phys_addr_t base)
{

    int iomemtype = cpu_register_io_memory(s5l8900_chipid_readfn,
                                           s5l8900_chipid_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xF, iomemtype);

}

static inline qemu_irq s5l8900_get_irq(struct s5l8900_state_s *s, int n)
{
    return s->irq[n / S5L8900_VIC_SIZE][n % S5L8900_VIC_SIZE];
}

s5l8900_state *s5l8900_init(void)
{

	s5l8900_state *s = (s5l8900_state *)qemu_mallocz(sizeof(s5l8900_state));
	i2c_bus *i2c;


    qemu_irq *cpu_irq;
    DeviceState *dev, *dev_prev;
	int i,j;

	s->env = cpu_init("arm1136");
	if(!s->env) {
    	fprintf(stderr, "Unable to find CPU definition\n");
    	exit(1);
  	}

    cpu_irq = arm_pic_init_cpu(s->env);
    s->irq = qemu_mallocz(S5L8900_VIC_N * sizeof(qemu_irq *));
    dev = pl192_init(S5L8900_VIC_BASE, 0,
                     cpu_irq[ARM_PIC_CPU_IRQ],
                     cpu_irq[ARM_PIC_CPU_FIQ], NULL);
    s->irq[0] = qemu_mallocz(S5L8900_VIC_SIZE * sizeof(qemu_irq));
    for (i = 0; i < S5L8900_VIC_SIZE; i++)
        s->irq[0][i] = qdev_get_gpio_in(dev, i);
    for (j = 1; j < S5L8900_VIC_N; j++) {
        dev_prev = dev;
        dev = pl192_init(S5L8900_VIC_BASE + S5L8900_VIC_SHIFT * j, j, NULL);

        s->irq[j] = qemu_mallocz(S5L8900_VIC_SIZE * sizeof(qemu_irq));
        for (i = 0; i < S5L8900_VIC_SIZE; i++)
            s->irq[j][i] = qdev_get_gpio_in(dev, i);
        pl192_chain(sysbus_from_qdev(dev_prev), sysbus_from_qdev(dev));
    }

	/* Chip info */
	s5l8900_chipid_init(S5L8900_CHIPID);

    /* CLKs */
	s5l8900_clk_init(CLOCK1);

	/* MIU */
	s5l8900_miu_init(MIU_BASE);

    /* Sytem Timer */
	s5l8900_timer_init(S5L8900_TIMER1);

	/* Uart */
    s5l8900_uart_init(S5L8900_UART0_BASE, 0, 256, s5l8900_get_irq(s, S5L8900_IRQ_UART0), serial_hds[0]);

	/* Uart + Radio */
    s5l8900_uart_init(S5L8900_UART1_BASE, 0, 256, s5l8900_get_irq(s, S5L8900_IRQ_UART0 /* XXX: fix irq for radio */), NULL);

    /* I2C */
    dev = sysbus_create_simple("s5l8900.i2c", S5l8900_I2C0_BASE,
                               s5l8900_get_irq(s, S5L8900_IRQ_I2C_0));
	i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

    dev = sysbus_create_simple("s5l8900.i2c", S5l8900_I2C1_BASE,
                               s5l8900_get_irq(s, S5L8900_IRQ_I2C_1));
    i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");


	/* PWU */
	pcf50633_init(i2c, PCF50633_ADDR_GET);
    pcf50633_init(i2c, PCF50633_ADDR_SET);


	/* SPI */
	set_spi_base(0);
    sysbus_create_simple("s5l8900.spi",
                         S5L8900_SPI0_BASE,
                         s5l8900_get_irq(s, S5L8900_SPI0_IRQ));

	set_spi_base(1);
    sysbus_create_simple("s5l8900.spi",
                         S5L8900_SPI1_BASE,
                         s5l8900_get_irq(s, S5L8900_SPI1_IRQ));

	set_spi_base(2);
    sysbus_create_simple("s5l8900.spi",
                         S5L8900_SPI2_BASE,
                         s5l8900_get_irq(s, S5L8900_SPI2_IRQ));

    /* USB-OTG */
    s5l8900_usb_otg_init(&nd_table[0],
                         S5L8900_USB_OTG_BASE,
                         s5l8900_get_irq(s, S5L8900_IRQ_OTG));

	return s;
}

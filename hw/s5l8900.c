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
#include "usb_synopsys.h"
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

struct s5l8900_gpio_s s5l8900_gpio_state[32];

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
			#if 0 /* only enable if your on 3.1.3 */
				{
					uint32_t debug_uart = 0xffffffff;
					cpu_physical_memory_write(0x1802765C, (uint8_t *)&debug_uart, 4);
				}
			#endif
				return 0xfffffffc;
        	case 0x8:
                return 0x4;

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

static void s5l8900_gpioic_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    //fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __func__, addr, value);
}

static uint32_t s5l8900_gpioic_read(void *opaque, target_phys_addr_t addr)
{
    //fprintf(stderr, "%s: offset 0x%08x\n", __func__, addr);

    switch(addr) {
		case 0x44:
			return 0x5000005;
        case 0x7a:
		case 0x7c:
            return 1;
    }

    return 0;
}

static CPUReadMemoryFunc *s5l8900_gpioic_readfn[] = {
    s5l8900_gpioic_read,
    s5l8900_gpioic_read,
    s5l8900_gpioic_read,
};

static CPUWriteMemoryFunc *s5l8900_gpioic_writefn[] = {
    s5l8900_gpioic_write,
    s5l8900_gpioic_write,
    s5l8900_gpioic_write,
};

static void s5l8900_gpioic_init(target_phys_addr_t base)
{

    int iomemtype = cpu_register_io_memory(s5l8900_gpioic_readfn,
                                           s5l8900_gpioic_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0x3FF, iomemtype);
}

static void s5l8900_chipid_init(target_phys_addr_t base)
{

    int iomemtype = cpu_register_io_memory(s5l8900_chipid_readfn,
                                           s5l8900_chipid_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xF, iomemtype);

}

static void s5l8900_gpio_write(void *opaque, target_phys_addr_t addr, uint32_t value) 
{
	fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __func__, addr, value);
}

static uint32_t s5l8900_gpio_read(void *opaque, target_phys_addr_t addr)
{
    fprintf(stderr, "%s: offset 0x%08x\n", __func__, addr);

	switch(addr) {
		case 0x7a:
			return 1;
		case 0x2c4:
			return s5l8900_gpio_state[0].gpio_state;
	}

    return 0;
}

static CPUReadMemoryFunc *s5l8900_gpio_readfn[] = {
    s5l8900_gpio_read,
    s5l8900_gpio_read,
    s5l8900_gpio_read,
};

static CPUWriteMemoryFunc *s5l8900_gpio_writefn[] = {
    s5l8900_gpio_write,
    s5l8900_gpio_write,
    s5l8900_gpio_write,
};

static void s5l8900_gpio_init(target_phys_addr_t base)
{

    int iomemtype = cpu_register_io_memory(s5l8900_gpio_readfn,
                                           s5l8900_gpio_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0x3FF, iomemtype);

	/* Vol gpios are inverted */
    s5l8900_gpio_state[0].gpio_state |= (1 << (BUTTONS_VOLUP & 0xf));
    s5l8900_gpio_state[0].gpio_state |= (1 << (BUTTONS_VOLDOWN & 0xf));


}

static inline qemu_irq s5l8900_get_irq(struct s5l8900_state_s *s, int n)
{
    return s->irq[n / S5L8900_VIC_SIZE][n % S5L8900_VIC_SIZE];
}

static uint32_t s5l8900_usb_phy_read(void *opaque, target_phys_addr_t offset)
{
	s5l8900_state *s = opaque;

	switch(offset)
	{
	case 0x0: // OPHYPWR
		return s->usb_ophypwr;

	case 0x4: // OPHYCLK
		return s->usb_ophyclk;

	case 0x8: // ORSTCON
		return s->usb_orstcon;

	case 0x20: // OPHYTUNE
		return s->usb_ophytune;

	default:
		hw_error("%s: read invalid location 0x%08x.\n", __func__, offset);
		return 0;
	}

	return 0;
}

static void s5l8900_usb_phy_write(void *opaque, target_phys_addr_t offset, uint32_t val)
{
	s5l8900_state *s = opaque;

	switch(offset)
	{
	case 0x0: // OPHYPWR
		s->usb_ophypwr = val;
		return;

	case 0x4: // OPHYCLK
		s->usb_ophyclk = val;
		return;

	case 0x8: // ORSTCON
		s->usb_orstcon = val;
		return;

	case 0x20: // OPHYTUNE
		s->usb_ophytune = val;
		return;

	default:
		hw_error("%s: write invalid location 0x%08x.\n", __func__, offset);
	}
}

static CPUReadMemoryFunc * const s5l8900_usb_phy_readfn[] = {
    s5l8900_usb_phy_read,
    s5l8900_usb_phy_read,
    s5l8900_usb_phy_read,
};

static CPUWriteMemoryFunc * const s5l8900_usb_phy_writefn[] = {
    s5l8900_usb_phy_write,
    s5l8900_usb_phy_write,
    s5l8900_usb_phy_write,
};

static void s5l8900_usb_phy_init(s5l8900_state *_state)
{
	_state->usb_ophypwr = 0;
	_state->usb_ophyclk = 0;
	_state->usb_orstcon = 0;
	_state->usb_ophytune = 0;

	int iomemtype = cpu_register_io_memory(s5l8900_usb_phy_readfn,
                                           s5l8900_usb_phy_writefn,
										   _state, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(S5L8900_USB_PHY_BASE, 0x40, iomemtype);
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

	/* GPIO */
	s5l8900_gpio_init(S5L8900_GPIO_BASE);

	/* GPIOIC */
	s5l8900_gpioic_init(S5L8900_GPIOIC_BASE);

	/* Uart */
    s5l8900_uart_init(S5L8900_UART0_BASE, 0, 256, s5l8900_get_irq(s, S5L8900_IRQ_UART0), serial_hds[0]);

	/* Uart + Radio */
    s5l8900_uart_init(S5L8900_UART1_BASE, 0, 256, s5l8900_get_irq(s, S5L8900_IRQ_UART0 /* XXX: fix irq for radio */), NULL);

    /* I2C 0 */
    dev = sysbus_create_simple("s5l8900.i2c", S5l8900_I2C0_BASE,
                               s5l8900_get_irq(s, S5L8900_IRQ_I2C_0));
	i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

    /* PWU  */
    pcf50633_init(i2c, PCF50633_ADDR_GET >> 1);

	/* I2C 1 */
    dev = sysbus_create_simple("s5l8900.i2c", S5l8900_I2C1_BASE,
                               s5l8900_get_irq(s, S5L8900_IRQ_I2C_1));
    i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

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
	register_synopsys_usb(S5L8900_USB_OTG_BASE,
			s5l8900_get_irq(s, S5L8900_IRQ_OTG));
	s5l8900_usb_phy_init(s);

	return s;
}

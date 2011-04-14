#ifndef S5L8900_H
#define S5L8900_H

#include "qemu-timer.h"

// Devices

// GPIO
#define S5L8900_GPIO_BASE              0x3E400000
#define S5L8900_IRQ_GPIO0              0x21
#define S5L8900_IRQ_GPIO1              0x20
#define S5L8900_IRQ_GPIO2              0x1f
#define S5L8900_IRQ_GPIO3              0x03
#define S5L8900_IRQ_GPIO4              0x02
#define S5L8900_IRQ_GPIO5              0x01
#define S5L8900_IRQ_GPIO6              0x00
#define S5L8900_GPIO_INTLEVEL          0x80
#define S5L8900_GPIO_INTSTAT           0xA0
#define S5L8900_GPIO_INTEN             0xC0
#define S5L8900_GPIO_INTTYPE           0xE0
#define S5L8900_GPIO_FSEL              0x320

#define BUTTONS_HOLD 0x1605
#define BUTTONS_HOME 0x1600
#define BUTTONS_VOLUP 0x1601
#define BUTTONS_VOLDOWN 0x1602


// PMU
#define PCF50633_ADDR_GET 0xe6
#define PCF50633_ADDR_SET 0xe7
#define BUTTONS_IIC_STATE 0x4B

// MIU
#define MIU_BASE 0x39A00000 
#define POWER_ID 0x44

// Chip ID
#define S5L8900_CHIPID	 0x3e500000

// UART
#define S5L8900_UART0_BASE	 0x3cc00000
#define S5L8900_UART1_BASE	 0x3cc04000
#define S5L8900_IRQ_UART0 	24

// TIMERS
#define S5L8900_IRQ_TIMER0	7
#define S5L8900_TIMER1	0x3E200000
#define TIMER_TICKSHIGH 0x80
#define TIMER_TICKSLOW 0x84

// VIC
#define S5l8900_I2C0_BASE 0x3C600000
#define S5l8900_I2C1_BASE 0x3c900000
#define S5L8900_IRQ_I2C_0 15
#define S5L8900_IRQ_I2C_1 0
#define S5L8900_VIC_N	  2
#define S5L8900_VIC_SIZE  32
#define S5L8900_VIC_BASE 0x38E00000
#define S5L8900_VIC_SHIFT 0x00001000
#define S5L8900_VIC1_BASE 0x38E01000

#define VICIRQSTATUS 0x000
#define VICRAWINTR 0x8
#define VICINTSELECT 0xC
#define VICINTENABLE 0x10
#define VICINTENCLEAR 0x14
#define VICSWPRIORITYMASK 0x24
#define VICVECTADDRS 0x100
#define VICADDRESS 0xF00
#define VICPERIPHID0 0xFE0
#define VICPERIPHID1 0xFE4
#define VICPERIPHID2 0xFE8
#define VICPERIPHID3 0xFEC

// USB
#define S5L8900_USB_PHY_BASE  0x3C400000
#define S5L8900_USB_OTG_BASE  0x38400000
#define S5L8900_IRQ_OTG 0x13

// SPI
#define S5L8900_SPI0_BASE 0x3C300000
#define S5L8900_SPI1_BASE 0x3CE00000
#define S5L8900_SPI2_BASE 0x3D200000

#define S5L8900_SPI0_IRQ 0x9
#define S5L8900_SPI1_IRQ 0xA
#define S5L8900_SPI2_IRQ 0xB

#define S5L8900_SPI_CONTROL 0x0
#define S5L8900_SPI_SETUP 0x4
#define S5L8900_SPI_STATUS 0x8
#define S5L8900_SPI_PIN 0xC
#define S5L8900_SPI_TXDATA 0x10
#define S5L8900_SPI_RXDATA 0x20
#define S5L8900_SPI_CLKDIVIDER 0x30
#define S5L8900_SPI_SPCNT 0x34
#define S5L8900_SPI_SPIDD 0x38

// Clock
#define CLOCK0   0x38100000
#define CLOCK1   0x3C500000
#define CLOCK1_CONFIG0 0x0
#define CLOCK1_CONFIG1 0x4
#define CLOCK1_CONFIG2 0x8
#define CLOCK1_PLL0CON 0x20
#define CLOCK1_PLL1CON 0x24
#define CLOCK1_PLL2CON 0x28
#define CLOCK1_PLL3CON 0x2C
#define CLOCK1_PLL0LCNT 0x30
#define CLOCK1_PLL1LCNT 0x34
#define CLOCK1_PLL2LCNT 0x38
#define CLOCK1_PLL3LCNT 0x3C
#define CLOCK1_PLLLOCK 0x40
#define CLOCK1_PLLMODE 0x44
#define CLOCK1_CL2_GATES 0x48
#define CLOCK1_CL3_GATES 0x4C


// DEBUG

#define S5L8900_DLVL_ERR   (1)
#define S5L8900_DLVL_WARN  (2)
#define S5L8900_DLVL_INFO  (3)
#define S5L8900_DLVL_INFO2 (4)

extern uint32_t g_debug;
extern uint32_t g_dlevel;
extern FILE *g_debug_fp;

#define S5L8900_OPAQUE(name, opaque) fprintf(stderr, name " is at %p\n", opaque)

#define S5L8900_DEBUG_CLK  		(0x00000001)
#define S5L8900_DEBUG_MIU  		(0x00000002)
#define S5L8900_DEBUG_CHIPID 	(0x00000003)

#define S5L8900_DEBUG(module, level, msg ...)                              \
    do {                                                                \
    if ((module) & g_debug || (level) <= g_dlevel) {                \
        fprintf(g_debug_fp, "%lld : ", qemu_get_clock_ms(vm_clock));    \
            fprintf(g_debug_fp, msg);                                   \
        }                                                               \
    } while (0)

typedef struct s5l8900_gpio_s
{
    uint32_t gpio_state;
    uint32_t int_state;

} s5l8900_gpio_s;


typedef struct s5l8900_state_s {
    CPUState *env;
    qemu_irq **irq;

	uint32_t usb_ophypwr;
	uint32_t usb_ophyclk;
	uint32_t usb_orstcon;
	uint32_t usb_ophytune;

} s5l8900_state;

s5l8900_state *s5l8900_init(void);


DeviceState *s5l8900_uart_init(target_phys_addr_t base, int instance,
                               int queue_size, qemu_irq irq,
                               CharDriverState *chr);
DeviceState *pcf50633_init(i2c_bus *bus, int addr);
void s5l8900_usb_otg_init(NICInfo *nd, target_phys_addr_t base, qemu_irq irq);
void set_spi_base(uint32_t base);
#endif

#ifndef S5L8900_H
#define S5L8900_H

#include "qemu-timer.h"
#include <openssl/sha.h>
#include <openssl/aes.h>

// These iPad values need to be removed in cleanup
#define RAM_BASE_ADDR 0x40000000
#define RAMEnd 0x60000000
#define LLB_LOAD_ADDR 0x84000000
#define KERNStart 0x80000000
#define RAM_SIZE (RAMEnd - RAM_BASE_ADDR)

// UART
#define S5L8930_UART0_BASE   0x82500000
#define S5L8930_UART0_IRQ	 0x16

// VIC
#define S5L8930_VIC_N     4
#define S5L8930_VIC_SIZE  32
#define S5L8930_VIC_BASE  0xBF200000
#define S5L8930_VIC_SHIFT 0x00010000

// PMGR
#define S5L8930_PMGR_BASE 0xBF100000

// Timers
#define S5L8930_TIMER1_BASE  0xBF102000
#define S5L8930_TIMER0_IRQ  5
#define S5L8930_TIMER1_IRQ  6
//#define TIMER_IRQSTAT 0x10000
#define TIMER_TICKSHIGH	    0x4
#define TIMER_TICKSLOW      0x0
#define TIMER_REGISTER 	    0x8
#define TIMER_REGISTER2	 	0xc
#define TIMER_REGISTER_TICK 0x10
#define TIMER_REGISTER_TICK2 0x14

#define TIMER_STATE_START 1
#define TIMER_STATE_STOP 0
#define TIMER_STATE_MANUALUPDATE 2

// GPIO
#define S5L8930_GPIO_BASE 0xBFA00000
#define S5L8930_GPIO_IRQ  0x74

// USB
#define S5L8930_USB_PHY_BASE 0x86000000
#define S5L8930_USB_OTG_BASE 0x86100000
#define S5L8930_IRQ_OTG		 0xd


// Misc sys 
#define S5L8900_MISCSYS_BASEADDR 0xBF800000

// Chip ID
#define S5L8930_CHIPID	   0xBF500000

// Power
#define POWER 0xBF100000
#define POWER_ID 0x4000
#define POWER_ID_EPOCH(x) ((x) >> 24)

// I2C
#define S5L8930_I2C0_BASE 0x83200000
#define S5L8930_I2C1_BASE 0x83300000
#define S5L8930_I2C2_BASE 0x83400000
#define S5L8930_IRQ_I2C_0 0x13
#define S5L8930_IRQ_I2C_1 0x14
#define S5L8930_IRQ_I2C_2 0x15

// I2C Devices 

// PMU
#define PCF50633_ADDR     0x74
// Charger 
#define S5L8930_CHG_ADDR  0x08

// SPI
#define S5L8930_SPI0_BASE  0x82000000
#define S5L8930_SPI0_IRQ   0x1D
#define S5L8930_SPI1_BASE  0x82100000
#define S5L8930_SPI1_IRQ   0x1E
#define S5L8930_SPI2_BASE  0x82200000
#define S5L8930_SPI2_IRQ   0x1F
#define S5L8930_SPI3_BASE  0x82300000
#define S5L8930_SPI3_IRQ   0x20
#define S5L8930_SPI4_BASE  0x82400000
#define S5L8930_SPI4_IRQ   0x21

// IOP
#define S5L8930_IOP_IRQ	   0x3

// CDMA
//#define key_89A ((uint8_t[]){0xdb, 0x1f, 0x5b, 0x33, 0x60, 0x6c, 0x5f, 0x1c, 0x19, 0x34, 0xaa, 0x66, 0x58, 0x9c, 0x06, 0x61})
static const uint8_t key_uid[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
static const uint8_t key_test[] = {0xe6, 0x4f, 0x47, 0x3c, 0x67, 0xf4, 0x93, 0x15, 0xfd, 0x1b, 0xf1, 0x8c, 0xcf, 0x92, 0x54, 0x8e};
static const uint8_t Key835[] = {0x48, 0xcc, 0xa9, 0xfb, 0x53, 0x7e, 0xdb, 0x19, 0xc5, 0x4d, 0xba, 0x9d, 0x98, 0xfa, 0x5f, 0xdb};
static const uint8_t Key89B[] = {0x0c, 0x34, 0x96, 0x06, 0x93, 0x64, 0x8a, 0x16, 0x4b, 0xb9, 0x7f, 0x38, 0x87, 0x77, 0x19, 0x22};
static const uint8_t kernKey435[] = {0x16, 0x52, 0xc8, 0x59, 0x14, 0x19, 0x0a, 0x2a, 0x99, 0x99, 0x70, 0x42, 0x8a, 0x9e, 0xa0, 0x3e, 0x7a, 0xbc, 0x6c, 0x2b, 0x36, 0xf9, 0x74, 0xd7, 0x26, 0x75, 0x8e, 0x2b, 0x95, 0x93, 0xcc, 0xdd};
static uint8_t kernIV435[] = {0x80, 0xf4, 0xc7, 0x0d, 0x2a, 0x18, 0xf3, 0xe4, 0x5e, 0xbb, 0xe4, 0x4f, 0x9e, 0x61, 0x21, 0xeb};
static const unsigned char deviceKey435[] = {0xc6, 0x99, 0xaf, 0xb2, 0x5b, 0x70, 0x74, 0xe1, 0x55, 0x05, 0x40, 0xc7, 0x15, 0x28, 0xb9, 0xa0, 0x93, 0x78, 0x95, 0x8b, 0xfd, 0xce, 0x8f, 0xf1, 0x74, 0xa2, 0x88,0x94,0x19,0x35, 0x6f, 0x30};
static unsigned char deviceIV435[] = {0x2a, 0x3c, 0x0a, 0x12, 0xcb, 0xfc, 0xfd, 0xa5, 0x64, 0xd3, 0x38, 0xe0, 0x68, 0xa8, 0xf3, 0xd1};


#define S5L8930_CDMA_BASE     0x87000000
#define S5L8930_CDMA_AES_BASE S5L8930_CDMA_BASE + 0x800000
#define S5L8930_CDMA_IRQ_BASE	  0x30
#define S5L8930_CDMA_CHANNEL5_IRQ 0x35
#define S5L8930_CDMA_CHANNEL6_IRQ 0x36
#define S5L8930_CDMA_CHANNEL7_IRQ 0x37
#define S5L8930_CDMA_CHANNEL8_IRQ 0x38


typedef enum AESKeyType {
    AESUID = 0, 
    AESGID = 1,
    AESCustom = 2
} AESKeyType;

#define AES_256 2
#define AES_192 1
#define AES_128 0

// SHA1 
#define S5L8930_SHA1_BASE 0x80100000

// H2FMI
#define S5L8930_H2FMI_BASE0 0x81200000
#define S5L8930_H2FMI_BASE1 0x81300000
#define S5L8930_H2FMI_IRQ0  0x22
#define S5L8930_H2FMI_IRQ1  0x23


// Debug stuff
#define S5L8930_DLVL_ERR   (1)
#define S5L8930_DLVL_WARN  (2)
#define S5L8930_DLVL_INFO  (3)
#define S5L8930_DLVL_INFO2 (4)

extern uint32_t g_debug;
extern uint32_t g_dlevel;
extern FILE *g_debug_fp;

#define S5L8930_OPAQUE(name, opaque) fprintf(stderr, name " is at %p\n", opaque)

#define S5L8930_DEBUG_CLK  		(0x00000001)
#define S5L8930_DEBUG_MIU  		(0x00000002)
#define S5L8930_DEBUG_CHIPID 	(0x00000004)
#define S5L8930_DEBUG_MISCSYS   (0x00000008)
#define S5L8930_DEBUG_PMGR		(0x00000010)
#define S5L8930_DEBUG_CDMA		(0x00000020)
#define S5L8930_DEBUG_SHA1		(0x00000040)
#define S5L8930_DEBUG_GPIO		(0x00000080)
#define S5L8930_DEBUG_USBPHY	(0x00000100)

#define S5L8930_DEBUG(module, level, msg ...)                              \
    do {                                                                \
    if ((module) & g_debug || (level) <= g_dlevel) {                \
        fprintf(g_debug_fp, "%lld : ", qemu_get_clock_ms(vm_clock));    \
            fprintf(g_debug_fp, msg);                                   \
        }                                                               \
    } while (0)

typedef struct s5l8930_state_s {
    CPUState *env;
	void *iop;
    qemu_irq **irq;

	/* CDMA */
	void *cdma;
	/* Timer */
	void *timer;

	/* PHY USB */
    uint32_t usb_ophypwr;
    uint32_t usb_ophyclk;
    uint32_t usb_orstcon;
    uint32_t usb_ophytune;

} s5l8930_state;

typedef struct s5l8930iop_state_s {
    CPUState *env;
    qemu_irq **irq;
} s5l8930iop_state_s;

s5l8930_state *s5l8930_init(void);
DeviceState *pcf50633_init(i2c_bus *bus, int addr);
DeviceState *ipadchg_init(i2c_bus *bus, int addr);
void s5l8930_iop_init(void *opaque);
void setTimerIRQ2(void *opaque, qemu_irq irq);
DeviceState *s5l8900_uart_init(target_phys_addr_t base, int instance,
                               int queue_size, qemu_irq irq,
                               CharDriverState *chr);
void do_aes_crypto(uint32_t *inBuf, uint32_t *outBuf, uint32_t size, uint32_t operation, void *opaque);
void replaceCDMAIRQHandlers(void *opaque, qemu_irq dma5, qemu_irq dma6, qemu_irq dma7, qemu_irq dma8);
DeviceState *s5l8930_h2fmi0_register(target_phys_addr_t base, qemu_irq irq);
DeviceState *s5l8930_h2fmi1_register(target_phys_addr_t base, qemu_irq irq);

void enableIOPH2fmi(void);

qemu_irq getH2FMI_IRQ_0(void);
qemu_irq getH2FMI_IRQ_1(void);


/*
typedef struct dmaAES_CRYPTO {
    uint32_t *unkn0;
    uint32_t unkn1;
    uint32_t *buffer;
    uint32_t size;
    uint32_t unkn4;
    uint32_t unkn5;
    uint32_t unkn6;
    uint32_t unkn7;
    uint32_t unkn8;
    uint32_t unkn9;
    uint32_t unkn10;
    uint32_t unkn11;
    uint32_t unkn12;
    uint32_t unkn13;
    uint32_t unkn14;
    uint32_t unkn15;
} __attribute__((packed)) dmaAES_CRYPTO;
*/
typedef struct segmentBuffer {
    uint32_t address;
    uint32_t flags;
    uint32_t buffer;
    uint32_t size; 
    uint32_t iv[4];
} segmentBuffer;

#define MAX_CDMA_CHAN 37

typedef struct s5l8930_cdma {
    uint32_t size[MAX_CDMA_CHAN];
	uint32_t segptr[MAX_CDMA_CHAN];
    uint32_t status[MAX_CDMA_CHAN];
    uint32_t dmaAesSetup[MAX_CDMA_CHAN];
	uint32_t config[MAX_CDMA_CHAN];
	uint32_t creg[MAX_CDMA_CHAN];
	uint32_t mstatus;
    uint8_t aesOperation;
    segmentBuffer *dmaSegment[MAX_CDMA_CHAN];
    AES_KEY decryptKey;
    uint8_t ivec[16];
    uint32_t custkey[8];
    uint32_t keyLen;
    uint8_t keyType;
	qemu_irq irqs[MAX_CDMA_CHAN];
} s5l8930_cdma_s;

#endif

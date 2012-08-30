#ifndef _IPAD1G_H
#define _IPAD1G_H

#define RAM_BASE_ADDR 0x40000000
#define RAMEnd 0x60000000
#define LLB_LOAD_ADDR 0x84000000
#define IBOOT_LOAD_ADDR 0x5FF00000
#define RAM_SIZE (RAMEnd - RAM_BASE_ADDR)

//#define LargeMemoryStart 0x46000000
#define RAM_HIGH_ADDR	0xC0000000

#define CLCD_BASE_ADDR  	0x89100000
#define CLCD_FRAMEBUFFER	0x4F700000

typedef struct ipad1g_clcd_s {
    DisplayState *ds;
    uint16_t palette[256];
    int invalidate;
    uint32_t lcd_ctrl;
    qemu_irq irq;
} ipad1g_clcd_s;

#endif

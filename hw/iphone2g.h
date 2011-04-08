#ifndef _IPHONE2G_H
#define _IPHONE2G_H

#include <openssl/sha.h>
#include <openssl/aes.h>

#define key_0x837 ((uint8_t[]){0x18, 0x84, 0x58, 0xA6, 0xD1, 0x50, 0x34, 0xDF, 0xE3, 0x86, 0xF2, 0x3B, 0x61, 0xD4, 0x37, 0x74})

#define LCD_BASE_ADDR 0x38900000
#define LCD_WIDTH 320
#define LCD_HEIGHT 480

#define AES_128_CBC_BLOCK_SIZE 64
#define AES_BASE_ADDR 0x38C00000
	#define IPHONE2G_AES_CONTROL 0x0
	#define IPHONE2G_AES_GO 0x4
	#define IPHONE2G_AES_UNKREG0 0x8
	#define IPHONE2G_AES_STATUS 0xC
	#define IPHONE2G_AES_UNKREG1 0x10
	#define IPHONE2G_AES_KEYLEN 0x14
	#define IPHONE2G_AES_INSIZE 0x18
	#define IPHONE2G_AES_INADDR 0x20
	#define IPHONE2G_AES_OUTSIZE 0x24
	#define IPHONE2G_AES_OUTADDR 0x28
	#define IPHONE2G_AES_AUXSIZE 0x2C
	#define IPHONE2G_AES_AUXADDR 0x30
	#define IPHONE2G_AES_SIZE3 0x34
	#define IPHONE2G_AES_KEY 0x4C
	#define IPHONE2G_AES_TYPE 0x6C
	#define IPHONE2G_AES_IV 0x74
	#define IPHONE2G_AES_KEYSIZE 0x20
	#define IPHONE2G_AES_IVSIZE 0x10



typedef struct iphone2g_aes_s
{
    AES_KEY decryptKey;
	uint8_t ivec[16];
	uint32_t insize;
	uint32_t inaddr;
	uint32_t outsize;
	uint32_t outaddr;
	uint32_t auxaddr;
	uint32_t keytype;
	uint32_t status;
	uint32_t ctrl;
	uint32_t unkreg0;
	uint32_t unkreg1;
	uint32_t keylen;
} iphone2g_aes_s;


typedef struct iphone2g_lcd_s {
    DisplayState *ds;
    uint16_t palette[256];
    int invalidate;
    uint32_t lcd_ctrl;
    qemu_irq irq;
} iphone2g_lcd_s;

#endif

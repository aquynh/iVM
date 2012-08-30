#ifndef _IPHONE2G_H
#define _IPHONE2G_H

#include <openssl/sha.h>
#include <openssl/aes.h>

#define key_uid ((uint8_t[]){0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF})

#define GET_BITS(x, start, length) ((((uint32_t)(x)) << (32 - ((start) + (length)))) >> (32 - (length)))
#define GET_KEYLEN(x) GET_BITS(x, 16, 2)

#define LCD_BASE_ADDR 0x38900000
#define LCD_WIDTH 320
#define LCD_HEIGHT 480

#define SHA1_BASE_ADDR 0x38000000
#define SHA_CONFIG  0x0
#define SHA_RESET   0x4
#define SHA_UNKSTAT 0x7c
#define SHA_HRESULT 0x84
#define SHA_INSIZE  0x8c

#define AES_128_CBC_BLOCK_SIZE 64
#define AES_BASE_ADDR 0x38C00000
#define AES_CONTROL 0x0
#define AES_GO 0x4
#define AES_UNKREG0 0x8
#define AES_STATUS 0xC
#define AES_UNKREG1 0x10
#define AES_KEYLEN 0x14
#define AES_INSIZE 0x18
#define AES_INADDR 0x20
#define AES_OUTSIZE 0x24
#define AES_OUTADDR 0x28
#define AES_AUXSIZE 0x2C
#define AES_AUXADDR 0x30
#define AES_SIZE3 0x34
#define AES_KEY_REG 0x4C
#define AES_TYPE 0x6C
#define AES_IV_REG 0x74
#define AES_KEYSIZE 0x20
#define AES_IVSIZE 0x10

typedef enum AESKeyType {
    AESCustom = 0,
    AESGID = 1,
    AESUID = 2
} AESKeyType;

typedef enum AESKeyLen {
    AES128 = 0,
    AES192 = 1,
    AES256 = 2
} AESKeyLen;

typedef struct aes_s
{
    AES_KEY decryptKey;
	uint32_t ivec[4];
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
	uint32_t operation;
	uint32_t keylen;
	uint32_t custkey[8]; 
} aes_s;


typedef struct iphone2g_lcd_s {
    DisplayState *ds;
    uint16_t palette[256];
    int invalidate;
    uint32_t lcd_ctrl;
    qemu_irq irq;
} iphone2g_lcd_s;

#endif

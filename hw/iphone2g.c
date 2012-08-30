/*
 * iPhone2g support
 *
 * Written by cmw
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "arm-misc.h"
#include "sysemu.h"
#include "qemu-timer.h"
#include "devices.h"
#include "console.h"
#include "block.h"
#include "blockdev.h"
#include "boards.h"
#include "loader.h"
#include "flash.h"
#include "framebuffer.h"

#include "iphone2g.h"
#include "s5l8900.h"

#define VROM_BASE_ADDR 	0x20000000
#define IBOOT_BASE_ADDR 0x18000000
#define RAM_BASE_ADDR  	0x08000000
#define RAM_HIGH_ADDR   0x80000000
#define RAM_SIZE 	   	0x08000000
#define NOR_BASE_ADDR   0x24000000

uint32_t g_debug = ( 0 //S5L8900_DEBUG_CLK
                    //0xffffffff |
                    );
uint32_t g_dlevel = 0;
FILE *g_debug_fp = NULL;

static target_phys_addr_t frame_base = 0;

struct iphone2g_s {
    struct s5l8900_state *cpu;
};

extern s5l8900_gpio_s s5l8900_gpio_state[32];

struct  keymap {
    int column;
    int row;
};

static struct keymap map[0xE0] = {
    [0 ... 0xDF] = { -1, -1 },
    [0x1e] = {0,0}, /* a */
    [0x30] = {0,1}, /* b */
    [0x2e] = {0,2}, /* c */
    [0x20] = {0,3}, /* d */
    [0x12] = {0,4}, /* e */
    [0x21] = {0,5}, /* f */
    [0x22] = {1,0}, /* g */
    [0x23] = {1,1}, /* h */
    [0x17] = {1,2}, /* i */
    [0x24] = {1,3}, /* j */
    [0x25] = {1,4}, /* k */
    [0x26] = {1,5}, /* l */
    [0x32] = {2,0}, /* m */
    [0x31] = {2,1}, /* n */
    [0x18] = {2,2}, /* o */
    [0x19] = {2,3}, /* p */
    [0x10] = {2,4}, /* q */
    [0x13] = {2,5}, /* r */
    [0x1f] = {3,0}, /* s */
    [0x14] = {3,1}, /* t */
    [0x16] = {3,2}, /* u */
    [0x2f] = {3,3}, /* v */
    [0x11] = {3,4}, /* w */
    [0x2d] = {3,5}, /* x */
    [0x15] = {4,2}, /* y */
    [0x2c] = {4,3}, /* z */
    [0xc7] = {5,0}, /* Home */
    [0x2a] = {5,1}, /* shift */
    [0x39] = {5,2}, /* space */
    [0x39] = {5,3}, /* space */
    [0x1c] = {5,5}, /*  enter */
    [0xc8] = {6,0}, /* up */
    [0xd0] = {6,1}, /* down */
    [0xcb] = {6,2}, /* left */
    [0xcd] = {6,3}, /* right */
};
/**************************************************************************/
/* LCD Module */
/**************************************************************************/

#include "pixel_ops.h"
#define draw_line_func drawfn

#define DEPTH 8
#include "omap_lcd_template.h"
#define DEPTH 15
#include "omap_lcd_template.h"
#define DEPTH 16
#include "omap_lcd_template.h"
#define DEPTH 32
#include "omap_lcd_template.h"

static draw_line_func draw_line_table2[33] = {
    [0 ... 32]  = 0,
    [8]     = draw_line2_8,
    [15]    = draw_line2_15,
    [16]    = draw_line2_16,
    [32]    = draw_line2_32,
}, draw_line_table4[33] = {
    [0 ... 32]  = 0,
    [8]     = draw_line4_8,
    [15]    = draw_line4_15,
    [16]    = draw_line4_16,
    [32]    = draw_line4_32,
}, draw_line_table8[33] = {
    [0 ... 32]  = 0,
    [8]     = draw_line8_8,
    [15]    = draw_line8_15,
    [16]    = draw_line8_16,
    [32]    = draw_line8_32,
}, draw_line_table12[33] = {
    [0 ... 32]  = 0,
    [8]     = draw_line12_8,
    [15]    = draw_line12_15,
    [16]    = draw_line12_16,
    [32]    = draw_line12_32,
}, draw_line_table16[33] = {
    [0 ... 32]  = 0,
    [8]     = draw_line16_8,
    [15]    = draw_line16_15,
    [16]    = draw_line16_16,
    [32]    = draw_line16_32,
};

static void iphone2g_lcd_update_display(void *opaque)
{
    iphone2g_lcd_s *lcd = (iphone2g_lcd_s *) opaque;
    draw_line_func draw_line;
    int src_width, dest_width;
    int height, first, last;
    int width, linesize, bpp;

	(void)draw_line_table12; // Unused var.

    if (!lcd || !lcd->ds || !ds_get_bits_per_pixel(lcd->ds) || !frame_base)
        return;

    switch (ds_get_bits_per_pixel(lcd->ds)) {
    case 8:
        dest_width = 1;
        break;
    case 15:
        dest_width = 2;
        break;
    case 16:
        dest_width = 2;
        break;
    case 24:
        dest_width = 3;
        break;
    case 32:
        dest_width = 4;
        break;
    default:
        fprintf(stderr, "%s: Bad color depth\n", __FUNCTION__);
        exit(1);
    }

    /* Colour depth */
    switch (4) {
    case 1:
        draw_line = draw_line_table2[ds_get_bits_per_pixel(lcd->ds)];
        bpp = 2;
        break;

    case 2:
        draw_line = draw_line_table4[ds_get_bits_per_pixel(lcd->ds)];
        bpp = 4;
        break;

    case 3:
        draw_line = draw_line_table8[ds_get_bits_per_pixel(lcd->ds)];
        bpp = 8;
        break;

    case 4 ... 7:
        draw_line = draw_line_table16[ds_get_bits_per_pixel(lcd->ds)];
        bpp = 16;
        break;

    default:
		fprintf(stderr, "%s: Bad color depth\n", __FUNCTION__);
        /* Unsupported at the moment.  */
        return;
    }

    /* Resolution */
    first = last = 0;
    width = 320;
    height = 480;
    lcd->invalidate = 1;

    /* Content */
    if (!ds_get_bits_per_pixel(lcd->ds))
        return;

    src_width =  width * bpp >> 3;
    linesize = ds_get_linesize(lcd->ds);

    framebuffer_update_display(lcd->ds, frame_base,
                               width, height,
                               src_width,       /* Length of source line, in bytes.  */
                               linesize,        /* Bytes between adjacent horizontal output pixels.  */
                               dest_width,      /* Bytes between adjacent vertical output pixels.  */
                               lcd->invalidate,
                               draw_line, lcd->palette,
                               &first, &last);
    if (first >= 0) {
        //fprintf(stderr, "dpy_update()\n");
        dpy_update(lcd->ds, 0, 0, width, height);
    }
    lcd->invalidate = 0;
}

static void iphone2g_lcd_invalidate_display(void *opaque) {
    iphone2g_lcd_s *iphone2g_lcd = opaque;
    iphone2g_lcd->invalidate = 1;
}

// Not implemented
static void iphone2g_lcd_screen_dump(void *opaque, const char *filename) {

}

// Not implemented
static uint32_t lcd_read(void *opaque, target_phys_addr_t offset)
{
    //iphone2g_lcd_s *s = (iphone2g_lcd_s *)opaque;
	//fprintf(stderr, "%s: offset 0x%08x\n", __FUNCTION__, offset);

    return 0;
}

static void lcd_write(void *opaque, target_phys_addr_t offset, uint32_t value)
{
	if(offset == 0x78) // Window 2 framebuffer. Doesn't detect active window yet!
	{
		// Framebuffer Address
		frame_base = value;
	}
}

static CPUReadMemoryFunc *lcd_readfn[] = {
    lcd_read,
    lcd_read,
    lcd_read,
};

static CPUWriteMemoryFunc *lcd_writefn[] = {
    lcd_write,
    lcd_write,
    lcd_write,
};

static iphone2g_lcd_s * iphone2g_lcd_init(target_phys_addr_t base)
{
    iphone2g_lcd_s *lcd = qemu_mallocz(sizeof(iphone2g_lcd_s));
    int io;

    io = cpu_register_io_memory(lcd_readfn, lcd_writefn, lcd, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0x7FF, io);

    lcd->ds = graphic_console_init(iphone2g_lcd_update_display,
                                   iphone2g_lcd_invalidate_display,
                                   iphone2g_lcd_screen_dump, NULL, lcd);
    return lcd;
}

static uint32_t aes_read(void *opaque, target_phys_addr_t offset)
{
	struct aes_s *aesop = (struct aes_s *)opaque;

	//fprintf(stderr, "%s: offset 0x%08x\n", __FUNCTION__, offset);

	switch(offset) {
		case AES_STATUS:
			return aesop->status;
	  default:
            //fprintf(stderr, "%s: UNMAPPED AES_ADDR @ offset 0x%08x\n", __FUNCTION__, offset);
			break;
	}

    return 0;
}

static void aes_write(void *opaque, target_phys_addr_t offset,
                       uint32_t value)
{
	struct aes_s *aesop = (struct aes_s *)opaque;
    static uint8_t keylenop = 0;

	uint8_t inbuf[0x1000];
	uint8_t *buf;

	//fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);

	switch(offset) {
			case AES_GO:
				memset(inbuf, 0, sizeof(inbuf));
				cpu_physical_memory_read((aesop->inaddr - 0x80000000), (uint8_t *)inbuf, aesop->insize);

				switch(aesop->keytype) {
						case AESGID:	
							fprintf(stderr, "%s: No support for GID key\n", __func__);
							return;			
						case AESUID:
						    AES_set_decrypt_key(key_uid, sizeof(key_uid) * 8, &aesop->decryptKey);
							break;
						case AESCustom:
							AES_set_decrypt_key((uint8_t *)aesop->custkey, 0x20 * 8, &aesop->decryptKey);
							break;
				}

				buf = (uint8_t *) qemu_mallocz(aesop->insize);

				AES_cbc_encrypt(inbuf, buf, aesop->insize, &aesop->decryptKey, (uint8_t *)aesop->ivec, aesop->operation);

				cpu_physical_memory_write((aesop->outaddr - 0x80000000), buf, aesop->insize);
				memset(aesop->custkey, 0, 0x20);
				memset(aesop->ivec, 0, 0x10);
				qemu_free(buf);
				keylenop = 0;
				aesop->outsize = aesop->insize;
				aesop->status = 0xf;
				break;
			case AES_KEYLEN:
				if(keylenop == 1) {
					aesop->operation = value;
				}
				keylenop++;
				aesop->keylen = value;
				break;
			case AES_INADDR:
				aesop->inaddr = value;
				break;
			case AES_INSIZE:
				aesop->insize = value;
				break;
			case AES_OUTSIZE:
                aesop->outsize = value;
                break;
			case AES_OUTADDR:
                aesop->outaddr = value;
                break;
			case AES_TYPE:
				aesop->keytype = value;
				break;
			case AES_KEY_REG ... ((AES_KEY_REG + AES_KEYSIZE) - 1):
				{
                    uint8_t idx = (offset - AES_KEY_REG) / 4;
                    aesop->custkey[idx] |= value;
					break;
				}
			case AES_IV_REG ... ((AES_IV_REG + AES_IVSIZE) -1 ):
				{
					uint8_t idx = (offset - AES_IV_REG) / 4;
					aesop->ivec[idx] |= value;
					break;
				}
		default:
			//fprintf(stderr, "%s: UNMAPPED AES_ADDR @ offset 0x%08x - 0x%08x\n", __FUNCTION__, offset, value);
			break;
	}

}

static CPUReadMemoryFunc *aes_readfn[] = {
    aes_read,
    aes_read,
    aes_read,
};

static CPUWriteMemoryFunc *aes_writefn[] = {
    aes_write,
    aes_write,
    aes_write,
};

static void aes_init(target_phys_addr_t base)
{
	struct aes_s *aesop = (struct aes_s *) qemu_mallocz(sizeof(aes_s));
    int io;

    io = cpu_register_io_memory(aes_readfn, aes_writefn, aesop, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xFF, io);
}

typedef struct sha1_status {
    uint32_t config;
    uint32_t reset;
    uint32_t hresult;
    uint32_t insize;
    uint32_t unkstat;
    uint8_t hashout[0x14];
} sha1_status_s;

static void sha1_reset(void *opaque)
{
    sha1_status_s *s = (sha1_status_s *)opaque;
	memset(s, 0, sizeof(sha1_status_s));
}

static uint32_t sha1_read(void *opaque, target_phys_addr_t offset)
{
	sha1_status_s *s = (sha1_status_s *)opaque;

    //fprintf(stderr, "%s: offset 0x%08x\n", __FUNCTION__, offset);

	switch(offset) {
		case SHA_CONFIG:
			return s->config;
		case SHA_RESET:
			return 0;
		case SHA_HRESULT:
			return s->hresult;
		case SHA_INSIZE:
			return s->insize;
		/* Hash result ouput */
		case 0x20 ... 0x34:
			//fprintf(stderr, "Hash out %08x\n",  *(uint32_t *)&s->hashout[offset - 0x20]);
			return *(uint32_t *)&s->hashout[offset - 0x20];
	}

    return 0;
}

static void sha1_write(void *opaque, target_phys_addr_t offset,
                       uint32_t value)
{
    sha1_status_s *s = (sha1_status_s *)opaque;

    //fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);

	switch(offset) {
		case SHA_CONFIG:
			if((value & 0x2) && (s->config & 0x8))
			{	
				uint8_t *hptr;

				if(!s->hresult || !s->insize)
					return;
				
				/* Why do they give us incorrect size? */
				s->insize += 0x20;

                hptr = (uint8_t *) qemu_mallocz(s->insize);
                cpu_physical_memory_read(s->hresult, (uint8_t *)hptr, s->insize);

    			SHA_CTX context;
    			SHA1_Init(&context);
    			SHA1_Update(&context, (uint8_t*) hptr, s->insize);
    			SHA1_Final(s->hashout, &context);
				
				qemu_free(hptr);
			} else {
				s->config = value;
			}
			break;
		case SHA_RESET:
			if(value & 1) 
				sha1_reset(s);
			break;
		case SHA_HRESULT:
			s->hresult = value;
			break;
		case SHA_INSIZE:
			s->insize = value;
			break;
		case SHA_UNKSTAT:
			s->unkstat = value;
			break;
	}


}

static CPUReadMemoryFunc * const sha1_readfn[] = {
    sha1_read,
    sha1_read,
    sha1_read,
};

static CPUWriteMemoryFunc * const sha1_writefn[] = {
    sha1_write,
    sha1_write,
    sha1_write,
};

static void sha1_init(target_phys_addr_t base)
{
    sha1_status_s *s = (sha1_status_s *) qemu_mallocz(sizeof(sha1_status_s));
    int iomemtype = cpu_register_io_memory(sha1_readfn,
                                           sha1_writefn,
                                           s, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xFF, iomemtype);
}

typedef struct iphone2gKeyState_s {
    struct  keymap *map;
} iphone2gKeyState_s;


static void iphone2g_keyboard_event (iphone2gKeyState_s *kp, int keycode)
{
    //fprintf(stderr, "Got keypad event 0x%02x\n", keycode);

       switch(keycode) {
        	case 0x36:
            	s5l8900_gpio_state[0].gpio_state |= 1 << (BUTTONS_HOME & 0xf);
                break;
            case 0xb6:
                s5l8900_gpio_state[0].gpio_state &= ~(1 << (BUTTONS_HOME & 0xf));
                break;
			case 0x01:
				s5l8900_gpio_state[0].gpio_state |= 1 << (BUTTONS_HOLD & 0xf);
                break;
            case 0x81:
                s5l8900_gpio_state[0].gpio_state &= ~(1 << (BUTTONS_HOLD & 0xf));
                break;
			case 0x51:
            	s5l8900_gpio_state[0].gpio_state &= ~(1 << (BUTTONS_VOLUP & 0xf));
                break;
			case 0xd1:
				s5l8900_gpio_state[0].gpio_state |= 1 << (BUTTONS_VOLUP & 0xf);
				break;
			case 0x53:
				s5l8900_gpio_state[0].gpio_state &= ~(1 << (BUTTONS_VOLDOWN & 0xf));
                break;
			case 0xd3:
				s5l8900_gpio_state[0].gpio_state |= 1 << (BUTTONS_VOLDOWN & 0xf);
				break;
       }

}

static void iphone2g_register_keyboard(void)
{
    iphone2gKeyState_s *kp = (iphone2gKeyState_s *) qemu_mallocz(sizeof(iphone2gKeyState_s));
    kp->map = map;
    qemu_add_kbd_event_handler((QEMUPutKBDEvent *) iphone2g_keyboard_event, kp);
}

static void iphone2g_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
	s5l8900_state *cpu;
    DriveInfo *dinfo;
 	uint32_t vrom_size, iboot_size;
	ram_addr_t phys_flash, ramoff;
	target_phys_addr_t vrom_base = VROM_BASE_ADDR;
	target_phys_addr_t iboot_base = IBOOT_BASE_ADDR;

    struct iphone2g_s *s = (struct iphone2g_s *) qemu_mallocz(sizeof(*s));

    g_debug_fp = stderr;

	cpu = s5l8900_init();

    iboot_size = 0x140000;

    vrom_size = get_image_size(option_rom[1].name);

	if(vrom_size != 0)
	{
		cpu_register_physical_memory(vrom_base, (ram_addr_t)vrom_size,(phys_flash = qemu_ram_alloc(NULL, "iphone2g.vrom", vrom_size)));
		load_image_targphys(option_rom[1].name, vrom_base, vrom_size);
	} else {
		printf("No vrom assuming OIB\n");
	}

	if(!get_image_size(option_rom[0].name))
	{
      printf("A valid bootloader iboot|openiboot must be supplied with -option-rom\n");
      exit(1);
  	}

    cpu_register_physical_memory(0x22000000, 0x500000, qemu_ram_alloc(NULL, "iphone2g.sram", 0x500000));

    cpu_register_physical_memory(iboot_base, (ram_addr_t)iboot_size, qemu_ram_alloc(NULL, "iphone2g.iboot", iboot_size) | IO_MEM_RAM);
    load_image_targphys(option_rom[0].name, iboot_base, iboot_size);


	ramoff = qemu_ram_alloc(NULL, "iphone2g.ram", RAM_SIZE);
	/* Map in default ram space */
    cpu_register_physical_memory(RAM_BASE_ADDR, RAM_SIZE, ramoff | IO_MEM_RAM);
	/* Also map higher ram space to default */
    //cpu_register_physical_memory(RAM_HIGH_ADDR, RAM_SIZE, ramoff | IO_MEM_RAM);
	/* Also also map to 0x0 as that's what OIB uses */
    cpu_register_physical_memory(0x0, RAM_SIZE, ramoff | IO_MEM_RAM);


    dinfo = drive_get(IF_PFLASH, 0, 0);

  	if (!dinfo) {
    	fprintf(stderr, "A NOR image must be given with the -pflash parameter\n");
        exit(1);
    }

    if(!pflash_cfi02_register(NOR_BASE_ADDR, qemu_ram_alloc(NULL, "iphone2g.xor", 1024*1024),
             dinfo->bdrv, 4096, 256,
                1, 2, 0x00bf, 0x273f, 0x0, 0x0, 0x555, 0x2aa, 0)) {
           fprintf(stderr, "qemu: Error registering NOR flash.\n");
            exit(1);
    }

	/* Init LCD */
	iphone2g_lcd_init(LCD_BASE_ADDR);

	/* Init AES hardware */
	aes_init(AES_BASE_ADDR);

	/* Init SHA1 hardware */
	sha1_init(SHA1_BASE_ADDR);
	
    /* Button emulation */
    iphone2g_register_keyboard();

	cpu->env->regs[15] = IBOOT_BASE_ADDR;
}

static QEMUMachine iphone2g_machine = {
    .name = "iPhone1",
    .desc = "iPhone 1",
    .init = iphone2g_init,
};

static void iphone2g_machine_init(void)
{
    qemu_register_machine(&iphone2g_machine);
}

machine_init(iphone2g_machine_init);

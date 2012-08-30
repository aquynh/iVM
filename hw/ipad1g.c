/*
 * iPad1G support
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

#include "ipad1g.h"
#include "s5l8930.h"

uint32_t g_debug = (//S5L8930_DEBUG_PMGR | //S5L8930_DEBUG_SHA1
					S5L8930_DEBUG_CDMA// | S5L8930_DEBUG_PMGR
					//| S5L8930_DEBUG_CLK
                    //0xffffffff |
					
                    );
uint32_t g_dlevel = 0;
FILE *g_debug_fp = NULL;

typedef struct ipad1g_state {
	struct s5l8930_state *cpu;
} ipad1g_s;

static target_phys_addr_t frame_base = 0;

#if 0
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
#endif
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

static uint32_t clcd_init = 0;

static void ipad1g_clcd_update_display(void *opaque)
{
    ipad1g_clcd_s *lcd = (ipad1g_clcd_s *) opaque;
    draw_line_func draw_line;
    int src_width, dest_width;
    int height, first, last;
    int width, linesize, bpp;

	(void)draw_line_table12; // Unused var.

/*
	if(clcd_init < 100){
		clcd_init++;
		return;
	}
*/
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
    width = 1024;
    height = 768;
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

static void ipad1g_clcd_invalidate_display(void *opaque) {
    ipad1g_clcd_s *ipad1g_clcd = opaque;
    ipad1g_clcd->invalidate = 1;
}

// Not implemented
static void ipad1g_clcd_screen_dump(void *opaque, const char *filename) {

}

static uint32_t clcd_read(void *opaque, target_phys_addr_t offset)
{
    //ipad1g_clcd_s *s = (ipad1g_clcd_s *)opaque;
	//fprintf(stderr, "%s: offset 0x%08x\n", __FUNCTION__, offset);

    return 0;
}

static void clcd_write(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    //ipad1g_clcd_s *s = (ipad1g_clcd_s *)opaque;
	fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);
	
    frame_base = CLCD_FRAMEBUFFER;

//	if(offset == 0x78) // Window 2 framebuffer. Doesn't detect active window yet!
/*
	{
		// Framebuffer Address
		//fprintf(stderr, "%s: Found framebuffer at 0x%08x.\n", __func__, value);
		frame_base = value;
	}
*/
}

static CPUReadMemoryFunc *clcd_readfn[] = {
    clcd_read,
    clcd_read,
    clcd_read,
};

static CPUWriteMemoryFunc *clcd_writefn[] = {
    clcd_write,
    clcd_write,
    clcd_write,
};

static ipad1g_clcd_s * ipad1g_clcd_init(target_phys_addr_t base)
{
    ipad1g_clcd_s *lcd = qemu_mallocz(sizeof(ipad1g_clcd_s));
    int io;

    io = cpu_register_io_memory(clcd_readfn, clcd_writefn, lcd, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xffff, io);

    lcd->ds = graphic_console_init(ipad1g_clcd_update_display,
                                   ipad1g_clcd_invalidate_display,
                                   ipad1g_clcd_screen_dump, NULL, lcd);
    return lcd;
}

#if 0
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
#endif

#if 0
static void unmapped_write(void *opaque, target_phys_addr_t offset,
                       uint32_t value)
{
    char *name = (char *)opaque;

    fprintf(stderr, "%s:%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, name, offset, value);
}

static uint32_t unmapped_read(void *opaque, target_phys_addr_t offset)
{
    char *name = (char *)opaque;

    fprintf(stderr, "%s:%s: offset 0x%08x\n", __FUNCTION__, name, offset);
    return 0;
}

static CPUReadMemoryFunc *unmapped_readfn[] = {
    unmapped_read,
    unmapped_read,
    unmapped_read,
};

static CPUWriteMemoryFunc *unmapped_writefn[] = {
    unmapped_write,
    unmapped_write,
    unmapped_write,
};

static void ipad1g_unmapped_hw_init(target_phys_addr_t base, int size, const char *name)
{
    int io;
    io = cpu_register_io_memory(unmapped_readfn, unmapped_writefn, (void *)name, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, size, io);
}

#endif
static void ipad1g_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
	s5l8930_state *cpu;
 	uint32_t llb_size, vrom_size;
	ram_addr_t phys_flash, ramoff;
	//target_phys_addr_t llb_base = LLB_LOAD_ADDR;
	target_phys_addr_t llb_base = IBOOT_LOAD_ADDR;
	//target_phys_addr_t llb_base = 0;
	target_phys_addr_t vrom_base  = 0x000000000;

    struct ipad1g_state *s = (struct ipad1g_state *) qemu_mallocz(sizeof(*s));

    g_debug_fp = stderr;

	cpu = s5l8930_init();

    llb_size = 0x100000; //get_image_size(option_rom[0].name);
    //llb_size = get_image_size(option_rom[0].name);

    //vrom_size = get_image_size(option_rom[0].name);
	vrom_size = 0;

    if(vrom_size != 0)
    {
        cpu_register_physical_memory(vrom_base, (ram_addr_t)vrom_size,(phys_flash = qemu_ram_alloc(NULL, "ipad1g.vrom", vrom_size)));
        load_image_targphys(option_rom[1].name, vrom_base, vrom_size);
		printf("Loaded vmrom @ 0x%08x with size %d\n", vrom_base, vrom_size);
    } else {
        printf("No vrom assuming OIB\n");
		//llb_base = 0x0;
		//llb_size = get_image_size(option_rom[0].name);
    }

	if(!get_image_size(option_rom[0].name))
	{
      printf("A valid llb image is required | openiboot must be supplied with -option-rom\n");
      exit(1);
  	}

    cpu_register_physical_memory(llb_base, (ram_addr_t)llb_size,(phys_flash = qemu_ram_alloc(NULL, "ipad1g.llb", llb_size)));
    load_image_targphys(option_rom[0].name, llb_base, llb_size);

	ramoff = qemu_ram_alloc(NULL, "ipad1g.ram", RAM_SIZE);
	printf("ramoff 0x%08x\n", ramoff);
	/* Map in default ram space */
    cpu_register_physical_memory(RAM_BASE_ADDR, RAM_SIZE, ramoff | IO_MEM_RAM);
	/* Also map higher ram space to default */
    cpu_register_physical_memory(RAM_HIGH_ADDR, RAM_SIZE, ramoff | IO_MEM_RAM);
    //cpu_register_physical_memory(KERNStart, RAM_SIZE, ramoff | IO_MEM_RAM);


	ramoff = qemu_ram_alloc(NULL, "iopram.ram", 0x100000);
	cpu_register_physical_memory(0x0, 0x100000, ramoff | IO_MEM_RAM);
	//* Also also map to 0x0 as that's what OIB uses */
    //cpu_register_physical_memory(0x0, RAM_SIZE, ramoff | IO_MEM_RAM);

    /* Init LCD */
    ipad1g_clcd_init(CLCD_BASE_ADDR);

	cpu->env->regs[15] = llb_base;
}

static QEMUMachine ipad1g_machine = {
    .name = "ipad1g",
    .desc = "iPad 1G",
    .max_cpus = 2,
    .init = ipad1g_init,
};

static void ipad1g_machine_init(void)
{
    qemu_register_machine(&ipad1g_machine);
}

machine_init(ipad1g_machine_init);

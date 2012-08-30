/*
 * Samsung S5L8930 processor support
 *
 * Written by cmw 
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
#include "s5l8930.h"
#include "usb_synopsys.h"
#include "net.h"
#include "i2c.h"

static void s5l8930_cdma_aes_init(target_phys_addr_t base, void *opaque);

int buffer_zero(uint8_t *buf, uint32_t size)
{
	int i;
	for(i=0; i < size;i++)
	{
		if(buf[i] != 0)
			return 0;
	}
	return 1;
}

typedef struct s5l8930_timer_s
{
    uint32_t    ticks_high;
    uint32_t    ticks_low;
    uint32_t    status;
    uint32_t    config;
	uint32_t    timer;
    uint32_t    prescaler;
    uint32_t    irqstat;

    QEMUTimer *st_timer;
    QEMUTimer *st_timer2;
    uint64_t tick_interval;
	uint64_t tick_interval2;
    qemu_irq    irq;
	qemu_irq	irq2;
	
	uint32_t	timer2;
	uint32_t    status2;

	uint32_t    val3030;

} s5l8930_timer_s;

void setTimerIRQ2(void *opaque, qemu_irq irq) {
    s5l8930_timer_s *s = (struct s5l8930_timer_s *) opaque;
    s->irq2 = irq;
}

static void s5l8930_st_tick(void *opaque);

static void s5l8930_st_set_timer(s5l8930_timer_s *s)
{
    uint64_t base = qemu_get_clock_ns(vm_clock);
	s->tick_interval = (((s->timer ? s->timer : 1000) * 1000) / 24);
	//s->tick_interval = 49992;
	//s->tick_interval = muldiv64(s->timer, get_ticks_per_sec(), 24000000);
    qemu_mod_timer(s->st_timer, base + s->tick_interval);

	//fprintf(stderr, "%s: starting timer. current %lld future %lld step %lld\n", __FUNCTION__, base, base + s->tick_interval, s->tick_interval);
}

/* counter step */
static void s5l8930_st_tick(void *opaque)
{
    s5l8930_timer_s *s = (s5l8930_timer_s *)opaque;
    if ((s->status & TIMER_STATE_START)) {
        //fprintf(stderr, "%s: Raising irq\n", __func__);
		qemu_irq_raise(s->irq);
		//s5l8930_st_set_timer(s);
    } 
    //s5l8930_st_set_timer(s);
}

static void s5l8930_st_set_timer2(s5l8930_timer_s *s)
{
    uint64_t base = qemu_get_clock_ns(vm_clock);
    s->tick_interval2 = (((s->timer2 ? s->timer2 : 1000) * 1000) / 24);
	//s->tick_interval2 = 49950000;
    //s->tick_interval = muldiv64(s->timer, get_ticks_per_sec(), 24000000);
    qemu_mod_timer(s->st_timer2, base + s->tick_interval2);

    //fprintf(stderr, "%s: starting timer. current %lld future %lld step %lld\n", __FUNCTION__, base, base + s->tick_interval2, s->tick_interval2);
}

/* counter step */
static void s5l8930_st_tick2(void *opaque)
{
    s5l8930_timer_s *s = (s5l8930_timer_s *)opaque;
    if ((s->status2 & TIMER_STATE_START)) {
        //fprintf(stderr, "%s: Raising timer2 irq\n", __func__);
        qemu_irq_raise(s->irq2);
        //s5l8930_st_set_timer2(s);
    }
}

static uint32_t s5l8930_timer1_read(void *opaque, target_phys_addr_t addr)
{
    s5l8930_timer_s *s = (struct s5l8930_timer_s *) opaque;
	uint64_t ticks;

    S5L8930_DEBUG(S5L8930_DEBUG_CLK, S5L8930_DLVL_ERR, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

    switch (addr) {
		case TIMER_TICKSHIGH:	 // needs to be fixed so that read from low first works as well
			ticks = (qemu_get_clock_ns(vm_clock) / 1000) * 24;
			s->ticks_high = (ticks >> 32);
			s->ticks_low = (ticks & 0xFFFFFFFF);
			return s->ticks_high;
		case TIMER_TICKSLOW:
            ticks = (qemu_get_clock_ns(vm_clock) / 1000) * 24;
            s->ticks_low = (ticks & 0xFFFFFFFF);
			return s->ticks_low;
		/* Timer is really sys reg which overlaps the MIU */
		case POWER_ID:
			return 0x2020001;
		case TIMER_REGISTER:
			return s->timer;
		case TIMER_REGISTER_TICK:
			return s->status;
		case TIMER_REGISTER2:
			return s->timer2;
		case TIMER_REGISTER_TICK2:
			return s->status2;
		case 0x3030:
			return s->val3030;
      default:
        S5L8930_DEBUG(S5L8930_DEBUG_CLK, S5L8930_DLVL_ERR, "%s: UNMAPPED offset = 0x%02x\n", __FUNCTION__, (int)addr);
		break;
    }
    return 0;
}

static void s5l8930_timer1_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{

    S5L8930_DEBUG(S5L8930_DEBUG_CLK, S5L8930_DLVL_ERR, "%s: offset = 0x%02x  value 0x%08x\n", __FUNCTION__, (int)addr, value);
    s5l8930_timer_s *s = (struct s5l8930_timer_s *) opaque;

    switch(addr){
		case 0x3030:
			s->val3030 = value;
			break;
		case TIMER_REGISTER_TICK:
			qemu_del_timer(s->st_timer);

			if (value & TIMER_STATE_MANUALUPDATE)
				qemu_irq_lower(s->irq);

            if (value & TIMER_STATE_START)
				s5l8930_st_set_timer(s);

            if (!(value & TIMER_STATE_START) || (value & TIMER_STATE_MANUALUPDATE)) {
				qemu_irq_lower(s->irq);
			}
            s->status = value;
            break;
		case TIMER_REGISTER:
            s->timer = value;
			if(value == 0xffffffff) {
				s->timer = 0;
				return;
			}
			
            if (s->status & TIMER_STATE_START) {
                s5l8930_st_set_timer(s);
            }
			break;
        case TIMER_REGISTER_TICK2:
            qemu_del_timer(s->st_timer2);
            if (!(value & TIMER_STATE_START) || (value & TIMER_STATE_MANUALUPDATE)) {
                qemu_irq_lower(s->irq2);
            }
            s->status2 = value;
            break;
        case TIMER_REGISTER2:
            s->timer2 = value;
            if(value == 0xffffffff) {
                s->timer2 = 0;
                return;
            }

            if (s->status2 & TIMER_STATE_START) {
                s5l8930_st_set_timer2(s);
            }
            break;

      default:
		S5L8930_DEBUG(S5L8930_DEBUG_CLK, S5L8930_DLVL_ERR, "%s: UNMAPPED offset = 0x%02x value = 0x%08x\n", __FUNCTION__, (int)addr, value);
        break;
    }

    return;
}

static CPUReadMemoryFunc *s5l8930_timer1_readfn[] = {
    s5l8930_timer1_read,
    s5l8930_timer1_read,
    s5l8930_timer1_read,
};

static CPUWriteMemoryFunc *s5l8930_timer1_writefn[] = {
    s5l8930_timer1_write,
    s5l8930_timer1_write,
    s5l8930_timer1_write,
};

static void *s5l8930_timer_init(target_phys_addr_t base, qemu_irq irq)
{
    struct s5l8930_timer_s *timer1 = (struct s5l8930_timer_s *) qemu_mallocz(sizeof(struct s5l8930_timer_s));

    int iomemtype = cpu_register_io_memory(s5l8930_timer1_readfn,
                                           s5l8930_timer1_writefn, timer1, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xffff, iomemtype);
    timer1->irq = irq;
    timer1->st_timer = qemu_new_timer_ns(vm_clock, s5l8930_st_tick, timer1);

	timer1->st_timer2 = qemu_new_timer_ns(vm_clock, s5l8930_st_tick2, timer1);

	return timer1;
}

static void s5l8930_misc_sys_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
	S5L8930_DEBUG(S5L8930_DEBUG_MISCSYS, S5L8930_DLVL_ERR, "%s: addr 0x%08x value 0x%08x\n", __FUNCTION__, addr, value);
}

static uint32_t s5l8930_misc_sys_read(void *opaque, target_phys_addr_t addr)
{
	S5L8930_DEBUG(S5L8930_DEBUG_MISCSYS, S5L8930_DLVL_ERR, "%s: addr 0x%08x\n", __FUNCTION__, addr);

	switch(addr){
		case 0x104:
			return 0x0;
		case 0x144:
		case 0x184:
			return 0x3;
	  default:
		return 0;
	}
}

static CPUReadMemoryFunc *s5l8930_misc_sys_readfn[] = {
    s5l8930_misc_sys_read,
    s5l8930_misc_sys_read,
    s5l8930_misc_sys_read,
};

static CPUWriteMemoryFunc *s5l8930_misc_sys_writefn[] = {
    s5l8930_misc_sys_write,
    s5l8930_misc_sys_write,
    s5l8930_misc_sys_write,
};

static void s5l8930_misc_sys_init(target_phys_addr_t base)
{
    int iomemtype = cpu_register_io_memory(s5l8930_misc_sys_readfn,
                                           s5l8930_misc_sys_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xfff, iomemtype);
}

typedef struct s5l8930_pmgr_s
{
    uint32_t armclk;
	uint32_t base0;
	uint32_t base1;
	uint32_t base2;
	uint32_t nc0_ref0;
	uint32_t medium0;
	uint32_t medium1;
	uint32_t hperf0;
	uint32_t hperf1;
	uint32_t hperf2clk;
	uint32_t vid1;
	uint32_t audioclk;
	uint32_t lperf1;
	uint32_t mipiclk;
	uint32_t prediv0;
	uint32_t prediv1;
	uint32_t prediv2;
	uint32_t prediv3;
	uint32_t prediv4;
	uint32_t sdioclk;
	uint32_t cdma;
	uint32_t d4clk;
} s5l8930_pmgr_s;

static void s5l8930_pmgr_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    S5L8930_DEBUG(S5L8930_DEBUG_PMGR, S5L8930_DLVL_ERR, "%s: addr 0x%08x value 0x%08x\n", __FUNCTION__, addr, value);

    s5l8930_pmgr_s *s = (struct s5l8930_pmgr_s *) opaque;

	switch(addr) {
			case 0x40:
				s->armclk = value;
				break;
			case 0x44:
				s->prediv0 = value;	
				break;
			case 0x48:
				s->prediv1 = value;
				break;
			case 0x4c:
				s->prediv2 = value;
				break;
			case 0x50:
				s->prediv3 = value;
				break;
			case 0x54:
            	s->prediv4 = value;
				break;
			case 0x5c:
				s->vid1 = value;
				break;
			case 0x60:
				s->nc0_ref0 = value;
				break;
            case 0x68:
				s->medium0 = value;
				break;
			case 0x70:
				s->base0 = value;
				break;
			case 0x74:
				s->base1 = value;
				break;
			case 0x78:
				s->base2 = value;
				break;
			case 0x84:
				s->audioclk = value;
			case 0x88:
				s->mipiclk = value;
				break;
			case 0x90:
				s->hperf1 = value;
				break;
			case 0x98:
				s->hperf0 = value;
				break;
			case 0x9c:
				s->hperf2clk = value;
				break;
			case 0xa0:
				s->cdma = value;
				break;
			case 0xa8:
				s->lperf1 = value;
				break;
			case 0xb0:
				s->sdioclk = value;
				break;
            case 0xCC:
                s->medium1 = value;
                break;
			case 0xd4:
				s->d4clk = value;
				break;
		default:
			break;
	}
}

static uint32_t s5l8930_pmgr_read(void *opaque, target_phys_addr_t addr)
{
    S5L8930_DEBUG(S5L8930_DEBUG_PMGR, S5L8930_DLVL_ERR, "%s: addr 0x%08x\n", __FUNCTION__, addr);
    s5l8930_pmgr_s *s = (struct s5l8930_pmgr_s *) opaque;

    switch(addr){
        case 0x0:
			return 0xa00187d1;
            //return (1 << 29);
		case 0x4:
			return 0x380960;
		case 0x8:
			return 0x40000000;
		case 0x10:
			return 0xa0010559;
		case 0x20:	
			return 0xa0008205;
		case 0x40:
			return s->armclk;
		case 0x44:
			return s->prediv0;
		case 0x48:
			return s->prediv1;
			//return 0xa000000a;
		case 0x4c:
			return s->prediv2;
			//return 0xa0000001;
		case 0x50:
			return s->prediv3;
			//return 0x80000005;
		case 0x54:
			return s->prediv4;
			//return 0xa000000f;
		case 0x5c:
			return s->vid1;
		case 0x60:
			return s->nc0_ref0;
		case 0x64:
			return 0x30000000;
		case 0x68:
			return s->medium0;
		case 0x70:
			return s->base0;
		case 0x74:
			return s->base1;
		case 0x78:
			return s->base2;
		case 0x84:
			return s->audioclk;
		case 0x88:
			return s->mipiclk;
		case 0x8c:
			return 0xa0000006;
		case 0x90:
			return s->hperf1;
		case 0x98:
			return s->hperf0;
		case 0x9c:
			return s->hperf2clk;
		case 0xa0:
			return s->cdma;
		case 0xa4:
			return 0x90000000;
		case 0xa8:
			return s->lperf1;
		case 0xac:	
			return 0x90000000;
		case 0xb0:
			return s->sdioclk;
		case 0xb4:
			return 0x90000000;
		case 0xc4:
			return 0x8000000c;
        case 0xCC:
            return s->medium1;
		case 0xd4:
			return s->d4clk;
    }
	return 0x0;
}
/* 
static const clock_t clocks[] = {
	{ 0,					0,	{ 0,	0,	0,	0 }},
	{ 0,					0,	{ 0,	0,	0,	0 }},
	{ 0,					0,	{ 0,	0,	0,	0 }},
	{ 0,					0,	{ 0,	0,	0,	0 }},
	{ 0,					0,	{ 0,	0,	0,	0 }},
	{ PMGR0_BASE + 0x40,	1,	{ 1,	2,	3,	0 }}, // ARMCLK
	{ PMGR0_BASE + 0x40,	2,	{ 5,	5,	5,	5 }},
	{ PMGR0_BASE + 0x40,	3,	{ 1,	2,	3,	0 }},
	{ PMGR0_BASE + 0x40,	4,	{ 1,	2,	3,	0 }},
	{ PMGR0_BASE + 0x40,	5,	{ 1,	2,	3,	0 }},
	{ PMGR0_BASE + 0x44,	1,	{ 7,	8,	3,	0 }}, // 10	// PREDIV0
	{ PMGR0_BASE + 0x48,	1,	{ 7,	8,	3,	0 }},		// PREDIV1
	{ PMGR0_BASE + 0x4C,	1,	{ 7,	8,	3,	0 }},		// PREDIV2
	{ PMGR0_BASE + 0x50,	1,	{ 7,	8,	3,	0 }},		// PREDIV3
	{ PMGR0_BASE + 0x54,	1,	{ 7,	8,	3,	0 }},		// PREDIV4
	{ PMGR0_BASE + 0x70,	0,	{ 0xA,	0xB,	0xC,	0xD }},	// BASE0
	{ PMGR0_BASE + 0x70,	1,	{ 0xF,	0xF,	0xF,	0xF }},	// BASE0_DIV0
	{ PMGR0_BASE + 0x70,	2,	{ 0xF,	0xF,	0xF,	0xF }},	// BASE0_DIV1
	{ PMGR0_BASE + 0x74,	0,	{ 0xA,	0xB,	0xC,	0xD }},	// BASE1
	{ PMGR0_BASE + 0x74,	1,	{ 0x12,	0x12,	0x12,	0x12 }},// BASE1_DIV0
	{ PMGR0_BASE + 0x78,	1,	{ 9,	9,	9,	9 }}, // 20		// BASE2
	{ PMGR0_BASE + 0x68,	1,	{ 7,	8,	3,	0 }},	// MEDIUM0
	{ PMGR0_BASE + 0xCC,	1,	{ 4,	4,	4,	4 }},	// MEDIUM1
	{ PMGR0_BASE + 0xC4,	1,	{ 0,	0,	0,	0 }},	
	{ PMGR0_BASE + 0x60,	1,	{ 7,	8,	3,	0 }},	// NCO_REF0
	{ PMGR0_BASE + 0x64,	1,	{ 7,	8,	3,	0 }},	// NCO_REF1
	{ PMGR0_BASE + 0xA0,	1,	{ 0xF,	0x10,	0x14,	0x12 }}, // CDMA
	{ PMGR0_BASE + 0xB4,	0,	{ 0x13,	0x10,	0x11,	0x12 }},
	{ PMGR0_BASE + 0x98,	0,	{ 0xF,	0x10,	0x14,	0x12 }}, // HPERF0
	{ PMGR0_BASE + 0x90,	1,	{ 0xA,	0xB,	0xC,	0xD }},	// HPERF1
	{ PMGR0_BASE + 0x9C,	0,	{ 0xF,	0x10,	0x14,	0x12 }}, // 30 // HPERF2-CLK
	{ PMGR0_BASE + 0x8C,	1,	{ 0xA,	0xB,	0xC,	0xD }},		// MPERF
	{ PMGR0_BASE + 0xA4,	0,	{ 0x13,	0x10,	0x11,	0x12 }},	// LPERF0
	{ PMGR0_BASE + 0xA8,	0,	{ 0xF,	0x10,	0x14,	0x12 }},	// LPERF1
	{ PMGR0_BASE + 0xAC,	0,	{ 0x13,	0x10,	0x11,	0x12 }},	// LPERF2
	{ PMGR0_BASE + 0xB0,	0,	{ 0x13,	0x10,	0x11,	0x12 }},	// LPERF3
	{ PMGR0_BASE + 0x94,	1,	{ 0xA,	0xB,	0xC,	0xE }},	// VID0-CLK
	{ PMGR0_BASE + 0x5C,	1,	{ 7,	8,	3,	0 }},			// VID1-CLK
	{ PMGR0_BASE + 0x80,	1,	{ 0xA,	0xB,	0xC,	0xD }},
	{ PMGR0_BASE + 0x84,	1,	{ 0xA,	0xB,	0xC,	0xD }}, // AUDIO-CLK
	{ PMGR0_BASE + 0x88,	1,	{ 0xA,	0xB,	0xC,	0xD }}, // 40 // MIPI-CLK
	{ PMGR0_BASE + 0xB8,	0,	{ 0x13,	0x10,	0x11,	0x12 }}, // SDIO-CLKs
	{ PMGR0_BASE + 0xFC,	1,	{ 0x17,	0x17,	0x17,	0x17 }},
	{ PMGR0_BASE + 0xBC,	0,	{ 0x13,	0x10,	0x11,	0x12 }}, // CLK50-CLK
	{ PMGR0_BASE + 0xC8,	1,	{ 4,	4,	4,	4 }},
	{ PMGR0_BASE + 0xD0,	0,	{ 0x15,	0x16,	0,	0 }},
	{ PMGR0_BASE + 0xD4,	1,	{ 0,	0,	0,	0 }},
	{ PMGR0_BASE + 0xDC,	0,	{ 0x15,	0x16,	0,	0 }},		// SPI0
	{ PMGR0_BASE + 0xE0,	0,	{ 0x15,	0x16,	0,	0 }},		// SPI1
	{ PMGR0_BASE + 0xE4,	0,	{ 0x15,	0x16,	0,	0 }},		// SPI2	
	{ PMGR0_BASE + 0xE8,	0,	{ 0x15,	0x16,	0,	0 }}, // 50 // SPI3
	{ PMGR0_BASE + 0xEC,	0,	{ 0x15,	0x16,	0,	0 }},		// SPI4
	{ PMGR0_BASE + 0xF0,	1,	{ 0x17,	0x17,	0x17,	0x17 }}, // I2C0
	{ PMGR0_BASE + 0xF4,	1,	{ 0x17,	0x17,	0x17,	0x17 }}, // I2C1
	{ PMGR0_BASE + 0xF8,	1,	{ 0x17,	0x17,	0x17,	0x17 }}  // I2C2
};*/
static CPUReadMemoryFunc *s5l8930_pmgr_readfn[] = {
    s5l8930_pmgr_read,
    s5l8930_pmgr_read,
    s5l8930_pmgr_read,
};

static CPUWriteMemoryFunc *s5l8930_pmgr_writefn[] = {
    s5l8930_pmgr_write,
    s5l8930_pmgr_write,
    s5l8930_pmgr_write,
};

void s5l8930_pmgr_reset(s5l8930_pmgr_s *pmgr)
{
/*
    uint32_t armclk;
    uint32_t base0;
    uint32_t base1;
    uint32_t base2;
    uint32_t medium0;
    uint32_t medium1;
    uint32_t hperf0;
    uint32_t hperf1;
*/
	pmgr->armclk = 0x80000000;
	pmgr->base0 = 0x80000002;
	pmgr->base1 = 0x90000002;
	pmgr->base2 = 0x80000004;
	pmgr->medium0 = 0xa0000015;
	pmgr->medium1 = 0x80000001;
	pmgr->hperf0 = 0xa0000000;
	pmgr->hperf1 = 0xa0000000;
	pmgr->hperf2clk = 0xa0000000;
	pmgr->lperf1 = 0xb0000000; 
    pmgr->prediv0 = 0x80000004;
	pmgr->prediv1 = 0xa000000a;
	pmgr->prediv2 = 0xa0000001;
	pmgr->prediv3 = 0x80000005;
	pmgr->prediv4 = 0xa000000f;
	pmgr->nc0_ref0 = 0xa0000003;
}

static void s5l8930_pmgr_init(target_phys_addr_t base)
{

    struct s5l8930_pmgr_s *pmgr = (struct s5l8930_pmgr_s *) qemu_mallocz(sizeof(struct s5l8930_pmgr_s));

    int iomemtype = cpu_register_io_memory(s5l8930_pmgr_readfn,
                                           s5l8930_pmgr_writefn, pmgr, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xfff, iomemtype);

	s5l8930_pmgr_reset(pmgr);
}

static uint32_t d_counter = 0;
void do_aes_crypto(uint32_t *inBuf, uint32_t *outBuf, uint32_t size, uint32_t operation, void *opaque)
{
		s5l8930_cdma_s *cdma = (s5l8930_cdma_s *) opaque;
/*
		char debugname[1024];

		fprintf(stderr, "%s: AES_GO of size %x\n", __FUNCTION__, size);
*/
		AES_set_decrypt_key((uint8_t *)cdma->custkey, cdma->keyLen, &cdma->decryptKey);
/*
	
		sprintf(debugname, "/tmp/aes/inbuf-%d.img", d_counter);
        FILE *fp = fopen(debugname, "w");
        fwrite(inBuf, 1, size, fp);
        fclose(fp);
*/
		AES_cbc_encrypt((uint8_t *)inBuf, (uint8_t *)outBuf, size, &cdma->decryptKey, (uint8_t *)cdma->ivec, operation);
/*
		sprintf(debugname, "/tmp/aes/outbuf-%d.img", d_counter);
        fp = fopen(debugname, "w");
        fwrite(outBuf, 1, size, fp);
        fclose(fp);
		d_counter++;
*/
}

static uint32_t s5l8930_cdma_read(void *opaque, target_phys_addr_t addr)
{
	s5l8930_cdma_s *cdma = (s5l8930_cdma_s *) opaque;
	uint32_t channel_reg = addr >> 12;

    S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: addr 0x%08x\n", __FUNCTION__, addr);

    if(channel_reg > 0x8) {
        fprintf(stderr, "%s: addr 0x%08x\n", __FUNCTION__, addr);
    }

    switch (addr & 0xff) {		
		case 0x0: /* status */
			//fprintf(stderr, "%s: returning status of 0x%08x for channel %d\n", __FUNCTION__,cdma->status[channel_reg], channel_reg);
			return cdma->status[channel_reg];
		case 0x4: /* config */
			return cdma->config[channel_reg];
		case 0x8: /* destination reg */
			return cdma->creg[channel_reg];
		case 0xc: /* Size */
			return cdma->size[channel_reg];
		case 0x10:
			return cdma->mstatus;
		case 0x14: /* Buffer */
			return 0;
    }
    return 0;
}

static void s5l8930_cdma_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    s5l8930_cdma_s *cdma = (s5l8930_cdma_s *) opaque;
    uint32_t channel_reg = addr >> 12;

    S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: addr 0x%08x value 0x%08x\n", __FUNCTION__, addr, value);

#if 0
	if(!(addr >> 8))
	{
		fprintf(stderr, "%s do we even need base reg for this?\n", __FUNCTION__);
		return;
	}
#endif
	if(channel_reg > 0x8) {
		fprintf(stderr, "%s: addr 0x%08x value 0x%08x\n", __FUNCTION__, addr, value);
	}
    switch (addr & 0xff) {
			case 0x0: /* Status */
				//Clear IRQ
				if((value & 0x80000) && (cdma->status[channel_reg] & 0x80000)) {
					//fprintf(stderr, "%s: Clearing IRQ for dma channel %d\n", __FUNCTION__, channel_reg);
					qemu_irq_lower(cdma->irqs[channel_reg]);
					value &= ~(1 << 19);
                    cdma->mstatus &= ~(1 << channel_reg);
				}
				// DMA_START
				if(value & 1) {
					/* First channel reg is used for inbound AES transfers. Second is used for outbound */
					switch(channel_reg) {
						case 1:
							break;
						case 2:
							//S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: AES_GO type %d size %x keyLen %d\n", __FUNCTION__, cdma->aesOperation, cdma->dmaSegment[channel_reg]->size, cdma->keyLen);
							fprintf(stderr, "%s: AES_GO type %d size %x keyLen %d keyType %d\n", __FUNCTION__, cdma->aesOperation, cdma->dmaSegment[channel_reg]->size, cdma->keyLen, cdma->keyType);
							{
								uint8_t *aesOutBuf = (uint8_t *)qemu_mallocz(cdma->dmaSegment[channel_reg]->size);
								uint8_t *aesInBuf = (uint8_t *)qemu_mallocz(cdma->dmaSegment[channel_reg]->size);
								char debugname[1024];
								uint8_t iv[16] = {0};
								
								switch(cdma->keyType) {
                                    case AESUID:
										S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: AESUID key in use \n", __FUNCTION__);
                                        AES_set_decrypt_key(key_uid, sizeof(key_uid) * 8, &cdma->decryptKey);
                                        break;
									case AESGID:
										/* We cant do anything here as we dont know the GID key */
										S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: AESGID requested!!! bad news mate\n", __FUNCTION__);
										break;
									case AESCustom:
										/* test */
										/*
										if(cdma->dmaSegment[channel_reg]->size == 0x80)
										{
                                        	S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: AESCustom key in use \n", __FUNCTION__);
											AES_set_decrypt_key(key_test, 128, &cdma->decryptKey);
											memset(cdma->ivec, 0x0, 0x10);
										} else {
										*/
										AES_set_decrypt_key((uint8_t *)cdma->custkey, cdma->keyLen, &cdma->decryptKey);
										break;
								}

								cpu_physical_memory_read((target_phys_addr_t)cdma->dmaSegment[1]->buffer, (uint8_t *)aesInBuf, cdma->dmaSegment[channel_reg]->size);
	
                                    sprintf(debugname, "/tmp/aes/inbuf-%d.img", d_counter);
                                    FILE *fp = fopen(debugname, "w");
                                    fwrite(aesInBuf, 1, cdma->dmaSegment[channel_reg]->size, fp);
                                    fclose(fp);
								switch(cdma->dmaSegment[channel_reg]->size) {
										case 0x5e42c0:
												AES_set_decrypt_key((uint8_t *)kernKey435, cdma->keyLen, &cdma->decryptKey);
												AES_cbc_encrypt(aesInBuf, aesOutBuf, cdma->dmaSegment[channel_reg]->size, &cdma->decryptKey, kernIV435, cdma->aesOperation);
												break;
										case 0xe9a0:
												AES_set_decrypt_key((uint8_t *)deviceKey435, cdma->keyLen, &cdma->decryptKey);
												AES_cbc_encrypt(aesInBuf, aesOutBuf, cdma->dmaSegment[channel_reg]->size, &cdma->decryptKey, deviceIV435, cdma->aesOperation);
												break;
									default:
										memset(iv, 0, 0x10);
										AES_cbc_encrypt(aesInBuf, aesOutBuf, cdma->dmaSegment[channel_reg]->size, &cdma->decryptKey, iv, cdma->aesOperation);
										break;
								}
								/* 
								switch(d_counter) {
									case 0:
										memcpy(aesOutBuf, Key835, 16);
										break;
									case 1:
										memcpy(aesOutBuf, Key89B, 16);
										break;
								  default:
									break;
								}
								*/
								if(cdma->keyType == AESCustom)
								{
                                    sprintf(debugname, "/tmp/aes/key-%d.img", d_counter);
                                    FILE *fp = fopen(debugname, "w");
                                    fwrite(cdma->custkey, 1, 32, fp);
                                    fclose(fp);

								}
								//if(cdma->dmaSegment[channel_reg]->size == 0x5e42c0) {
									sprintf(debugname, "/tmp/aes/outbuf-%d.img", d_counter);
									fp = fopen(debugname, "w");
									fwrite(aesOutBuf, 1, cdma->dmaSegment[channel_reg]->size, fp);
									fclose(fp);
								//}
								d_counter++;

								cpu_physical_memory_write((target_phys_addr_t)cdma->dmaSegment[channel_reg]->buffer, aesOutBuf, cdma->dmaSegment[channel_reg]->size);

								qemu_free(aesOutBuf);
								qemu_free(aesInBuf);
								memset(cdma->ivec, 0, 0x10);
							}
							cdma->keyLen = 0;
							cdma->keyType = 0;
							cdma->aesOperation = 0;
							break;
					case 0x5: /* H2FMI 0 */
                    case 0x6: /* H2FMI 0 META */
                    case 0x7: /* H2FMI 1 */
                    case 0x8: /* H2FMI 1 META */
						{
							uint8_t direction = (cdma->config[channel_reg] & 0x2);
							if(direction) {	
								// its a write
								fprintf(stderr, "%s: got write\n", __FUNCTION__);
							} else {
								// its a read
								segmentBuffer segBuf;
								uint8_t *outBuf, *inBuf;
								uint32_t i, soffset;
                                uint32_t nextSeg, firstSeg = cdma->dmaSegment[channel_reg]->address;
								size_t size = 0;

								/* If FLAG_DATA is set then no AES */
								if(cdma->dmaSegment[channel_reg]->flags & 1)
									firstSeg = cdma->segptr[channel_reg];
								else {
                                    memcpy(cdma->ivec, cdma->dmaSegment[channel_reg]->iv, 0x10);
									/*
									fprintf(stderr, "%s: printing iv: {", __FUNCTION__);
									for(i=0;i < 0x10;i++) {
										fprintf(stderr, "0x%02x,", (uint8_t)cdma->ivec[i]);
									}
                                	fprintf(stderr, "\n");
									*/
								}

								nextSeg = firstSeg;
                                inBuf = (uint8_t *)qemu_mallocz(cdma->size[channel_reg] + 4);
                                outBuf = (uint8_t *)qemu_mallocz(cdma->size[channel_reg] + 4);
								soffset = 0;

								do 
								{
									// read segments
									fprintf(stderr, "%s: reading seg 0x%08x\n", __func__, nextSeg);
									cpu_physical_memory_read((target_phys_addr_t)nextSeg, (uint8_t *)&segBuf, sizeof(segmentBuffer));

									fprintf(stderr, "%s: got seg 0x%08x, addr 0x%08x, size 0x%08x\n", __func__, segBuf.flags,
											segBuf.address, segBuf.size);

									if(segBuf.flags & 3)
									{

										fprintf(stderr, "%s: reading segment size %d\n", __FUNCTION__, segBuf.size);
										if(size + segBuf.size > cdma->size[channel_reg])
										{
											fprintf(stderr, "%s: used too much buffer!\n", __func__);
											break;
										}

									    //for(i=0; i < segBuf.size; i += 4)
									    //	cpu_physical_memory_read((target_phys_addr_t)cdma->creg[channel_reg], inBuf+i, 4);
										//cpu_physical_memory_read((target_phys_addr_t)cdma->creg[channel_reg], inBuf, segBuf.size);
										for(i = 0; i < segBuf.size; i++)
											cpu_physical_memory_read((target_phys_addr_t)cdma->creg[channel_reg], inBuf+size+i, 1);
										size += segBuf.size;
									}
									
									nextSeg = segBuf.address;
									fprintf(stderr, "%s: next seg 0x%08x\n", __func__, nextSeg);

								} while(nextSeg && size < cdma->size[channel_reg]);

								if(size != cdma->size[channel_reg])
									fprintf(stderr, "%s: size incorrect! Expected %d, got %d.\n", __func__, cdma->size[channel_reg], size);

                                if((cdma->dmaSegment[channel_reg]->flags & 0x1) || buffer_zero(inBuf, cdma->size[channel_reg]))
                              		memcpy(outBuf, inBuf, size);
								else { 
									//fprintf(stderr, "%s: doing crypto on %x bytes\n", __FUNCTION__, size);
                                	do_aes_crypto((uint32_t *)inBuf, (uint32_t *)outBuf, cdma->size[channel_reg], AES_DECRYPT, cdma);
								}

								nextSeg = firstSeg;
								soffset = 0;

								do
								{
									// read segments
									fprintf(stderr, "%s: reading seg 0x%08x\n", __func__, nextSeg);
									cpu_physical_memory_read((target_phys_addr_t)nextSeg, (uint8_t *)&segBuf, sizeof(segmentBuffer));

									if(!(segBuf.flags & 2))
										break;

									fprintf(stderr, "%s: writing out segment size %d to location 0x%08x segptr 0x%08x\n", __FUNCTION__, segBuf.size, segBuf.buffer, nextSeg);

									if(!(channel_reg & 1))
									{
										fprintf(stderr, "meta: ");
										int b;
										for(b = 0; b < segBuf.size; b++)
											fprintf(stderr, "%02x ", outBuf[b]);
										fprintf(stderr, "\n");
									}

                                    cpu_physical_memory_write((target_phys_addr_t)segBuf.buffer, (uint8_t *)outBuf+soffset, segBuf.size);
									soffset += segBuf.size;
                                    nextSeg = segBuf.address;

								} while(segBuf.flags & 0x1);

					            cdma->size[channel_reg] = 0;
								cdma->status[channel_reg] &= ~((1 << 19) | (1 << 18));
								cdma->status[channel_reg] |= 0x80000;
								cdma->mstatus |= 1 << channel_reg;
								//cdma->cstatus[channel_reg] &= ~((1 << 16) | (1 << 17));
								//cpu_physical_memory_write((target_phys_addr_t)0x5FF29590, &cdma->size[channel_reg], 4);

                                qemu_free(inBuf);
                                qemu_free(outBuf);
								memset(cdma->ivec, 0, 0x10);
								//fprintf(stderr, "%s: triggering IRQ for channel %d\n", __FUNCTION__, channel_reg);	
								qemu_irq_raise(cdma->irqs[channel_reg]);
							}
							return;
						}
						break;
					}
				}
				//fprintf(stderr, "%s: setting status for channel %d to value 0x%08x\n", __FUNCTION__, channel_reg, value);
				cdma->status[channel_reg] = value;
				break;
			case 0x4: /* config reg */
					cdma->config[channel_reg] = value;
					break;
			case 0x8: /* destination reg */
					cdma->creg[channel_reg] = value;
					break;
			case 0xc: /* csize */
					cdma->size[channel_reg] = value;
					break;
			case 0x10:
					cdma->mstatus = value;
					break;
			case 0x14: /* Segment pointer */
				if(!value)
					break;

				switch(channel_reg) {
					case 1: /* Only grab operation from inbound dma for aes */
					cdma->aesOperation = (cdma->dmaAesSetup[channel_reg] >> 16) & 1;
				 default:
				   if(cdma->dmaSegment[channel_reg])
				   {
					   // its stale.. lets flush 
					   qemu_free(cdma->dmaSegment[channel_reg]);
				   }
				   cdma->dmaSegment[channel_reg] = (segmentBuffer *)qemu_mallocz(sizeof(segmentBuffer));
				   cpu_physical_memory_read(value, (uint8_t *)cdma->dmaSegment[channel_reg], sizeof(segmentBuffer));
				   //S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: dmaAes got buffer 0x%08x - size %x\n", __FUNCTION__, cdma->dmaAes[channel_reg]->buffer, cdma->dmaAes[channel_reg]->size);
				}
				cdma->segptr[channel_reg] = value;	
				break;
	}
}

static CPUReadMemoryFunc *s5l8930_cdma_readfn[] = {
    s5l8930_cdma_read,
    s5l8930_cdma_read,
    s5l8930_cdma_read,
};

static CPUWriteMemoryFunc *s5l8930_cdma_writefn[] = {
    s5l8930_cdma_write,
    s5l8930_cdma_write,
    s5l8930_cdma_write,
};

void replaceCDMAIRQHandlers(void *opaque, qemu_irq dma5, qemu_irq dma6, qemu_irq dma7, qemu_irq dma8)
{
    s5l8930_cdma_s *cdma = (s5l8930_cdma_s *) opaque;
    cdma->irqs[5] = dma5;
    cdma->irqs[6] = dma6;
    cdma->irqs[7] = dma7;
    cdma->irqs[8] = dma8;
}

static void * s5l8930_cdma_init(target_phys_addr_t base, qemu_irq dma5, qemu_irq dma6, qemu_irq dma7, qemu_irq dma8)
{
    s5l8930_cdma_s *cdma = (s5l8930_cdma_s *) qemu_mallocz(sizeof(s5l8930_cdma_s));

    int iomemtype = cpu_register_io_memory(s5l8930_cdma_readfn,
                                           s5l8930_cdma_writefn, cdma, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xffff, iomemtype);

	cdma->irqs[5] = dma5;
	cdma->irqs[6] = dma6;
	cdma->irqs[7] = dma7;
	cdma->irqs[8] = dma8;

	s5l8930_cdma_aes_init(S5L8930_CDMA_AES_BASE, cdma);
	
	return (void *)cdma;
}

static uint32_t s5l8930_cdma_aes_read(void *opaque, target_phys_addr_t addr)
{
    s5l8930_cdma_s *cdma = (s5l8930_cdma_s *) opaque;
    uint32_t channel_reg = addr >> 12;

    S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: addr 0x%08x\n", __FUNCTION__, addr);

    switch (addr & 0xff) {
		case 0x0: // Setup
			return cdma->dmaAesSetup[channel_reg];
        case 0x10 ... 0x1c: /* AES IV */
            {
                uint8_t idx = ((addr & 0xff) - 0x10) / 4;
                return cdma->ivec[idx];
            }
        case 0x20 ... 0x3c: /* AES Key */
            {
                uint8_t idx = ((addr & 0xff) - 0x20) / 4;
                return cdma->custkey[idx];
            }
      default:
		return 0;
	}
}

static void s5l8930_cdma_aes_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    s5l8930_cdma_s *cdma = (s5l8930_cdma_s *) opaque;
    uint32_t channel_reg = addr >> 12;

    S5L8930_DEBUG(S5L8930_DEBUG_CDMA, S5L8930_DLVL_ERR, "%s: addr 0x%08x value 0x%08x\n", __FUNCTION__, addr, value);
	
    switch (addr & 0xff) {
        case 0x0: // Setup
        	cdma->dmaAesSetup[channel_reg] = value;

			if((cdma->dmaAesSetup[channel_reg] >> 20) & 0x1) {

				cdma->keyType = AESCustom;

				switch((cdma->dmaAesSetup[channel_reg] >> 18) & 0x2)
				{
					case AES_256:
						cdma->keyLen = 256;
						break;
					case AES_192:
						cdma->keyLen = 192;
						break;
					case AES_128:
						cdma->keyLen = 128;
						break;
				  default:
					cdma->keyLen = 128;
					break;
				} 
			} else if((cdma->dmaAesSetup[channel_reg] >> 21) & 0x1) {
				cdma->keyType = AESGID;
				cdma->keyLen = 256;
			} else {
				cdma->keyType = AESUID;
				cdma->keyLen = 256;
			}
            break;
        case 0x10 ... 0x1c: /* AES IV */
            {
                uint8_t idx = ((addr & 0xff) - 0x10) / 4;
                cdma->ivec[idx] = value;
                break;
            }
        case 0x20 ... 0x3c: /* AES Key */
            {
                 uint8_t idx = ((addr & 0xff) - 0x20) / 4;
                 cdma->custkey[idx] = value;
                 break;
            }
    }

}

static CPUReadMemoryFunc *s5l8930_cdma_aes_readfn[] = {
    s5l8930_cdma_aes_read,
    s5l8930_cdma_aes_read,
    s5l8930_cdma_aes_read,
};

static CPUWriteMemoryFunc *s5l8930_cdma_aes_writefn[] = {
    s5l8930_cdma_aes_write,
    s5l8930_cdma_aes_write,
    s5l8930_cdma_aes_write,
};

static void s5l8930_cdma_aes_init(target_phys_addr_t base, void *opaque)
{
    int iomemtype = cpu_register_io_memory(s5l8930_cdma_aes_readfn,
                                           s5l8930_cdma_aes_writefn, opaque, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xffff, iomemtype);
}

static uint32_t s5l8930_chipid_read(void *opaque, target_phys_addr_t addr)
{

	S5L8930_DEBUG(S5L8930_DEBUG_CHIPID, S5L8930_DLVL_WARN, "%s: offset = 0x%02x\n", __FUNCTION__, (int)addr);

	switch(addr) {
			case 0x0:	
				return 0x31800587; 
        	case 0x8:
				return 0xf6a15b30;
			case 0xc:
				return 0x47002735;

		default:
			 S5L8930_DEBUG(S5L8930_DEBUG_CHIPID, S5L8930_DLVL_ERR, "%s: UNMAPPED offset = 0x%02x\n", __FUNCTION__, (int)addr);
	}

	return 0;
}

static CPUReadMemoryFunc *s5l8930_chipid_readfn[] = {
    s5l8930_chipid_read,
    s5l8930_chipid_read,
    s5l8930_chipid_read,
};

static CPUWriteMemoryFunc *s5l8930_chipid_writefn[] = {
    NULL,
    NULL,
    NULL,
};

static void s5l8930_chipid_init(target_phys_addr_t base)
{

    int iomemtype = cpu_register_io_memory(s5l8930_chipid_readfn,
                                           s5l8930_chipid_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xF, iomemtype);

}

typedef struct sha1_status {
    uint32_t status;
    uint32_t reset;
    uint32_t hresult;
    uint32_t inWordCnt;
    uint32_t unkstat;
	uint32_t *hashIn;
    uint8_t hashout[0x14];
} sha1_status_s;

static void sha1_reset(void *opaque)
{
    sha1_status_s *s = (sha1_status_s *)opaque;

	/* Wow huge buffer lets ditch for now */
	if(s->hashIn)
		qemu_free(s->hashIn);

    memset(s, 0, sizeof(sha1_status_s));

    S5L8930_DEBUG(S5L8930_DEBUG_SHA1, S5L8930_DLVL_ERR, "Resetting SHA1\n");
}

static uint32_t sha1_read(void *opaque, target_phys_addr_t offset)
{
    sha1_status_s *s = (sha1_status_s *)opaque;
    uint32_t retVal;

    S5L8930_DEBUG(S5L8930_DEBUG_SHA1, S5L8930_DLVL_ERR, "%s: offset 0x%08x\n", __FUNCTION__, offset);
    switch(offset) {
        // Hash result ouput
        case 0x20 ... 0x30:
			if(offset == 0x20) 
			{ /* It's hashing time baby */
				/*
                SHA_CTX context;
                SHA1_Init(&context);
                SHA1_Update(&context, (uint8_t*)s->hashIn, (s->inWordCnt * 4) - 0x40);
                SHA1_Final(s->hashout, &context);
				*/

#if 0
				{
                	uint32_t ctr;
					for( ctr = 0; ctr < 20; ctr++ )
					{
							fprintf(stderr, "%x ", s->hashout[ctr] );
					}
					fprintf(stderr, "\n");
					FILE *fp = fopen("/tmp/sha1.img", "w");
					fwrite(s->hashIn, 1, (s->inWordCnt * 4) - 0x40, fp);
					fclose(fp);
				}
#endif
				}
				retVal = *(uint32_t *)&s->hashout[offset - 0x20];
            S5L8930_DEBUG(S5L8930_DEBUG_SHA1, S5L8930_DLVL_ERR, "Hash out %08x\n",  *(uint32_t *)&s->hashout[offset - 0x20]);
			if(offset == 0x30) 
				sha1_reset(s);
            return retVal;
    }
    return 0;
}

static void sha1_write(void *opaque, target_phys_addr_t offset,
                       uint32_t value)
{
    sha1_status_s *s = (sha1_status_s *)opaque;

    S5L8930_DEBUG(S5L8930_DEBUG_SHA1, S5L8930_DLVL_ERR, "%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);

    switch(offset) {
		case 0x40 ... 0x7c: /* In buffer regs */
			if(!s->inWordCnt) 
				s->hashIn = (uint32_t *)qemu_mallocz(0x40000);
			if(s->inWordCnt >= 0x10000) 
			{
				S5L8930_DEBUG(S5L8930_DEBUG_SHA1, S5L8930_DLVL_ERR, "%s: error out of sha1 buffer\n", __FUNCTION__);
				break;
			}	
			S5L8930_DEBUG(S5L8930_DEBUG_SHA1, S5L8930_DLVL_ERR, "%s: adding value to hash 0x%08x inWordCnt %x\n", __FUNCTION__, value, s->inWordCnt);
			s->hashIn[s->inWordCnt++] = value;
			break;
    }

}

static CPUReadMemoryFunc * const s5l8930_sha1_readfn[] = {
    sha1_read,
    sha1_read,
    sha1_read,
};

static CPUWriteMemoryFunc * const s5l8930_sha1_writefn[] = {
    sha1_write,
    sha1_write,
    sha1_write,
};

static void s5l8930_sha1_init(target_phys_addr_t base)
{
    sha1_status_s *s = (sha1_status_s *) qemu_mallocz(sizeof(sha1_status_s));
    int iomemtype = cpu_register_io_memory(s5l8930_sha1_readfn,
                                           s5l8930_sha1_writefn,
                                           s, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0xFF, iomemtype);
}

static void s5l8930_gpio_write(void *opaque, target_phys_addr_t addr, uint32_t value) 
{
	 S5L8930_DEBUG(S5L8930_DEBUG_GPIO, S5L8930_DLVL_ERR, "%s: offset 0x%08x value 0x%08x\n", __func__, addr, value);
}

static uint32_t s5l8930_gpio_read(void *opaque, target_phys_addr_t addr)
{
     S5L8930_DEBUG(S5L8930_DEBUG_GPIO, S5L8930_DLVL_ERR, "%s: offset 0x%08x\n", __func__, addr);

	switch(addr) {
		case 0xa0:
			return 1; /* board id? */
		case 0x9c:
			return 1; /* board id? */
	}

    return 0;
}
static CPUReadMemoryFunc *s5l8930_gpio_readfn[] = {
    s5l8930_gpio_read,
    s5l8930_gpio_read,
    s5l8930_gpio_read,
};

static CPUWriteMemoryFunc *s5l8930_gpio_writefn[] = {
    s5l8930_gpio_write,
    s5l8930_gpio_write,
    s5l8930_gpio_write,
};

static void s5l8930_gpio_init(target_phys_addr_t base)
{

    int iomemtype = cpu_register_io_memory(s5l8930_gpio_readfn,
                                           s5l8930_gpio_writefn, NULL, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0x3FF, iomemtype);
}

static inline qemu_irq s5l8930_get_irq(struct s5l8930_state_s *s, int n)
{
    return s->irq[n / S5L8930_VIC_SIZE][n % S5L8930_VIC_SIZE];
}

static uint32_t s5l8930_usb_phy_read(void *opaque, target_phys_addr_t offset)
{
	s5l8930_state *s = opaque;

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
		S5L8930_DEBUG(S5L8930_DEBUG_USBPHY, S5L8930_DLVL_ERR, "%s: read invalid location 0x%08x.\n", __func__, offset);
		return 0;
	}

	return 0;
}

static void s5l8930_usb_phy_write(void *opaque, target_phys_addr_t offset, uint32_t val)
{
	s5l8930_state *s = opaque;

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
		S5L8930_DEBUG(S5L8930_DEBUG_USBPHY, S5L8930_DLVL_ERR, "%s: write invalid location 0x%08x.\n", __func__, offset);
	}
}

static CPUReadMemoryFunc * const s5l8930_usb_phy_readfn[] = {
    s5l8930_usb_phy_read,
    s5l8930_usb_phy_read,
    s5l8930_usb_phy_read,
};

static CPUWriteMemoryFunc * const s5l8930_usb_phy_writefn[] = {
    s5l8930_usb_phy_write,
    s5l8930_usb_phy_write,
    s5l8930_usb_phy_write,
};

static void s5l8930_usb_phy_init(s5l8930_state *_state)
{
	_state->usb_ophypwr = 0;
	_state->usb_ophyclk = 0;
	_state->usb_orstcon = 0;
	_state->usb_ophytune = 0;

	int iomemtype = cpu_register_io_memory(s5l8930_usb_phy_readfn,
                                           s5l8930_usb_phy_writefn,
										   _state, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(S5L8930_USB_PHY_BASE, 0x40, iomemtype);
}

static void unmapped_write(void *opaque, target_phys_addr_t offset,
                       uint32_t value)
{
    char *name = (char *)opaque;

    fprintf(stderr, "%s:%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, name, offset, value);
}

static void *s5l8930;
void triggerSDIO(void) {

    s5l8930_state *s = s5l8930;
    qemu_irq_raise(s5l8930_get_irq(s, 0x26));
}

static uint32_t unmapped_read(void *opaque, target_phys_addr_t offset)
{
    char *name = (char *)opaque;

    fprintf(stderr, "%s:%s: offset 0x%08x\n", __FUNCTION__, name, offset);
	if(offset == 0x110) {
		triggerSDIO();
	}
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

static char ceata[] = "ceata";

static void s5l8930_unmapped_hw_init(target_phys_addr_t base, int size, const char *name)
{
    int io;
    io = cpu_register_io_memory(unmapped_readfn, unmapped_writefn, (void *)name, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, size, io);
}

s5l8930_state *s5l8930_init(void)
{

	s5l8930_state *s = (s5l8930_state *)qemu_mallocz(sizeof(s5l8930_state));
	s5l8930iop_state_s *iop= (s5l8930iop_state_s *)qemu_mallocz(sizeof(s5l8930iop_state_s));

    i2c_bus *i2c;

    qemu_irq *cpu_irq;
	void *cdma;
    DeviceState *dev, *dev_prev;
	int i,j;

    s->env = cpu_init("cortex-a8");
	if(!s->env) {
    	fprintf(stderr, "Unable to find CPU definition\n");
    	exit(1);
  	}

    cpu_irq = arm_pic_init_cpu(s->env);

    // Allocate 4 vic controllers
    s->irq = qemu_mallocz(S5L8930_VIC_N * sizeof(qemu_irq *));
    dev = pl192_init(S5L8930_VIC_BASE, 0,
                     cpu_irq[ARM_PIC_CPU_IRQ],
                     cpu_irq[ARM_PIC_CPU_FIQ], NULL);
	// Each with 32 irq's
    s->irq[0] = qemu_mallocz(S5L8930_VIC_SIZE * sizeof(qemu_irq));
    for (i = 0; i < S5L8930_VIC_SIZE; i++)
        s->irq[0][i] = qdev_get_gpio_in(dev, i);
    for (j = 1; j < S5L8930_VIC_N; j++) {
        dev_prev = dev;
        dev = pl192_init(S5L8930_VIC_BASE + S5L8930_VIC_SHIFT * j, j, NULL);

        s->irq[j] = qemu_mallocz(S5L8930_VIC_SIZE * sizeof(qemu_irq));
        for (i = 0; i < S5L8930_VIC_SIZE; i++)
            s->irq[j][i] = qdev_get_gpio_in(dev, i);
        pl192_chain(sysbus_from_qdev(dev_prev), sysbus_from_qdev(dev));
    }

	/* Chip info */
	s5l8930_chipid_init(S5L8930_CHIPID);

    /* CDMA AES */
    s->cdma = s5l8930_cdma_init(S5L8930_CDMA_BASE, s5l8930_get_irq(s, S5L8930_CDMA_CHANNEL5_IRQ), s5l8930_get_irq(s, S5L8930_CDMA_CHANNEL6_IRQ), s5l8930_get_irq(s, S5L8930_CDMA_CHANNEL7_IRQ), s5l8930_get_irq(s, S5L8930_CDMA_CHANNEL8_IRQ));

    /* SHA1 */
	s5l8930_sha1_init(S5L8930_SHA1_BASE);

    /* Sytem Timer */
	s->timer = s5l8930_timer_init(S5L8930_TIMER1_BASE, s5l8930_get_irq(s, S5L8930_TIMER1_IRQ));

	/* PMGR */
	s5l8930_pmgr_init(S5L8930_PMGR_BASE);

	/* GPIO */
	s5l8930_gpio_init(S5L8930_GPIO_BASE);

	/* Uart */
    s5l8900_uart_init(S5L8930_UART0_BASE, 0, 256, s5l8930_get_irq(s, S5L8930_UART0_IRQ), serial_hds[0]);

    /* I2C 0 */
    dev = sysbus_create_simple("s5l8930.i2c", S5L8930_I2C0_BASE,
                               s5l8930_get_irq(s, S5L8930_IRQ_I2C_0));
    i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

    /* PWU  */
    pcf50633_init(i2c, PCF50633_ADDR);

    /* Charger */
	ipadchg_init(i2c, S5L8930_CHG_ADDR);

    /* I2C 1 */
    dev = sysbus_create_simple("s5l8930.i2c", S5L8930_I2C1_BASE,
                               s5l8930_get_irq(s, S5L8930_IRQ_I2C_1));
    i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

    /* Charger */
    ipadchg_init(i2c, S5L8930_CHG_ADDR);

    /* I2C 2 */
    dev = sysbus_create_simple("s5l8930.i2c", S5L8930_I2C2_BASE,
                               s5l8930_get_irq(s, S5L8930_IRQ_I2C_2));
    i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");

    /* Charger */
    ipadchg_init(i2c, S5L8930_CHG_ADDR);

	/* Misc sys value */
	s5l8930_misc_sys_init(S5L8900_MISCSYS_BASEADDR);

    /* SPI */
    sysbus_create_simple("s5l8930.spi",
                         S5L8930_SPI0_BASE,
                         s5l8930_get_irq(s, S5L8930_SPI0_IRQ));
    s5l8930_set_spi_base(1);
    sysbus_create_simple("s5l8930.spi",
                         S5L8930_SPI1_BASE,
                         s5l8930_get_irq(s, S5L8930_SPI1_IRQ));
    s5l8930_set_spi_base(2);
    sysbus_create_simple("s5l8930.spi",
                         S5L8930_SPI2_BASE,
                         s5l8930_get_irq(s, S5L8930_SPI2_IRQ));
    s5l8930_set_spi_base(3);
    sysbus_create_simple("s5l8930.spi",
                         S5L8930_SPI3_BASE,
                         s5l8930_get_irq(s, S5L8930_SPI3_IRQ));
    s5l8930_set_spi_base(4);
    sysbus_create_simple("s5l8930.spi",
                         S5L8930_SPI4_BASE,
                         s5l8930_get_irq(s, S5L8930_SPI4_IRQ));
    /* IOP */
    s5l8930_iop_init(s);

	/* H2FMI */

	s5l8930_h2fmi0_register(S5L8930_H2FMI_BASE0, getH2FMI_IRQ_0());//s5l8930_get_irq(s, S5L8930_H2FMI_IRQ0));
	s5l8930_h2fmi1_register(S5L8930_H2FMI_BASE1, getH2FMI_IRQ_1());//s5l8930_get_irq(s, S5L8930_H2FMI_IRQ1));

    /* USB-OTG */
    register_synopsys_usb(S5L8930_USB_OTG_BASE,
            s5l8930_get_irq(s, S5L8930_IRQ_OTG));
    s5l8930_usb_phy_init(s);

	/* CE ATA */
	s5l8930_unmapped_hw_init(0x81000000, 0x1000, ceata);
	
	s5l8930 = s;

	return s;
}

/* 
 IOP emulation for a4 by cmw
 */

#include "sysbus.h"
#include "hw.h"
#include "arm-misc.h"
#include "sysemu.h"
#include "qemu-common.h"
#include "primecell.h"
#include "cpu.h"
#include "exec-all.h"
#include "s5l8930.h"

#define ARM_MODE_IOP 6
#define ARM_MODE_NORM 7

#define S5L8930_ARM7_VIC_N     4
#define S5L8930_ARM7_VIC_SIZE  32
#define S5L8930_ARM7_VIC_BASE  0xBF300000
#define S5L8930_ARM7_VIC_SHIFT 0x00010000

extern void switch_iop_mode(CPUState *env, uint32_t new_mode);
extern CPUState *get_current_cpu(void);

typedef struct s5l8930_iop_s
{
	CPUState *s5l8930env;
	CPUState *iopenv;
	qemu_irq **irq;

	void *cdma;
	void *timer;
    uint32_t status;
	uint32_t startaddr;
    char *name;
	qemu_irq iopirq;
} s5l8930_iop_s;

static inline qemu_irq s5l8930_get_irq(struct s5l8930_state_s *s, int n)
{
    return s->irq[n / S5L8930_VIC_SIZE][n % S5L8930_VIC_SIZE];
}

static inline qemu_irq s5l8930_iop_get_irq(struct s5l8930_iop_s *s, int n)
{
    return s->irq[n / S5L8930_VIC_SIZE][n % S5L8930_VIC_SIZE];
}

static void do_iop_run(void *opaque)
{
    s5l8930_iop_s *s = (s5l8930_iop_s *)opaque;
    uint64_t physical, page_size, end;
	uint32_t address;
	target_ulong size;
    int prot, zbits, ret;
	/* hack to fix some power gate init issue @ pmgr_enable_gates */
	uint8_t buf[] = {0x00,0x20}; /* nop */
	uint32_t addr = 0x40762790;
	int i;
	for(i=0;i < 0x10;i+=2) 
		cpu_physical_memory_write((target_phys_addr_t)(addr + i), (uint8_t *)buf, 0x2);

	replaceCDMAIRQHandlers(s->cdma, s5l8930_iop_get_irq(s, S5L8930_CDMA_CHANNEL5_IRQ), s5l8930_iop_get_irq(s, S5L8930_CDMA_CHANNEL6_IRQ), s5l8930_iop_get_irq(s, S5L8930_CDMA_CHANNEL7_IRQ), s5l8930_iop_get_irq(s, S5L8930_CDMA_CHANNEL8_IRQ));

	fprintf(stderr, "start running on iop proc\n");
	s->iopenv->regs[15] = s->startaddr;
	cpu_interrupt(s->iopenv, CPU_INTERRUPT_EXITTB);
	cpu_interrupt(s->s5l8930env, CPU_INTERRUPT_EXITTB);
}

static void s5l8930_iop_write(void *opaque, target_phys_addr_t offset,
				uint32_t value)
{
    s5l8930_iop_s *s = (s5l8930_iop_s *)opaque;

    //fprintf(stderr, "%s:%s: offset 0x%08x value 0x%08x isARM7_CPU 0x%p\n", __FUNCTION__, s->name, offset, value, get_current_cpu());
	//cpu_synchronize_all_states();

    switch(offset) {
			case 0x18:
			case 0x1c:
			case 0x24:
				return;

            case 0x100:
                if(value & 0x10) { /* Stop CPU ? */
                    s->status |= 0x2;
                    break;
                }
				if(value & 0x1) {
					fprintf(stderr, "switching cpu state to start IOP processing @ address 0x%08x\n", s->startaddr);
					cpu_interrupt(s->s5l8930env, CPU_INTERRUPT_HALT);
					//pause_all_vcpus();
					//switch_iop_mode(s->env, ARM_MODE_IOP);
					run_on_cpu(s->iopenv, do_iop_run, s);
				    //s->iopenv->regs[15] = s->startaddr;
					//resume_all_vcpus();
				}
                s->status = value;
                break;
			case 0x110:
				s->startaddr = value;
				break;
        default:
	    	fprintf(stderr, "%s:%s: UNKNOWN offset 0x%08x value 0x%08x isARM7_CPU 0x%p\n", __FUNCTION__, s->name, offset, value, get_current_cpu());
            break;
    }
}

static uint32_t s5l8930_iop_read(void *opaque, target_phys_addr_t offset)
{

    s5l8930_iop_s *s = (s5l8930_iop_s *)opaque;

    fprintf(stderr, "%s:%s: offset 0x%08x isARM7_CPU 0x%p\n", __FUNCTION__, s->name, offset, get_current_cpu());

    switch(offset) {
        case 0x18:
        case 0x1c:
        case 0x24:
            return 0;
        case 0x100:
            return s->status;
		case 0x110:
			return s->startaddr;
		default:
			fprintf(stderr, "%s:%s: UNKNOWN offset 0x%08x isARM7_CPU 0x%p\n", __FUNCTION__, s->name, offset, get_current_cpu());
        break;
    }

    return 0;
}

static CPUReadMemoryFunc *s5l8930_iop_readfn[] = {
    s5l8930_iop_read,
    s5l8930_iop_read,
    s5l8930_iop_read,
};

static CPUWriteMemoryFunc *s5l8930_iop_writefn[] = {
    s5l8930_iop_write,
    s5l8930_iop_write,
    s5l8930_iop_write,
};

static char name0[] = "s5l8930_iop0";
static char name1[] = "s5l8930_iop1";

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

static char iop_txbuf[] = "iop_txbuf";

static void iop_unmapped_hw_init(target_phys_addr_t base, int size, const char *name)
{
    int io;
    io = cpu_register_io_memory(unmapped_readfn, unmapped_writefn, (void *)name, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, size, io);
}

static CPUState *IOPCpuState;
static CPUState *MainCpuState;
static void *IOPState;

CPUState *getMainCpuEnv(void) {
	return MainCpuState;
}
CPUState *getIOPCpuEnv(void) {
	return IOPCpuState;
}

void *getIOPState(void) {
	return IOPState;
}

qemu_irq getH2FMI_IRQ_0(void) {
    s5l8930_iop_s *s = (s5l8930_iop_s *) getIOPState();
	return s5l8930_iop_get_irq(s, S5L8930_H2FMI_IRQ0);
}

qemu_irq getH2FMI_IRQ_1(void) {
    s5l8930_iop_s *s = (s5l8930_iop_s *) getIOPState();
    return s5l8930_iop_get_irq(s, S5L8930_H2FMI_IRQ1);
}

void iopTriggerIRQ(void) 
{
	s5l8930_iop_s *s = (s5l8930_iop_s *) getIOPState();
    //qemu_irq_raise(s5l8930_iop_get_irq(s, S5L8930_IOP_IRQ));
}
void s5l8930_iop_init(void *opaque)
{
    int io;
    s5l8930_iop_s *s = (s5l8930_iop_s *) qemu_mallocz(sizeof(s5l8930_iop_s));
    s5l8930_state *s5l8930 = opaque;
	CPUState *env;
    qemu_irq *cpu_irq;
    DeviceState *dev, *dev_prev;
    int i,j;

    env = cpu_init("cortex-a8");
    if(!env) {
        fprintf(stderr, "Unable to find CPU for IOP proccess definition\n");
        exit(1);
    }

  	cpu_irq = arm_pic_init_cpu(env);

    // Allocate 4 vic controllers
    s->irq = qemu_mallocz(S5L8930_ARM7_VIC_N * sizeof(qemu_irq *));
    dev = pl192_init(S5L8930_ARM7_VIC_BASE, 4,
                     cpu_irq[ARM_PIC_CPU_IRQ],
                     cpu_irq[ARM_PIC_CPU_FIQ], NULL);
    // Each with 32 irq's
    s->irq[0] = qemu_mallocz(S5L8930_ARM7_VIC_SIZE * sizeof(qemu_irq));
    for (i = 0; i < S5L8930_ARM7_VIC_SIZE; i++)
        s->irq[0][i] = qdev_get_gpio_in(dev, i);
    for (j = 1; j < S5L8930_ARM7_VIC_N; j++) {
        dev_prev = dev;
        dev = pl192_init(S5L8930_ARM7_VIC_BASE + S5L8930_ARM7_VIC_SHIFT * j, 4+j, NULL);

        s->irq[j] = qemu_mallocz(S5L8930_ARM7_VIC_SIZE * sizeof(qemu_irq));
        for (i = 0; i < S5L8930_ARM7_VIC_SIZE; i++)
            s->irq[j][i] = qdev_get_gpio_in(dev, i);
        pl192_chain(sysbus_from_qdev(dev_prev), sysbus_from_qdev(dev));
    }


    uint32_t base = 0x86300000;
    s->name = name0;
	s->s5l8930env = s5l8930->env;
	s->iopenv = env;
	s->cdma = s5l8930->cdma;
	s->timer = s5l8930->timer;
	s->iopirq = s5l8930_get_irq(s5l8930, S5L8930_IOP_IRQ);
    io = cpu_register_io_memory(s5l8930_iop_readfn, s5l8930_iop_writefn, s, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(base, 0x1000, io);

	setTimerIRQ2(s->timer, s5l8930_iop_get_irq(s, S5L8930_TIMER0_IRQ));
	IOPCpuState = s->iopenv;
	MainCpuState = s5l8930->env;
	IOPState = s;
	cpu_interrupt(env, CPU_INTERRUPT_HALT);
}

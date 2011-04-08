/*
 * ARM PrimeCell PL192 Vector Interrupt Controller
 *
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 */

#include "sysbus.h"
#include "primecell.h"


#define PL192_INT_SOURCES   32
#define PL192_DAISY_IRQ     PL192_INT_SOURCES
#define PL192_NO_IRQ        PL192_INT_SOURCES+1
#define PL192_PRIO_LEVELS   16

#define PL192_IRQSTATUS         0x00
#define PL192_FIQSTATUS         0x04
#define PL192_RAWINTR           0x08
#define PL192_INTSELECT         0x0C
#define PL192_INTENABLE         0x10
#define PL192_INTENCLEAR        0x14
#define PL192_SOFTINT           0x18
#define PL192_SOFTINTCLEAR      0x1C
#define PL192_PROTECTION        0x20
#define PL192_SWPRIORITYMASK    0x24
#define PL192_PRIORITYDAISY     0x28
#define PL192_VECTADDR          0xF00
#define PL192_IOMEM_SIZE        0x1000

#define PL190_ITCR              0x300
#define PL190_VECTADDR          0x30
#define PL190_DEFVECTADDR       0x34

#define PL192_IOMEM_SIZE    0x1000


typedef struct pl192_state_s {
    SysBusDevice busdev;
    uint32_t instance;

    /* Control registers */
    uint32_t irq_status;
    uint32_t fiq_status;
    uint32_t rawintr;
    uint32_t intselect;
    uint32_t intenable;
    uint32_t softint;
    uint32_t protection;
    uint32_t sw_priority_mask;
    uint32_t vect_addr[PL192_INT_SOURCES];
    uint32_t vect_priority[PL192_INT_SOURCES];
    uint32_t address;

    /* Currently processed interrupt and
       highest priority interrupt */
    uint32_t current;
    uint32_t current_highest;

    /* Priority masking logic */
    int32_t stack_i;
    uint32_t priority_stack[PL192_PRIO_LEVELS+1];
    uint8_t irq_stack[PL192_PRIO_LEVELS+1];
    uint32_t priority;

    /* Daisy-chain interface */
    uint32_t daisy_vectaddr;
    uint32_t daisy_priority;
    struct pl192_state_s *daisy_callback;
    uint8_t  daisy_input;

    /* Parent interrupts */
    qemu_irq irq;
    qemu_irq fiq;

    /* Next controller in chain */
    struct pl192_state_s *daisy;
} pl192_state;

const unsigned char pl192_id[] =
{ 0x92, 0x11, 0x04, 0x00, 0x0D, 0xF0, 0x05, 0xB1 };


static void pl192_update(pl192_state *);

static void pl192_raise(pl192_state *s, int is_fiq)
{
    if (is_fiq) {
        if (s->fiq) {
            /* Raise parent FIQ */
            qemu_irq_raise(s->fiq);
        } else {
            if (s->daisy) {
                /* FIQ is directly propagated through daisy chain */
                pl192_raise(s->daisy, is_fiq);
            } else {
                hw_error("pl192: cannot raise FIQ. This usually means that "
                         "initialization was done incorrectly.\n");
            }
        }
    } else {
        if (s->irq) {
            /* Raise parent IRQ */
            qemu_irq_raise(s->irq);
        } else {
            if (s->daisy) {
                /* Setup daisy input of the next chained contorller and force
                   it to update it's state */
                s->daisy->daisy_vectaddr = s->address;
                s->daisy->daisy_callback = s;
                s->daisy->daisy_input = 1;
                pl192_update(s->daisy);
            } else {
                hw_error("pl192: cannot raise IRQ. This usually means that "
                         "initialization was done incorrectly.\n");
            }
        }
    }
}

static void pl192_lower(pl192_state *s, int is_fiq)
{
    /* Lower parrent interrupt if there is one */
    if (is_fiq && s->fiq) {
        qemu_irq_lower(s->fiq);
    }
    if (!is_fiq && s->irq) {
        qemu_irq_lower(s->irq);
    }
    /* Propagate to the previous controller in chain if needed */
    if (s->daisy) {
        if (!is_fiq) {
            s->daisy->daisy_input = 0;
            pl192_update(s->daisy);
        } else {
            pl192_lower(s->daisy, is_fiq);
        }
    }
}

/* Find interrupt of the highest priority */
static uint32_t pl192_priority_sorter(pl192_state *s)
{
    int i;
    uint32_t prio_irq[PL192_PRIO_LEVELS];

    for (i = 0; i < PL192_PRIO_LEVELS; i++) {
        prio_irq[i] = PL192_NO_IRQ;
    }
    if (s->daisy_input) {
        prio_irq[s->daisy_priority] = PL192_DAISY_IRQ;
    }
    for (i = PL192_INT_SOURCES - 1; i >= 0; i--) {
        if (s->irq_status & (1 << i)) {
            prio_irq[s->vect_priority[i]] = i;
        }
    }
    for (i = 0; i < PL192_PRIO_LEVELS; i++) {
        if ((s->sw_priority_mask & (1 << i)) &&
            prio_irq[i] <= PL192_DAISY_IRQ) {
            return prio_irq[i];
        }
    }
    return PL192_NO_IRQ;
}

static void pl192_update(pl192_state *s)
{
    /* TODO: does SOFTINT affects IRQ_STATUS??? */
    s->irq_status = (s->rawintr | s->softint) & s->intenable & ~s->intselect;
    s->fiq_status = (s->rawintr | s->softint) & s->intenable & s->intselect;
    if (s->fiq_status) {
        pl192_raise(s, 1);
    } else {
        pl192_lower(s, 1);
    }
    if (s->irq_status || s->daisy_input) {
        s->current_highest = pl192_priority_sorter(s);
        if (s->current_highest < PL192_INT_SOURCES) {
            s->address = s->vect_addr[s->current_highest];
        } else {
            s->address = s->daisy_vectaddr;
        }
        if (s->current_highest != s->current) {
            if (s->current_highest < PL192_INT_SOURCES) {
                if (s->vect_priority[s->current_highest] >= s->priority) {
                    return ;
                }
            }
            if (s->current_highest == PL192_DAISY_IRQ) {
                if (s->daisy_priority >= s->priority) {
                    return ;
                }
            }
            if (s->current_highest <= PL192_DAISY_IRQ) {
                pl192_raise(s, 0);
            } else {
                pl192_lower(s, 0);
            }
        }
    } else {
        s->current_highest = PL192_NO_IRQ;
        pl192_lower(s, 0);
    }
}

/* Set priority level when an interrupt have been acknoledged by CPU.
   Also save interrupt id and priority to stack so it can be restored
   lately. */
static inline void pl192_mask_priority(pl192_state *s)
{
    if (s->stack_i >= PL192_INT_SOURCES) {
        hw_error("pl192: internal error\n");
    }
    s->stack_i++;
    if (s->current == PL192_DAISY_IRQ) {
        s->priority = s->daisy_priority;
    } else {
        s->priority = s->vect_priority[s->current];
    }
    s->priority_stack[s->stack_i] = s->priority;
    s->irq_stack[s->stack_i] = s->current;
}

/* Set priority level when interrupt have been successfully processed by CPU.
   Also restore previous interrupt id and priority level. */
static inline void pl192_unmask_priority(pl192_state *s)
{
    if (s->stack_i < 1) {
        hw_error("pl192: internal error\n");
    }
    s->stack_i--;
    s->priority = s->priority_stack[s->stack_i];
    s->current = s->irq_stack[s->stack_i];
}

/* IRQ was acknoledged by CPU. Update controller state accordingly */
static uint32_t pl192_irq_ack(pl192_state *s)
{
    int is_daisy = (s->current_highest == PL192_DAISY_IRQ);
    uint32_t res = s->address;

    s->current = s->current_highest;
    pl192_mask_priority(s);
    if (is_daisy) {
        pl192_mask_priority(s->daisy_callback);
    }
    pl192_update(s);
    return res;
}

/* IRQ was processed by CPU. Update controller state accrodingly */
static void pl192_irq_fin(pl192_state *s)
{
    int is_daisy = (s->current == PL192_DAISY_IRQ);

    pl192_unmask_priority(s);
    if (is_daisy) {
        pl192_unmask_priority(s->daisy_callback);
    }
    pl192_update(s);
    if (s->current == PL192_NO_IRQ) {
        pl192_lower(s, 0);
    }
}

static uint32_t pl192_read(void *opaque, target_phys_addr_t offset)
{
    pl192_state *s = (pl192_state *) opaque;

    if (offset & 3) {
        hw_error("pl192: bad read offset " TARGET_FMT_plx "\n", offset);
        return 0;
    }

    if (offset >= 0xfe0 && offset < 0x1000) {
        return pl192_id[(offset - 0xfe0) >> 2];
    }
    if (offset >= 0x100 && offset < 0x180) {
        return s->vect_addr[(offset - 0x100) >> 2];
    }
    if (offset >= 0x200 && offset < 0x280) {
        return s->vect_priority[(offset - 0x200) >> 2];
    }

    switch (offset) {
        case PL192_IRQSTATUS:
            return s->irq_status;
        case PL192_FIQSTATUS:
            return s->fiq_status;
        case PL192_RAWINTR:
            return s->rawintr;
        case PL192_INTSELECT:
            return s->intselect;
        case PL192_INTENABLE:
            return s->intenable;
        case PL192_SOFTINT:
            return s->softint;
        case PL192_PROTECTION:
            return s->protection;
        case PL192_SWPRIORITYMASK:
            return s->sw_priority_mask;
        case PL192_PRIORITYDAISY:
            return s->daisy_priority;
        case PL192_INTENCLEAR:
			return 0;
        case PL192_SOFTINTCLEAR:
            hw_error("pl192: attempt to read write-only register (offset = "
                     TARGET_FMT_plx ")\n", offset);
        case PL192_VECTADDR:
            return pl192_irq_ack(s);
        /* Workaround for kernel code using PL190 */
        case PL190_ITCR:
        case PL190_VECTADDR:
        case PL190_DEFVECTADDR:
            return 0;
        default:
            hw_error("pl192: bad read offset " TARGET_FMT_plx "\n", offset);
            return 0;
    }
}

static void pl192_write(void *opaque, target_phys_addr_t offset,
                        uint32_t value)
{
    pl192_state *s = (pl192_state *) opaque;

    if (offset & 3) {
        hw_error("pl192: bad write offset " TARGET_FMT_plx "\n", offset);
    }

    if (offset >= 0xfe0 && offset < 0x1000) {
        hw_error("pl192: attempt to write to a read-only register (offset = "
                 TARGET_FMT_plx ")\n", offset);
    }
    if (offset >= 0x100 && offset < 0x180) {
        s->vect_addr[(offset - 0x100) >> 2] = value;
        pl192_update(s);
        return;
    }
    if (offset >= 0x200 && offset < 0x280) {
        s->vect_priority[(offset - 0x200) >> 2] = value & 0xf;
        pl192_update(s);
        return;
    }

    switch (offset) {
        case PL192_IRQSTATUS:
            /* This is a readonly register, but linux tries to write to it
               anyway.  Ignore the write.  */
            return;
        case PL192_FIQSTATUS:
        case PL192_RAWINTR:
            hw_error("pl192: attempt to write to a read-only register (offset = "
                     TARGET_FMT_plx ")\n", offset);
            break;
        case PL192_INTSELECT:
            s->intselect = value;
            break;
        case PL192_INTENABLE:
            s->intenable |= value;
            break;
        case PL192_INTENCLEAR:
            s->intenable &= ~value;
            break;
        case PL192_SOFTINT:
            s->softint |= value;
            break;
        case PL192_SOFTINTCLEAR:
            s->softint &= ~value;
            break;
        case PL192_PROTECTION:
            /* TODO: implement protection */
            s->protection = value & 1;
            break;
        case PL192_SWPRIORITYMASK:
            s->sw_priority_mask = value & 0xffff;
            break;
        case PL192_PRIORITYDAISY:
            s->daisy_priority = value & 0xf;
            break;
        case PL192_VECTADDR:
            pl192_irq_fin(s);
            return;
        case PL190_ITCR:
        case PL190_VECTADDR:
        case PL190_DEFVECTADDR:
            /* NB: This thing is not present here, but linux wants to write it */
            /* Ignore written value */
            return;
        default:
            hw_error("pl192: bad write offset " TARGET_FMT_plx "\n", offset);
            return;
    }

    pl192_update(s);
}

static void pl192_irq_handler(void *opaque, int irq, int level)
{
    pl192_state *s = (pl192_state *) opaque;

    if (level) {
        s->rawintr |= 1 << irq;
    } else {
        s->rawintr &= ~(1 << irq);
    }
    pl192_update(opaque);
}

static void pl192_reset(DeviceState *d)
{
    pl192_state *s = FROM_SYSBUS(pl192_state, sysbus_from_qdev(d));
    int i;

    for (i = 0; i < PL192_INT_SOURCES; i++) {
        s->vect_priority[i] = 0xf;
    }
    s->sw_priority_mask = 0xffff;
    s->daisy_priority = 0xf;
    s->current = PL192_NO_IRQ;
    s->current_highest = PL192_NO_IRQ;
    s->stack_i = 0;
    s->priority_stack[0] = 0x10;
    s->irq_stack[0] = PL192_NO_IRQ;
    s->priority = 0x10;
}

static CPUReadMemoryFunc * const pl192_readfn[] = {
    pl192_read,
    pl192_read,
    pl192_read
};

static CPUWriteMemoryFunc * const pl192_writefn[] = {
    pl192_write,
    pl192_write,
    pl192_write
};

static void pl192_save(QEMUFile *f, void *opaque)
{
    pl192_state *s = (pl192_state *) opaque;
    int i;

    qemu_put_be32s(f, &s->irq_status);
    qemu_put_be32s(f, &s->fiq_status);
    qemu_put_be32s(f, &s->rawintr);
    qemu_put_be32s(f, &s->intselect);
    qemu_put_be32s(f, &s->intenable);
    qemu_put_be32s(f, &s->softint);
    qemu_put_be32s(f, &s->protection);
    qemu_put_be32s(f, &s->sw_priority_mask);

    for (i = 0; i < PL192_INT_SOURCES; i++) {
        qemu_put_be32s(f, &s->vect_addr[i]);
        qemu_put_be32s(f, &s->vect_priority[i]);
    }

    qemu_put_be32s(f, &s->address);
    qemu_put_be32s(f, &s->current);
    qemu_put_be32s(f, &s->current_highest);
    qemu_put_sbe32s(f, &s->stack_i);

    for (i = 0; i <= PL192_PRIO_LEVELS; i++) {
        qemu_put_be32s(f, &s->priority_stack[i]);
        qemu_put_8s   (f, &s->irq_stack[i]);
    }

    qemu_put_be32s(f, &s->priority);
    qemu_put_be32s(f, &s->daisy_vectaddr);
    qemu_put_be32s(f, &s->daisy_priority);
    qemu_put_8s   (f, &s->daisy_input);
}

static int pl192_load(QEMUFile *f, void *opaque, int version_id)
{
    pl192_state *s = (pl192_state *) opaque;
    int i;

    if (version_id != 1) {
        return -EINVAL;
    }

    qemu_get_be32s(f, &s->irq_status);
    qemu_get_be32s(f, &s->fiq_status);
    qemu_get_be32s(f, &s->rawintr);
    qemu_get_be32s(f, &s->intselect);
    qemu_get_be32s(f, &s->intenable);
    qemu_get_be32s(f, &s->softint);
    qemu_get_be32s(f, &s->protection);
    qemu_get_be32s(f, &s->sw_priority_mask);

    for (i = 0; i < PL192_INT_SOURCES; i++) {
        qemu_get_be32s(f, &s->vect_addr[i]);
        qemu_get_be32s(f, &s->vect_priority[i]);
    }

    qemu_get_be32s(f, &s->address);
    qemu_get_be32s(f, &s->current);
    qemu_get_be32s(f, &s->current_highest);
    qemu_get_sbe32s(f, &s->stack_i);

    for (i = 0; i <= PL192_PRIO_LEVELS; i++) {
        qemu_get_be32s(f, &s->priority_stack[i]);
        qemu_get_8s   (f, &s->irq_stack[i]);
    }

    qemu_get_be32s(f, &s->priority);
    qemu_get_be32s(f, &s->daisy_vectaddr);
    qemu_get_be32s(f, &s->daisy_priority);
    qemu_get_8s   (f, &s->daisy_input);

    return 0;
}

DeviceState *pl192_init(target_phys_addr_t base, int instance, ...)
{
    va_list va;
    qemu_irq irq;
    int n;

    DeviceState *dev = qdev_create(NULL, "pl192");
    SysBusDevice *s  = sysbus_from_qdev(dev);
    qdev_prop_set_uint32(dev, "instance", instance);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, base);

    va_start(va, instance);
    n = 0;
    while (1) {
        irq = va_arg(va, qemu_irq);
        if (!irq)
            break;
        sysbus_connect_irq(s, n, irq);
        n++;
    }
    return dev;
}

static int pl192_init1(SysBusDevice *dev)
{
    pl192_state *s = FROM_SYSBUS(pl192_state, dev);
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);
    sysbus_init_irq(dev, &s->fiq);

    /* Allocate IRQs */
    qdev_init_gpio_in(&dev->qdev, pl192_irq_handler, PL192_INT_SOURCES);

    /* Map Interrupt Controller registers to memory */
    iomemtype = cpu_register_io_memory(pl192_readfn, pl192_writefn, s, DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, PL192_IOMEM_SIZE, iomemtype);

    /* TODO: Interrupt Controller coprocessor??? */
    pl192_reset(&s->busdev.qdev);

    register_savevm(&dev->qdev, "pl192", s->instance, 1,
                    pl192_save, pl192_load, s);

    return 0;
}

static SysBusDeviceInfo pl192_info = {
    .init       = pl192_init1,
    .qdev.name  = "pl192",
    .qdev.size  = sizeof(pl192_state),
    .qdev.reset = pl192_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("instance", pl192_state, instance, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void pl192_register_devices(void)
{
    sysbus_register_withprop(&pl192_info);
}

void pl192_chain(void *first, void *next)
{
    pl192_state *s1 = FROM_SYSBUS(pl192_state, (SysBusDevice *)first);
    pl192_state *s2 = FROM_SYSBUS(pl192_state, (SysBusDevice *)next);

    s2->daisy = s1;
}

device_init(pl192_register_devices)

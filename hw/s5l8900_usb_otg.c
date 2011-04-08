/*
 * S5L8900 OTG USB 
 * 
 * -COMPLETELY BROKEN-
 * Copyright (c) 2009 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
 */

#include "sysbus.h"
#include "qemu-common.h"
#include "qemu-timer.h"
#include "usb.h"
#include "net.h"
#include "irq.h"
#include "hw.h"
#include "s5l8900.h"


/* Interrupts */
#define USB_INT_MODEMIS     (1 <<  1) /* Mode Mismatch Interrupt */
#define USB_INT_OTGINT      (1 <<  2) /* OTG Interrupt */
#define USB_INT_SOF         (1 <<  3) /* Start of (micro) Frame */
#define USB_INT_RXFLVL      (1 <<  4) /* RxFIFO Non-Empty */
#define USB_INT_NPTXFEMP    (1 <<  5) /* Non-periodic TxFIFO Empty */
#define USB_INT_GINNAKEFF   (1 <<  6) /* Global IN Non-periodic NAK Effective */
#define USB_INT_GOUTNAKEFF  (1 <<  7) /* Global OUT NAK Effective */
#define USB_INT_ERLYSUSP    (1 << 10) /* Early Suspend */
#define USB_INT_USBSUSP     (1 << 11) /* USB Suspend */
#define USB_INT_USBRST      (1 << 12) /* USB Reset */
#define USB_INT_ENUMDONE    (1 << 13) /* Enumeration Done */
#define USB_INT_ISOUTDROP   (1 << 14) /* Isochronous OUT Packet Dropped */
#define USB_INT_EOPF        (1 << 15) /* End of Periodic Frame */
#define USB_INT_IEPINT      (1 << 18) /* IN Endpoints Interrupt */
#define USB_INT_OEPINT      (1 << 19) /* OUT Endpoints Interrupt */
#define USB_INT_INCOMPLISOIN \
                            (1 << 20) /* Incomplete Isochronous IN Transfer */
#define USB_INT_INCOMPLISOOUT \
                            (1 << 21) /* Incomplete Isochronous OUT Transfer */
#define USB_INT_FETSUSP     (1 << 22) /* Data Fetch Suspended */
//#define USB_INT_PRTINT    (1 << 24) /* Host Port Interrupt */
//#define USB_INT_HCHINT    (1 << 25) /* Host Channels Interrupt */
#define USB_INT_PTXFEMP     (1 << 26) /* Periodic TxFIFO Empty */
#define USB_INT_CONIDSTSCHNG \
                            (1 << 28) /* Connector ID Status Change */
#define USB_INT_DISCONINT   (1 << 29) /* Disconnect Detected Interrupt */
#define USB_INT_SESSREQINT  (1 << 30) /* New Session Detected Interrupt */
#define USB_INT_WKUPINT     (1 << 31) /* Resume Interrupt */

#define EP_INT_XFERCOMPL    (1 <<  0) /* Transfer complete */
#define EP_INT_EPDISABLED   (1 <<  1) /* Endpoint disabled */
#define EP_INT_AHBERR       (1 <<  2) /* AHB error */
#define EP_INT_SETUP        (1 <<  3) /* [OUT] Setup phase done */
#define EP_INT_TIMEOUT      (1 <<  3) /* [IN] Timeout */
#define EP_INT_OUTTKNEPDIS  (1 <<  4) /* [OUT] Token Received When EP Disabled */
#define EP_INT_INTKNFIFOEMP (1 <<  4) /* [IN] Token Received When FIFO is Empty */
#define EP_INT_STSPHSERCVD  (1 <<  5) /* [OUT] Status Phase Received For Control Write */
#define EP_INT_INTTKNEPMIS  (1 <<  5) /* [IN] Token Received With EP Missmatch */
#define EP_INT_BACK2BACK    (1 <<  6) /* [OUT] Back-to-Back SETUP Packets Receive */
#define EP_INT_INEPNAKEFF   (1 <<  6) /* [IN] Endpoint NAK Effective */
#define EP_INT_TXFEMP       (1 <<  7) /* Transmit FIFO Empty */
#define EP_INT_OUTPKTERR    (1 <<  8) /* [OUT] Packet Error */
#define EP_INT_TXFIFOUNDRN  (1 <<  8) /* [IN] FIFO Underrun */
#define EP_INT_BNAINTR      (1 <<  9) /* Buffer not Available */


#define OTG_EP_DIR_IN       0x80
#define OTG_EP_DIR_OUT      0

#define OTG_EP_ENABLE       (1U << 31)
#define OTG_EP_DISABLE      (1 << 30)

#define OTG_EP_COUNT        16


typedef enum {
    OTG_STATE_START = 0,
    OTG_STATE_RESET,
    OTG_STATE_SPEEDDETECT,
    OTG_STATE_SETCONFIG_S,
    OTG_STATE_SETCONFIG_W,
    OTG_STATE_SETCONFIG_D,
    OTG_STATE_SETIFACE_S,
    OTG_STATE_SETIFACE_W,
    OTG_STATE_SETIFACE_D,
    OTG_STATE_OPERATIONAL
} OtgLogicalState;

typedef struct S5L8900UsbOtgEndPoint {
    uint32_t n;
    uint32_t ctrl;
    uint32_t interrupt;
    uint32_t transfer_size;
    uint32_t dma_addr;
    uint32_t dma_buf;
    uint32_t in_fifo_size;

    uint8_t dir;

    struct S5L8900UsbOtgState *parent;
} S5L8900UsbOtgEndPoint;

typedef struct S5L8900UsbOtgState {
    SysBusDevice busdev;

    struct S5L8900PhyState {
        uint32_t power;
        uint32_t clock;
        uint32_t reset;
        uint32_t tune0;
        uint32_t tune1;
    } phy;

    uint32_t gotg_ctl;
    uint32_t gotg_int;
    uint32_t gahb_cfg;
    uint32_t gusb_cfg;
    uint32_t grst_ctl;
    uint32_t gint_sts;
    uint32_t gint_msk;
    uint32_t grx_stsr;
    uint32_t grx_stsp;
    uint32_t grx_fsiz;
    uint32_t gnptx_fsiz;
    uint32_t gnptx_sts;
    uint32_t hnptx_fsiz;
    uint32_t daint_sts;
    uint32_t daint_msk;
    uint32_t diep_msk;
    uint32_t doep_msk;

    S5L8900UsbOtgEndPoint ep_in[OTG_EP_COUNT];
    S5L8900UsbOtgEndPoint ep_out[OTG_EP_COUNT];

    OtgLogicalState state;

    NICState *nic;
    NICConf conf;
    qemu_irq irq;
    uint8_t buf[1600];
    uint32_t buf_size;
    uint8_t buf_full;
} S5L8900UsbOtgState;


static const uint8_t otg_setup_packet[] = {
    0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00
};
static const uint8_t otg_setup_iface[] = {
    0x01, 0x0B, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00
};
static const uint8_t otg_setup_config[] = {
    0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void s5l8900_usb_otg_update_irq(S5L8900UsbOtgState *s)
{
    if (s->gint_sts & s->gint_msk) {
    	fprintf(stderr,"%s: qemu_irq_raise()\n", __func__);
        //qemu_irq_raise(s->irq);
    } else {
		 fprintf(stderr,"%s: qemu_irq_lower()\n", __func__);
        qemu_irq_lower(s->irq);
    }
}

static void s5l8900_usb_otg_initial_reset(DeviceState *d)
{
    S5L8900UsbOtgState *s =
        FROM_SYSBUS(S5L8900UsbOtgState, sysbus_from_qdev(d));
    int i;

    s->phy.power = 0x000001F9;
    s->phy.clock = 0x00000000;
    s->phy.reset = 0x00000009; /* TODO: I believe it should be 0 */
    s->phy.tune0 = 0x000919B3;
    s->phy.tune1 = 0x000919B3;

    s->gotg_ctl = 0x00010000;
    s->gotg_int = 0x00000000;
    s->gahb_cfg = 0x00000000;
    s->gusb_cfg = 0x00001408;
    s->grst_ctl = 0x80000000;
    s->gint_sts = 0x04000020;
    s->gint_msk = 0x00000000;

    for (i = 0; i < OTG_EP_COUNT; i++) {
        s->ep_in[i].parent = s;
        s->ep_in[i].dir = OTG_EP_DIR_IN;
        s->ep_in[i].n = i;
        s->ep_out[i].parent = s;
        s->ep_out[i].dir = OTG_EP_DIR_OUT;
        s->ep_out[i].n = i;
    }

    s->state = OTG_STATE_START;
    s->buf_full = 0;
}

static void s5l8900_usb_otg_reset(S5L8900UsbOtgState *s)
{
    s5l8900_usb_otg_initial_reset(&s->busdev.qdev);
    s->state = OTG_STATE_RESET;
    s->gotg_ctl += 0x000C0000;
    s->gint_sts |= USB_INT_USBRST;
}

static uint32_t s5l8900_usb_otg_phy_mm_read(void *opaque,
                                            target_phys_addr_t offset)
{
    S5L8900UsbOtgState *s = (S5L8900UsbOtgState *)opaque;

    fprintf(stderr, "%s: offset 0x%08x\n", __func__, offset);


    switch (offset) {
    case 0x00:
        return s->phy.power;
    case 0x04:
        return s->phy.clock;
    case 0x08:
        return s->phy.reset;
    default:
        hw_error("s5l8900.usb_otg: bad read offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static void s5l8900_usb_otg_phy_mm_write(void *opaque, target_phys_addr_t offset,
                                         uint32_t val)
{
    S5L8900UsbOtgState *s = (S5L8900UsbOtgState *)opaque;

    fprintf(stderr, "%s: offset 0x%08x val 0x%08x\n", __func__, offset, val);

    switch (offset) {
    case 0x00:
        s->phy.power = val;
        break;
    case 0x04:
        s->phy.clock = val;
        break;
    case 0x08:
        /* TODO: actually reset USB OTG */
        if (val & 0x1f) {
            s5l8900_usb_otg_reset(s);
            s->gint_sts |= USB_INT_USBRST;
            s5l8900_usb_otg_update_irq(s);
        }
        break;
    default:
        hw_error("s5l8900.usb_otg: bad write offset 0x" TARGET_FMT_plx "\n",
                 offset);
    }
}

static CPUReadMemoryFunc * const s5l8900_usb_otg_phy_mm_readfn[] = {
    s5l8900_usb_otg_phy_mm_read,
    s5l8900_usb_otg_phy_mm_read,
    s5l8900_usb_otg_phy_mm_read,
};

static CPUWriteMemoryFunc * const s5l8900_usb_otg_phy_mm_writefn[] = {
    s5l8900_usb_otg_phy_mm_write,
    s5l8900_usb_otg_phy_mm_write,
    s5l8900_usb_otg_phy_mm_write,
};

static void s5l8900_usb_otg_ep_update_irq(S5L8900UsbOtgEndPoint *s)
{

    fprintf(stderr, "%s: \n", __func__);

    if (s->interrupt) {
        if (s->dir == OTG_EP_DIR_IN) {
            s->parent->daint_sts |= (1 << s->n);
            if (s->parent->daint_sts & s->parent->daint_msk & 0xffff) {
                s->parent->gint_sts |= USB_INT_IEPINT;
            } else {
                s->parent->gint_sts &= ~USB_INT_IEPINT;
            }
        } else {
            s->parent->daint_sts |= (1 << (s->n + 16));
            if (s->parent->daint_sts & s->parent->daint_msk & 0xffff0000) {
                s->parent->gint_sts |= USB_INT_OEPINT;
            } else {
                s->parent->gint_sts &= ~USB_INT_OEPINT;
            }
        }
    } else {
        if (s->dir == OTG_EP_DIR_IN) {
            s->parent->daint_sts &= ~(1 << s->n);
            if (s->parent->daint_sts & s->parent->daint_msk & 0xffff) {
                s->parent->gint_sts |= USB_INT_IEPINT;
            } else {
                s->parent->gint_sts &= ~USB_INT_IEPINT;
            }
        } else {
            s->parent->daint_sts &= ~(1 << (s->n + 16));
            if (s->parent->daint_sts & s->parent->daint_msk & 0xffff0000) {
                s->parent->gint_sts |= USB_INT_OEPINT;
            } else {
                s->parent->gint_sts &= ~USB_INT_OEPINT;
            }
        }
    }
    s5l8900_usb_otg_update_irq(s->parent);
}

static void s5l8900_usb_otg_act(S5L8900UsbOtgState *s)
{
    fprintf(stderr, "%s: \n", __func__);

    switch (s->state) {
        case OTG_STATE_START:
        case OTG_STATE_RESET:
        case OTG_STATE_SPEEDDETECT:
            break;
        case OTG_STATE_SETCONFIG_S:
            if (s->ep_out[0].ctrl & OTG_EP_ENABLE) {
                s->state = OTG_STATE_SETCONFIG_W;
                cpu_physical_memory_write(s->ep_out[0].dma_addr,
                                          otg_setup_config, 8);
                s->ep_out[0].ctrl &= ~OTG_EP_ENABLE;
                s->ep_out[0].interrupt |= EP_INT_SETUP|EP_INT_XFERCOMPL;
                s5l8900_usb_otg_ep_update_irq(&s->ep_out[0]);
            }
            break;
        case OTG_STATE_SETIFACE_S:
            if (s->ep_out[0].ctrl & OTG_EP_ENABLE) {
                s->state = OTG_STATE_SETIFACE_W;
                cpu_physical_memory_write(s->ep_out[0].dma_addr,
                                          otg_setup_iface, 8);
                s->ep_out[0].ctrl &= ~OTG_EP_ENABLE;
                s->ep_out[0].interrupt |= EP_INT_SETUP|EP_INT_XFERCOMPL;
                s5l8900_usb_otg_ep_update_irq(&s->ep_out[0]);
            }
            break;
        default:
            break;
    }
}

static void s5l8900_usb_otg_data_tx(S5L8900UsbOtgEndPoint *s)
{
    uint8_t buf[1600];
    uint32_t size = s->transfer_size & 0x7ffff;

    fprintf(stderr, "%s: ", __func__);

    cpu_physical_memory_read(s->dma_addr, buf, size);
    qemu_send_packet(&s->parent->nic->nc, buf, size);
    s->interrupt |= EP_INT_XFERCOMPL|EP_INT_TXFEMP;
    s5l8900_usb_otg_ep_update_irq(s);
}

static void s5l8900_usb_otg_data_rx(S5L8900UsbOtgEndPoint *s)
{
    uint32_t size = s->parent->buf_size;

    fprintf(stderr, "%s: \n", __func__);

    if (s->parent->buf_size > (s->transfer_size & 0x7ffff)) {
        s->parent->buf_full = 0;
        /* Packet dropped */
        return ;
    }
    cpu_physical_memory_write(s->dma_addr, s->parent->buf, size);
    s->dma_buf = s->dma_addr + size;
    s->transfer_size -= size;
    s->ctrl &= ~OTG_EP_ENABLE;
    s->interrupt |= EP_INT_XFERCOMPL;
    s->parent->buf_full = 0;
    s5l8900_usb_otg_ep_update_irq(s);
}

static uint32_t s5l8900_usb_otg_ep_read(S5L8900UsbOtgEndPoint *s,
                                        target_phys_addr_t addr)
{
    fprintf(stderr, "%s: \n", __func__);

    switch (addr) {
    case 0x00:
        return s->ctrl;
    case 0x08:
        return s->interrupt;
    case 0x10:
        return s->transfer_size;
    case 0x14:
        return s->dma_addr;
    case 0x1C:
        return s->dma_buf;
    default:
        hw_error("s5l8900.usb_otg: bad write offset 0x" TARGET_FMT_plx "\n",
                 addr);
    }
}

static uint32_t s5l8900_usb_otg_ep_write(S5L8900UsbOtgEndPoint *s,
                                         target_phys_addr_t addr, uint32_t val)
{
    fprintf(stderr, "%s: addr 0x%08x val 0x%08x\n", __func__, addr, val);

    switch (addr) {
    case 0x00:
        if ((val & OTG_EP_DISABLE) && (s->ctrl & OTG_EP_ENABLE)) {
            s->ctrl &= ~OTG_EP_ENABLE;
            val &= ~OTG_EP_DISABLE;
            s->interrupt |= EP_INT_EPDISABLED;
            s5l8900_usb_otg_ep_update_irq(s);
        }
        val &= ~OTG_EP_DISABLE;
        if (val & OTG_EP_ENABLE) {
            s5l8900_usb_otg_act(s->parent);
            if (s->n == 0 && s->dir == OTG_EP_DIR_IN) {
                s->interrupt |= EP_INT_XFERCOMPL|EP_INT_TXFEMP;
                if (s->parent->state == OTG_STATE_SETCONFIG_W) {
                    s->parent->state = OTG_STATE_SETIFACE_S;
                } else if (s->parent->state == OTG_STATE_SETIFACE_W) {
                    s->parent->state = OTG_STATE_OPERATIONAL;
                }
                s5l8900_usb_otg_ep_update_irq(s);
            }
            if (s->n != 0 && s->dir == OTG_EP_DIR_IN) {
                s5l8900_usb_otg_data_tx(s);
                val &= ~OTG_EP_ENABLE;
            }
            if (s->n != 0 && s->dir == OTG_EP_DIR_OUT &&
                s->parent->buf_full == 1) {
                s5l8900_usb_otg_data_rx(s);
                val &= ~OTG_EP_ENABLE;
            }
        }
        val &= ~(0xC << 24); /* TODO: handle NAK? */
        s->ctrl = val;
        /* TODO: handle control */
        break;
    case 0x08:
        s->interrupt &= ~val;
        s5l8900_usb_otg_ep_update_irq(s);
        break;
    case 0x10:
        s->transfer_size = val;
        break;
    case 0x14:
        s->dma_addr = val;
        break;
    default:
        hw_error("s5l8900.usb_otg: bad write offset 0x" TARGET_FMT_plx "\n",
                 addr);
    }
    return 0;
}

static uint32_t s5l8900_usb_otg_read(void *opaque, target_phys_addr_t addr)
{
    S5L8900UsbOtgState *s = (S5L8900UsbOtgState *)opaque;

/*
    if (addr >= 0x100000 && addr < 0x100028) {
        return s5l8900_usb_otg_phy_mm_read(opaque, addr - 0x100000);
    }
*/

    fprintf(stderr, "%s: offset 0x%08x\n", __func__, addr);

    switch (addr) {
    case 0x00:
        return s->gotg_ctl;
    case 0x04:
        return s->gotg_int;
    case 0x08:
        return s->gahb_cfg;
    case 0x0C:
        return s->gusb_cfg;
    case 0x10:
        return s->grst_ctl;
    case 0x14:
        return s->gint_sts;
    case 0x18:
        return s->gint_msk;
    case 0x1C:
        return s->grx_stsr;
    case 0x20:
        return s->grx_stsp;
    case 0x24:
        return s->grx_fsiz;
    case 0x28:
        return s->gnptx_fsiz;
    case 0x2C:
        return s->gnptx_sts;
    case 0x30:
        return s->hnptx_fsiz;
    case 0x100 ... 0x13C:
        return s->ep_in[(addr - 0x100) >> 2].in_fifo_size;
    case 0x810:
        return s->diep_msk;
    case 0x814:
        return s->doep_msk;
    case 0x818:
        return s->daint_sts;
    case 0x81C:
        return s->daint_msk;
    case 0x900 ... 0xAFC:
        addr -= 0x900;
        return s5l8900_usb_otg_ep_read(&s->ep_in[addr >> 5], addr & 0x1f);
    case 0xB00 ... 0xCFC:
        addr -= 0xB00;
        return s5l8900_usb_otg_ep_read(&s->ep_out[addr >> 5], addr & 0x1f);
    }
    return 0;
}

static void s5l8900_usb_otg_write(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    int i;
    S5L8900UsbOtgState *s = (S5L8900UsbOtgState *)opaque;

	/*
    if (addr >= 0x100000 && addr < 0x100028) {
        s5l8900_usb_otg_phy_mm_write(opaque, addr - 0x100000, val);
        return;
    }
	*/

	fprintf(stderr, "%s: offset 0x%08x val 0x%08x\n", __func__, addr, val);

    switch (addr) {
    case 0x00:
        s->gotg_ctl = val;
        break;
    case 0x04:
        s->gotg_int &= ~val;
        s5l8900_usb_otg_update_irq(s);
        break;
    case 0x08:
        s->gahb_cfg = val;
        break;
    case 0x0C:
        s->gusb_cfg = val;
        break;
    case 0x10:
        if (val & 1) {
            s5l8900_usb_otg_reset(s);
            s->gint_sts |= USB_INT_USBRST;
            s->state = OTG_STATE_RESET;
            s5l8900_usb_otg_update_irq(s);
        } else if (val & 0x0f) {
            s->gint_sts |= USB_INT_USBRST;
            s->state = OTG_STATE_RESET;
            s5l8900_usb_otg_update_irq(s);
        }
        //s->grst_ctl = val & (~0x3f);
        s5l8900_usb_otg_update_irq(s);
        break;
    case 0x14:
        val &= ~(7 << 24);
        val &= ~(3 << 18);
        val &= ~(0xf << 4);
        val &= ~0x5;
        s->gint_sts &= ~val;
        if (val == 0x1000) {
            s->gint_sts |= USB_INT_ENUMDONE;
            s->state = OTG_STATE_SPEEDDETECT;
            s5l8900_usb_otg_act(s);
        }
        if (val == USB_INT_ENUMDONE) {
            s->state = OTG_STATE_SETCONFIG_S;
            s5l8900_usb_otg_act(s);
        }
        s5l8900_usb_otg_update_irq(s);
        break;
    case 0x18:
        s->gint_msk = val;
        break;
    case 0x24:
        s->grx_fsiz = val;
        break;
    case 0x28:
        s->gnptx_fsiz = val;
        break;
    case 0x30:
        s->hnptx_fsiz = val;
        break;
    case 0x100 ... 0x13C:
        s->ep_in[(addr - 0x100 ) >> 2].in_fifo_size = val;
        break;
    case 0x810:
        s->diep_msk = val;
        for (i = 0; i < OTG_EP_COUNT; i++) {
            s5l8900_usb_otg_ep_update_irq(&s->ep_in[i]);
        }
        break;
    case 0x814:
        s->doep_msk = val;
        for (i = 0; i < OTG_EP_COUNT; i++) {
            s5l8900_usb_otg_ep_update_irq(&s->ep_out[i]);
        }
        break;
    case 0x81C:
        s->daint_msk = val;
        if (s->daint_sts & 0xffff & s->daint_msk) {
            s->gint_sts |= USB_INT_IEPINT;
        } else {
            s->gint_sts &= ~USB_INT_IEPINT;
        }
        if ((s->daint_sts & s->daint_msk) >> 16) {
            s->gint_sts |= USB_INT_OEPINT;
        } else {
            s->gint_sts &= ~USB_INT_OEPINT;
        }
		if(s->daint_msk == 0xFFFFFFFF)
			break;
        s5l8900_usb_otg_update_irq(s);
        break;
    case 0x900 ... 0xAFC:
        addr -= 0x900;
        s5l8900_usb_otg_ep_write(&s->ep_in[addr >> 5], addr & 0x1f, val);
        break;
    case 0xB00 ... 0xCFC:
        addr -= 0xB00;
        s5l8900_usb_otg_ep_write(&s->ep_out[addr >> 5], addr & 0x1f, val);
        break;
    }
}

static CPUReadMemoryFunc * const s5l8900_usb_otg_readfn[] = {
    s5l8900_usb_otg_read,
    s5l8900_usb_otg_read,
    s5l8900_usb_otg_read
};

static CPUWriteMemoryFunc * const s5l8900_usb_otg_writefn[] = {
    s5l8900_usb_otg_write,
    s5l8900_usb_otg_write,
    s5l8900_usb_otg_write
};

static int s5l8900_usb_otg_init1(SysBusDevice *dev)
{
    int iomemtype;
    S5L8900UsbOtgState *s = FROM_SYSBUS(S5L8900UsbOtgState, dev);

    iomemtype =
        cpu_register_io_memory(s5l8900_usb_otg_readfn,
                               s5l8900_usb_otg_writefn, s, DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, 0x100000, iomemtype);
    sysbus_init_irq(dev, &s->irq);


	/* Remap OTG PHY regs */
    iomemtype = cpu_register_io_memory(s5l8900_usb_otg_phy_mm_readfn,
                                           s5l8900_usb_otg_phy_mm_writefn, s, DEVICE_LITTLE_ENDIAN);
    cpu_register_physical_memory(0x3c400000, 0x40, iomemtype);

	/*
    qemu_macaddr_default_if_unset(&s->conf.macaddr);
	*/
    s5l8900_usb_otg_initial_reset(&s->busdev.qdev);

	/*
    s->nic = qemu_new_nic(&net_s5l8900_usb_otg_info, &s->conf,
                          dev->qdev.info->name, dev->qdev.id, s);
    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);

    register_savevm(&dev->qdev, "s5l8900.usb.otg", -1, 1,
                    s5l8900_usb_otg_save, s5l8900_usb_otg_load, s);
	*/

    return 0;
}

static SysBusDeviceInfo s5l8900_usb_otg_info = {
    .init = s5l8900_usb_otg_init1,
    .qdev.name  = "s5l8900.usb.otg",
    .qdev.size  = sizeof(S5L8900UsbOtgState),
    .qdev.reset = s5l8900_usb_otg_initial_reset,
    .qdev.props = (Property[]) {
        DEFINE_NIC_PROPERTIES(S5L8900UsbOtgState, conf),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void s5l8900_usb_otg_register_devices(void)
{
    sysbus_register_withprop(&s5l8900_usb_otg_info);
}

/* Legacy helper function.  Should go away when machine config files are
   implemented.  */
void s5l8900_usb_otg_init(NICInfo *nd, target_phys_addr_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    //qemu_check_nic_model(nd, "s5l8900-usb-otg");
    dev = qdev_create(NULL, "s5l8900.usb.otg");
    //qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

device_init(s5l8900_usb_otg_register_devices)

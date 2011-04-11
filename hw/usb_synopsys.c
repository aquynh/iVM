/*
 * Synopsys DesignWareCore for USB OTG.
 *
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysbus.h"
#include "qemu-common.h"
#include "qemu-timer.h"
#include "usb.h"
#include "net.h"
#include "irq.h"
#include "hw.h"
#include "usb_synopsys.h"
#include "tcp_usb.h"

#define DEVICE_NAME		"usb_synopsys"

// Maximums supported by OIB
#define USB_NUM_ENDPOINTS	8
#define USB_NUM_FIFOS		16

#define RX_FIFO_DEPTH				0x1C0
#define TX_FIFO_DEPTH				0x1C0
#define TX_FIFO_STARTADDR			0x200
#define PERIODIC_TX_FIFO_STARTADDR	0x21B
#define PERIODIC_TX_FIFO_DEPTH		0x100

// Registers
#define GOTGCTL		0x0
#define GOTGINT		0x4
#define GAHBCFG		0x8
#define GUSBCFG		0xC
#define GRSTCTL		0x10
#define GINTSTS		0x14
#define GINTMSK		0x18
#define GRXFSIZ		0x24
#define GNPTXFSIZ	0x28
#define GNPTXFSTS	0x2C
#define GHWCFG1		0x44
#define GHWCFG2		0x48
#define GHWCFG3		0x4C
#define GHWCFG4		0x50
#define DIEPTXF(x)	(0x100 + (4*(x)))
#define DCFG		0x800
#define DCTL		0x804
#define DSTS		0x808
#define DIEPMSK		0x810
#define DOEPMSK		0x814
#define DAINTSTS	0x818
#define DAINTMSK	0x81C
#define DTKNQR1		0x820
#define DTKNQR2		0x824
#define DTKNQR3		0x830
#define DTKNQR4		0x834
#define USB_INREGS	0x900
#define USB_OUTREGS	0xB00
#define USB_EPREGS_SIZE 0x200

#define USB_FIFO_START	0x1000
#define USB_FIFO_SIZE	(0x100*(USB_NUM_FIFOS+1))
#define USB_FIFO_END	(USB_FIFO_START+USB_FIFO_SIZE)

#define PCGCCTL     0xE00

#define PCGCCTL_ONOFF_MASK  3   // bits 0, 1
#define PCGCCTL_ON          0
#define PCGCCTL_OFF         1

#define GOTGCTL_BSESSIONVALID (1 << 19)
#define GOTGCTL_SESSIONREQUEST (1 << 1)

#define GAHBCFG_DMAEN (1 << 5)
#define GAHBCFG_BSTLEN_SINGLE (0 << 1)
#define GAHBCFG_BSTLEN_INCR (1 << 1)
#define GAHBCFG_BSTLEN_INCR4 (3 << 1)
#define GAHBCFG_BSTLEN_INCR8 (5 << 1)
#define GAHBCFG_BSTLEN_INCR16 (7 << 1)
#define GAHBCFG_MASKINT 0x1

#define GUSBCFG_TURNAROUND_MASK 0xF
#define GUSBCFG_TURNAROUND_SHIFT 10
#define GUSBCFG_HNPENABLE (1 << 9)
#define GUSBCFG_SRPENABLE (1 << 8)
#define GUSBCFG_PHYIF16BIT (1 << 3)
#define USB_UNKNOWNREG1_START 0x1708

#define GHWCFG2_TKNDEPTH_SHIFT	26
#define GHWCFG2_TKNDEPTH_MASK	0xF
#define GHWCFG2_NUM_ENDPOINTS_SHIFT	10
#define GHWCFG2_NUM_ENDPOINTS_MASK	0xf

#define GHWCFG4_DED_FIFO_EN			(1 << 25)

#define GRSTCTL_AHBIDLE			(1 << 31)
#define GRSTCTL_TXFFLUSH		(1 << 5)
#define GRSTCTL_TXFFNUM_SHIFT	6
#define GRSTCTL_TXFFNUM_MASK	0x1f
#define GRSTCTL_CORESOFTRESET	0x1
#define GRSTCTL_TKNFLUSH		3

#define GINTMSK_NONE        0x0
#define GINTMSK_OTG         (1 << 2)
#define GINTMSK_SOF         (1 << 3)
#define GINTMSK_GINNAKEFF   (1 << 6)
#define GINTMSK_GOUTNAKEFF  (1 << 7)
#define GINTMSK_SUSPEND     (1 << 11)
#define GINTMSK_RESET       (1 << 12)
#define GINTMSK_ENUMDONE    (1 << 13)
#define GINTMSK_EPMIS       (1 << 17)
#define GINTMSK_INEP        (1 << 18)
#define GINTMSK_OEP         (1 << 19)
#define GINTMSK_DISCONNECT  (1 << 29)
#define GINTMSK_RESUME      (1 << 31)

#define GOTGINT_SESENDDET	(1 << 2)

#define FIFO_DEPTH_SHIFT 16

#define GNPTXFSTS_GET_TXQSPCAVAIL(x) GET_BITS(x, 16, 8)

#define GHWCFG4_DED_FIFO_EN         (1 << 25)

#define DAINT_ALL                   0xFFFFFFFF
#define DAINT_NONE                  0
#define DAINT_OUT_SHIFT             16
#define DAINT_IN_SHIFT              0

#define DCTL_SFTDISCONNECT			0x2
#define DCTL_PROGRAMDONE			(1 << 11)
#define DCTL_CGOUTNAK				(1 << 10)
#define DCTL_SGOUTNAK				(1 << 9)
#define DCTL_CGNPINNAK				(1 << 8)
#define DCTL_SGNPINNAK				(1 << 7)

#define DSTS_GET_SPEED(x) GET_BITS(x, 1, 2)

#define DCFG_NZSTSOUTHSHK           (1 << 2)
#define DCFG_EPMSCNT                (1 << 18)
#define DCFG_HISPEED                0x0
#define DCFG_FULLSPEED              0x1
#define DCFG_DEVICEADDR_UNSHIFTED_MASK 0x7F
#define DCFG_DEVICEADDR_SHIFT 4
#define DCFG_DEVICEADDRMSK (DCFG_DEVICEADDR_UNSHIFTED_MASK << DCFG_DEVICEADDR_SHIFT)
#define DCFG_ACTIVE_EP_COUNT_MASK	0x1f
#define DCFG_ACTIVE_EP_COUNT_SHIFT	18

#define DOEPTSIZ0_SUPCNT_MASK 0x3
#define DOEPTSIZ0_SUPCNT_SHIFT 29
#define DOEPTSIZ0_PKTCNT_MASK 0x1
#define DEPTSIZ0_XFERSIZ_MASK 0x7F
#define DIEPTSIZ_MC_MASK 0x3
#define DIEPTSIZ_MC_SHIFT 29
#define DEPTSIZ_PKTCNT_MASK 0x3FF
#define DEPTSIZ_PKTCNT_SHIFT 19
#define DEPTSIZ_XFERSIZ_MASK 0x1FFFF

// ENDPOINT_DIRECTIONS register has two bits per endpoint. 0, 1 for endpoint 0. 1, 2 for end point 1, etc.
#define USB_EP_DIRECTION(ep) (USBDirection)(2-((GET_REG(USB + GHWCFG1) >> ((ep) * 2)) & 0x3))
#define USB_ENDPOINT_DIRECTIONS_BIDIR 0
#define USB_ENDPOINT_DIRECTIONS_IN 1
#define USB_ENDPOINT_DIRECTIONS_OUT 2

#define USB_START_DELAYUS 10000
#define USB_SFTDISCONNECT_DELAYUS 4000
#define USB_ONOFFSTART_DELAYUS 100
#define USB_RESETWAITFINISH_DELAYUS 1000
#define USB_SFTCONNECT_DELAYUS 250
#define USB_PROGRAMDONE_DELAYUS 10

#define USB_EPCON_ENABLE		(1 << 31)
#define USB_EPCON_DISABLE		(1 << 30)
#define USB_EPCON_SETD0PID		(1 << 28)
#define USB_EPCON_SETNAK		(1 << 27)
#define USB_EPCON_CLEARNAK		(1 << 26)
#define USB_EPCON_TXFNUM_MASK	0xf
#define USB_EPCON_TXFNUM_SHIFT	22
#define USB_EPCON_STALL			(1 << 21)
#define USB_EPCON_TYPE_MASK		0x3
#define USB_EPCON_TYPE_SHIFT	18
#define USB_EPCON_NAKSTS		(1 << 17)
#define USB_EPCON_ACTIVE		(1 << 15)
#define USB_EPCON_NEXTEP_MASK	0xF
#define USB_EPCON_NEXTEP_SHIFT	11
#define USB_EPCON_MPS_MASK		0x7FF

#define USB_EPINT_INEPNakEff 0x40
#define USB_EPINT_INTknEPMis 0x20
#define USB_EPINT_INTknTXFEmp 0x10
#define USB_EPINT_TimeOUT 0x8
#define USB_EPINT_AHBErr 0x4
#define USB_EPINT_EPDisbld 0x2
#define USB_EPINT_XferCompl 0x1

#define USB_EPINT_Back2BackSetup (1 << 6)
#define USB_EPINT_OUTTknEPDis 0x10
#define USB_EPINT_SetUp 0x8
#define USB_EPINT_EpDisbld 0x1
#define USB_EPINT_NONE 0
#define USB_EPINT_ALL 0xFFFFFFFF

#define USB_2_0 0x0200

#define USB_HIGHSPEED 0
#define USB_FULLSPEED 1
#define USB_LOWSPEED 2
#define USB_FULLSPEED_48_MHZ 3

#define USB_CONTROLEP 0

typedef struct _synopsys_usb_ep_state
{
	uint32_t control;
	uint32_t tx_size;
	uint32_t fifo;
	uint32_t interrupt_status;

	target_phys_addr_t dma_address;
	target_phys_addr_t dma_buffer;

} synopsys_usb_ep_state;

typedef struct _synopsys_usb_state
{
	SysBusDevice busdev;
	qemu_irq irq;

	char *server_host;
	uint32_t server_port;
	tcp_usb_state_t tcp_state;

	uint32_t ghwcfg1;
	uint32_t ghwcfg2;
	uint32_t ghwcfg3;
	uint32_t ghwcfg4;

	uint32_t grxfsiz;
	uint32_t gnptxfsiz;

	uint32_t grstctl;
	uint32_t gintmsk;
	uint32_t gintsts;

	uint32_t dptxfsiz[USB_NUM_FIFOS];

	uint32_t dctl;
	uint32_t dcfg;
	uint32_t dsts;
	uint32_t daintmsk;
	uint32_t daintsts;

	synopsys_usb_ep_state in_eps[USB_NUM_ENDPOINTS];
	synopsys_usb_ep_state out_eps[USB_NUM_ENDPOINTS];

	uint8_t fifos[0x100 * (USB_NUM_FIFOS+1)];

} synopsys_usb_state;

static inline size_t synopsys_usb_tx_fifo_start(synopsys_usb_state *_state, uint32_t _fifo)
{
	if(_fifo == 0)
		return _state->gnptxfsiz >> 16;
	else
		return _state->dptxfsiz[_fifo-1] >> 16;
}

static inline size_t synopsys_usb_tx_fifo_size(synopsys_usb_state *_state, uint32_t _fifo)
{
	if(_fifo == 0)
		return _state->gnptxfsiz & 0xFFFF;
	else
		return _state->dptxfsiz[_fifo-1] & 0xFFFF;
}

static void synopsys_usb_update_irq(synopsys_usb_state *_state)
{
	_state->daintsts = 0;
	_state->gintsts &=~ (GINTMSK_OEP | GINTMSK_INEP);

	int i;
	for(i = 0; i < USB_NUM_ENDPOINTS; i++)
	{
		if(_state->out_eps[i].interrupt_status)
		{
			_state->daintsts |= 1 << (i+DAINT_OUT_SHIFT);
			if(_state->daintmsk & (1 << (i+DAINT_OUT_SHIFT)))
				_state->gintsts |= GINTMSK_OEP;
		}

		if(_state->in_eps[i].interrupt_status)
		{
			_state->daintsts |= 1 << (i+DAINT_IN_SHIFT);
			if(_state->daintmsk & (1 << (i+DAINT_IN_SHIFT)))
				_state->gintsts |= GINTMSK_INEP;
		}
	}
	
	if(_state->gintmsk & _state->gintsts)
	{
		printf("USB: IRQ triggered.\n");
		qemu_irq_raise(_state->irq);
	}
	else
	{
		printf("USB: IRQ lowered.\n");
		qemu_irq_lower(_state->irq);
	}
}

static void synopsys_usb_update_ep(synopsys_usb_state *_state, synopsys_usb_ep_state *_ep)
{
	if(_ep->control & USB_EPCON_SETNAK)
	{
		_ep->control |= USB_EPCON_NAKSTS;
		_ep->interrupt_status |= USB_EPINT_INEPNakEff;
		_ep->control &=~ USB_EPCON_SETNAK;
	}

	if(_ep->control & USB_EPCON_DISABLE)
	{
		_ep->interrupt_status |= USB_EPINT_EPDisbld;
		_ep->control &=~ (USB_EPCON_DISABLE | USB_EPCON_ENABLE);
	}
}

static void synopsys_usb_update_in_ep(synopsys_usb_state *_state, uint8_t _ep)
{
	synopsys_usb_ep_state *eps = &_state->in_eps[_ep];
	synopsys_usb_update_ep(_state, eps);
}

static void synopsys_usb_update_out_ep(synopsys_usb_state *_state, uint8_t _ep)
{
	synopsys_usb_ep_state *eps = &_state->out_eps[_ep];
	synopsys_usb_update_ep(_state, eps);
}

static int synopsys_usb_tcp_callback(tcp_usb_state_t *_state, void *_arg, tcp_usb_header_t *_hdr, char *_buffer)
{
	synopsys_usb_state *state = _arg;

	_hdr->addr = (state->dcfg & DCFG_DEVICEADDRMSK) >> DCFG_DEVICEADDR_SHIFT;

	if(_hdr->flags & tcp_usb_reset)
	{
		state->gintsts |= GINTMSK_RESET;
		synopsys_usb_update_irq(state);
		return 0;
	}

	if(_hdr->flags & tcp_usb_enumdone)
	{
		state->gintsts |= GINTMSK_ENUMDONE;
		synopsys_usb_update_irq(state);
	}

	uint8_t ep = _hdr->ep & 0x7f;
	if(_hdr->ep & USB_DIR_IN)
	{
		synopsys_usb_ep_state *eps = &state->in_eps[ep];

		if(eps->control & USB_EPCON_STALL)
		{
			eps->control &=~ USB_EPCON_STALL; // Should this be EP0 only
			printf("USB: Stall.\n");
			return USB_RET_STALL;
		}
		else if(eps->control & USB_EPCON_ENABLE)
		{
			// Do IN transfer!
			eps->control &=~ USB_EPCON_ENABLE;

			size_t sz = eps->tx_size & DEPTSIZ_XFERSIZ_MASK;
			size_t amtDone = sz;
			if(amtDone > _hdr->length)
				amtDone = _hdr->length;

			if(eps->fifo >= USB_NUM_FIFOS)
				hw_error("usb_synopsys: USB transfer on non-existant FIFO %d!\n", eps->fifo);

			size_t txfz = synopsys_usb_tx_fifo_size(state, eps->fifo);
			if(amtDone > txfz)
				amtDone = txfz;

			size_t txfs = synopsys_usb_tx_fifo_start(state, eps->fifo);
			if(txfs + txfz > sizeof(state->fifos))
				hw_error("usb_synopsys: USB transfer would overflow FIFO buffer!\n");

			printf("USB: Starting IN transfer on EP %d (%d)...\n", ep, amtDone);

			if(amtDone > 0)
			{
				if(eps->dma_address)
				{
					cpu_physical_memory_read(eps->dma_address, &state->fifos[txfs], amtDone);
					eps->dma_address += amtDone;
				}

				memcpy(_buffer, (char*)&state->fifos[txfs], amtDone);
			}

			printf("USB: IN transfer complete!\n");

			eps->tx_size = (eps->tx_size &~ DEPTSIZ_XFERSIZ_MASK)
							| ((sz-amtDone) & DEPTSIZ_XFERSIZ_MASK);
			eps->interrupt_status |= USB_EPINT_XferCompl;

			synopsys_usb_update_irq(state);

			return amtDone;
		}
		else
			return USB_RET_NAK;
	}
	else // OUT
	{	
		synopsys_usb_ep_state *eps = &state->out_eps[ep];
		
		if(eps->control & USB_EPCON_STALL)
		{
			eps->control &=~ USB_EPCON_STALL; // Should this be EP0 only
			printf("USB: Stall.\n");
			return USB_RET_STALL;
		}
		else if(eps->control & USB_EPCON_ENABLE)
		{
			// Do OUT transfer!
			eps->control &=~ USB_EPCON_ENABLE;

			size_t sz = eps->tx_size & DEPTSIZ_XFERSIZ_MASK;
			size_t amtDone = sz;
			if(amtDone > _hdr->length)
				amtDone = _hdr->length;

			size_t rxfz = state->grxfsiz;
			if(amtDone > rxfz)
				amtDone = rxfz;

			if(rxfz > sizeof(state->fifos))
				hw_error("usb_synopsys: USB transfer would overflow FIFO buffer!\n");

			printf("USB: Starting OUT transfer on EP %d (%d)...\n", ep, amtDone);

			if(amtDone > 0)
			{
				memcpy((char*)state->fifos, _buffer, amtDone);

				if(eps->dma_address)
				{
					printf("USB: DMA copying to 0x%08x.\n", eps->dma_address);
					cpu_physical_memory_write(eps->dma_address, state->fifos, amtDone);
					eps->dma_address += amtDone;
				}
			}

			printf("USB: OUT transfer complete!\n");

			if(_hdr->flags & tcp_usb_setup)
			{
				printf("USB: Setup %02x %02x %02x %02x %02x %02x %02x %02x\n",
						state->fifos[0],
						state->fifos[1],
						state->fifos[2],
						state->fifos[3],
						state->fifos[4],
						state->fifos[5],
						state->fifos[6],
						state->fifos[7]);

				eps->interrupt_status |= USB_EPINT_SetUp;
			}
			else
				eps->interrupt_status |= USB_EPINT_XferCompl;

			eps->tx_size = (eps->tx_size &~ DEPTSIZ_XFERSIZ_MASK)
							| ((sz-amtDone) & DEPTSIZ_XFERSIZ_MASK);

			synopsys_usb_update_irq(state);

			return amtDone;
		}
		else
			return USB_RET_NAK;
	}
}

static uint32_t synopsys_usb_in_ep_read(synopsys_usb_state *_state, uint8_t _ep, target_phys_addr_t _addr)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Tried to read from disabled EP %d.\n", _ep);
		return 0;
	}

    switch (_addr)
	{
    case 0x00:
        return _state->in_eps[_ep].control;

    case 0x08:
        return _state->in_eps[_ep].interrupt_status;

    case 0x10:
        return _state->in_eps[_ep].tx_size;

    case 0x14:
        return _state->in_eps[_ep].dma_address;

    case 0x1C:
        return _state->in_eps[_ep].dma_buffer;

    default:
        hw_error("usb_synopsys: bad ep read offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }

	return 0;
}

static uint32_t synopsys_usb_out_ep_read(synopsys_usb_state *_state, int _ep, target_phys_addr_t _addr)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Tried to read from disabled EP %d.\n", _ep);
		return 0;
	}

    switch (_addr)
	{
    case 0x00:
        return _state->out_eps[_ep].control;

    case 0x08:
        return _state->out_eps[_ep].interrupt_status;

    case 0x10:
        return _state->out_eps[_ep].tx_size;

    case 0x14:
        return _state->out_eps[_ep].dma_address;

    case 0x1C:
        return _state->out_eps[_ep].dma_buffer;

    default:
        hw_error("usb_synopsys: bad ep read offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }

	return 0;
}

static uint32_t synopsys_usb_read(void *_arg, target_phys_addr_t _addr)
{
	synopsys_usb_state *state = _arg;

	switch(_addr)
	{
	case GRSTCTL:
		return state->grstctl;

	case GHWCFG1:
		return state->ghwcfg1;

	case GHWCFG2:
		return state->ghwcfg2;

	case GHWCFG3:
		return state->ghwcfg3;

	case GHWCFG4:
		return state->ghwcfg4;

	case GINTMSK:
		return state->gintmsk;

	case GINTSTS:
		return state->gintsts;

	case DAINTMSK:
		return state->daintmsk;
	
	case DAINTSTS:
		return state->daintsts;

	case DCTL:
		return state->dctl;

	case DCFG:
		return state->dcfg;

	case DSTS:
		return state->dsts;

	case GNPTXFSTS:
		return 0xFFFFFFFF;

	case GRXFSIZ:
		return state->grxfsiz;

	case GNPTXFSIZ:
		return state->gnptxfsiz;

	case DIEPTXF(1) ... DIEPTXF(USB_NUM_FIFOS+1):
		_addr -= DIEPTXF(1);
		_addr >>= 2;
		return state->dptxfsiz[_addr];

	case USB_INREGS ... (USB_INREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_INREGS;
		return synopsys_usb_in_ep_read(state, _addr >> 5, _addr & 0x1f);

	case USB_OUTREGS ... (USB_OUTREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_OUTREGS;
		return synopsys_usb_out_ep_read(state, _addr >> 5, _addr & 0x1f);

	case USB_FIFO_START ... USB_FIFO_END-4:
		_addr -= USB_FIFO_START;
		return *((uint32_t*)(&state->fifos[_addr]));
	}

	return 0;
}

static void synopsys_usb_in_ep_write(synopsys_usb_state *_state, int _ep, target_phys_addr_t _addr, uint32_t _val)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Wrote to disabled EP %d.\n", _ep);
		return;
	}

    switch (_addr)
	{
    case 0x00:
		_state->in_eps[_ep].control = _val;
		synopsys_usb_update_in_ep(_state, _ep);
		return;

    case 0x08:
        _state->in_eps[_ep].interrupt_status &=~ _val;
		synopsys_usb_update_irq(_state);
		return;

    case 0x10:
        _state->in_eps[_ep].tx_size = _val;
		return;

    case 0x14:
        _state->in_eps[_ep].dma_address = _val;
		return;

    case 0x1C:
        _state->in_eps[_ep].dma_buffer = _val;
		return;

    default:
        hw_error("usb_synopsys: bad ep write offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }
}

static void synopsys_usb_out_ep_write(synopsys_usb_state *_state, int _ep, target_phys_addr_t _addr, uint32_t _val)
{
	if(_ep >= USB_NUM_ENDPOINTS)
	{
		hw_error("usb_synopsys: Wrote to disabled EP %d.\n", _ep);
		return;
	}

    switch (_addr)
	{
	case 0x00:
        _state->out_eps[_ep].control = _val;
		synopsys_usb_update_out_ep(_state, _ep);
		return;

    case 0x08:
        _state->out_eps[_ep].interrupt_status &=~ _val;
		synopsys_usb_update_irq(_state);
		return;

    case 0x10:
        _state->out_eps[_ep].tx_size = _val;
		return;

    case 0x14:
        _state->out_eps[_ep].dma_address = _val;
		return;

    case 0x1C:
        _state->out_eps[_ep].dma_buffer = _val;
		return;

    default:
        hw_error("usb_synopsys: bad ep write offset 0x" TARGET_FMT_plx "\n", _addr);
		break;
    }
}

static void synopsys_usb_write(void *_arg, target_phys_addr_t _addr, uint32_t _val)
{
	synopsys_usb_state *state = _arg;

	switch(_addr)
	{
	case GRSTCTL:
		if(_val & GRSTCTL_CORESOFTRESET)
		{
			state->grstctl = GRSTCTL_CORESOFTRESET;

			// Do reset stuff
			if(state->server_host)
			{
				tcp_usb_cleanup(&state->tcp_state);
				tcp_usb_init(&state->tcp_state, synopsys_usb_tcp_callback, NULL, state);

				printf("Connecting to USB server at %s:%d...\n",
						state->server_host, state->server_port);

				int ret = tcp_usb_connect(&state->tcp_state, state->server_host, state->server_port);
				if(ret < 0)
					hw_error("Failed to connect to USB server (%d).\n", ret);

				printf("Connected to USB server.\n");
			}

			state->grstctl &= ~GRSTCTL_CORESOFTRESET;
			state->grstctl |= GRSTCTL_AHBIDLE;
			state->gintsts |= GINTMSK_RESET;
			synopsys_usb_update_irq(state);
		}
		else if(_val == 0)
			state->grstctl = _val;

		return;

	case GINTMSK:
		state->gintmsk = _val;
		synopsys_usb_update_irq(state);
		break;

	case GINTSTS:
		state->gintsts &=~ _val;
		synopsys_usb_update_irq(state);
		return;

	case DAINTMSK:
		state->daintmsk = _val;
		synopsys_usb_update_irq(state);
		return;
	
	case DAINTSTS:
		state->daintsts &=~ _val;
		synopsys_usb_update_irq(state);
		return;

	case DCTL:
		if((_val & DCTL_SGNPINNAK) != (state->dctl & DCTL_SGNPINNAK)
				&& (_val & DCTL_SGNPINNAK))
		{
			state->gintsts |= GINTMSK_GINNAKEFF;
			_val &=~ DCTL_SGNPINNAK;
		}

		if((_val & DCTL_SGOUTNAK) != (state->dctl & DCTL_SGOUTNAK)
				&& (_val & DCTL_SGOUTNAK))
		{
			state->gintsts |= GINTMSK_GOUTNAKEFF;
			_val &=~ DCTL_SGOUTNAK;
		}

		state->dctl = _val;
		synopsys_usb_update_irq(state);
		return;

	case DCFG:
		printf("USB: dcfg = 0x%08x.\n", _val);
		state->dcfg = _val;
		return;

	case GRXFSIZ:
		state->grxfsiz = _val;
		return;

	case GNPTXFSIZ:
		state->gnptxfsiz = _val;
		return;

	case DIEPTXF(1) ... DIEPTXF(USB_NUM_FIFOS+1):
		_addr -= DIEPTXF(1);
		_addr >>= 2;
		state->dptxfsiz[_addr] = _val;
		return;

	case USB_INREGS ... (USB_INREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_INREGS;
		synopsys_usb_in_ep_write(state, _addr >> 5, _addr & 0x1f, _val);
		return;

	case USB_OUTREGS ... (USB_OUTREGS + USB_EPREGS_SIZE - 4):
		_addr -= USB_OUTREGS;
		synopsys_usb_out_ep_write(state, _addr >> 5, _addr & 0x1f, _val);
		return;

	case USB_FIFO_START ... USB_FIFO_END-4:
		_addr -= USB_FIFO_START;
		*((uint32_t*)(&state->fifos[_addr])) = _val;
		return;
	}
}

static CPUReadMemoryFunc *synopsys_usb_readfn[] = {
    synopsys_usb_read,
    synopsys_usb_read,
    synopsys_usb_read
};

static CPUWriteMemoryFunc *synopsys_usb_writefn[] = {
    synopsys_usb_write,
    synopsys_usb_write,
    synopsys_usb_write
};

static void synopsys_usb_initial_reset(DeviceState *dev)
{
	synopsys_usb_state *state =
		FROM_SYSBUS(synopsys_usb_state, sysbus_from_qdev(dev));

	// Values from iPhone 2G.
	state->ghwcfg1 = 0;
	state->ghwcfg2 = 0x7a8f60d0;
	state->ghwcfg3 = 0x082000e8;
	state->ghwcfg4 = 0x01f08024;

	state->dctl = 0;
	state->dcfg = 0;
	state->dsts = 0;

	state->gintmsk = 0;
	state->gintsts = 0;
	state->daintmsk = 0;
	state->daintsts = 0;

	state->grxfsiz = 0x100;
	state->gnptxfsiz = (0x100 << 16) | 0x100;

	uint32_t counter = 0x200;
	int i;
	for(i = 0; i < USB_NUM_FIFOS; i++)
	{
		state->dptxfsiz[i] = (counter << 16) | 0x100;
		counter += 0x100;
	}

	for(i = 0; i < USB_NUM_ENDPOINTS; i++)
	{
		synopsys_usb_ep_state *in = &state->in_eps[i];
		in->control = 0;
		in->dma_address = 0;
		in->fifo = 0;
		in->tx_size = 0;

		synopsys_usb_ep_state *out = &state->out_eps[i];
		out->control = 0;
		out->dma_address = 0;
		out->fifo = 0;
		out->tx_size = 0;
	}

	synopsys_usb_update_irq(state);
}

static int synopsys_usb_init(SysBusDevice *dev)
{
	synopsys_usb_state *state =
		FROM_SYSBUS(synopsys_usb_state, dev);

	tcp_usb_init(&state->tcp_state, NULL, NULL, NULL);

    int iomemtype = cpu_register_io_memory(synopsys_usb_readfn,
                               synopsys_usb_writefn, state, DEVICE_LITTLE_ENDIAN);

    sysbus_init_mmio(dev, 0x100000, iomemtype);
    sysbus_init_irq(dev, &state->irq);

	synopsys_usb_initial_reset(&state->busdev.qdev);
	return 0;
}

static SysBusDeviceInfo synopsys_usb_info = {
    .init = synopsys_usb_init,
    .qdev.name  = DEVICE_NAME,
    .qdev.size  = sizeof(synopsys_usb_state),
    .qdev.reset = synopsys_usb_initial_reset,
    .qdev.props = (Property[]) {
		DEFINE_PROP_STRING("host", synopsys_usb_state, server_host),
		DEFINE_PROP_UINT32("port", synopsys_usb_state, server_port, 7642),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void synopsys_usb_register(void)
{
    sysbus_register_withprop(&synopsys_usb_info);
}
device_init(synopsys_usb_register);

// Helper for adding to a machine
void register_synopsys_usb(target_phys_addr_t _addr, qemu_irq _irq)
{
    DeviceState *dev = qdev_create(NULL, DEVICE_NAME);
	qdev_init_nofail(dev);

	SysBusDevice *sdev = sysbus_from_qdev(dev);
    sysbus_mmio_map(sdev, 0, _addr);
    sysbus_connect_irq(sdev, 0, _irq);
}


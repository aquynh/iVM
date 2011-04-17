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
#include "tcp_usb.h"

#define DEVICE_NAME		"tcp_usb_bus"

//#define TCP_USB_BUS_DEBUG

#ifdef TCP_USB_BUS_DEBUG
#	define debug_printf(args...) fprintf(stderr, DEVICE_NAME ": " args);
#else
#	define debug_printf(args...)
#endif


typedef struct _tcp_bus_state
{
	SysBusDevice busdev;
	
	uint32_t port;

	int closed;
	QemuThread thread;
	tcp_usb_host_state_t tcp_usb_state;

} tcp_bus_state_t;

typedef struct _tcp_passthrough_state
{
	USBDevice dev;

	tcp_bus_state_t *parent;
	tcp_usb_state_t *tcp;

	tcp_usb_header_t header;
	USBPacket *packet;
	int cancelled;
} tcp_passthrough_state_t;

static int passthrough_tcp(tcp_usb_state_t *_state, void *_arg, tcp_usb_header_t *_hdr, char *_buffer)
{
	tcp_passthrough_state_t *state = _arg;
	if(!state)
		return -EINVAL;

	if(_hdr->flags & tcp_usb_reset)
	{
		debug_printf("%s: reset.\n", __func__);
		return 0;
	}

	if(state->packet == NULL)
	{
		debug_printf("%s: no packet.\n", __func__);
		return 0;
	}

	debug_printf("%s: completing packet len = %d!\n", __func__, _hdr->length);
	state->packet->len = _hdr->length;
	usb_packet_complete(state->packet);
	state->packet = NULL;

	state->dev.addr = _hdr->addr;
	return 0;
}

static void passthrough_cancel(USBPacket *_packet, void *_arg)
{
	tcp_passthrough_state_t *state = _arg;
	state->cancelled = 1;
	state->packet = NULL;

	debug_printf("%s.\n", __func__);
}

static void passthrough_closed(tcp_usb_state_t *_state, void *_arg)
{
	debug_printf("%s.\n", __func__);

	tcp_passthrough_state_t *state = _arg;
	if(!state)
		return;

	state->tcp->callback_arg = NULL;
	state->tcp = NULL;
	tcp_usb_cleanup(_state);
	free(_state);

	if(_arg)
		usb_device_detach(&state->dev);
}

static int passthrough_init(USBDevice *dev)
{
	tcp_passthrough_state_t *state = DO_UPCAST(tcp_passthrough_state_t, dev, dev);
	state->packet = NULL;
	return 0;
}

static void passthrough_reset(USBDevice *dev)
{
	tcp_passthrough_state_t *state = (tcp_passthrough_state_t*)dev;
	if(state->tcp == NULL)
		return;

	if(tcp_usb_closed(state->tcp))
		return;

	state->packet = NULL;

	debug_printf("%s: reset!\n", __func__);

	tcp_usb_header_t *header = &state->header;
	header->addr = 0;
	header->ep = 0;
	header->flags = tcp_usb_reset;
	header->length = 0;
	tcp_usb_request(state->tcp, header, NULL);
}

static int do_token_setup(tcp_passthrough_state_t *s, USBPacket *p)
{
	debug_printf("%s.\n", __func__);

    int request, value, index;

    if (p->len != 8)
        return USB_RET_STALL;
 
    memcpy(s->dev.setup_buf, p->data, 8);
    s->dev.setup_len   = (s->dev.setup_buf[7] << 8) | s->dev.setup_buf[6];
    s->dev.setup_index = 0;

    request = (s->dev.setup_buf[0] << 8) | s->dev.setup_buf[1];
    value   = (s->dev.setup_buf[3] << 8) | s->dev.setup_buf[2];
    index   = (s->dev.setup_buf[5] << 8) | s->dev.setup_buf[4];
	
	s->header.addr = s->dev.addr;
	s->header.ep = 0;
	s->header.flags = tcp_usb_setup;
	s->header.length = 8;

	if(request == USB_REQ_SET_ADDRESS)
		s->header.flags |= tcp_usb_enumdone;

	usb_defer_packet(p, passthrough_cancel, s);
	if(tcp_usb_request(s->tcp, &s->header, (char*)p->data) < 0)
	{
		debug_printf("tcp_usb: SETUP fail'd!\n");
		return USB_RET_STALL;
	}

	return USB_RET_ASYNC;
}

static int do_token_in(tcp_passthrough_state_t *s, USBPacket *p)
{
	debug_printf("%s: %d.\n", __func__, p->devep);

	s->header.ep = p->devep | USB_DIR_IN;
	s->header.length = s->dev.setup_len;
	s->header.addr = s->dev.addr;
	s->header.flags = 0;

	usb_defer_packet(p, passthrough_cancel, s);
	if(tcp_usb_request(s->tcp, &s->header, (char*)p->data) < 0)
	{
		debug_printf("tcp_usb: IN %d fail'd.\n", p->devep);
		return USB_RET_STALL;
	}

	return USB_RET_ASYNC;
}

static int do_token_out(tcp_passthrough_state_t *s, USBPacket *p)
{
	debug_printf("%s: %d.\n", __func__, p->devep);

	s->header.ep = p->devep & 0x7f;
	s->header.length = p->len;
	s->header.addr = s->dev.addr;
	s->header.flags = 0;

	usb_defer_packet(p, passthrough_cancel, s);
	if(tcp_usb_request(s->tcp, &s->header, (char*)p->data) < 0)
	{
		debug_printf("tcp_usb: OUT %d fail'd.\n", p->devep);
		return USB_RET_STALL;
	}

	return USB_RET_ASYNC;
}

static int passthrough_packet(USBDevice *s, USBPacket *p)
{
	switch(p->pid) {
    case USB_MSG_ATTACH:
        s->state = USB_STATE_ATTACHED;
        if (s->info->handle_attach) {
            s->info->handle_attach(s);
        }
        return 0;

    case USB_MSG_DETACH:
        s->state = USB_STATE_NOTATTACHED;
        return 0;

    case USB_MSG_RESET:
        s->remote_wakeup = 0;
        s->addr = 0;
        s->state = USB_STATE_DEFAULT;
        if (s->info->handle_reset) {
            s->info->handle_reset(s);
        }
        return 0;
    }

    /* Rest of the PIDs must match our address */
    if (s->state < USB_STATE_DEFAULT || p->devaddr != s->addr)
        return USB_RET_NODEV;

	tcp_passthrough_state_t *state = DO_UPCAST(tcp_passthrough_state_t, dev, s);
	if(state->tcp == NULL)
		return USB_RET_STALL;

	if(state->packet)
		return USB_RET_NAK;

	state->packet = p;

    switch (p->pid) {
    case USB_TOKEN_SETUP:
        return do_token_setup(state, p);

    case USB_TOKEN_IN:
        return do_token_in(state, p);

    case USB_TOKEN_OUT:
        return do_token_out(state, p);
 
    default:
        return USB_RET_STALL;
    }
}

static int passthrough_data(USBDevice *dev, USBPacket *p)
{
	tcp_passthrough_state_t *state = DO_UPCAST(tcp_passthrough_state_t, dev, dev);
	if(state->tcp == NULL)
		return USB_RET_STALL;

	int ret = USB_RET_STALL;
	tcp_usb_header_t *header = &state->header;
	switch(p->pid)
	{
	case USB_TOKEN_OUT:
		header->ep = p->devep & 0x7f;
		header->flags = 0;
		header->addr = dev->addr;
		header->length = p->len;
		
		usb_defer_packet(p, passthrough_cancel, state);
		ret = tcp_usb_request(state->tcp, header, (char*)p->data);
		if(ret < 0)
			return USB_RET_NAK;

		debug_printf("Host USB: OUT token on %02x -> 0x%08x.\n", p->devep, ret);
		return USB_RET_ASYNC;

	case USB_TOKEN_IN:
		header->ep = p->devep | USB_DIR_IN;
		header->flags = 0;
		header->addr = dev->addr;
		header->length = p->len;

		usb_defer_packet(p, passthrough_cancel, state);
		ret = tcp_usb_request(state->tcp, header, (char*)p->data);
		if(ret < 0)
			return USB_RET_NAK;

		debug_printf("Host USB: IN token on %02x -> 0x%08x.\n", p->devep, ret);
		return USB_RET_ASYNC;
	}

	return USB_RET_STALL;
}

static void passthrough_destroy(USBDevice *dev)
{
}

static struct USBDeviceInfo tcp_passthrough_info = {
        .product_desc   = "USB Passthrough Device",
        .qdev.name      = "tcp_usb_passthrough",
        .usbdevice_name = "passthrough",
        .qdev.size      = sizeof(tcp_passthrough_state_t),
        .init           = passthrough_init,
        .handle_packet  = passthrough_packet,
		.handle_data    = passthrough_data,
        .handle_reset   = passthrough_reset,
        .handle_destroy = passthrough_destroy,
};

static void *tcp_bus_thread(void *_arg)
{
	tcp_bus_state_t *state = _arg;

	while(state->closed == 0)
	{
		tcp_usb_state_t *newState = malloc(sizeof(*newState));
		tcp_usb_init(newState, passthrough_tcp, passthrough_closed, NULL);

		if(tcp_usb_accept(&state->tcp_usb_state, newState) < 0)
		{
			fprintf(stderr, "%s: Failed to accept socket.\n", __func__);

			tcp_usb_cleanup(newState);
			free(newState);
			continue;
		}

		USBBus *bus = usb_bus_find(-1);
		if(bus == NULL)
		{
			fprintf(stderr, "%s: No bus to attach networked device to!\n", __func__);
			tcp_usb_cleanup(newState);
			free(newState);
			state->closed = 1;
			return NULL;
		}

		tcp_passthrough_state_t *dev = 
			(tcp_passthrough_state_t*)usb_create(bus, "tcp_usb_passthrough");
		dev->parent = state;
		dev->tcp = newState;
		newState->callback_arg = dev;
		qdev_init_nofail(&dev->dev.qdev);

		usb_device_detach(&dev->dev);
		usb_device_attach(&dev->dev);
	}

	return NULL;
}

static void tcp_bus_reset(DeviceState *dev)
{
}

static int tcp_bus_init(SysBusDevice *dev)
{
	tcp_bus_state_t *state = FROM_SYSBUS(tcp_bus_state_t, dev);
	
	state->closed = 0;
	tcp_usb_host_init(&state->tcp_usb_state);

	if(tcp_usb_host(&state->tcp_usb_state, state->port) < 0)
		hw_error("Failed to bind USB server socket.\n");

	printf("TCP USB server started on port %d!\n", state->port);
	qemu_thread_create(&state->thread, tcp_bus_thread, state);
	return 0;
}

static SysBusDeviceInfo tcp_bus_info = {
    .init = tcp_bus_init,
    .qdev.name  = DEVICE_NAME,
    .qdev.size  = sizeof(tcp_bus_state_t),
    .qdev.reset = tcp_bus_reset,
    .qdev.props = (Property[]) {
		DEFINE_PROP_UINT32("port", tcp_bus_state_t, port, 7642),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void tcp_bus_register(void)
{
    sysbus_register_withprop(&tcp_bus_info);
	usb_qdev_register(&tcp_passthrough_info);
}
device_init(tcp_bus_register);

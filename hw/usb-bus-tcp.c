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

#define DEVICE_NAME		"usb_tcp_bus"
#define NUM_PORTS		5

typedef struct _tcp_bus_state
{
	SysBusDevice busdev;
	
	uint32_t port;

	int closed;
	pthread_t thread;
	tcp_usb_host_state_t tcp_usb_state;
	QLIST_HEAD(, tcp_usb_state_t) children;

} tcp_bus_state_t;

typedef struct _tcp_passthrough_state
{
	USBDevice dev;

	tcp_bus_state_t *parent;
	tcp_usb_state_t *tcp;
} tcp_passthrough_state_t;

static int passthrough_init(USBDevice *dev)
{
	return 0;
}

static void passthrough_reset(USBDevice *dev)
{
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

	tcp_passthrough_state_t *state = (tcp_passthrough_state_t*)s;
	if(state->tcp->closed)
	{
		usb_device_detach(s);
		tcp_usb_cleanup(state->tcp);
		state->tcp = NULL;
		return USB_RET_STALL;
	}

	uint8_t ep = p->devep & 0x3f;
	size_t sz = p->len;

	if(tcp_usb_okay(state->tcp))
		return USB_RET_STALL;

	switch(p->pid)
	{
	case USB_TOKEN_SETUP:
		tcp_usb_send(state->tcp, ep, (char*)p->data, sz, NULL, NULL);
		printf("Host USB setup %d.\n", sz);
		return sz;

	case USB_TOKEN_OUT:
		tcp_usb_send(state->tcp, ep, (char*)p->data, sz, NULL, NULL);
		printf("Host USB sent %d.\n", sz);
		return sz;

	case USB_TOKEN_IN:
		if((tcp_usb_state(state->tcp) & (1 << ep)) == 0)
			return USB_RET_NAK;

		printf("Host USB: recv on EP %d (%d)...\n", ep, sz);
		tcp_usb_recv_sync(state->tcp, ep, (char*)p->data, &sz);
		if(sz == 0xFFFF)
			return USB_RET_STALL;

		printf("Host USB recv'd %d.\n", sz);
		return sz;
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
        .handle_reset   = passthrough_reset,
        .handle_destroy = passthrough_destroy,
};

static void *tcp_bus_thread(void *_arg)
{
	tcp_bus_state_t *state = _arg;
	int fails = 0;

	while(state->closed == 0)
	{
		tcp_usb_state_t *newState = malloc(sizeof(*newState));
		tcp_usb_init(newState);

		while(tcp_usb_accept(&state->tcp_usb_state, newState) < 0)
		{
			fails++;
			if(fails > 25)
			{
				printf("USB: Connection failure!\n");
				tcp_usb_cleanup(newState);
				free(newState);
				state->closed = 1;
				return NULL;
			}
		}

		fails = 0;

		USBBus *bus = usb_bus_find(-1);
		if(bus == NULL)
		{
			printf("USB: No bus to attach networked device to!\n");
			tcp_usb_cleanup(newState);
			free(newState);
			state->closed = 1;
			return NULL;
		}

		tcp_passthrough_state_t *dev = 
			(tcp_passthrough_state_t*)usb_create(bus, "tcp_usb_passthrough");
		dev->parent = state;
		dev->tcp = newState;
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
	QLIST_INIT(&state->children);
	tcp_usb_host_init(&state->tcp_usb_state);

	if(tcp_usb_host(&state->tcp_usb_state, state->port) < 0)
		hw_error("Failed to bind USB server socket.\n");

	printf("TCP USB server started!\n");
	pthread_create(&state->thread, NULL, tcp_bus_thread, state);
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

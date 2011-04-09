#ifndef  HW_TCP_USB
#define  HW_TCP_USB

/*
 * USB over TCP driver.
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

#include <pthread.h>

struct _tcp_usb_state;
typedef void (*tcp_usb_callback_t)(struct _tcp_usb_state *_status, uint8_t _ep, size_t _amt, void *_arg);

#define TCP_USB_NUM_EP 16

typedef struct _tcp_usb_message
{
	char *data;
	size_t size;
	tcp_usb_callback_t callback;
	void *arg;

	pthread_mutex_t mutex;
	pthread_cond_t avail;
	pthread_cond_t sync;
} tcp_usb_message_t;

typedef struct _tcp_usb_state
{
	int socket;
	pthread_t thread;
	pthread_mutex_t write_mutex;
	int closed;

	uint32_t status;
	uint32_t remote_status;

	tcp_usb_message_t queued_writes[TCP_USB_NUM_EP];
	tcp_usb_message_t queued_reads[TCP_USB_NUM_EP];

} tcp_usb_state_t;

void tcp_usb_init(tcp_usb_state_t *_state);
void tcp_usb_cleanup(tcp_usb_state_t *_state);

int tcp_usb_okay(tcp_usb_state_t *_state);

int tcp_usb_connect(tcp_usb_state_t *_state, char *_host, uint32_t _port);

uint32_t tcp_usb_state(tcp_usb_state_t *_state);

int tcp_usb_recv(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t _amt, tcp_usb_callback_t _cb, void *_arg);
int tcp_usb_send(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t _amt, tcp_usb_callback_t _cb, void *_arg);

int tcp_usb_recv_sync(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t *_amt);
int tcp_usb_send_sync(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t *_amt);

typedef struct _tcp_usb_host_state
{
	int socket;
	pthread_t thread;

} tcp_usb_host_state_t;

void tcp_usb_host_init(tcp_usb_host_state_t *_state);
void tcp_usb_host_cleanup(tcp_usb_host_state_t *_state);

int tcp_usb_host_okay(tcp_usb_host_state_t *_state);

int tcp_usb_host(tcp_usb_host_state_t *_state, uint32_t _port);
int tcp_usb_accept(tcp_usb_host_state_t *_host, tcp_usb_state_t *_client);

#endif //HW_TCP_USB

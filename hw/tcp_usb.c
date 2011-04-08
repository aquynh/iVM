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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "sysbus.h"
#include "qemu-common.h"
#include "qemu-timer.h"
#include "net.h"
#include "irq.h"
#include "hw.h"
#include "tcp_usb.h"

enum
{
	packet_status,
	packet_data,
	packet_request,
};

typedef struct _tcp_usb_header
{
	uint8_t type;
	uint8_t ep;
	uint16_t length;

} __attribute__((packed)) tcp_usb_header_t;

void tcp_usb_init(tcp_usb_state_t *_state)
{
	_state->socket = 0;
	_state->thread = 0;
	_state->closed = 0;

	_state->status = 0;
	_state->remote_status = 0;

	memset(_state->queued_writes, 0, sizeof(_state->queued_writes));
	memset(_state->queued_reads, 0, sizeof(_state->queued_reads));
}

void tcp_usb_cleanup(tcp_usb_state_t *_state)
{
	_state->closed = 1;

	if(_state->thread)
		pthread_cancel(_state->thread);

	if(_state->socket)
		close(_state->socket);
}

int tcp_usb_okay(tcp_usb_state_t *_state)
{
	return _state->closed;
}

static int read_block(int _sock, void *_data, size_t _amt)
{
	char *ptr = _data;
	int ret = read(_sock, ptr, _amt);
	if(ret <= 0)
		return ret;

	int done = ret;
	_amt -= done;

	while(done < _amt)
	{
		ret = read(_sock, ptr, _amt);
		if(ret <= 0)
			return done;
		
		done += ret;
		_amt -= ret;
	}

	return done;
}

static void *tcp_usb_thread(void *_arg)
{
	tcp_usb_state_t *state = _arg;

	while(state->closed == 0)
	{
		tcp_usb_header_t header;
		int ret = read_block(state->socket, &header, sizeof(header));
		if(ret <= 0)
		{
			printf("USB: Socket error %d.\n", ret);
			state->closed = 1;
			return NULL;
		}

		printf("USB: Got packet (%d): %02x %02x %04x.\n",
				ret, header.type, header.ep, header.length);

		tcp_usb_message_t *msg;
		switch(header.type)
		{
		case packet_status:
			read(state->socket, &state->remote_status,
					sizeof(state->remote_status));

			printf("USB: Status updated 0x%08x.\n", state->remote_status);
			break;

		case packet_request:
			msg = &state->queued_writes[header.ep];

			state->status &=~ (1 << header.ep);

			tcp_usb_header_t shdr;
			shdr.type = packet_data;
			shdr.ep = header.ep;
			shdr.length = msg->size;

			write(state->socket, &shdr, sizeof(shdr));
			write(state->socket, msg->data, msg->size);

			msg->callback(state, header.ep, msg->size, msg->arg);
			msg->data = NULL;
			break;

		case packet_data:
			msg = &state->queued_reads[header.ep];

			read(state->socket, msg->data, msg->size);
			msg->callback(state, header.ep, header.length, msg->arg);
			msg->data = NULL;
			break;
		}
	}

	return NULL;
}

static void tcp_usb_send_status(tcp_usb_state_t *_state)
{
	tcp_usb_header_t hdr;
	hdr.type = packet_status;
	hdr.ep = 0;
	hdr.length = 0;

	write(_state->socket, &hdr, sizeof(hdr));
	write(_state->socket, &_state->status, sizeof(_state->status));
}

int tcp_usb_connect(tcp_usb_state_t *_state, char *_host, uint32_t _port)
{
	_state->socket = socket(AF_INET, SOCK_STREAM, 0);
	
	struct hostent *hostname = gethostbyname(_host);

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(_port);
	memcpy(&server_addr.sin_addr.s_addr,
			hostname->h_addr, hostname->h_length);

	if(connect(_state->socket, &server_addr, sizeof(server_addr)))
		return -EIO;

	tcp_usb_send_status(_state);

	pthread_create(&_state->thread, NULL, tcp_usb_thread, _state);

	return 0;
}

uint32_t tcp_usb_state(tcp_usb_state_t *_state)
{
	return _state->remote_status;
}

int tcp_usb_recv(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t _amt, tcp_usb_callback_t _cb, void *_arg)
{
	if((_state->remote_status & (1 << _ep)) == 0)
		return -ENOENT;

	tcp_usb_message_t *msg = &_state->queued_reads[_ep];
	msg->data = (char*)_data;
	msg->size = _amt;
	msg->callback = _cb;
	msg->arg = _arg;

	tcp_usb_header_t hdr;
	hdr.type = packet_request;
	hdr.ep = _ep;
	hdr.length = 0;
	write(_state->socket, &hdr, sizeof(hdr));
	return 0;
}

int tcp_usb_send(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t _amt, tcp_usb_callback_t _cb, void *_arg)
{
	tcp_usb_message_t *msg = &_state->queued_writes[_ep];
	msg->data = (char*)_data;
	msg->size = _amt;
	msg->callback = _cb;
	msg->arg = _arg;

	_state->status |= (1 << _ep);

	tcp_usb_send_status(_state);
	return 0;
}

void tcp_usb_host_init(tcp_usb_host_state_t *_state)
{
	_state->socket = 0;
	_state->thread = 0;
}

void tcp_usb_host_cleanup(tcp_usb_host_state_t *_state)
{
	if(_state->thread)
		pthread_cancel(_state->thread);

	if(_state->socket)
		close(_state->socket);
}

int tcp_usb_host_okay(tcp_usb_host_state_t *_state)
{
	return 0;
}

int tcp_usb_host(tcp_usb_host_state_t *_state, uint32_t _port)
{
	_state->socket = socket(AF_INET, SOCK_STREAM, 0);
	if(!_state->socket)
		return -EIO;

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(_port);
	server_addr.sin_addr.s_addr = INADDR_ANY;

	if(bind(_state->socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
		return -EIO;

	listen(_state->socket, 5);
	return 0;
}

int tcp_usb_accept(tcp_usb_host_state_t *_host, tcp_usb_state_t *_client)
{
	struct sockaddr_in addr;
	size_t addr_sz;

	printf("USB: waiting on accept...\n");

	_client->socket = accept(_host->socket, &addr, &addr_sz);
	if(_client->socket == 0)
		return -EIO;

	printf("USB: USB device accepted!\n");

	tcp_usb_send_status(_client);
	pthread_create(&_client->thread, NULL, tcp_usb_thread, _client);
	return 0;
}

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
	pthread_mutex_init(&_state->write_mutex, NULL);
	_state->closed = 0;

	_state->status = 0;
	_state->remote_status = 0;

	memset(_state->queued_writes, 0, sizeof(_state->queued_writes));
	memset(_state->queued_reads, 0, sizeof(_state->queued_reads));

	int i;
	for(i = 0; i < TCP_USB_NUM_EP; i++)
	{
		pthread_mutex_init(&_state->queued_writes[i].mutex, NULL);
		pthread_mutex_init(&_state->queued_reads[i].mutex, NULL);

		pthread_cond_init(&_state->queued_writes[i].avail, NULL);
		pthread_cond_init(&_state->queued_reads[i].avail, NULL);

		pthread_cond_init(&_state->queued_writes[i].sync, NULL);
		pthread_cond_init(&_state->queued_reads[i].sync, NULL);
	}
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

		//printf("USB: Got packet (%d): %02x %02x %04x.\n",
		//		ret, header.type, header.ep, header.length);

		int i;
		tcp_usb_message_t *msg;
		switch(header.type)
		{
		case packet_status:
			read_block(state->socket, &state->remote_status,
					sizeof(state->remote_status));

			for(i = 0; i < TCP_USB_NUM_EP; i++)
			{
				if(state->remote_status & (1 << i))
				{
					msg = &state->queued_reads[i];

					if(msg->data)
					{
						printf("%s: Data available!\n", __func__);

						tcp_usb_header_t hdr;
						hdr.type = packet_request;
						hdr.ep = i;
						hdr.length = 0;

						pthread_mutex_lock(&state->write_mutex);
						write(state->socket, &hdr, sizeof(hdr));
						pthread_mutex_unlock(&state->write_mutex);
					}
					else
					{
						pthread_mutex_lock(&msg->mutex);
						pthread_cond_signal(&msg->avail);
						pthread_mutex_unlock(&msg->mutex);
					}
				}
			}

			printf("USB: Status updated 0x%08x.\n", state->remote_status);
			break;

		case packet_request:
			msg = &state->queued_writes[header.ep];

			state->status &=~ (1 << header.ep);
			pthread_mutex_lock(&msg->mutex);
			pthread_cond_signal(&msg->avail);
			pthread_mutex_unlock(&msg->mutex);

			//printf("USB: Write on EP %d (%d).\n", header.ep, msg->size);
			
			if(msg->size != 0xFFFF)
			{
				printf("Send:");
				for(i = 0; i < msg->size; i++)
				{
					if((i%8) == 0)
						printf("\t");
					printf("%02x ", msg->data[i] & 0xFF);
					if((i%8) == 7)
						printf("\n");
				}

				if(!i || (i%8) != 0)
					printf("\n");
			}

			tcp_usb_header_t shdr;
			shdr.type = packet_data;
			shdr.ep = header.ep;
			shdr.length = msg->size;

			pthread_mutex_lock(&state->write_mutex);
			write(state->socket, &shdr, sizeof(shdr));

			if(shdr.length && shdr.length != 0xFFFF)
				write(state->socket, msg->data, shdr.length);
			pthread_mutex_unlock(&state->write_mutex);

			if(msg->callback)
				msg->callback(state, header.ep, msg->size, msg->arg);

			msg->data = NULL;
			break;

		case packet_data:
			msg = &state->queued_reads[header.ep];

			state->remote_status &=~ (1 << header.ep);

			//printf("USB: Read on EP %d (%d).\n", header.ep, header.length);

			if(msg->size < header.length)
				printf("USB: warning overflow.\n");

			if(header.length && header.length != 0xFFFF)
			{
				ret = read_block(state->socket, msg->data, header.length);
				if(ret < 0)
					printf("USB: Second read failed with %d.\n", ret);
			}

			if(header.length != 0xFFFF)
			{
				printf("Recv:");
				for(i = 0; i < header.length; i++)
				{
					if((i%8) == 0)
						printf("\t");
					printf("%02x ", msg->data[i] & 0xFF);
					if((i%8) == 7)
						printf("\n");
				}

				if(!i || (i%8) != 0)
					printf("\n");
			}

			if(msg->callback)
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

	printf("%s: Sending status 0x%08x.\n", __func__, _state->status);

	pthread_mutex_lock(&_state->write_mutex);
	write(_state->socket, &hdr, sizeof(hdr));
	write(_state->socket, &_state->status, sizeof(_state->status));
	pthread_mutex_unlock(&_state->write_mutex);

	printf("%s: done.\n", __func__);
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
	printf("%s.\n", __func__);

	tcp_usb_message_t *msg = &_state->queued_reads[_ep];

	msg->data = (char*)_data;
	msg->size = _amt;
	msg->callback = _cb;
	msg->arg = _arg;

	if((tcp_usb_state(_state) & (1 << _ep)) != 0)
	{
		printf("%s: Data available!\n", __func__);

		tcp_usb_header_t hdr;
		hdr.type = packet_request;
		hdr.ep = _ep;
		hdr.length = 0;

		pthread_mutex_lock(&_state->write_mutex);
		write(_state->socket, &hdr, sizeof(hdr));
		pthread_mutex_unlock(&_state->write_mutex);
	}

	printf("%s exit.\n", __func__);

	return 0;
}

int tcp_usb_send(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t _amt, tcp_usb_callback_t _cb, void *_arg)
{
	if(_state->status & (1 << _ep))
		return -EBUSY;

	tcp_usb_message_t *msg = &_state->queued_writes[_ep];
	msg->data = (char*)_data;
	msg->size = _amt;
	msg->callback = _cb;
	msg->arg = _arg;

	_state->status |= (1 << _ep);

	tcp_usb_send_status(_state);
	return 0;
}

static void tcp_usb_recv_complete(tcp_usb_state_t *_state, uint8_t _ep, size_t _amt, void *_arg)
{
	printf("%s.\n", __func__);

	tcp_usb_message_t *msg = &_state->queued_reads[_ep];
	pthread_mutex_lock(&msg->mutex);
	pthread_cond_signal(&msg->sync);
	*((size_t*)_arg) = _amt;
	pthread_mutex_unlock(&msg->mutex);

	printf("%s done.\n", __func__);
}

int tcp_usb_recv_sync(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t *_amt)
{
	printf("%s.\n", __func__);

	tcp_usb_message_t *msg = &_state->queued_reads[_ep];
	pthread_mutex_lock(&msg->mutex);

	if((tcp_usb_state(_state) & (1 << _ep)) == 0)
		pthread_cond_wait(&msg->avail, &msg->mutex);

	int ret = tcp_usb_recv(_state, _ep, _data, *_amt, tcp_usb_recv_complete, _amt);
	pthread_cond_wait(&msg->sync, &msg->mutex);
	pthread_mutex_unlock(&msg->mutex);

	printf("%s done.\n", __func__);

	return ret;
}

static void tcp_usb_send_complete(tcp_usb_state_t *_state, uint8_t _ep, size_t _amt, void *_arg)
{
	tcp_usb_message_t *msg = &_state->queued_writes[_ep];
	pthread_mutex_lock(&msg->mutex);
	pthread_cond_wait(&msg->sync, &msg->mutex);
	*((size_t*)_arg) = _amt;
	pthread_mutex_unlock(&msg->mutex);
}

int tcp_usb_send_sync(tcp_usb_state_t *_state, uint8_t _ep, const char *_data, size_t *_amt)
{
	tcp_usb_message_t *msg = &_state->queued_writes[_ep];
	pthread_mutex_lock(&msg->mutex);

	if(_state->status & (1 << _ep))
		pthread_cond_wait(&msg->avail, &msg->mutex);

	int ret = tcp_usb_send(_state, _ep, _data, *_amt, tcp_usb_send_complete, _amt);
	pthread_cond_wait(&msg->sync, &msg->mutex);
	pthread_mutex_unlock(&msg->mutex);
	return ret;
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
	
	struct linger linger;
	linger.l_onoff = 0;
	linger.l_linger = 0;
	setsockopt(_state->socket, SOL_SOCKET, SO_LINGER, (char *)&linger, sizeof(linger));

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
	size_t addr_sz = sizeof(addr);

	printf("USB: waiting on accept...\n");

	// TODO: This is returning -1 a lot, probably due to
	// the magic in QEMU's internals, there should be a better
	// fix than this.
	while((_client->socket = accept(_host->socket, &addr, &addr_sz)) == -1);
	if(_client->socket <= 0)
	{
		printf("USB: accept error %d.\n", errno);
		return -EIO;
	}

	printf("USB: USB device accepted!\n");

	tcp_usb_send_status(_client);
	pthread_create(&_client->thread, NULL, tcp_usb_thread, _client);
	return 0;
}

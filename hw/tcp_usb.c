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
#include "usb.h"
#include "tcp_usb.h"

void tcp_usb_init(tcp_usb_state_t *_state, tcp_usb_callback_t _cb, void *_arg)
{
	_state->callback = _cb;
	_state->callback_arg = _arg;
	_state->socket = 0;
	_state->closed = 1;
}

void tcp_usb_cleanup(tcp_usb_state_t *_state)
{
	if(!_state->closed)
		_state->closed = 1;

	if(_state->socket)
		close(_state->socket);
}

int tcp_usb_closed(tcp_usb_state_t *_state)
{
	return _state->closed != 0;
}

static int read_block(int _sock, void *_data, size_t _amt)
{
	char *ptr = _data;
	int ret = read(_sock, ptr, _amt);
	if(ret <= 0)
		return ret;

	int done = ret;

	while(done < _amt)
	{
		ret = read(_sock, ptr+done, _amt-done);
		if(ret <= 0)
			return done;
		
		done += ret;
	}

	return done;
}

static int write_block(int _sock, void *_data, size_t _amt)
{
	char *ptr = _data;
	int ret = write(_sock, _data, _amt);
	if(ret <= 0)
		return ret;

	int done = ret;

	while(done < _amt)
	{
		ret = write(_sock, ptr+done, _amt-done);
		if(ret <= 0)
			return ret;

		done += ret;
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

		char *data = NULL;
		if(header.length > 0)
		{
			data = malloc(header.length);
			if((header.ep & USB_DIR_IN) == 0) // OUT
			{
				ret = read_block(state->socket, data, header.length);
				if(ret <= 0)
				{
					printf("USB: Socket error %d, during data read.\n", ret);
					state->closed = 1;
					return NULL;
				}
			}
		}

		int16_t len = USB_RET_STALL;
		if(state->callback)
			len = state->callback(state, state->callback_arg, &header, data);

		header.length = len;
		ret = write_block(state->socket, &header, sizeof(header));
		if(ret <= 0)
		{
			printf("USB: Socket error %d, during header write.\n", ret);
			state->closed = 1;
			return NULL;
		}

		if((header.ep & USB_DIR_IN) != 0
				&& len > 0) // IN
		{
			ret = write_block(state->socket, data, len);
			if(ret <= 0)
			{
				printf("USB: Socket error %d, during data write.\n", ret);
				state->closed = 1;
				return NULL;
			}
		}

		if(data)
			free(data);
	}

	state->closed = 1;
	return NULL;
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

	_state->closed = 0;
	qemu_thread_create(&_state->thread, tcp_usb_thread, _state);

	return 0;
}

int tcp_usb_request(tcp_usb_state_t *_state, tcp_usb_header_t *_header, const char *_data)
{
	//printf("%s.\n", __func__);

	int ret = write_block(_state->socket, _header, sizeof(*_header));
	if(ret <= 0)
	{
		printf("%s: header write failed.\n", __func__);
		return ret;
	}

	if(_header->length > 0 && (_header->ep & USB_DIR_IN) == 0) // OUT
	{
		ret = write_block(_state->socket, (char*)_data, _header->length);
		if(ret <= 0)
		{
			printf("%s: data write failed.\n", __func__);
			return ret;
		}
	}

	do
	{
		ret = read_block(_state->socket, _header, sizeof(*_header));
	}
	while(ret == -1);

	if(ret <= 0)
	{
		printf("%s: header read failed.\n", __func__);
		return ret;
	}

	if(_header->length > 0 && (_header->ep & USB_DIR_IN) != 0) // IN
	{
		do
		{
			ret = read(_state->socket, (char*)_data, _header->length);
		}
		while(ret == -1);

		if(ret <= 0)
		{
			printf("%s: data read failed.\n", __func__);
			return ret;
		}
	}

	//printf("%s exit.\n", __func__);

	return _header->length;
}

void tcp_usb_host_init(tcp_usb_host_state_t *_state)
{
	_state->socket = 0;
}

void tcp_usb_host_cleanup(tcp_usb_host_state_t *_state)
{
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

	_client->socket = accept(_host->socket, &addr, &addr_sz);
	if(_client->socket <= 0)
	{
		printf("USB: accept error %d.\n", errno);
		return -EIO;
	}

	_client->closed = 0;
	printf("USB: USB device accepted!\n");
	return 0;
}

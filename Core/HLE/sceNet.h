// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "Core/HLE/proAdhoc.h"

// Option Names
#define PSP_SO_REUSEPORT		0x0200
#define PSP_SO_NBIO				0x1009

// Infrastructure Errno Numbers
#define INET_EAGAIN			0x0B
#define INET_ETIMEDOUT		0x74
#define INET_EINPROGRESS	0x77
#define INET_EISCONN		0x7F

// On-Demand Nonblocking Flag
#define INET_MSG_DONTWAIT	0x80

// Event Flags
#define INET_POLLRDNORM		0x0040
#define INET_POLLWRNORM		0x0004

// TODO: Determine how many handlers we can actually have
const size_t MAX_APCTL_HANDLERS = 32;

enum {
	ERROR_NET_BUFFER_TOO_SMALL				= 0x80400706,

	ERROR_NET_INET_ALREADY_INITIALIZED		= 0x80410201,

	ERROR_NET_RESOLVER_BAD_ID				= 0x80410408,
	ERROR_NET_RESOLVER_ALREADY_STOPPED		= 0x8041040a,
	ERROR_NET_RESOLVER_INVALID_HOST			= 0x80410414,

	ERROR_NET_APCTL_ALREADY_INITIALIZED		= 0x80410a01,
};

enum {
	PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST	= 5,
};

// Sockaddr
typedef struct SceNetInetSockaddr {
	uint8_t sa_len;
	uint8_t sa_family;
	uint8_t sa_data[14];
} SceNetInetSockaddr;

// Sockaddr_in
typedef struct SceNetInetSockaddrIn {
	uint8_t sin_len;
	uint8_t sin_family;
	u16_le sin_port; //uint16_t
	u32_le sin_addr; //uint32_t
	uint8_t sin_zero[8];
} SceNetInetSockaddrIn;

// Polling Event Field
typedef struct SceNetInetPollfd { //similar format to pollfd in 32bit (pollfd in 64bit have different size)
	s32_le fd;
	s16_le events;
	s16_le revents;
} SceNetInetPollfd;

struct ProductStruct {
	s32_le unknown; // Unknown, set to 0 // Product Type ?
	char product[PRODUCT_CODE_LENGTH]; // Game ID (Example: ULUS10000)
};

struct ApctlHandler {
	u32 entryPoint;
	u32 argument;
};

class PointerWrap;

void Register_sceNet();
void Register_sceWlanDrv();
void Register_sceNetUpnp();

void __NetInit();
void __NetShutdown();
void __NetDoState(PointerWrap &p);

int sceNetInetPoll(void *fds, u32 nfds, int timeout);
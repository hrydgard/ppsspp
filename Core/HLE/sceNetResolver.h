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
#include <memory>
#include <shared_mutex>

enum {
    // pspnet_resolver
    ERROR_NET_RESOLVER_NOT_TERMINATED		= 0x80410401,
    ERROR_NET_RESOLVER_NO_DNS_SERVER		= 0x80410402,
    ERROR_NET_RESOLVER_INVALID_PTR			= 0x80410403,
    ERROR_NET_RESOLVER_INVALID_BUFLEN		= 0x80410404,
    ERROR_NET_RESOLVER_INVALID_ID			= 0x80410405,
    ERROR_NET_RESOLVER_ID_MAX				= 0x80410406,
    ERROR_NET_RESOLVER_NO_MEM				= 0x80410407,
    ERROR_NET_RESOLVER_BAD_ID				= 0x80410408, // ERROR_NET_RESOLVER_ID_NOT_FOUND
    ERROR_NET_RESOLVER_CTX_BUSY				= 0x80410409,
    ERROR_NET_RESOLVER_ALREADY_STOPPED		= 0x8041040a,
    ERROR_NET_RESOLVER_NOT_SUPPORTED		= 0x8041040b,
    ERROR_NET_RESOLVER_BUF_NO_SPACE			= 0x8041040c,
    ERROR_NET_RESOLVER_INVALID_PACKET		= 0x8041040d,
    ERROR_NET_RESOLVER_STOPPED				= 0x8041040e,
    ERROR_NET_RESOLVER_SOCKET				= 0x8041040f,
    ERROR_NET_RESOLVER_TIMEOUT				= 0x80410410,
    ERROR_NET_RESOLVER_NO_RECORD			= 0x80410411,
    ERROR_NET_RESOLVER_RES_PACKET_FORMAT	= 0x80410412,
    ERROR_NET_RESOLVER_RES_SERVER_FAILURE	= 0x80410413,
    ERROR_NET_RESOLVER_INVALID_HOST			= 0x80410414, // ERROR_NET_RESOLVER_NO_HOST
    ERROR_NET_RESOLVER_RES_NOT_IMPLEMENTED	= 0x80410415,
    ERROR_NET_RESOLVER_RES_SERVER_REFUSED	= 0x80410416,
    ERROR_NET_RESOLVER_INTERNAL				= 0x80410417,
};

void __NetResolverShutdown();

void Register_sceNetResolver();

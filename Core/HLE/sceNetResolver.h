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

class SceNetResolver;

class NetResolver {
public:
	NetResolver(const NetResolver& other) = default;

	NetResolver() :
		mId(0),
		mIsRunning(false),
		mBufferAddr(0),
		mBufferLen(0) {
	}

	NetResolver(const int id, const u32 bufferAddr, const int bufferLen) :
		mId(id),
		mIsRunning(false),
		mBufferAddr(bufferAddr),
		mBufferLen(bufferLen) {
	}

	int GetId() const { return mId; }

	bool GetIsRunning() const { return mIsRunning; }

	void SetIsRunning(const bool isRunning) { this->mIsRunning = isRunning; }

private:
	int mId;
	bool mIsRunning;
	u32 mBufferAddr;
	u32 mBufferLen;
};

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

class SceNetResolver {
public:
    static void Init();
    static void Shutdown();
    static std::shared_ptr<SceNetResolver> Get();

    std::shared_ptr<NetResolver> GetNetResolver(u32 resolverId);
    std::shared_ptr<NetResolver> CreateNetResolver(u32 bufferPtr, u32 bufferLen);
    bool TerminateNetResolver(u32 resolverId);
    bool DeleteNetResolver(u32 resolverId);
    void ClearNetResolvers();

private:
    static std::shared_ptr<SceNetResolver> gInstance;
    static std::shared_mutex gLock;

    int mCurrentNetResolverId = 1;
    std::unordered_map<u32, std::shared_ptr<NetResolver>> mNetResolvers;
    std::shared_mutex mNetResolversLock;
};

void Register_sceNetResolver();

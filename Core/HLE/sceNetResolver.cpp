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

#if __linux__ || __APPLE__ || defined(__OpenBSD__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#endif

#include "Common/Net/Resolve.h"
#include "Common/Data/Text/Parsers.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PortManager.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMutex.h"
#include "sceUtility.h"

#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNetResolver.h"

#include <iostream>
#include <mutex>

#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNp.h"
#include "Core/Reporting.h"
#include "Core/Instance.h"

struct NetResolver {
	int id = 0;
	bool isRunning = false;
	u32 bufferAddr = 0;
	u32 bufferLen = 0;
};

static int g_currentNetResolverId = 1;
static std::unordered_map<u32, NetResolver> g_netResolvers;

// NOTE: It starts as true, needed for Outrun 2006.
static bool g_netResolverInitialized = true;

static int sceNetResolverInit() {
	// Hardcoded mHostToAlias entries here have been moved to the infra-dns.json file.
	g_netResolverInitialized = true;
	return hleLogInfo(Log::sceNet, 0);
}

void __NetResolverShutdown() {
	g_netResolvers.clear();
	g_netResolverInitialized = false;
}

static int sceNetResolverTerm() {
	__NetResolverShutdown();
	return hleLogInfo(Log::sceNet, 0);
}

// TODO: Add DoState, store the ID gen and active resolvers.

// Note: timeouts are in seconds
static int NetResolver_StartNtoA(NetResolver *resolver, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
	std::string hostname = std::string(safe_string(Memory::GetCharPointer(hostnamePtr)));
	
	// Process hostname with infra-DNS
	std::string processedHostname = net::ProcessHostnameWithInfraDNS(hostname);

	// Flag resolver as in-progress - not relevant for sync functions but potentially relevant for async
	resolver->isRunning = true;
	addrinfo *resolved = nullptr;
	std::string err;
	if (!net::DNSResolve(processedHostname, "", &resolved, err)) {
		ERROR_LOG(Log::sceNet, "OS DNS Error Resolving %s (%s)", processedHostname.c_str(), err.c_str());
		resolver->isRunning = false;
		return SCE_NET_RESOLVER_ERROR_INVALID_HOST;
	}

	// Process results
	SockAddrIN4 addr{};
	if (resolved) {
		for (auto ptr = resolved; ptr != nullptr; ptr = ptr->ai_next) {
			switch (ptr->ai_family) {
			case AF_INET:
				addr.in = *(sockaddr_in *)ptr->ai_addr;
				break;
			}
		}
		net::DNSResolveFree(resolved);
		Memory::Write_U32(addr.in.sin_addr.s_addr, inAddrPtr);
		INFO_LOG(Log::sceNet, "%s - Hostname: %s => IPv4: %s", __FUNCTION__, hostname.c_str(),
			ip2str(addr.in.sin_addr, false).c_str());
	}

	// Flag resolver as complete
	resolver->isRunning = false;
	return 0;
}

static int sceNetResolverStartNtoA(int resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}

	auto iter = g_netResolvers.find(resolverId);
	if (iter == g_netResolvers.end()) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_BAD_ID, "Bad Resolver Id: %i", resolverId);
	}

	for (int attempt = 0; attempt < retry; ++attempt) {
		const int status = NetResolver_StartNtoA(&iter->second, hostnamePtr, inAddrPtr, timeout, retry);
		if (status >= 0) {
			return hleLogDebugOrError(Log::sceNet, status);
		}
	}
	return hleLogError(Log::sceNet, -1);
}

static int sceNetResolverStartNtoAAsync(int resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}

	auto iter = g_netResolvers.find(resolverId);
	if (iter == g_netResolvers.end()) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_BAD_ID, "Bad Resolver Id: %i", resolverId);
	}

	const int status = NetResolver_StartNtoA(&iter->second, hostnamePtr, inAddrPtr, timeout, retry);
	return hleLogDebugOrError(Log::sceNet, status);
}

static int sceNetResolverPollAsync(int resolverId, u32 unknown) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}

    // TODO: Implement after confirming that this returns the state of resolver.isRunning
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetResolverWaitAsync(int resolverId, u32 unknown) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}

	// TODO: Implement after confirming that this blocks current thread until resolver.isRunning flips to false
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetResolverStartAtoN(int resolverId, u32 inAddr, u32 hostnamePtr, int hostnameLength, int timeout, int retry) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}
    // TODO: Implement via getnameinfo
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetResolverStartAtoNAsync(int resolverId, u32 inAddr, u32 hostnamePtr, int hostnameLength, int timeout, int retry) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetResolverCreate(u32 resolverIdPtr, u32 bufferPtr, int bufferLen) {
	if (!Memory::IsValidRange(resolverIdPtr, 4))
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_INVALID_PTR, "Invalid Ptr: %08x", resolverIdPtr);

	if (Memory::IsValidRange(bufferPtr, 4) && bufferLen < 1)
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_INVALID_BUFLEN, "Invalid Buffer Length: %i", bufferLen);

	// Outrun calls this without init. Something else must have initialized it, unclear what.

	// TODO: Consider using SceUidManager instead of this 1-indexed id - not sure what the PSP does. Actually, probably not using SceUID.
	// TODO: Implement SCE_NET_RESOLVER_ERROR_ID_MAX (possibly 32?)
	const int currentNetResolverId = g_currentNetResolverId++;
	g_netResolvers[currentNetResolverId] = NetResolver{
		currentNetResolverId, // id
		false,
		bufferPtr, // bufferPtr
		(u32)bufferLen // bufferLen
	};

	Memory::WriteUnchecked_U32(currentNetResolverId, resolverIdPtr);
	return hleLogDebug(Log::sceNet, 0, "ID: %d", currentNetResolverId);
}

static int sceNetResolverStop(u32 resolverId) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}

	const auto resolverIter = g_netResolvers.find(resolverId);

	if (resolverIter == g_netResolvers.end()) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_BAD_ID, "Bad Resolver Id: %i", resolverId);
	}

	if (resolverIter->second.isRunning) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_ALREADY_STOPPED, "Resolver Already Stopped (Id: %i)", resolverId);
	}

	resolverIter->second.isRunning = false;
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetResolverDelete(u32 resolverId) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_STOPPED, "Resolver subsystem not running");
	}

	const auto resolverIter = g_netResolvers.find(resolverId);
	if (resolverIter == g_netResolvers.end()) {
		return hleLogError(Log::sceNet, SCE_NET_RESOLVER_ERROR_BAD_ID, "Bad Resolver Id: %i", resolverId);
	}
	g_netResolvers.erase(resolverIter);

	return hleLogDebug(Log::sceNet, 0);
}

const HLEFunction sceNetResolver[] = {
	{0X224C5F44, &WrapI_IUUII<sceNetResolverStartNtoA>, "sceNetResolverStartNtoA", 'i', "ixxii"},
	{0X244172AF, &WrapI_UUI<sceNetResolverCreate>, "sceNetResolverCreate", 'i', "xxi"},
	{0X94523E09, &WrapI_U<sceNetResolverDelete>, "sceNetResolverDelete", 'i', "i"},
	{0XF3370E61, &WrapI_V<sceNetResolverInit>, "sceNetResolverInit", 'i', ""},
	{0X808F6063, &WrapI_U<sceNetResolverStop>, "sceNetResolverStop", 'i', "i"},
	{0X6138194A, &WrapI_V<sceNetResolverTerm>, "sceNetResolverTerm", 'i', ""},
	{0X629E2FB7, &WrapI_IUUIII<sceNetResolverStartAtoN>, "sceNetResolverStartAtoN", 'i', "ixxiii"},
	{0X14C17EF9, &WrapI_IUUII<sceNetResolverStartNtoAAsync>, "sceNetResolverStartNtoAAsync", 'i', "ixxii"},
	{0XAAC09184, &WrapI_IUUIII<sceNetResolverStartAtoNAsync>, "sceNetResolverStartAtoNAsync", 'i', "ixxiii"},
	{0X12748EB9, &WrapI_IU<sceNetResolverWaitAsync>, "sceNetResolverWaitAsync", 'i', "ix"},
	{0X4EE99358, &WrapI_IU<sceNetResolverPollAsync>, "sceNetResolverPollAsync", 'i', "ix"},
};

void Register_sceNetResolver() {
	RegisterHLEModule("sceNetResolver", ARRAY_SIZE(sceNetResolver), sceNetResolver);
}

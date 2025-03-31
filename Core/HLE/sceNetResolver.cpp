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
static std::mutex g_netResolversLock;  // Do we really need this?

// NOTE: It starts as true, needed for Outrun 2006.
static bool g_netResolverInitialized = true;

static int sceNetResolverInit() {
	// Hardcoded mHostToAlias entries here have been moved to the infra-dns.json file.
	return hleLogInfo(Log::sceNet, 0);
}

void __NetResolverShutdown() {
	std::unique_lock lock(g_netResolversLock);
	g_netResolvers.clear();
}

static int sceNetResolverTerm() {
	g_netResolverInitialized = false;
	__NetResolverShutdown();
	return hleLogInfo(Log::sceNet, 0);
}

// Note: timeouts are in seconds
static int NetResolver_StartNtoA(u32 resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
	std::unique_lock lock(g_netResolversLock);

	auto iter = g_netResolvers.find(resolverId);
	if (iter == g_netResolvers.end()) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_BAD_ID, "Bad Resolver Id: %i", resolverId);
	}

	addrinfo* resolved = nullptr;
	std::string err, hostname = std::string(safe_string(Memory::GetCharPointer(hostnamePtr)));
	SockAddrIN4 addr{};
	addr.in.sin_addr.s_addr = INADDR_NONE;

	// Resolve any aliases. First check the ini file, then check the hardcoded DNS config.
	auto aliasIter = g_Config.mHostToAlias.find(hostname);
	if (aliasIter != g_Config.mHostToAlias.end()) {
		const std::string& alias = aliasIter->second;
		INFO_LOG(Log::sceNet, "%s - Resolved alias %s from hostname %s", __FUNCTION__, alias.c_str(), hostname.c_str());
		hostname = alias;
	}

	if (g_Config.bInfrastructureAutoDNS) {
		// Also look up into the preconfigured fixed DNS JSON.
		auto fixedDNSIter = g_infraDNSConfig.fixedDNS.find(hostname);
		if (fixedDNSIter != g_infraDNSConfig.fixedDNS.end()) {
			const std::string& domainIP = fixedDNSIter->second;
			INFO_LOG(Log::sceNet, "%s - Resolved IP %s from fixed DNS lookup with '%s'", __FUNCTION__, domainIP.c_str(), hostname.c_str());
			hostname = domainIP;
		}
	}

	// Check if hostname is already an IPv4 address, if so we do not need further lookup. This usually happens
	// after the mHostToAlias or fixedDNSIter lookups, which effectively both are hardcoded DNS.
	uint32_t resolvedAddr;
	if (inet_pton(AF_INET, hostname.c_str(), &resolvedAddr)) {
		INFO_LOG(Log::sceNet, "Not looking up '%s', already an IP address.", hostname.c_str());
		Memory::Write_U32(resolvedAddr, inAddrPtr);
		return 0;
	}

	// Flag resolver as in-progress - not relevant for sync functions but potentially relevant for async
	iter->second.isRunning = true;

	// Now use the configured primary DNS server to do a lookup.
	// If auto DNS, use the server from that config.
	const std::string &dnsServer = (g_Config.bInfrastructureAutoDNS && !g_infraDNSConfig.dns.empty()) ? g_infraDNSConfig.dns : g_Config.sInfrastructureDNSServer;
	if (net::DirectDNSLookupIPV4(dnsServer.c_str(), hostname.c_str(), &resolvedAddr)) {
		char temp[32];
		inet_ntop(AF_INET, &resolvedAddr, temp, sizeof(temp));
		INFO_LOG(Log::sceNet, "Direct lookup of '%s' from '%s' succeeded: %s (%08x)", hostname.c_str(), dnsServer.c_str(), temp, resolvedAddr);
		iter->second.isRunning = false;
		Memory::Write_U32(resolvedAddr, inAddrPtr);
		return 0;
	}

	WARN_LOG(Log::sceNet, "Direct DNS lookup of '%s' at DNS server '%s' failed. Trying OS DNS...", hostname.c_str(), g_Config.sInfrastructureDNSServer.c_str());

	// Attempt to execute a DNS resolution
	if (!net::DNSResolve(hostname, "", &resolved, err)) {
		// TODO: Return an error based on the outputted "err" (unfortunately it's already converted to string)
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_INVALID_HOST, "OS DNS Error Resolving %s (%s)\n", hostname.c_str(),
			err.c_str());
	}

	// If successful, write to memory
	if (resolved != nullptr) {
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
	iter->second.isRunning = false;
	return 0;
}

static int sceNetResolverStartNtoA(int resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_STOPPED, "Resolver Subsystem Stopped (Resolver Id: %i)", resolverId);
	}

	for (int attempt = 0; attempt < retry; ++attempt) {
		if (const int status = NetResolver_StartNtoA(resolverId, hostnamePtr, inAddrPtr, timeout, retry); status >= 0) {
			return hleLogInfo(Log::sceNet, status);
		}
	}
	return -1;
}

static int sceNetResolverStartNtoAAsync(int resolverId, u32 hostnamePtr, u32 inAddrPtr, int timeout, int retry) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_STOPPED, "Resolver Subsystem Stopped (Resolver Id: %i)",
			resolverId);
	}
	ERROR_LOG_REPORT_ONCE(sceNetResolverStartNtoAAsync, Log::sceNet, "UNIMPL %s(%d, %08x, %08x, %d, %d) at %08x",
                          __FUNCTION__, resolverId, hostnamePtr, inAddrPtr, timeout, retry, currentMIPS->pc);
    return NetResolver_StartNtoA(resolverId, hostnamePtr, inAddrPtr, timeout, retry);
}

static int sceNetResolverPollAsync(int resolverId, u32 unknown) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverPollAsync, Log::sceNet, "UNIMPL %s(%d, %08x) at %08x", __FUNCTION__, resolverId, unknown, currentMIPS->pc);
    // TODO: Implement after confirming that this returns the state of resolver.isRunning
    return 0;
}

static int sceNetResolverWaitAsync(int resolverId, u32 unknown) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverWaitAsync, Log::sceNet, "UNIMPL %s(%d, %08x) at %08x", __FUNCTION__, resolverId, unknown, currentMIPS->pc);
    // TODO: Implement after confirming that this blocks current thread until resolver.isRunning flips to false
    return 0;
}

static int sceNetResolverStartAtoN(int resolverId, u32 inAddr, u32 hostnamePtr, int hostnameLength, int timeout, int retry) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverStartAtoN, Log::sceNet, "UNIMPL %s(%d, %08x[%s], %08x, %i, %i, %i) at %08x",
                          __FUNCTION__, resolverId, inAddr, ip2str(*(in_addr*)&inAddr, false).c_str(), hostnamePtr,
                          hostnameLength, timeout, retry, currentMIPS->pc);
    // TODO: Implement via getnameinfo
    return 0;
}

static int sceNetResolverStartAtoNAsync(int resolverId, u32 inAddr, u32 hostnamePtr, int hostnameLength, int timeout, int retry) {
    ERROR_LOG_REPORT_ONCE(sceNetResolverStartAtoNAsync, Log::sceNet, "UNIMPL %s(%d, %08x[%s], %08x, %i, %i, %i) at %08x",
                          __FUNCTION__, resolverId, inAddr, ip2str(*(in_addr*)&inAddr, false).c_str(), hostnamePtr,
                          hostnameLength, timeout, retry, currentMIPS->pc);
    return 0;
}

static int sceNetResolverCreate(u32 resolverIdPtr, u32 bufferPtr, int bufferLen) {
	if (!Memory::IsValidRange(resolverIdPtr, 4))
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_INVALID_PTR, "Invalid Ptr: %08x", resolverIdPtr);

	if (Memory::IsValidRange(bufferPtr, 4) && bufferLen < 1)
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_INVALID_BUFLEN, "Invalid Buffer Length: %i", bufferLen);

	// Outrun calls this without init.

	// TODO: Consider using SceUidManager instead of this 1-indexed id
	// TODO: Implement ERROR_NET_RESOLVER_ID_MAX (possibly 32?)
	std::unique_lock lock(g_netResolversLock);
	int currentNetResolverId = g_currentNetResolverId++;
	g_netResolvers[currentNetResolverId] = NetResolver{
		currentNetResolverId, // id
		false,
		bufferPtr, // bufferPtr
		(u32)bufferLen // bufferLen
	};

	Memory::Write_U32(currentNetResolverId, resolverIdPtr);
	return hleLogInfo(Log::sceNet, 0, "ID: %d", Memory::Read_U32(resolverIdPtr));
}

static int sceNetResolverStop(u32 resolverId) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_STOPPED, "Resolver Subsystem Stopped");
	}

	std::unique_lock lock(g_netResolversLock);

	const auto resolverIter = g_netResolvers.find(resolverId);

	if (resolverIter == g_netResolvers.end()) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_BAD_ID, "Bad Resolver Id: %i", resolverId);
	}

	if (resolverIter->second.isRunning) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_ALREADY_STOPPED, "Resolver Already Stopped (Id: %i)", resolverId);
	}

	resolverIter->second.isRunning = false;
	return hleLogInfo(Log::sceNet, 0);
}

static int sceNetResolverDelete(u32 resolverId) {
	if (!g_netResolverInitialized) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_STOPPED, "Resolver Subsystem Stopped");
	}

	std::unique_lock lock(g_netResolversLock);

	const auto resolverIter = g_netResolvers.find(resolverId);
	if (resolverIter == g_netResolvers.end()) {
		return hleLogError(Log::sceNet, ERROR_NET_RESOLVER_BAD_ID, "Bad Resolver Id: %i", resolverId);
	}
	g_netResolvers.erase(resolverIter);

	return hleLogInfo(Log::sceNet, 0);
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

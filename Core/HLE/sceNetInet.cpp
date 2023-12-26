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
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MemMapHelpers.h"

#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetInet.h"

#include <iostream>
#include <shared_mutex>

#include "Core/HLE/sceNp.h"
#include "Core/Reporting.h"
// TODO: move Core/Net
#include "Core/Net/InetCommon.h"
#include "Core/Net/SceSocket.h"
#include "Core/Util/PortManager.h"

#define SCENET Log::sceNet

#if PPSSPP_PLATFORM(SWITCH) && !defined(INADDR_NONE)
// Missing toolchain define
#define INADDR_NONE 0xFFFFFFFF
#elif PPSSPP_PLATFORM(WINDOWS)
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#define ERROR_WHEN_NONBLOCKING_CALL_OCCURS WSAEWOULDBLOCK
using netBufferType = char;
#else
#define ERROR_WHEN_NONBLOCKING_CALL_OCCURS EWOULDBLOCK
#include <ifaddrs.h>
using netBufferType = void;
#endif

// TODO: socket domain
// TODO: socket level
// TODO: ignore reuseaddr (option)
// TODO: ignore port (option)
// TODO: ignore SO_NOSIGPIPE
// TODO: timeouts (PSP_NET_INET_SO_SNDTIMEO)

struct SceTimeval {
	u32 tv_sec;		/* Seconds.  */
	u32 tv_usec;	/* Microseconds.  */
};

class SceFdSetOperations {
public:
	typedef long int fdMask;
	static constexpr int gFdsBitsCount = 8 * static_cast<int>(sizeof(fdMask));

	struct FdSet {
		fdMask mFdsBits[256 / gFdsBitsCount];
	};

	static void Set(FdSet *sceFdSetBits, int socket) {
		sceFdSetBits->mFdsBits[Position(socket)] |= ConvertToMask(socket);
	}

	static bool IsSet(const FdSet *sceFdSetBits, int socket) {
		return (sceFdSetBits->mFdsBits[Position(socket)] & ConvertToMask(socket)) != 0;
	}

	static void Clear(FdSet *sceFdSetBits, int socket) {
		sceFdSetBits->mFdsBits[Position(socket)] &= ~ConvertToMask(socket);
	}

	static void Zero(FdSet *sceFdSetBits) {
		memset(sceFdSetBits->mFdsBits, 0, sizeof(FdSet));
	}

private:
	static int Position(const int socket) {
		return socket / gFdsBitsCount;
	}

	static int ConvertToMask(const int socket) {
		return static_cast<fdMask>(1UL << (socket % gFdsBitsCount));
	}
};

// static int getLastPlatformError() {
// #if PPSSPP_PLATFORM(WINDOWS)
// 	return WSAGetLastError();
// #else
// 	return errno;
// #endif
// }

static bool sceSockaddrToNativeSocketAddr(sockaddr_in &dest, u32 sockAddrInternetPtr, size_t addressLength) {
	const auto sceNetSockaddrIn = Memory::GetTypedPointerRange<SceNetInetSockaddrIn>(sockAddrInternetPtr, addressLength);
	if (sceNetSockaddrIn == nullptr || addressLength == 0) {
		return false;
	}

	memset(&dest, 0, sizeof(dest));
	dest.sin_family = sceNetSockaddrIn->sin_family;
	dest.sin_port = sceNetSockaddrIn->sin_port;
	dest.sin_addr.s_addr = sceNetSockaddrIn->sin_addr;
	DEBUG_LOG(SCENET, "sceSockaddrToNativeSocketAddr: Family %i, port %i, addr %s, len %i", dest.sin_family, ntohs(dest.sin_port), ip2str(dest.sin_addr, false).c_str(), sceNetSockaddrIn->sin_len);
	return true;
}

static bool writeSockAddrInToSceSockAddr(u32 destAddrPtr, u32 destAddrLenPtr, sockaddr_in src) {
	const auto sceNetSocklenPtr = reinterpret_cast<u32*>(Memory::GetPointerWrite(destAddrLenPtr));
	u32 sceNetSocklen = 0;
	if (sceNetSocklenPtr != nullptr) {
		sceNetSocklen = *sceNetSocklenPtr;
	}
	const auto sceNetSockaddrIn = Memory::GetTypedPointerWriteRange<SceNetInetSockaddrIn>(destAddrPtr, sceNetSocklen);
	if (sceNetSockaddrIn == nullptr) {
		return false;
	}
	INFO_LOG(SCENET, "writeSockAddrInToSceSockAddr: %lu vs %i", sizeof(SceNetInetSockaddrIn), sceNetSocklen);
	if (sceNetSocklenPtr) {
		*sceNetSocklenPtr = std::min<u32>(sceNetSocklen, sizeof(SceNetInetSockaddr));
	}
	if (sceNetSocklen >= 1) {
		sceNetSockaddrIn->sin_len = sceNetSocklen;
	}
	if (sceNetSocklen >= 2) {
		sceNetSockaddrIn->sin_family = src.sin_family;
	}
	if (sceNetSocklen >= 4) {
		sceNetSockaddrIn->sin_port = src.sin_port;
	}
	if (sceNetSocklen >= 8) {
		sceNetSockaddrIn->sin_addr = src.sin_addr.s_addr;
	}
	return true;
}

static int setBlockingMode(int nativeSocketId, bool nonblocking) {
#if PPSSPP_PLATFORM(WINDOWS)
	unsigned long val = nonblocking ? 1 : 0;
	return ioctlsocket(fd, FIONBIO, &val);
#else
	// Change to Non-Blocking Mode
	if (nonblocking) {
		return fcntl(nativeSocketId, F_SETFL, O_NONBLOCK);
	} else {
		const int flags = fcntl(nativeSocketId, F_GETFL);

		// Remove Non-Blocking Flag
		return fcntl(nativeSocketId, F_SETFL, flags & ~O_NONBLOCK);
	}
#endif
}

static int sceNetInetInit() {
    ERROR_LOG(SCENET, "UNTESTED sceNetInetInit()");
    return SceNetInet::Init() ? 0 : ERROR_NET_INET_ALREADY_INITIALIZED;
}

int sceNetInetTerm() {
    ERROR_LOG(SCENET, "UNTESTED sceNetInetTerm()");
	SceNetInet::Shutdown();
    return 0;
}

static int sceNetInetSocket(int domain, int type, int protocol) {
	WARN_LOG_ONCE(sceNetInetSocket, SCENET, "UNTESTED sceNetInetSocket(%i, %i, %i)", domain, type, protocol);
	auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const int nativeSocketId = socket(domain, type, protocol);
	const auto sceSocket = sceNetInet->CreateAndAssociateSceSocket(nativeSocketId);

	if (!sceSocket) {
		close(nativeSocketId);
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, ERROR_NET_INET_INVALID_ARG, "%s: Unable to create new SceSocket for native socket id %i, closing", __func__, nativeSocketId);
	}

	return sceSocket->GetSceSocketId();
}

static int sceNetInetGetsockopt(int socket, int level, int inetOptname, u32 optvalPtr, u32 optlenPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetInetGetsockopt(%i, %i, %i, %08x, %08x)", socket, level, inetOptname, optvalPtr, optlenPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	if (!sceSocket->IsSockoptNameAllowed(inetOptname)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Unknown optname %04x", inetOptname);
	}

	const auto optname = SceSocket::TranslateInetOptnameToNativeOptname(static_cast<InetSocketOptionName>(inetOptname));
	if (optname != inetOptname) {
		DEBUG_LOG(SCENET, "sceNetInetSetsockopt: Translated optname %04x into %04x", inetOptname, optname);
	}

	const auto nativeSocketId = sceSocket->GetNativeSocketId();

#if PPSSPP_PLATFORM(WINDOWS)
	auto optlen = reinterpret_cast<int*>(Memory::GetPointerWrite(optlenPtr));
#else
	auto optlen = reinterpret_cast<u32*>(Memory::GetPointerWrite(optlenPtr));
#endif
	if (optlen == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "[%i] %s: Invalid pointer %08x", nativeSocketId, __func__, optlenPtr);
	}

	const auto optval = Memory::GetTypedPointerWriteRange<netBufferType>(optvalPtr, *optlen);
	if (optval == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "[%i] %s: Invalid pointer range %08x (size %i)", nativeSocketId, __func__, optvalPtr, *optlen);
	}

	// TODO: implement non-blocking sockopt
	const int ret = getsockopt(nativeSocketId, SOL_SOCKET, optname, optval, optlen);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(SCENET, ret, "[%i] %s: returned error %i: %s", nativeSocketId, __func__, error, strerror(error));
	}
	return ret;
}

static int sceNetInetSetsockopt(int socket, int level, int inetOptname, u32 optvalPtr, int optlen) {
	WARN_LOG_ONCE(sceNetInetSetsockopt, SCENET, "UNTESTED sceNetInetSetsockopt(%i, %i, %i, %08x, %i)", socket, level, inetOptname, optvalPtr, optlen);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	if (!sceSocket->IsSockoptNameAllowed(inetOptname)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Unknown optname %04x", inetOptname);
	}

	const auto optname = SceSocket::TranslateInetOptnameToNativeOptname(static_cast<InetSocketOptionName>(inetOptname));
	if (optname != inetOptname) {
		DEBUG_LOG(SCENET, "sceNetInetSetsockopt: Translated optname %04x into %04x", inetOptname, optname);
	}

	// If optlens of != sizeof(u32) are created, split out the handling into separate functions for readability
	if (optlen != sizeof(u32)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "[%i]: %s: Unhandled optlen %i for optname %04x", sceSocket->GetNativeSocketId(), __func__, optlen, inetOptname);
	}

	auto optval = Memory::Read_U32(optvalPtr);
	const auto nativeSocketId = sceSocket->GetNativeSocketId();
	INFO_LOG(SCENET, "[%i] setsockopt_u32(%i, %i, %i, %i)", nativeSocketId, nativeSocketId, level, optname, optval);

	switch (optname) {
		// Unmatched PSP functions - no direct equivalent
		case INET_SO_NONBLOCK: {
			const bool nonblocking = optval != 0;
			sceSocket->SetNonBlocking(nonblocking);
			INFO_LOG(SCENET, "[%i] setsockopt_u32: Set non-blocking=%i", nativeSocketId, nonblocking);
			if (setBlockingMode(nativeSocketId, nonblocking) != 0) {
				const auto error = sceNetInet->SetLastErrorToMatchPlatform();
				ERROR_LOG(SCENET, "[%i] Failed to set to non-blocking: %i: %s", nativeSocketId, error, strerror(error));
			}
			return 0;
		}
		default: {
			INFO_LOG(SCENET, "UNTESTED sceNetInetSetsockopt(%i, %i, %i, %u, %i)", nativeSocketId, level, optname, optval, 4);
			int ret = setsockopt(nativeSocketId, SOL_SOCKET, optname, reinterpret_cast<netBufferType*>(&optval), sizeof(optval));
			INFO_LOG(SCENET, "setsockopt_u32: setsockopt returned %i for %i", ret, nativeSocketId);
			return ret;
		}
	}
}

static int sceNetInetConnect(int socket, u32 sockAddrInternetPtr, int addressLength) {
	WARN_LOG_ONCE(sceNetInetConnect, SCENET, "UNTESTED sceNetInetConnect(%i, %08x, %i, %i)", socket, sockAddrInternetPtr, Memory::Read_U32(sockAddrInternetPtr), addressLength);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForSceSocketId(nativeSocketId, socket)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	sockaddr_in convertedSockaddr{};
	if (!sceSockaddrToNativeSocketAddr(convertedSockaddr, sockAddrInternetPtr, addressLength)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "[%i] %s: Error translating sceSockaddr to native sockaddr", nativeSocketId, __func__);
	}

	DEBUG_LOG(SCENET, "[%i] sceNetInetConnect: Connecting to %s on %i", nativeSocketId, ip2str(convertedSockaddr.sin_addr, false).c_str(), ntohs(convertedSockaddr.sin_port));

	int ret = connect(nativeSocketId, reinterpret_cast<sockaddr*>(&convertedSockaddr), sizeof(convertedSockaddr));
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(SCENET, ret, "[%i] %s: Error connecting %i: %s", nativeSocketId, __func__, error, strerror(error));
	}
	return hleLogSuccessI(SCENET, ret);
}

static int sceNetInetListen(int socket, int backlog) {
	WARN_LOG_ONCE(sceNetInetListen, SCENET, "UNTESTED %s(%i, %i)", __func__, socket, backlog);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForSceSocketId(nativeSocketId, socket)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	// TODO: here
	if (backlog == PSP_NET_INET_SOMAXCONN) {
		backlog = SOMAXCONN;
	}

	const int ret = listen(socket, backlog);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(SCENET, ret, "[%i] %s: Error listening %i: %s", nativeSocketId, __func__, error, strerror(error));
	}
	return hleLogSuccessI(SCENET, ret);
}

static int sceNetInetAccept(int socket, u32 addrPtr, u32 addrLenPtr) {
	WARN_LOG_ONCE(sceNetInetListen, SCENET, "UNTESTED %s(%i, %08x, %08x)", __func__, socket, addrPtr, addrLenPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForSceSocketId(nativeSocketId, socket)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	sockaddr_in sockaddrIn{};
	socklen_t socklen;
	int ret = accept(nativeSocketId, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		if (error != ERROR_WHEN_NONBLOCKING_CALL_OCCURS) {
			hleLogError(SCENET, ret, "[%i] %s: Encountered error %i: %s", nativeSocketId, __func__, error, strerror(error));
		}
		return ret;
	}

	if (addrPtr != 0 && !writeSockAddrInToSceSockAddr(addrPtr, addrLenPtr, sockaddrIn)) {
		sceNetInet->SetLastError(EFAULT);
		hleLogError(SCENET, ret, "[%i] %s: Encountered error trying to write to addrPtr, probably invalid memory range", nativeSocketId, __func__);
	}
	return hleLogSuccessI(SCENET, ret);
}

int sceNetInetPoll(void *fds, u32 nfds, int timeout) { // timeout in miliseconds
	DEBUG_LOG(SCENET, "UNTESTED sceNetInetPoll(%p, %d, %i) at %08x", fds, nfds, timeout, currentMIPS->pc);
	const auto fdarray = static_cast<SceNetInetPollfd*>(fds); // SceNetInetPollfd/pollfd, sceNetInetPoll() have similarity to BSD poll() but pollfd have different size on 64bit
//#ifdef _WIN32
	//WSAPoll only available for Vista or newer, so we'll use an alternative way for XP since Windows doesn't have poll function like *NIX
	if (nfds > FD_SETSIZE) {
		ERROR_LOG(SCENET, "sceNetInetPoll: nfds=%i is greater than FD_SETSIZE=%i, unable to poll", nfds, FD_SETSIZE);
		return -1;
	}
	fd_set readfds, writefds, exceptfds;
	FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);
	for (int i = 0; i < static_cast<s32>(nfds); i++) {
		if (fdarray[i].events & (INET_POLLRDNORM))
			FD_SET(fdarray[i].fd, &readfds); // (POLLRDNORM | POLLIN)
		if (fdarray[i].events & (INET_POLLWRNORM))
			FD_SET(fdarray[i].fd, &writefds); // (POLLWRNORM | POLLOUT)
		//if (fdarray[i].events & (ADHOC_EV_ALERT)) // (POLLRDBAND | POLLPRI) // POLLERR
		FD_SET(fdarray[i].fd, &exceptfds);
		fdarray[i].revents = 0;
	}
	timeval tmout{};
	tmout.tv_sec = timeout / 1000; // seconds
	tmout.tv_usec = (timeout % 1000) * 1000; // microseconds
	const int ret = select(nfds, &readfds, &writefds, &exceptfds, &tmout);
	if (ret < 0)
		return -1;
	int eventCount = 0;
	for (int i = 0; i < static_cast<s32>(nfds); i++) {
		if (FD_ISSET(fdarray[i].fd, &readfds))
			fdarray[i].revents |= INET_POLLRDNORM; //POLLIN
		if (FD_ISSET(fdarray[i].fd, &writefds))
			fdarray[i].revents |= INET_POLLWRNORM; //POLLOUT
		fdarray[i].revents &= fdarray[i].events;
		if (FD_ISSET(fdarray[i].fd, &exceptfds))
			fdarray[i].revents |= ADHOC_EV_ALERT; // POLLPRI; // POLLERR; // can be raised on revents regardless of events bitmask?
		if (fdarray[i].revents)
			eventCount++;
	}
//#else
	/*
	// Doesn't work properly yet
	pollfd *fdtmp = (pollfd *)malloc(sizeof(pollfd) * nfds);
	// Note: sizeof(pollfd) = 16bytes in 64bit and 8bytes in 32bit, while sizeof(SceNetInetPollfd) is always 8bytes
	for (int i = 0; i < (s32)nfds; i++) {
		fdtmp[i].fd = fdarray[i].fd;
		fdtmp[i].events = 0;
		if (fdarray[i].events & INET_POLLRDNORM) fdtmp[i].events |= (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI);
		if (fdarray[i].events & INET_POLLWRNORM) fdtmp[i].events |= (POLLOUT | POLLWRNORM | POLLWRBAND);
		fdtmp[i].revents = 0;
		fdarray[i].revents = 0;
	}
	retval = poll(fdtmp, (nfds_t)nfds, timeout); //retval = WSAPoll(fdarray, nfds, timeout);
	for (int i = 0; i < (s32)nfds; i++) {
		if (fdtmp[i].revents & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)) fdarray[i].revents |= INET_POLLRDNORM;
		if (fdtmp[i].revents & (POLLOUT | POLLWRNORM | POLLWRBAND)) fdarray[i].revents |= INET_POLLWRNORM;
		fdarray[i].revents &= fdarray[i].events;
		if (fdtmp[i].revents & POLLERR) fdarray[i].revents |= POLLERR; //INET_POLLERR // can be raised on revents regardless of events bitmask?
	}
	free(fdtmp);
	*/
//#endif
	return eventCount;
}

static int sceNetInetSelect(int maxfd, u32 readFdsPtr, u32 writeFdsPtr, u32 exceptFdsPtr, u32 timeoutPtr) {
	WARN_LOG_ONCE(sceNetInetSelect, SCENET, "UNTESTED sceNetInetSelect(%i, %08x, %08x, %08x, %08x)", maxfd, readFdsPtr, writeFdsPtr, exceptFdsPtr, timeoutPtr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int recomputedMaxFd = 1;
	fd_set readFds;
	sceNetInet->TranslateSceFdSetToNativeFdSet(recomputedMaxFd, readFds, readFdsPtr);
	fd_set writeFds;
	sceNetInet->TranslateSceFdSetToNativeFdSet(recomputedMaxFd, writeFds, writeFdsPtr);
	fd_set exceptFds;
	sceNetInet->TranslateSceFdSetToNativeFdSet(recomputedMaxFd, exceptFds, exceptFdsPtr);

	timeval tv{};
	if (timeoutPtr != 0) {
		const auto sceTimeval = Memory::GetTypedPointerRange<SceTimeval>(timeoutPtr, sizeof(SceTimeval));
		if (sceTimeval != nullptr) {
			tv.tv_sec = sceTimeval->tv_sec;
			tv.tv_usec = sceTimeval->tv_usec;
			DEBUG_LOG(SCENET, "sceNetInetSelect: Timeout seconds=%lu, useconds=%lu", tv.tv_sec, tv.tv_usec);
		} else {
			WARN_LOG(SCENET, "sceNetInetSelect: Encountered invalid timeout value, continuing anyway");
		}
	}

	// Since the fd_set structs are allocated on the stack (and always so), only pass in their pointers if the input pointer is non-null
	const int ret = select(recomputedMaxFd,  readFdsPtr != 0 ? &readFds : nullptr, writeFdsPtr != 0 ? &writeFds : nullptr,  exceptFdsPtr != 0 ? &exceptFds : nullptr, timeoutPtr != 0 ? &tv : nullptr);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		ERROR_LOG(SCENET, "sceNetInetSelect: Received error from select() %i: %s", error, strerror(error));
	}

	INFO_LOG(SCENET, "sceNetInetSelect: select() returned %i", ret);
	return hleDelayResult(ret, "TODO: unhack", 300);
}

static int sceNetInetClose(int socket) {
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogWarning(SCENET, -1, "%s: Attempting to close socket %i which does not exist", __func__, socket);
	}

	const int ret = close(sceSocket->GetNativeSocketId());
	if (!sceNetInet->EraseNativeSocket(socket)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Unable to clear mapping of sceSocketId->nativeSocketId, was there contention?", __func__);
	}
	return ret;
}

static u32 sceNetInetInetAddr(const char *hostname) {
	ERROR_LOG(SCENET, "UNTESTED sceNetInetInetAddr(%s)", hostname);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	in_addr inAddr{};
	// TODO: de-dupe
#if PPSSPP_PLATFORM(WINDOWS)
	const int ret = inet_pton(AF_INET, hostname, &inAddr);
#else
	const int ret = inet_aton(hostname, &inAddr);
#endif
	if (ret != 0) {
		sceNetInet->SetLastErrorToMatchPlatform();
		return inAddr.s_addr;
	}
	return ret;
}

static int sceNetInetInetAton(const char *hostname, u32 addrPtr) {
	ERROR_LOG(SCENET, "UNTESTED %s(%s, %08x)", __func__, hostname, addrPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	if (!Memory::IsValidAddress(addrPtr)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Invalid addrPtr: %08x", __func__, addrPtr);
	}

	in_addr inAddr{};
#if PPSSPP_PLATFORM(WINDOWS)
	const int ret = inet_pton(AF_INET, hostname, &inAddr);
#else
	const int ret = inet_aton(hostname, &inAddr);
#endif
	if (ret != 0) {
		Memory::Write_U32(inAddr.s_addr, addrPtr);
	}
	return ret;
}

static u32 sceNetInetInetNtop(int addressFamily, u32 srcPtr, u32 dstBufPtr, u32 dstBufSize) {
	WARN_LOG_ONCE(sceNetInetInetNtop, SCENET, "UNTESTED %s(%i, %08x, %08x, %i)", __func__, addressFamily, srcPtr, dstBufPtr, dstBufSize);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto srcSockaddrIn = Memory::GetTypedPointerWriteRange<SceNetInetSockaddrIn>(srcPtr, sizeof(SceNetInetSockaddrIn));
	if (srcSockaddrIn == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, 0, "%s: Invalid memory range for srcPtr %08x", __func__, srcPtr);
	}

	const auto dstBuf = Memory::GetTypedPointerWriteRange<char>(dstBufPtr, dstBufSize);
	if (dstBuf == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, 0, "%s: Invalid memory range for dstBufPtr %08x, size %i", __func__, dstBufPtr, dstBufSize);
	}

	if (!dstBufSize) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, 0, "%s: dstBufSize must be > 0", __func__);
	}

	// TODO: convert address family
	if (inet_ntop(addressFamily, reinterpret_cast<netBufferType*>(srcSockaddrIn), dstBuf, dstBufSize) == nullptr) {
		// Allow partial output in case it's desired for some reason
	}
	return hleLogSuccessX(SCENET, dstBufPtr);
}

static int sceNetInetInetPton(int addressFamily, const char *hostname, u32 dstBufPtr) {
	WARN_LOG_ONCE(sceNetInetInetPton, SCENET, "UNTESTED %s(%i, %s, %08x)", __func__, addressFamily, hostname, dstBufPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto srcSockaddrIn = Memory::GetTypedPointerWriteRange<SceNetInetSockaddrIn>(srcPtr, sizeof(SceNetInetSockaddrIn));
	if (srcSockaddrIn == nullptr) {
		return hleLogError(SCENET, 0, "%s: Invalid memory range for srcPtr %08x", __func__, srcPtr);
	}

	// IPv4, the only supported address family on PSP, will always be 32 bits
	const auto dstBuf = Memory::GetTypedPointerWriteRange<u32>(dstBufPtr, sizeof(u32));
	if (dstBuf == nullptr) {
		return hleLogError(SCENET, 0, "%s: Invalid memory range for dstBufPtr %08x, size %i", __func__, dstBufPtr, dstBufSize);
	}

	// TODO: convert address family
	// TODO: If af does not contain a valid address family, -1 is returned and errno is set to EAFNOSUPPORT.
	const int ret = inet_pton(addressFamily, reinterpret_cast<netBufferType*>(srcSockaddrIn), dstBuf);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(SCENET, ret, "%s: inet_pton returned %i: %s", __func__, sceNetInet->GetLastError(), strerror(error));
	}
	return hleLogSuccessI(SCENET, ret);
}

static int sceNetInetGetpeername(int socket, u32 addrPtr, u32 addrLenPtr) {
	ERROR_LOG(SCENET, "UNTESTED sceNetInetGetsockname(%i, %08x, %08x)", socket, addrPtr, addrLenPtr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForSceSocketId(nativeSocketId, socket)) {
		ERROR_LOG(SCENET, "%s: Requested socket %i which does not exist", __func__, socket);
		return -1;
	}

	// Write PSP sockaddr to native sockaddr in preparation of getpeername
	sockaddr_in sockaddrIn{};
	socklen_t socklen = sizeof(sockaddr_in);
	if (!sceSockaddrToNativeSocketAddr(sockaddrIn, addrPtr, addrLenPtr)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "[%i]: %s: Encountered invalid addrPtr %08x and/or invalid addrLenPtr %08x", nativeSocketId, addrPtr, addrLenPtr);
	}

	const int ret = getpeername(nativeSocketId, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(SCENET, ret, "[%i] %s: Failed to execute getpeername %i: %s", nativeSocketId, __func__, error, strerror(error));
	}

	if (!writeSockAddrInToSceSockAddr(addrPtr, addrLenPtr, sockaddrIn)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "[%i] %s: Failed to write results of getpeername to SceNetInetSockaddrIn", nativeSocketId, __func__);
	}
	return ret;
}

static int sceNetInetGetsockname(int socket, u32 addrPtr, u32 addrLenPtr) {
	ERROR_LOG(SCENET, "UNTESTED %s(%i, %08x, %08x)", __func__, socket, addrPtr, addrLenPtr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForSceSocketId(nativeSocketId, socket)) {
		ERROR_LOG(SCENET, "%s: Requested socket %i which does not exist", __func__, socket);
		return -1;
	}

	sockaddr_in sockaddrIn{};
	socklen_t socklen = sizeof(sockaddr_in);
	const int ret = getsockname(nativeSocketId, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(SCENET, ret, "[%i] %s: Failed to execute getsockname %i: %s", nativeSocketId, __func__, error, strerror(error));
	}

	if (!writeSockAddrInToSceSockAddr(addrPtr, addrLenPtr, sockaddrIn)) {
		return hleLogError(SCENET, -1, "[%i] %s: Failed to write results of getsockname to SceNetInetSockaddrIn", nativeSocketId, __func__);
	}
	return ret;
}

static int sceNetInetRecv(int socket, u32 bufPtr, u32 bufLen, int flags) {
	WARN_LOG_ONCE(sceNetInetRecv, SCENET, "UNTESTED sceNetInetRecv(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		WARN_LOG(SCENET, "sceNetInetClose: Attempting to close socket %i which does not exist", socket);
		return -1;
	}

	const auto dstBuf = Memory::GetTypedPointerWriteRange<netBufferType>(bufPtr, bufLen);
	if (dstBuf == nullptr) {
		return hleLogError(SCENET, ERROR_NET_INET_INVALID_ARG, "sceNetInetRecv: Invalid pointer %08x (size %i)", bufPtr, bufLen);
	}

	// TODO: debug log the API calls
	const int nativeFlags = sceSocket->TranslateInetFlagsToNativeFlags(flags);

	const int ret = recv(sceSocket->GetNativeSocketId(), dstBuf, bufLen, nativeFlags);
	if (ret < 0) {
		if (const auto error = sceNetInet->SetLastErrorToMatchPlatform(); error != ERROR_WHEN_NONBLOCKING_CALL_OCCURS) {
			ERROR_LOG(SCENET, "[%i]: %s: recv() encountered error %i: %s", socket, __func__, error, strerror(error));
		}
	}
	return ret;
}

static int sceNetInetRecvfrom(int socket, u32 bufPtr, u32 bufLen, int flags, u32 fromAddr, u32 fromLenAddr) {
	WARN_LOG_ONCE(sceNetInetRecvFrom, SCENET, "UNTESTED sceNetInetRecvfrom(%i, %08x, %i, %08x, %08x, %08x)", socket, bufPtr, bufLen, flags, fromAddr, fromLenAddr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const auto nativeSocketId = sceSocket->GetNativeSocketId();
	const auto dstBuf = Memory::GetTypedPointerWriteRange<netBufferType>(bufPtr, bufLen);
	if (dstBuf == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "[%i] %s: Invalid pointer range: %08x (size %i)", nativeSocketId, __func__, bufPtr, bufLen);
	}

	const int nativeFlags = sceSocket->TranslateInetFlagsToNativeFlags(flags);
	sockaddr_in sockaddrIn{};
	socklen_t socklen = sizeof(sockaddr_in);
	Memory::Memset(bufPtr, 0, bufLen, __func__);

	const int ret = recvfrom(nativeSocketId, dstBuf, bufLen, nativeFlags, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);

	if (ret < 0) {
		if (const auto error = sceNetInet->SetLastErrorToMatchPlatform();
			error != 0 && error != ERROR_WHEN_NONBLOCKING_CALL_OCCURS) {
			WARN_LOG(SCENET, "[%i] sceNetInetRecvfrom: Received error %i: %s", nativeSocketId, error, strerror(error));
		}
		return hleDelayResult(ret, "TODO: unhack", 500);
	}

	if (ret > 0) {
		if (!writeSockAddrInToSceSockAddr(fromAddr, fromLenAddr, sockaddrIn)) {
			ERROR_LOG(SCENET, "[%i] sceNetInetRecvfrom: Error writing native sockaddr to sceSockaddr", nativeSocketId);
		}
		INFO_LOG(SCENET, "[%i] sceNetInetRecvfrom: Got %i bytes from recvfrom", nativeSocketId, ret);
	}
	return hleDelayResult(ret, "TODO: unhack", 500);
}

static int sceNetInetSend(int socket, u32 bufPtr, u32 bufLen, int flags) {
	WARN_LOG_ONCE(sceNetInetSend, SCENET, "UNTESTED sceNetInetSend(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const auto resolvedPtr = Memory::GetTypedPointerRange<netBufferType>(bufPtr, bufLen);
	if (resolvedPtr == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "[%i] %s: Invalid pointer range: %08x (size %i)", socket, __func__, bufPtr, bufLen);
	}

	const int nativeFlags = sceSocket->TranslateInetFlagsToNativeFlags(flags);

	const int ret = send(sceSocket->GetNativeSocketId(), resolvedPtr, bufLen, nativeFlags);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(SCENET, ret, "[%i]: %s: send() encountered error %i: %s", socket, __func__, error, strerror(error));
	}
	return hleLogSuccessI(SCENET, ret);
}

static int sceNetInetSendto(int socket, u32 bufPtr, u32 bufLen, int flags, u32 toAddr, u32 toLen) {
	ERROR_LOG_ONCE(sceNetInetSendto, SCENET, "UNTESTED sceNetInetSendto(%i, %08x, %i, %08x, %08x, %i)", socket, bufPtr, bufLen, flags, toAddr, toLen);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const int nativeSocketId = sceSocket->GetNativeSocketId();
	const auto srcBuf = Memory::GetTypedPointerRange<netBufferType>(bufPtr, bufLen);
	if (srcBuf == nullptr) {
		ERROR_LOG(SCENET, "[%i] sceNetInetSendto: Invalid pointer range: %08x (size %i)", socket, bufPtr, bufLen);
		return -1;
	}

	const int nativeFlags = sceSocket->TranslateInetFlagsToNativeFlags(flags);
	sockaddr_in convertedSockAddr{};
	if (!sceSockaddrToNativeSocketAddr(convertedSockAddr, toAddr, toLen)) {
		ERROR_LOG(SCENET, "[%i] sceNetInetSendto: Unable to translate sceSockAddr to native sockaddr", nativeSocketId);
		return -1;
	}

	// TODO: improve debug log
	DEBUG_LOG(SCENET, "[%i] sceNetInetSendto: Writing %i bytes to %s on port %i", nativeSocketId, bufLen, ip2str(convertedSockAddr.sin_addr, false).c_str(), ntohs(convertedSockAddr.sin_port));

	const int ret = sendto(nativeSocketId, srcBuf, bufLen, nativeFlags, reinterpret_cast<sockaddr*>(&convertedSockAddr), sizeof(sockaddr_in));
	DEBUG_LOG(SCENET, "[%i] sceNetInetSendto: sendto returned %i", nativeSocketId, ret);

	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		WARN_LOG(SCENET, "[%i] sceNetInetSendto: Got error %i=%s", nativeSocketId, error, strerror(error));
	}

	return ret;
}

static int sceNetInetGetErrno() {
	ERROR_LOG_ONCE(sceNetInetGetErrno, SCENET, "UNTESTED sceNetInetGetErrno()");
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto nativeError = sceNetInet->GetLastError();
	if (nativeError != ERROR_WHEN_NONBLOCKING_CALL_OCCURS && nativeError != 0) {
		INFO_LOG(SCENET, "Requested sceNetInetGetErrno %i=%s", nativeError, strerror(nativeError));
	}

	// TODO: consider moving below function to SceNetInet
	return SceSocket::TranslateNativeErrorToInetError(nativeError);
}

static int sceNetInetBind(int socket, u32 addrPtr, u32 addrLen) {
	WARN_LOG_ONCE(sceNetInetSend, SCENET, "UNTESTED sceNetInetBind(%i, %08x, %08x)", socket, addrPtr, addrLen);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(SCENET, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto sceSocket = sceNetInet->GetSceSocket(socket);
	if (!sceSocket) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(SCENET, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const int nativeSocketId = sceSocket->GetNativeSocketId();

#if PPSSPP_PLATFORM(LINUX)
	// Set broadcast
	// TODO: move broadcast SceSocket
	int broadcastEnabled = 1;
	int sockoptRet = setsockopt(nativeSocketId, SOL_SOCKET, SO_BROADCAST, &broadcastEnabled, sizeof(broadcastEnabled));

	// Set reuseport / reuseaddr by default
	// TODO: evaluate
	int opt = 1;
#if defined(SO_REUSEPORT)
	setsockopt(nativeSocketId, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
	setsockopt(nativeSocketId, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#elif PPSSPP_PLATFORM(WINDOWS)
	// Set broadcast
	// TODO: move broadcast SceSocket
	int broadcastEnabled = 1;
	int sockoptRet = setsockopt(nativeSocketId, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&broadcastEnabled), sizeof(broadcastEnabled));
	int opt = 1;
	setsockopt(nativeSocketId, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));
#endif

	sockaddr_in convertedSockaddr{};
	if (!sceSockaddrToNativeSocketAddr(convertedSockaddr, addrPtr, addrLen)) {
		ERROR_LOG(SCENET, "[%i] Error translating sceSockaddr to native sockaddr", nativeSocketId);
		return -1;
	}
	socklen_t socklen = sizeof(convertedSockaddr);

	// Get default outbound sockaddr when INADDR_ANY or INADDR_BROADCAST are used
	if (const auto addr = convertedSockaddr.sin_addr.s_addr; addr == INADDR_ANY || addr == INADDR_BROADCAST) {
		if (!getDefaultOutboundSockaddr(convertedSockaddr, socklen)) {
			WARN_LOG(SCENET, "Failed to get default bound address");
			return -1;
		}
	}

	// TODO: check whether setting to blocking and then non-blocking is valid
	setBlockingMode(nativeSocketId, false);
	INFO_LOG(SCENET, "[%i] Binding to family %i, port %i, addr %s sockoptRet %i", nativeSocketId, convertedSockaddr.sin_family, ntohs(convertedSockaddr.sin_port), ip2str(convertedSockaddr.sin_addr, false).c_str(), sockoptRet);
	const int ret = bind(nativeSocketId, reinterpret_cast<sockaddr*>(&convertedSockaddr), socklen);
	INFO_LOG(SCENET, "Bind returned %i for fd=%i", ret, nativeSocketId);
	setBlockingMode(nativeSocketId, sceSocket->IsNonBlocking());

	// Set UPnP
	const auto port = ntohs(convertedSockaddr.sin_port);
	switch (sceSocket->GetProtocol()) {
		case INET_PROTOCOL_TCP: {
			UPnP_Add(IP_PROTOCOL_TCP, port, port);
			break;
		}
		case INET_PROTOCOL_UDP: {
			UPnP_Add(IP_PROTOCOL_UDP, port, port);
			break;
		}
		default: {
			WARN_LOG(SCENET, "[%i]: Unknown IP protocol %04x when attempting to set up UPnP port forwarding", nativeSocketId, sceSocket->GetProtocol());
			break;
		}
	}
	return ret;
}

// TODO: fix retmasks
const HLEFunction sceNetInet[] = {
	{0X17943399, &WrapI_V<sceNetInetInit>,           "sceNetInetInit",                  'i', ""     },
	{0X4CFE4E56, nullptr,                            "sceNetInetShutdown",              '?', ""     },
	{0XA9ED66B9, &WrapI_V<sceNetInetTerm>,           "sceNetInetTerm",                  'i', ""     },
	{0X8B7B220F, &WrapI_III<sceNetInetSocket>,       "sceNetInetSocket",                'i', "iii"  },
	{0X4A114C7C, &WrapI_IIIUU<sceNetInetGetsockopt>, "sceNetInetGetsockopt",            'i', "iiixx"},
	{0X2FE71FE7, &WrapI_IIIUI<sceNetInetSetsockopt>, "sceNetInetSetsockopt",            'i', "iiixi"},
	{0X410B34AA, &WrapI_IUI<sceNetInetConnect>,      "sceNetInetConnect",               'i', "ixi"  },
	{0X805502DD, nullptr,                            "sceNetInetCloseWithRST",          '?', ""     },
	{0XD10A1A7A, &WrapI_II<sceNetInetListen>,        "sceNetInetListen",                '?', ""     },
	{0XDB094E1B, &WrapI_IUU<sceNetInetAccept>,       "sceNetInetAccept",                '?', ""     },
	{0XFAABB1DD, &WrapI_VUI<sceNetInetPoll>,         "sceNetInetPoll",                  'i', "pxi"  },
	{0X5BE8D595, &WrapI_IUUUU<sceNetInetSelect>,     "sceNetInetSelect",                'i', "ixxxx"},
	{0X8D7284EA, &WrapI_I<sceNetInetClose>,          "sceNetInetClose",                 '?', ""     },
	{0XCDA85C99, &WrapI_IUUI<sceNetInetRecv>,        "sceNetInetRecv",                  'i', "ixxi" },
	{0XC91142E4, &WrapI_IUUIUU<sceNetInetRecvfrom>,  "sceNetInetRecvfrom",              'i', "ixxxxx"},
	{0XEECE61D2, nullptr,                            "sceNetInetRecvmsg",               '?', ""     },
	{0X7AA671BC, &WrapI_IUUI<sceNetInetSend>,        "sceNetInetSend",                  'i', "ixxx" },
	{0X05038FC7, &WrapI_IUUIUU<sceNetInetSendto>,    "sceNetInetSendto",                'i', "ixxxxx"},
	{0X774E36F4, nullptr,                            "sceNetInetSendmsg",               '?', ""     },
	{0XFBABE411, &WrapI_V<sceNetInetGetErrno>,       "sceNetInetGetErrno",              'i', ""     },
	{0X1A33F9AE, &WrapI_IUU<sceNetInetBind>,         "sceNetInetBind",                  'i', ""     },
	{0XB75D5B0A, &WrapU_C<sceNetInetInetAddr>,       "sceNetInetInetAddr",              'u', "p"    },
	{0X1BDF5D13, &WrapI_CU<sceNetInetInetAton>,      "sceNetInetInetAton",              'i', "sx"   },
	{0XD0792666, &WrapU_IUUU<sceNetInetInetNtop>,    "sceNetInetInetNtop",              '?', ""     },
	{0XE30B8C19, &WrapI_ICU<sceNetInetInetPton>,     "sceNetInetInetPton",              '?', ""     },
	{0X8CA3A97E, nullptr,                            "sceNetInetGetPspError",           '?', ""     },
	{0XE247B6D6, &WrapI_IUU<sceNetInetGetpeername>,"sceNetInetGetpeername",           '?', ""     },
	{0X162E6FD5, &WrapI_IUU<sceNetInetGetsockname>,  "sceNetInetGetsockname",           '?', ""     },
	{0X80A21ABD, nullptr,                            "sceNetInetSocketAbort",           '?', ""     },
	{0X39B0C7D3, nullptr,                            "sceNetInetGetUdpcbstat",          '?', ""     },
	{0XB3888AD4, nullptr,                            "sceNetInetGetTcpcbstat",          '?', ""     },
};

std::shared_ptr<SceNetInet> SceNetInet::gInstance;
std::shared_mutex SceNetInet::gLock;

bool SceNetInet::Init() {
	auto lock = std::unique_lock(gLock);
	if (gInstance) {
		return false;
	}
	gInstance = std::make_shared<SceNetInet>();
	return true;
}

bool SceNetInet::Shutdown() {
	auto lock = std::unique_lock(gLock);
	if (!gInstance) {
		return false;
	}
	gInstance->CloseAllRemainingSockets();
	gInstance = nullptr;
	return true;
}

int SceNetInet::GetLastError() {
	auto lock = std::shared_lock(mLock);
	return mLastError;
}

void SceNetInet::SetLastError(const int error) {
	auto lock = std::unique_lock(mLock);
	mLastError = error;
}

// TODO: ensure this is applied to every function
int SceNetInet::SetLastErrorToMatchPlatform() {
	int error;
#if PPSSPP_PLATFORM(WINDOWS)
	error = WSAGetLastError();
#else
	error = errno;
#endif
	SetLastError(error);
	return error;
}

std::shared_ptr<SceSocket> SceNetInet::CreateAndAssociateSceSocket(int nativeSocketId) {
	auto lock = std::unique_lock(mLock);

	int sceSocketId = ++mCurrentSceSocketId;
	if (const auto it = mSceSocketIdToNativeSocket.find(sceSocketId); it != mSceSocketIdToNativeSocket.end()) {
		WARN_LOG(SCENET, "%s: Attempted to re-associate socket from already-associated sceSocketId: %i", __func__, sceSocketId);
		return nullptr;
	}
	auto sceSocket = std::make_shared<SceSocket>(sceSocketId, nativeSocketId);
	mSceSocketIdToNativeSocket.emplace(sceSocketId, sceSocket);
	return sceSocket;
}

std::shared_ptr<SceSocket> SceNetInet::GetSceSocket(int sceSocketId) {
	auto lock = std::shared_lock(mLock);

	const auto it = mSceSocketIdToNativeSocket.find(sceSocketId);
	if (it == mSceSocketIdToNativeSocket.end()) {
		WARN_LOG(SCENET, "%s: Attempted to get unassociated socket from sceSocketId: %i", __func__, sceSocketId);
		return nullptr;
	}

	return it->second;
}

bool SceNetInet::GetNativeSocketIdForSceSocketId(int& nativeSocketId, int sceSocketId) {
	const auto sceSocket = GetSceSocket(sceSocketId);
	if (!sceSocket)
		return false;
	nativeSocketId = sceSocket->GetNativeSocketId();
	return true;
}

bool SceNetInet::EraseNativeSocket(int sceSocketId) {
	auto lock = std::unique_lock(mLock);

	const auto it = mSceSocketIdToNativeSocket.find(sceSocketId);
	if (it == mSceSocketIdToNativeSocket.end()) {
		WARN_LOG(SCENET, "%s: Attempted to delete unassociated socket from sceSocketId: %i", __func__, sceSocketId);
		return false;
	}
	mSceSocketIdToNativeSocket.erase(it);
	return true;
}

bool SceNetInet::TranslateSceFdSetToNativeFdSet(int &maxFd, fd_set& destFdSet, u32 fdsPtr) const {
	if (fdsPtr == 0) {
		// Allow nullptr to be used without failing
		return true;
	}

	FD_ZERO(&destFdSet);
	const auto sceFdSet = Memory::GetTypedPointerRange<SceFdSetOperations::FdSet>(fdsPtr, sizeof(SceFdSetOperations::FdSet));
	if (sceFdSet == nullptr) {
		ERROR_LOG(SCENET, "%s: Invalid fdsPtr %08x", __func__, fdsPtr);
		return false;
	}

	int setSize = 0;
	for (auto& it : mSceSocketIdToNativeSocket) {
		const auto sceSocket = it.first;
		const auto nativeSocketId = it.second->GetNativeSocketId();
		if (nativeSocketId + 1 > maxFd) {
			maxFd = nativeSocketId + 1;
		}
		if (SceFdSetOperations::IsSet(sceFdSet, sceSocket)) {
			if (++setSize > FD_SETSIZE) {
				ERROR_LOG(SCENET, "%s: Encountered input FD_SET which is greater than max supported size %i", __func__, setSize);
				return false;
			}
			DEBUG_LOG(SCENET, "%s: Translating input %i into %i", __func__, sceSocket, nativeSocketId);
			FD_SET(nativeSocketId, &destFdSet);
		}
	}

	DEBUG_LOG(SCENET, "%s: Translated %i sockets", __func__, setSize);
	return true;
}

void SceNetInet::CloseAllRemainingSockets() const {
	for (auto& it : mSceSocketIdToNativeSocket) {
		if (!it.second) {
			continue;
		}
		close(it.second->GetNativeSocketId());
	}
}

void Register_sceNetInet() {
	RegisterModule("sceNetInet", ARRAY_SIZE(sceNetInet), sceNetInet);
}

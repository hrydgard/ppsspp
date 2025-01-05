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
#include "Core/Net/InetSocket.h"
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

/**
 * @file sceNetInet.cpp
 *
 * @brief A shim for SceNetInet functions to operate over native POSIX sockets. These POSIX socket implementations are
 * largely standarized and consistent between platforms with the exception of Windows which uses Winsock2 which itself
 * is similar enough.
 *
 * Glossary:
 *  - Inet: Anything with is prefaced with Inet, regardless of case, is the PSP variant of the function / structure / etc.
 *
 * Standards:
 * - C++-style implementations are preferred over C-style implementations when applicable (see SceFdSetOperations) for
 *   an example which implements fd_set and FD_* functions using C++-style code.
 * - The last error is implemented within SceNetInet itself and is not a direct passthrough from the platforms errno
 *   implementation.
 *   - Invalid arguments (unmapped sockets, invalid options / flags etc) are mapped to EINVAL.
 *   - Invalid memory regions are mapped to EFAULT.
 * - hleLogError should be used to return errors.
 * - hleLogSuccess* should be used to return success, with optional messages. These message formattings have > 0
 *   overhead so be mindful of their usage.
 * - SceNetInet must have SetLastError called in all error cases <b>except</b> when SceNetInet itself is not initialized.
 * - DEBUG_LOG should be used where it makes sense.
 * - Comments must be left to indicate the intent of each section of each function. The comments should be short and
 *   concise while not mentioning any specific games or similar in particular. Mention the constraints that came from
 *   the game.
 * - Explicit mappings should be used over implicit passthrough. Cases which are not known to work for PSP should be
 *   mapped to an EINVAL error unless it is demonstrated to work as expected.
 * - It should not be possible for a game to crash the application via any shim; think what would happen if every
 *   parameter is randomized.
 */

struct PspInetTimeval {
	u32 tv_sec;		/* Seconds.  */
	u32 tv_usec;	/* Microseconds.  */
};

class PspInetFdSetOperations {
public:
	typedef long int fdMask;
	static constexpr int gFdsBitsCount = 8 * static_cast<int>(sizeof(fdMask));

	struct FdSet {
		fdMask mFdsBits[256 / gFdsBitsCount];
	};

	static void Set(FdSet &sceFdSetBits, int socket) {
		sceFdSetBits.mFdsBits[Position(socket)] |= ConvertToMask(socket);
	}

	static bool IsSet(const FdSet &sceFdSetBits, int socket) {
		return (sceFdSetBits.mFdsBits[Position(socket)] & ConvertToMask(socket)) != 0;
	}

	static void Clear(FdSet &sceFdSetBits, int socket) {
		sceFdSetBits.mFdsBits[Position(socket)] &= ~ConvertToMask(socket);
	}

	static void Zero(FdSet &sceFdSetBits) {
		memset(sceFdSetBits.mFdsBits, 0, sizeof(FdSet));
	}

private:
	static int Position(const int socket) {
		return socket / gFdsBitsCount;
	}

	static int ConvertToMask(const int socket) {
		return static_cast<fdMask>(1UL << (socket % gFdsBitsCount));
	}
};

static bool inetSockaddrToNativeSocketAddr(sockaddr_in &dest, u32 sockAddrInternetPtr, size_t addressLength) {
	const auto inetSockaddrIn = Memory::GetTypedPointerRange<SceNetInetSockaddrIn>(sockAddrInternetPtr, (u32)addressLength);
	if (inetSockaddrIn == nullptr || addressLength == 0) {
		return false;
	}

	// Clear dest of any existing data and copy relevant fields from inet sockaddr_in
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = inetSockaddrIn->sin_family;
	dest.sin_port = inetSockaddrIn->sin_port;
	dest.sin_addr.s_addr = inetSockaddrIn->sin_addr;
	DEBUG_LOG(Log::sceNet, "sceSockaddrToNativeSocketAddr: Family %i, port %i, addr %s, len %i", dest.sin_family, ntohs(dest.sin_port), ip2str(dest.sin_addr, false).c_str(), inetSockaddrIn->sin_len);
	return true;
}

static bool writeSockAddrInToInetSockAddr(u32 destAddrPtr, u32 destAddrLenPtr, sockaddr_in src) {
	const auto sceNetSocklenPtr = reinterpret_cast<u32*>(Memory::GetPointerWrite(destAddrLenPtr));
	u32 sceNetSocklen = 0;
	if (sceNetSocklenPtr != nullptr) {
		sceNetSocklen = *sceNetSocklenPtr;
	}
	const auto sceNetSockaddrIn = Memory::GetTypedPointerWriteRange<SceNetInetSockaddrIn>(destAddrPtr, sceNetSocklen);
	if (sceNetSockaddrIn == nullptr) {
		return false;
	}
	DEBUG_LOG(Log::sceNet, "writeSockAddrInToSceSockAddr size: %d vs %d", (int)sizeof(SceNetInetSockaddrIn), sceNetSocklen);
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
	return ioctlsocket(nativeSocketId, FIONBIO, &val);
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
	ERROR_LOG(Log::sceNet, "UNTESTED sceNetInetInit()");
	return SceNetInet::Init() ? 0 : ERROR_NET_INET_ALREADY_INITIALIZED;
}

int sceNetInetTerm() {
	ERROR_LOG(Log::sceNet, "UNTESTED sceNetInetTerm()");
	SceNetInet::Shutdown();
	return hleLogSuccessI(Log::sceNet, 0);
}

static int sceNetInetSocket(int inetAddressFamily, int inetType, int inetProtocol) {
	WARN_LOG_ONCE(sceNetInetSocket, Log::sceNet, "UNTESTED sceNetInetSocket(%i, %i, %i)", inetAddressFamily, inetType, inetProtocol);
	auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	// Translate address family, type, and protocol. There is some complexity around the type in particular where
	// flags are able to be encoded in the most significant bits.

	int nativeAddressFamily;
	if (!SceNetInet::TranslateInetAddressFamilyToNative(nativeAddressFamily, inetAddressFamily)) {
		sceNetInet->SetLastError(EAFNOSUPPORT);
		return hleLogError(Log::sceNet, -1, "%s: Unable to translate inet address family %i", __func__, inetAddressFamily);
	}

	int nativeType;
	bool nonBlocking;
	if (!SceNetInet::TranslateInetSocketTypeToNative(nativeType, nonBlocking, inetType)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(Log::sceNet, -1, "%s: Unable to translate inet type %08x", __func__, inetType);
	}

	int nativeProtocol;
	if (!SceNetInet::TranslateInetProtocolToNative(nativeProtocol, inetProtocol)) {
		sceNetInet->SetLastError(EPROTONOSUPPORT);
		return hleLogError(Log::sceNet, -1, "%s: Unable to translate inet protocol %i", __func__, inetProtocol);
	}

	// Attempt to open socket
	const int nativeSocketId = socket(nativeAddressFamily, nativeType, nativeProtocol);
	if (nativeSocketId < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(Log::sceNet, -1, "%s: Unable to open socket(%i, %i, %i) with error %i: %s", __func__, nativeAddressFamily, nativeType, nativeProtocol, error, strerror(error));
	}

	// Map opened socket to an inet socket which is 1-indexed
	const auto inetSocket = sceNetInet->CreateAndAssociateInetSocket(nativeSocketId, nativeProtocol, nonBlocking);

	// Set non-blocking mode since the translation function does not translate non-blocking mode due to platform incompatibilities
	if (nonBlocking) {
		setBlockingMode(nativeSocketId, true);
	}

	// Close opened socket if such a socket exists
	if (!inetSocket) {
		close(nativeSocketId);
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, ERROR_NET_INET_INVALID_ARG, "%s: Unable to create new InetSocket for native socket id %i, closing", __func__, nativeSocketId);
	}
	return inetSocket->GetInetSocketId();
}

static int sceNetInetGetsockopt(int socket, int inetSocketLevel, int inetOptname, u32 optvalPtr, u32 optlenPtr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetInetGetsockopt(%i, %i, %i, %08x, %08x)", socket, inetSocketLevel, inetOptname, optvalPtr, optlenPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const auto nativeSocketId = inetSocket->GetNativeSocketId();

	int nativeSocketLevel;
	if (!SceNetInet::TranslateInetSocketLevelToNative(nativeSocketLevel, inetSocketLevel)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Unknown socket level %04x", nativeSocketId, __func__, inetSocketLevel);
	}

	int nativeOptname;
	if (!SceNetInet::TranslateInetOptnameToNativeOptname(nativeOptname, inetOptname)) {
		sceNetInet->SetLastError(ENOPROTOOPT);
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Unknown optname %04x", inetOptname);
	}

	if (nativeOptname != inetOptname) {
		DEBUG_LOG(Log::sceNet, "sceNetInetSetsockopt: Translated optname %04x into %04x", inetOptname, nativeOptname);
	}

	socklen_t *optlen = reinterpret_cast<socklen_t *>(Memory::GetPointerWrite(optlenPtr));
	if (!optlen) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Invalid pointer %08x", nativeSocketId, __func__, optlenPtr);
	}

	const auto optval = Memory::GetTypedPointerWriteRange<netBufferType>(optvalPtr, *optlen);
	if (optval == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Invalid pointer range %08x (size %i)", nativeSocketId, __func__, optvalPtr, *optlen);
	}

	switch (nativeOptname) {
		// No direct equivalents
		case INET_SO_NONBLOCK: {
			if (*optlen != sizeof(u32)) {
				sceNetInet->SetLastError(EFAULT);
				return hleLogError(Log::sceNet, -1, "[%i] %s: Invalid optlen %i for INET_SO_NONBLOCK", nativeSocketId, __func__, *optlen);
			}
			Memory::Write_U32(optvalPtr, inetSocket->IsNonBlocking() ? 1 : 0);
			return hleLogSuccessI(Log::sceNet, 0);
		}
		// Direct 1:1 mappings
		default: {
			// TODO: implement non-blocking getsockopt
			const int ret = getsockopt(nativeSocketId, nativeSocketLevel, nativeOptname, optval, optlen);
			if (ret < 0) {
				const auto error = sceNetInet->SetLastErrorToMatchPlatform();
				return hleLogError(Log::sceNet, ret, "[%i] %s: returned error %i: %s", nativeSocketId, __func__, error, strerror(error));
			}
			return hleLogSuccessI(Log::sceNet, ret);
		}
	}
}

static int sceNetInetSetsockopt(int socket, int inetSocketLevel, int inetOptname, u32 optvalPtr, int optlen) {
	WARN_LOG_ONCE(sceNetInetSetsockopt, Log::sceNet, "UNTESTED sceNetInetSetsockopt(%i, %i, %i, %08x, %i)", socket, inetSocketLevel, inetOptname, optvalPtr, optlen);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const auto nativeSocketId = inetSocket->GetNativeSocketId();

	int nativeSocketLevel;
	if (!SceNetInet::TranslateInetSocketLevelToNative(nativeSocketLevel, inetSocketLevel)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Unknown socket level %04x", nativeSocketId, __func__, inetSocketLevel);
	}

	int nativeOptname;
	if (!SceNetInet::TranslateInetOptnameToNativeOptname(nativeOptname, inetOptname)) {
		sceNetInet->SetLastError(ENOPROTOOPT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Unknown optname %04x", nativeSocketId, __func__, inetOptname);
	}

	if (nativeOptname != inetOptname) {
		DEBUG_LOG(Log::sceNet, "sceNetInetSetsockopt: Translated optname %04x into %04x", inetOptname, nativeOptname);
	}

	// If optlens of != sizeof(u32) are created, split out the handling into separate functions for readability
	if (optlen != sizeof(u32)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(Log::sceNet, -1, "[%i]: %s: Unhandled optlen %i for optname %04x", nativeSocketId, __func__, optlen, inetOptname);
	}

	if (!Memory::IsValidAddress(optvalPtr)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i]: %s: Invalid address %08x for optval", nativeSocketId, __func__, optvalPtr);
	}

	auto optval = Memory::Read_U32(optvalPtr);
	DEBUG_LOG(Log::sceNet, "[%i] setsockopt(%i, %i, %i, %i)", nativeSocketId, nativeSocketId, nativeSocketLevel, nativeOptname, optval);

	switch (nativeOptname) {
		// Unmatched PSP functions - no direct equivalent
		case INET_SO_NONBLOCK: {
			const bool nonblocking = optval != 0;
			inetSocket->SetNonBlocking(nonblocking);
			INFO_LOG(Log::sceNet, "[%i] setsockopt_u32: Set non-blocking=%i", nativeSocketId, nonblocking);
			if (setBlockingMode(nativeSocketId, nonblocking) != 0) {
				const auto error = sceNetInet->SetLastErrorToMatchPlatform();
				ERROR_LOG(Log::sceNet, "[%i] %s: Failed to set to non-blocking with error %i: %s", nativeSocketId, __func__, error, strerror(error));
			}
			return hleLogSuccessI(Log::sceNet, 0);
		}
		// Functions with identical structs to native functions
		default: {
			INFO_LOG(Log::sceNet, "UNTESTED sceNetInetSetsockopt(%i, %i, %i, %u, %i)", nativeSocketId, nativeSocketLevel, nativeOptname, optval, 4);
			const int ret = setsockopt(nativeSocketId, nativeSocketLevel, nativeOptname, reinterpret_cast<netBufferType*>(&optval), sizeof(optval));
			INFO_LOG(Log::sceNet, "setsockopt_u32: setsockopt returned %i for %i", ret, nativeSocketId);
			if (ret < 0) {
				const auto error = sceNetInet->SetLastErrorToMatchPlatform();
				return hleLogError(Log::sceNet, ret, "[%i] %s: Failed to set optname %04x to %08x with error %i: %s", nativeSocketId, __func__, nativeOptname, optval, error, strerror(error));
			}
			return hleLogSuccessI(Log::sceNet, ret);
		}
	}
}

static int sceNetInetConnect(int socket, u32 sockAddrInternetPtr, int addressLength) {
	WARN_LOG_ONCE(sceNetInetConnect, Log::sceNet, "UNTESTED sceNetInetConnect(%i, %08x, %i, %i)", socket, sockAddrInternetPtr, Memory::Read_U32(sockAddrInternetPtr), addressLength);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForInetSocketId(nativeSocketId, socket)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	// Translate inet sockaddr to native sockaddr
	sockaddr_in convertedSockaddr{};
	if (!inetSockaddrToNativeSocketAddr(convertedSockaddr, sockAddrInternetPtr, addressLength)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Error translating sceSockaddr to native sockaddr", nativeSocketId, __func__);
	}

	DEBUG_LOG(Log::sceNet, "[%i] sceNetInetConnect: Connecting to %s on %i", nativeSocketId, ip2str(convertedSockaddr.sin_addr, false).c_str(), ntohs(convertedSockaddr.sin_port));

	// Attempt to connect using translated sockaddr
	int ret = connect(nativeSocketId, reinterpret_cast<sockaddr*>(&convertedSockaddr), sizeof(convertedSockaddr));
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(Log::sceNet, ret, "[%i] %s: Error connecting %i: %s", nativeSocketId, __func__, error, strerror(error));
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetListen(int socket, int backlog) {
	WARN_LOG_ONCE(sceNetInetListen, Log::sceNet, "UNTESTED %s(%i, %i)", __func__, socket, backlog);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForInetSocketId(nativeSocketId, socket)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	// Map PSP_NET_INET_SOMAXCONN (128) to platform SOMAXCONN
	if (backlog == PSP_NET_INET_SOMAXCONN) {
		backlog = SOMAXCONN;
	}

	// Attempt to listen using either backlog, or SOMAXCONN as per above
	const int ret = listen(socket, backlog);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(Log::sceNet, ret, "[%i] %s: Error listening %i: %s", nativeSocketId, __func__, error, strerror(error));
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetAccept(int socket, u32 addrPtr, u32 addrLenPtr) {
	WARN_LOG_ONCE(sceNetInetListen, Log::sceNet, "UNTESTED %s(%i, %08x, %08x)", __func__, socket, addrPtr, addrLenPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForInetSocketId(nativeSocketId, socket)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	// Attempt to accept a connection which will provide us with a sockaddrIn containing remote connection details
	sockaddr_in sockaddrIn{};
	socklen_t socklen;
	const int ret = accept(nativeSocketId, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);
	if (ret < 0) {
		// Ensure that ERROR_WHEN_NONBLOCKING_CALL_OCCURS is not mapped to an hleLogError
		if (const auto error = sceNetInet->SetLastErrorToMatchPlatform();
			error != ERROR_WHEN_NONBLOCKING_CALL_OCCURS) {
			hleLogError(Log::sceNet, ret, "[%i] %s: Encountered error %i: %s", nativeSocketId, __func__, error, strerror(error));
		}
		return hleLogSuccessI(Log::sceNet, ret);
	}

	// Don't call writeSockAddrInToInetSockAddr when addrPtr is 0, otherwise do and send false to EFAULT
	if (addrPtr != 0 && !writeSockAddrInToInetSockAddr(addrPtr, addrLenPtr, sockaddrIn)) {
		sceNetInet->SetLastError(EFAULT);
		hleLogError(Log::sceNet, ret, "[%i] %s: Encountered error trying to write to addrPtr, probably invalid memory range", nativeSocketId, __func__);
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

int sceNetInetPoll(void *fds, u32 nfds, int timeout) { // timeout in miliseconds
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetInetPoll(%p, %d, %i) at %08x", fds, nfds, timeout, currentMIPS->pc);
	const auto fdarray = static_cast<SceNetInetPollfd*>(fds); // SceNetInetPollfd/pollfd, sceNetInetPoll() have similarity to BSD poll() but pollfd have different size on 64bit
//#ifdef _WIN32
	//WSAPoll only available for Vista or newer, so we'll use an alternative way for XP since Windows doesn't have poll function like *NIX
	if (nfds > FD_SETSIZE) {
		ERROR_LOG(Log::sceNet, "sceNetInetPoll: nfds=%i is greater than FD_SETSIZE=%i, unable to poll", nfds, FD_SETSIZE);
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
	WARN_LOG_ONCE(sceNetInetSelect, Log::sceNet, "UNTESTED sceNetInetSelect(%i, %08x, %08x, %08x, %08x)", maxfd, readFdsPtr, writeFdsPtr, exceptFdsPtr, timeoutPtr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	// Translate all input fd_sets to native fd_sets. None of these will be nullptr and so this needs to be checked later.
	int recomputedMaxFd = 1;
	fd_set readFds;
	sceNetInet->TranslateInetFdSetToNativeFdSet(recomputedMaxFd, readFds, readFdsPtr);
	fd_set writeFds;
	sceNetInet->TranslateInetFdSetToNativeFdSet(recomputedMaxFd, writeFds, writeFdsPtr);
	fd_set exceptFds;
	sceNetInet->TranslateInetFdSetToNativeFdSet(recomputedMaxFd, exceptFds, exceptFdsPtr);

	// Convert timeval when applicable
	timeval tv{};
	if (timeoutPtr != 0) {
		const auto inetTimeval = Memory::GetTypedPointerRange<PspInetTimeval>(timeoutPtr, sizeof(PspInetTimeval));
		if (inetTimeval != nullptr) {
			tv.tv_sec = inetTimeval->tv_sec;
			tv.tv_usec = inetTimeval->tv_usec;
			DEBUG_LOG(Log::sceNet, "%s: Timeout seconds=%lu, useconds=%lu", __func__, tv.tv_sec, tv.tv_usec);
		} else {
			WARN_LOG(Log::sceNet, "%s: Encountered invalid timeout value, continuing anyway", __func__);
		}
	}

	// Since the fd_set structs are allocated on the stack (and always so), only pass in their pointers if the input pointer is non-null
	const int ret = select(recomputedMaxFd,  readFdsPtr != 0 ? &readFds : nullptr, writeFdsPtr != 0 ? &writeFds : nullptr,  exceptFdsPtr != 0 ? &exceptFds : nullptr, timeoutPtr != 0 ? &tv : nullptr);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		ERROR_LOG(Log::sceNet, "%s: Received error from select() %i: %s", __func__, error, strerror(error));
	}

	INFO_LOG(Log::sceNet, "%s: select() returned %i", __func__, ret);
	return hleDelayResult(ret, "TODO: unhack", 500);
}

static int sceNetInetClose(int socket) {
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogWarning(Log::sceNet, -1, "%s: Attempting to close socket %i which does not exist", __func__, socket);
	}

	const int ret = close(inetSocket->GetNativeSocketId());
	if (!sceNetInet->EraseNativeSocket(socket)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "%s: Unable to clear mapping of inetSocketId->nativeSocketId, was there contention?", __func__);
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static u32 sceNetInetInetAddr(const char *hostname) {
	ERROR_LOG(Log::sceNet, "UNTESTED sceNetInetInetAddr(%s)", hostname);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	in_addr inAddr{};
#if PPSSPP_PLATFORM(WINDOWS)
	const int ret = inet_pton(AF_INET, hostname, &inAddr);
#else
	const int ret = inet_aton(hostname, &inAddr);
#endif
	if (ret != 0) {
		sceNetInet->SetLastErrorToMatchPlatform();
		return inAddr.s_addr;
	}

	// TODO: Should this return ret or inAddr.sAddr? Conflicting info between the two PRs!

	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetInetAton(const char *hostname, u32 addrPtr) {
	ERROR_LOG(Log::sceNet, "UNTESTED %s(%s, %08x)", __func__, hostname, addrPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	if (hostname == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "%s: Invalid hostname: %08x", __func__, hostname);
	}

	if (!Memory::IsValidAddress(addrPtr)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "%s: Invalid addrPtr: %08x", __func__, addrPtr);
	}

	// Convert the input hostname into an inaddr
	in_addr inAddr{};
#if PPSSPP_PLATFORM(WINDOWS)
	// Use inet_pton to accomplish the same behavior on Winsock2 which is missing inet_aton
	const int ret = inet_pton(AF_INET, hostname, &inAddr);
#else
	const int ret = inet_aton(hostname, &inAddr);
#endif

	if (ret == 0) {
		// inet_aton does not set errno when an error occurs, so neither should we
		return hleLogError(Log::sceNet, ret, "%s: Invalid hostname %s", __func__, hostname);
	}

	// Write back to addrPtr if ret is != 0
	Memory::Write_U32(inAddr.s_addr, addrPtr);
	return hleLogSuccessI(Log::sceNet, ret);
}

static u32 sceNetInetInetNtop(int inetAddressFamily, u32 srcPtr, u32 dstBufPtr, u32 dstBufSize) {
	WARN_LOG_ONCE(sceNetInetInetNtop, Log::sceNet, "UNTESTED %s(%i, %08x, %08x, %i)", __func__, inetAddressFamily, srcPtr, dstBufPtr, dstBufSize);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto srcSockaddrIn = Memory::GetTypedPointerWriteRange<SceNetInetSockaddrIn>(srcPtr, sizeof(SceNetInetSockaddrIn));
	if (srcSockaddrIn == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, 0, "%s: Invalid memory range for srcPtr %08x", __func__, srcPtr);
	}

	const auto dstBuf = Memory::GetTypedPointerWriteRange<char>(dstBufPtr, dstBufSize);
	if (dstBuf == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, 0, "%s: Invalid memory range for dstBufPtr %08x, size %i", __func__, dstBufPtr, dstBufSize);
	}

	if (!dstBufSize) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, 0, "%s: dstBufSize must be > 0", __func__);
	}

	int nativeAddressFamily;
	if (!SceNetInet::TranslateInetAddressFamilyToNative(nativeAddressFamily, inetAddressFamily)) {
		sceNetInet->SetLastError(EAFNOSUPPORT);
		return hleLogError(Log::sceNet, 0, "%s: Unknown address family %04x", __func__, inetAddressFamily);
	}

	if (inet_ntop(nativeAddressFamily, reinterpret_cast<netBufferType*>(srcSockaddrIn), dstBuf, dstBufSize) == nullptr) {
		// Allow partial output in case it's desired for some reason
	}
	return hleLogSuccessX(Log::sceNet, dstBufPtr);
}

static int sceNetInetInetPton(int inetAddressFamily, const char *hostname, u32 dstPtr) {
	WARN_LOG_ONCE(sceNetInetInetPton, Log::sceNet, "UNTESTED %s(%i, %s, %08x)", __func__, inetAddressFamily, hostname, dstPtr);

	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	if (hostname == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, 0, "%s: Invalid memory range for hostname %08x", __func__, hostname);
	}

	// IPv4, the only supported address family on PSP, will always be 32 bits
	const auto dst = Memory::GetTypedPointerWriteRange<u32>(dstPtr, sizeof(u32));
	if (dst == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, 0, "%s: Invalid memory range for dstPtr %08x, size %i", __func__, dstPtr, sizeof(u32));
	}

	// Translate inet address family to native
	int nativeAddressFamily;
	if (!SceNetInet::TranslateInetAddressFamilyToNative(nativeAddressFamily, inetAddressFamily)) {
		sceNetInet->SetLastError(EAFNOSUPPORT);
		return hleLogError(Log::sceNet, 0, "%s: Unknown address family %04x", __func__, inetAddressFamily);
	}

	const int ret = inet_pton(inetAddressFamily, hostname, dst);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(Log::sceNet, ret, "%s: inet_pton returned %i: %s", __func__, sceNetInet->GetLastError(), strerror(error));
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetGetpeername(int socket, u32 addrPtr, u32 addrLenPtr) {
	ERROR_LOG(Log::sceNet, "UNTESTED sceNetInetGetsockname(%i, %08x, %08x)", socket, addrPtr, addrLenPtr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForInetSocketId(nativeSocketId, socket)) {
		ERROR_LOG(Log::sceNet, "%s: Requested socket %i which does not exist", __func__, socket);
		return -1;
	}

	// Write PSP sockaddr to native sockaddr in preparation of getpeername
	sockaddr_in sockaddrIn{};
	socklen_t socklen = sizeof(sockaddr_in);
	if (!inetSockaddrToNativeSocketAddr(sockaddrIn, addrPtr, addrLenPtr)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i]: %s: Encountered invalid addrPtr %08x and/or invalid addrLenPtr %08x", nativeSocketId, addrPtr, addrLenPtr);
	}

	const int ret = getpeername(nativeSocketId, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(Log::sceNet, ret, "[%i] %s: Failed to execute getpeername %i: %s", nativeSocketId, __func__, error, strerror(error));
	}

	// Write output of getpeername to the input addrPtr
	if (!writeSockAddrInToInetSockAddr(addrPtr, addrLenPtr, sockaddrIn)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Failed to write results of getpeername to SceNetInetSockaddrIn", nativeSocketId, __func__);
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetGetsockname(int socket, u32 addrPtr, u32 addrLenPtr) {
	ERROR_LOG(Log::sceNet, "UNTESTED %s(%i, %08x, %08x)", __func__, socket, addrPtr, addrLenPtr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	int nativeSocketId;
	if (!sceNetInet->GetNativeSocketIdForInetSocketId(nativeSocketId, socket)) {
		sceNetInet->SetLastError(EINVAL);
		return hleLogError(Log::sceNet, -1, "%s: Requested socket %i which does not exist", __func__, socket);
	}

	// Set sockaddrIn to the result of getsockname
	sockaddr_in sockaddrIn{};
	socklen_t socklen = sizeof(sockaddr_in);
	const int ret = getsockname(nativeSocketId, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(Log::sceNet, ret, "[%i] %s: Failed to execute getsockname %i: %s", nativeSocketId, __func__, error, strerror(error));
	}

	// Write output of getsockname to the input addrPtr
	if (!writeSockAddrInToInetSockAddr(addrPtr, addrLenPtr, sockaddrIn)) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Failed to write results of getsockname to SceNetInetSockaddrIn", nativeSocketId, __func__);
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetRecv(int socket, u32 bufPtr, u32 bufLen, int flags) {
	WARN_LOG_ONCE(sceNetInetRecv, Log::sceNet, "UNTESTED sceNetInetRecv(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to close socket %i which does not exist", __func__, socket);
	}

	const auto nativeSocketId = inetSocket->GetNativeSocketId();
	const auto dstBuf = Memory::GetTypedPointerWriteRange<netBufferType>(bufPtr, bufLen);
	if (dstBuf == nullptr) {
		return hleLogError(Log::sceNet, -1, "[%i] %s: Invalid pointer %08x (size %i)", nativeSocketId, __func__, bufPtr, bufLen);
	}

	const int nativeFlags = SceNetInet::TranslateInetFlagsToNativeFlags(flags, inetSocket->IsNonBlocking());
	const int ret = recv(nativeSocketId, dstBuf, bufLen, nativeFlags);
	DEBUG_LOG(Log::sceNet, "[%i] %s: Called recv with buf size %i which returned %i", nativeSocketId, __func__, bufLen, ret);
	if (ret < 0) {
		if (const auto error = sceNetInet->SetLastErrorToMatchPlatform();
			error != ERROR_WHEN_NONBLOCKING_CALL_OCCURS) {
			ERROR_LOG(Log::sceNet, "[%i]: %s: recv() encountered error %i: %s", socket, __func__, error, strerror(error));
		}
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetRecvfrom(int socket, u32 bufPtr, u32 bufLen, int flags, u32 fromAddr, u32 fromLenAddr) {
	WARN_LOG_ONCE(sceNetInetRecvFrom, Log::sceNet, "UNTESTED sceNetInetRecvfrom(%i, %08x, %i, %08x, %08x, %08x)", socket, bufPtr, bufLen, flags, fromAddr, fromLenAddr);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const auto nativeSocketId = inetSocket->GetNativeSocketId();
	const auto dstBuf = Memory::GetTypedPointerWriteRange<netBufferType>(bufPtr, bufLen);
	if (dstBuf == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Invalid pointer range: %08x (size %i)", nativeSocketId, __func__, bufPtr, bufLen);
	}

	// Translate PSP flags to native flags and prepare sockaddrIn to receive peer address
	const int nativeFlags = SceNetInet::TranslateInetFlagsToNativeFlags(flags, inetSocket->IsNonBlocking());
	sockaddr_in sockaddrIn{};
	socklen_t socklen = sizeof(sockaddr_in);
	Memory::Memset(bufPtr, 0, bufLen, __func__);

	const int ret = recvfrom(nativeSocketId, dstBuf, bufLen, nativeFlags, reinterpret_cast<sockaddr*>(&sockaddrIn), &socklen);
	if (ret < 0) {
		if (const auto error = sceNetInet->SetLastErrorToMatchPlatform();
			error != 0 && error != ERROR_WHEN_NONBLOCKING_CALL_OCCURS) {
			WARN_LOG(Log::sceNet, "[%i] %s: Received error %i: %s", nativeSocketId, __func__, error, strerror(error));
		}
		return hleDelayResult(ret, "TODO: unhack", 500);
	}

	// If ret was successful, write peer sockaddr to input fromAddr
	if (ret > 0) {
		if (!writeSockAddrInToInetSockAddr(fromAddr, fromLenAddr, sockaddrIn)) {
			ERROR_LOG(Log::sceNet, "[%i] %s: Error writing native sockaddr to sceSockaddr", nativeSocketId, __func__);
		}
		INFO_LOG(Log::sceNet, "[%i] %s: Got %i bytes from recvfrom", nativeSocketId, __func__, ret);
	}
	return hleDelayResult(ret, "TODO: unhack", 500);
}

static int sceNetInetSend(int socket, u32 bufPtr, u32 bufLen, int flags) {
	WARN_LOG_ONCE(sceNetInetSend, Log::sceNet, "UNTESTED %s(%i, %08x, %i, %08x)", __func__, socket, bufPtr, bufLen, flags);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const auto buf = Memory::GetTypedPointerRange<netBufferType>(bufPtr, bufLen);
	if (buf == nullptr) {
		sceNetInet->SetLastError(EFAULT);
		return hleLogError(Log::sceNet, -1, "[%i] %s: Invalid pointer range: %08x (size %i)", socket, __func__, bufPtr, bufLen);
	}

	// Translate PSP flags to native flags and send
	const int nativeFlags = SceNetInet::TranslateInetFlagsToNativeFlags(flags, inetSocket->IsNonBlocking());
	const int ret = send(inetSocket->GetNativeSocketId(), buf, bufLen, nativeFlags);
	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		return hleLogError(Log::sceNet, ret, "[%i]: %s: send() encountered error %i: %s", socket, __func__, error, strerror(error));
	}
	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetSendto(int socket, u32 bufPtr, u32 bufLen, int flags, u32 toAddr, u32 toLen) {
	ERROR_LOG_ONCE(sceNetInetSendto, Log::sceNet, "UNTESTED sceNetInetSendto(%i, %08x, %i, %08x, %08x, %i)", socket, bufPtr, bufLen, flags, toAddr, toLen);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const int nativeSocketId = inetSocket->GetNativeSocketId();
	const auto srcBuf = Memory::GetTypedPointerRange<netBufferType>(bufPtr, bufLen);
	if (srcBuf == nullptr) {
		ERROR_LOG(Log::sceNet, "[%i] %s: Invalid pointer range: %08x (size %i)", nativeSocketId, __func__, bufPtr, bufLen);
		return -1;
	}

	// Translate PSP flags to native flags and convert toAddr to native addr
	const int nativeFlags = SceNetInet::TranslateInetFlagsToNativeFlags(flags, inetSocket->IsNonBlocking());
	sockaddr_in convertedSockAddr{};
	if (!inetSockaddrToNativeSocketAddr(convertedSockAddr, toAddr, toLen)) {
		ERROR_LOG(Log::sceNet, "[%i] %s: Unable to translate sceSockAddr to native sockaddr", nativeSocketId, __func__);
		return -1;
	}

	DEBUG_LOG(Log::sceNet, "[%i] %s: Writing %i bytes to %s on port %i", nativeSocketId, __func__, bufLen, ip2str(convertedSockAddr.sin_addr, false).c_str(), ntohs(convertedSockAddr.sin_port));

	const int ret = sendto(nativeSocketId, srcBuf, bufLen, nativeFlags, reinterpret_cast<sockaddr*>(&convertedSockAddr), sizeof(sockaddr_in));
	DEBUG_LOG(Log::sceNet, "[%i] %s: sendto returned %i", nativeSocketId, __func__, ret);

	if (ret < 0) {
		const auto error = sceNetInet->SetLastErrorToMatchPlatform();
		WARN_LOG(Log::sceNet, "[%i] %s: Got error %i=%s", nativeSocketId, __func__, error, strerror(error));
	}

	return hleLogSuccessI(Log::sceNet, ret);
}

static int sceNetInetGetErrno() {
	ERROR_LOG_ONCE(sceNetInetGetErrno, Log::sceNet, "UNTESTED sceNetInetGetErrno()");
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto nativeError = sceNetInet->GetLastError();
	if (nativeError != ERROR_WHEN_NONBLOCKING_CALL_OCCURS && nativeError != 0) {
		INFO_LOG(Log::sceNet, "Requested %s %i=%s", __func__, nativeError, strerror(nativeError));
	}

	return SceNetInet::TranslateNativeErrorToInetError(nativeError);
}

static int sceNetInetBind(int socket, u32 addrPtr, u32 addrLen) {
	WARN_LOG_ONCE(sceNetInetSend, Log::sceNet, "UNTESTED sceNetInetBind(%i, %08x, %08x)", socket, addrPtr, addrLen);
	const auto sceNetInet = SceNetInet::Get();
	if (!sceNetInet) {
		return hleLogError(Log::sceNet, ERROR_NET_INET_CONFIG_INVALID_ARG, "Inet Subsystem Not Running - Use sceNetInetInit");
	}

	const auto inetSocket = sceNetInet->GetInetSocket(socket);
	if (!inetSocket) {
		sceNetInet->SetLastError(EBADF);
		return hleLogError(Log::sceNet, -1, "%s: Attempting to operate on unmapped socket %i", __func__, socket);
	}

	const int nativeSocketId = inetSocket->GetNativeSocketId();

	// Convert PSP bind addr to native bind addr
	sockaddr_in convertedSockaddr{};
	if (!inetSockaddrToNativeSocketAddr(convertedSockaddr, addrPtr, addrLen)) {
		ERROR_LOG(Log::sceNet, "[%i] Error translating sceSockaddr to native sockaddr", nativeSocketId);
		return -1;
	}
	socklen_t socklen = sizeof(convertedSockaddr);

	// Get default outbound sockaddr when INADDR_ANY or INADDR_BROADCAST are used
	if (const auto addr = convertedSockaddr.sin_addr.s_addr; addr == INADDR_ANY || addr == INADDR_BROADCAST) {
		if (!getDefaultOutboundSockaddr(convertedSockaddr, socklen)) {
			WARN_LOG(Log::sceNet, "Failed to get default bound address");
			return -1;
		}
	}

	// TODO: check whether setting to blocking and then non-blocking is valid
	setBlockingMode(nativeSocketId, false);
	INFO_LOG(Log::sceNet, "[%i] Binding to family %i, port %i, addr %s", nativeSocketId, convertedSockaddr.sin_family, ntohs(convertedSockaddr.sin_port), ip2str(convertedSockaddr.sin_addr, false).c_str());
	const int ret = bind(nativeSocketId, reinterpret_cast<sockaddr*>(&convertedSockaddr), socklen);
	INFO_LOG(Log::sceNet, "Bind returned %i for fd=%i", ret, nativeSocketId);
	setBlockingMode(nativeSocketId, inetSocket->IsNonBlocking());

	// Set UPnP
	const auto port = ntohs(convertedSockaddr.sin_port);
	switch (inetSocket->GetProtocol()) {
		case IPPROTO_UDP: {
			UPnP_Add(IP_PROTOCOL_UDP, port, port);
			break;
		}
		case IPPROTO_IP:
		case IPPROTO_TCP: {
			UPnP_Add(IP_PROTOCOL_TCP, port, port);
			break;
		}
		// TODO: Unknown IP protocol 000f when attempting to set up UPnP port forwarding
		default: {
			WARN_LOG(Log::sceNet, "[%i]: Unknown IP protocol %04x when attempting to set up UPnP port forwarding", nativeSocketId, inetSocket->GetProtocol());
			break;
		}
	}
	return hleLogSuccessI(Log::sceNet, ret);
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
	{0XE247B6D6, &WrapI_IUU<sceNetInetGetpeername>,  "sceNetInetGetpeername",           '?', ""     },
	{0X162E6FD5, &WrapI_IUU<sceNetInetGetsockname>,  "sceNetInetGetsockname",           '?', ""     },
	{0X80A21ABD, nullptr,                            "sceNetInetSocketAbort",           '?', ""     },
	{0X39B0C7D3, nullptr,                            "sceNetInetGetUdpcbstat",          '?', ""     },
	{0XB3888AD4, nullptr,                            "sceNetInetGetTcpcbstat",          '?', ""     },
};;

std::shared_ptr<SceNetInet> SceNetInet::gInstance;

std::shared_mutex SceNetInet::gLock;

std::unordered_map<PspInetAddressFamily, int> SceNetInet::gInetAddressFamilyToNativeAddressFamily = {
	{ PSP_NET_INET_AF_UNSPEC, AF_UNSPEC },
	{ PSP_NET_INET_AF_LOCAL, AF_UNIX },
	{ PSP_NET_INET_AF_INET, AF_INET },
};

std::unordered_map<PspInetSocketType, int> SceNetInet::gInetSocketTypeToNativeSocketType = {
	{ PSP_NET_INET_SOCK_STREAM, SOCK_STREAM },
	{ PSP_NET_INET_SOCK_DGRAM, SOCK_DGRAM },
	{ PSP_NET_INET_SOCK_RAW, SOCK_RAW },
	{ PSP_NET_INET_SOCK_RDM, SOCK_RDM },
	{ PSP_NET_INET_SOCK_SEQPACKET, SOCK_SEQPACKET },
};

std::unordered_map<PspInetProtocol, int> SceNetInet::gInetProtocolToNativeProtocol = {
	{ PSP_NET_INET_IPPROTO_IP, IPPROTO_IP },
	{ PSP_NET_INET_IPPROTO_ICMP, IPPROTO_ICMP },
	{ PSP_NET_INET_IPPROTO_IGMP, IPPROTO_IGMP },
	{ PSP_NET_INET_IPPROTO_TCP, IPPROTO_TCP },
	{ PSP_NET_INET_IPPROTO_EGP, IPPROTO_EGP },
	{ PSP_NET_INET_IPPROTO_PUP, IPPROTO_PUP },
	{ PSP_NET_INET_IPPROTO_UDP, IPPROTO_UDP },
	{ PSP_NET_INET_IPPROTO_IDP, IPPROTO_IDP },
	{ PSP_NET_INET_IPPROTO_RAW, IPPROTO_RAW },
};

// Windows compat workarounds (ugly! may not work!)
#if PPSSPP_PLATFORM(WINDOWS)
#define SO_REUSEPORT (SO_BROADCAST|SO_REUSEADDR)
#define SO_TIMESTAMP 0
#define MSG_DONTWAIT 0
#endif

// TODO: commented out optnames
std::unordered_map<PspInetSocketOptionName, int> SceNetInet::gInetSocketOptnameToNativeOptname = {
	{ INET_SO_ACCEPTCONN, SO_ACCEPTCONN },
	{ INET_SO_REUSEADDR, SO_REUSEADDR },
	{ INET_SO_KEEPALIVE, SO_KEEPALIVE },
	{ INET_SO_DONTROUTE, SO_DONTROUTE },
	{ INET_SO_BROADCAST, SO_BROADCAST },
	// { INET_SO_USELOOPBACK, INET_SO_USELOOPBACK },
	{ INET_SO_LINGER, SO_LINGER },
	{ INET_SO_OOBINLINE, SO_OOBINLINE },
	{ INET_SO_REUSEPORT, SO_REUSEPORT },
	{ INET_SO_TIMESTAMP, SO_TIMESTAMP },
	// { INET_SO_ONESBCAST, INET_SO_ONESBCAST },
	{ INET_SO_SNDBUF, SO_SNDBUF },
	{ INET_SO_RCVBUF, SO_RCVBUF },
	{ INET_SO_SNDLOWAT, SO_SNDLOWAT },
	{ INET_SO_RCVLOWAT, SO_RCVLOWAT },
	{ INET_SO_SNDTIMEO, SO_SNDTIMEO },
	{ INET_SO_RCVTIMEO, SO_RCVTIMEO },
	{ INET_SO_ERROR, SO_ERROR },
	{ INET_SO_TYPE, SO_TYPE },
	// { INET_SO_OVERFLOWED, INET_SO_OVERFLOWED },
	{ INET_SO_NONBLOCK, INET_SO_NONBLOCK },
};

std::unordered_map<PspInetMessageFlag, int> SceNetInet::gInetMessageFlagToNativeMessageFlag = {
	{ INET_MSG_OOB, MSG_OOB },
	{ INET_MSG_PEEK, MSG_PEEK },
	{ INET_MSG_DONTROUTE, MSG_DONTROUTE },
#if defined(MSG_EOR)
	{ INET_MSG_EOR, MSG_EOR },
#endif
	{ INET_MSG_TRUNC, MSG_TRUNC },
	{ INET_MSG_CTRUNC, MSG_CTRUNC },
	{ INET_MSG_WAITALL, MSG_WAITALL },
	{ INET_MSG_DONTWAIT, MSG_DONTWAIT },
#if defined(MSG_BCAST)
	{ INET_MSG_BCAST, MSG_BCAST },
#endif
#if defined(MSG_MCAST)
	{ INET_MSG_MCAST, MSG_MCAST },
#endif
};

std::unordered_map<int, InetErrorCode> SceNetInet::gNativeErrorCodeToInetErrorCode = {
	{ EINPROGRESS, INET_EINPROGRESS }
};

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

bool SceNetInet::TranslateInetAddressFamilyToNative(int &destAddressFamily, int srcAddressFamily) {
	const auto it = gInetAddressFamilyToNativeAddressFamily.find(static_cast<PspInetAddressFamily>(srcAddressFamily));
	if (it == gInetAddressFamilyToNativeAddressFamily.end()) {
		return false;
	}
	destAddressFamily = it->second;
	return true;
}

bool SceNetInet::TranslateInetSocketLevelToNative(int &destSocketLevel, int srcSocketLevel) {
	if (srcSocketLevel != PSP_NET_INET_SOL_SOCKET) {
		return false;
	}
	destSocketLevel = SOL_SOCKET;
	return true;
}

bool SceNetInet::TranslateInetSocketTypeToNative(int &destSocketType, bool &destNonBlocking, int srcSocketType) {
	// First, take the base socket type
	const int baseSocketType = static_cast<PspInetSocketType>(srcSocketType & PSP_NET_INET_SOCK_TYPE_MASK);
	const auto it = gInetSocketTypeToNativeSocketType.find(static_cast<PspInetSocketType>(baseSocketType));
	if (it == gInetSocketTypeToNativeSocketType.end()) {
		return false;
	}
	// Set base value for dest
	destSocketType = it->second;
	// Find any flags which are set, noting that this highly depends on the native platform and unknowns are ignored
	const int srcFlags = srcSocketType & PSP_NET_INET_SOCK_FLAGS_MASK;
#if defined(SOCK_DCCP)
	if ((srcFlags & PSP_NET_INET_SOCK_DCCP) != 0) {
		destSocketType |= SOCK_DCCP;
	}
#endif
#if defined(SOCK_PACKET)
	if ((srcFlags & PSP_NET_INET_SOCK_PACKET) != 0) {
		destSocketType |= SOCK_PACKET;
	}
#endif
#if defined(SOCK_CLOEXEC)
	if ((srcFlags & PSP_NET_INET_SOCK_CLOEXEC) != 0) {
		destSocketType |= SOCK_CLOEXEC;
	}
#endif
	if ((srcFlags & PSP_NET_INET_SOCK_NONBLOCK) != 0) {
		destNonBlocking = true;
	}
#if defined(SOCK_NOSIGPIPE)
	if ((srcFlags & PSP_NET_INET_SOCK_NOSIGPIPE) != 0) {
		destSocketType |= SOCK_NOSIGPIPE;
	}
#endif
	return true;
}

bool SceNetInet::TranslateInetProtocolToNative(int &destProtocol, int srcProtocol) {
	const auto it = gInetProtocolToNativeProtocol.find(static_cast<PspInetProtocol>(srcProtocol));
	if (it == gInetProtocolToNativeProtocol.end()) {
		return false;
	}
	destProtocol = it->second;
	return true;
}

bool SceNetInet::TranslateInetOptnameToNativeOptname(int &destOptname, const int inetOptname) {
	const auto it = gInetSocketOptnameToNativeOptname.find(static_cast<PspInetSocketOptionName>(inetOptname));
	if (it == gInetSocketOptnameToNativeOptname.end()) {
		return false;
	}
	destOptname = it->second;
	return true;
}

int SceNetInet::TranslateInetFlagsToNativeFlags(const int messageFlags, const bool nonBlocking) {
	int nativeFlags = 0; // The actual platform flags
	int foundFlags = 0; // The inet equivalent of the native flags, used to verify that no remaining flags need to be set
	for (const auto [inetFlag, nativeFlag] : gInetMessageFlagToNativeMessageFlag) {
		if ((messageFlags & inetFlag) != 0) {
			nativeFlags |= nativeFlag;
			foundFlags |= inetFlag;
		}
	}

#if !PPSSPP_PLATFORM(WINDOWS)
	if (nonBlocking) {
		nativeFlags |= MSG_DONTWAIT;
	}
#endif

	// Check for any inet flags which were not successfully mapped into a native flag
	if (const int missingFlags = messageFlags & ~foundFlags; missingFlags != 0) {
		for (int i = 0; i < sizeof(int) * 8; i++) {
			if (const int val = 1 << i; (missingFlags & val) != 0) {
				DEBUG_LOG(Log::sceNet, "Encountered unsupported platform flag at bit %i (actual value %04x), undefined behavior may ensue.", i, val);
			}
		}
	}
	return nativeFlags;
}

int SceNetInet::TranslateNativeErrorToInetError(const int nativeError) {
	if (const auto it = gNativeErrorCodeToInetErrorCode.find(nativeError);
		it != gNativeErrorCodeToInetErrorCode.end()) {
		return it->second;
	}
	return nativeError;
}

int SceNetInet::GetLastError() {
	auto lock = std::shared_lock(mLock);
	return mLastError;
}

void SceNetInet::SetLastError(const int error) {
	auto lock = std::unique_lock(mLock);
	mLastError = error;
}

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

std::shared_ptr<InetSocket> SceNetInet::CreateAndAssociateInetSocket(int nativeSocketId, int protocol, bool nonBlocking) {
	auto lock = std::unique_lock(mLock);

	int inetSocketId = ++mCurrentInetSocketId;
	if (const auto it = mInetSocketIdToNativeSocket.find(inetSocketId); it != mInetSocketIdToNativeSocket.end()) {
		WARN_LOG(Log::sceNet, "%s: Attempted to re-associate socket from already-associated inetSocketId: %i", __func__, inetSocketId);
		return nullptr;
	}
	auto inetSocket = std::make_shared<InetSocket>(inetSocketId, nativeSocketId, protocol, nonBlocking);
	inetSocket->SetNonBlocking(nonBlocking);
	mInetSocketIdToNativeSocket.emplace(inetSocketId, inetSocket);
	return inetSocket;
}

std::shared_ptr<InetSocket> SceNetInet::GetInetSocket(int inetSocketId) {
	auto lock = std::shared_lock(mLock);

	const auto it = mInetSocketIdToNativeSocket.find(inetSocketId);
	if (it == mInetSocketIdToNativeSocket.end()) {
		WARN_LOG(Log::sceNet, "%s: Attempted to get unassociated socket from inetSocketId: %i", __func__, inetSocketId);
		return nullptr;
	}
	return it->second;
}

bool SceNetInet::GetNativeSocketIdForInetSocketId(int& nativeSocketId, int inetSocketId) {
	const auto inetSocket = GetInetSocket(inetSocketId);
	if (!inetSocket) {
		return false;
	}
	nativeSocketId = inetSocket->GetNativeSocketId();
	return true;
}

bool SceNetInet::EraseNativeSocket(int inetSocketId) {
	auto lock = std::unique_lock(mLock);

	const auto it = mInetSocketIdToNativeSocket.find(inetSocketId);
	if (it == mInetSocketIdToNativeSocket.end()) {
		WARN_LOG(Log::sceNet, "%s: Attempted to delete unassociated socket from inetSocketId: %i", __func__, inetSocketId);
		return false;
	}
	mInetSocketIdToNativeSocket.erase(it);
	return true;
}

bool SceNetInet::TranslateInetFdSetToNativeFdSet(int &maxFd, fd_set& destFdSet, u32 fdsPtr) const {
	if (fdsPtr == 0) {
		// Allow nullptr to be used without failing
		return true;
	}

	FD_ZERO(&destFdSet);
	const auto sceFdSet = Memory::GetTypedPointerRange<PspInetFdSetOperations::FdSet>(fdsPtr, sizeof(PspInetFdSetOperations::FdSet));
	if (sceFdSet == nullptr) {
		ERROR_LOG(Log::sceNet, "%s: Invalid fdsPtr %08x", __func__, fdsPtr);
		return false;
	}

	int setSize = 0;
	for (auto& it : mInetSocketIdToNativeSocket) {
		const auto inetSocket = it.first;
		const auto nativeSocketId = it.second->GetNativeSocketId();
		maxFd = std::max(nativeSocketId + 1, maxFd);
		if (PspInetFdSetOperations::IsSet(*sceFdSet, inetSocket)) {
			if (++setSize > FD_SETSIZE) {
				ERROR_LOG(Log::sceNet, "%s: Encountered input FD_SET which is greater than max supported size %i", __func__, setSize);
				return false;
			}
			DEBUG_LOG(Log::sceNet, "%s: Translating input %i into %i", __func__, inetSocket, nativeSocketId);
			FD_SET(nativeSocketId, &destFdSet);
		}
	}

	DEBUG_LOG(Log::sceNet, "%s: Translated %i sockets", __func__, setSize);
	return true;
}

void SceNetInet::CloseAllRemainingSockets() const {
	for (const auto &[first, second] : mInetSocketIdToNativeSocket) {
		if (!second) {
			continue;
		}
		close(second->GetNativeSocketId());
	}
}

void Register_sceNetInet() {
	RegisterModule("sceNetInet", std::size(sceNetInet), sceNetInet);
}

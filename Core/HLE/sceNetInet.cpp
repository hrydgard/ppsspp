#include <algorithm>
#include "Common/StringUtils.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"

#include "Core/HLE/SocketManager.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceNetInet.h"
#include "Core/HLE/sceNp.h"
#include "Core/HLE/sceNp2.h"
#include "Core/HLE/NetInetConstants.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMapHelpers.h"
#include "Core/Util/PortManager.h"
#include "Core/Instance.h"
#ifdef __MINGW32__
#include <mswsock.h>
#endif
#ifndef _WIN32
#include <poll.h>
#endif
#include <vector>

// This is the PSP networking errno.
// It has a separate value per-thread, hence the map.

static std::map<int, int> g_inetLastErrno;

// Functions should only call this on error, NOT on success.
// Returns a PSP errno (ERROR_INET_*), in case it's needed.
static int UpdateErrnoFromHost(int threadID, int hostErrno, const char *func) {
	int newErrno = convertInetErrnoHost2PSP(hostErrno);

	// This will do the right thing if not already present - insert a zero value.
	int lastErrno = g_inetLastErrno[threadID];

	if (lastErrno == 0 && newErrno == 0) {
		WARN_LOG(Log::sceNet, "BAD: errno set to 0 in %s. Functions should not clear errno.", func);
	} else if (lastErrno != 0 && newErrno == 0) {
		ERROR_LOG(Log::sceNet, "BAD: errno cleared (previously %s) in %s. Functions should not clear errno.", convertInetErrno2str(lastErrno), func);
		lastErrno = 0;
	} else if (lastErrno == newErrno) {
		VERBOSE_LOG(Log::sceNet, "errno remained %s in %s (host: %d)", convertInetErrno2str(newErrno), func, hostErrno);
	} else {
		DEBUG_LOG(Log::sceNet, "errno set to %s in %s (host: %d)", convertInetErrno2str(newErrno), func, hostErrno);
		lastErrno = newErrno;
	}

	g_inetLastErrno[threadID] = lastErrno;
	return lastErrno;
}

// A descriptor that isn't open fails the call with -1 and EBADF (pspautotests net/inet/errors).
static int InetBadSocket(int socket, const char *func) {
	UpdateErrnoFromHost(__KernelGetCurThread(), EBADF, func);
	return hleLogError(Log::sceNet, -1, "Bad socket #%d", socket);
}

// For errors that PPSSPP decides on itself rather than the host, in the PSP's numbering. A blocked
// call retried later passes the thread it's for, which isn't the current one.
static void SetInetErrno(int pspErrno, const char *func, SceUID threadID = __KernelGetCurThread()) {
	DEBUG_LOG(Log::sceNet, "errno set to %s in %s", convertInetErrno2str(pspErrno), func);
	g_inetLastErrno[threadID] = pspErrno;
}

// socket(), close() and setsockopt() clear errno when they succeed (pspautotests net/inet/errors and
// options). Whether the other calls do hasn't been checked.
static void ClearInetErrno() {
	g_inetLastErrno[__KernelGetCurThread()] = 0;
}

// The PSP keeps errno in a record per thread, made by the thread's first call. Until then,
// sceNetInetGetErrno says there's none (pspautotests net/inet/errnothread).
static void InetThreadSeen() {
	g_inetLastErrno.emplace(__KernelGetCurThread(), 0);
}

// A socket the game names in a call, which also counts as the thread's first call.
static bool GetCallSocket(int socket, InetSocket **inetSock) {
	InetThreadSeen();
	return g_socketManager.GetInetSocket(socket, inetSock);
}

// Game memory that a host call will read or write directly. Fails with EFAULT rather than letting
// the host run past the end of it.
static bool InetValidRange(u32 ptr, u32 size, const char *func, SceUID threadID = __KernelGetCurThread()) {
	if (size == 0 || Memory::IsValidRange(ptr, size)) {
		return true;
	}
	SetInetErrno(ERROR_INET_EFAULT, func, threadID);
	return false;
}

bool netInetInited = false;

// A blocking call waiting for its sockets. Host sockets are always non-blocking: rather than block
// the whole emulator in the host call, the thread waits, and a CoreTiming event retries the call
// every millisecond until it completes or its timeout runs out in emulated time.
enum class InetWaitOp : int {
	Select,
	Poll,
	Recv,
	RecvFrom,
	Send,
	SendTo,
	Accept,
	Connect,
};

struct InetWait {
	InetWaitOp op;
	u32 args[6];
	s64 deadline;  // In ticks, or -1 to wait for as long as it takes.
};

// One non-blocking attempt at a call. When it would block, errno isn't set yet: hostErrno says
// what to set it to if the caller doesn't wait.
struct InetTry {
	int result;
	bool wouldBlock;
	int hostErrno;
};

// By waiting thread. Sockets don't survive a savestate, but the waits are saved so a thread
// doesn't wait forever: its next retry finds the socket gone and fails.
static std::map<SceUID, InetWait> g_inetWaits;
static int eventInetWaitPoll = -1;
static const int INET_WAIT_POLL_US = 1000;
static const SceUID INET_WAIT_ID = 0x494E4554;

static void __NetInetWaitPoll(u64 userdata, int cyclesLate);

void __NetInetInit() {
	g_inetWaits.clear();
	eventInetWaitPoll = CoreTiming::RegisterEvent("NetInetWaitPoll", &__NetInetWaitPoll);
}

void __NetInetDoState(PointerWrap &p, bool hasState, bool hasErrno) {
	if (hasState) {
		Do(p, g_inetWaits);
		Do(p, eventInetWaitPoll);
	} else if (p.mode == p.MODE_READ) {
		g_inetWaits.clear();
		eventInetWaitPoll = -1;
	}
	if (hasErrno) {
		Do(p, g_inetLastErrno);
	} else if (p.mode == p.MODE_READ) {
		g_inetLastErrno.clear();
	}
	CoreTiming::RestoreRegisterEvent(eventInetWaitPoll, "NetInetWaitPoll", &__NetInetWaitPoll);
}

static const char *InetSocketUPnPProtocol(const InetSocket *inetSock) {
	return inetSock->type == PSP_NET_INET_SOCK_STREAM ? IP_PROTOCOL_TCP : IP_PROTOCOL_UDP;
}

// Closes a socket, and removes the port forward its bind() set up, unless another socket still
// has the port (through SO_REUSEPORT).
static void CloseInetSocket(InetSocket *inetSock) {
	if (inetSock->upnpPort) {
		bool shared = false;
		for (int i = SocketManager::MIN_VALID_INET_SOCKET; i < SocketManager::VALID_INET_SOCKET_COUNT; i++) {
			const InetSocket &other = g_socketManager.Sockets()[i];
			if (&other != inetSock && other.state == SocketState::UsedNetInet && other.upnpPort == inetSock->upnpPort && other.type == inetSock->type) {
				shared = true;
				break;
			}
		}
		if (!shared) {
			UPnP_Remove(InetSocketUPnPProtocol(inetSock), inetSock->upnpPort);
		}
	}
	g_socketManager.Close(inetSock);
}

void __NetInetShutdown() {
	if (!netInetInited) {
		return;
	}

	netInetInited = false;
	for (int i = SocketManager::MIN_VALID_INET_SOCKET; i < SocketManager::VALID_INET_SOCKET_COUNT; i++) {
		const InetSocket &inetSock = g_socketManager.Sockets()[i];
		if (inetSock.state == SocketState::UsedNetInet && inetSock.upnpPort) {
			UPnP_Remove(InetSocketUPnPProtocol(&inetSock), inetSock.upnpPort);
		}
	}
	g_socketManager.CloseAll();
	g_inetLastErrno.clear();
}

static int sceNetInetInit() {
	if (netInetInited)
		return hleLogError(Log::sceNet, SCE_NET_INET_ERROR_ALREADY_INITIALIZED);
	g_inetLastErrno.clear();
	netInetInited = true;
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetInetTerm() {
	INFO_LOG(Log::sceNet, "sceNetInetTerm()");
	__NetInetShutdown();
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetInetGetErrno(MIPSState *mips) {
	const auto it = g_inetLastErrno.find(__KernelGetCurThread());
	if (it == g_inetLastErrno.end()) {
		return hleLogDebug(Log::sceNet, ERROR_NET_NO_SPACE, "no call on this thread yet");
	}
	const int lastErrno = it->second;

	if (lastErrno == ERROR_INET_EAGAIN) {
		return hleLogVerbose(Log::sceNet, lastErrno, "returning %s at %08x", convertInetErrno2str(lastErrno), mips->pc);
	} else {
		return hleLogDebug(Log::sceNet, lastErrno, "returning %s at %08x", convertInetErrno2str(lastErrno), mips->pc);
	}
}

static int sceNetInetGetPspError(MIPSState *mips) {
	int threadID = __KernelGetCurThread();
	const int lastErrno = g_inetLastErrno[threadID];

	uint32_t error = convertInetErrno2PSPError(lastErrno);
	return hleLogInfo(Log::sceNet, error, "returning %s converted to %08x at %08x", convertInetErrno2str(lastErrno), error, mips->pc);
}

// The address parsers work like 4.4BSD's, which the host's may not (pspautotests net/inet/addr).
// Both stop at a space, so "1.2.3.4 " is accepted.

// inet_aton(): one to four parts, each decimal, octal (leading 0) or hex (0x), the last one filling
// the remaining bytes, so "1.2.3", "16909060" and "0x7f.1" are all addresses. Returns the address in
// network byte order.
static bool InetParseAton(const char *str, u32 *addr) {
	u32 parts[4];
	int count = 0;
	const char *p = str;
	while (true) {
		if (!isdigit((unsigned char)*p)) {
			return false;
		}
		int base = 10;
		if (*p == '0') {
			p++;
			base = 8;
			if (*p == 'x' || *p == 'X') {
				p++;
				base = 16;
			}
		}
		u64 value = 0;
		while (true) {
			const unsigned char c = *p;
			int digit;
			if (isdigit(c)) {
				digit = c - '0';
				if (digit >= base) {
					return false;
				}
			} else if (base == 16 && isxdigit(c)) {
				digit = tolower(c) - 'a' + 10;
			} else {
				break;
			}
			value = value * base + digit;
			if (value > 0xFFFFFFFF) {
				return false;
			}
			p++;
		}
		parts[count++] = (u32)value;
		if (*p != '.') {
			break;
		}
		if (count == 4) {
			return false;
		}
		p++;
	}
	if (*p != '\0' && !isspace((unsigned char)*p)) {
		return false;
	}

	// All but the last part are single bytes, and the last fills what's left.
	static const u32 lastMax[4] = { 0xFFFFFFFF, 0xFFFFFF, 0xFFFF, 0xFF };
	u32 value = 0;
	for (int i = 0; i < count - 1; i++) {
		if (parts[i] > 0xFF) {
			return false;
		}
		value |= parts[i] << (24 - 8 * i);
	}
	if (parts[count - 1] > lastMax[count - 1]) {
		return false;
	}
	value |= parts[count - 1];
	*addr = htonl(value);
	return true;
}

// inet_pton() for AF_INET: exactly four decimal bytes, without leading zeros.
static bool InetParsePton4(const char *str, u32 *addr) {
	u8 bytes[4];
	const char *p = str;
	for (int i = 0; i < 4; i++) {
		if (i > 0) {
			if (*p != '.') {
				return false;
			}
			p++;
		}
		if (!isdigit((unsigned char)*p)) {
			return false;
		}
		if (*p == '0' && isdigit((unsigned char)p[1])) {
			return false;
		}
		int value = 0;
		while (isdigit((unsigned char)*p)) {
			value = value * 10 + (*p - '0');
			if (value > 255) {
				return false;
			}
			p++;
		}
		bytes[i] = (u8)value;
	}
	if (*p != '\0' && !isspace((unsigned char)*p)) {
		return false;
	}
	memcpy(addr, bytes, sizeof(bytes));
	return true;
}

static int sceNetInetInetPton(int af, const char* hostname, u32 inAddrPtr) {
	if (af != PSP_NET_INET_AF_INET) {
		// INET6 too (pspautotests net/inet/addr).
		SetInetErrno(ERROR_INET_EAFNOSUPPORT, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "unsupported family %d", af);
	}
	if (!Memory::IsValidRange(inAddrPtr, 4)) {
		return hleLogError(Log::sceNet, 0, "invalid arg"); //-1
	}

	u32 addr;
	if (!hostname || !InetParsePton4(hostname, &addr)) {
		return hleLogDebug(Log::sceNet, 0, "not an address");
	}
	Memory::WriteUnchecked_U32(addr, inAddrPtr);
	return hleLogDebug(Log::sceNet, 1);
}

static int sceNetInetInetAton(const char* hostname, u32 inAddrPtr) {
	if (!Memory::IsValidRange(inAddrPtr, 4)) {
		return hleLogError(Log::sceNet, 0, "invalid arg"); //-1
	}

	u32 addr;
	if (!hostname || !InetParseAton(hostname, &addr)) {
		return hleLogDebug(Log::sceNet, 0, "not an address");
	}
	Memory::WriteUnchecked_U32(addr, inAddrPtr);
	return hleLogDebug(Log::sceNet, 1);
}

// Returns dst, or 0 with errno set: ENOSPC when the string and its terminator don't fit, leaving dst
// untouched, and EAFNOSUPPORT for anything but AF_INET (pspautotests net/inet/addr).
static u32 sceNetInetInetNtop(int af, u32 srcInAddrPtr, u32 dstBufPtr, u32 bufsize) {
	if (af != PSP_NET_INET_AF_INET) {
		SetInetErrno(ERROR_INET_EAFNOSUPPORT, __FUNCTION__);
		return hleLogError(Log::sceNet, 0, "unsupported family %d", af);
	}
	if (!Memory::IsValidRange(srcInAddrPtr, 4)) {
		return hleLogError(Log::sceNet, 0, "invalid arg");
	}

	const u8 *src = Memory::GetPointerUnchecked(srcInAddrPtr);
	char str[16];
	const int len = snprintf(str, sizeof(str), "%u.%u.%u.%u", src[0], src[1], src[2], src[3]);
	if ((u32)len + 1 > bufsize) {
		SetInetErrno(ERROR_INET_ENOSPC, __FUNCTION__);
		return hleLogDebug(Log::sceNet, 0, "%s doesn't fit in %d bytes", str, bufsize);
	}
	if (!Memory::IsValidRange(dstBufPtr, len + 1)) {
		return hleLogError(Log::sceNet, 0, "invalid arg");
	}
	Memory::MemcpyUnchecked(dstBufPtr, str, len + 1);
	return hleLogDebug(Log::sceNet, dstBufPtr, "%s", str);
}

static u32_le sceNetInetInetAddr(const char *hostname) {
	u32 addr;
	if (!hostname || !InetParseAton(hostname, &addr)) {
		return hleLogDebug(Log::sceNet, INADDR_NONE, "not an address");
	}
	return hleLogDebug(Log::sceNet, addr);
}

// The PSP fills in a whole SceNetInetSockaddr (sa_len 16) and copies as much of it as namelen
// allows, setting namelen to the bytes copied (pspautotests net/inet/poll).
static void WriteSockaddrToPSP(const SockAddrIN4 &saddr, u32 namePtr, u32 namelenPtr) {
	SceNetInetSockaddr name{};
	name.sa_len = (u8)sizeof(name);
	name.sa_family = saddr.addr.sa_family;
	memcpy(name.sa_data, saddr.addr.sa_data, sizeof(name.sa_data));
	const s32 namelen = (s32)Memory::ReadUnchecked_U32(namelenPtr);
	const u32 copyLen = (u32)std::clamp(namelen, 0, (s32)sizeof(name));
	if (Memory::IsValidRange(namePtr, copyLen)) {
		Memory::Memcpy(namePtr, &name, copyLen, "InetSockaddr");
	}
	Memory::WriteUnchecked_U32(copyLen, namelenPtr);
}

static int sceNetInetGetpeername(int socket, u32 namePtr, u32 namelenPtr) {
	if (!Memory::IsValidAddress(namePtr) || !Memory::IsValidRange(namelenPtr, 4)) {
		UpdateErrnoFromHost(__KernelGetCurThread(), EFAULT, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "invalid arg");
	}

	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	SockAddrIN4 saddr{};
	socklen_t len = sizeof(saddr);
	int retval = getpeername(inetSock->sock, (sockaddr*)&saddr, &len);
	DEBUG_LOG(Log::sceNet, "Getpeername: Family = %s, Address = %s, Port = %d", inetSocketDomain2str(saddr.addr.sa_family).c_str(), ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	if (retval < 0) {
		// 4.4BSD checks for a connection first, so it's ENOTCONN where macOS says EINVAL, once the
		// peer has closed it (pspautotests net/inet/errors).
		const int hostErrno = socket_errno;
		UpdateErrnoFromHost(__KernelGetCurThread(), hostErrno == EINVAL ? ENOTCONN : hostErrno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}

	WriteSockaddrToPSP(saddr, namePtr, namelenPtr);
	return hleLogInfo(Log::sceNet, 0);
}

static int sceNetInetGetsockname(int socket, u32 namePtr, u32 namelenPtr) {
	if (!Memory::IsValidAddress(namePtr) || !Memory::IsValidRange(namelenPtr, 4)) {
		UpdateErrnoFromHost(__KernelGetCurThread(), EFAULT, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "invalid arg");
	}

	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	SockAddrIN4 saddr{};
	socklen_t len = sizeof(saddr);
	int retval = getsockname(inetSock->sock, (sockaddr*)&saddr, &len);
	DEBUG_LOG(Log::sceNet, "Getsockname: Family = %s, Address = %s, Port = %d", inetSocketDomain2str(saddr.addr.sa_family).c_str(), ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}

	WriteSockaddrToPSP(saddr, namePtr, namelenPtr);
	return hleLogInfo(Log::sceNet, 0);
}

static InetTry TryRecvFrom(SceUID threadID, const u32 *args);
static InetTry TrySendTo(SceUID threadID, const u32 *args);
static InetTry TryAccept(SceUID threadID, const u32 *args);
static InetTry TryConnectDone(SceUID threadID, const u32 *args);

// The host socket behind a guest one, or false if there's none. Unlike GetHostSocketFromInetSocket,
// doesn't assert, since a socket can be closed while a call waits for it, and works on Windows,
// where SOCKET is unsigned and can't be checked for -1 with < 0.
static bool GetHostSocket(int socket, SOCKET *hostSocket) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return false;
	}
	*hostSocket = inetSock->sock;
	return true;
}

// Sets errno for a failed send. An unconnected UDP socket sending without an address is EDESTADDRREQ,
// which Winsock calls ENOTCONN (pspautotests net/inet/errors and msg).
static void SetSendErrno(SceUID threadID, const InetSocket *inetSock, int hostErrno, const char *func) {
	if (inetSock->type != PSP_NET_INET_SOCK_STREAM && hostErrno == ENOTCONN) {
		SetInetErrno(ERROR_INET_EDESTADDRREQ, func, threadID);
	} else {
		UpdateErrnoFromHost(threadID, hostErrno, func);
	}
}

static bool IsWouldBlock(int hostErrno) {
	return hostErrno == EAGAIN || hostErrno == EWOULDBLOCK;
}

// How long a blocking call on this socket may wait: 0 not at all, -1 forever.
static s64 SocketWaitUs(const InetSocket *inetSock, u32 flags, bool send) {
	if (inetSock->nonblocking || (flags & PSP_NET_INET_MSG_DONTWAIT)) {
		return 0;
	}
	// In whole 10 ms clock ticks, and less than one is no timeout (pspautotests net/inet/limits).
	const u32 ticks = (send ? inetSock->sendTimeoutUs : inetSock->recvTimeoutUs) / 10000;
	return ticks == 0 ? -1 : (s64)ticks * 10000;
}

static void ZeroInetFdSets(u32 readfdsPtr, u32 writefdsPtr, u32 exceptfdsPtr) {
	for (u32 ptr : { readfdsPtr, writefdsPtr, exceptfdsPtr }) {
		if (ptr) {
			NetInetFD_ZERO((SceNetInetFdSet *)Memory::GetPointerWriteOrException(ptr));
		}
	}
}

// A host socket to check without waiting: in, what to check for, and out, what it's ready for.
struct HostSocketReady {
	SOCKET sock;
	bool read;
	bool write;
	bool except;
};

// Returns how many of the checks came back ready, like select(), or -1 with socket_errno set.
// Elsewhere select() can't take a descriptor numbered FD_SETSIZE or above, so this uses poll(), mapped
// as select() would see it. A hung-up socket counts as writable too: macOS reports a refused connect
// as just POLLHUP, which its select() shows as writable. Winsock's select() has no such limit, just one of FD_SETSIZE (64)
// sockets per set, so it's called on groups of that many.
static int CheckHostSockets(HostSocketReady *checks, int count) {
	int ready = 0;
#if defined(_WIN32)
	for (int start = 0; start < count; start += FD_SETSIZE) {
		const int n = std::min(count - start, (int)FD_SETSIZE);
		fd_set rd, wr, ex;
		FD_ZERO(&rd);
		FD_ZERO(&wr);
		FD_ZERO(&ex);
		bool anyRd = false, anyWr = false, anyEx = false;
		for (int i = start; i < start + n; i++) {
			if (checks[i].read) { FD_SET(checks[i].sock, &rd); anyRd = true; }
			if (checks[i].write) { FD_SET(checks[i].sock, &wr); anyWr = true; }
			if (checks[i].except) { FD_SET(checks[i].sock, &ex); anyEx = true; }
		}
		if (!anyRd && !anyWr && !anyEx) {
			// Winsock's select() fails on empty sets.
			continue;
		}
		timeval tmout = { 0, 0 };
		if (select(0, anyRd ? &rd : nullptr, anyWr ? &wr : nullptr, anyEx ? &ex : nullptr, &tmout) < 0) {
			return -1;
		}
		for (int i = start; i < start + n; i++) {
			checks[i].read = checks[i].read && FD_ISSET(checks[i].sock, &rd);
			checks[i].write = checks[i].write && FD_ISSET(checks[i].sock, &wr);
			checks[i].except = checks[i].except && FD_ISSET(checks[i].sock, &ex);
			ready += checks[i].read + checks[i].write + checks[i].except;
		}
	}
#else
	std::vector<pollfd> fds(count);
	for (int i = 0; i < count; i++) {
		fds[i].fd = checks[i].sock;
		fds[i].events = (checks[i].read ? POLLIN : 0) | (checks[i].write ? POLLOUT : 0) | (checks[i].except ? POLLPRI : 0);
	}
	if (count > 0 && poll(fds.data(), count, 0) < 0) {
		return -1;
	}
	for (int i = 0; i < count; i++) {
		const short revents = fds[i].revents;
		if (revents & POLLNVAL) {
			errno = EBADF;
			return -1;
		}
		checks[i].read = checks[i].read && (revents & (POLLIN | POLLHUP | POLLERR));
		checks[i].write = checks[i].write && (revents & (POLLOUT | POLLERR | POLLHUP));
		// macOS adds POLLPRI to POLLHUP when the peer closes, with nothing for select() to report.
		checks[i].except = checks[i].except && (revents & POLLPRI) && !(revents & POLLHUP);
		ready += checks[i].read + checks[i].write + checks[i].except;
	}
#endif
	return ready;
}

// args: nfds, readfds, writefds, exceptfds. The sets are only overwritten with the result when
// something is ready, so they still hold the request between attempts.
static InetTry TrySelect(SceUID threadID, const u32 *args, bool firstTry) {
	const int nfds = (int)args[0];
	SceNetInetFdSet *sets[3] = {
		args[1] ? (SceNetInetFdSet *)Memory::GetPointerWriteOrException(args[1]) : nullptr,
		args[2] ? (SceNetInetFdSet *)Memory::GetPointerWriteOrException(args[2]) : nullptr,
		args[3] ? (SceNetInetFdSet *)Memory::GetPointerWriteOrException(args[3]) : nullptr,
	};

	HostSocketReady checks[PSP_NET_INET_FD_SETSIZE];
	int guestFds[PSP_NET_INET_FD_SETSIZE];
	int count = 0;
	for (int i = SocketManager::MIN_VALID_INET_SOCKET; i < nfds; i++) {
		bool want[3];
		for (int s = 0; s < 3; s++) {
			want[s] = sets[s] && NetInetFD_ISSET(i, sets[s]);
		}
		if (!want[0] && !want[1] && !want[2]) {
			continue;
		}
		SOCKET sock;
		if (!GetHostSocket(i, &sock)) {
			// Fails, leaving the sets alone (pspautotests net/inet/polledge).
			UpdateErrnoFromHost(threadID, EBADF, "sceNetInetSelect");
			return { -1, false, 0 };
		}
		checks[count] = { sock, want[0], want[1], want[2] };
		guestFds[count] = i;
		count++;
	}

	if (count == 0) {
		// Nothing to check: a select() used as a sleep.
		return { 0, true, EAGAIN };
	}

	const int retval = CheckHostSockets(checks, count);
	if (retval == 0) {
		return { 0, true, EAGAIN };
	}
	if (retval < 0) {
		UpdateErrnoFromHost(threadID, socket_errno, "sceNetInetSelect");
		return { retval, false, 0 };
	}

	// Convert the results back to PSP fd_sets.
	for (auto *set : sets) {
		if (set) {
			NetInetFD_ZERO(set);
		}
	}
	for (int i = 0; i < count; i++) {
		const bool ready[3] = { checks[i].read, checks[i].write, checks[i].except };
		for (int s = 0; s < 3; s++) {
			if (ready[s]) {
				NetInetFD_SET(guestFds[i], sets[s]);
			}
		}
	}
	return { retval, false, 0 };
}

// args: fds, nfds. Rewrites every revents, so repeating it is harmless.
static InetTry TryPoll(SceUID threadID, const u32 *args) {
	SceNetInetPollfd *fdarray = (SceNetInetPollfd*)Memory::GetPointerOrException(args[0]); // SceNetInetPollfd/pollfd, sceNetInetPoll() have similarity to BSD poll() but pollfd have different size on 64bit
	const int nfds = (int)args[1];

	std::vector<HostSocketReady> checks(nfds);
	std::vector<bool> open(nfds);
	int hostCount = 0;
	for (int i = 0; i < nfds; i++) {
		SOCKET hostSocket;
		fdarray[i].revents = 0;
		open[i] = GetHostSocket(fdarray[i].fd, &hostSocket);
		if (open[i]) {
			const int events = fdarray[i].events;
			checks[hostCount++] = { hostSocket, (events & (INET_POLLRDNORM | INET_POLLIN)) != 0, (events & (INET_POLLWRNORM | INET_POLLOUT)) != 0, (events & (INET_POLLRDBAND | INET_POLLPRI)) != 0 };
		}
	}

	if (nfds == 0) {
		return { 0, true, EAGAIN };
	}
	if (CheckHostSockets(checks.data(), hostCount) < 0) {
		UpdateErrnoFromHost(threadID, socket_errno, "sceNetInetPoll");
		return { -1, false, 0 };
	}

	int retval = 0;
	int check = 0;
	for (int i = 0; i < nfds; i++) {
		if (!open[i]) {
			// As on hardware (pspautotests net/inet/poll).
			fdarray[i].revents = INET_POLLNVAL;
			retval++;
			continue;
		}
		// Only what was asked for: no POLLHUP or POLLERR, even for a peer that closed or reset the
		// connection (pspautotests net/inet/polledge).
		const HostSocketReady &ready = checks[check++];
		if (ready.read)
			fdarray[i].revents |= (INET_POLLRDNORM | INET_POLLIN);
		if (ready.write)
			fdarray[i].revents |= (INET_POLLWRNORM | INET_POLLOUT);
		if (ready.except)
			fdarray[i].revents |= (INET_POLLRDBAND | INET_POLLPRI);
		fdarray[i].revents &= fdarray[i].events;
		if (fdarray[i].revents)
			retval++;
		VERBOSE_LOG(Log::sceNet, "Poll Socket#%d Fd: %d, events: %04x, revents: %04x", i, fdarray[i].fd, fdarray[i].events, fdarray[i].revents);
	}
	if (retval == 0) {
		return { 0, true, EAGAIN };
	}
	return { retval, false, 0 };
}

// args: socket, buf, len, flags.
static InetTry TryRecv(SceUID threadID, const u32 *args) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket((int)args[0], &inetSock)) {
		UpdateErrnoFromHost(threadID, EBADF, "sceNetInetRecv");
		return { -1, false, 0 };
	}
	int flgs = args[3] & ~PSP_NET_INET_MSG_DONTWAIT; // removing non-POSIX flag, which is an alternative way to use non-blocking mode
	flgs = convertMSGFlagsPSP2Host(flgs);
	int retval = recv(inetSock->sock, (char*)Memory::GetPointerOrException(args[1]), args[2], flgs | MSG_NOSIGNAL);
	if (retval < 0) {
		const int hostErrno = socket_errno;
		if (IsWouldBlock(hostErrno)) {
			return { retval, true, hostErrno };
		}
		UpdateErrnoFromHost(threadID, hostErrno, "sceNetInetRecv");
		return { retval, false, 0 };
	}

	std::string datahex;
	DataToHexString(10, 0, Memory::GetPointerOrException(args[1]), retval, &datahex);
	VERBOSE_LOG(Log::sceNet, "Data Dump (%d bytes):\n%s", retval, datahex.c_str());
	return { retval, false, 0 };
}

// args: socket, buf, len, flags.
static InetTry TrySend(SceUID threadID, const u32 *args) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket((int)args[0], &inetSock)) {
		UpdateErrnoFromHost(threadID, EBADF, "sceNetInetSend");
		return { -1, false, 0 };
	}
	int flgs = args[3] & ~PSP_NET_INET_MSG_DONTWAIT; // removing non-POSIX flag, which is an alternative way to use non-blocking mode
	flgs = convertMSGFlagsPSP2Host(flgs);
	int retval = send(inetSock->sock, (char*)Memory::GetPointerOrException(args[1]), args[2], flgs | MSG_NOSIGNAL);
	if (retval < 0) {
		const int hostErrno = socket_errno;
		if (IsWouldBlock(hostErrno)) {
			return { retval, true, hostErrno };
		}
		SetSendErrno(threadID, inetSock, hostErrno, "sceNetInetSend");
	}
	return { retval, false, 0 };
}

static InetTry TryInetOp(InetWaitOp op, SceUID threadID, const u32 *args, bool firstTry) {
	switch (op) {
	case InetWaitOp::Select: return TrySelect(threadID, args, firstTry);
	case InetWaitOp::Poll: return TryPoll(threadID, args);
	case InetWaitOp::Recv: return TryRecv(threadID, args);
	case InetWaitOp::RecvFrom: return TryRecvFrom(threadID, args);
	case InetWaitOp::Send: return TrySend(threadID, args);
	case InetWaitOp::SendTo: return TrySendTo(threadID, args);
	case InetWaitOp::Accept: return TryAccept(threadID, args);
	case InetWaitOp::Connect: return TryConnectDone(threadID, args);
	default: return { -1, false, 0 };
	}
}

// What a call returns when it would still block and won't wait any longer.
static int InetWouldBlockResult(InetWaitOp op, SceUID threadID, const u32 *args, int hostErrno, const char *func) {
	switch (op) {
	case InetWaitOp::Select:
		ZeroInetFdSets(args[1], args[2], args[3]);
		return 0;
	case InetWaitOp::Poll:
		// Every revents is already 0.
		return 0;
	default:
		UpdateErrnoFromHost(threadID, hostErrno, func);
		return -1;
	}
}

// Called when the first attempt would block. Returns true if the thread now waits for the call to
// complete, and the result is delivered when it wakes.
static bool StartInetWait(InetWaitOp op, const u32 *args, int numArgs, s64 timeoutUs) {
	if (timeoutUs == 0 || !__KernelIsDispatchEnabled() || __IsInInterrupt()) {
		return false;
	}
	const SceUID threadID = __KernelGetCurThread();
	InetWait &wait = g_inetWaits[threadID];
	wait = {};
	wait.op = op;
	memcpy(wait.args, args, numArgs * sizeof(u32));
	wait.deadline = timeoutUs < 0 ? -1 : CoreTiming::GetTicks(currentMIPS) + usToCycles(timeoutUs);
	s64 firstPoll = usToCycles(INET_WAIT_POLL_US);
	if (timeoutUs > 0) {
		firstPoll = std::min(firstPoll, (s64)usToCycles(timeoutUs));
	}
	CoreTiming::ScheduleEvent(firstPoll, eventInetWaitPoll, threadID);
	__KernelWaitCurThread(WAITTYPE_NET, INET_WAIT_ID, 0, 0, false, "inet wait");
	return true;
}

static void __NetInetWaitPoll(u64 userdata, int cyclesLate) {
	const SceUID threadID = (SceUID)userdata;
	auto it = g_inetWaits.find(threadID);
	if (it == g_inetWaits.end()) {
		return;
	}
	u32 error;
	if (__KernelGetWaitID(threadID, WAITTYPE_NET, error) != INET_WAIT_ID || error != 0) {
		// Woken some other way, or gone.
		g_inetWaits.erase(it);
		return;
	}

	const InetWait wait = it->second;
	const InetTry attempt = TryInetOp(wait.op, threadID, wait.args, false);
	const s64 now = CoreTiming::GetTicks(currentMIPS);
	if (attempt.wouldBlock && (wait.deadline < 0 || now < wait.deadline)) {
		s64 next = usToCycles(INET_WAIT_POLL_US);
		if (wait.deadline >= 0) {
			next = std::min(next, wait.deadline - now);
		}
		CoreTiming::ScheduleEvent(next, eventInetWaitPoll, userdata);
		return;
	}

	g_inetWaits.erase(it);
	int result = attempt.result;
	if (attempt.wouldBlock) {
		// Timed out.
		result = InetWouldBlockResult(wait.op, threadID, wait.args, attempt.hostErrno, "inet wait");
	}
	DEBUG_LOG(Log::sceNet, "Inet wait (op %d) of thread %d done: %d", (int)wait.op, threadID, result);
	__KernelResumeThreadFromWait(threadID, result);
	__KernelReSchedule("inet wait done");
}

// FIXME: nfds is number of fd(s) as in posix poll, or was it maximum fd value as in posix select? Star Wars Battlefront Renegade seems to set the nfds to 64, while Coded Arms Contagion is using 256
int sceNetInetSelect(int nfds, u32 readfdsPtr, u32 writefdsPtr, u32 exceptfdsPtr, u32 timeoutPtr) {
	InetThreadSeen();
	for (u32 ptr : { readfdsPtr, writefdsPtr, exceptfdsPtr }) {
		if (ptr && !InetValidRange(ptr, sizeof(SceNetInetFdSet), __FUNCTION__)) {
			return hleLogError(Log::sceNet, -1, "bad fd set %08x", ptr);
		}
	}
	if (timeoutPtr && !InetValidRange(timeoutPtr, sizeof(SceNetInetTimeval), __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad timeout %08x", timeoutPtr);
	}
	SceNetInetTimeval *timeout = timeoutPtr ? (SceNetInetTimeval*)Memory::GetPointerWriteOrException(timeoutPtr) : nullptr;

	if (nfds > 256) {
		// Probably never happens, just for safety.
		ERROR_LOG(Log::sceNet, "Bad nfds value, resetting to 256: %d", nfds);
		nfds = 256;
	}

	if (timeout && timeout->tv_usec >= 1000000) {
		// Rejected before looking at the sets (pspautotests net/inet/blocking).
		UpdateErrnoFromHost(__KernelGetCurThread(), EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "bad tv_usec %d", (int)timeout->tv_usec);
	}

	const u32 args[] = { (u32)nfds, readfdsPtr, writefdsPtr, exceptfdsPtr };
	// A null timeout waits for as long as it takes.
	const s64 waitUs = timeout ? (s64)timeout->tv_sec * 1000000 + timeout->tv_usec : -1;
	InetTry attempt = TrySelect(__KernelGetCurThread(), args, true);
	if (attempt.wouldBlock) {
		if (StartInetWait(InetWaitOp::Select, args, ARRAY_SIZE(args), waitUs)) {
			return hleLogDebug(Log::sceNet, 0, "waiting, timeout %lld us", (long long)waitUs);
		}
		attempt.result = InetWouldBlockResult(InetWaitOp::Select, __KernelGetCurThread(), args, attempt.hostErrno, __FUNCTION__);
	}
	return hleDelayResult(hleLogDebug(Log::sceNet, attempt.result), "workaround until blocking-socket", 500); // Using hleDelayResult as a workaround for games that need blocking-socket to be implemented (ie. Coded Arms Contagion)
}

int sceNetInetPoll(MIPSState *mips, u32 fdsPtr, u32 nfds, int timeout) {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetInetPoll(%08x, %d, %i) at %08x", fdsPtr, nfds, timeout, mips->pc);
	InetThreadSeen();
	if (nfds > PSP_NET_INET_FD_SETSIZE)
		nfds = PSP_NET_INET_FD_SETSIZE;
	if (!InetValidRange(fdsPtr, nfds * (u32)sizeof(SceNetInetPollfd), __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad fds %08x", fdsPtr);
	}
	SceNetInetPollfd *fdarray = (SceNetInetPollfd*)Memory::GetPointerOrException(fdsPtr);
	// A negative fd isn't open, so it gets POLLNVAL like any other (pspautotests net/inet/polledge).

	const u32 args[] = { fdsPtr, nfds };
	// In milliseconds, like POSIX poll() (pspautotests net/inet/blocking). Negative waits for as long as it takes.
	const s64 waitUs = timeout < 0 ? -1 : (s64)timeout * 1000;
	InetTry attempt = TryPoll(__KernelGetCurThread(), args);
	if (attempt.wouldBlock) {
		if (StartInetWait(InetWaitOp::Poll, args, ARRAY_SIZE(args), waitUs)) {
			return hleLogDebug(Log::sceNet, 0, "waiting, timeout %lld us", (long long)waitUs);
		}
		attempt.result = 0;
	}
	return hleDelayResult(hleLogDebug(Log::sceNet, attempt.result), "workaround until blocking-socket", 1000); // Using hleDelayResult as a workaround for games that need blocking-socket to be implemented
}

static int sceNetInetRecv(int socket, u32 bufPtr, u32 bufLen, u32 flags) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	if (!InetValidRange(bufPtr, bufLen, __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad buffer %08x, %d bytes", bufPtr, bufLen);
	}

	const u32 args[] = { (u32)socket, bufPtr, bufLen, flags };
	InetTry attempt = TryRecv(__KernelGetCurThread(), args);
	if (attempt.wouldBlock) {
		if (StartInetWait(InetWaitOp::Recv, args, ARRAY_SIZE(args), SocketWaitUs(inetSock, flags, false))) {
			return hleLogDebug(Log::sceNet, 0, "waiting");
		}
		attempt.result = InetWouldBlockResult(InetWaitOp::Recv, __KernelGetCurThread(), args, attempt.hostErrno, __FUNCTION__);
		return hleDelayResult(hleLogDebug(Log::sceNet, attempt.result, "EAGAIN"), "workaround until blocking-socket", 500);
	}
	if (attempt.result < 0) {
		return hleDelayResult(hleLogError(Log::sceNet, attempt.result), "workaround until blocking-socket", 500);
	}
	return hleDelayResult(hleLogInfo(Log::sceNet, attempt.result), "workaround until blocking-socket", 500); // Using hleDelayResult as a workaround for games that need blocking-socket to be implemented
}

static int sceNetInetSend(int socket, u32 bufPtr, u32 bufLen, u32 flags) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}
	if (!InetValidRange(bufPtr, bufLen, __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad buffer %08x, %d bytes", bufPtr, bufLen);
	}

	std::string datahex;
	DataToHexString(10, 0, Memory::GetPointerOrException(bufPtr), bufLen, &datahex);
	VERBOSE_LOG(Log::sceNet, "Data Dump (%d bytes):\n%s", bufLen, datahex.c_str());

	const u32 args[] = { (u32)socket, bufPtr, bufLen, flags };
	InetTry attempt = TrySend(__KernelGetCurThread(), args);
	if (attempt.wouldBlock) {
		if (StartInetWait(InetWaitOp::Send, args, ARRAY_SIZE(args), SocketWaitUs(inetSock, flags, true))) {
			return hleLogDebug(Log::sceNet, 0, "waiting");
		}
		return hleLogDebug(Log::sceNet, InetWouldBlockResult(InetWaitOp::Send, __KernelGetCurThread(), args, attempt.hostErrno, __FUNCTION__), "EAGAIN");
	}
	if (attempt.result < 0) {
		return hleLogError(Log::sceNet, attempt.result);
	}
	return hleLogInfo(Log::sceNet, attempt.result);
}

static int sceNetInetSocket(MIPSState *mips, int domain, int type, int protocol) {
	INFO_LOG(Log::sceNet, "sceNetInetSocket(%d, %d, %d) at %08x - Socket: Domain = %s, Type = %s, Protocol = %s",
		domain, type, protocol, mips->pc, inetSocketDomain2str(domain).c_str(), inetSocketType2str(type).c_str(), inetSocketProto2str(protocol).c_str());

	// Anything but an AF_INET TCP, UDP or raw socket, with a protocol that fits, is EPROTONOSUPPORT
	// (pspautotests net/inet/errors).
	const bool typeOk = type == PSP_NET_INET_SOCK_STREAM || type == PSP_NET_INET_SOCK_DGRAM || type == PSP_NET_INET_SOCK_RAW;
	const bool protocolOk = protocol == 0 || type == PSP_NET_INET_SOCK_RAW ||
		(type == PSP_NET_INET_SOCK_STREAM && protocol == PSP_NET_INET_IPPROTO_TCP) ||
		(type == PSP_NET_INET_SOCK_DGRAM && protocol == PSP_NET_INET_IPPROTO_UDP);
	if (domain != PSP_NET_INET_AF_INET || !typeOk || !protocolOk) {
		SetInetErrno(ERROR_INET_EPROTONOSUPPORT, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "unsupported socket %d, %d, %d", domain, type, protocol);
	}

	int socket;
	int hostErrno = 0;
	InetSocket *inetSock = g_socketManager.CreateSocket(&socket, &hostErrno, SocketState::UsedNetInet, domain, type, protocol);
	if (!inetSock) {
		UpdateErrnoFromHost(__KernelGetCurThread(), hostErrno, __FUNCTION__);
		return hleLogError(Log::sceNet, -1);
	}

	// Ignore SIGPIPE when supported (ie. BSD/MacOS)
	setSockNoSIGPIPE(inetSock->sock, 1);
	// TODO: We should always use non-blocking mode and simulate blocking mode
	changeBlockingMode(inetSock->sock, 1);
	// Enable Port Re-use, required for multiple-instance
	setSockReuseAddrPort(inetSock->sock);
	// Disable Connection Reset error on UDP to avoid strange behavior
	setUDPConnReset(inetSock->sock, false);
	if (type == PSP_NET_INET_SOCK_STREAM) {
		// Keeps the host from buffering far more than the PSP's few dozen KB.
		const int sndBuf = 65536;
		setsockopt(inetSock->sock, SOL_SOCKET, SO_SNDBUF, (const char *)&sndBuf, sizeof(sndBuf));
	}
	ClearInetErrno();
	return hleLogDebug(Log::sceNet, socket);
}

// The socket-level options work like 4.4BSD's sosetopt() and sogetopt(): they're kept in InetSocket,
// and only the ones that change what the host socket does are passed on (pspautotests net/inet/options).
static bool IsInetFlagOption(int optname) {
	switch (optname) {
	case PSP_NET_INET_SO_DEBUG:
	case PSP_NET_INET_SO_REUSEADDR:
	case PSP_NET_INET_SO_KEEPALIVE:
	case PSP_NET_INET_SO_DONTROUTE:
	case PSP_NET_INET_SO_BROADCAST:
	case PSP_NET_INET_SO_USELOOPBACK:
	case PSP_NET_INET_SO_OOBINLINE:
	case PSP_NET_INET_SO_REUSEPORT:
	case PSP_NET_INET_SO_TIMESTAMP:
		return true;
	default:
		return false;
	}
}

// Flags the host socket gets too. SO_REUSEADDR and SO_REUSEPORT are always on there, for multiple instances.
static bool IsInetHostFlagOption(int optname) {
	return optname == PSP_NET_INET_SO_KEEPALIVE || optname == PSP_NET_INET_SO_DONTROUTE || optname == PSP_NET_INET_SO_BROADCAST || optname == PSP_NET_INET_SO_OOBINLINE;
}

// Other levels go to the protocol: a TCP socket takes TCP and IP options, any other just IP ones.
// Anything else is EINVAL, as in 4.4BSD's tcp_ctloutput() and ip_ctloutput().
static bool IsInetProtocolLevel(const InetSocket *inetSock, int level) {
	return level == PSP_NET_INET_IPPROTO_IP || (level == PSP_NET_INET_IPPROTO_TCP && inetSock->type == PSP_NET_INET_SOCK_STREAM);
}

// 4.4BSD's sb_max as sbreserve() scales it, with 256-byte mbufs (pspautotests net/inet/limits).
static const int INET_MAX_SOCKET_BUFFER = 262144 * 2048 / (256 + 2048);

static int InetSetsockopt(MIPSState *mips, int socket, int level, int optname, u32 optvalPtr, int optlen) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	const u32 optval = optlen >= 4 && Memory::IsValidRange(optvalPtr, 4) ? Memory::ReadUnchecked_U32(optvalPtr) : 0;
	INFO_LOG(Log::sceNet, "sceNetInetSetsockopt(%i, %i, %i, %08x, %i) at %08x: Level = %s, OptName = %s, OptValue = %d",
		socket, level, optname, optvalPtr, optlen, mips->pc,
		inetSockoptLevel2str(level).c_str(), inetSockoptName2str(optname, level).c_str(), optval);

	if (level != PSP_NET_INET_SOL_SOCKET) {
		if (!IsInetProtocolLevel(inetSock, level)) {
			SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
			return hleLogError(Log::sceNet, -1, "bad level %d", level);
		}
		if (optlen < 0 || !InetValidRange(optvalPtr, optlen, __FUNCTION__)) {
			return hleLogError(Log::sceNet, -1, "invalid arg");
		}
		int retval = setsockopt(inetSock->sock, convertSockoptLevelPSP2Host(level), convertSockoptNamePSP2Host(optname, level), (const char *)Memory::GetPointerUnchecked(optvalPtr), optlen);
		if (retval < 0) {
			UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
			return hleLogError(Log::sceNet, retval);
		}
		if (level == PSP_NET_INET_IPPROTO_IP && optname == PSP_NET_INET_IP_TTL && optlen >= 4) {
			inetSock->ipTtl = (int)optval;
		}
		return hleLogDebug(Log::sceNet, 0);
	}

	switch (optname) {
	case PSP_NET_INET_SO_NOSIGPIPE: // WARNING: Not sure about this one. But we definitely don't want signals, so we ignore it.
		return hleLogWarning(Log::sceNet, 0, "NOSIGPIPE should never be modified (should always be off)");
	case PSP_NET_INET_SO_ONESBCAST:
		// Seen in Outrun 2006 (account registration), assuming that the flag mapping is correct, we can't support this. So we warn-log and pretend success.
		return hleLogWarning(Log::sceNet, 0, "PSP_NET_INET_SO_ONESBCAST unsupported, ignoring");
	default:
		break;
	}

	if (optlen < (optname == PSP_NET_INET_SO_LINGER ? (int)sizeof(SceNetInetLinger) : 4)) {
		SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "optlen %d too short", optlen);
	}
	if (!InetValidRange(optvalPtr, optname == PSP_NET_INET_SO_LINGER ? sizeof(SceNetInetLinger) : 4, __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "invalid arg");
	}

	switch (optname) {
	case PSP_NET_INET_SO_LINGER:
	{
		const SceNetInetLinger *pspLinger = (const SceNetInetLinger *)Memory::GetPointerUnchecked(optvalPtr);
		inetSock->soOptions = pspLinger->l_onoff ? (inetSock->soOptions | PSP_NET_INET_SO_LINGER) : (inetSock->soOptions & ~PSP_NET_INET_SO_LINGER);
		inetSock->lingerTime = pspLinger->l_linger;
		struct linger hostLinger{};
		hostLinger.l_onoff = pspLinger->l_onoff ? 1 : 0;
		hostLinger.l_linger = pspLinger->l_linger;
		if (setsockopt(inetSock->sock, SOL_SOCKET, SO_LINGER, (const char *)&hostLinger, sizeof(hostLinger)) < 0) {
			WARN_LOG(Log::sceNet, "Host SO_LINGER failed: %d", socket_errno);
		}
		return hleLogDebug(Log::sceNet, 0);
	}

	case PSP_NET_INET_SO_SNDBUF:
	case PSP_NET_INET_SO_RCVBUF:
		if ((s32)optval < 1) {
			SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
			return hleLogError(Log::sceNet, -1, "bad buffer size %d", (s32)optval);
		}
		// UNO asks for a very large one, and needs this to fail.
		if ((s32)optval > INET_MAX_SOCKET_BUFFER) {
			SetInetErrno(ERROR_INET_ENOBUFS, __FUNCTION__);
			return hleLogError(Log::sceNet, -1, "buffer size %d too large", (s32)optval);
		}
		(optname == PSP_NET_INET_SO_SNDBUF ? inetSock->sndBuf : inetSock->rcvBuf) = (int)optval;
		if (setsockopt(inetSock->sock, SOL_SOCKET, optname == PSP_NET_INET_SO_SNDBUF ? SO_SNDBUF : SO_RCVBUF, (const char *)&optval, sizeof(optval)) < 0) {
			WARN_LOG(Log::sceNet, "Host buffer size %d failed: %d", (int)optval, socket_errno);
		}
		return hleLogDebug(Log::sceNet, 0);

	case PSP_NET_INET_SO_SNDLOWAT:
	case PSP_NET_INET_SO_RCVLOWAT:
		if ((s32)optval < 1) {
			SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
			return hleLogError(Log::sceNet, -1, "bad low-water mark %d", (s32)optval);
		}
		(optname == PSP_NET_INET_SO_SNDLOWAT ? inetSock->sndLowat : inetSock->rcvLowat) = (int)optval;
		return hleLogDebug(Log::sceNet, 0);

	case PSP_NET_INET_SO_SNDTIMEO:
	case PSP_NET_INET_SO_RCVTIMEO:
		// A single 32-bit value in microseconds. A timeval's tv_sec is read as one.
		(optname == PSP_NET_INET_SO_RCVTIMEO ? inetSock->recvTimeoutUs : inetSock->sendTimeoutUs) = optval;
		return hleLogDebug(Log::sceNet, 0);

	case PSP_NET_INET_SO_NBIO:
		inetSock->nonblocking = optval != 0;
		return hleLogDebug(Log::sceNet, 0);

	default:
		break;
	}

	if (!IsInetFlagOption(optname)) {
		SetInetErrno(ERROR_INET_ENOPROTOOPT, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "unsupported option %04x", optname);
	}
	inetSock->soOptions = optval ? (inetSock->soOptions | optname) : (inetSock->soOptions & ~optname);
	if (IsInetHostFlagOption(optname)) {
		const int hostVal = optval ? 1 : 0;
		if (setsockopt(inetSock->sock, SOL_SOCKET, convertSockoptNamePSP2Host(optname, level), (const char *)&hostVal, sizeof(hostVal)) < 0) {
			WARN_LOG(Log::sceNet, "Host %s failed: %d", inetSockoptName2str(optname, level).c_str(), socket_errno);
		}
	}
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetInetSetsockopt(MIPSState *mips, int socket, int level, int optname, u32 optvalPtr, int optlen) {
	const int result = InetSetsockopt(mips, socket, level, optname, optvalPtr, optlen);
	if (result >= 0) {
		ClearInetErrno();
	}
	return result;
}

static int sceNetInetGetsockopt(MIPSState *mips, int socket, int level, int optname, u32 optvalPtr, u32 optlenPtr) {
	DEBUG_LOG(Log::sceNet, "sceNetInetGetsockopt(%i, %i, %i, %08x, %08x) at %08x: Level = %s, OptName = %s", socket, level, optname, optvalPtr, optlenPtr, mips->pc,
		inetSockoptLevel2str(level).c_str(), inetSockoptName2str(optname, level).c_str());

	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}
	if (!InetValidRange(optlenPtr, 4, __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad optlen %08x", optlenPtr);
	}
	const u32 bufLen = Memory::ReadUnchecked_U32(optlenPtr);

	// Room for the longest, IP_OPTIONS' 40 bytes.
	alignas(4) u8 value[40]{};
	u32 valueLen = 4;
	if (level != PSP_NET_INET_SOL_SOCKET) {
		if (!IsInetProtocolLevel(inetSock, level)) {
			SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
			return hleLogError(Log::sceNet, -1, "bad level %d", level);
		}
		socklen_t hostLen = sizeof(value);
		if (level == PSP_NET_INET_IPPROTO_IP && optname == PSP_NET_INET_IP_TTL) {
			*(u32_le *)value = inetSock->ipTtl;
		} else if (getsockopt(inetSock->sock, convertSockoptLevelPSP2Host(level), convertSockoptNamePSP2Host(optname, level), (char *)value, &hostLen) < 0) {
			UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
			return hleLogError(Log::sceNet, -1);
		} else {
			valueLen = (u32)hostLen;
		}
		if (level == PSP_NET_INET_IPPROTO_TCP && optname == PSP_NET_INET_TCP_NODELAY) {
			// BSD's TF_NODELAY flag, as an int (pspautotests net/inet/options). Winsock may give a single byte.
			const bool nodelay = valueLen == 1 ? value[0] != 0 : *(const u32_le *)value != 0;
			*(u32_le *)value = nodelay ? 4 : 0;
			valueLen = 4;
		}
	} else {
		u32_le *words = (u32_le *)value;
		switch (optname) {
		case PSP_NET_INET_SO_LINGER:
			words[0] = inetSock->soOptions & PSP_NET_INET_SO_LINGER;
			words[1] = inetSock->lingerTime;
			valueLen = 8;
			break;
		case PSP_NET_INET_SO_SNDBUF: words[0] = inetSock->sndBuf; break;
		case PSP_NET_INET_SO_RCVBUF: words[0] = inetSock->rcvBuf; break;
		case PSP_NET_INET_SO_SNDLOWAT: words[0] = inetSock->sndLowat; break;
		case PSP_NET_INET_SO_RCVLOWAT: words[0] = inetSock->rcvLowat; break;
		// Read back in 10 ms clock ticks: 250000 reads as 25 (pspautotests net/inet/options).
		case PSP_NET_INET_SO_SNDTIMEO: words[0] = inetSock->sendTimeoutUs / 10000; break;
		case PSP_NET_INET_SO_RCVTIMEO: words[0] = inetSock->recvTimeoutUs / 10000; break;
		case PSP_NET_INET_SO_TYPE: words[0] = inetSock->type; break;
		case PSP_NET_INET_SO_NBIO: words[0] = inetSock->nonblocking ? 0x80 : 0; break;
		case PSP_NET_INET_SO_NOSIGPIPE: words[0] = 0; break;
		case PSP_NET_INET_SO_ERROR:
		{
			int hostError = 0;
			socklen_t hostLen = sizeof(hostError);
			if (getsockopt(inetSock->sock, SOL_SOCKET, SO_ERROR, (char *)&hostError, &hostLen) < 0) {
				UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
				return hleLogError(Log::sceNet, -1);
			}
			// An SCE error code, not an errno: ECONNREFUSED reads as 0x8001006F (pspautotests net/inet/tcp).
			words[0] = hostError ? (0x80010000 | convertInetErrnoHost2PSP(hostError)) : 0;
			break;
		}
		default:
			if (!IsInetFlagOption(optname)) {
				SetInetErrno(ERROR_INET_ENOPROTOOPT, __FUNCTION__);
				return hleLogError(Log::sceNet, -1, "unsupported option %04x", optname);
			}
			words[0] = inetSock->soOptions & optname;
			break;
		}
	}

	// Copies as much as fits, and says how much that was (pspautotests net/inet/options).
	const u32 copyLen = std::min(valueLen, bufLen);
	if (!InetValidRange(optvalPtr, copyLen, __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad optval %08x", optvalPtr);
	}
	if (copyLen > 0) {
		Memory::Memcpy(optvalPtr, value, copyLen, "InetGetsockopt");
	}
	Memory::WriteUnchecked_U32(copyLen, optlenPtr);
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetInetBind(MIPSState *mips, int socket, u32 namePtr, int namelen) {
	INFO_LOG(Log::sceNet, "sceNetInetBind(%i, %08x, %i) at %08x", socket, namePtr, namelen, mips->pc);

	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	if (!InetValidRange(namePtr, sizeof(SceNetInetSockaddr), __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad address %08x", namePtr);
	}
	SceNetInetSockaddr* name = (SceNetInetSockaddr*)Memory::GetPointerOrException(namePtr);
	// (Only lengths under 16 have been seen to fail. 4.4BSD fails any but 16.)
	if (namelen < (int)sizeof(SceNetInetSockaddr)) {
		SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "namelen %d", namelen);
	}
	if (name->sa_family != PSP_NET_INET_AF_INET && ((const SceNetInetSockaddrIn *)name)->sin_addr != INADDR_ANY) {
		// Not found among the local addresses, whose family is compared too (pspautotests net/inet/errors).
		SetInetErrno(ERROR_INET_EADDRNOTAVAIL, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "family %d", name->sa_family);
	}
	SockAddrIN4 saddr{};
	// TODO: Should've created convertSockaddrPSP2Host (and Host2PSP too) function as it's being used pretty often, thus fixing a bug on it will be tedious when scattered all over the places
	saddr.addr.sa_family = name->sa_family;
	int len = std::min(namelen > 0 ? namelen : 0, static_cast<int>(sizeof(saddr)));
	memcpy(saddr.addr.sa_data, name->sa_data, sizeof(name->sa_data));
	// The address rewrites below are for talking to ad hoc peers (each instance on localhost gets its own
	// 127.0.0.x), so they only apply while the game uses ad hoc. Infrastructure binds what the game asked for.
	const bool adhocActive = netAdhocctlInited;
	if (adhocActive && isLocalServer) {
		getLocalIp(&saddr.in);
	}
	// FIXME: On non-Windows broadcast to INADDR_BROADCAST(255.255.255.255) might not be received by the sender itself when binded to specific IP (ie. 192.168.0.2) or INADDR_BROADCAST.
	//        Meanwhile, it might be received by itself when binded to subnet (ie. 192.168.0.255) or INADDR_ANY(0.0.0.0).
	//
	// Replace INADDR_ANY (and INADDR_BROADCAST too) with a specific IP (using AdhocServer IP address as reference) in order not to send data through the wrong interface (especially during broadcast),
	// But let's do this only when using built-in Adhoc Server, otherwise UNO won't works
	if (adhocActive && g_Config.bEnableAdhocServer && (saddr.in.sin_addr.s_addr == INADDR_ANY || saddr.in.sin_addr.s_addr == INADDR_BROADCAST)) {
		// Get Local IP Address
		sockaddr_in sockAddr{};
		getLocalIp(&sockAddr);
		INFO_LOG(Log::sceNet, "Bind: Address Replacement = %s => %s", ip2str(saddr.in.sin_addr).c_str(), ip2str(sockAddr.sin_addr).c_str());
		saddr.in.sin_addr.s_addr = sockAddr.sin_addr.s_addr;
	}
	// TODO: Make use Port Offset only for PPSSPP to PPSSPP communications (ie. IP addresses available in the group/friendlist), otherwise should be considered as Online Service thus should use the port as is.
	//saddr.in.sin_port = htons(ntohs(saddr.in.sin_port) + portOffset);

	INFO_LOG(Log::sceNet, "sceNetInetBind: Family = %s, Address = %s, Port = %d", inetSocketDomain2str(saddr.addr.sa_family).c_str(), ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	// Update socket debug metadata
	inetSock->addr = ip2str(saddr.in.sin_addr);
	inetSock->port = ntohs(saddr.in.sin_port);

	changeBlockingMode(inetSock->sock, 0);
	int retval = bind(inetSock->sock, (struct sockaddr*)&saddr, len);
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		changeBlockingMode(inetSock->sock, 1);
		return hleLogError(Log::sceNet, retval);
	}
	changeBlockingMode(inetSock->sock, 1);
	// Update binded port number if it was 0 (any port)
	memcpy(name->sa_data, saddr.addr.sa_data, sizeof(name->sa_data));
	// Enable Port-forwarding

	// Check the socket type/protocol for SOCK_STREAM/SOCK_DGRAM or IPPROTO_TCP/IPPROTO_UDP instead of forwarding both protocols like in ANR2ME's original change.
	// Port 0 is a port the host picks, which there's no point forwarding.
	unsigned short port = ntohs(saddr.in.sin_port);
	if (port != 0) {
		if (inetSock->upnpPort && inetSock->upnpPort != port) {
			UPnP_Remove(InetSocketUPnPProtocol(inetSock), inetSock->upnpPort);
		}
		UPnP_Add(InetSocketUPnPProtocol(inetSock), port, port);
		inetSock->upnpPort = port;
	}

	// Workaround: Send a dummy 0 size message to AdhocServer IP to make sure the socket actually bound to an address when binded with INADDR_ANY before using getsockname, seems to fix sending DGRAM from incorrect port issue on Android
	/*saddr.in.sin_addr.s_addr = g_adhocServerIP.in.sin_addr.s_addr;
	saddr.in.sin_port = 0;
	sendto(socket, dummyPeekBuf64k, 0, MSG_NOSIGNAL, (struct sockaddr*)&saddr, sizeof(saddr));
	*/
	return hleLogInfo(Log::sceNet, retval);
}

// args: socket. Checks a connect() that was in progress.
static InetTry TryConnectDone(SceUID threadID, const u32 *args) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket((int)args[0], &inetSock)) {
		UpdateErrnoFromHost(threadID, EBADF, "sceNetInetConnect");
		return { -1, false, 0 };
	}
	HostSocketReady check = { inetSock->sock, false, true, true };
	int ready = CheckHostSockets(&check, 1);
	if (ready == 0) {
		return { -1, true, EINPROGRESS };
	}
	int sockError = 0;
	socklen_t len = sizeof(sockError);
	if (ready < 0) {
		sockError = socket_errno;
	} else if (getsockopt(inetSock->sock, SOL_SOCKET, SO_ERROR, (char *)&sockError, &len) < 0) {
		sockError = socket_errno;
	}
	if (sockError != 0) {
		UpdateErrnoFromHost(threadID, sockError, "sceNetInetConnect");
		return { -1, false, 0 };
	}
	return { 0, false, 0 };
}

static int sceNetInetConnect(int socket, u32 sockAddrPtr, int sockAddrLen) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	// Still using warn log here so it stands out in the log

	if (!InetValidRange(sockAddrPtr, sizeof(SceNetInetSockaddr), __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad address %08x", sockAddrPtr);
	}
	SceNetInetSockaddr* dst = (SceNetInetSockaddr*)Memory::GetPointerOrException(sockAddrPtr);
	SockAddrIN4 saddr{};
	int dstlen = std::min(sockAddrLen > 0 ? sockAddrLen : 0, static_cast<int>(sizeof(saddr)));
	saddr.addr.sa_family = dst->sa_family;
	memcpy(saddr.addr.sa_data, dst->sa_data, sizeof(dst->sa_data));

	int retval = connect(inetSock->sock, (struct sockaddr*)&saddr.addr, dstlen);
	int hostErrno = socket_errno;
	if (retval < 0 && connectInProgress(hostErrno)) {
		// A blocking socket waits for the connection to be made, or to fail.
		const u32 args[] = { (u32)socket };
		if (StartInetWait(InetWaitOp::Connect, args, ARRAY_SIZE(args), SocketWaitUs(inetSock, 0, true))) {
			return hleLogDebug(Log::sceNet, 0, "waiting to connect to %s, port %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
		}
	}
	if (retval < 0) {
		int pspErrno = UpdateErrnoFromHost(__KernelGetCurThread(), hostErrno, __FUNCTION__);
		if (connectInProgress(hostErrno))
			retval = hleLogDebug(Log::sceNet, retval, "errno = %s Address = %s, Port = %d", convertInetErrno2str(pspErrno), ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
		else
			retval = hleLogError(Log::sceNet, retval, "errno = %s Address = %s, Port = %d", convertInetErrno2str(pspErrno), ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
		
		return retval;
	}

	if (saddr.in.sin_port == 53) {
		WARN_LOG(Log::G3D, "Game connected to DNS server %s (port 53), likely for doing its own DNS lookups!", ip2str(saddr.in.sin_addr, false).c_str());
		// We should sniff these messages...
	}

	return hleLogInfo(Log::sceNet, retval, "Connect: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
}

static int sceNetInetListen(int socket, int backlog) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	int retval = listen(inetSock->sock, (backlog == PSP_NET_INET_SOMAXCONN ? SOMAXCONN : backlog));
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}

	return hleLogInfo(Log::sceNet, retval);
}

// args: socket, addr, addrlen.
static InetTry TryAccept(SceUID threadID, const u32 *args) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket((int)args[0], &inetSock)) {
		UpdateErrnoFromHost(threadID, EBADF, "sceNetInetAccept");
		return { -1, false, 0 };
	}

	// The game's address is written afterwards, as much of it as its length allows.
	SockAddrIN4 saddr{};
	socklen_t hostLen = sizeof(saddr);
	int newHostSocket = accept(inetSock->sock, (struct sockaddr*)&saddr.addr, &hostLen);
	if (newHostSocket < 0) {
		const int hostErrno = socket_errno;
		if (IsWouldBlock(hostErrno)) {
			return { -1, true, hostErrno };
		}
		UpdateErrnoFromHost(threadID, hostErrno, "sceNetInetAccept");
		return { -1, false, 0 };
	}
	// Linux doesn't pass the listening socket's non-blocking mode on to the accepted one.
	changeBlockingMode(newHostSocket, 1);

	int newSocketId;
	InetSocket *newInetSocket = g_socketManager.AdoptSocket(&newSocketId, newHostSocket, inetSock);
	if (!newInetSocket) {
		ERROR_LOG(Log::sceNet, "Accept: out of socket IDs");
		closesocket(newHostSocket);
		UpdateErrnoFromHost(threadID, EMFILE, "sceNetInetAccept");
		return { -1, false, 0 };
	}

	if (args[1] && args[2]) {
		// pspautotests net/inet/tcp.
		WriteSockaddrToPSP(saddr, args[1], args[2]);
	}
	DEBUG_LOG(Log::sceNet, "Accept: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	return { newSocketId, false, 0 };
}

static int sceNetInetAccept(int socket, u32 addrPtr, u32 addrLenPtr) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	if ((addrPtr && !InetValidRange(addrPtr, sizeof(SceNetInetSockaddr), __FUNCTION__)) || (addrLenPtr && !InetValidRange(addrLenPtr, 4, __FUNCTION__))) {
		return hleLogError(Log::sceNet, -1, "bad address %08x or length %08x", addrPtr, addrLenPtr);
	}

	const u32 args[] = { (u32)socket, addrPtr, addrLenPtr };
	InetTry attempt = TryAccept(__KernelGetCurThread(), args);
	if (attempt.wouldBlock) {
		if (StartInetWait(InetWaitOp::Accept, args, ARRAY_SIZE(args), SocketWaitUs(inetSock, 0, false))) {
			return hleLogDebug(Log::sceNet, 0, "waiting");
		}
		return hleLogDebug(Log::sceNet, InetWouldBlockResult(InetWaitOp::Accept, __KernelGetCurThread(), args, attempt.hostErrno, __FUNCTION__), "EAGAIN");
	}
	if (attempt.result < 0) {
		return hleLogError(Log::sceNet, attempt.result);
	}
	return hleLogInfo(Log::sceNet, attempt.result);
}

static int sceNetInetShutdown(int socket, int how) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	// Convert HOW from PSP to Host
	int hostHow;
	switch (how) {
	case PSP_NET_INET_SHUT_RD: hostHow = SHUT_RD; break;
	case PSP_NET_INET_SHUT_WR: hostHow = SHUT_WR; break;
	case PSP_NET_INET_SHUT_RDWR: hostHow = SHUT_RDWR; break;
	default:
		// pspautotests net/inet/errors and states.
		SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "bad how %d", how);
	}

	int retVal = shutdown(inetSock->sock, hostHow);  // no translation
	return hleLogInfo(Log::sceNet, retVal);
}

static int sceNetInetSocketAbort(int socket) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	// FIXME: either using shutdown/close or select? probably using select if blocking mode is being simulated with non-blocking
	int retVal = shutdown(inetSock->sock, SHUT_RDWR);
	return hleLogInfo(Log::sceNet, retVal);
}

static int sceNetInetClose(int socket) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	CloseInetSocket(inetSock);
	ClearInetErrno();
	return hleLogInfo(Log::sceNet, 0);
}

// TODO: How is this different than just sceNetInetClose?
static int sceNetInetCloseWithRST(int socket) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	// Based on http://deepix.github.io/2016/10/21/tcprst.html
	struct linger sl {};
	sl.l_onoff = 1;		// non-zero value enables linger option in kernel
	sl.l_linger = 0;	// timeout interval in seconds
	setsockopt(inetSock->sock, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
	CloseInetSocket(inetSock);
	return hleLogInfo(Log::sceNet, 0);
}

// args: socket, buf, len, flags, from, fromlen.
static InetTry TryRecvFrom(SceUID threadID, const u32 *args) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket((int)args[0], &inetSock)) {
		UpdateErrnoFromHost(threadID, EBADF, "sceNetInetRecvfrom");
		return { -1, false, 0 };
	}

	// The game's address is written afterwards, as in TryAccept.
	SockAddrIN4 saddr{};
	socklen_t hostLen = sizeof(saddr);
	int flgs = args[3] & ~PSP_NET_INET_MSG_DONTWAIT; // removing non-POSIX flag, which is an alternative way to use non-blocking mode
	flgs = convertMSGFlagsPSP2Host(flgs);
	int retval = recvfrom(inetSock->sock, (char*)Memory::GetPointerOrException(args[1]), (int)args[2], flgs | MSG_NOSIGNAL, (struct sockaddr*)&saddr.addr, &hostLen);
	if (retval < 0) {
		const int hostErrno = socket_errno;
		if (IsWouldBlock(hostErrno)) {
			return { retval, true, hostErrno };
		}
		UpdateErrnoFromHost(threadID, hostErrno, "sceNetInetRecvfrom");
		return { retval, false, 0 };
	}

	if (args[4] && args[5]) {
		// pspautotests net/inet/udp.
		WriteSockaddrToPSP(saddr, args[4], args[5]);
	}

	std::string datahex;
	DataToHexString(0, 0, Memory::GetPointerOrException(args[1]), retval, &datahex);
	VERBOSE_LOG(Log::sceNet, "Data Dump (%d bytes):\n%s", retval, datahex.c_str());
	DEBUG_LOG(Log::sceNet, "RecvFrom: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	return { retval, false, 0 };
}

static int sceNetInetRecvfrom(int socket, u32 bufferPtr, int len, int flags, u32 fromPtr, u32 fromlenPtr) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	if (!InetValidRange(bufferPtr, (u32)len, __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad buffer %08x, %d bytes", bufferPtr, len);
	}
	if ((fromPtr && !InetValidRange(fromPtr, sizeof(SceNetInetSockaddr), __FUNCTION__)) || (fromlenPtr && !InetValidRange(fromlenPtr, 4, __FUNCTION__))) {
		return hleLogError(Log::sceNet, -1, "bad address %08x or length %08x", fromPtr, fromlenPtr);
	}

	const u32 args[] = { (u32)socket, bufferPtr, (u32)len, (u32)flags, fromPtr, fromlenPtr };
	InetTry attempt = TryRecvFrom(__KernelGetCurThread(), args);
	if (attempt.wouldBlock) {
		if (StartInetWait(InetWaitOp::RecvFrom, args, ARRAY_SIZE(args), SocketWaitUs(inetSock, flags, false))) {
			return hleLogDebug(Log::sceNet, 0, "waiting");
		}
		attempt.result = InetWouldBlockResult(InetWaitOp::RecvFrom, __KernelGetCurThread(), args, attempt.hostErrno, __FUNCTION__);
		// Using hleDelayResult as a workaround for games that need blocking-socket to be implemented (ie. Coded Arms Contagion)
		return hleDelayResult(hleLogDebug(Log::sceNet, attempt.result, "EAGAIN"), "workaround until blocking-socket", 500);
	}
	if (attempt.result < 0) {
		return hleDelayResult(hleLogError(Log::sceNet, attempt.result), "workaround until blocking-socket", 500);
	}
	// Using hleDelayResult as a workaround for games that need blocking-socket to be implemented (ie. Coded Arms Contagion)
	return hleDelayResult(hleLogDebug(Log::sceNet, attempt.result), "workaround until blocking-socket", 500);
}

// args: socket, buf, len, flags, to, tolen.
static InetTry TrySendTo(SceUID threadID, const u32 *args) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket((int)args[0], &inetSock)) {
		UpdateErrnoFromHost(threadID, EBADF, "sceNetInetSendto");
		return { -1, false, 0 };
	}
	const u32 bufferPtr = args[1];
	const int len = (int)args[2];

	SceNetInetSockaddr* dst = (SceNetInetSockaddr*)Memory::GetCharPointer(args[4]);
	int flgs = args[3] & ~PSP_NET_INET_MSG_DONTWAIT; // removing non-POSIX flag, which is an alternative way to use non-blocking mode
	flgs = convertMSGFlagsPSP2Host(flgs);
	SockAddrIN4 saddr{};
	int dstlen = std::min((int)args[5] > 0 ? (int)args[5] : 0, static_cast<int>(sizeof(saddr)));
	if (dst) {
		saddr.addr.sa_family = dst->sa_family;
		memcpy(saddr.addr.sa_data, dst->sa_data, sizeof(dst->sa_data));
	}

	int retval;
	bool isBcast = isBroadcastIP(saddr.in.sin_addr.s_addr);
	// Broadcast/Multicast, use real broadcast/multicast if there is no one in peerlist
	if (isBcast && getActivePeerCount() > 0) {
		// Acquire Peer Lock
		peerlock.lock();
		SceNetAdhocctlPeerInfo* peer = friends;
		for (; peer != NULL; peer = peer->next) {
			// Does Skipping sending to timed out friends could cause desync when players moving group at the time MP game started?
			if (peer->last_recv == 0)
				continue;

			saddr.in.sin_addr.s_addr = peer->ip_addr;
			retval = sendto(inetSock->sock, (char*)Memory::GetPointerOrException(bufferPtr), len, flgs | MSG_NOSIGNAL, (struct sockaddr*)&saddr.addr, dstlen);
			if (retval < 0) {
				DEBUG_LOG(Log::sceNet, "SendTo(BC): Socket error %d", socket_errno);
			} else {
				DEBUG_LOG(Log::sceNet, "SendTo(BC): Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
			}
		}
		// Free Peer Lock
		peerlock.unlock();
		retval = len;
	}
	// Unicast or real broadcast/multicast
	else {
		// FIXME: On non-Windows(including PSP too?) broadcast to INADDR_BROADCAST(255.255.255.255) might not be received by the sender itself when binded to specific IP (ie. 192.168.0.2) or INADDR_BROADCAST.
		//        Meanwhile, it might be received by itself when binded to subnet (ie. 192.168.0.255) or INADDR_ANY(0.0.0.0).
		retval = sendto(inetSock->sock, (char*)Memory::GetPointerOrException(bufferPtr), len, flgs | MSG_NOSIGNAL, (struct sockaddr*)&saddr.addr, dstlen);
	}
	if (retval < 0) {
		const int hostErrno = socket_errno;
		if (IsWouldBlock(hostErrno)) {
			return { retval, true, hostErrno };
		}
		SetSendErrno(threadID, inetSock, hostErrno, "sceNetInetSendto");
		return { retval, false, 0 };
	}
	DEBUG_LOG(Log::sceNet, "SendTo: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	return { retval, false, 0 };
}

static int sceNetInetSendto(int socket, u32 bufferPtr, int len, int flags, u32 toPtr, int tolen) {
	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}
	if (!InetValidRange(bufferPtr, (u32)len, __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad buffer %08x, %d bytes", bufferPtr, len);
	}
	if (toPtr && !InetValidRange(toPtr, sizeof(SceNetInetSockaddr), __FUNCTION__)) {
		return hleLogError(Log::sceNet, -1, "bad address %08x", toPtr);
	}
	if (toPtr && tolen < (int)sizeof(SceNetInetSockaddr)) {
		// Decided here, since Winsock says EFAULT (pspautotests net/inet/msg).
		SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "tolen %d", tolen);
	}

	std::string datahex;
	DataToHexString(0, 0, Memory::GetPointerOrException(bufferPtr), len, &datahex);
	VERBOSE_LOG(Log::sceNet, "Data Dump (%d bytes):\n%s", len, datahex.c_str());

	const u32 args[] = { (u32)socket, bufferPtr, (u32)len, (u32)flags, toPtr, (u32)tolen };
	InetTry attempt = TrySendTo(__KernelGetCurThread(), args);
	if (attempt.wouldBlock) {
		if (StartInetWait(InetWaitOp::SendTo, args, ARRAY_SIZE(args), SocketWaitUs(inetSock, flags, true))) {
			return hleLogDebug(Log::sceNet, 0, "waiting");
		}
		return hleLogDebug(Log::sceNet, InetWouldBlockResult(InetWaitOp::SendTo, __KernelGetCurThread(), args, attempt.hostErrno, __FUNCTION__), "EAGAIN");
	}
	if (attempt.result < 0) {
		return hleLogError(Log::sceNet, attempt.result);
	}
	return hleLogDebug(Log::sceNet, attempt.result);
}

// Similar to POSIX's sendmsg or Winsock2's WSASendMsg? Are their packets compatible one another?
// Games using this: The Warrior's Code
static int sceNetInetSendmsg(int socket, u32 msghdrPtr, int flags) {
	// Note: sendmsg is concatenating iovec buffers before sending it, and send/sendto is just a wrapper for sendmsg according to https://stackoverflow.com/questions/4258834/how-sendmsg-works
	int retval = -1;
	if (!InetValidRange(msghdrPtr, sizeof(InetMsghdr), __FUNCTION__)) {
		return hleLogError(Log::sceNet, retval);
	}

	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	InetMsghdr* pspMsghdr = (InetMsghdr*)Memory::GetPointerOrException(msghdrPtr);
	// Everything below that the host reads from the game's memory.
	if (pspMsghdr->msg_iovlen < 0) {
		SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, retval, "bad msg_iovlen %d", (int)pspMsghdr->msg_iovlen);
	}
	// In 64 bits, so a huge count can't wrap around to a small size.
	const u64 iovBytes = (u64)pspMsghdr->msg_iovlen * sizeof(SceNetIovec);
	if ((pspMsghdr->msg_name && !InetValidRange(pspMsghdr->msg_name, sizeof(SceNetInetSockaddr), __FUNCTION__)) ||
		(pspMsghdr->msg_iov && !InetValidRange(pspMsghdr->msg_iov, iovBytes > 0xFFFFFFFF ? 0xFFFFFFFF : (u32)iovBytes, __FUNCTION__)) ||
		(pspMsghdr->msg_control && !InetValidRange(pspMsghdr->msg_control, pspMsghdr->msg_controllen, __FUNCTION__))) {
		return hleLogError(Log::sceNet, retval, "bad pointer in msghdr");
	}
	if (pspMsghdr->msg_iov) {
		const SceNetIovec *pspIov = (const SceNetIovec *)Memory::GetPointerUnchecked(pspMsghdr->msg_iov);
		for (int i = 0; i < pspMsghdr->msg_iovlen; i++) {
			if (pspIov[i].iov_base && !InetValidRange(pspIov[i].iov_base, pspIov[i].iov_len, __FUNCTION__)) {
				return hleLogError(Log::sceNet, retval, "bad iov[%d] %08x, %d bytes", i, (u32)pspIov[i].iov_base, (u32)pspIov[i].iov_len);
			}
		}
	}
	// Any control data fails it, as does an address shorter than a sockaddr_in, on UDP and TCP alike
	// (pspautotests net/inet/msg).
	if (pspMsghdr->msg_control && pspMsghdr->msg_controllen) {
		SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, retval, "control data, %d bytes", (int)pspMsghdr->msg_controllen);
	}
	if (pspMsghdr->msg_name && pspMsghdr->msg_namelen < sizeof(SceNetInetSockaddr)) {
		SetInetErrno(ERROR_INET_EINVAL, __FUNCTION__);
		return hleLogError(Log::sceNet, retval, "namelen %d", (int)pspMsghdr->msg_namelen);
	}
	int flgs = flags & ~PSP_NET_INET_MSG_DONTWAIT; // removing non-POSIX flag, which is an alternative way to use non-blocking mode
	flgs = convertMSGFlagsPSP2Host(flgs);
	SockAddrIN4 saddr{};
#if defined(_WIN32)
	WSAMSG hdr;
	WSACMSGHDR* chdr = NULL;
	size_t iovecsize = sizeof(WSABUF);
	WSABUF* iov = (WSABUF*)malloc(pspMsghdr->msg_iovlen * iovecsize);
#else
	msghdr hdr;
	cmsghdr* chdr = nullptr;
	size_t iovecsize = sizeof(iovec);
	iovec* iov = (iovec*)malloc(pspMsghdr->msg_iovlen * iovecsize);
#endif
	if (iov == NULL) {
		UpdateErrnoFromHost(__KernelGetCurThread(), ENOBUFS, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}
	memset(iov, 0, pspMsghdr->msg_iovlen * iovecsize);
	memset(&hdr, 0, sizeof(hdr));
	if (pspMsghdr->msg_name != 0) {
		SceNetInetSockaddr* pspSaddr = (SceNetInetSockaddr*)Memory::GetPointerOrException(pspMsghdr->msg_name);
		saddr.addr.sa_family = pspSaddr->sa_family;
		size_t datalen = std::min(pspMsghdr->msg_namelen - (sizeof(pspSaddr->sa_len) + sizeof(pspSaddr->sa_family)), sizeof(saddr.addr.sa_data));
		memcpy(saddr.addr.sa_data, pspSaddr->sa_data, datalen);
		DEBUG_LOG(Log::sceNet, "SendMsg: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
#if defined(_WIN32)
		hdr.name = &saddr.addr;
		hdr.namelen = static_cast<int>(datalen + sizeof(saddr.addr.sa_family));
#else
		hdr.msg_name = &saddr.addr;
		hdr.msg_namelen = static_cast<int>(datalen + sizeof(saddr.addr.sa_family));
#endif
	}
#if defined(_WIN32)
	hdr.lpBuffers = iov;
	hdr.dwBufferCount = pspMsghdr->msg_iovlen;
#else
	hdr.msg_iov = iov;
	hdr.msg_iovlen = pspMsghdr->msg_iovlen;
#endif
	if (pspMsghdr->msg_iov != 0) {
		SceNetIovec* pspIov = (SceNetIovec*)Memory::GetPointerOrException(pspMsghdr->msg_iov);
		for (int i = 0; i < pspMsghdr->msg_iovlen; i++) {
			if (pspIov[i].iov_base != 0) {
#if defined(_WIN32)
				iov[i].buf = (char*)Memory::GetPointerOrException(pspIov[i].iov_base);
				iov[i].len = pspIov[i].iov_len;
#else
				iov[i].iov_base = (char*)Memory::GetPointerOrException(pspIov[i].iov_base);
				iov[i].iov_len = pspIov[i].iov_len;
#endif
			}
		}
	}
	// Control's Level (ie. host's SOL_SOCKET to/from psp's PSP_NET_INET_SOL_SOCKET) and Type (ie. SCM_RIGHTS) might need to be converted to be cross-platform
	if (pspMsghdr->msg_control != 0) {
#if defined(_WIN32)
		chdr = (WSACMSGHDR*)malloc(pspMsghdr->msg_controllen);
#else
		chdr = (cmsghdr*)malloc(pspMsghdr->msg_controllen);
#endif
		if (chdr == NULL) {
			UpdateErrnoFromHost(__KernelGetCurThread(), ENOBUFS, __FUNCTION__);
			free(iov);
			return hleLogError(Log::sceNet, retval);
		}
		InetCmsghdr* pspCmsghdr = (InetCmsghdr*)Memory::GetPointerOrException(pspMsghdr->msg_control);
		// TODO: Convert InetCmsghdr into platform-specific struct as they're affected by 32/64bit
		memcpy(chdr, pspCmsghdr, pspMsghdr->msg_controllen);
#if defined(_WIN32)
		hdr.Control.buf = (char*)chdr; // (char*)pspCmsghdr;
		hdr.Control.len = pspMsghdr->msg_controllen;
		// Note: Many existing implementations of CMSG_FIRSTHDR never look at msg_controllen and just return the value of cmsg_control.
		if (pspMsghdr->msg_controllen >= sizeof(InetCmsghdr)) {
			// TODO: Creates our own CMSG_* macros (32-bit version of it, similar to the one on PSP) to avoid alignment/size issue that can lead to memory corruption/out of bound issue.
			for (WSACMSGHDR* cmsgptr = WSA_CMSG_FIRSTHDR(&hdr); cmsgptr != NULL; cmsgptr = WSA_CMSG_NXTHDR(&hdr, cmsgptr)) {
				if (cmsgptr->cmsg_len < sizeof(WSACMSGHDR))
					break;
				cmsgptr->cmsg_type = convertCMsgTypePSP2Host(cmsgptr->cmsg_type, cmsgptr->cmsg_level);
				cmsgptr->cmsg_level = convertSockoptLevelPSP2Host(cmsgptr->cmsg_level);
			}
		}
#else
		hdr.msg_control = (char*)chdr; // (char*)pspCmsghdr;
		hdr.msg_controllen = pspMsghdr->msg_controllen;
		// Note: Many existing implementations of CMSG_FIRSTHDR never look at msg_controllen and just return the value of cmsg_control.
		if (pspMsghdr->msg_controllen >= sizeof(InetCmsghdr)) {
			for (cmsghdr* cmsgptr = CMSG_FIRSTHDR(&hdr); cmsgptr != NULL; cmsgptr = CMSG_NXTHDR(&hdr, cmsgptr)) {
				// Some CMSG_NXTHDR implementations return the same header again for a zero length.
				if (cmsgptr->cmsg_len < sizeof(cmsghdr))
					break;
				cmsgptr->cmsg_type = convertCMsgTypePSP2Host(cmsgptr->cmsg_type, cmsgptr->cmsg_level);
				cmsgptr->cmsg_level = convertSockoptLevelPSP2Host(cmsgptr->cmsg_level);
			}
		}
#endif
	}
	// Flags (ie. PSP_NET_INET_MSG_OOB) might need to be converted to be cross-platform
#if defined(_WIN32)
	hdr.dwFlags = convertMSGFlagsPSP2Host(pspMsghdr->msg_flags & ~PSP_NET_INET_MSG_DONTWAIT) | MSG_NOSIGNAL;
#else
	hdr.msg_flags = convertMSGFlagsPSP2Host(pspMsghdr->msg_flags & ~PSP_NET_INET_MSG_DONTWAIT) | MSG_NOSIGNAL;
#endif
	unsigned long sent = 0;
	bool isBcast = isBroadcastIP(saddr.in.sin_addr.s_addr);
	// Broadcast/Multicast, use real broadcast/multicast if there is no one in peerlist
	if (isBcast && getActivePeerCount() > 0) {
		// Acquire Peer Lock
		peerlock.lock();
		SceNetAdhocctlPeerInfo* peer = friends;
		for (; peer != NULL; peer = peer->next) {
			// Does Skipping sending to timed out friends could cause desync when players moving group at the time MP game started?
			if (peer->last_recv == 0)
				continue;

			saddr.in.sin_addr.s_addr = peer->ip_addr;
#if defined(_WIN32)
			int result = WSASendMsg(inetSock->sock, &hdr, flgs | MSG_NOSIGNAL, &sent, NULL, NULL);
			if (static_cast<int>(sent) > retval)
				retval = sent;
#else
			size_t result = sendmsg(inetSock->sock, &hdr, flgs | MSG_NOSIGNAL);
			if (static_cast<int>(result) > retval)
				retval = result;
#endif
			if (retval != SOCKET_ERROR) {
				DEBUG_LOG(Log::sceNet, "SendMsg(BC): Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
			} else {
				DEBUG_LOG(Log::sceNet, "SendMsg(BC): Socket error %d", socket_errno);
			}
		}
		// Free Peer Lock
		peerlock.unlock();
		// TODO: Calculate number of bytes supposed to be sent
		retval = std::max(retval, 0); // Broadcast always success?
	}
	// Unicast or real broadcast/multicast
	else {
		// FIXME: On non-Windows(including PSP too?) broadcast to INADDR_BROADCAST(255.255.255.255) might not be received by the sender itself when binded to specific IP (ie. 192.168.0.2) or INADDR_BROADCAST.
		//        Meanwhile, it might be received by itself when binded to subnet (ie. 192.168.0.255) or INADDR_ANY(0.0.0.0).
		/*if (isBcast) {
			// TODO: Replace Broadcast with Multicast to be more consistent across platform
			// Replace Limited Broadcast(255.255.255.255) with Direct Broadcast(ie. 192.168.0.255) for accurate targetting when there are multiple interfaces, to avoid receiving it's own broadcasted data through IP 169.254.x.x on Windows (which is not recognized as it's own IP by the game)
			// Get Local IP Address
			sockaddr_in sockAddr{};
			getLocalIp(&sockAddr);
			// Change the last number to 255 to indicate a common broadcast address (the accurate way should be: ip | (~subnetmask))
			((u8*)&sockAddr.sin_addr.s_addr)[3] = 255;
			saddr.in.sin_addr.s_addr = sockAddr.sin_addr.s_addr;
			DEBUG_LOG(Log::sceNet, "SendMsg(BC): Address Replacement = %s", ip2str(saddr.in.sin_addr).c_str());
		}*/
#if defined(_WIN32)
		int result = WSASendMsg(inetSock->sock, &hdr, flgs | MSG_NOSIGNAL, &sent, NULL, NULL);
		if (result != SOCKET_ERROR)
			retval = sent;
#else
		retval = sendmsg(inetSock->sock, &hdr, flgs | MSG_NOSIGNAL);
#endif
	}
	free(chdr);
	free(iov);
	/*  // Example with 1 Msg buffer and without CMsg
		msghdr msg;
		iovec iov[1];
		int buflen = pspMsghdr->msg_iovlen;
		char* buf = (char*)malloc(buflen);

		memset(&msg, 0, sizeof(msg));
		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		iov[0].iov_base = buf;
		iov[0].iov_len = buflen;

		retval = sendmsg(inetSock->sock, &msg, flags);
		free(buf);
	*/
	if (retval < 0) {
		const int hostErrno = socket_errno;
		SetSendErrno(__KernelGetCurThread(), inetSock, hostErrno, __FUNCTION__);
		if (IsWouldBlock(hostErrno)) {
			return hleLogDebug(Log::sceNet, retval, "EAGAIN");
		}
		return hleLogError(Log::sceNet, retval);
	}
	return hleLogInfo(Log::sceNet, retval); // returns number of bytes sent?
}

// Similar to POSIX's recvmsg or Mswsock's WSARecvMsg? Are their packets compatible one another?
// Games using this: World of Poker
static int sceNetInetRecvmsg(MIPSState *mips, int socket, u32 msghdrPtr, int flags) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %08x, %08x) at %08x", __FUNCTION__, socket, msghdrPtr, flags, mips->pc);

	InetSocket *inetSock;
	if (!GetCallSocket(socket, &inetSock)) {
		return InetBadSocket(socket, __FUNCTION__);
	}

	// Reference: http://www.masterraghu.com/subjects/np/introduction/unix_network_programming_v1.3/ch14lev1sec5.html
	int retval = -1;
	if (!InetValidRange(msghdrPtr, sizeof(InetMsghdr), __FUNCTION__)) {
		return hleLogError(Log::sceNet, retval);
	}
	InetMsghdr* pspMsghdr = (InetMsghdr*)Memory::GetPointerOrException(msghdrPtr);
	int flgs = flags & ~PSP_NET_INET_MSG_DONTWAIT; // removing non-POSIX flag, which is an alternative way to use non-blocking mode
	flgs = convertMSGFlagsPSP2Host(flgs);
	SockAddrIN4 saddr{};
#if defined(_WIN32)
	WSAMSG hdr;
	WSACMSGHDR* chdr = NULL;
	size_t iovecsize = sizeof(WSABUF);
	WSABUF* iov = (WSABUF*)malloc(pspMsghdr->msg_iovlen * iovecsize);
#else
	msghdr hdr;
	cmsghdr* chdr = nullptr;
	size_t iovecsize = sizeof(iovec);
	iovec* iov = (iovec*)malloc(pspMsghdr->msg_iovlen * iovecsize);
#endif
	if (iov == NULL) {
		UpdateErrnoFromHost(__KernelGetCurThread(), ENOBUFS, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}
	memset(iov, 0, pspMsghdr->msg_iovlen * iovecsize);
	memset(&hdr, 0, sizeof(hdr));

	// TODO: Do similar to the already working sceNetInetSendmsg but in reverse
	//if (pspMsghdr->msg_name != 0) { ... }

	//UNFINISHED
	free(iov);

	return hleLogError(Log::sceNet, retval); // returns number of bytes received?
}

// TODO: fix retmasks
const HLEFunction sceNetInet[] = {
	{0X17943399, &WrapI_V<sceNetInetInit>,           "sceNetInetInit",                  'i', ""       },
	{0X4CFE4E56, &WrapI_II<sceNetInetShutdown>,      "sceNetInetShutdown",              'i', "ii"     },
	{0XA9ED66B9, &WrapI_V<sceNetInetTerm>,           "sceNetInetTerm",                  'i', ""       },
	{0X8B7B220F, &WrapI_III<sceNetInetSocket>,       "sceNetInetSocket",                'i', "iii"    },
	{0X2FE71FE7, &WrapI_IIIUI<sceNetInetSetsockopt>, "sceNetInetSetsockopt",            'i', "iiixi"  },
	{0X4A114C7C, &WrapI_IIIUU<sceNetInetGetsockopt>, "sceNetInetGetsockopt",            'i', "iiixx"  },
	{0X410B34AA, &WrapI_IUI<sceNetInetConnect>,      "sceNetInetConnect",               'i', "ixi"    },
	{0X805502DD, &WrapI_I<sceNetInetCloseWithRST>,   "sceNetInetCloseWithRST",          'i', "i"      },
	{0XD10A1A7A, &WrapI_II<sceNetInetListen>,        "sceNetInetListen",                'i', "ii"     },
	{0XDB094E1B, &WrapI_IUU<sceNetInetAccept>,       "sceNetInetAccept",                'i', "ixx"    },
	{0XFAABB1DD, &WrapI_UUI<sceNetInetPoll>,         "sceNetInetPoll",                  'i', "xxi"    },
	{0X5BE8D595, &WrapI_IUUUU<sceNetInetSelect>,     "sceNetInetSelect",                'i', "ixxxx"  },
	{0X8D7284EA, &WrapI_I<sceNetInetClose>,          "sceNetInetClose",                 'i', "i"      },
	{0XCDA85C99, &WrapI_IUUU<sceNetInetRecv>,        "sceNetInetRecv",                  'i', "ixxx"   },
	{0XC91142E4, &WrapI_IUIIUU<sceNetInetRecvfrom>,  "sceNetInetRecvfrom",              'i', "ixiixx" },
	{0XEECE61D2, &WrapI_IUI<sceNetInetRecvmsg>,      "sceNetInetRecvmsg",               'i', "ixi"    },
	{0X7AA671BC, &WrapI_IUUU<sceNetInetSend>,        "sceNetInetSend",                  'i', "ixxx"   },
	{0X05038FC7, &WrapI_IUIIUI<sceNetInetSendto>,	 "sceNetInetSendto",                'i', "ixiixi" },
	{0X774E36F4, &WrapI_IUI<sceNetInetSendmsg>,      "sceNetInetSendmsg",               'i', "ixi"    },
	{0XFBABE411, &WrapI_V<sceNetInetGetErrno>,       "sceNetInetGetErrno",              'i', ""       },
	{0X1A33F9AE, &WrapI_IUI<sceNetInetBind>,         "sceNetInetBind",                  'i', "ixi"    },
	{0XB75D5B0A, &WrapU_C<sceNetInetInetAddr>,       "sceNetInetInetAddr",              'x', "s"      },
	{0X1BDF5D13, &WrapI_CU<sceNetInetInetAton>,      "sceNetInetInetAton",              'i', "sx"     },
	{0XD0792666, &WrapU_IUUU<sceNetInetInetNtop>,    "sceNetInetInetNtop",              'x', "ixxx"   },
	{0XE30B8C19, &WrapI_ICU<sceNetInetInetPton>,     "sceNetInetInetPton",              'i', "isx"    },
	{0X8CA3A97E, &WrapI_V<sceNetInetGetPspError>,    "sceNetInetGetPspError",           'i', ""       },
	{0XE247B6D6, &WrapI_IUU<sceNetInetGetpeername>,  "sceNetInetGetpeername",           'i', "ixx"    },
	{0X162E6FD5, &WrapI_IUU<sceNetInetGetsockname>,  "sceNetInetGetsockname",           'i', "ixx"    },
	{0X80A21ABD, &WrapI_I<sceNetInetSocketAbort>,    "sceNetInetSocketAbort",           'i', "i"      },
	{0X39B0C7D3, nullptr,                            "sceNetInetGetUdpcbstat",          '?', ""       },
	{0XB3888AD4, nullptr,                            "sceNetInetGetTcpcbstat",          '?', ""       },
	{0X2D5868C0, nullptr,                            "sceNetInetDelArp",                '?', ""       },
	{0XCCC18C45, nullptr,                            "sceNetInetAddArp",                '?', ""       }
};

void Register_sceNetInet() {
	RegisterHLEModule("sceNetInet", std::size(sceNetInet), sceNetInet);
}

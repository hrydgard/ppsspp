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

void __NetInetDoState(PointerWrap &p, bool hasState) {
	if (hasState) {
		Do(p, g_inetWaits);
		Do(p, eventInetWaitPoll);
	} else if (p.mode == p.MODE_READ) {
		g_inetWaits.clear();
		eventInetWaitPoll = -1;
	}
	CoreTiming::RestoreRegisterEvent(eventInetWaitPoll, "NetInetWaitPoll", &__NetInetWaitPoll);
}

void __NetInetShutdown() {
	if (!netInetInited) {
		return;
	}

	netInetInited = false;
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
	int threadID = __KernelGetCurThread();
	const int lastErrno = g_inetLastErrno[threadID];

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

static int sceNetInetInetPton(int af, const char* hostname, u32 inAddrPtr) {
	if (!Memory::IsValidAddress(inAddrPtr)) {
		return hleLogError(Log::sceNet, 0, "invalid arg"); //-1
	}

	int retval = inet_pton(convertSocketDomainPSP2Host(af), hostname, (void*)Memory::GetPointerOrException(inAddrPtr));
	// Note that inet_pton can set errno!
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}
	return hleLogDebug(Log::sceNet, retval);
}

static int sceNetInetInetAton(const char* hostname, u32 inAddrPtr) {
	if (!Memory::IsValidAddress(inAddrPtr)) {
		return hleLogError(Log::sceNet, 0, "invalid arg"); //-1
	}

	// TODO: Wait what, we're calling pton in aton?
	int retval = inet_pton(AF_INET, hostname, (void*)Memory::GetPointerOrException(inAddrPtr));
	// inet_aton() returns nonzero if the address is valid, zero if not.
	return hleLogDebug(Log::sceNet, retval);
}

// TODO: Need to find out whether it's possible to get partial output or not, since Coded Arms Contagion is using a small bufsize(4)
static u32 sceNetInetInetNtop(int af, u32 srcInAddrPtr, u32 dstBufPtr, u32 bufsize) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetInetInetNtop(%i, %08x, %08x, %d)", af, srcInAddrPtr, dstBufPtr, bufsize);
	if (!Memory::IsValidAddress(srcInAddrPtr)) {
		return hleLogError(Log::sceNet, 0, "invalid arg");
	}
	if (!Memory::IsValidAddress(dstBufPtr) || bufsize < 1/*8*/) { // usually 8 or 16, but Coded Arms Contagion is using bufsize = 4
		UpdateErrnoFromHost(__KernelGetCurThread(), ENOSPC, __FUNCTION__);
		return hleLogError(Log::sceNet, 0, "invalid arg");
	}

	if (inet_ntop(convertSocketDomainPSP2Host(af), Memory::GetCharPointer(srcInAddrPtr), (char*)Memory::GetCharPointer(dstBufPtr), bufsize) == NULL) {
		//return hleLogDebug(Log::sceNet, 0, "invalid arg?"); // Temporarily commented out in case it's allowed to have partial output
	}
	return hleLogDebug(Log::sceNet, dstBufPtr, "%s", safe_string(Memory::GetCharPointer(dstBufPtr)));
}

static u32_le sceNetInetInetAddr(const char *hostname) {
	if (hostname == nullptr || hostname[0] == '\0') {
		return hleLogError(Log::sceNet, INADDR_NONE, "invalid arg");
	}

	u32 retval = INADDR_NONE; // inet_addr(hostname); // deprecated?
	inet_pton(AF_INET, hostname, &retval); // Alternative to the deprecated inet_addr

	return hleLogDebug(Log::sceNet, retval);
}

static int sceNetInetGetpeername(int socket, u32 namePtr, u32 namelenPtr) {
	if (!Memory::IsValidAddress(namePtr) || !Memory::IsValidAddress(namelenPtr)) {
		UpdateErrnoFromHost(__KernelGetCurThread(), EFAULT, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "invalid arg");
	}

	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	SceNetInetSockaddr* name = (SceNetInetSockaddr*)Memory::GetPointerOrException(namePtr);
	int* namelen = (int*)Memory::GetPointerOrException(namelenPtr);
	SockAddrIN4 saddr{};
	// TODO: Should've created convertSockaddrPSP2Host (and Host2PSP too) function as it's being used pretty often, thus fixing a bug on it will be tedious when scattered all over the places
	saddr.addr.sa_family = name->sa_family;
	int len = std::min(*namelen > 0 ? *namelen : 0, static_cast<int>(sizeof(saddr)));
	name->sa_len = len;
	memcpy(saddr.addr.sa_data, name->sa_data, sizeof(name->sa_data));

	int retval = getpeername(inetSock->sock, (sockaddr*)&saddr, (socklen_t*)&len);
	DEBUG_LOG(Log::sceNet, "Getpeername: Family = %s, Address = %s, Port = %d", inetSocketDomain2str(saddr.addr.sa_family).c_str(), ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	*namelen = len;
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}

	// We shouldn't use the returned len here, because the returned len is the actual size needed, which can be larger than the inputted len
	memcpy(name->sa_data, saddr.addr.sa_data, name->sa_len - (sizeof(name->sa_len) + sizeof(name->sa_family)));
	name->sa_family = saddr.addr.sa_family;
	return hleLogInfo(Log::sceNet, 0);
}

static int sceNetInetGetsockname(int socket, u32 namePtr, u32 namelenPtr) {
	if (!Memory::IsValidAddress(namePtr) || !Memory::IsValidAddress(namelenPtr)) {
		UpdateErrnoFromHost(__KernelGetCurThread(), EFAULT, __FUNCTION__);
		return hleLogError(Log::sceNet, -1, "invalid arg");
	}

	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	SceNetInetSockaddr* name = (SceNetInetSockaddr*)Memory::GetPointerOrException(namePtr);
	int* namelen = (int*)Memory::GetPointerOrException(namelenPtr);
	SockAddrIN4 saddr{};
	saddr.addr.sa_family = name->sa_family;
	int len = std::min(*namelen > 0 ? *namelen : 0, static_cast<int>(sizeof(saddr)));
	name->sa_len = len;
	memcpy(saddr.addr.sa_data, name->sa_data, sizeof(name->sa_data));
	
	int retval = getsockname(inetSock->sock, (sockaddr*)&saddr, (socklen_t*)&len);
	DEBUG_LOG(Log::sceNet, "Getsockname: Family = %s, Address = %s, Port = %d", inetSocketDomain2str(saddr.addr.sa_family).c_str(), ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	*namelen = len;
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}

	// We shouldn't use the returned len here, because the returned len is the actual size needed, which can be larger than the inputted len
	memcpy(name->sa_data, saddr.addr.sa_data, name->sa_len - (sizeof(name->sa_len) + sizeof(name->sa_family)));
	name->sa_family = saddr.addr.sa_family;
	return hleLogInfo(Log::sceNet, 0);
}

static InetTry TryRecvFrom(SceUID threadID, const u32 *args);
static InetTry TrySendTo(SceUID threadID, const u32 *args);
static InetTry TryAccept(SceUID threadID, const u32 *args);
static InetTry TryConnectDone(SceUID threadID, const u32 *args);

static bool IsWouldBlock(int hostErrno) {
	return hostErrno == EAGAIN || hostErrno == EWOULDBLOCK;
}

// How long a blocking call on this socket may wait: 0 not at all, -1 forever.
static s64 SocketWaitUs(const InetSocket *inetSock, u32 flags, bool send) {
	if (inetSock->nonblocking || (flags & PSP_NET_INET_MSG_DONTWAIT)) {
		return 0;
	}
	const u32 timeoutUs = send ? inetSock->sendTimeoutUs : inetSock->recvTimeoutUs;
	return timeoutUs == 0 ? -1 : timeoutUs;
}

static void ZeroInetFdSets(u32 readfdsPtr, u32 writefdsPtr, u32 exceptfdsPtr) {
	for (u32 ptr : { readfdsPtr, writefdsPtr, exceptfdsPtr }) {
		if (ptr) {
			NetInetFD_ZERO((SceNetInetFdSet *)Memory::GetPointerWriteOrException(ptr));
		}
	}
}

// args: nfds, readfds, writefds, exceptfds. The sets are only overwritten with the result when
// something is ready, so they still hold the request between attempts.
static InetTry TrySelect(SceUID threadID, const u32 *args, bool firstTry) {
	const int nfds = (int)args[0];
	SceNetInetFdSet	*readfds = args[1] ? (SceNetInetFdSet*)Memory::GetPointerWriteOrException(args[1]) : nullptr;
	SceNetInetFdSet	*writefds = args[2] ? (SceNetInetFdSet*)Memory::GetPointerWriteOrException(args[2]) : nullptr;
	SceNetInetFdSet	*exceptfds = args[3] ? (SceNetInetFdSet*)Memory::GetPointerWriteOrException(args[3]) : nullptr;

	// First, translate the specified fd_sets to host sockets.

	fd_set rdfds, wrfds, exfds;
	FD_ZERO(&rdfds);
	FD_ZERO(&wrfds);
	FD_ZERO(&exfds);

	int rdcnt = 0, wrcnt = 0, excnt = 0;

	int maxHostSocket = 0;

	// Save the mapping during setup.
	SOCKET hostSockets[256]{};

	struct {
		SceNetInetFdSet *set;
		fd_set *hostSet;
		int *count;
		const char *name;
	} sets[] = {
		{ readfds, &rdfds, &rdcnt, "read" },
		{ writefds, &wrfds, &wrcnt, "write" },
		{ exceptfds, &exfds, &excnt, "except" },
	};

	for (int i = SocketManager::MIN_VALID_INET_SOCKET; i < nfds; i++) {
		for (auto &set : sets) {
			if (!set.set || !NetInetFD_ISSET(i, set.set)) {
				continue;
			}
			SOCKET sock = g_socketManager.GetHostSocketFromInetSocket(i);
			_dbg_assert_(sock != 0);
			if (sock < 0) {
				if (firstTry) {
					ERROR_LOG(Log::sceNet, "Bad socket #%d in %s fds", i, set.name);
				}
				continue;
			}
			hostSockets[i] = sock;
			if (sock > maxHostSocket)
				maxHostSocket = sock;
			if (*set.count < FD_SETSIZE) {
				FD_SET(sock, set.hostSet); // This might pointed to a non-existing socket or sockets belonged to other programs on Windows, because most of the time Windows socket have an id above 1k instead of 0-255
				(*set.count)++;
			} else if (firstTry) {
				ERROR_LOG(Log::sceNet, "Hit set size (%s)", set.name);
			}
		}
	}

	// Unlikely to hit these.
	_dbg_assert_(rdcnt < FD_SETSIZE);
	_dbg_assert_(wrcnt < FD_SETSIZE);
	_dbg_assert_(excnt < FD_SETSIZE);

	timeval tmout = { 0, 0 };
	int retval = select(maxHostSocket + 1, readfds ? &rdfds : nullptr, writefds ? &wrfds : nullptr, exceptfds ? &exfds : nullptr, &tmout);
	if (retval == 0) {
		return { 0, true, EAGAIN };
	}
	if (retval < 0) {
		UpdateErrnoFromHost(threadID, socket_errno, "sceNetInetSelect");
		return { retval, false, 0 };
	}

	// Convert the results back to PSP fd_sets.
	for (auto &set : sets) {
		if (set.set) {
			NetInetFD_ZERO(set.set);
		}
	}
	for (int i = SocketManager::MIN_VALID_INET_SOCKET; i < nfds; i++) {
		if (hostSockets[i] == 0) {
			continue;
		}
		for (auto &set : sets) {
			if (set.set && FD_ISSET(hostSockets[i], set.hostSet)) {
				NetInetFD_SET(i, set.set);
			}
		}
	}
	return { retval, false, 0 };
}

// args: fds, nfds. Rewrites every revents, so repeating it is harmless.
static InetTry TryPoll(SceUID threadID, const u32 *args) {
	SceNetInetPollfd *fdarray = (SceNetInetPollfd*)Memory::GetPointerOrException(args[0]); // SceNetInetPollfd/pollfd, sceNetInetPoll() have similarity to BSD poll() but pollfd have different size on 64bit
	const int nfds = (int)args[1];

	int maxHostFd = 0;
	fd_set readfds{}, writefds{}, exceptfds{};
	FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);
	for (int i = 0; i < nfds; i++) {
		SOCKET hostSocket = g_socketManager.GetHostSocketFromInetSocket(fdarray[i].fd);
		fdarray[i].revents = 0;
		if (hostSocket < 0) {
			continue;
		}
		if (hostSocket > maxHostFd) {
			maxHostFd = hostSocket;
		}
		_dbg_assert_(hostSocket != 0);
		FD_SET(hostSocket, &readfds);
		FD_SET(hostSocket, &writefds);
		FD_SET(hostSocket, &exceptfds);
	}

	timeval tmout = { 0, 0 };
	int retval = select(maxHostFd + 1, &readfds, &writefds, &exceptfds, &tmout);
	if (retval < 0) {
		UpdateErrnoFromHost(threadID, EINTR, "sceNetInetPoll");
		return { retval, false, 0 };
	}

	retval = 0;
	for (int i = 0; i < nfds; i++) {
		SOCKET hostSocket = g_socketManager.GetHostSocketFromInetSocket(fdarray[i].fd);
		if (hostSocket < 0) {
			continue;
		}
		if ((fdarray[i].events & (INET_POLLRDNORM | INET_POLLIN)) && FD_ISSET(hostSocket, &readfds))
			fdarray[i].revents |= (INET_POLLRDNORM | INET_POLLIN); //POLLIN_SET
		if ((fdarray[i].events & (INET_POLLWRNORM | INET_POLLOUT)) && FD_ISSET(hostSocket, &writefds))
			fdarray[i].revents |= (INET_POLLWRNORM | INET_POLLOUT); //POLLOUT_SET
		fdarray[i].revents &= fdarray[i].events;
		if (FD_ISSET(hostSocket, &exceptfds))
			fdarray[i].revents |= (INET_POLLRDBAND | INET_POLLPRI | INET_POLLERR); //POLLEX_SET; // Can be raised on revents regardless of events bitmask?
		if (fdarray[i].revents)
			retval++;
		VERBOSE_LOG(Log::sceNet, "Poll Socket#%d Fd: %d, events: %04x, revents: %04x, availToRecv: %d", i, fdarray[i].fd, fdarray[i].events, fdarray[i].revents, (int)getAvailToRecv(fdarray[i].fd));
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
		UpdateErrnoFromHost(threadID, hostErrno, "sceNetInetSend");
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
	SceNetInetPollfd *fdarray = (SceNetInetPollfd*)Memory::GetPointerOrException(fdsPtr);

	if (nfds > FD_SETSIZE)
		nfds = FD_SETSIZE;

	for (int i = 0; i < (s32)nfds; i++) {
		if (fdarray[i].fd < 0) {
			// In Unix, this is OK and means it the fd should be ignored, except fdarray[i].revents should be zeroed.
			UpdateErrnoFromHost(__KernelGetCurThread(), EINVAL, __FUNCTION__);
			return hleLogError(Log::sceNet, -1, "invalid socket id");
		}
	}

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
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
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
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
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
	return hleLogDebug(Log::sceNet, socket);
}

static int sceNetInetSetsockopt(MIPSState *mips, int socket, int level, int optname, u32 optvalPtr, int optlen) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	const u32 optval = Memory::IsValid4AlignedAddress(optvalPtr) ? Memory::ReadUnchecked_U32(optvalPtr) : 0;
	INFO_LOG(Log::sceNet, "sceNetInetSetsockopt(%i, %i, %i, %08x, %i) at %08x: Level = %s, OptName = %s, OptValue = %d",
		socket, level, optname, optvalPtr, optlen, mips->pc,
		inetSockoptLevel2str(level).c_str(), inetSockoptName2str(optname, level).c_str(), optval);

	timeval tval{};
	// TODO: Ignoring SO_NBIO/SO_NONBLOCK flag if we always use non-blocking mode (ie. simulated blocking mode)
	if (level == PSP_NET_INET_SOL_SOCKET) {
		switch (optname) {
		case PSP_NET_INET_SO_NBIO:
			inetSock->nonblocking = optval;
			return hleLogDebug(Log::sceNet, 0);
		// FIXME: Should we ignore SO_BROADCAST flag since we are using fake broadcast (ie. only broadcast to friends),
		//        But Infrastructure/Online play might need to use broadcast for SSDP and to support LAN MP with real PSP
		/*else if (level == PSP_NET_INET_SOL_SOCKET && optname == PSP_NET_INET_SO_BROADCAST) {
			//memcpy(&sock->so_broadcast, (int*)optval, std::min(sizeof(sock->so_broadcast), optlen));
			return hleLogSuccessI(Log::sceNet, 0);
		}*/
		// TODO: Ignoring SO_REUSEADDR flag to prevent disrupting multiple-instance feature
		case PSP_NET_INET_SO_REUSEADDR:
			//memcpy(&sock->reuseaddr, (int*)optval, std::min(sizeof(sock->reuseaddr), optlen));
			return hleLogDebug(Log::sceNet, 0);

		// TODO: Ignoring SO_REUSEPORT flag to prevent disrupting multiple-instance feature (not sure if PSP has SO_REUSEPORT or not tho, defined as 15 on Android)
		case PSP_NET_INET_SO_REUSEPORT: // 15
			//memcpy(&sock->reuseport, (int*)optval, std::min(sizeof(sock->reuseport), optlen));
			return hleLogDebug(Log::sceNet, 0);

		// TODO: Ignoring SO_NOSIGPIPE flag to prevent crashing PPSSPP (not sure if PSP has NOSIGPIPE or not tho, defined as 0x1022 on Darwin)
		case PSP_NET_INET_SO_NOSIGPIPE: // WARNING: Not sure about this one. But we definitely don't want signals, so we ignore it.
			//memcpy(&sock->nosigpipe, (int*)optval, std::min(sizeof(sock->nosigpipe), optlen));
			return hleLogWarning(Log::sceNet, 0, "NOSIGPIPE should never be modified (should always be off)");

		// It seems UNO game will try to set socket buffer size with a very large size and ended getting error (-1), so we should also limit the buffer size to replicate PSP behavior
		case PSP_NET_INET_SO_RCVBUF:
		case PSP_NET_INET_SO_SNDBUF: // PSP_NET_INET_SO_NOSIGPIPE ?
			// TODO: For SOCK_STREAM max buffer size is 8 Mb on BSD, while max SOCK_DGRAM is 65535 minus the IP & UDP Header size
			if (optval > 8 * 1024 * 1024) {
				UpdateErrnoFromHost(__KernelGetCurThread(), ENOBUFS, __FUNCTION__); // FIXME: return ENOBUFS for SOCK_STREAM, and EINVAL for SOCK_DGRAM
				return hleLogError(Log::sceNet, -1, "buffer size too large?");
			}
			break;

		case PSP_NET_INET_SO_ONESBCAST:
			// Seen in Outrun 2006 (account registration), assuming that the flag mapping is correct, we can't support this. So we warn-log and pretend success.
			return hleLogWarning(Log::sceNet, 0, "PSP_NET_INET_SO_ONESBCAST unsupported, ignoring");

		default:
			break;
		}
	}

	int retval = 0;
	// PSP timeout are a single 32bit value (micro seconds)
	if (level == PSP_NET_INET_SOL_SOCKET && (optname == PSP_NET_INET_SO_RCVTIMEO || optname == PSP_NET_INET_SO_SNDTIMEO)) {
		(optname == PSP_NET_INET_SO_RCVTIMEO ? inetSock->recvTimeoutUs : inetSock->sendTimeoutUs) = optval;
	}
	if (level == PSP_NET_INET_SOL_SOCKET && optval && (optname == PSP_NET_INET_SO_RCVTIMEO || optname == PSP_NET_INET_SO_SNDTIMEO)) {
		tval.tv_sec = optval / 1000000; // seconds
		tval.tv_usec = (optval % 1000000); // microseconds
		retval = setsockopt(inetSock->sock, convertSockoptLevelPSP2Host(level), convertSockoptNamePSP2Host(optname, level), (char*)&tval, sizeof(tval));
	} else {
		retval = setsockopt(inetSock->sock, convertSockoptLevelPSP2Host(level), convertSockoptNamePSP2Host(optname, level), (char*)&optval, optlen);
	}
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}
	return hleLogDebug(Log::sceNet, retval);
}

static int sceNetInetGetsockopt(MIPSState *mips, int socket, int level, int optname, u32 optvalPtr, u32 optlenPtr) {
	WARN_LOG(Log::sceNet, "sceNetInetGetsockopt(%i, %i, %i, %08x, %08x) at %08x", socket, level, optname, optvalPtr, optlenPtr, mips->pc);

	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	u32_le* optval = (u32_le*)Memory::GetPointerOrException(optvalPtr);
	socklen_t* optlen = (socklen_t*)Memory::GetPointerOrException(optlenPtr);
	DEBUG_LOG(Log::sceNet, "SockOpt: Level = %s, OptName = %s", inetSockoptLevel2str(level).c_str(), inetSockoptName2str(optname, level).c_str());
	timeval tval{};
	// TODO: Ignoring SO_NBIO/SO_NONBLOCK flag if we always use non-bloking mode (ie. simulated blocking mode)
	if (level == PSP_NET_INET_SOL_SOCKET && optname == PSP_NET_INET_SO_NBIO) {
		//*optlen = std::min(sizeof(sock->nonblocking), *optlen);
		//memcpy((int*)optval, &sock->nonblocking, *optlen);
		//if (sock->nonblocking && *optlen>0) *optval = 0x80; // on true, returning 0x80 when retrieved using getsockopt?
		return hleLogDebug(Log::sceNet, 0);
	}
	// FIXME: Should we ignore SO_BROADCAST flag since we are using fake broadcast (ie. only broadcast to friends),
	//        But Infrastructure/Online play might need to use broadcast for SSDP and to support LAN MP with real PSP
	/*else if (level == PSP_NET_INET_SOL_SOCKET && optname == PSP_NET_INET_SO_BROADCAST) {
		// *optlen = std::min(sizeof(sock->so_broadcast), *optlen);
		//memcpy((int*)optval, &sock->so_broadcast, *optlen);
		//if (sock->so_broadcast && *optlen>0) *optval = 0x80; // on true, returning 0x80 when retrieved using getsockopt?
		return hleLogSuccessI(Log::sceNet, 0);
	}*/
	// TODO: Ignoring SO_REUSEADDR flag to prevent disrupting multiple-instance feature
	else if (level == PSP_NET_INET_SOL_SOCKET && optname == PSP_NET_INET_SO_REUSEADDR) {
		//*optlen = std::min(sizeof(sock->reuseaddr), *optlen);
		//memcpy((int*)optval, &sock->reuseaddr, *optlen);
		return hleLogDebug(Log::sceNet, 0);
	}
	// TODO: Ignoring SO_REUSEPORT flag to prevent disrupting multiple-instance feature (not sure if PSP has SO_REUSEPORT or not tho, defined as 15 on Android)
	else if (level == PSP_NET_INET_SOL_SOCKET && optname == PSP_NET_INET_SO_REUSEPORT) { // 15
		//*optlen = std::min(sizeof(sock->reuseport), *optlen);
		//memcpy((int*)optval, &sock->reuseport, *optlen);
		return hleLogDebug(Log::sceNet, 0);
	}
	// TODO: Ignoring SO_NOSIGPIPE flag to prevent crashing PPSSPP (not sure if PSP has NOSIGPIPE or not tho, defined as 0x1022 on Darwin)
	else if (level == PSP_NET_INET_SOL_SOCKET && optname == 0x1022) { // PSP_NET_INET_SO_NOSIGPIPE ?
		//*optlen = std::min(sizeof(sock->nosigpipe), *optlen);
		//memcpy((int*)optval, &sock->nosigpipe, *optlen);
		return hleLogDebug(Log::sceNet, 0);
	}
	int retval = 0;
	// PSP timeout are a single 32bit value (micro seconds)
	if (level == PSP_NET_INET_SOL_SOCKET && optval && (optname == PSP_NET_INET_SO_RCVTIMEO || optname == PSP_NET_INET_SO_SNDTIMEO)) {
		socklen_t tvlen = sizeof(tval);
		retval = getsockopt(inetSock->sock, convertSockoptLevelPSP2Host(level), convertSockoptNamePSP2Host(optname, level), (char*)&tval, &tvlen);
		if (retval != SOCKET_ERROR) {
			u64_le val = (tval.tv_sec * 1000000LL) + tval.tv_usec;
			memcpy(optval, &val, std::min(static_cast<socklen_t>(sizeof(val)), std::min(static_cast<socklen_t>(sizeof(*optval)), *optlen)));
		}
	} else {
		retval = getsockopt(inetSock->sock, convertSockoptLevelPSP2Host(level), convertSockoptNamePSP2Host(optname, level), (char*)optval, optlen);
	}
	if (retval < 0) {
		UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}
	DEBUG_LOG(Log::sceNet, "SockOpt: OptValue = %d", *optval);
	return hleLogDebug(Log::sceNet, retval);
}

static int sceNetInetBind(MIPSState *mips, int socket, u32 namePtr, int namelen) {
	INFO_LOG(Log::sceNet, "sceNetInetBind(%i, %08x, %i) at %08x", socket, namePtr, namelen, mips->pc);

	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	SceNetInetSockaddr* name = (SceNetInetSockaddr*)Memory::GetPointerOrException(namePtr);
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
	unsigned short port = ntohs(saddr.in.sin_port);
	UPnP_Add((inetSock->type == PSP_NET_INET_SOCK_STREAM) ? IP_PROTOCOL_TCP : IP_PROTOCOL_UDP, port, port);

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
	fd_set wrfds, exfds;
	FD_ZERO(&wrfds);
	FD_ZERO(&exfds);
	FD_SET(inetSock->sock, &wrfds);
	FD_SET(inetSock->sock, &exfds);
	timeval tmout = { 0, 0 };
	int ready = select((int)inetSock->sock + 1, nullptr, &wrfds, &exfds, &tmout);
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
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	// Still using warn log here so it stands out in the log

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
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
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

	SceNetInetSockaddr* src = (SceNetInetSockaddr*)Memory::GetCharPointer(args[1]);
	socklen_t* srclen = (socklen_t*)Memory::GetCharPointer(args[2]);
	SockAddrIN4 saddr{};
	if (srclen)
		*srclen = std::min((*srclen) > 0 ? *srclen : 0, static_cast<socklen_t>(sizeof(saddr)));

	int newHostSocket = accept(inetSock->sock, (struct sockaddr*)&saddr.addr, srclen);
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
		// Ran out of space. Shouldn't really happen.
		ERROR_LOG(Log::sceNet, "Accept: out of socket IDs");
		UpdateErrnoFromHost(threadID, ENOMEM, "sceNetInetAccept");
		return { -1, false, 0 };
	}

	if (src) {
		src->sa_family = saddr.addr.sa_family;
		memcpy(src->sa_data, saddr.addr.sa_data, sizeof(src->sa_data));
		src->sa_len = srclen ? *srclen : 0;
	}
	DEBUG_LOG(Log::sceNet, "Accept: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	return { newSocketId, false, 0 };
}

static int sceNetInetAccept(int socket, u32 addrPtr, u32 addrLenPtr) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
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
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	// Convert HOW from PSP to Host
	int hostHow = how;
	switch (how) {
	case PSP_NET_INET_SHUT_RD: hostHow = SHUT_RD; break;
	case PSP_NET_INET_SHUT_WR: hostHow = SHUT_WR; break;
	case PSP_NET_INET_SHUT_RDWR: hostHow = SHUT_RDWR; break;
	}

	int retVal = shutdown(inetSock->sock, hostHow);  // no translation
	return hleLogInfo(Log::sceNet, retVal);
}

static int sceNetInetSocketAbort(int socket) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	// FIXME: either using shutdown/close or select? probably using select if blocking mode is being simulated with non-blocking
	int retVal = shutdown(inetSock->sock, SHUT_RDWR);
	return hleLogInfo(Log::sceNet, retVal);
}

static int sceNetInetClose(int socket) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	g_socketManager.Close(inetSock);
	return hleLogInfo(Log::sceNet, 0);
}

// TODO: How is this different than just sceNetInetClose?
static int sceNetInetCloseWithRST(int socket) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	// Based on http://deepix.github.io/2016/10/21/tcprst.html
	struct linger sl {};
	sl.l_onoff = 1;		// non-zero value enables linger option in kernel
	sl.l_linger = 0;	// timeout interval in seconds
	setsockopt(inetSock->sock, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
	g_socketManager.Close(inetSock);
	return hleLogInfo(Log::sceNet, 0);
}

// args: socket, buf, len, flags, from, fromlen.
static InetTry TryRecvFrom(SceUID threadID, const u32 *args) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket((int)args[0], &inetSock)) {
		UpdateErrnoFromHost(threadID, EBADF, "sceNetInetRecvfrom");
		return { -1, false, 0 };
	}

	SceNetInetSockaddr* src = (SceNetInetSockaddr*)Memory::GetCharPointer(args[4]);
	socklen_t* srclen = (socklen_t*)Memory::GetCharPointer(args[5]);
	SockAddrIN4 saddr{};
	if (srclen)
		*srclen = std::min((*srclen) > 0 ? *srclen : 0, static_cast<socklen_t>(sizeof(saddr)));
	int flgs = args[3] & ~PSP_NET_INET_MSG_DONTWAIT; // removing non-POSIX flag, which is an alternative way to use non-blocking mode
	flgs = convertMSGFlagsPSP2Host(flgs);
	int retval = recvfrom(inetSock->sock, (char*)Memory::GetPointerOrException(args[1]), (int)args[2], flgs | MSG_NOSIGNAL, (struct sockaddr*)&saddr.addr, srclen);
	if (retval < 0) {
		const int hostErrno = socket_errno;
		if (IsWouldBlock(hostErrno)) {
			return { retval, true, hostErrno };
		}
		UpdateErrnoFromHost(threadID, hostErrno, "sceNetInetRecvfrom");
		return { retval, false, 0 };
	}

	if (src) {
		src->sa_family = saddr.addr.sa_family;
		memcpy(src->sa_data, saddr.addr.sa_data, sizeof(src->sa_data));
		src->sa_len = srclen ? *srclen : 0;
	}

	std::string datahex;
	DataToHexString(0, 0, Memory::GetPointerOrException(args[1]), retval, &datahex);
	VERBOSE_LOG(Log::sceNet, "Data Dump (%d bytes):\n%s", retval, datahex.c_str());
	DEBUG_LOG(Log::sceNet, "RecvFrom: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	return { retval, false, 0 };
}

static int sceNetInetRecvfrom(int socket, u32 bufferPtr, int len, int flags, u32 fromPtr, u32 fromlenPtr) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
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
		UpdateErrnoFromHost(threadID, hostErrno, "sceNetInetSendto");
		return { retval, false, 0 };
	}
	DEBUG_LOG(Log::sceNet, "SendTo: Address = %s, Port = %d", ip2str(saddr.in.sin_addr).c_str(), ntohs(saddr.in.sin_port));
	return { retval, false, 0 };
}

static int sceNetInetSendto(int socket, u32 bufferPtr, int len, int flags, u32 toPtr, int tolen) {
	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
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
	if (!Memory::IsValidAddress(msghdrPtr)) {
		UpdateErrnoFromHost(__KernelGetCurThread(), EFAULT, __FUNCTION__);
		return hleLogError(Log::sceNet, retval);
	}

	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
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
		if (UpdateErrnoFromHost(__KernelGetCurThread(), socket_errno, __FUNCTION__) == ERROR_INET_EAGAIN) {
			return hleLogDebug(Log::sceNet, retval, "EAGAIN");
		} else {
			return hleLogError(Log::sceNet, retval);
		}
	}
	return hleLogInfo(Log::sceNet, retval); // returns number of bytes sent?
}

// Similar to POSIX's recvmsg or Mswsock's WSARecvMsg? Are their packets compatible one another?
// Games using this: World of Poker
static int sceNetInetRecvmsg(MIPSState *mips, int socket, u32 msghdrPtr, int flags) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %08x, %08x) at %08x", __FUNCTION__, socket, msghdrPtr, flags, mips->pc);

	InetSocket *inetSock;
	if (!g_socketManager.GetInetSocket(socket, &inetSock)) {
		return hleLogError(Log::sceNet, ERROR_INET_EBADF, "Bad socket #%d", socket);
	}

	// Reference: http://www.masterraghu.com/subjects/np/introduction/unix_network_programming_v1.3/ch14lev1sec5.html
	int retval = -1;
	if (!Memory::IsValidAddress(msghdrPtr)) {
		UpdateErrnoFromHost(__KernelGetCurThread(), EFAULT, __FUNCTION__);
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

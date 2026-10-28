#include "Common/Net/SocketCompat.h"
#include "Core/HLE/NetInetConstants.h"
#include "Core/HLE/SocketManager.h"
#include "Common/Log.h"

#include <mutex>

SocketManager g_socketManager;
static std::mutex g_socketMutex;  // TODO: Remove once the adhoc thread is gone

InetSocket *SocketManager::CreateSocket(int *index, int *returned_errno, SocketState state, int domain, int type, int protocol) {
	_dbg_assert_(state != SocketState::Unused);

	int hostDomain = convertSocketDomainPSP2Host(domain);
	int hostType = convertSocketTypePSP2Host(type);
	int hostProtocol = convertSocketProtoPSP2Host(protocol);

	SOCKET hostSock = ::socket(hostDomain, hostType, hostProtocol);
	if (hostSock < 0) {
		*returned_errno = socket_errno;
		return nullptr;
	}

	std::lock_guard<std::mutex> guard(g_socketMutex);

	const int i = FindFreeSlot();
	if (i == 0) {
		// The PSP runs out at 255 too, with EMFILE (pspautotests net/inet/errors).
		ERROR_LOG(Log::sceNet, "Ran out of socket handles!");
		closesocket(hostSock);
		*index = 0;
		*returned_errno = EMFILE;
		return nullptr;
	}

	*index = i;
	InetSocket *inetSock = inetSockets_ + i;
	*inetSock = {};  // Reset to default.
	inetSock->sock = hostSock;
	inetSock->state = state;
	inetSock->domain = domain;
	inetSock->type = type;
	inetSock->protocol = protocol;
	inetSock->nonblocking = false;
	// TCP and UDP defaults from pspautotests net/inet/options. Other types get UDP's.
	const bool stream = type == PSP_NET_INET_SOCK_STREAM;
	inetSock->sndBuf = stream ? 16384 : 9216;
	inetSock->rcvBuf = stream ? 16384 : 41600;
	inetSock->sndLowat = 2048;
	inetSock->rcvLowat = 1;
	inetSock->ipTtl = 64;
	*returned_errno = 0;
	return inetSock;
}

// Numbers are handed out round-robin, the next free one after the last, wrapping from 255 to 1
// (pspautotests net/inet/states). Returns 0 if they're all in use.
int SocketManager::FindFreeSlot() {
	for (int n = 0; n < VALID_INET_SOCKET_COUNT - MIN_VALID_INET_SOCKET; n++) {
		int i = lastSlot_ + 1 + n;
		if (i >= VALID_INET_SOCKET_COUNT) {
			i -= VALID_INET_SOCKET_COUNT - MIN_VALID_INET_SOCKET;
		}
		if (inetSockets_[i].state == SocketState::Unused) {
			lastSlot_ = i;
			return i;
		}
	}
	return 0;
}

InetSocket *SocketManager::AdoptSocket(int *index, SOCKET hostSocket, const InetSocket *derive) {
	std::lock_guard<std::mutex> guard(g_socketMutex);

	const int i = FindFreeSlot();
	if (i == 0) {
		// No space. Shouldn't ever happen.
		*index = 0;
		return nullptr;
	}

	*index = i;
	InetSocket *inetSock = inetSockets_ + i;
	// Options and all, like BSD's sonewconn().
	*inetSock = *derive;
	inetSock->sock = hostSocket;
	inetSock->addr.clear();
	inetSock->port = 0;
	inetSock->upnpPort = 0;
	inetSock->boundPort = 0;
	inetSock->tcpClosed = false;
	inetSock->listening = false;
	return inetSock;
}

// The slot is freed even if the host's close fails, since the host socket is no use either way.
bool SocketManager::Close(InetSocket *inetSocket) {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	_dbg_assert_(inetSocket->state != SocketState::Unused);
	const bool closed = closesocket(inetSocket->sock) == 0;
	if (!closed) {
		ERROR_LOG(Log::sceNet, "closesocket(%d) failed: %d", (int)inetSocket->sock, socket_errno);
	}
	inetSocket->state = SocketState::Unused;
	inetSocket->sock = 0;
	return closed;
}

bool SocketManager::GetInetSocket(int sock, InetSocket **inetSocket) {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	if (sock < MIN_VALID_INET_SOCKET || sock >= ARRAY_SIZE(inetSockets_) || inetSockets_[sock].state == SocketState::Unused) {
		*inetSocket = nullptr;
		return false;
	}
	*inetSocket = inetSockets_ + sock;
	return true;
}

// Simplified mappers, only really useful in select/poll
SOCKET SocketManager::GetHostSocketFromInetSocket(int sock) {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	if (sock < MIN_VALID_INET_SOCKET || sock >= ARRAY_SIZE(inetSockets_) || inetSockets_[sock].state == SocketState::Unused) {
		_dbg_assert_(false);
		return -1;
	}
	if (sock == 0) {
		// Map 0 to 0, special case.
		return 0;
	}
	return inetSockets_[sock].sock;
}

void SocketManager::CloseAll() {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	for (auto &sock : inetSockets_) {
		if (sock.state != SocketState::Unused) {
			closesocket(sock.sock);
		}
		sock.state = SocketState::Unused;
		sock.sock = 0;
	}
	lastSlot_ = 0;
}

const char *SocketStateToString(SocketState state) {
	switch (state) {
	case SocketState::Unused: return "unused";
	case SocketState::UsedNetInet: return "netInet";
	case SocketState::UsedProAdhoc: return "proAdhoc";
	default:
		return "N/A";
	}
}

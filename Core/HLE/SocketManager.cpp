#include "Core/HLE/SocketManager.h"
#include "Common/Log.h"

#include <mutex>

// We use this array from 1 and forward. It's probably not a good idea to return 0 as a socket.
InetSocket g_inetSockets[256];
static std::mutex g_socketMutex;  // TODO: Remove once the adhoc thread is gone

int AllocInetSocket() {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	for (int i = MIN_VALID_INET_SOCKET; i < ARRAY_SIZE(g_inetSockets); i++) {
		if (g_inetSockets[i].state == SocketState::Unused) {
			return i;
		}
	}
	_dbg_assert_(false);
	ERROR_LOG(Log::sceNet, "Ran out of socket handles! This is BAD.");
	return 0;
}

bool GetInetSocket(int sock, InetSocket **inetSocket) {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	if (sock < MIN_VALID_INET_SOCKET || sock >= ARRAY_SIZE(g_inetSockets) || g_inetSockets[sock].state == SocketState::Unused) {
		*inetSocket = nullptr;
		return false;
	}
	*inetSocket = &g_inetSockets[sock];
	return true;
}

// Simplified mappers, only really useful in select/poll
SOCKET GetHostSocketFromInetSocket(int sock) {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	if (sock < MIN_VALID_INET_SOCKET || sock >= ARRAY_SIZE(g_inetSockets) || g_inetSockets[sock].state == SocketState::Unused) {
		_dbg_assert_(false);
		return -1;
	}
	if (sock == 0) {
		// Map 0 to 0, special case.
		return 0;
	}
	return g_inetSockets[sock].sock;
}

void CloseAllSockets() {
	for (auto &sock : g_inetSockets) {
		if (sock.state != SocketState::Unused) {
			closesocket(sock.sock);
		}
		sock.state = SocketState::Unused;
		sock.sock = 0;
	}
}

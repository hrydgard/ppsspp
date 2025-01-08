#include "Core/HLE/SocketManager.h"
#include "Common/Log.h"

#include <mutex>

SocketManager g_socketManager;
static std::mutex g_socketMutex;  // TODO: Remove once the adhoc thread is gone

InetSocket *SocketManager::AllocSocket(int *index) {
	std::lock_guard<std::mutex> guard(g_socketMutex);
	for (int i = MIN_VALID_INET_SOCKET; i < ARRAY_SIZE(inetSockets_); i++) {
		if (inetSockets_[i].state == SocketState::Unused) {
			*index = i;
			return inetSockets_ + i;
		}
	}
	_dbg_assert_(false);

	ERROR_LOG(Log::sceNet, "Ran out of socket handles! This is BAD.");
	*index = 0;
	return 0;
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
	for (auto &sock : inetSockets_) {
		if (sock.state != SocketState::Unused) {
			closesocket(sock.sock);
		}
		sock.state = SocketState::Unused;
		sock.sock = 0;
	}
}

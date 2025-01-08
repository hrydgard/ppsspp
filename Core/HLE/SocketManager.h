#pragma once

#include "Common/Net/SocketCompat.h"

// Keep track of who's using a socket.
enum class SocketState {
	Unused,
	UsedNetInet,
	UsedProAdhoc,
};

const char *SocketStateToString(SocketState state);

// Internal socket state tracking
struct InetSocket {
	SOCKET sock;  // native socket
	SocketState state;
	// NOTE: These are the PSP types. Can be converted to the host types if needed.
	int domain;
	int type;
	int protocol;
	bool nonblocking;
};

// Only use this for sockets whose ID are exposed to the game.
// Don't really need to bother with the others, as the game doesn't know about them.
class SocketManager {
public:
	enum {
		VALID_INET_SOCKET_COUNT = 256,
		MIN_VALID_INET_SOCKET = 61,
	};

	InetSocket *CreateSocket(int *index, SocketState state, int domain, int type, int protocol);
	bool GetInetSocket(int sock, InetSocket **inetSocket);
	SOCKET GetHostSocketFromInetSocket(int sock);
	bool Close(InetSocket *inetSocket);
	void CloseAll();

	// For debugger
	const InetSocket *Sockets() {
		return inetSockets_;
	}

private:
	// We use this array from MIN_VALID_INET_SOCKET and forward. It's probably not a good idea to return 0 as a socket.
	InetSocket inetSockets_[VALID_INET_SOCKET_COUNT];
};

extern SocketManager g_socketManager;

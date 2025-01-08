#pragma once

#include "Common/Net/SocketCompat.h"

// Keep track of who's using a socket.
enum class SocketState {
	Unused,
	UsedNetInet,
	UsedProAdhoc,
};

// Internal socket state tracking
struct InetSocket {
	SOCKET sock;  // native socket
	SocketState state;
	// NOTE: These are the PSP types for now
	int domain;
	int type;
	int protocol;
	// These are the host types for convenience.
	int hostDomain;
	int hostType;
	int hostProtocol;
};

#define MIN_VALID_INET_SOCKET 20
#define VALID_INET_SOCKET_COUNT 256

extern InetSocket g_inetSockets[VALID_INET_SOCKET_COUNT];

int AllocInetSocket();
bool GetInetSocket(int sock, InetSocket **inetSocket);
SOCKET GetHostSocketFromInetSocket(int sock);
void CloseAllSockets();

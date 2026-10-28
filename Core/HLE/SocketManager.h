#pragma once

#include "Common/CommonTypes.h"
#include "Common/Net/SocketCompat.h"

// Keep track of who's using a socket.
enum class SocketState {
	Unused = 0,
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
	// SO_RCVTIMEO and SO_SNDTIMEO, for the blocking calls we wait for in emulated time. 0 is forever.
	u32 recvTimeoutUs;
	u32 sendTimeoutUs;
	// The other socket-level options, as the game sees them. Only a few are passed on to the host socket.
	u32 soOptions;  // PSP_NET_INET_SO_* flags, like BSD's so_options.
	int lingerTime;
	int sndBuf;
	int rcvBuf;
	int sndLowat;
	int rcvLowat;
	// The port bind() asked UPnP to forward, removed on close. 0 if none.
	u16 upnpPort;
	// What the game bound the socket to, for telling when a port is in use. boundPort is 0 if it hasn't
	// bound one itself. boundAddr is in network byte order.
	u32 boundAddr;
	u16 boundPort;
	// A TCP socket whose protocol control block 4.4BSD has thrown away: after shutting down the sending
	// side while not connected, or a failed connect. Most calls on it then fail with EINVAL.
	bool tcpClosed;
	// Whether a listen() on it succeeded.
	bool listening;
	// IP_TTL, kept here since the host's default differs (Windows' is 128, the PSP's 64).
	int ipTtl;
	// Metadata for debug use only.
	std::string addr;
	int port;
};

// Only use this for sockets whose ID are exposed to the game.
// Don't really need to bother with the others, as the game doesn't know about them.
class SocketManager {
public:
	enum {
		VALID_INET_SOCKET_COUNT = 256,
		MIN_VALID_INET_SOCKET = 1,
	};

	InetSocket *CreateSocket(int *index, int *returned_errno, SocketState state, int domain, int type, int protocol);
	// for accept()
	InetSocket *AdoptSocket(int *index, SOCKET hostSocket, const InetSocket *derive);

	bool GetInetSocket(int sock, InetSocket **inetSocket);
	SOCKET GetHostSocketFromInetSocket(int sock);
	bool Close(InetSocket *inetSocket);
	void CloseAll();

	// For debugger
	const InetSocket *Sockets() {
		return inetSockets_;
	}

private:
	int FindFreeSlot();

	// The last number handed out. Numbers go round-robin from there.
	int lastSlot_ = 0;
	// We use this array from MIN_VALID_INET_SOCKET and forward. It's probably not a good idea to return 0 as a socket.
	InetSocket inetSockets_[VALID_INET_SOCKET_COUNT];
};

extern SocketManager g_socketManager;

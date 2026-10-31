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

// The P2P transport: ad hoc sockets as the host's own UDP (PDP) and TCP (PTP) sockets, straight to the
// other consoles.

#include "ppsspp_config.h"

#include <mutex>
#include <string>

#include "Common/Net/SocketCompat.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"
#include "Common/TimeUtil.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Util/PortManager.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/AdhocTransport.h"
#include "Core/HLE/NetAdhocCommon.h"
#include "Core/HLE/NetInetConstants.h"

#ifdef _WIN32
#undef errno
#define errno WSAGetLastError()
#endif

void P2PTransport::CloseHostSocket(AdhocSocket *sock) {
	int fd = -1;
	if (sock->type == SOCK_PTP)
		fd = sock->data.ptp.id;
	else if (sock->type == SOCK_PDP)
		fd = sock->data.pdp.id;

	if (fd > 0) {
		// Close Socket
		shutdown(fd, SD_RECEIVE);
		closesocket(fd);
	}
}

// A connection in progress shows up as established in GetPtpStat, and PollSocket reports CONNECT, as
// soon as it's up (pspautotests net/adhoc/pair/ptpconnect).
// Whether a socket whose connect() returned EISCONN is really connected. macOS answers a repeated
// connect() with EISCONN after the first one was refused too; then getpeername fails and the refusal
// is in SO_ERROR, which goes to *error.
static bool ptpReallyConnected(int fd, int *error) {
	struct sockaddr_in sin;
	socklen_t sinlen = sizeof(sin);
	if (getpeername(fd, (struct sockaddr *)&sin, &sinlen) == 0) {
		return true;
	}
	int soError = 0;
	socklen_t soErrorLen = sizeof(soError);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soError, &soErrorLen);
	*error = soError != 0 ? soError : ECONNREFUSED;
	return false;
}

int P2PTransport::PdpSendTo(int id, const SceNetEtherAddr *mac, uint16_t dport, const sockaddr_in &target, const void *data, int len, int *error) {
	// Out of the group, it goes into the air and reaches nobody (pspautotests net/adhoc/pair/vanish).
	if (offAdhocNetwork() && !isLocalMac(mac)) {
		return len;
	}
	int sent = sendto(adhocSockets[id - 1]->data.pdp.id, (const char *)data, len, MSG_NOSIGNAL, (const struct sockaddr *)&target, sizeof(target));
	*error = socket_errno;
	return sent;
}

bool P2PTransport::PdpTakeDatagram(int id, SceNetEtherAddr *mac, uint16_t *port, std::vector<u8> &data) {
	int fd = adhocSockets[id - 1]->data.pdp.id;
	while (true) {
		struct sockaddr_in sin {};
		socklen_t sinlen = sizeof(sin);
		// On Windows this can fail with WSAECONNRESET when an earlier sendto's destination was unreachable;
		// that's nothing waiting too.
		int received = recvfrom(fd, dummyPeekBuf64k, dummyPeekBuf64kSize, MSG_NOSIGNAL, (struct sockaddr *)&sin, &sinlen);
		if (received == SOCKET_ERROR) {
			return false;
		}
		// Only from someone in the group (another app could be sending to the same port), and out of the
		// group, from nobody else (pspautotests net/adhoc/pair/vanish).
		if (!resolveIP(sin.sin_addr.s_addr, mac) || (offAdhocNetwork() && !isLocalMac(mac))) {
			continue;
		}
		*port = ntohs(sin.sin_port) - portOffset;
		data.assign(dummyPeekBuf64k, dummyPeekBuf64k + received);
		return true;
	}
}

// send() and recv() on a PTP socket, as a PSP's radio would see them (pspautotests net/adhoc/pair/vanish):
// out of the group, a send goes nowhere and nothing arrives, and a connection to a peer that has left the
// network fails once its retransmits would have run out.
static int ptpSendToPeer(AdhocSocket *sock, const char *data, int len, int *error) {
	if (offAdhocNetwork()) {
		*error = 0;
		return len;
	}
	if (ptpPeerGone(sock)) {
		*error = ECONNRESET;
		return SOCKET_ERROR;
	}
	int sent = send(sock->data.ptp.id, data, len, MSG_NOSIGNAL);
	*error = socket_errno;
	if (sent > 0) {
		ptpNoteSend(sock);
	}
	return sent;
}

static int ptpRecvFromPeer(AdhocSocket *sock, char *buf, int len, int *error) {
	if (offAdhocNetwork()) {
		*error = EAGAIN;
		return SOCKET_ERROR;
	}
	if (ptpPeerGone(sock)) {
		*error = ECONNRESET;
		return SOCKET_ERROR;
	}
	int received = recv(sock->data.ptp.id, buf, len, MSG_NOSIGNAL);
	*error = socket_errno;
	return received;
}

int P2PTransport::DoBlockingPtpSend(AdhocSocketRequest &req, s64 &result) {
	auto sock = adhocSockets[req.id - 1];
	auto& ptpsocket = sock->data.ptp;

	// Send Data
	int ret = 0;
	int sockerr = 0;
	ret = ptpSendToPeer(sock, (const char*)req.buffer, *req.length, &sockerr);

	// Success
	if (ret > 0) {
		// Save Length
		*req.length = ret;

		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u\n", req.id, ptpsocket.lport, ret, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

		// Set to Established on successful Send when an attempt to Connect was initiated
		if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
			ptpSetEstablished(sock);

		// Return Success
		result = 0;
	}
	else if (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (sockerr == ENOTCONN || connectInProgress(sockerr))))) {
		u64 now = CoreTiming::GetGlobalTimeUs();
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else
			result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	}
	else {
		// Change Socket State. // FIXME: Does Alerted Socket should be closed too?
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

		// Disconnected
		result = SCE_NET_ADHOC_ERROR_DISCONNECTED;
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int P2PTransport::DoBlockingPtpRecv(AdhocSocketRequest &req, s64 &result) {
	auto sock = adhocSockets[req.id - 1];
	auto& ptpsocket = sock->data.ptp;

	int ret = 0;
	int sockerr = 0;
	ret = ptpRecvFromPeer(sock, (char*)req.buffer, std::max(0, *req.length), &sockerr);

	// Received Data. POSIX: May received 0 bytes when the remote peer already closed the connection.
	if (ret > 0) {
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %s:%u\n", req.id, ptpsocket.lport, ret, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);
		// Save Length
		*req.length = ret;

		// Update last recv timestamp
		peerlock.lock();
		auto peer = findFriend(&ptpsocket.paddr);
		if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
		peerlock.unlock();

		// Set to Established on successful Recv when an attempt to Connect was initiated
		if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
			ptpSetEstablished(sock);

		result = 0;
	}
	else if (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (sockerr == ENOTCONN || connectInProgress(sockerr))))) {
		u64 now = CoreTiming::GetGlobalTimeUs();
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else
			result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	}
	else {
		// Change Socket State. // FIXME: Does Alerted Socket should be closed too?
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

		// Disconnected
		result = SCE_NET_ADHOC_ERROR_DISCONNECTED; // SCE_NET_ADHOC_ERROR_INVALID_ARG
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int P2PTransport::DoBlockingPtpAccept(AdhocSocketRequest &req, s64 &result) {
	auto& ptpsocket = adhocSockets[req.id - 1]->data.ptp;

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	socklen_t sinlen = sizeof(sin);
	int ret, sockerr;

	// Check if listening socket is ready to accept
	ret = IsSocketReady(ptpsocket.id, true, false, &sockerr);
	if (ret > 0) {
		// Accept Connection
		ret = accept(ptpsocket.id, (struct sockaddr*)&sin, &sinlen);
		sockerr = socket_errno;
	}

	// Accepted New Connection
	int newid = 0;
	if (ret > 0) {
		// Fails (and closes the connection) when it comes from someone not in the group (yet); then keep
		// waiting, like the relay transport.
		newid = AcceptPtpSocket(req.id, ret, sin, req.remoteMAC, req.remotePort);
	}
	if (newid > 0) {
		result = newid;
	}
	else if (ret >= 0 || (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK))) {
		u64 now = CoreTiming::GetGlobalTimeUs();
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else {
				result = SCE_NET_ADHOC_ERROR_TIMEOUT;
		}
	}
	else
		result = SCE_NET_ADHOC_ERROR_INVALID_ARG; //SCE_NET_ADHOC_ERROR_TIMEOUT

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

bool P2PTransport::PtpConnectionUp(AdhocSocket *sock) {
	if (IsSocketReady(sock->data.ptp.id, true, true) <= 0) {
		return false;
	}
	struct sockaddr_in sin;
	socklen_t sinlen = sizeof(sin);
	memset(&sin, 0, sinlen);
	// Ensure that the connection really established or not, since "select" alone can't accurately detects it
	return getpeername(sock->data.ptp.id, (struct sockaddr*)&sin, &sinlen) != SOCKET_ERROR;
}

bool P2PTransport::PtpConnectedSinceOpen(AdhocSocket *sock) {
	// Not getpeername alone: on Windows it succeeds while the connect is still in progress, even one that
	// is about to be refused.
	return PtpConnectionUp(sock);
}

int P2PTransport::PtpConnect(int id, const sockaddr_in &sin, int *error) {
	int fd = adhocSockets[id - 1]->data.ptp.id;
	int ret = connect(fd, (const struct sockaddr *)&sin, sizeof(sin));
	*error = socket_errno;
	if (ret == SOCKET_ERROR && *error == EISCONN) {
		ptpReallyConnected(fd, error);
	}
	return ret;
}

int P2PTransport::PtpConnectPoll(AdhocSocket *sock, int *error) {
	int fd = sock->data.ptp.id;
	*error = 0;
#if !defined(_WIN32)
	if (fd < 0 || fd >= FD_SETSIZE) {
		*error = EBADF;
		return SOCKET_ERROR;
	}
#endif
	// Windows never makes a socket whose connect failed writable: the failure (like a refusal) shows in the
	// exception set, and the reason in SO_ERROR.
	fd_set writefds, exceptfds;
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	FD_SET(fd, &writefds);
	FD_SET(fd, &exceptfds);
	timeval tv = {};
	int ret = select(fd + 1, nullptr, &writefds, &exceptfds, &tv);
	if (ret < 0) {
		*error = socket_errno;
		return ret;
	}
	if (ret > 0) {
		// Elsewhere a failed connect is writable, with its reason in SO_ERROR too.
		int soError = 0;
		socklen_t soErrorLen = sizeof(soError);
		getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soError, &soErrorLen);
		if (soError != 0 || FD_ISSET(fd, &exceptfds)) {
			*error = soError != 0 ? soError : ECONNREFUSED;
			return SOCKET_ERROR;
		}
	}
	return ret;
}

int P2PTransport::PtpConnectVerify(int id, sockaddr_in *sin, int ret, int *error) {
	auto& ptpsocket = adhocSockets[id - 1]->data.ptp;
	socklen_t sinlen = sizeof(*sin);
	// Note: "getpeername" shouldn't failed if the connection has been established, but on Windows it may succeed even when "connect" is still in-progress and not accepted yet (ie. "Tales of VS" on Windows)
	ret = getpeername(ptpsocket.id, (struct sockaddr*)sin, &sinlen);
	if (ret == SOCKET_ERROR) {
		int err = socket_errno;
		VERBOSE_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: getpeername(%i) error %i, sockerr = %i", id, ptpsocket.lport, ptpsocket.id, err, *error);
		*error = err;
	}
	return ret;
}

bool P2PTransport::PtpConnectPending(int error) {
	return connectInProgress(error);
}

bool P2PTransport::PdpReadyToSend(AdhocSocket *sock) {
	return IsSocketReady(sock->data.pdp.id, false, true) > 0;
}

int P2PTransport::PdpCreate(const SceNetEtherAddr *saddr, int port, int bufferSize, u32 flag, bool isClient, bool enetReliable) {
	// Create Internet UDP Socket
	// Socket is remapped through adhocSockets
	int usocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (usocket == INVALID_SOCKET) {
		DEBUG_LOG(Log::sceNet, "%s: sock creation failed", __func__);
		return ERROR_NET_NO_SPACE;
	}

	// Change socket buffer size to be consistent on all platforms.
	// Send Buffer should be smaller than Recv Buffer to prevent faster device from flooding slower device too much.
	setSockBufferSize(usocket, SO_SNDBUF, bufferSize*5); //PSP_ADHOC_PDP_MFS
	// Recv Buffer should be equal or larger than Send Buffer. Using larger Recv Buffer might helped reduces dropped packets during a slowdown, but too large may cause slow performance on Warriors Orochi 2.
	setSockBufferSize(usocket, SO_RCVBUF, bufferSize*10); //PSP_ADHOC_PDP_MFS*10

	// Ignore SIGPIPE when supported (ie. BSD/MacOS)
	setSockNoSIGPIPE(usocket, 1);

	// Enable Port Re-use, this will allow binding to an already used port, but only one of them can read the data (shared receive buffer?)
	setSockReuseAddrPort(usocket);

	// Disable Connection Reset error on UDP to avoid strange behavior https://stackoverflow.com/questions/34242622/windows-udp-sockets-recvfrom-fails-with-error-10054
	setUDPConnReset(usocket, false);

	// Binding Information for local Port
	struct sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	if (isLocalServer) {
		getLocalIp(&addr);
	}
	uint16_t requestedport = static_cast<int>(port + static_cast<int>(portOffset));
	// Avoid getting random port due to port offset when original port wasn't 0 (ie. original_port + port_offset = 65536 = 0)
	if (requestedport == 0 && port > 0)
		requestedport = 65535; // Hopefully it will be safe to default it to 65535 since there can't be more than one port that can bumped into 65536

	// Show a warning about privileged ports
	if (requestedport != 0 && requestedport < 1024) {
		WARN_LOG(Log::sceNet, "sceNetAdhocPdpCreate - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
	}
	addr.sin_port = htons(requestedport);


	// Bound Socket to local Port
	int iResult = bind(usocket, (struct sockaddr*)&addr, sizeof(addr));
	// A port picked for port 0 may be taken on the host; pick another.
	for (int tries = 0; iResult != 0 && isClient && tries < 16; tries++) {
		port = get_unused_pdp_port();
		requestedport = static_cast<int>(port + static_cast<int>(portOffset));
		if (requestedport == 0)
			requestedport = 65535;
		addr.sin_port = htons(requestedport);
		iResult = bind(usocket, (struct sockaddr*)&addr, sizeof(addr));
	}

	if (iResult != 0) {
		DEBUG_LOG(Log::sceNet, "%s: sock bind failed", __func__);
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Port")) + " " + std::to_string(port + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 0.0f, "portbindfail");
		closesocket(usocket);
		return ERROR_NET_NO_SPACE;
	}

	// Workaround: Send a dummy 0 size message to AdhocServer IP to make sure the socket actually bound to an address when binded with INADDR_ANY before using getsockname, seems to fix sending from incorrect port issue on MGS:PW on Android
	auto server_ip = GetAdhocServerIP();
	addr.sin_addr.s_addr = server_ip.in.sin_addr.s_addr;
	addr.sin_port = 0;
	sendto(usocket, dummyPeekBuf64k, 0, MSG_NOSIGNAL, (struct sockaddr*)&addr, sizeof(addr));
	// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
	socklen_t len = sizeof(addr);
	if (getsockname(usocket, (struct sockaddr*)&addr, &len) == 0) {
		uint16_t boundport = ntohs(addr.sin_port);
		if (port + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
			WARN_LOG(Log::sceNet, "sceNetAdhocPdpCreate - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", port, requestedport, boundport, boundport - portOffset);
		port = boundport - portOffset;
	}

	// Allocate Memory for Internal Data
	AdhocSocket * internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));

	if (internal == NULL) {
		DEBUG_LOG(Log::sceNet, "%s: out of memory when allocating internal socket tracking", __func__);
		closesocket(usocket);
		return ERROR_NET_NO_SPACE;
	}

	// Find Free Translator Index
	// FIXME: We should probably use an increasing index instead of looking for an empty slot from beginning if we want to simulate a real socket id
	int i = 0;
	i = FindFreeAdhocSocketSlot();

	if (i >= MAX_SOCKET) {
		DEBUG_LOG(Log::sceNet, "%s: out of slot when allocating internal socket tracking", __func__);
		free(internal);
		closesocket(usocket);
		return ERROR_NET_NO_SPACE;
	}

	// Clear Memory
	memset(internal, 0, sizeof(AdhocSocket));

	// Socket Type
	internal->type = SOCK_PDP;
	internal->nonblocking = flag;
	internal->buffer_size = bufferSize;
	internal->isClient = isClient;

	// Fill in Data
	internal->data.pdp.id = usocket;
	internal->data.pdp.laddr = *saddr;
	internal->data.pdp.lport = port; //getLocalPort(usocket) - portOffset;

	// Link Socket to Translator ID
	adhocSockets[i] = internal;

	// Forward Port on Router
	//sceNetPortOpen("UDP", port);
	UPnP_Add(IP_PROTOCOL_UDP, isOriPort ? port : port + portOffset, port + portOffset); // g_PortManager.Add(IP_PROTOCOL_UDP, isOriPort ? port : port + portOffset, port + portOffset);

	// Switch to non-blocking for futher usage
	changeBlockingMode(usocket, 1);

	// Success
	INFO_LOG(Log::sceNet, "sceNetAdhocPdpCreate - PSP Socket id: %i, Host Socket id: %i", i + 1, usocket);
	return i + 1;
}

static int hostSocketOf(const AdhocSocket *sock) {
	return sock->type == SOCK_PTP ? sock->data.ptp.id : sock->data.pdp.id;
}

int P2PTransport::PollWait(SceNetAdhocPollSd *sds, int count, int timeout, AdhocPollState *states) {
	//WSAPoll only available for Vista or newer, so we'll use an alternative way for XP since Windows doesn't have poll function like *NIX
	fd_set readfds, writefds, exceptfds;
	int maxfd = 0;
	FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);

	for (int i = 0; i < count; i++) {
		if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
			int fd = hostSocketOf(adhocSockets[sds[i].id - 1]);
			if (fd > maxfd) maxfd = fd;
			FD_SET(fd, &readfds);
			FD_SET(fd, &writefds);
			FD_SET(fd, &exceptfds);
		}
	}
	timeval tmout;
	tmout.tv_sec = timeout / 1000000; // seconds
	tmout.tv_usec = (timeout % 1000000); // microseconds
	int ret = select(maxfd + 1, &readfds, &writefds, &exceptfds, &tmout);
	if (ret < 0) {
		return ret;
	}
	for (int i = 0; i < count; i++) {
		if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
			auto sock = adhocSockets[sds[i].id - 1];
			int fd = hostSocketOf(sock);
			states[i].readable = FD_ISSET(fd, &readfds);
			states[i].writable = FD_ISSET(fd, &writefds);
			states[i].error = FD_ISSET(fd, &exceptfds);
		}
	}
	return ret;
}

// A connection PtpOpen started may have come up since anything looked at it. A refused one reports
// DISCONNECT (pspautotests net/adhoc/poll).
void P2PTransport::PollCheckConnecting(int id, AdhocSocket *sock, const AdhocPollState &state) {
	if (!state.writable && !state.error) {
		return;
	}
	int connectError = 0;
	if (ptpReallyConnected(sock->data.ptp.id, &connectError)) {
		ptpSetEstablished(sock);
	} else if (!connectInProgress(connectError)) {
		if (RecreatePtpSocket(id) < 0) {
			WARN_LOG(Log::sceNet, "sceNetAdhocPollSocket[%i]: Failed to recreate the refused socket", id);
		}
		sock->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
	}
}

bool P2PTransport::PtpPeerClosed(AdhocSocket *sock) {
	char peek;
	return recv(sock->data.ptp.id, &peek, 1, MSG_PEEK | MSG_NOSIGNAL) == 0;
}

int P2PTransport::PdpDelete(int id) {
	auto sock = adhocSockets[id - 1];

	// Close Connection
	shutdown(sock->data.pdp.id, SD_RECEIVE);
	closesocket(sock->data.pdp.id);

	// Remove Port Forward from Router
	//sceNetPortClose("UDP", sock->lport);
	//g_PortManager.Remove(IP_PROTOCOL_UDP, isOriPort ? sock->lport : sock->lport + portOffset); // Let's not remove mapping in real-time as it could cause lags/disconnection when joining a room with slow routers

	// Free Memory
	free(sock);

	// Free Translation Slot
	adhocSockets[id - 1] = NULL;

	// Success
	return 0;
}

void P2PTransport::PtpUpdateRecvStat(AdhocSocket *sock) {
	// Set available bytes to be received
	sock->data.ptp.rcv_sb_cc = getAvailToRecv(sock->data.ptp.id);
	// It seems real PSP respecting the socket buffer size arg, so we may need to cap the value to the buffer size arg since we use larger buffer
	sock->data.ptp.rcv_sb_cc = std::min(sock->data.ptp.rcv_sb_cc, (u32_le)sock->buffer_size);
	// There might be a possibility for the data to be taken by the OS, thus FIONREAD returns 0, but can be Received
	if (sock->data.ptp.rcv_sb_cc == 0) {
		// Let's try to peek the data size
		int received = recv(sock->data.ptp.id, dummyPeekBuf64k, std::min((u32)dummyPeekBuf64kSize, sock->buffer_size), MSG_PEEK | MSG_NOSIGNAL);
		if (received > 0)
			sock->data.ptp.rcv_sb_cc = received;
	}
}

int P2PTransport::PtpOpen(const SceNetEtherAddr *saddr, int sport, const SceNetEtherAddr *daddr, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) {
	bool isClient = false;
	// Random Port required
	if (sport == 0) {
		isClient = true;
		//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
		sport = -static_cast<int>(portOffset);
	}

	// Create Infrastructure Socket (?)
	// Socket is remapped through adhocSockets
	int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Valid Socket produced
	if (tcpsocket > 0) {
		// Change socket MSS
		setSockMSS(tcpsocket, PSP_ADHOC_PTP_MSS);

		// Change socket buffer size to be consistent on all platforms.
		setSockBufferSize(tcpsocket, SO_SNDBUF, bufsize*5); //PSP_ADHOC_PTP_MSS
		setSockBufferSize(tcpsocket, SO_RCVBUF, bufsize*10); //PSP_ADHOC_PTP_MSS*10

		// Enable KeepAlive
		setSockKeepAlive(tcpsocket, true, rexmt_int / 1000000L, rexmt_cnt);

		// Ignore SIGPIPE when supported (ie. BSD/MacOS)
		setSockNoSIGPIPE(tcpsocket, 1);

		// Enable Port Re-use
		setSockReuseAddrPort(tcpsocket);

		// Apply Default Send Timeout Settings to Socket
		setSockTimeout(tcpsocket, SO_SNDTIMEO, rexmt_int);

		// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
		setSockNoDelay(tcpsocket, 1);

		// Binding Information for local Port
		struct sockaddr_in addr {};
		// addr.sin_len = sizeof(addr);
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		if (isLocalServer) {
			getLocalIp(&addr);
		}
		uint16_t requestedport = static_cast<int>(sport + static_cast<int>(portOffset));
		// Avoid getting random port due to port offset when original port wasn't 0 (ie. original_port + port_offset = 65536 = 0)
		if (requestedport == 0 && sport > 0)
			requestedport = 65535; // Hopefully it will be safe to default it to 65535 since there can't be more than one port that can bumped into 65536
		// Show a warning about privileged ports
		if (requestedport != 0 && requestedport < 1024) {
			WARN_LOG(Log::sceNet, "sceNetAdhocPtpOpen - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
		}
		addr.sin_port = htons(requestedport);

		// Bound Socket to local Port
		if (bind(tcpsocket, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
			// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
			socklen_t len = sizeof(addr);
			if (getsockname(tcpsocket, (struct sockaddr*)&addr, &len) == 0) {
				uint16_t boundport = ntohs(addr.sin_port);
				if (sport + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
					WARN_LOG(Log::sceNet, "sceNetAdhocPtpOpen - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", sport, requestedport, boundport, boundport - portOffset);
				sport = boundport - portOffset;
			}

			// Allocate Memory
			AdhocSocket* internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));

			// Allocated Memory
			if (internal != NULL) {
				// Find Free Translator ID
				// FIXME: We should probably use an increasing index instead of looking for an empty slot from beginning if we want to simulate a real socket id
				int i = 0;
				i = FindFreeAdhocSocketSlot();

				// Found Free Translator ID
				if (i < MAX_SOCKET) {
					// Clear Memory
					memset(internal, 0, sizeof(AdhocSocket));

					// Socket Type
					internal->type = SOCK_PTP;
					internal->retry_interval = rexmt_int;
					internal->retry_count = rexmt_cnt;
					internal->nonblocking = flag;
					internal->buffer_size = bufsize;
					internal->isClient = isClient;

					// Copy Infrastructure Socket ID
					internal->data.ptp.id = tcpsocket;

					// Copy Address & Port Information
					internal->data.ptp.laddr = *saddr;
					internal->data.ptp.paddr = *daddr;
					internal->data.ptp.lport = sport;
					internal->data.ptp.pport = dport;

					// Link PTP Socket
					adhocSockets[i] = internal;

					// Add Port Forward to Router. We may not even need to forward this local port, since PtpOpen usually have port 0 (any port) as source port and followed by PtpConnect (which mean acting as Client), right?
					//sceNetPortOpen("TCP", sport);
					if (!isClient)
						UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset);

					// Switch to non-blocking for futher usage
					changeBlockingMode(tcpsocket, 1);

					// PtpOpen starts connecting by itself, as on the PSP (see ptp_open_postoffice).
					// TODO: Need to handle ECONNREFUSED better on non-Windows, if there are games that never called PtpConnect and only relies on [blocking?] PtpOpen to get connected
					NetAdhocPtp_Connect(i + 1, rexmt_int, 1, false);

					// Workaround to give some time to get connected before returning from PtpOpen over high latency
					INFO_LOG(Log::sceNet, "sceNetAdhocPtpOpen - PSP Socket id: %i, Host Socket id: %i", i + 1, tcpsocket);

					// Return PTP Socket id
					if (g_Config.bForcedFirstConnect && internal->attemptCount == 1) {
						return hleDelayResult(hleLogDebug(Log::sceNet, i + 1), "delayed ptpopen", rexmt_int);
					} else {
						return hleLogDebug(Log::sceNet, i + 1);
					}
				}

				// Free Memory
				free(internal);
			}
		}
		else {
			ERROR_LOG(Log::sceNet, "Socket error (%i) when binding port %u", socket_errno, ntohs(addr.sin_port));
			auto n = GetI18NCategory(I18NCat::NETWORKING);
			g_OSD.Show(OSDType::MESSAGE_ERROR,
				std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 0.0f, "portbindfail");
		}

		// Close Socket
		closesocket(tcpsocket);

		// Port not available (exclusively in use?)
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_NOT_AVAIL, "port not available"); // SCE_NET_ADHOC_ERROR_PORT_IN_USE; // SCE_NET_ADHOC_ERROR_INVALID_PORT;
	}

	// Socket not created
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
}

int P2PTransport::PtpAccept(int id, SceNetEtherAddr *addr, uint16_t *port, int timeout, int flag) {
	auto& ptpsocket = adhocSockets[id - 1]->data.ptp;

	// Address Information
	struct sockaddr_in peeraddr;
	memset(&peeraddr, 0, sizeof(peeraddr));
	socklen_t peeraddrlen = sizeof(peeraddr);
	int error = 0;

	int newsocket = 0;
	// Check if listening socket is ready to accept
	newsocket = IsSocketReady(ptpsocket.id, true, false, &error);
	if (newsocket > 0) {
		// Accept Connection
		newsocket = accept(ptpsocket.id, (struct sockaddr*)&peeraddr, &peeraddrlen);
		error = socket_errno;
	}

	if (newsocket == 0 || (newsocket == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK))) {
		if (flag == 0) {
			// Simulate blocking behaviour with non-blocking socket
			u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
			return WaitBlockingAdhocSocket(threadSocketId, PTP_ACCEPT, id, nullptr, nullptr, timeout, addr, port, "ptp accept");
		}
		// Prevent spamming Debug Log with retries of non-bocking socket
		else {
			VERBOSE_LOG(Log::sceNet, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", id, error);
		}
	}

	// Accepted New Connection
	if (newsocket > 0) {
		int newid = AcceptPtpSocket(id, newsocket, peeraddr, addr, port);
		if (newid >= 0)
			return newid;
	}

	// Action would block
	if (flag)
		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

	// Timeout
	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_TIMEOUT, "timeout");
}

int P2PTransport::PtpClose(int id) {
	auto socket = adhocSockets[id - 1];

	// Close Connection
	shutdown(socket->data.ptp.id, SD_RECEIVE);
	closesocket(socket->data.ptp.id);

	// Remove Port Forward from Router
	//sceNetPortClose("TCP", socket->lport);
	//g_PortManager.Remove(IP_PROTOCOL_TCP, isOriPort ? socket->lport : socket->lport + portOffset); // Let's not remove mapping in real-time as it could cause lags/disconnection when joining a room with slow routers

	// Free Memory
	free(socket);

	// Free Reference
	adhocSockets[id - 1] = NULL;

	// Success
	return 0;
}

int P2PTransport::PtpListen(const SceNetEtherAddr *saddr, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, bool isClient, int flag) {
	// Create Infrastructure Socket (?)
	// Socket is remapped through adhocSockets
	int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Valid Socket produced
	if (tcpsocket > 0) {
		// Change socket MSS
		setSockMSS(tcpsocket, PSP_ADHOC_PTP_MSS);

		// Change socket buffer size to be consistent on all platforms.
		setSockBufferSize(tcpsocket, SO_SNDBUF, bufsize*5); //PSP_ADHOC_PTP_MSS
		setSockBufferSize(tcpsocket, SO_RCVBUF, bufsize*10); //PSP_ADHOC_PTP_MSS*10

		// Enable KeepAlive
		setSockKeepAlive(tcpsocket, true, rexmt_int / 1000000L, rexmt_cnt);

		// Ignore SIGPIPE when supported (ie. BSD/MacOS)
		setSockNoSIGPIPE(tcpsocket, 1);

		// Enable Port Re-use
		setSockReuseAddrPort(tcpsocket);

		// Apply Default Receive Timeout Settings to Socket
		setSockTimeout(tcpsocket, SO_RCVTIMEO, rexmt_int);

		// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
		setSockNoDelay(tcpsocket, 1);

		// Binding Information for local Port
		struct sockaddr_in addr {};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		if (isLocalServer) {
			getLocalIp(&addr);
		}
		uint16_t requestedport = static_cast<int>(sport + static_cast<int>(portOffset));
		// Avoid getting random port due to port offset when original port wasn't 0 (ie. original_port + port_offset = 65536 = 0)
		if (requestedport == 0 && sport > 0)
			requestedport = 65535; // Hopefully it will be safe to default it to 65535 since there can't be more than one port that can bumped into 65536
		// Show a warning about privileged ports
		if (requestedport != 0 && requestedport < 1024) {
			WARN_LOG(Log::sceNet, "sceNetAdhocPtpListen - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
		}
		addr.sin_port = htons(requestedport);

		int iResult = 0;
		// Bound Socket to local Port
		if ((iResult = bind(tcpsocket, (struct sockaddr*)&addr, sizeof(addr))) == 0) {
			// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
			socklen_t len = sizeof(addr);
			if (getsockname(tcpsocket, (struct sockaddr*)&addr, &len) == 0) {
				uint16_t boundport = ntohs(addr.sin_port);
				if (sport + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
					WARN_LOG(Log::sceNet, "sceNetAdhocPtpListen - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", sport, requestedport, boundport, boundport - portOffset);
				sport = boundport - portOffset;
			}
			// Switch into Listening Mode
			if ((iResult = listen(tcpsocket, backlog)) == 0) {
				// Allocate Memory
				AdhocSocket* internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));

				// Allocated Memory
				if (internal != NULL) {
					// Find Free Translator ID
					// FIXME: We should probably use an increasing index instead of looking for an empty slot from beginning if we want to simulate a real socket id
					int i = 0;
					i = FindFreeAdhocSocketSlot();

					// Found Free Translator ID
					if (i < MAX_SOCKET) {
						// Clear Memory
						memset(internal, 0, sizeof(AdhocSocket));

						// Socket Type
						internal->type = SOCK_PTP;
						internal->retry_interval = rexmt_int;
						internal->retry_count = rexmt_cnt;
						internal->nonblocking = flag;
						internal->buffer_size = bufsize;
						internal->isClient = isClient;

						// Copy Infrastructure Socket ID
						internal->data.ptp.id = tcpsocket;

						// Copy Address & Port Information
						internal->data.ptp.laddr = *saddr;
						internal->data.ptp.lport = sport;

						// Flag Socket as Listener
						internal->data.ptp.state = ADHOC_PTP_STATE_LISTEN;

						// Link PTP Socket
						adhocSockets[i] = internal;

						// Add Port Forward to Router
						//sceNetPortOpen("TCP", sport);
						UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset);

						// Switch to non-blocking for futher usage
						changeBlockingMode(tcpsocket, 1);

						// Return PTP Socket id
						return hleLogDebug(Log::sceNet, i + 1, "sceNetAdhocPtpListen - PSP Socket id: %i, Host Socket id: %i", i + 1, tcpsocket);
					}

					// Free Memory
					free(internal);
				}
			}
		}
		else {
			auto n = GetI18NCategory(I18NCat::NETWORKING);
			g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 0.0f, "portbindfail");
		}

		if (iResult == SOCKET_ERROR) {
			int error = socket_errno;
			ERROR_LOG(Log::sceNet, "sceNetAdhocPtpListen[%i]: Socket Error (%i)", sport, error);
		}

		// Close Socket
		closesocket(tcpsocket);

		// Port not available (exclusively in use?)
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_NOT_AVAIL, "port not available"); //SCE_NET_ADHOC_ERROR_PORT_IN_USE; // SCE_NET_ADHOC_ERROR_INVALID_PORT;
	}

	// Socket not available
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ID_NOT_AVAIL, "socket id not available");
}

int P2PTransport::PtpSend(int id, const char *data, int *len, int timeout, int flag) {
	auto socket = adhocSockets[id - 1];
	auto& ptpsocket = socket->data.ptp;

	// Acquire Network Lock
	// _acquireNetworkLock();

	// Send Data
	int sent = 0;
	int error = 0;
	sent = ptpSendToPeer(socket, data, *len, &error);

	// Free Network Lock
	// _freeNetworkLock();

	// Success
	if (sent > 0) {
		hleEatMicro(122);
		// Save Length
		*len = sent;

		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u\n", id, ptpsocket.lport, sent, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

		// Set to Established on successful Send when an attempt to Connect was initiated
		if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
			ptpSetEstablished(socket);

		// Return Success
		return 0;
	}

	// Non-Critical Error
	else if (sent == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (error == ENOTCONN || connectInProgress(error))))) {
		// Non-Blocking
		if (flag)
			return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

		// Simulate blocking behaviour with non-blocking socket
		u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
		return WaitBlockingAdhocSocket(threadSocketId, PTP_SEND, id, (void*)data, len, timeout, nullptr, nullptr, "ptp send");
	}

	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i:%u -> %s:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport, sent, error);

	// Change Socket State
	ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

	// Disconnected
	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_DISCONNECTED, "disconnected");
}

int P2PTransport::PtpRecv(int id, void *buf, int *len, int timeout, int flag) {
	auto socket = adhocSockets[id - 1];
	auto& ptpsocket = socket->data.ptp;

	// Acquire Network Lock
	// _acquireNetworkLock();

	// TODO: Use a different thread (similar to sceIo) for recvfrom, recv & accept to prevent blocking-socket from blocking emulation
	int received = 0;
	int error = 0;

	// Receive Data. POSIX: May received 0 bytes when the remote peer already closed the connection.
	received = ptpRecvFromPeer(socket, (char*)buf, std::max(0, *len), &error);

	if (received == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (error == ENOTCONN || connectInProgress(error))))) {
		if (flag == 0) {
			// Simulate blocking behaviour with non-blocking socket
			u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
			return WaitBlockingAdhocSocket(threadSocketId, PTP_RECV, id, buf, len, timeout, nullptr, nullptr, "ptp recv");
		}

		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");
	}

	// Free Network Lock
	// _freeNetworkLock();

	hleEatMicro(50);

	// Received Data
	if (received > 0) {
		// Save Length
		*len = received;

		// Update last recv timestamp, may cause disconnection not detected properly tho
		peerlock.lock();
		auto peer = findFriend(&ptpsocket.paddr);
		if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
		peerlock.unlock();

		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %s:%u\n", id, ptpsocket.lport, received, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

		// Set to Established on successful Recv when an attempt to Connect was initiated
		if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
			ptpSetEstablished(socket);

		// Return Success
		return 0;
	}

	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, received, error);

	if (*len == 0)
		return 0;

	// Change Socket State
	ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

	// Disconnected
	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_DISCONNECTED, "disconnected");
}

int P2PTransport::PtpFlush(int socketId) {
	// Get original Nagle algo value
	int n = getSockNoDelay(socketId);

	// Disable Nagle Algo to send immediately
	setSockNoDelay(socketId, 1);

	// Send Empty Data just to trigger Nagle on/off effect to flush the send buffer, Do we need to trigger this at all or is it automatically flushed?
	//changeBlockingMode(socket->id, nonblock);
	int ret = send(socketId, "", 0, MSG_NOSIGNAL);
	if (ret == SOCKET_ERROR) ret = socket_errno;
	//changeBlockingMode(socket->id, 1);

	// Restore/Enable Nagle Algo
	setSockNoDelay(socketId, n);

	return ret;
}

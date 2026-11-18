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

#include "libretro/NetpacketTransport.h"
#include "libretro/NetpacketAdhoc.h"
#include "Core/HLE/AdhocTransport.h"
#include "Core/HLE/NetAdhocCommon.h"
#include "Core/HLE/NetInetConstants.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/CoreTiming.h"
#include "ext/aemu_postoffice/client/postoffice_client.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Libretro {

class NetpacketTransport : public AdhocTransport {
public:
	explicit NetpacketTransport(NetpacketAdhoc &network) : network_(network) {}
	void CloseHostSocket(AdhocSocket *sock) override;
	void Poll() override;

	int PdpCreate(const SceNetEtherAddr *saddr, int port, int bufferSize, u32 flag, bool isClient, bool enetReliable) override;
	int PdpDelete(int id) override;
	int PdpSendTo(int id, const SceNetEtherAddr *mac, uint16_t dport, const sockaddr_in &target, const void *data, int len, int *error) override;
	bool PdpTakeDatagram(int id, SceNetEtherAddr *mac, uint16_t *port, std::vector<u8> &data) override;

	int PtpOpen(const SceNetEtherAddr *saddr, int sport, const SceNetEtherAddr *daddr, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) override;
	int PtpListen(const SceNetEtherAddr *saddr, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, bool isClient, int flag) override;
	int PtpClose(int id) override;
	int PtpSend(int id, const char *data, int *len, int timeout, int flag) override;
	int PtpSendNow(int id, const char *data, int len) override;
	int PtpRecv(int id, void *buf, int *len, int timeout, int flag) override;
	int PtpAccept(int id, SceNetEtherAddr *addr, uint16_t *port, int timeout, int flag) override;
	int DoBlockingPtpSend(AdhocSocketRequest &req, s64 &result) override;
	int DoBlockingPtpRecv(AdhocSocketRequest &req, s64 &result) override;
	int DoBlockingPtpAccept(AdhocSocketRequest &req, s64 &result) override;
	int PtpConnect(int id, const sockaddr_in &sin, int *error) override;
	int PtpConnectPoll(AdhocSocket *sock, int *error) override;
	bool PtpConnectionUp(AdhocSocket *sock) override;
	void PtpUpdateRecvStat(AdhocSocket *sock) override;
	int PtpFlush(int socketId) override;

	int PollWait(SceNetAdhocPollSd *sds, int count, int timeout, AdhocPollState *states) override;

	bool PdpReadyToSend(AdhocSocket *sock) override { return true; }
	int PtpConnectVerify(int id, sockaddr_in *sin, int ret, int *error) override { return ret; }
	bool PtpConnectPending(int error) override { return error == EAGAIN || error == EWOULDBLOCK; }
	bool PtpConnectedSinceOpen(AdhocSocket *sock) override { return PtpConnectionUp(sock); }
	void PollCheckConnecting(int id, AdhocSocket *sock, const AdhocPollState &state) override;
	bool PtpPeerClosed(AdhocSocket *sock) override;

private:
	struct Pending {
		void *handle;
		SceNetEtherAddr mac;
		int port;
	};
	struct Block {
		std::vector<u8> data;
		u64 readableAt;
	};
	struct Socket {
		void *handle = nullptr;
		int backlog = 1;
		std::deque<Pending> pending;
		std::deque<Block> blocks;
		size_t offset = 0, bytes = 0;
		u64 lastReadableAt = 0;
	};
	int Allocate(int type, const SceNetEtherAddr *mac, int port, int bufferSize);
	void FillBacklog(AdhocSocket *sock);
	void FillStream(AdhocSocket *sock);
	size_t Readable(const Socket &socket) const;
	int SendNow(int id, const char *data, int *len);
	int ReceiveNow(int id, void *data, int *len);
	int AcceptNow(int id, SceNetEtherAddr *mac, uint16_t *port);
	int Retry(AdhocSocketRequest &req, int value, s64 &result);
	NetpacketAdhoc &network_;
	std::map<AdhocSocket *, Socket> sockets_;
};

std::unique_ptr<AdhocTransport> CreateNetpacketTransport(NetpacketAdhoc &network) {
	return std::make_unique<NetpacketTransport>(network);
}

int NetpacketTransport::Allocate(int type, const SceNetEtherAddr *mac, int port, int bufferSize) {
	int slot = FindFreeAdhocSocketSlot();
	if (slot == MAX_SOCKET) {
		return ERROR_NET_NO_SPACE;
	}
	auto sock = (AdhocSocket *)calloc(1, sizeof(AdhocSocket));
	if (!sock) {
		return ERROR_NET_NO_SPACE;
	}
	sock->type = type;
	sock->buffer_size = bufferSize;
	// These identify PSP waits only; they are never host file descriptors.
	if (type == SOCK_PDP) {
		sock->data.pdp.id = 0x4000 + slot;
		sock->data.pdp.laddr = *mac;
		sock->data.pdp.lport = port;
	} else {
		sock->data.ptp.id = 0x4000 + slot;
		sock->data.ptp.laddr = *mac;
		sock->data.ptp.lport = port;
	}
	sockets_.emplace(sock, Socket{});
	adhocSockets[slot] = sock;
	return slot + 1;
}

void NetpacketTransport::CloseHostSocket(AdhocSocket *sock) {
	auto it = sockets_.find(sock);
	if (it == sockets_.end()) {
		return;
	}
	for (auto &pending : it->second.pending) {
		network_.Close(pending.handle);
	}
	network_.Close(it->second.handle);
	sockets_.erase(it);
}

int NetpacketTransport::PdpDelete(int id) {
	auto sock = adhocSockets[id - 1];
	CloseHostSocket(sock);
	free(sock);
	adhocSockets[id - 1] = nullptr;
	return 0;
}

int NetpacketTransport::PtpClose(int id) {
	return PdpDelete(id);
}

int NetpacketTransport::PdpCreate(const SceNetEtherAddr *saddr, int port, int bufferSize, u32 flag, bool isClient, bool enetReliable) {
	int id = Allocate(SOCK_PDP, saddr, port, bufferSize);
	if (id > 0) {
		adhocSockets[id - 1]->isClient = isClient;
		int status;
		sockets_.at(adhocSockets[id - 1]).handle = network_.DatagramCreate(port, &status);
		if (!sockets_.at(adhocSockets[id - 1]).handle) {
			PdpDelete(id);
			return ERROR_NET_NO_SPACE;
		}
	}
	return id;
}

int NetpacketTransport::PdpSendTo(int id, const SceNetEtherAddr *mac, uint16_t dport, const sockaddr_in &target, const void *data, int len, int *error) {
	if (offAdhocNetwork() && !isLocalMac(mac)) {
		return len;
	}
	auto &socket = sockets_.at(adhocSockets[id - 1]);
	int result = network_.DatagramSend(socket.handle, (const char *)mac, dport, (const char *)data, len);
	if (result == 0) {
		return len;
	}
	*error = EAGAIN;
	return SOCKET_ERROR;
}

bool NetpacketTransport::PdpTakeDatagram(int id, SceNetEtherAddr *mac, uint16_t *port, std::vector<u8> &data) {
	auto &socket = sockets_.at(adhocSockets[id - 1]);
	char buffer[AEMU_POSTOFFICE_PDP_BLOCK_MAX];
	int size = sizeof(buffer), senderPort = 0;
	if (network_.DatagramReceive(socket.handle, (char *)mac, &senderPort, buffer, &size) != 0) {
		return false;
	}
	if (offAdhocNetwork() && !isLocalMac(mac)) {
		return false;
	}
	*port = senderPort;
	data.assign(buffer, buffer + size);
	return true;
}

int NetpacketTransport::PtpOpen(const SceNetEtherAddr *saddr, int sport, const SceNetEtherAddr *daddr, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) {
	int id = Allocate(SOCK_PTP, saddr, sport ? sport : get_random_unused_ptp_port(), bufsize);
	if (id > 0) {
		auto sock = adhocSockets[id - 1];
		sock->retry_interval = rexmt_int;
		sock->retry_count = rexmt_cnt;
		sock->data.ptp.paddr = *daddr;
		sock->data.ptp.pport = dport;
		sock->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
		NetAdhocPtp_Connect(id, 0, 1, false);
	}
	return id;
}

int NetpacketTransport::PtpListen(const SceNetEtherAddr *saddr, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, bool isClient, int flag) {
	int id = Allocate(SOCK_PTP, saddr, sport ? sport : get_random_unused_ptp_port(), bufsize);
	if (id > 0) {
		auto sock = adhocSockets[id - 1];
		sock->retry_interval = rexmt_int;
		sock->retry_count = rexmt_cnt;
		sock->data.ptp.state = ADHOC_PTP_STATE_LISTEN;
		sock->isClient = isClient;
		auto &socket = sockets_.at(sock);
		socket.backlog = std::max(1, backlog);
		int status;
		socket.handle = network_.Listen(sock->data.ptp.lport, &status);
		if (!socket.handle) {
			PtpClose(id);
			return ERROR_NET_NO_SPACE;
		}
	}
	return id;
}

int NetpacketTransport::PtpConnect(int id, const sockaddr_in &sin, int *error) {
	auto sock = adhocSockets[id - 1];
	auto &socket = sockets_.at(sock);
	if (!socket.handle) {
		if (isLocalMac(&sock->data.ptp.paddr) && !isPTPPortInUse(sock->data.ptp.pport, true)) {
			*error = ECONNREFUSED;
			return SOCKET_ERROR;
		}
		socket.handle = network_.BeginConnect(sock->data.ptp.lport, (const char *)&sock->data.ptp.paddr, sock->data.ptp.pport);
	}
	int state = network_.ConnectionState(socket.handle);
	*error = state < 0 ? ECONNREFUSED : state == 0 ? EAGAIN : 0;
	return state == 1 ? 0 : SOCKET_ERROR;
}

int NetpacketTransport::PtpConnectPoll(AdhocSocket *sock, int *error) {
	int state = network_.ConnectionState(sockets_.at(sock).handle);
	*error = state < 0 ? ECONNREFUSED : state == 0 ? EAGAIN : 0;
	return state == 1 ? 1 : SOCKET_ERROR;
}

bool NetpacketTransport::PtpConnectionUp(AdhocSocket *sock) {
	return network_.ConnectionState(sockets_.at(sock).handle) == 1;
}

void NetpacketTransport::PollCheckConnecting(int id, AdhocSocket *sock, const AdhocPollState &state) {
	int status = network_.ConnectionState(sockets_.at(sock).handle);
	if (status == 1) {
		ptpSetEstablished(sock);
	} else if (status < 0) {
		ptpSetRefused(sock);
	}
}

bool NetpacketTransport::PtpPeerClosed(AdhocSocket *sock) {
	return network_.Dead(sockets_.at(sock).handle);
}

void NetpacketTransport::FillBacklog(AdhocSocket *sock) {
	auto &socket = sockets_.at(sock);
	while ((int)socket.pending.size() < socket.backlog) {
		Pending pending{};
		int status;
		pending.handle = network_.Accept(socket.handle, (char *)&pending.mac, &pending.port, &status);
		if (!pending.handle) {
			break;
		}
		socket.pending.push_back(pending);
	}
}

int NetpacketTransport::AcceptNow(int id, SceNetEtherAddr *mac, uint16_t *port) {
	auto listener = adhocSockets[id - 1];
	FillBacklog(listener);
	auto &socket = sockets_.at(listener);
	if (socket.pending.empty()) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}
	int accepted = Allocate(SOCK_PTP, &listener->data.ptp.laddr, listener->data.ptp.lport, listener->buffer_size);
	if (accepted < 0) {
		return accepted;
	}
	auto pending = socket.pending.front();
	socket.pending.pop_front();
	auto sock = adhocSockets[accepted - 1];
	sock->retry_interval = listener->retry_interval;
	sock->retry_count = listener->retry_count;
	sock->attemptCount = 1;
	sock->data.ptp.paddr = pending.mac;
	sock->data.ptp.pport = pending.port;
	sockets_.at(sock).handle = pending.handle;
	ptpSetEstablished(sock);
	if (mac) {
		*mac = pending.mac;
	}
	if (port) {
		*port = pending.port;
	}
	return accepted;
}

void NetpacketTransport::FillStream(AdhocSocket *sock) {
	auto &socket = sockets_.at(sock);
	while (socket.bytes < std::max(sock->buffer_size, (u32)1)) {
		int size = network_.StreamNextSize(socket.handle);
		if (size <= 0) {
			break;
		}
		Block block;
		block.data.resize(size);
		if (network_.StreamReceive(socket.handle, (char *)block.data.data(), &size) != 0) {
			break;
		}
		block.data.resize(size);
		// Match RelayTransport's receive pacing (net/adhoc/pair/ptpsegments, ptpmsg).
		block.readableAt = CoreTiming::GetGlobalTimeUsScaled();
		if (socket.lastReadableAt != 0) {
			block.readableAt = std::max(block.readableAt, socket.lastReadableAt + 950 + (u64)size * 13 / 20);
		}
		socket.lastReadableAt = block.readableAt;
		socket.bytes += size;
		socket.blocks.push_back(std::move(block));
	}
}

size_t NetpacketTransport::Readable(const Socket &socket) const {
	u64 now = CoreTiming::GetGlobalTimeUsScaled();
	size_t bytes = 0;
	for (const auto &block : socket.blocks) {
		if (block.readableAt > now) {
			break;
		}
		bytes += block.data.size();
	}
	return bytes ? bytes - socket.offset : 0;
}

void NetpacketTransport::Poll() {
	for (auto &entry : sockets_) {
		auto sock = entry.first;
		if (sock->type == SOCK_PTP) {
			if (sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN) {
				FillBacklog(sock);
			} else if (sock->data.ptp.state == ADHOC_PTP_STATE_ESTABLISHED) {
				FillStream(sock);
			}
		}
	}
}

int NetpacketTransport::SendNow(int id, const char *data, int *len) {
	if (offAdhocNetwork()) {
		return 0;
	}
	auto sock = adhocSockets[id - 1];
	if (ptpPeerGone(sock)) {
		return SCE_NET_ADHOC_ERROR_DISCONNECTED;
	}
	int size = std::min(*len, 50 * 1024);
	int result = network_.StreamSend(sockets_.at(sock).handle, data, size);
	if (result == 0) {
		*len = size;
		ptpNoteSend(sock, size);
		return 0;
	}
	return result == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK ? SCE_NET_ADHOC_ERROR_WOULD_BLOCK : SCE_NET_ADHOC_ERROR_DISCONNECTED;
}

int NetpacketTransport::ReceiveNow(int id, void *data, int *len) {
	if (offAdhocNetwork()) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}
	auto sock = adhocSockets[id - 1];
	if (ptpPeerGone(sock)) {
		return SCE_NET_ADHOC_ERROR_DISCONNECTED;
	}
	FillStream(sock);
	auto &socket = sockets_.at(sock);
	size_t readable = Readable(socket);
	if (!readable) {
		return socket.bytes == 0 && network_.Dead(socket.handle) ? SCE_NET_ADHOC_ERROR_DISCONNECTED : SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}
	size_t want = std::min((size_t)std::max(0, *len), readable), copied = 0;
	while (copied < want) {
		auto &block = socket.blocks.front();
		size_t n = std::min(want - copied, block.data.size() - socket.offset);
		memcpy((u8 *)data + copied, block.data.data() + socket.offset, n);
		copied += n;
		socket.offset += n;
		socket.bytes -= n;
		if (socket.offset == block.data.size()) {
			socket.blocks.pop_front();
			socket.offset = 0;
		}
	}
	*len = (int)copied;
	return 0;
}

int NetpacketTransport::PtpSendNow(int id, const char *data, int len) {
	int result = SendNow(id, data, &len);
	return result == 0 ? len : result == SCE_NET_ADHOC_ERROR_WOULD_BLOCK ? 0 : -1;
}

int NetpacketTransport::PtpSend(int id, const char *data, int *len, int timeout, int flag) {
	int result = SendNow(id, data, len);
	if (result == 0) {
		hleEatMicro(AdhocPtpSendUs(*len));
	} else if (result == SCE_NET_ADHOC_ERROR_DISCONNECTED) {
		adhocSockets[id - 1]->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
	} else if (result == SCE_NET_ADHOC_ERROR_WOULD_BLOCK && !flag) {
		u64 waitId = (u64)__KernelGetCurThread() << 32 | adhocSockets[id - 1]->data.ptp.id;
		return WaitBlockingAdhocSocket(waitId, PTP_SEND, id, (void *)data, len, timeout, nullptr, nullptr, "netpacket send");
	}
	return result;
}

int NetpacketTransport::PtpRecv(int id, void *data, int *len, int timeout, int flag) {
	int result = ReceiveNow(id, data, len);
	if (result == 0) {
		hleEatMicro(AdhocPtpRecvUs(*len));
	} else if (result == SCE_NET_ADHOC_ERROR_DISCONNECTED) {
		adhocSockets[id - 1]->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
	} else if (result == SCE_NET_ADHOC_ERROR_WOULD_BLOCK && !flag) {
		u64 waitId = (u64)__KernelGetCurThread() << 32 | adhocSockets[id - 1]->data.ptp.id;
		return WaitBlockingAdhocSocket(waitId, PTP_RECV, id, data, len, timeout, nullptr, nullptr, "netpacket recv");
	}
	return result;
}

int NetpacketTransport::PtpAccept(int id, SceNetEtherAddr *addr, uint16_t *port, int timeout, int flag) {
	int result = AcceptNow(id, addr, port);
	if (result == SCE_NET_ADHOC_ERROR_WOULD_BLOCK && !flag) {
		u64 waitId = (u64)__KernelGetCurThread() << 32 | adhocSockets[id - 1]->data.ptp.id;
		return WaitBlockingAdhocSocket(waitId, PTP_ACCEPT, id, nullptr, nullptr, timeout, addr, port, "netpacket accept");
	}
	return result;
}

int NetpacketTransport::Retry(AdhocSocketRequest &req, int value, s64 &result) {
	if (value == SCE_NET_ADHOC_ERROR_WOULD_BLOCK) {
		if (!req.timeout || CoreTiming::GetGlobalTimeUs() - req.startTime <= req.timeout) {
			return -1;
		}
		value = SCE_NET_ADHOC_ERROR_TIMEOUT;
	} else if (value == SCE_NET_ADHOC_ERROR_DISCONNECTED) {
		adhocSockets[req.id - 1]->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
	}
	result = value;
	return 0;
}

int NetpacketTransport::DoBlockingPtpSend(AdhocSocketRequest &req, s64 &result) {
	int value = SendNow(req.id, (const char *)req.buffer, req.length);
	if (value == 0 && adhocSockets[req.id - 1]->data.ptp.state == ADHOC_PTP_STATE_SYN_SENT) {
		ptpSetEstablished(adhocSockets[req.id - 1]);
	}
	return Retry(req, value, result);
}

int NetpacketTransport::DoBlockingPtpRecv(AdhocSocketRequest &req, s64 &result) {
	return Retry(req, ReceiveNow(req.id, req.buffer, req.length), result);
}

int NetpacketTransport::DoBlockingPtpAccept(AdhocSocketRequest &req, s64 &result) {
	return Retry(req, AcceptNow(req.id, req.remoteMAC, req.remotePort), result);
}

void NetpacketTransport::PtpUpdateRecvStat(AdhocSocket *sock) {
	int bytes = 0;
	if (sock->data.ptp.state != ADHOC_PTP_STATE_LISTEN) {
		FillStream(sock);
		bytes = (int)Readable(sockets_.at(sock));
	}
	sock->data.ptp.rcv_sb_cc = bytes;
}

int NetpacketTransport::PtpFlush(int socketId) {
	// NetpacketPoll drains the outgoing reliable packets on the frontend thread.
	return 0;
}

int NetpacketTransport::PollWait(SceNetAdhocPollSd *sds, int count, int timeout, AdhocPollState *states) {
	for (int i = 0; i < count; i++) {
		int id = sds[i].id;
		if (id <= 0 || id > MAX_SOCKET || !adhocSockets[id - 1]) {
			continue;
		}
		auto sock = adhocSockets[id - 1];
		auto &socket = sockets_.at(sock);
		if (sock->type == SOCK_PDP) {
			states[i].readable = network_.DatagramSize(socket.handle, false) > 0;
			states[i].writable = network_.Writable(socket.handle) > 0;
		} else if (sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN) {
			FillBacklog(sock);
			states[i].readable = !socket.pending.empty();
		} else if (sock->data.ptp.state == ADHOC_PTP_STATE_ESTABLISHED) {
			FillStream(sock);
			states[i].readable = Readable(socket) > 0 || network_.Dead(socket.handle);
			states[i].writable = network_.Writable(socket.handle) > 0;
		} else {
			int status = network_.ConnectionState(socket.handle);
			states[i].writable = status == 1;
			states[i].error = status < 0;
		}
	}
	return 0;
}

}

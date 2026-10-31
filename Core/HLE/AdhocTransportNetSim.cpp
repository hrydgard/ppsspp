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

// The network simulation transport (see NetSimTransport in AdhocTransport.h). Times are emulated, so a
// delay is the same at any speed.
// - PDP: a datagram the transport inside delivers is held until its arrival, then handed over, and goes
//   into the socket's queue, where it may not fit, as if it had just arrived.
// - PTP: a send is accepted at once and held until its departure, then sent from Poll(). With the same
//   options on every instance that gives the same round trip as delaying the receive. Nothing is lost
//   on a connection: a "lost" send goes out one retransmit interval (from PtpOpen) later instead.
// Jitter delays without reordering. The random numbers are seeded per instance when ad hoc starts.

#include <algorithm>
#include <cerrno>

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/AdhocTransport.h"

NetSimTransport::NetSimTransport(std::unique_ptr<AdhocTransport> inner, uint32_t seed) : inner_(std::move(inner)), random_(seed) {}

bool NetSimTransport::Enabled() {
	return g_Config.iAdhocSimDelayMs > 0 || g_Config.iAdhocSimJitterMs > 0 || g_Config.iAdhocSimLossPercent > 0;
}

u64 NetSimTransport::Delay() {
	u64 delayUs = (u64)std::max(g_Config.iAdhocSimDelayMs, 0) * 1000;
	if (g_Config.iAdhocSimJitterMs > 0) {
		delayUs += random_() % ((u64)g_Config.iAdhocSimJitterMs * 1000 + 1);
	}
	return delayUs;
}

bool NetSimTransport::Lost() {
	return (int)(random_() % 100) < g_Config.iAdhocSimLossPercent;
}

u32 NetSimTransport::HeldBytes(const AdhocSocket *sock) const {
	u32 bytes = 0;
	auto it = sends_.find(sock);
	if (it != sends_.end()) {
		for (const HeldSend &send : it->second) {
			bytes += (u32)send.data.size();
		}
	}
	return bytes;
}

void NetSimTransport::CloseHostSocket(AdhocSocket *sock) {
	datagrams_.erase(sock);
	sends_.erase(sock);
	inner_->CloseHostSocket(sock);
}

void NetSimTransport::Poll() {
	inner_->Poll();
	for (int i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] != NULL && adhocSockets[i]->type == SOCK_PTP) {
			SendHeld(i + 1, false);
		}
	}
}

int NetSimTransport::PdpDelete(int id) {
	datagrams_.erase(adhocSockets[id - 1]);
	return inner_->PdpDelete(id);
}

bool NetSimTransport::PdpTakeDatagram(int id, SceNetEtherAddr *mac, uint16_t *port, std::vector<u8> &data) {
	std::deque<HeldDatagram> &held = datagrams_[adhocSockets[id - 1]];
	const u64 now = CoreTiming::GetGlobalTimeUs();
	HeldDatagram datagram{};
	while (inner_->PdpTakeDatagram(id, &datagram.mac, &datagram.port, datagram.data)) {
		if (Lost()) {
			continue;
		}
		datagram.arrivalUs = now + Delay();
		if (!held.empty()) {
			datagram.arrivalUs = std::max(datagram.arrivalUs, held.back().arrivalUs);
		}
		held.push_back(datagram);
	}
	if (held.empty() || held.front().arrivalUs > now) {
		return false;
	}
	*mac = held.front().mac;
	*port = held.front().port;
	data = std::move(held.front().data);
	held.pop_front();
	return true;
}

int NetSimTransport::PtpClose(int id) {
	// What's held goes out first, as far as it can.
	SendHeld(id, true);
	sends_.erase(adhocSockets[id - 1]);
	return inner_->PtpClose(id);
}

int NetSimTransport::PtpSend(int id, const char *data, int *len, int timeout, int flag) {
	AdhocSocket *sock = adhocSockets[id - 1];
	std::deque<HeldSend> &held = sends_[sock];
	u64 delayUs = Delay();
	if (Lost()) {
		delayUs += sock->retry_interval;
	}
	u64 departureUs = CoreTiming::GetGlobalTimeUs() + delayUs;
	if (!held.empty()) {
		departureUs = std::max(departureUs, held.back().departureUs);
	}
	held.push_back({ departureUs, std::vector<u8>(data, data + *len) });
	hleEatMicro(AdhocPtpSendUs(*len));
	return 0;
}

// Sends what's due, or with all, everything that can go now.
void NetSimTransport::SendHeld(int id, bool all) {
	auto it = sends_.find(adhocSockets[id - 1]);
	if (it == sends_.end()) {
		return;
	}
	const u64 now = CoreTiming::GetGlobalTimeUs();
	std::deque<HeldSend> &held = it->second;
	while (!held.empty() && (all || held.front().departureUs <= now)) {
		std::vector<u8> &data = held.front().data;
		int sent = inner_->PtpSendNow(id, (const char *)data.data(), (int)data.size());
		if (sent < 0) {
			// The connection is gone: DISCONNECTED from the game's next call, as when PtpSend fails.
			adhocSockets[id - 1]->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
			held.clear();
			return;
		}
		if (sent == 0) {
			return;
		}
		data.erase(data.begin(), data.begin() + sent);
		if (data.empty()) {
			held.pop_front();
		}
	}
}

void NetSimTransport::PtpUpdateRecvStat(AdhocSocket *sock) {
	inner_->PtpUpdateRecvStat(sock);
	// Held sends haven't gone yet.
	sock->data.ptp.snd_sb_cc = HeldBytes(sock);
}

int NetSimTransport::PtpFlush(int socketId) {
	// socketId is the transport's own ID for the socket.
	for (int i = 0; i < MAX_SOCKET; i++) {
		AdhocSocket *sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PTP && sock->data.ptp.id == socketId && HeldBytes(sock) != 0) {
			return EAGAIN;
		}
	}
	return inner_->PtpFlush(socketId);
}

// Copyright (c) 2012- PPSSPP Project.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "libretro/LibretroNetpacket.h"
#include "libretro/NetpacketAdhoc.h"
#include "Core/HLE/NetAdhocTransport.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include <cstring>

namespace Libretro {
namespace {
NetpacketAdhoc transport;
bool enabled = false;
// These are only accessed from frontend callbacks, never the CPU/network threads.
retro_netpacket_send_t sendPacket = nullptr;
uint16_t localId = 0;
struct NetworkSettings {
	bool wlan, server, upnp, udp;
	int relay, port;
	std::string hostname;
};
NetworkSettings previousSettings;

void RETRO_CALLCONV Start(uint16_t id, retro_netpacket_send_t send, retro_netpacket_poll_receive_t) {
	localId = id;
	transport.Start(id);
	sendPacket = send;
}
void RETRO_CALLCONV Receive(const void *data, size_t size, uint16_t from) {
	transport.Receive(data, size, from);
}
void RETRO_CALLCONV Stop() {
	NetpacketStop();
}
void RETRO_CALLCONV Poll() {
	NetpacketPoll();
}
bool RETRO_CALLCONV Connected(uint16_t id) {
	return transport.Connected(id);
}
void RETRO_CALLCONV Disconnected(uint16_t id) {
	transport.Disconnected(id);
}
const retro_netpacket_callback callbacks = {
	Start, Receive, Stop, Poll, Connected, Disconnected, "ppsspp-adhoc-experimental-1",
};
}

void NetpacketConfig() {
	if (!enabled)
		return;
	g_Config.bEnableWlan = true;
	g_Config.bEnableAdhocServer = false;
	g_Config.bEnableUPnP = false;
	g_Config.bAdhocUseUdp = false;
	g_Config.iAdhocServerRelayMode = (int)AdhocServerRelayMode::AlwaysOn;
	g_Config.iPortOffset = 0;
	// The HLE still resolves this address; the injected transport opens no sockets.
	g_Config.sProAdhocServer = "127.0.0.1";
}

bool NetpacketEnabled() {
	return enabled;
}

bool NetpacketInit(retro_environment_t environment) {
	// Also clean up an earlier failed content load, for which a frontend may
	// legitimately omit retro_unload_game(). No PSP threads run at this point.
	NetpacketShutdown();
	retro_variable variable{"ppsspp_retroarch_netplay", nullptr};
	enabled = environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value && !strcmp(variable.value, "enabled");
	if (!enabled) {
		environment(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, nullptr);
		return true;
	}
	if (!environment(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, (void *)&callbacks)) {
		enabled = false;
		return false;
	}
	previousSettings = {g_Config.bEnableWlan, g_Config.bEnableAdhocServer,
		g_Config.bEnableUPnP, g_Config.bAdhocUseUdp, g_Config.iAdhocServerRelayMode,
		g_Config.iPortOffset, g_Config.sProAdhocServer};
	auto &t = g_netAdhocTransport;
	t.init = [](bool) { return 0; };
	t.pump = []() { return 1; };
	t.deinit = []() {};
	t.getLocalMac = [](uint8_t *mac) { transport.LocalMac(mac); };
	t.ctlCreate = [](const adhocctl_addr_v4 *, const adhocctl_addr_v4 *, const char *game, const char *nick, const char *, bool, bool (*)(void *), void *) { return transport.ControlCreate(game, nick); };
	t.ctlDestroy = [](void *h) { transport.ControlDestroy(h); };
	t.ctlEvent = [](void *h, adhocctl_event *e) { transport.ControlEvent(h, e); };
	t.ctlPing = [](void *h) { return transport.ControlPing(h); };
	t.ctlConnect = [](void *h, const char *group) { return transport.ControlJoin(h, group); };
	t.ctlDisconnect = [](void *h) { return transport.ControlLeave(h); };
	t.ctlScan = [](void *h) { return transport.ControlScan(h); };
	t.ctlChat = [](void *h, const char *message) { return transport.ControlChat(h, message); };
	t.pdpCreate = [](const aemu_postoffice_sock_addr *, const char *, int port, bool, bool, bool (*)(void *), void *, int *state) { return transport.DatagramCreate(port, state); };
	t.pdpDelete = [](void *h) { transport.Close(h); };
	t.pdpSend = [](void *h, const char *mac, int port, const char *data, int size, bool) { return transport.DatagramSend(h, mac, port, data, size); };
	t.pdpRecv = [](void *h, char *mac, int *port, char *data, int *size, bool) { return transport.DatagramReceive(h, mac, port, data, size); };
	t.pdpPeek = [](void *h) { return transport.DatagramSize(h, false); };
	t.pdpBuffered = [](void *h) { return transport.DatagramSize(h, true); };
	t.pdpWritable = [](void *h) { return transport.Writable(h); };
	t.pdpDead = [](void *h) { return transport.Dead(h); };
	t.ptpListen = [](const aemu_postoffice_sock_addr *, const char *, int port, bool, bool (*)(void *), void *, int *state) { return transport.Listen(port, state); };
	t.ptpAccept = [](void *h, char *mac, int *port, bool, bool (*)(void *), void *, int *state) { return transport.Accept(h, mac, port, state); };
	t.ptpConnect = [](const aemu_postoffice_sock_addr *, const char *, int source, const char *mac, int port, bool, bool (*abort)(void *), void *arg, int *state) { return transport.Connect(source, mac, port, abort, arg, state); };
	t.ptpSend = [](void *h, const char *data, int size, bool) { return transport.StreamSend(h, data, size); };
	t.ptpRecv = [](void *h, char *data, int *size, bool) { return transport.StreamReceive(h, data, size); };
	t.ptpClose = [](void *h) { transport.Close(h); };
	t.ptpListenClose = [](void *h) { transport.Close(h); };
	t.ptpPeek = [](void *h) { return transport.StreamSize(h); };
	t.ptpWritable = [](void *h) { return transport.Writable(h); };
	t.ptpPending = [](void *h) { return transport.Pending(h); };
	t.ptpDead = [](void *h) { return transport.Dead(h); };
	NetpacketConfig();
	return true;
}

void NetpacketPoll() {
	if (!sendPacket)
		return;
	// Bounded work per frame. Host-local requests use exactly the same wire parser.
	for (int round = 0; round < 4; round++) {
		auto packets = transport.TakePackets();
		if (packets.empty())
			break;
		for (const auto &packet : packets) {
			if (!sendPacket)
				return;
			if (packet.peer == localId)
				transport.Receive(packet.data.data(), packet.data.size(), localId);
			else
				sendPacket(RETRO_NETPACKET_RELIABLE, packet.data.data(), packet.data.size(), packet.peer);
		}
	}
	if (sendPacket)
		sendPacket(RETRO_NETPACKET_FLUSH_HINT, nullptr, 0, RETRO_NETPACKET_BROADCAST);
}
void NetpacketStop() {
	sendPacket = nullptr;
	transport.Stop();
}
void NetpacketShutdown() {
	NetpacketStop();
	// Called only after PSP_Shutdown, when handles have been closed and threads joined.
	if (enabled) {
		g_netAdhocTransport = NetAdhocTransport{};
		g_Config.bEnableWlan = previousSettings.wlan;
		g_Config.bEnableAdhocServer = previousSettings.server;
		g_Config.bEnableUPnP = previousSettings.upnp;
		g_Config.bAdhocUseUdp = previousSettings.udp;
		g_Config.iAdhocServerRelayMode = previousSettings.relay;
		g_Config.iPortOffset = previousSettings.port;
		g_Config.sProAdhocServer = previousSettings.hostname;
	}
	enabled = false;
}
}

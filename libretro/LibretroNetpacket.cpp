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

#include "libretro/LibretroNetpacket.h"
#include "libretro/NetpacketAdhoc.h"
#include "Core/HLE/AdhocTransport.h"
#include "libretro/NetpacketTransport.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include <cstring>
#include <chrono>
#include <thread>

namespace Libretro {
namespace {
NetpacketAdhoc transport;
class NetpacketControl : public AdhocControlTransport {
public:
	bool IsExternal() const override { return true; }
	std::unique_ptr<AdhocTransport> CreateDataTransport() override { return CreateNetpacketTransport(transport); }
	bool GetLocalMac(u8 mac[6]) override {
		transport.LocalMac(mac);
		return true;
	}
	void *Login(const adhocctl_addr_v4 *, const adhocctl_addr_v4 *, const char *game, const char *nick, const char *, bool, bool (*abort)(void *), void *arg) override {
		while (!transport.Active()) {
			if (abort && abort(arg)) {
				return nullptr;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return transport.ControlCreate(game, nick);
	}
	void Logout(void *handle) override { transport.ControlDestroy(handle); }
	void GetEvent(void *handle, adhocctl_event *event) override { transport.ControlEvent(handle, event); }
	adhocctl_call_status Ping(void *handle) override { return transport.ControlPing(handle); }
	adhocctl_call_status Connect(void *handle, const char *group) override { return transport.ControlJoin(handle, group); }
	adhocctl_call_status Disconnect(void *handle) override { return transport.ControlLeave(handle); }
	adhocctl_call_status Scan(void *handle) override { return transport.ControlScan(handle); }
	adhocctl_call_status Chat(void *handle, const char *message) override { return transport.ControlChat(handle, message); }
};
NetpacketControl controlProvider;
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
	Start, Receive, Stop, Poll, Connected, Disconnected, "ppsspp-adhoc-experimental-2",
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
	// Keep game-specific server settings out of this session. Discovery is handled by the frontend.
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
	SetAdhocControlTransport(&controlProvider);
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
		SetAdhocControlTransport(nullptr);
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

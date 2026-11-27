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

// Loads the real core and runs pspautotests/tests/net/adhoc/self.prx against
// a synthetic second PSP. This verifies the HLE -> frontend packet boundary.
#include "libretro.h"
#include "libretro/NetpacketAdhoc.h"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/socket.h>
#include <cerrno>
#endif

static retro_netpacket_callback callbacks{};
static Libretro::NetpacketAdhoc peer;
static std::string systemDir, saveDir;
static bool quit = false, enabled = true, supported = true;
static std::atomic<bool> gameExited{false};
static unsigned frames = 0, receivedPackets = 0;
static std::thread::id frontendThread;
static void Check(bool value, const char *message) {
	if (!value) {
		fprintf(stderr, "FAIL: %s\n", message);
		abort();
	}
}
static void RETRO_CALLCONV Log(enum retro_log_level, const char *format, ...) {
	va_list args;
	va_start(args, format);
	va_list copy;
	va_copy(copy, args);
	char message[4096];
	vsnprintf(message, sizeof(message), format, copy);
	va_end(copy);
	if (strstr(message, "[sceKernel] sceKernelExitGame")) {
		gameExited = true;
	}
	vfprintf(stderr, format, args);
	va_end(args);
}
static bool RETRO_CALLCONV Environment(unsigned cmd, void *data) {
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		static_cast<retro_log_callback *>(data)->log = Log;
		return true;
	case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
		if (!supported)
			return false;
		callbacks = data ? *static_cast<retro_netpacket_callback *>(data) : retro_netpacket_callback{};
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		auto variable = static_cast<retro_variable *>(data);
		if (!strcmp(variable->key, "ppsspp_retroarch_netplay"))
			variable->value = enabled ? "enabled" : "disabled";
		else if (!strcmp(variable->key, "ppsspp_cpu_core"))
			variable->value = "Interpreter";
		else if (!strcmp(variable->key, "ppsspp_backend"))
			variable->value = "none";
		else
			return false;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*static_cast<bool *>(data) = false;
		return true;
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*static_cast<const char **>(data) = systemDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*static_cast<const char **>(data) = saveDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		return true;
	case RETRO_ENVIRONMENT_SHUTDOWN:
		quit = true;
		return true;
	default:
		return false;
	}
}
static void RETRO_CALLCONV Video(const void *, unsigned, unsigned, size_t) { frames++; }
static size_t RETRO_CALLCONV Audio(const int16_t *, size_t frames) { return frames; }
static void RETRO_CALLCONV PollInput() {}
static int16_t RETRO_CALLCONV Input(unsigned, unsigned, unsigned, unsigned) { return 0; }
static void RETRO_CALLCONV Send(int, const void *data, size_t size, uint16_t id) {
	Check(std::this_thread::get_id() == frontendThread, "frontend send called on worker thread");
	if (size) {
		Check(id == 1, "unexpected destination");
		peer.Receive(data, size, 0);
		receivedPackets++;
	}
}
static void RETRO_CALLCONV Receive() {}

#ifndef _WIN32
// An inherited datagram socket joins two separate core processes, driven by core_pair.py.
static int pairSocket = -1;
static uint16_t pairId = 0;
static void RETRO_CALLCONV PairSend(int, const void *data, size_t size, uint16_t id) {
	Check(std::this_thread::get_id() == frontendThread, "pair send on frontend thread");
	if (size) {
		Check(id == (pairId ^ 1), "pair destination");
		Check(send(pairSocket, data, size, 0) == (ssize_t)size, "pair packet sent");
	}
}
static void RETRO_CALLCONV PairReceive() {
	char packet[65536];
	for (;;) {
		ssize_t size = recv(pairSocket, packet, sizeof(packet), MSG_DONTWAIT);
		if (size < 0) {
			Check(errno == EAGAIN || errno == EWOULDBLOCK, "pair receive");
			break;
		}
		callbacks.receive(packet, size, pairId ^ 1);
		receivedPackets++;
	}
}
#endif

int main(int argc, char **argv) {
	Check(argc >= 5, "usage: NetpacketCoreSmoke core self.prx system-directory save-directory [disabled|unsupported]");
	frontendThread = std::this_thread::get_id();
	systemDir = argv[3];
	saveDir = argv[4];
	if (argc > 5) {
		enabled = strcmp(argv[5], "disabled") != 0;
		supported = strcmp(argv[5], "unsupported") != 0;
	}
#ifdef _WIN32
	WSADATA wsa;
	Check(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "Winsock initialized");
	auto library = LoadLibraryA(argv[1]);
	auto symbol = [&](const char *name) { return GetProcAddress(library, name); };
#else
	auto library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (!library)
		fprintf(stderr, "%s\n", dlerror());
	auto symbol = [&](const char *name) { return dlsym(library, name); };
#endif
	Check(library != nullptr, "core loads with all dependencies");
#define LOAD(name) auto name = reinterpret_cast<decltype(&::name)>(symbol(#name)); Check(name != nullptr, #name)
	LOAD(retro_set_environment); LOAD(retro_set_video_refresh); LOAD(retro_set_audio_sample_batch);
	LOAD(retro_set_input_poll); LOAD(retro_set_input_state); LOAD(retro_init); LOAD(retro_deinit);
	LOAD(retro_load_game); LOAD(retro_unload_game); LOAD(retro_run); LOAD(retro_serialize_size);
	retro_set_environment(Environment);
	retro_set_video_refresh(Video);
	retro_set_audio_sample_batch(Audio);
	retro_set_input_poll(PollInput);
	retro_set_input_state(Input);
	retro_init();
	retro_game_info game{argv[2], nullptr, 0, nullptr};
	bool loaded = retro_load_game(&game);
	if (!supported) {
		Check(!loaded && !callbacks.start, "unsupported frontend rejected");
		retro_deinit();
		puts("PASS unsupported frontend rejected cleanly");
		return 0;
	}
	Check(loaded, "content loads");
#ifndef _WIN32
	if (argc == 8 && !strcmp(argv[5], "pair")) {
		pairId = (uint16_t)atoi(argv[6]);
		pairSocket = atoi(argv[7]);
		Check(pairId <= 1 && pairSocket >= 0 && callbacks.start, "pair setup");
		callbacks.start(pairId, PairSend, PairReceive);
		if (pairId == 0) {
			Check(callbacks.connected(1), "pair client accepted");
		}
		auto start = std::chrono::steady_clock::now();
		for (int frame = 0; frame < 1200 && !quit && !gameExited; frame++) {
			PairReceive();
			callbacks.poll();
			retro_run();
			std::this_thread::sleep_until(start + std::chrono::microseconds((frame + 1) * 1000000LL / 60));
		}
		// Deliver the PSP's final leave/logout messages before stopping the session.
		callbacks.poll();
		callbacks.stop();
		retro_unload_game();
		retro_deinit();
		Check(receivedPackets > 0 && frames > 0, "pair exchanged packets and produced frames");
		printf("PASS pair frontend, id=%u frames=%u packets=%u\n", pairId, frames, receivedPackets);
		return 0;
	}
#endif
	void *control = nullptr, *datagram = nullptr;
	if (enabled) {
		Check(callbacks.start && callbacks.receive && callbacks.poll, "Netpacket registered");
		Check(retro_serialize_size() == 0, "savestates disabled in packet mode");
		callbacks.start(0, Send, Receive);
		Check(callbacks.connected(1), "second player accepted");
		peer.Start(1);
		char nick[128] = "Smoke test peer";
		control = peer.ControlCreate("TEST00001", nick);
		Check(control, "peer control created");
		Check(peer.ControlJoin(control, "SELFTEST") == ADHOCCTL_CALL_SUCCESS, "peer joined group");
		int state;
		datagram = peer.DatagramCreate(0x4568, &state);
		Check(datagram && state == 0, "peer datagram opened");
	} else {
		Check(!callbacks.start, "disabled option does not register Netpacket");
	}
	bool foundPeer = false, foundData = false;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while (!quit && std::chrono::steady_clock::now() < deadline) {
		if (enabled) {
			for (auto &packet : peer.TakePackets())
				callbacks.receive(packet.data.data(), packet.data.size(), 1);
			callbacks.poll();
			adhocctl_event event;
			peer.ControlEvent(control, &event);
			if (event.type == ADHOCCTL_CLIENT_EVENT_CONNECT)
				foundPeer = true;
			char mac[6], buffer[64];
			int port, size = sizeof(buffer);
			if (peer.DatagramReceive(datagram, mac, &port, buffer, &size) == 0 && size == 10 && !memcmp(buffer, "broadcast", 10))
				foundData = true;
		}
		retro_run();
		if ((foundPeer && foundData && frames > 120) || (!enabled && frames > 120))
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	if (enabled) {
		callbacks.disconnected(1);
		callbacks.stop();
	}
	retro_unload_game();
	if (enabled) {
		// Reload without deinitializing the core: both the interface and HLE
		// transport must return to their default state when the option is off.
		enabled = false;
		Check(retro_load_game(&game), "reload with option disabled");
		Check(!callbacks.start, "packet interface cleared on disabled reload");
		for (int i = 0; i < 120; i++)
			retro_run();
		retro_unload_game();
		enabled = true;
	}
	retro_deinit();
	Check(frames > 0, "emulation produced video frames");
	if (enabled)
		Check(foundPeer && foundData && receivedPackets > 0, "real PSP ad hoc discovery and broadcast reached second peer");
	printf("PASS core load/boot/unload, %u video frames, %u packets, peer=%d datagram=%d enabled=%d\n", frames, receivedPackets, foundPeer, foundData, enabled);
}

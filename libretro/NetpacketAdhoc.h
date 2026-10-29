// Copyright (c) 2012- PPSSPP Project.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "ext/aemu_postoffice/adhocctl/client/adhocctl.h"

namespace Libretro {

// Thread-safe PSP-side transport. The frontend owns delivery of TakePackets()
// and calls Receive(); no libretro callbacks are made on an emulation thread.
class NetpacketAdhoc {
public:
	struct Packet {
		uint16_t peer;
		std::vector<uint8_t> data;
	};
	NetpacketAdhoc();
	~NetpacketAdhoc();
	void Start(uint16_t id);
	void Stop();
	bool Connected(uint16_t id);
	void Disconnected(uint16_t id);
	void Receive(const void *data, size_t size, uint16_t from);
	std::vector<Packet> TakePackets();
	void LocalMac(uint8_t *mac);
	// Whether the frontend session has started, and whether a player is in our group (a PEER announced it).
	bool Active();
	bool HasPeer(uint16_t id);
	uint16_t Self();

	void *ControlCreate(const char *game, const char *nickname);
	void ControlDestroy(void *handle);
	void ControlEvent(void *handle, adhocctl_event *event);
	adhocctl_call_status ControlPing(void *handle);
	adhocctl_call_status ControlJoin(void *handle, const char *group);
	adhocctl_call_status ControlLeave(void *handle);
	adhocctl_call_status ControlScan(void *handle);
	adhocctl_call_status ControlChat(void *handle, const char *message);

	void *DatagramCreate(int port, int *state);
	int DatagramSend(void *handle, const char *mac, int port, const char *data, int size);
	int DatagramReceive(void *handle, char *mac, int *port, char *data, int *size);
	int DatagramSize(void *handle, bool total);
	void *Listen(int port, int *state);
	void *Accept(void *handle, char *mac, int *port, int *state);
	void *Connect(int localPort, const char *mac, int port, bool (*abort)(void *), void *arg, int *state);
	int StreamSend(void *handle, const char *data, int size);
	int StreamReceive(void *handle, char *data, int *size);
	int StreamSize(void *handle);
	int Writable(void *handle);
	int Pending(void *handle);
	bool Dead(void *handle);
	void Close(void *handle);

private:
	struct State;
	std::unique_ptr<State> s_;
};
}

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
	bool Active();

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
	void *BeginConnect(int localPort, const char *mac, int port);
	// -1 when closed, 0 while connecting, 1 when established.
	int ConnectionState(void *handle);
	int StreamSend(void *handle, const char *data, int size);
	int StreamReceive(void *handle, char *data, int *size);
	int StreamSize(void *handle);
	int StreamNextSize(void *handle);
	int Writable(void *handle);
	int Pending(void *handle);
	bool Dead(void *handle);
	void Close(void *handle);

private:
	struct State;
	std::unique_ptr<State> s_;
};
}

// Copyright (c) 2012- PPSSPP Project.
// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone transport regression tests; no emulator, ROM or frontend required.
#include "libretro/NetpacketAdhoc.h"
#include "ext/aemu_postoffice/client/postoffice_client.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <random>
#include <thread>

using Libretro::NetpacketAdhoc;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); } } while (false)
constexpr int BLOCK = AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;

struct Network {
	NetpacketAdhoc node[3];
	void *control[3]{};
	char mac[3][6]{};
	Network() {
		for (int i = 0; i < 3; i++) {
			node[i].Start(i);
			node[i].LocalMac(reinterpret_cast<uint8_t *>(mac[i]));
			if (i)
				CHECK(node[0].Connected(i));
			char nick[128] = "Test player";
			control[i] = node[i].ControlCreate("TEST00001", nick);
			CHECK(control[i]);
		}
		Pump();
	}
	void Pump() {
		for (int round = 0; round < 32; round++) {
			bool sent = false;
			for (int from = 0; from < 3; from++) {
				for (auto &packet : node[from].TakePackets()) {
					CHECK(packet.peer < 3);
					node[packet.peer].Receive(packet.data.data(), packet.data.size(), from);
					sent = true;
				}
			}
			if (!sent)
				return;
		}
		CHECK(false);
	}
	void Join() {
		for (int i = 0; i < 3; i++)
			CHECK(node[i].ControlJoin(control[i], "GROUP001") == ADHOCCTL_CALL_SUCCESS);
		Pump();
		for (int i = 0; i < 3; i++) {
			int peers = 0, leaders = 0;
			adhocctl_event e;
			while (true) {
				node[i].ControlEvent(control[i], &e);
				if (e.type == ADHOCCTL_CLIENT_EVENT_WOULD_BLOCK)
					break;
				if (e.type == ADHOCCTL_CLIENT_EVENT_CONNECT) {
					CHECK(e.connect.id != (uint32_t)i + 1);
					peers++;
				} else if (e.type == ADHOCCTL_CLIENT_EVENT_LEADER_MAC) {
					CHECK(!memcmp(e.leader_mac.mac, mac[0], 6));
					leaders++;
				} else {
					CHECK(false);
				}
			}
			CHECK(peers == 2 && leaders == 1);
		}
	}
};

static void DiscoveryAndDatagrams() {
	Network n;
	n.Join();
	CHECK(n.node[2].ControlScan(n.control[2]) == ADHOCCTL_CALL_SUCCESS);
	n.Pump();
	adhocctl_event e;
	n.node[2].ControlEvent(n.control[2], &e);
	CHECK(e.type == ADHOCCTL_CLIENT_EVENT_SCAN && !memcmp(e.scan.group_name, "GROUP001", 8));
	n.node[2].ControlEvent(n.control[2], &e);
	CHECK(e.type == ADHOCCTL_CLIENT_EVENT_SCAN_COMPLETE);
	int state;
	void *socket[3];
	for (int i = 0; i < 3; i++) {
		socket[i] = n.node[i].DatagramCreate(3000, &state);
		CHECK(socket[i] && state == 0);
	}
	CHECK(n.node[1].DatagramSend(socket[1], "\xff\xff\xff\xff\xff\xff", 3000, "hello", 5) == 0);
	n.Pump();
	char mac[6], data[64]{};
	int port, size = 3;
	CHECK(n.node[0].DatagramReceive(socket[0], mac, &port, data, &size) == AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC && size == 5);
	CHECK(n.node[0].DatagramReceive(socket[0], mac, &port, data, &size) == 0);
	CHECK(size == 5 && port == 3000 && !memcmp(mac, n.mac[1], 6) && !memcmp(data, "hello", 5));
	CHECK(n.node[1].DatagramSize(socket[1], true) == 0);
	CHECK(n.node[2].DatagramSize(socket[2], true) == 5);
	// Group changes revoke former peers, including client-to-client links.
	CHECK(n.node[2].ControlJoin(n.control[2], "GROUP002") == ADHOCCTL_CALL_SUCCESS);
	n.Pump();
	size = 64;
	CHECK(n.node[2].DatagramReceive(socket[2], mac, &port, data, &size) == 0);
	CHECK(n.node[1].DatagramSend(socket[1], n.mac[2], 3000, "secret", 6) == 0);
	n.Pump();
	CHECK(n.node[2].DatagramSize(socket[2], true) == 0);
	// An unknown peer cannot inject PSP data or host discovery events.
	CHECK(n.node[1].DatagramSend(socket[1], n.mac[0], 3000, "bad", 3) == 0);
	for (auto &p : n.node[1].TakePackets())
		n.node[0].Receive(p.data.data(), p.data.size(), 99);
	CHECK(n.node[0].DatagramSize(socket[0], true) == 0);
	for (int i = 0; i < 3; i++)
		n.node[i].Close(socket[i]);
	puts("PASS discovery, scan, broadcast, truncation, group isolation, sender validation");
}

static void Streams() {
	Network n;
	n.Join();
	int state;
	void *listener = n.node[1].Listen(4000, &state);
	CHECK(listener && state == 0);
	auto connecting = std::async(std::launch::async, [&] { int result; return n.node[2].Connect(4001, n.mac[1], 4000, nullptr, nullptr, &result); });
	char mac[6];
	int port;
	void *accepted = nullptr;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < deadline) {
		n.Pump();
		if (!accepted && n.node[1].Pending(listener))
			accepted = n.node[1].Accept(listener, mac, &port, &state);
		if (connecting.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready)
			break;
	}
	CHECK(accepted && state == 0 && port == 4001 && !memcmp(mac, n.mac[2], 6));
	CHECK(connecting.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready);
	void *connected = connecting.get();
	CHECK(connected);
	// Fill the receive window without reading, then verify backpressure.
	std::vector<char> block(32 * 1024);
	for (size_t i = 0; i < block.size(); i++)
		block[i] = (char)i;
	for (int i = 0; i < 8; i++)
		CHECK(n.node[2].StreamSend(connected, block.data(), block.size()) == 0);
	CHECK(n.node[2].StreamSend(connected, block.data(), block.size()) == BLOCK);
	n.Pump();
	CHECK(n.node[1].StreamSize(accepted) == 256 * 1024);
	std::vector<char> received(256 * 1024);
	int offset = 0;
	while (offset < (int)received.size()) {
		int size = std::min<int>(7919, received.size() - offset);
		CHECK(n.node[1].StreamReceive(accepted, received.data() + offset, &size) == 0);
		offset += size;
	}
	for (size_t i = 0; i < received.size(); i++)
		CHECK(received[i] == block[i % block.size()]);
	n.Pump();
	CHECK(n.node[2].StreamSend(connected, "last", 4) == 0);
	n.node[2].Close(connected);
	n.Pump();
	int size = 10;
	CHECK(n.node[1].StreamReceive(accepted, received.data(), &size) == 0 && size == 4);
	CHECK(!memcmp(received.data(), "last", 4));
	CHECK(n.node[1].StreamReceive(accepted, received.data(), &size) == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD);
	n.node[1].Close(accepted);
	n.node[1].Close(listener);
	// Session stop wakes a blocked connector promptly, without a frontend call on its thread.
	connecting = std::async(std::launch::async, [&] { int result; return n.node[2].Connect(4002, n.mac[1], 4999, nullptr, nullptr, &result); });
	n.node[2].Stop();
	CHECK(connecting.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
	CHECK(connecting.get() == nullptr);
	n.node[0].Disconnected(2);
	n.Pump();
	CHECK(n.node[0].Connected(2));
	n.node[2].ControlDestroy(n.control[2]);
	n.node[2].Start(2);
	char nick[128] = "Reconnected";
	n.control[2] = n.node[2].ControlCreate("TEST00001", nick);
	CHECK(n.control[2]);
	CHECK(n.node[2].ControlJoin(n.control[2], "GROUP001") == ADHOCCTL_CALL_SUCCESS);
	n.Pump();
	puts("PASS client-to-client PTP, backpressure, partial reads, close, stop, reconnect");
}

static void MalformedAndLimits() {
	Network n;
	n.Join();
	std::mt19937 random(84721);
	std::vector<uint8_t> packet(1024);
	for (int trial = 0; trial < 20000; trial++) {
		size_t length = random() % packet.size();
		for (size_t j = 0; j < length; j++)
			packet[j] = random();
		if (length >= 13 && trial % 2 == 0) {
			memcpy(packet.data(), "PSP\1", 4);
			packet[4] = 1 + random() % 19;
		}
		n.node[1].Receive(packet.data(), length, trial % 3);
	}
	CHECK(n.node[1].ControlPing(n.control[1]) == ADHOCCTL_CALL_SUCCESS);
	std::vector<void *> sockets;
	int state;
	while (void *h = n.node[1].DatagramCreate(5000 + sockets.size(), &state))
		sockets.push_back(h);
	CHECK(sockets.size() == 255);
	std::vector<char> payload(10 * 1024, 'x');
	int sent = 0;
	while (n.node[1].DatagramSend(sockets[0], n.mac[0], 5000, payload.data(), payload.size()) == 0) {
		CHECK(++sent < 500);
	}
	CHECK(sent > 100);
	n.Pump();
	CHECK(n.node[1].DatagramSend(sockets[0], n.mac[0], 5000, payload.data(), payload.size()) == 0);
	for (void *h : sockets)
		n.node[1].Close(h);
	puts("PASS malformed packets, socket limit, bounded queues and recovery");
}

int main() {
	DiscoveryAndDatagrams();
	Streams();
	MalformedAndLimits();
	puts("All Netpacket ad hoc tests passed.");
}

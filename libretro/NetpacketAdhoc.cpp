// Copyright (c) 2012- PPSSPP Project.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "libretro/NetpacketAdhoc.h"
#include "ext/aemu_postoffice/client/postoffice_client.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>

namespace Libretro {
namespace {
constexpr size_t WINDOW = 256 * 1024;
constexpr size_t QUEUE_LIMIT = 4 * 1024 * 1024;
constexpr size_t MAX_SOCKETS = 256;  // 255 HLE socket slots plus adhocctl.
constexpr size_t MAX_PLAYERS = 16;
constexpr size_t HEADER = 13;
constexpr size_t MAX_PAYLOAD = 50 * 1024;
enum Kind : uint8_t {
	LOGIN = 1, LOGOUT, JOIN, LEAVE, SCAN, CHAT,
	PEER, GONE, LEADER, GROUP, SCANNED, MESSAGE,
	DATAGRAM, CONNECT, ACCEPT, DATA, CREDIT, CLOSE, CANCEL,
};

void Put32(std::vector<uint8_t> &v, uint32_t n) {
	for (int i = 0; i < 4; i++)
		v.push_back((n >> (i * 8)) & 255);
}
uint32_t Get32(const uint8_t *p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void Mac(uint16_t id, void *out) {
	// Some games require the low two bits of the first octet to be clear.
	uint8_t mac[6] = {0x04, 0x50, 0x53, 0x50, uint8_t(id >> 8), uint8_t(id)};
	memcpy(out, mac, 6);
}
int PeerId(const char *mac) {
	auto p = reinterpret_cast<const uint8_t *>(mac);
	// PPSSPP's isMacMatch likewise ignores the first octet, which games may change.
	if (p[1] != 0x50 || p[2] != 0x53 || p[3] != 0x50)
		return -1;
	return p[4] * 256 + p[5];
}
}

struct NetpacketAdhoc::State {
	enum Type { CONTROL, PDP, LISTENER, STREAM };
	struct Datagram {
		uint16_t peer, port;
		std::vector<uint8_t> data;
	};
	struct Request {
		uint16_t peer, port;
		uint32_t endpoint;
	};
	struct Socket {
		Type type;
		uint32_t id;
		uint16_t port = 0, peer = 0;
		uint32_t remote = 0;
		bool dead = false, connected = false;
		size_t credit = WINDOW, bytes = 0;
		std::deque<uint8_t> stream;
		std::deque<Datagram> datagrams;
		std::deque<Request> requests;
		std::deque<adhocctl_event> events;
	};
	struct Player {
		uint32_t control = 0;
		std::array<uint8_t, 9> game{};
		std::array<uint8_t, 128> nickname{};
		std::array<uint8_t, 8> group{};
		bool joined = false;
		uint16_t leader = 0;
	};
	std::mutex mutex;
	std::condition_variable changed;
	bool active = false;
	uint16_t self = 0;
	uint32_t nextId = 1;
	Socket *control = nullptr;
	std::map<uint32_t, std::unique_ptr<Socket>> sockets;
	std::map<uint16_t, Player> players;  // Host-owned discovery service.
	std::set<uint16_t> clients, peers;
	std::vector<Packet> outgoing;
	size_t queued = 0;

	void Stop() {
		active = false;
		for (auto &entry : sockets)
			entry.second->dead = true;
		peers.clear();
		outgoing.clear();
		queued = 0;
		changed.notify_all();
	}
	Socket *New(Type type, int port = 0) {
		if (!active || sockets.size() >= MAX_SOCKETS || nextId == 0 || port < 0 || port > 65535)
			return nullptr;
		auto p = std::unique_ptr<Socket>(new Socket{});
		p->type = type;
		p->id = nextId++;
		p->port = port;
		auto result = p.get();
		sockets.emplace(p->id, std::move(p));
		return result;
	}
	bool Send(uint16_t peer, Kind kind, uint32_t a = 0, uint32_t b = 0, const void *data = nullptr, size_t size = 0) {
		if (!active || size > MAX_PAYLOAD || queued + HEADER + size > QUEUE_LIMIT || outgoing.size() >= 4096)
			return false;
		Packet packet{peer, {'P', 'S', 'P', 1, kind}};
		Put32(packet.data, a);
		Put32(packet.data, b);
		if (size) {
			auto p = static_cast<const uint8_t *>(data);
			packet.data.insert(packet.data.end(), p, p + size);
		}
		queued += packet.data.size();
		outgoing.push_back(std::move(packet));
		return true;
	}
	// Losing a control/stream message would leave endpoints inconsistent. Fail
	// the session instead of silently dropping it; data sends apply backpressure.
	void MustSend(uint16_t peer, Kind kind, uint32_t a = 0, uint32_t b = 0, const void *data = nullptr, size_t size = 0) {
		if (!Send(peer, kind, a, b, data, size))
			Stop();
	}
	void Event(const adhocctl_event &event) {
		if (!control || control->dead)
			return;
		if (control->events.size() >= 256) {
			Stop();
			return;
		}
		control->events.push_back(event);
	}
	void KillPeer(uint16_t peer) {
		peers.erase(peer);
		for (auto &entry : sockets) {
			auto &s = *entry.second;
			if (s.type == STREAM && s.peer == peer)
				s.dead = true;
			s.requests.erase(std::remove_if(s.requests.begin(), s.requests.end(), [peer](const Request &r) { return r.peer == peer; }), s.requests.end());
		}
		changed.notify_all();
	}
	void Leave(uint16_t id) {
		auto found = players.find(id);
		if (found == players.end() || !found->second.joined)
			return;
		Player old = found->second;
		found->second.joined = false;
		for (auto &entry : players) {
			auto &p = entry.second;
			if (p.joined && p.game == old.game && p.group == old.group) {
				MustSend(entry.first, GONE, p.control, id);
				MustSend(id, GONE, old.control, entry.first);
			}
		}
	}
	void Discover(uint16_t from, Kind kind, uint32_t token, const uint8_t *data, size_t size) {
		if (self != 0 || !clients.count(from))
			return;
		if (kind == LOGIN) {
			if (size != 137 || !token)
				return;
			Leave(from);
			if (!active)
				return;
			Player p;
			p.control = token;
			memcpy(p.game.data(), data, 9);
			memcpy(p.nickname.data(), data + 9, 128);
			p.nickname.back() = 0;
			players[from] = p;
			return;
		}
		auto it = players.find(from);
		if (it == players.end() || it->second.control != token)
			return;
		if ((kind == LOGOUT || kind == LEAVE) && size == 0) {
			Leave(from);
			if (kind == LOGOUT)
				players.erase(from);
		} else if (kind == JOIN && size == 8) {
			Leave(from);
			if (!active)
				return;
			auto &p = players[from];
			memcpy(p.group.data(), data, 8);
			p.leader = from;
			for (const auto &entry : players) {
				const auto &other = entry.second;
				if (other.joined && other.game == p.game && other.group == p.group) {
					p.leader = other.leader;
					MustSend(from, PEER, p.control, entry.first, other.nickname.data(), 128);
					MustSend(entry.first, PEER, other.control, from, p.nickname.data(), 128);
				}
			}
			p.joined = true;
			MustSend(from, LEADER, p.control, p.leader);
		} else if (kind == SCAN && size == 0) {
			std::set<std::array<uint8_t, 8>> seen;
			for (const auto &entry : players) {
				const auto &p = entry.second;
				if (p.joined && p.game == it->second.game && seen.insert(p.group).second)
					MustSend(from, GROUP, token, p.leader, p.group.data(), 8);
			}
			MustSend(from, SCANNED, token);
		} else if (kind == CHAT && size <= 63 && it->second.joined) {
			std::array<uint8_t, 191> message{};
			memcpy(message.data(), it->second.nickname.data(), 128);
			memcpy(message.data() + 128, data, size);
			for (const auto &entry : players) {
				const auto &p = entry.second;
				if (p.joined && p.game == it->second.game && p.group == it->second.group)
					MustSend(entry.first, MESSAGE, p.control, from, message.data(), 128 + size);
			}
		}
	}
};

NetpacketAdhoc::NetpacketAdhoc() : s_(new State{}) {}
NetpacketAdhoc::~NetpacketAdhoc() = default;

void NetpacketAdhoc::Start(uint16_t id) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	s_->Stop();
	s_->players.clear();
	s_->clients.clear();
	s_->self = id;
	s_->active = id != 65535;
	s_->clients.insert(0);
}
void NetpacketAdhoc::Stop() {
	std::lock_guard<std::mutex> guard(s_->mutex);
	s_->Stop();
}
bool NetpacketAdhoc::Connected(uint16_t id) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	if (!s_->active || s_->self != 0 || id == 0 || id == 65535 || s_->clients.size() >= MAX_PLAYERS)
		return false;
	return s_->clients.insert(id).second;
}
void NetpacketAdhoc::Disconnected(uint16_t id) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	s_->Leave(id);
	s_->players.erase(id);
	s_->clients.erase(id);
	s_->KillPeer(id);
}
void NetpacketAdhoc::LocalMac(uint8_t *mac) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	Mac(s_->self, mac);
}
bool NetpacketAdhoc::Active() {
	std::lock_guard<std::mutex> guard(s_->mutex);
	return s_->active;
}
bool NetpacketAdhoc::HasPeer(uint16_t id) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	return s_->active && s_->peers.count(id) != 0;
}
uint16_t NetpacketAdhoc::Self() {
	std::lock_guard<std::mutex> guard(s_->mutex);
	return s_->self;
}
std::vector<NetpacketAdhoc::Packet> NetpacketAdhoc::TakePackets() {
	std::lock_guard<std::mutex> guard(s_->mutex);
	std::vector<Packet> packets;
	packets.swap(s_->outgoing);
	s_->queued = 0;
	return packets;
}

void NetpacketAdhoc::Receive(const void *buffer, size_t length, uint16_t from) {
	if (!buffer || length < HEADER || length > HEADER + MAX_PAYLOAD || from == 65535)
		return;
	auto bytes = static_cast<const uint8_t *>(buffer);
	if (memcmp(bytes, "PSP\1", 4) != 0 || bytes[4] < LOGIN || bytes[4] > CANCEL)
		return;
	Kind kind = (Kind)bytes[4];
	uint32_t a = Get32(bytes + 5), b = Get32(bytes + 9);
	const uint8_t *data = bytes + HEADER;
	size_t size = length - HEADER;
	std::lock_guard<std::mutex> guard(s_->mutex);
	if (!s_->active)
		return;
	if (kind <= CHAT) {
		s_->Discover(from, kind, a, data, size);
		return;
	}
	if (kind <= MESSAGE) {
		if (from != 0 || !s_->control || s_->control->dead || s_->control->id != a || b >= 65535)
			return;
		adhocctl_event e{};
		if (kind == PEER && size == 128 && b != s_->self && s_->peers.size() < MAX_PLAYERS) {
			s_->peers.insert(b);
			e.type = ADHOCCTL_CLIENT_EVENT_CONNECT;
			Mac(b, e.connect.mac);
			e.connect.id = b + 1;  // Synthetic address, never used with host sockets.
			memcpy(e.connect.nickname, data, 128);
			e.connect.nickname[127] = 0;
		} else if (kind == GONE && size == 0) {
			s_->KillPeer(b);
			e.type = ADHOCCTL_CLIENT_EVENT_DISCONNECT;
			e.disconnect.id = b + 1;
		} else if (kind == LEADER && size == 0) {
			e.type = ADHOCCTL_CLIENT_EVENT_LEADER_MAC;
			Mac(b, e.leader_mac.mac);
		} else if (kind == GROUP && size == 8) {
			e.type = ADHOCCTL_CLIENT_EVENT_SCAN;
			memcpy(e.scan.group_name, data, 8);
			Mac(b, e.scan.leader_mac);
		} else if (kind == SCANNED && size == 0) {
			e.type = ADHOCCTL_CLIENT_EVENT_SCAN_COMPLETE;
		} else if (kind == MESSAGE && size >= 128 && size <= 191) {
			e.type = ADHOCCTL_CLIENT_EVENT_CHAT;
			memcpy(e.chat.nickname, data, 128);
			e.chat.nickname[127] = 0;
			memcpy(e.chat.message, data + 128, size - 128);
		} else {
			return;
		}
		s_->Event(e);
		return;
	}
	if (from != s_->self && !s_->peers.count(from))
		return;
	if (kind == DATAGRAM && a <= 65535 && b <= 65535 && size <= 10 * 1024) {
		for (auto &entry : s_->sockets) {
			auto &socket = *entry.second;
			if (socket.type == State::PDP && !socket.dead && socket.port == b && socket.bytes + size <= WINDOW && socket.datagrams.size() < 256) {
				socket.datagrams.push_back({from, (uint16_t)a, std::vector<uint8_t>(data, data + size)});
				socket.bytes += size;
				break;
			}
		}
		return;
	}
	if (kind == CONNECT && a && b <= 65535 && size == 4 && Get32(data) <= 65535) {
		for (auto &entry : s_->sockets) {
			auto &socket = *entry.second;
			if (socket.type == State::LISTENER && !socket.dead && socket.port == b && socket.requests.size() < MAX_PLAYERS) {
				for (const auto &r : socket.requests) {
					if (r.peer == from && r.endpoint == a)
						return;
				}
				socket.requests.push_back({from, (uint16_t)Get32(data), a});
				return;
			}
		}
		s_->MustSend(from, CLOSE, a);
		return;
	}
	if (kind == CANCEL && size == 0) {
		for (auto &entry : s_->sockets) {
			auto &socket = *entry.second;
			if (socket.type == State::STREAM && socket.peer == from && socket.remote == a)
				socket.dead = true;
			socket.requests.erase(std::remove_if(socket.requests.begin(), socket.requests.end(), [from, a](const State::Request &r) { return r.peer == from && r.endpoint == a; }), socket.requests.end());
		}
		return;
	}
	auto it = s_->sockets.find(a);
	if (it == s_->sockets.end()) {
		if (kind == ACCEPT && size == 0 && b)
			s_->MustSend(from, CLOSE, b, a);
		return;
	}
	auto &socket = *it->second;
	if (socket.dead || socket.type != State::STREAM || socket.peer != from)
		return;
	if (kind == ACCEPT && size == 0 && b && !socket.connected) {
		socket.remote = b;
		socket.connected = true;
		s_->changed.notify_all();
	} else if (kind == CLOSE && size == 0 && (b == socket.remote || (!socket.connected && b == 0))) {
		socket.dead = true;
		s_->changed.notify_all();
	} else if (socket.connected && socket.remote == b) {
		if (kind == DATA && size && socket.stream.size() + size <= WINDOW) {
			socket.stream.insert(socket.stream.end(), data, data + size);
		} else if (kind == CREDIT && size == 4 && Get32(data) <= WINDOW - socket.credit) {
			socket.credit += Get32(data);
		} else {
			socket.dead = true;
			s_->MustSend(from, CLOSE, socket.remote, socket.id);
		}
	}
}

void *NetpacketAdhoc::ControlCreate(const char *game, const char *nickname) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	if (s_->control)
		return nullptr;
	auto socket = s_->New(State::CONTROL);
	if (!socket)
		return nullptr;
	s_->control = socket;
	uint8_t data[137];
	memcpy(data, game, 9);
	memcpy(data + 9, nickname, 128);
	s_->MustSend(0, LOGIN, socket->id, 0, data, sizeof(data));
	return socket;
}
void NetpacketAdhoc::ControlDestroy(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket)
		return;
	if (!socket->dead)
		s_->MustSend(0, LOGOUT, socket->id);
	s_->control = nullptr;
	s_->sockets.erase(socket->id);
	// No sockets may continue talking to a former group.
	auto peers = s_->peers;
	for (uint16_t peer : peers)
		s_->KillPeer(peer);
}
void NetpacketAdhoc::ControlEvent(void *handle, adhocctl_event *event) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	*event = {};
	if (!socket || socket->dead) {
		event->type = ADHOCCTL_CLIENT_EVENT_ERROR;
	} else if (socket->events.empty()) {
		event->type = ADHOCCTL_CLIENT_EVENT_WOULD_BLOCK;
	} else {
		*event = socket->events.front();
		socket->events.pop_front();
	}
}
adhocctl_call_status NetpacketAdhoc::ControlPing(void *handle) {
	return Dead(handle) ? ADHOCCTL_CALL_ERROR : ADHOCCTL_CALL_SUCCESS;
}
adhocctl_call_status NetpacketAdhoc::ControlJoin(void *handle, const char *group) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return ADHOCCTL_CALL_ERROR;
	return s_->Send(0, JOIN, socket->id, 0, group, 8) ? ADHOCCTL_CALL_SUCCESS : ADHOCCTL_CALL_WOULDBLOCK;
}
adhocctl_call_status NetpacketAdhoc::ControlLeave(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return ADHOCCTL_CALL_ERROR;
	return s_->Send(0, LEAVE, socket->id) ? ADHOCCTL_CALL_SUCCESS : ADHOCCTL_CALL_WOULDBLOCK;
}
adhocctl_call_status NetpacketAdhoc::ControlScan(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return ADHOCCTL_CALL_ERROR;
	return s_->Send(0, SCAN, socket->id) ? ADHOCCTL_CALL_SUCCESS : ADHOCCTL_CALL_WOULDBLOCK;
}
adhocctl_call_status NetpacketAdhoc::ControlChat(void *handle, const char *message) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return ADHOCCTL_CALL_ERROR;
	return s_->Send(0, CHAT, socket->id, 0, message, std::min<size_t>(strlen(message), 63)) ? ADHOCCTL_CALL_SUCCESS : ADHOCCTL_CALL_WOULDBLOCK;
}

void *NetpacketAdhoc::DatagramCreate(int port, int *state) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = s_->New(State::PDP, port);
	*state = socket ? 0 : AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	return socket;
}
int NetpacketAdhoc::DatagramSend(void *handle, const char *mac, int port, const char *data, int size) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	if (size < 0 || size > 10 * 1024 || port < 0 || port > 65535)
		return AEMU_POSTOFFICE_CLIENT_UNKNOWN;
	bool broadcast = memcmp(mac, "\xff\xff\xff\xff\xff\xff", 6) == 0;
	auto targets = broadcast ? s_->peers : std::set<uint16_t>{};
	int peer = PeerId(mac);
	if (!broadcast && (peer == s_->self || s_->peers.count(peer)))
		targets.insert(peer);
	if (s_->queued + (HEADER + size) * targets.size() > QUEUE_LIMIT || s_->outgoing.size() + targets.size() > 4096)
		return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	for (uint16_t target : targets)
		s_->Send(target, DATAGRAM, socket->port, port, data, size);
	return 0;
}
int NetpacketAdhoc::DatagramReceive(void *handle, char *mac, int *port, char *data, int *size) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	if (socket->datagrams.empty())
		return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	const auto &d = socket->datagrams.front();
	if (*size < (int)d.data.size()) {
		*size = (int)d.data.size();
		return AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC;
	}
	Mac(d.peer, mac);
	*port = d.port;
	*size = (int)d.data.size();
	memcpy(data, d.data.data(), d.data.size());
	socket->bytes -= d.data.size();
	socket->datagrams.pop_front();
	return 0;
}
int NetpacketAdhoc::DatagramSize(void *handle, bool total) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	return total ? (int)socket->bytes : (socket->datagrams.empty() ? 0 : (int)socket->datagrams.front().data.size());
}
void *NetpacketAdhoc::Listen(int port, int *state) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = s_->New(State::LISTENER, port);
	*state = socket ? 0 : AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	return socket;
}
void *NetpacketAdhoc::Accept(void *handle, char *mac, int *port, int *state) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto listener = static_cast<State::Socket *>(handle);
	*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	if (!listener || listener->dead)
		return nullptr;
	*state = AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	if (listener->requests.empty())
		return nullptr;
	auto socket = s_->New(State::STREAM, listener->port);
	if (!socket)
		return nullptr;
	auto request = listener->requests.front();
	listener->requests.pop_front();
	socket->peer = request.peer;
	socket->remote = request.endpoint;
	socket->connected = true;
	Mac(request.peer, mac);
	*port = request.port;
	*state = 0;
	s_->MustSend(socket->peer, ACCEPT, socket->remote, socket->id);
	return socket;
}
void *NetpacketAdhoc::Connect(int localPort, const char *mac, int port, bool (*abort)(void *), void *arg, int *state) {
	std::unique_lock<std::mutex> guard(s_->mutex);
	*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	int peer = PeerId(mac);
	if ((peer != s_->self && !s_->peers.count(peer)) || port < 0 || port > 65535)
		return nullptr;
	auto socket = s_->New(State::STREAM, localPort);
	if (!socket)
		return nullptr;
	socket->peer = peer;
	std::vector<uint8_t> payload;
	Put32(payload, localPort);
	s_->MustSend(peer, CONNECT, socket->id, port, payload.data(), payload.size());
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!socket->connected && !socket->dead && std::chrono::steady_clock::now() < deadline) {
		if (abort && abort(arg))
			break;
		s_->changed.wait_for(guard, std::chrono::milliseconds(10));
	}
	if (!socket->dead && socket->connected) {
		*state = 0;
		return socket;
	}
	if (s_->active)
		s_->MustSend(peer, CANCEL, socket->id);
	s_->sockets.erase(socket->id);
	return nullptr;
}
int NetpacketAdhoc::StreamSend(void *handle, const char *data, int size) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	if (size < 0 || size > (int)MAX_PAYLOAD)
		return AEMU_POSTOFFICE_CLIENT_UNKNOWN;
	if (size == 0)
		return 0;
	if (!socket->connected || (size_t)size > socket->credit || !s_->Send(socket->peer, DATA, socket->remote, socket->id, data, size))
		return AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	socket->credit -= size;
	return 0;
}
int NetpacketAdhoc::StreamReceive(void *handle, char *data, int *size) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket)
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	if (socket->stream.empty())
		return socket->dead ? AEMU_POSTOFFICE_CLIENT_SESSION_DEAD : AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK;
	if (*size < 0)
		return AEMU_POSTOFFICE_CLIENT_UNKNOWN;
	*size = (int)std::min<size_t>(*size, socket->stream.size());
	for (int i = 0; i < *size; i++) {
		data[i] = socket->stream.front();
		socket->stream.pop_front();
	}
	if (*size && !socket->dead) {
		std::vector<uint8_t> credit;
		Put32(credit, *size);
		s_->MustSend(socket->peer, CREDIT, socket->remote, socket->id, credit.data(), credit.size());
	}
	return 0;
}
int NetpacketAdhoc::StreamSize(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	return socket ? (int)socket->stream.size() : AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}
int NetpacketAdhoc::Writable(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket || socket->dead)
		return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	return s_->queued + MAX_PAYLOAD + HEADER <= QUEUE_LIMIT && s_->outgoing.size() < 4096 && (socket->type != State::STREAM || socket->credit >= MAX_PAYLOAD);
}
int NetpacketAdhoc::Pending(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	return socket && !socket->dead && !socket->requests.empty();
}
bool NetpacketAdhoc::Dead(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	return !socket || socket->dead;
}
void NetpacketAdhoc::Close(void *handle) {
	std::lock_guard<std::mutex> guard(s_->mutex);
	auto socket = static_cast<State::Socket *>(handle);
	if (!socket)
		return;
	if (socket->type == State::STREAM && socket->connected && !socket->dead)
		s_->MustSend(socket->peer, CLOSE, socket->remote, socket->id);
	for (const auto &r : socket->requests)
		s_->MustSend(r.peer, CLOSE, r.endpoint);
	s_->sockets.erase(socket->id);
}
}

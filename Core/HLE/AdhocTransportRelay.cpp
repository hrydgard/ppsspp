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

// The relay transport: ad hoc sockets carried by an aemu_postoffice server.

#include "ppsspp_config.h"

#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "Common/Net/SocketCompat.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Instance.h"
#include "Core/Util/PortManager.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/AdhocTransport.h"
#include "Core/HLE/NetAdhocCommon.h"
#include "Core/HLE/NetInetConstants.h"

#include "ext/aemu_postoffice/client/postoffice_client.h"

#ifdef _WIN32
#undef errno
#define errno WSAGetLastError()
#endif

static void closeRelayBacklog(const AdhocSocket *listener);

void RelayTransport::CloseHostSocket(AdhocSocket *sock) {
	if (sock->type == SOCK_PTP) {
		// sync
		if (sock->connectThread != NULL) {
			sock->connectThread->join();
			delete sock->connectThread;
		}
		void *socket = sock->postofficeHandle;
		if (socket != NULL) {
			if (sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN) {
				ptp_listen_close(socket);
			} else {
				ptp_close(socket);
			}
		}
		closeRelayBacklog(sock);
	} else {
		if (sock->postofficeHandle != NULL) {
			pdp_delete(sock->postofficeHandle);
		}
	}
}

static void handle_relay_connect_failure() {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	if (!trackingRelayFailure) {
		trackingRelayFailure = true;
		relayLastFailure = std::chrono::steady_clock::now();
	}
	int been_failing_since_sec = (std::chrono::steady_clock::now() - relayLastFailure) / std::chrono::seconds(1);
	if (been_failing_since_sec < RELAY_FAILURE_DISABLE_THRESHOLD_SEC) {
		g_OSD.Show(OSDType::MESSAGE_ERROR, n->T("Failed connecting to relay server"), 4.0f, "relay_status");
	} else {
		g_OSD.Show(OSDType::MESSAGE_ERROR, ApplySafeSubstitutions(n->T("Failed connecting to relay server for %1 seconds, relay disabled"), RELAY_FAILURE_DISABLE_THRESHOLD_SEC), 10.0f, "relay_status");
		relayDisabled = true;
	}
}

static void handle_relay_connect_success() {
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	if (trackingRelayFailure) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, n->T("Connection to relay has recovered"), 2.0f, "relay_status");
	}
	if (relayFirstConnect) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, n->T("Connected to relay"), 2.0f, "relay_status");
		relayFirstConnect = false;
	}
	trackingRelayFailure = false;
}

static void *pdp_postoffice_recover(int idx) {
	if (relayDisabled) {
		return NULL;
	}

	AdhocSocket *internal = adhocSockets[idx];
	if (internal->postofficeHandle != NULL) {
		return internal->postofficeHandle;
	}

	INFO_LOG(Log::sceNet, "%s: recovering pdp socket id %d", __func__, idx + 1);

	struct aemu_postoffice_sock_addr addr;
	auto server_ip = GetAdhocServerIP();
	addr.addr = server_ip.in.sin_addr.s_addr;
	addr.port = htons(AEMU_POSTOFFICE_PORT);

	SceNetEtherAddr local_mac;
	getLocalMac(&local_mac);

	int state;
	internal->postofficeHandle = pdp_create_v4(&addr, (const char *)&local_mac, offset_port_simple(internal->data.pdp.lport), netAdhocUseUdp, internal->postofficePdpEnetReliable, NULL, NULL, &state);
	if (state != AEMU_POSTOFFICE_CLIENT_OK) {
		ERROR_LOG(Log::sceNet, "%s: failed creating pdp socket on aemu postoffice library, %d", __func__, state);
		handle_relay_connect_failure();
	} else {
		handle_relay_connect_success();
	}

	INFO_LOG(Log::sceNet, "%s: pdp recovery for id %d: %p", __func__, idx + 1, internal->postofficeHandle);
	return internal->postofficeHandle;
}

static int pdp_recv_postoffice(int idx, SceNetEtherAddr *saddr, uint16_t *sport, void *data, int *len) {
	AdhocSocket *internal = adhocSockets[idx];
	void *pdp_sock = pdp_postoffice_recover(idx);
	if (pdp_sock == NULL) {
		return SOCKET_ERROR;
	}

	int sport_copy = 0;
	SceNetEtherAddr saddr_copy = {0};
	int len_copy = *len;

	int pdp_recv_status = pdp_recv(pdp_sock, (char *)&saddr_copy, &sport_copy, (char *)data, &len_copy, true);
	if (pdp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		handle_relay_connect_failure();
		pdp_delete(internal->postofficeHandle);
		internal->postofficeHandle = NULL;
		return SOCKET_ERROR;
	}
	if (pdp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK) {
		return SOCKET_ERROR;
	}
	// Out of the group, nothing from anyone else reaches us (pspautotests net/adhoc/pair/vanish).
	if (offAdhocNetwork() && !isLocalMac(&saddr_copy)) {
		return SOCKET_ERROR;
	}

	*len = len_copy;
	if (saddr != NULL) {
		*saddr = saddr_copy;
	}
	if (sport != NULL) {
		*sport = reverse_port_simple(sport_copy);
	}
	return 0;
}

bool RelayTransport::PdpTakeDatagram(int id, SceNetEtherAddr *mac, uint16_t *port, std::vector<u8> &data) {
	static char buf[AEMU_POSTOFFICE_PDP_BLOCK_MAX];
	int len = sizeof(buf);
	if (pdp_recv_postoffice(id - 1, mac, port, buf, &len) != 0) {
		return false;
	}
	data.assign(buf, buf + len);
	return true;
}

static int pdp_send_postoffice(int idx, const SceNetEtherAddr *daddr, uint16_t dport, const void *data, int len) {
	// Out of the group, it goes into the air and reaches nobody (pspautotests net/adhoc/pair/vanish),
	// whether or not the relay session is up.
	if (offAdhocNetwork() && !isLocalMac(daddr)) {
		return 0;
	}

	AdhocSocket *internal = adhocSockets[idx];
	void *pdp_sock = pdp_postoffice_recover(idx);
	if (pdp_sock == NULL) {
		return SOCKET_ERROR;
	}

	SceNetEtherAddr fixed_daddr = *daddr;
	fixGameMac(&fixed_daddr);

	int pdp_send_status = pdp_send(pdp_sock, (const char *)&fixed_daddr, offset_port_simple(dport), (char *)data, len, true);
	if (pdp_send_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		handle_relay_connect_failure();
		pdp_delete(internal->postofficeHandle);
		internal->postofficeHandle = NULL;
		return SOCKET_ERROR;
	}
	if (pdp_send_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK) {
		return SOCKET_ERROR;
	}
	if (pdp_send_status == AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY) {
		// this is pretty critical
		ERROR_LOG(Log::sceNet, "%s: critical: huge client buf %d what is going on please fix", __func__, len);
	}
	return 0;
}

int RelayTransport::PdpSendTo(int id, const SceNetEtherAddr *mac, uint16_t dport, const sockaddr_in &target, const void *data, int len, int *error) {
	if (pdp_send_postoffice(id - 1, mac, dport, data, len) == 0) {
		return len;
	}
	*error = EAGAIN;
	return SOCKET_ERROR;
}

static int ptp_send_postoffice(int idx, const void *data, int *len) {
	// Out of the group, the send succeeds and the data goes nowhere (pspautotests net/adhoc/pair/vanish),
	// whatever became of the relay meanwhile.
	if (offAdhocNetwork()) {
		return 0;
	}

	if (relayDisabled) {
		return SOCKET_ERROR;
	}

	AdhocSocket *internal = adhocSockets[idx];

	if (internal->postofficeHandle == NULL) {
		// this should only happen on ptp_open sockets, where ptp_connect is still in progress on another thread
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	if (ptpPeerGone(internal)) {
		return SOCKET_ERROR;
	}

	if (*len > AEMU_POSTOFFICE_PTP_BLOCK_MAX) {
		// force fragmentation for giant sends
		*len = AEMU_POSTOFFICE_PTP_BLOCK_MAX;
	}

	int ptp_send_status = ptp_send(internal->postofficeHandle, (const char *)data, *len, true);
	if (ptp_send_status == AEMU_POSTOFFICE_CLIENT_OK) {
		ptpNoteSend(internal);
	}
	if (ptp_send_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		// the session is dead, need to be reflected to the other side
		// this is not necessarily a relay failure, could simply be the other side disconnecting
		//handle_relay_connect_failure();
		return SOCKET_ERROR;
	}
	if (ptp_send_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}
	if (ptp_send_status == AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY) {
		// this is pretty critical
		ERROR_LOG(Log::sceNet, "%s: critical: huge client buf %d what is going on please fix", __func__, len ? *len : 0);
	}

	return 0;
}

int RelayTransport::DoBlockingPtpSend(AdhocSocketRequest &req, s64 &result) {
	auto sock = adhocSockets[req.id - 1];
	auto& ptpsocket = sock->data.ptp;
	int ret = ptp_send_postoffice(req.id - 1, req.buffer, req.length);
	if (ret == 0) {
		// sent
		// Set to Established on successful Send when an attempt to Connect was initiated
		if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
			ptpSetEstablished(sock);

		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u\n", req.id, ptpsocket.lport, ret, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

		result = 0;
		return 0;
	}
	if (ret == SOCKET_ERROR) {
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
		result = SCE_NET_ADHOC_ERROR_DISCONNECTED;

		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i]: Socket Error (%i)", req.id, 0);

		return 0;
	}

	// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
	u64 now = CoreTiming::GetGlobalTimeUs();
	if (req.timeout == 0 || now - req.startTime <= req.timeout) {
		return -1;
	}
	result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i]: Socket Error (%i)", req.id, EAGAIN);
	return 0;
}

static int ptp_recv_postoffice(int idx, void *data, int *len) {
	// Out of the group, nothing arrives, and the connection doesn't fail either (pspautotests
	// net/adhoc/pair/vanish), whatever became of the relay meanwhile.
	if (offAdhocNetwork()) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	if (relayDisabled) {
		return SOCKET_ERROR;
	}

	AdhocSocket *internal = adhocSockets[idx];

	if (internal->postofficeHandle == NULL) {
		// this should only happen on ptp_open sockets, where ptp_connect is still in progress on another thread
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	if (ptpPeerGone(internal)) {
		return SOCKET_ERROR;
	}

	int len_copy = *len;

	int ptp_recv_status = ptp_recv(internal->postofficeHandle, (char *)data, &len_copy, true);
	if (ptp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		// the session is dead, need to be reflected to the other side
		// this is not necessarily a relay failure, could simply be the other side disconnecting
		//handle_relay_connect_failure();
		return SOCKET_ERROR;
	}
	if (ptp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	// AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC is okay, it just means it has data in it's user space buffer

	*len = len_copy;
	return 0;
}

int RelayTransport::DoBlockingPtpRecv(AdhocSocketRequest &req, s64 &result) {
	auto& ptpsocket = adhocSockets[req.id - 1]->data.ptp;
	int ret = ptp_recv_postoffice(req.id - 1, req.buffer, req.length);
	if (ret == 0) {
		// we got data
		result = 0;
		return 0;
	}
	if (ret == SOCKET_ERROR) {
		// the socket died, let the game know
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
		result = SCE_NET_ADHOC_ERROR_DISCONNECTED;
		return 0;
	}

	// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
	u64 now = CoreTiming::GetGlobalTimeUs();
	if (req.timeout == 0 || now - req.startTime <= req.timeout) {
		return -1;
	}
	result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i]: Socket Error (%i)", req.id, EAGAIN);
	return 0;
}

static void *ptp_listen_postoffice_recover(int idx) {
	if (relayDisabled) {
		return NULL;
	}

	AdhocSocket *internal = adhocSockets[idx];
	if (internal->postofficeHandle != NULL) {
		return internal->postofficeHandle;
	}

	INFO_LOG(Log::sceNet, "%s: recovering ptp listen socket id %d", __func__, idx + 1);

	struct aemu_postoffice_sock_addr addr;
	auto server_ip = GetAdhocServerIP();
	addr.addr = server_ip.in.sin_addr.s_addr;
	addr.port = htons(AEMU_POSTOFFICE_PORT);

	int state;
	internal->postofficeHandle = ptp_listen_v4(&addr, (const char*)&internal->data.ptp.laddr, internal->data.ptp.lport + portOffset, netAdhocUseUdp, NULL, NULL, &state);

	if (state != AEMU_POSTOFFICE_CLIENT_OK) {
		ERROR_LOG(Log::sceNet, "%s: failed recovering ptp listen socket, %d", __func__, state);
		handle_relay_connect_failure();
	} else {
		handle_relay_connect_success();
	}

	INFO_LOG(Log::sceNet, "%s: ptp listen recovery for id %d: %p", __func__, idx + 1, internal->postofficeHandle);

	return internal->postofficeHandle;
}

// A PSP completes a connect to a listener by itself, without the game calling Accept, up to the
// listener's backlog (pspautotests net/adhoc/pair/ptppoll, pair/ptpclose). The relay only answers the
// connecting side once the listening side accepts, so connections are accepted from the relay into this
// queue as they arrive, and PtpAccept takes them from it.
struct RelayPendingConnection {
	void *handle;
	SceNetEtherAddr mac;
	int port;
};

struct RelayBacklog {
	int size = 1;
	std::deque<RelayPendingConnection> pending;
};

static std::map<const AdhocSocket *, RelayBacklog> relayBacklogs;

// Accepts what the relay has for a listener, while there's room in its backlog.
static void fillRelayBacklog(int idx) {
	AdhocSocket *listener = adhocSockets[idx];
	RelayBacklog &backlog = relayBacklogs[listener];
	while ((int)backlog.pending.size() < backlog.size) {
		void *ptp_listen_socket = ptp_listen_postoffice_recover(idx);
		if (ptp_listen_socket == NULL) {
			return;
		}
		int state = 0;
		RelayPendingConnection connection = {};
		connection.handle = ptp_accept(ptp_listen_socket, (char *)&connection.mac, &connection.port, true, NULL, NULL, &state);
		if (connection.handle == NULL) {
			if (state == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
				handle_relay_connect_failure();
				ptp_listen_close(listener->postofficeHandle);
				listener->postofficeHandle = NULL;
			}
			if (state == AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY) {
				// pretty critical here
				ERROR_LOG(Log::sceNet, "%s: aemu_postoffice ran out of memory to accept a new connection", __func__);
			}
			return;
		}
		handle_relay_connect_success();
		backlog.pending.push_back(connection);
	}
}

// Closes the connections a listener accepted that the game never took.
static void closeRelayBacklog(const AdhocSocket *listener) {
	auto it = relayBacklogs.find(listener);
	if (it == relayBacklogs.end()) {
		return;
	}
	for (auto &connection : it->second.pending) {
		ptp_close(connection.handle);
	}
	relayBacklogs.erase(it);
}

// The peer closed the connection, or the relay lost it. (A PSP's poll then reports DISCONNECT, pspautotests
// net/adhoc/poll.)
bool RelayTransport::PtpPeerClosed(AdhocSocket *sock) {
	return sock->postofficeHandle != NULL && ptp_is_dead(sock->postofficeHandle);
}

static void fillRelayBacklogs() {
	for (int i = 0; i < MAX_SOCKET; i++) {
		AdhocSocket *sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PTP && sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN && sock->postofficeHandle != NULL) {
			fillRelayBacklog(i);
		}
	}
}

// Whether a PTP connect the emulation hasn't waited for yet is still going. With markWaited, notes that it
// has now.
static bool relayConnectInFlight(bool markWaited = false) {
	bool found = false;
	for (int i = 0; i < MAX_SOCKET; i++) {
		AdhocSocket *sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PTP && sock->connectThread != NULL && !sock->connectThreadDone && !sock->connectThreadWaited) {
			found = true;
			if (markWaited) {
				sock->connectThreadWaited = true;
			}
		}
	}
	return found;
}

// How long the emulation waits for a PTP connect through the relay, in real time.
static const double RELAY_CONNECT_WAIT_SECONDS = 1.0;

void RelayTransport::Poll() {
	fillRelayBacklogs();

	// A connect takes a few round trips through the relay, in real time, and the PSP has an opened
	// connection up a few ms after PtpOpen (pspautotests net/adhoc/pair/ptppoll). So wait for it here, at
	// most a millisecond of emulated time after it started, instead of letting the emulation speed decide
	// when the game sees it. The listener may be on this console, so keep accepting meanwhile.
	if (!relayConnectInFlight()) {
		return;
	}
	// The connect completes when the other side accepts, so other instances kept in step with this one
	// mustn't wait for it meanwhile.
	InstanceSyncStandAside(true);
	double start = time_now_d();
	while (relayConnectInFlight()) {
		if (time_now_d() - start > RELAY_CONNECT_WAIT_SECONDS) {
			WARN_LOG(Log::sceNet, "Relay: a PTP connect took over %.1f s, no longer waiting for it", RELAY_CONNECT_WAIT_SECONDS);
			relayConnectInFlight(true);
			break;
		}
		sleep_ms(1, "relay-connect");
		fillRelayBacklogs();
	}
	InstanceSyncStandAside(false);
}

static int ptp_accept_postoffice(int idx, SceNetEtherAddr *saddr, uint16_t *sport) {
	fillRelayBacklog(idx);
	RelayBacklog &backlog = relayBacklogs[adhocSockets[idx]];
	if (backlog.pending.empty()) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}
	void *new_ptp_socket = backlog.pending.front().handle;
	SceNetEtherAddr mac_cpy = backlog.pending.front().mac;
	int port_cpy = backlog.pending.front().port;
	backlog.pending.pop_front();
	AdhocSocket *internal = (AdhocSocket *)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		ERROR_LOG(Log::sceNet, "%s: critical: ran out of heap memory while accepting new connection", __func__);
		ptp_close(new_ptp_socket);
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}
	memset(internal, 0, sizeof(AdhocSocket));
	internal->retry_interval = adhocSockets[idx]->retry_interval;
	internal->retry_count = adhocSockets[idx]->retry_count;
	internal->attemptCount = 1; // Connected, so a closed socket reports DISCONNECTED (as the native accept does)

	internal->type = SOCK_PTP;
	internal->postofficeHandle = new_ptp_socket;
	internal->data.ptp.laddr = adhocSockets[idx]->data.ptp.laddr;
	internal->data.ptp.lport = adhocSockets[idx]->data.ptp.lport;
	internal->data.ptp.paddr = mac_cpy;
	internal->data.ptp.pport = reverse_port_simple(port_cpy);
	ptpSetEstablished(internal);
	internal->data.ptp.rcv_sb_cc = adhocSockets[idx]->data.ptp.rcv_sb_cc;
	internal->data.ptp.snd_sb_cc = adhocSockets[idx]->data.ptp.snd_sb_cc;
	internal->flags = 0;
	internal->connectThread = NULL;

	AdhocSocket **slot = NULL;
	int i;
	i = FindFreeAdhocSocketSlot();
	if (i < MAX_SOCKET) {
		slot = &adhocSockets[i];
	}

	if (slot == NULL) {
		ptp_close(new_ptp_socket);
		free(internal);
		ERROR_LOG(Log::sceNet, "%s: critical: cannot find an empty mapper slot", __func__);
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	// Its own, not the listener's: blocking calls are told apart by it.
	internal->data.ptp.id = AEMU_POSTOFFICE_ID_BASE + i;
	*slot = internal;

	if (saddr != NULL) {
		*saddr = mac_cpy;
	}
	if (sport != NULL) {
		*sport = reverse_port_simple(port_cpy);
	}

	INFO_LOG(Log::sceNet, "%s: accepted ptp socket with id %d %p", __func__, i + 1, internal->postofficeHandle);
	return i + 1;
}

int RelayTransport::DoBlockingPtpAccept(AdhocSocketRequest &req, s64 &result) {
	int ret = ptp_accept_postoffice(req.id - 1, req.remoteMAC, req.remotePort);
	if (ret >= 0) {
		result = ret;
		return 0;
	}

	// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
	u64 now = CoreTiming::GetGlobalTimeUs();
	if (req.timeout == 0 || now - req.startTime <= req.timeout) {
		return -1;
	}
	result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", req.id, EAGAIN);
	return 0;
}

static int ptp_connect_postoffice(int idx, const char *caller) {
	if (relayDisabled) {
		return SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED;
	}

	AdhocSocket *internal = adhocSockets[idx];

	struct aemu_postoffice_sock_addr addr;
	auto server_ip = GetAdhocServerIP();
	addr.addr = server_ip.in.sin_addr.s_addr;
	addr.port = htons(AEMU_POSTOFFICE_PORT);

	if (internal->postofficeHandle != NULL) {
		return 0;
	}

	if (internal->connectThreadDone) {
		if (internal->connectThread != NULL) {
			internal->connectThread->join();
			delete internal->connectThread;
			internal->connectThread = NULL;
		}

		internal->connectThreadDone = false;
		internal->connectThreadResult = 0;
		internal->connectThreadWaited = false;

		internal->connectThread = new std::thread([internal, addr, idx] {
			int state;
			SceNetEtherAddr fixed_daddr = internal->data.ptp.paddr;
			fixGameMac(&fixed_daddr);
			void *ptp_socket = ptp_connect_v4(&addr, (const char *)&internal->data.ptp.laddr, offset_port_simple(internal->data.ptp.lport), (const char *)&fixed_daddr, offset_port_simple(internal->data.ptp.pport), netAdhocUseUdp, NULL, NULL, &state);
			if (ptp_socket == NULL) {
				internal->connectThreadResult = SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED;
				ERROR_LOG(Log::sceNet, "%s: failed connecting to ptp socket, %d", __func__, state);
				// do not count ptp connect failure as relay failure, since it could be the other client not accepting the connection
				//handle_relay_connect_failure();
				internal->connectThreadDone = true;
				return;
			}
			// see above, ptp connect result is not used for checking if relay is working
			//handle_relay_connect_success();
			internal->postofficeHandle = ptp_socket;
			internal->connectThreadResult = 0;
			INFO_LOG(Log::sceNet, "%s: connected ptp socket with id %d %p", __func__, idx + 1, internal->postofficeHandle);
			internal->connectThreadDone = true;
			return;
		});;

		DEBUG_LOG(Log::sceNet, "%s: started connect thread on id %d", __func__, idx + 1);
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	DEBUG_LOG(Log::sceNet, "%s: id %d is connecting", __func__, idx + 1);
	return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
}

bool RelayTransport::PtpConnectionUp(AdhocSocket *sock) {
	return sock->postofficeHandle != NULL;
}

int RelayTransport::PtpConnect(int id, const sockaddr_in &sin, int *error) {
	if (ptp_connect_postoffice(id - 1, __func__) != 0) {
		*error = EAGAIN;
		return SOCKET_ERROR;
	}
	return 0;
}

int RelayTransport::PtpConnectPoll(AdhocSocket *sock, int *error) {
	if (sock->postofficeHandle != NULL) {
		// the handle is there, we're ready to go
		*error = 0;
		return 1;
	}
	if (sock->connectThreadDone && sock->connectThreadResult == SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED) {
		*error = ECONNREFUSED;
	} else {
		*error = EAGAIN;
	}
	return SOCKET_ERROR;
}

static int pdp_create_postoffice(const SceNetEtherAddr *saddr, int sport, int bufsize, bool enet_reliable) {
	if (relayDisabled) {
		return hleLogDebug(Log::sceNet, ERROR_NET_NO_SPACE, "relay disabled");
	}

	AdhocSocket *internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		return hleLogDebug(Log::sceNet, ERROR_NET_NO_SPACE, "net no space");
	}
	memset(internal, 0, sizeof(AdhocSocket));

	internal->type = SOCK_PDP;
	internal->postofficeHandle = NULL;
	internal->data.pdp.laddr = *saddr;
	internal->data.pdp.lport = sport;
	internal->data.pdp.rcv_sb_cc = 0;
	internal->buffer_size = bufsize;
	internal->flags = 0;
	internal->lastAttempt = 0;
	internal->internalLastAttempt = 0;
	internal->postofficePdpEnetReliable = enet_reliable;

	AdhocSocket **free_slot = NULL;
	int i;
	i = FindFreeAdhocSocketSlot();
	if (i < MAX_SOCKET) {
		free_slot = &adhocSockets[i];
	}
	if (free_slot == NULL) {
		free(internal);
		return ERROR_NET_NO_SPACE;
	}

	internal->data.pdp.id = AEMU_POSTOFFICE_ID_BASE + i;

	*free_slot = internal;
	pdp_postoffice_recover(i);
	INFO_LOG(Log::sceNet, "%s: created pdp socket with id %d %p", __func__, i + 1, internal->postofficeHandle);
	return i + 1;
}

int RelayTransport::PdpCreate(const SceNetEtherAddr *saddr, int port, int bufferSize, u32 flag, bool isClient, bool enetReliable) {
	return pdp_create_postoffice(saddr, port, bufferSize, enetReliable);
}

static bool postoffice_can_recv_or_listen_has_request(int idx) {
	AdhocSocket *internal = adhocSockets[idx];
	if (internal->type == SOCK_PTP) {
		if (internal->data.ptp.state == ADHOC_PTP_STATE_LISTEN) {
			fillRelayBacklog(idx);
			return !relayBacklogs[internal].pending.empty();
		} else if (internal->data.ptp.state == ADHOC_PTP_STATE_ESTABLISHED) {
			void *ptp_handle = internal->postofficeHandle;
			if (ptp_handle == NULL) {
				return false;
			}
			int peek_len = ptp_peek_next_size(ptp_handle);
			// A closed connection reads as readable, so the poll looks at PtpPeerClosed.
			if (peek_len == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
				return true;
			}
			if (peek_len > 0) {
				return true;
			}
		}
	} else {
		void *pdp_handle = pdp_postoffice_recover(idx);
		if (pdp_handle == NULL) {
			return false;
		}
		int peek_len = pdp_buffered_data_size(pdp_handle);
		if (peek_len == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
			internal->postofficeHandle = NULL;
			pdp_delete(pdp_handle);
			return false;
		}
		if (peek_len > 0) {
			return true;
		}
	}
	return false;
}

static bool postoffice_can_send(int idx) {
	AdhocSocket *internal = adhocSockets[idx];
	if (internal->type == SOCK_PTP) {
		// A listener's or unconnected socket's handle isn't a PDP session (pspautotests net/adhoc/pair/ptppoll).
		if (internal->data.ptp.state != ADHOC_PTP_STATE_ESTABLISHED) {
			return false;
		}
		void *ptp_handle = internal->postofficeHandle;
		if (ptp_handle == NULL) {
			return false;
		}
		if (ptp_is_dead(ptp_handle)) {
			return false;
		}
		return ptp_send_buf_not_full(ptp_handle);
	} else {
		void *pdp_handle = pdp_postoffice_recover(idx);
		if (pdp_handle == NULL) {
			return false;
		}
		if (pdp_is_dead(pdp_handle)) {
			internal->postofficeHandle = NULL;
			pdp_delete(pdp_handle);
			return false;
		}
		return pdp_send_buf_not_full(pdp_handle);
	}
	return false;
}

int RelayTransport::PollWait(SceNetAdhocPollSd *sds, int count, int timeout, AdhocPollState *states) {
	for (int i = 0; i < count; i++) {
		if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
			states[i].readable = postoffice_can_recv_or_listen_has_request(sds[i].id - 1);
			states[i].writable = (sds[i].events & ADHOC_EV_SEND) && postoffice_can_send(sds[i].id - 1);
		}
	}
	return 0;
}

static int pdp_delete_postoffice(int idx) {
	AdhocSocket *internal = adhocSockets[idx];
	if (internal->postofficeHandle != NULL) {
		pdp_delete(internal->postofficeHandle);
	}
	adhocSockets[idx] = NULL;
	free(internal);
	INFO_LOG(Log::sceNet, "%s: closed pdp socket with id %d", __func__, idx + 1);
	return 0;
}

int RelayTransport::PdpDelete(int id) {
	return pdp_delete_postoffice(id - 1);
}

void RelayTransport::PtpUpdateRecvStat(AdhocSocket *sock) {
	// A listener's handle is a listen session, which has no data.
	int waiting = 0;
	if (sock->postofficeHandle != NULL && sock->data.ptp.state != ADHOC_PTP_STATE_LISTEN) {
		waiting = ptp_peek_next_size(sock->postofficeHandle);
	}
	sock->data.ptp.rcv_sb_cc = std::max(0, waiting);
}

static int ptp_open_postoffice(const SceNetEtherAddr *saddr, uint16_t sport, const SceNetEtherAddr *daddr, uint16_t dport, uint32_t bufsize, uint32_t rexmt_int, int rexmt_cnt) {
	AdhocSocket *internal = (AdhocSocket *)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		ERROR_LOG(Log::sceNet, "%s: ran out of heap memory trying to open ptp socket", __func__);
		return ERROR_NET_NO_SPACE;
	}
	memset(internal, 0, sizeof(AdhocSocket));
	internal->retry_interval = rexmt_int;
	internal->retry_count = rexmt_cnt;

	internal->type = SOCK_PTP;
	internal->postofficeHandle = NULL;
	internal->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
	internal->data.ptp.laddr = *saddr;
	internal->data.ptp.lport = sport == 0 ? get_random_unused_ptp_port() : sport;
	internal->data.ptp.paddr = *daddr;
	internal->data.ptp.pport = dport;
	internal->data.ptp.rcv_sb_cc = bufsize;
	internal->data.ptp.snd_sb_cc = 0;
	internal->flags = 0;
	internal->connectThread = NULL;
	internal->connectThreadDone = true;
	internal->lastAttempt = 0;
	internal->internalLastAttempt = 0;

	AdhocSocket **slot = NULL;
	int i;
	i = FindFreeAdhocSocketSlot();
	if (i < MAX_SOCKET) {
		slot = &adhocSockets[i];
	}

	if (slot == NULL) {
		free(internal);
		ERROR_LOG(Log::sceNet, "%s: cannot find free socket mapper slot while opening ptp socket", __func__);
		return ERROR_NET_NO_SPACE;
	}

	internal->data.ptp.id = AEMU_POSTOFFICE_ID_BASE + i;

	*slot = internal;
	INFO_LOG(Log::sceNet, "%s: created ptp socket with id %d", __func__, i + 1);

	// PtpOpen starts connecting by itself: on the PSP the client is ESTABLISHED right after it (pspautotests
	// net/adhoc/ptp). The Warriors sends without calling PtpConnect.
	NetAdhocPtp_Connect(i + 1, 0, 1, false);

	return i + 1;
}

int RelayTransport::PtpOpen(const SceNetEtherAddr *saddr, int sport, const SceNetEtherAddr *daddr, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) {
	return ptp_open_postoffice(saddr, sport, daddr, dport, bufsize, rexmt_int, rexmt_cnt);
}

int RelayTransport::PtpAccept(int id, SceNetEtherAddr *addr, uint16_t *port, int timeout, int flag) {
	int newsocket = ptp_accept_postoffice(id - 1, addr, port);
	if (newsocket >= 0) {
		return newsocket;
	}

	// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
	if (flag == 0) {
		// Simulate blocking behaviour with non-blocking socket
		u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | adhocSockets[id - 1]->data.ptp.id;
		return WaitBlockingAdhocSocket(threadSocketId, PTP_ACCEPT, id, nullptr, nullptr, timeout, addr, port, "ptp accept");
	}
	// Prevent spamming Debug Log with retries of non-bocking socket
	VERBOSE_LOG(Log::sceNet, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", id, EAGAIN);
	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");
}

static int ptp_close_postoffice(int idx) {
	AdhocSocket *internal = adhocSockets[idx];

	// sync
	if (internal->connectThread != NULL) {
		internal->connectThread->join();
		delete internal->connectThread;
	}

	void *socket = internal->postofficeHandle;
	if (socket != NULL) {
		if (internal->data.ptp.state == ADHOC_PTP_STATE_LISTEN) {
			ptp_listen_close(socket);
		} else {
			ptp_close(socket);
		}
	}
	closeRelayBacklog(internal);
	adhocSockets[idx] = NULL;
	free(internal);
	INFO_LOG(Log::sceNet, "%s: closed ptp socket with id %d", __func__, idx + 1);
	return 0;
}

int RelayTransport::PtpClose(int id) {
	return ptp_close_postoffice(id - 1);
}

static int ptp_listen_postoffice(const SceNetEtherAddr *saddr, uint16_t sport, uint32_t bufsize, uint32_t rexmt_int, int rexmt_cnt) {
	if (relayDisabled) {
		return ERROR_NET_NO_SPACE;
	}

	// server connect delegated to accept
	AdhocSocket *internal = (AdhocSocket *)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		ERROR_LOG(Log::sceNet, "%s: out of heap memory when creating ptp listen socket", __func__);
		return ERROR_NET_NO_SPACE;
	}
	memset(internal, 0, sizeof(AdhocSocket));
	internal->retry_interval = rexmt_int;
	internal->retry_count = rexmt_cnt;

	internal->postofficeHandle = NULL;
	internal->type = SOCK_PTP;
	internal->data.ptp.laddr = *saddr;
	internal->data.ptp.lport = sport;
	internal->data.ptp.state = ADHOC_PTP_STATE_LISTEN;
	internal->data.ptp.rcv_sb_cc = bufsize;
	internal->data.ptp.snd_sb_cc = 0;
	internal->flags = 0;
	internal->connectThread = NULL;
	internal->lastAttempt = 0;
	internal->internalLastAttempt = 0;

	AdhocSocket **slot = NULL;
	int i;
	i = FindFreeAdhocSocketSlot();
	if (i < MAX_SOCKET) {
		slot = &adhocSockets[i];
	}

	if (slot == NULL) {
		ERROR_LOG(Log::sceNet, "%s: out of socket slots when creating adhoc ptp listen socket", __func__);
		free(internal);
		return ERROR_NET_NO_SPACE;
	}

	internal->data.ptp.id = AEMU_POSTOFFICE_ID_BASE + i;

	*slot = internal;
	ptp_listen_postoffice_recover(i);
	INFO_LOG(Log::sceNet, "%s: created ptp listen socket with id %d %p", __func__, i + 1, internal->postofficeHandle);
	return i + 1;
}

int RelayTransport::PtpListen(const SceNetEtherAddr *saddr, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, bool isClient, int flag) {
	int id = ptp_listen_postoffice(saddr, sport, bufsize, rexmt_int, rexmt_cnt);
	if (id > 0) {
		relayBacklogs[adhocSockets[id - 1]].size = std::max(1, backlog);
	}
	return id;
}

int RelayTransport::PtpSend(int id, const char *data, int *len, int timeout, int flag) {
	auto& ptpsocket = adhocSockets[id - 1]->data.ptp;
	int sent = ptp_send_postoffice(id - 1, data, len);
	if (sent == 0) {
		hleEatMicro(122);
		return 0;
	}
	if (sent == SOCKET_ERROR) {
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
		return SCE_NET_ADHOC_ERROR_DISCONNECTED;
	}

	// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
	if (flag)
		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

	// Simulate blocking behaviour with non-blocking socket
	u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
	return WaitBlockingAdhocSocket(threadSocketId, PTP_SEND, id, (void*)data, len, timeout, nullptr, nullptr, "ptp send");
}

int RelayTransport::PtpRecv(int id, void *buf, int *len, int timeout, int flag) {
	auto& ptpsocket = adhocSockets[id - 1]->data.ptp;
	int received = ptp_recv_postoffice(id - 1, buf, len);
	if (received == 0) {
		// we got data
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, received, 0);
		hleEatMicro(50);
		return 0;
	}
	if (received == SOCKET_ERROR) {
		// the socket died, let the game know
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, received, 0);
		return SCE_NET_ADHOC_ERROR_DISCONNECTED;
	}

	// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
	if (flag == 0) {
		// Simulate blocking behaviour with non-blocking socket
		u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
		return WaitBlockingAdhocSocket(threadSocketId, PTP_RECV, id, buf, len, timeout, nullptr, nullptr, "ptp recv");
	}

	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");
}

int RelayTransport::PtpFlush(int socketId) {
	// there's no manual flushing in the relay library, it's by default no delay there
	return 0;
}

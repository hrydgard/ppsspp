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

#include "Core/HLE/proAdhoc.h"
#include <chrono>
#include <vector>

#include "Core/HLE/sceNetAdhoc.h"

// What a transport found when polling a socket.
struct AdhocPollState {
	bool readable = false;
	bool writable = false;
	bool error = false;
};

// How ad hoc sockets reach the other consoles: through the aemu_postoffice relay, or directly over the
// host's own UDP/TCP sockets (P2P). The sceNetAdhoc functions do everything a PSP does the same either
// way, and call one of these for the parts that depend on the transport. Transport() picks the one in
// use, which follows serverHasRelay.
//
// Ids are PSP socket IDs (1-based slots in adhocSockets). Unless said otherwise, the arguments, the
// socket and its alerts have been checked.
class AdhocTransport {
public:
	virtual ~AdhocTransport() {}

	// Frees the host side of a socket (not the AdhocSocket), when all are deleted at once.
	virtual void CloseHostSocket(AdhocSocket *sock) = 0;
	// Called every millisecond of emulated time while the ad hoc server connection is up, for work that
	// happens on a PSP whatever the game calls.
	virtual void Poll() {}
	// Whether the connection to a local ad hoc server should be bound to our own local IP, so that
	// instances on one machine look like different consoles to it.
	virtual bool BindServerConnectionToLocalIp() = 0;

	// PDP

	// Opens the host side of a PDP socket and registers it in adhocSockets. Returns the socket ID, or an
	// error.
	virtual int PdpCreate(const SceNetEtherAddr *saddr, int port, int bufferSize, u32 flag, bool isClient, bool enetReliable) = 0;
	// Closes it and frees its slot.
	virtual int PdpDelete(int id) = 0;
	// Sends one datagram to one peer, whose address resolveMAC() has filled into target. Returns the
	// number of bytes sent, or SOCKET_ERROR with an errno value in *error.
	virtual int PdpSendTo(int id, const SceNetEtherAddr *mac, uint16_t dport, const sockaddr_in &target, const void *data, int len, int *error) = 0;
	// Takes the next datagram that has arrived from someone in the group, without waiting: its sender,
	// the sender's port and its data. False if there's none. (Received datagrams go into the socket's
	// queue in sceNetAdhoc.cpp, which PdpRecv and GetPdpStat read.)
	virtual bool PdpTakeDatagram(int id, SceNetEtherAddr *mac, uint16_t *port, std::vector<u8> &data) = 0;
	// Whether game mode should try sending from its socket now.
	virtual bool PdpReadyToSend(AdhocSocket *sock) = 0;

	// PTP

	// The host side of sceNetAdhocPtpOpen and sceNetAdhocPtpListen once the ports have been checked.
	virtual int PtpOpen(const SceNetEtherAddr *saddr, int sport, const SceNetEtherAddr *daddr, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) = 0;
	virtual int PtpListen(const SceNetEtherAddr *saddr, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, bool isClient, int flag) = 0;
	// Closes it and frees its slot.
	virtual int PtpClose(int id) = 0;
	// The rest of sceNetAdhocPtpSend / PtpRecv / PtpAccept, and their blocking retries.
	virtual int PtpSend(int id, const char *data, int *len, int timeout, int flag) = 0;
	virtual int PtpRecv(int id, void *buf, int *len, int timeout, int flag) = 0;
	virtual int PtpAccept(int id, SceNetEtherAddr *addr, uint16_t *port, int timeout, int flag) = 0;
	virtual int DoBlockingPtpSend(AdhocSocketRequest &req, s64 &result) = 0;
	virtual int DoBlockingPtpRecv(AdhocSocketRequest &req, s64 &result) = 0;
	virtual int DoBlockingPtpAccept(AdhocSocketRequest &req, s64 &result) = 0;
	// Starts connecting to sin. Returns 0 if connected, or SOCKET_ERROR with an errno value in *error.
	virtual int PtpConnect(int id, const sockaddr_in &sin, int *error) = 0;
	// Checks on a connect in progress: > 0 when it can be verified, or SOCKET_ERROR / 0 with *error.
	virtual int PtpConnectPoll(AdhocSocket *sock, int *error) = 0;
	// Confirms a connection PtpConnect or PtpConnectPoll reported (ret), filling in the peer's address.
	virtual int PtpConnectVerify(int id, sockaddr_in *sin, int ret, int *error) = 0;
	// Whether a connect that failed with error is still on its way.
	virtual bool PtpConnectPending(int error) = 0;
	// Whether the connection a socket is making is up now.
	virtual bool PtpConnectionUp(AdhocSocket *sock) = 0;
	// Whether the connection PtpOpen started has come up (checked before connecting again).
	virtual bool PtpConnectedSinceOpen(AdhocSocket *sock) = 0;
	// Updates rcv_sb_cc, the bytes waiting to be received, for sceNetAdhocGetPtpStat.
	virtual void PtpUpdateRecvStat(AdhocSocket *sock) = 0;
	// Sends what's buffered. Returns 0 or an errno value.
	virtual int PtpFlush(int socketId) = 0;

	// Polling

	// Fills in states[i] for each valid socket in sds, waiting up to timeout (microseconds) where the
	// transport can wait. Returns a negative value on failure.
	virtual int PollWait(SceNetAdhocPollSd *sds, int count, int timeout, AdhocPollState *states) = 0;
	// For a PTP socket still connecting: notices the connection coming up, or being refused.
	virtual void PollCheckConnecting(int id, AdhocSocket *sock, const AdhocPollState &state) = 0;
	// For a readable connected PTP socket: whether that's because the peer closed it.
	virtual bool PtpPeerClosed(AdhocSocket *sock) = 0;
};

// Shared with the transports, from sceNetAdhoc.cpp.

#define AEMU_POSTOFFICE_PORT 27313
#define AEMU_POSTOFFICE_ID_BASE 2048
#define RELAY_FAILURE_DISABLE_THRESHOLD_SEC 15

extern std::chrono::time_point<std::chrono::steady_clock> relayLastFailure;
extern bool trackingRelayFailure;
extern bool relayDisabled;
extern bool relayFirstConnect;

uint16_t offset_port_simple(uint16_t port);
uint16_t reverse_port_simple(uint16_t port);
bool offAdhocNetwork();
bool isLocalMac(const SceNetEtherAddr *mac);
void ptpSetEstablished(AdhocSocket *sock);
void ptpSetRefused(AdhocSocket *sock);
void pdpDropAllQueues();
bool ptpPeerGone(const AdhocSocket *sock);
void ptpNoteSend(AdhocSocket *sock);
// The next free slot in adhocSockets, or MAX_SOCKET.
int FindFreeAdhocSocketSlot();
uint16_t get_unused_pdp_port();
uint16_t get_random_unused_ptp_port();
int WaitBlockingAdhocSocket(u64 threadSocketId, int type, int pspSocketId, void* buffer, s32_le* len, u32 timeoutUS, SceNetEtherAddr* remoteMAC, u16_le* remotePort, const char* reason);
int AcceptPtpSocket(int ptpId, int newsocket, sockaddr_in& peeraddr, SceNetEtherAddr* addr, u16_le* port);
int RecreatePtpSocket(int ptpId);
int NetAdhocPtp_Connect(int id, int timeout, int flag, bool allowForcedConnect = true);

// In AdhocTransportRelay.cpp and AdhocTransportP2P.cpp.
class RelayTransport : public AdhocTransport {
public:
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

	bool BindServerConnectionToLocalIp() override { return false; }
	bool PdpReadyToSend(AdhocSocket *sock) override { return true; }
	// A connection's handle only comes once it's up, so there's no in-between to check.
	int PtpConnectVerify(int id, sockaddr_in *sin, int ret, int *error) override { return ret; }
	bool PtpConnectPending(int error) override { return true; }
	bool PtpConnectedSinceOpen(AdhocSocket *sock) override { return false; }
	void PollCheckConnecting(int id, AdhocSocket *sock, const AdhocPollState &state) override {}
	bool PtpPeerClosed(AdhocSocket *sock) override;
};

class P2PTransport : public AdhocTransport {
public:
	void CloseHostSocket(AdhocSocket *sock) override;

	int PdpCreate(const SceNetEtherAddr *saddr, int port, int bufferSize, u32 flag, bool isClient, bool enetReliable) override;
	int PdpDelete(int id) override;
	int PdpSendTo(int id, const SceNetEtherAddr *mac, uint16_t dport, const sockaddr_in &target, const void *data, int len, int *error) override;
	bool PdpTakeDatagram(int id, SceNetEtherAddr *mac, uint16_t *port, std::vector<u8> &data) override;

	int PtpOpen(const SceNetEtherAddr *saddr, int sport, const SceNetEtherAddr *daddr, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) override;
	int PtpListen(const SceNetEtherAddr *saddr, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, bool isClient, int flag) override;
	int PtpClose(int id) override;
	int PtpSend(int id, const char *data, int *len, int timeout, int flag) override;
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

	bool BindServerConnectionToLocalIp() override { return true; }
	bool PdpReadyToSend(AdhocSocket *sock) override;
	int PtpConnectVerify(int id, sockaddr_in *sin, int ret, int *error) override;
	bool PtpConnectPending(int error) override;
	bool PtpConnectedSinceOpen(AdhocSocket *sock) override;
	void PollCheckConnecting(int id, AdhocSocket *sock, const AdhocPollState &state) override;
	bool PtpPeerClosed(AdhocSocket *sock) override;
};

AdhocTransport &Transport();

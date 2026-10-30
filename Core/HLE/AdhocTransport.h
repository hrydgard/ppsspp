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
#include "Core/HLE/sceNetAdhoc.h"

// What a transport found when polling a socket.
struct AdhocPollState {
	bool readable = false;
	bool writable = false;
	bool error = false;
	// An alert raised on the socket earlier (P2P reports it in poll, the relay doesn't).
	bool alerted = false;
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
	// The rest of sceNetAdhocPdpRecv.
	virtual int PdpRecv(int id, SceNetEtherAddr *saddr, uint16_t *sport, void *buf, int *len, u32 timeout, int flag) = 0;
	// The rest of a blocking receive's retry.
	virtual int DoBlockingPdpRecv(AdhocSocketRequest &req, s64 &result) = 0;
	// Updates rcv_sb_cc, the bytes waiting to be received, for sceNetAdhocGetPdpStat.
	virtual void PdpUpdateRecvStat(int id, AdhocSocket *sock) = 0;
	// Whether game mode should try sending to / receiving from its socket now.
	virtual bool PdpReadyToSend(AdhocSocket *sock) = 0;
	virtual bool PdpReadyToRecv(AdhocSocket *sock) = 0;

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

AdhocTransport &Transport();

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

// How ad hoc sockets reach the other consoles: through the aemu_postoffice relay, or directly over the
// host's own UDP/TCP sockets (P2P). The sceNetAdhoc functions do everything a PSP does the same either
// way, and call one of these for the parts that depend on the transport. Transport() picks the one in
// use, which follows serverHasRelay.
class AdhocTransport {
public:
	virtual ~AdhocTransport() {}

	// PDP

	// Opens the host side of a PDP socket and registers it in adhocSockets. Returns the socket ID, or an
	// error. The arguments have been checked.
	virtual int PdpCreate(const SceNetEtherAddr *saddr, int port, int bufferSize, u32 flag, bool isClient, bool enetReliable) = 0;
	// Closes it and frees its slot.
	virtual int PdpDelete(int id) = 0;
	// Sends one datagram to one peer, whose address resolveMAC() has filled into target. Returns the
	// number of bytes sent, or SOCKET_ERROR with an errno value in *error.
	virtual int PdpSendTo(int id, const SceNetEtherAddr *mac, uint16_t dport, const sockaddr_in &target, const void *data, int len, int *error) = 0;
	// The rest of sceNetAdhocPdpRecv once the arguments, the socket and its alerts have been checked.
	virtual int PdpRecv(int id, SceNetEtherAddr *saddr, uint16_t *sport, void *buf, int *len, u32 timeout, int flag) = 0;
	// The rest of a blocking receive's retry, after the socket and its alerts have been checked.
	virtual int DoBlockingPdpRecv(AdhocSocketRequest &req, s64 &result) = 0;
	// Updates rcv_sb_cc, the bytes waiting to be received, for sceNetAdhocGetPdpStat.
	virtual void PdpUpdateRecvStat(int id, AdhocSocket *sock) = 0;
	// Whether game mode should try sending to / receiving from its socket now.
	virtual bool PdpReadyToSend(AdhocSocket *sock) = 0;
	virtual bool PdpReadyToRecv(AdhocSocket *sock) = 0;
};

AdhocTransport &Transport();

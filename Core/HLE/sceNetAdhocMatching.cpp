// Copyright (c) 2025- PPSSPP Project.

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


#include "Core/HLE/sceNetAdhocMatching.h"
#include "Core/HLE/sceNetAdhoc.h"

#include <deque>
#include <algorithm>


#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/MemMapHelpers.h"
#include "Common/Serialize/SerializeFuncs.h"

#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/ErrorCodes.h"


std::vector<SceUID> matchingThreads;
std::deque<MatchingArgs> matchingEvents;

bool netAdhocMatchingInited;
static bool savedNetAdhocMatchingInited; // the storage to use during the savestating routine in sceNetAdhoc.cpp

void DoNetAdhocMatchingInited(PointerWrap &p) {
	Do(p, netAdhocMatchingInited);
}

void DoNetAdhocMatchingThreads(PointerWrap &p) {
	Do(p, matchingThreads);
}

void ZeroNetAdhocMatchingThreads() {
	for (auto& it : matchingThreads) {
		it = 0;
	}
}

void SaveNetAdhocMatchingInited() {
	savedNetAdhocMatchingInited = netAdhocMatchingInited;
}

void RestoreNetAdhocMatchingInited() {
	netAdhocMatchingInited = savedNetAdhocMatchingInited;
}

int netAdhocMatchingStarted = 0;

constexpr u32 defaultLastRecvDelta = 10000; //10000 usec worked well for games published by Falcom (ie. Ys vs Sora Kiseki, Vantage Master Portable)


void __UpdateMatchingHandler(const MatchingArgs &ArgsPtr) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	matchingEvents.push_back(ArgsPtr);
}

// Matching callback is void function: typedef void(*SceNetAdhocMatchingHandler)(int id, int event, SceNetEtherAddr * peer, int optlen, void * opt);
// Important! The MIPS call need to be fully executed before the next MIPS call invoked, as the game (ie. DBZ Tag Team) may need to prepare something for the next callback event to use
// Note: Must not lock peerlock within this function to prevent race-condition with other thread whos owning peerlock and trying to lock context->eventlock owned by this thread
void notifyMatchingHandler(SceNetAdhocMatchingContext * context, ThreadMessage * msg, void * opt, u32_le &bufAddr, u32_le &bufLen, u32_le * args) {
	// Don't share buffer address space with other mipscall in the queue since mipscalls aren't immediately executed
	MatchingArgs argsNew = { 0 };
	u32_le dataBufLen = msg->optlen + 8; //max(bufLen, msg->optlen + 8);
	u32_le dataBufAddr = userMemory.Alloc(dataBufLen); // We will free this memory after returning from mipscall. FIXME: Are these buffers supposed to be taken/pre-allocated from the memory pool during sceNetAdhocMatchingInit?
	uint8_t *dataPtr = Memory::GetPointerWriteRange(dataBufAddr, dataBufLen);
	if (dataPtr) {
		memcpy(dataPtr, &msg->mac, sizeof(msg->mac));
		if (msg->optlen > 0)
			memcpy(dataPtr + 8, opt, msg->optlen);

		argsNew.data[1] = msg->opcode;
		argsNew.data[2] = dataBufAddr;
		argsNew.data[3] = msg->optlen;
		argsNew.data[4] = dataBufAddr + 8; // OptData Addr
	}
	else {
		argsNew.data[1] = PSP_ADHOC_MATCHING_EVENT_ERROR; // not sure where to put the error code for EVENT_ERROR tho
		//argsNew.data[2] = dataBufAddr; // FIXME: Is the MAC address mandatory (ie. can't be null pointer) even for EVENT_ERROR? Where should we put this MAC data in the case we failed to allocate the memory? may be on the memory pool?
	}
	argsNew.data[0] = context->id;
	argsNew.data[5] = context->handler.entryPoint; //not part of callback argument, just borrowing a space to store callback address so i don't need to search the context first later

	// ScheduleEvent_Threadsafe_Immediate seems to get mixed up with interrupt (returning from mipscall inside an interrupt) and getting invalid address before returning from interrupt
	__UpdateMatchingHandler(argsNew);
}

// Using matchingId = -1 to delete all matching events
void deleteMatchingEvents(const int matchingId = -1) {
	for (auto it = matchingEvents.begin(); it != matchingEvents.end(); ) {
		if (matchingId < 0 || it->data[0] == matchingId) {
			if (Memory::IsValidAddress(it->data[2]))
				userMemory.Free(it->data[2]);
			it = matchingEvents.erase(it);
		}
		else
			++it;
	}
}


/**
* Broadcast Ping Message to other Matching Users
* @param context Matching Context Pointer
*/
void broadcastPingMessage(SceNetAdhocMatchingContext * context) {
	// Ping Opcode
	uint8_t ping = PSP_ADHOC_MATCHING_PACKET_PING;

	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Send Broadcast
	// FIXME: Not sure whether this PING supposed to be sent only to AdhocMatching members or to everyone in Adhocctl Group, since we already pinging the AdhocServer to avoid getting kicked out of Adhocctl Group
	auto peer = friends; // Use context->peerlist if only need to send to AdhocMatching members
	for (; peer != NULL; peer = peer->next) {
		// Skipping soon to be removed peer
		if (peer->last_recv == 0)
			continue;

		u16_le port = context->port;
		auto it = (*context->peerPort).find(peer->mac_addr);
		if (it != (*context->peerPort).end())
			port = it->second;

		context->socketlock->lock();
		hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)&peer->mac_addr, port, &ping, (u32)sizeof(ping), 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();
	}
}

/**
* Broadcast Hello Message to other Matching Users
* @param context Matching Context Pointer
*/
void broadcastHelloMessage(SceNetAdhocMatchingContext * context) {
	static uint8_t * hello = NULL;
	static int32_t len = -5;

	// Allocate Hello Message Buffer, reuse when necessary
	if ((int32_t)context->hellolen > len) {
		uint8_t* tmp = (uint8_t *)realloc(hello, 5LL + context->hellolen);
		if (tmp != NULL) {
			hello = tmp;
			len = context->hellolen;
		}
	}

	if (hello == NULL) {
		// Failed to allocate the Hello Message Buffer
		return;
	}

	// Hello Opcode
	hello[0] = PSP_ADHOC_MATCHING_PACKET_HELLO;

	// Hello Data Length (have to memcpy this to avoid cpu alignment crash)
	memcpy(hello + 1, &context->hellolen, sizeof(context->hellolen));

	// FIXME: When using JPCSP + prx files the data being sent have a header of 12 bytes instead of 5 bytes: 
	// [01(always 1? size of the next data? or combined with next byte as U16_BE opcode?) 01(matching opcode, or combined with previous byte as U16_BE opcode?) 01 E0(size of next data + hello data in big-endian/U16_BE) 00 0F 42 40(U32_BE? time?) 00 0F 42 40(U32_BE? time?)], 
	// followed by hello data (0x1D8 bytes of opt data, based on Ys vs. Sora no Kiseki), and followed by 16 bytes of (optional?) footer [01 00 00 .. 00 00](footer doesn't exist if the size after opcode is 00 00)

	// Copy Hello Data
	if (context->hellolen > 0) memcpy(hello + 5, context->hello, context->hellolen);

	std::string hellohex;
	DataToHexString(10, 0, context->hello, context->hellolen, &hellohex);
	DEBUG_LOG(Log::sceNet, "HELLO Dump (%d bytes):\n%s", context->hellolen, hellohex.c_str());

	// Send Broadcast, so everyone know we have a room here
	peerlock.lock();
	SceNetAdhocctlPeerInfo* peer = friends;
	for (; peer != NULL; peer = peer->next) {
		// Skipping soon to be removed peer
		if (peer->last_recv == 0)
			continue;

		u16_le port = context->port;
		auto it = (*context->peerPort).find(peer->mac_addr);
		if (it != (*context->peerPort).end())
			port = it->second;

		context->socketlock->lock();
		hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)&peer->mac_addr, port, hello, 5 + context->hellolen, 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();
	}
	peerlock.unlock();
}

/**
* Send Accept Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendAcceptPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int optlen, void * opt) {
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	if (peer == NULL || (peer->state != PSP_ADHOC_MATCHING_PEER_CHILD && peer->state != PSP_ADHOC_MATCHING_PEER_P2P)) {
		// Not found
		return;
	}

	// Required Sibling Buffer
	uint32_t siblingbuflen = 0;

	// Parent Mode
	if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) siblingbuflen = (u32)sizeof(SceNetEtherAddr) * (countConnectedPeers(context) - 2);

	// Sibling Count
	int siblingcount = siblingbuflen / sizeof(SceNetEtherAddr);

	// Allocate Accept Message Buffer
	uint8_t * accept = (uint8_t *)malloc(9LL + optlen + siblingbuflen);

	if (accept == NULL) {
		// Failed to allocate the Accept Message Buffer
		return;
	}

	// Accept Opcode
	accept[0] = PSP_ADHOC_MATCHING_PACKET_ACCEPT;

	// Optional Data Length
	memcpy(accept + 1, &optlen, sizeof(optlen));

	// Sibling Count
	memcpy(accept + 5, &siblingcount, sizeof(siblingcount));

	// Copy Optional Data
	if (optlen > 0) memcpy(accept + 9, opt, optlen);

	// Parent Mode Extra Data required
	if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && siblingcount > 0) {
		// Create MAC Array Pointer
		uint8_t * siblingmacs = (uint8_t *)(accept + 9 + optlen);

		// MAC Writing Pointer
		int i = 0;

		// Iterate Peer List
		SceNetAdhocMatchingMemberInternal * item = context->peerlist;
		for (; item != NULL; item = item->next) {
			// Ignore Target
			if (item == peer) continue;

			// Copy Child MAC
			if (item->state == PSP_ADHOC_MATCHING_PEER_CHILD) {
				// Clone MAC the stupid memcpy way to shut up PSP CPU
				memcpy(siblingmacs + sizeof(SceNetEtherAddr) * i++, &item->mac, sizeof(SceNetEtherAddr));
			}
		}
	}

	// Send Data
	context->socketlock->lock();
	hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)mac, (*context->peerPort)[*mac], accept, 9 + optlen + siblingbuflen, 0, ADHOC_F_NONBLOCK);
	context->socketlock->unlock();

	// Free Memory
	free(accept);

	// Spawn Local Established Event
	spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, mac, 0, NULL);
}

/**
* Send Join Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendJoinPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int optlen, void * opt) {
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	if (peer == NULL || peer->state != PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST) {
		// Valid peer not found
		return;
	}

	// Allocate Join Message Buffer
	uint8_t * join = (uint8_t *)malloc(5LL + optlen);

	if (join == NULL) {
		// Failed to allocate the Join Message Buffer
		return;
	}

	// Join Opcode
	join[0] = PSP_ADHOC_MATCHING_PACKET_JOIN;

	// Optional Data Length
	memcpy(join + 1, &optlen, sizeof(optlen));

	// Copy Optional Data
	if (optlen > 0) memcpy(join + 5, opt, optlen);

	// Send Data
	context->socketlock->lock();
	hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)mac, (*context->peerPort)[*mac], join, 5 + optlen, 0, ADHOC_F_NONBLOCK);
	context->socketlock->unlock();

	// Free Memory
	free(join);
}

/**
* Send Cancel Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendCancelPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int optlen, void * opt) {
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Allocate Cancel Message Buffer
	uint8_t * cancel = (uint8_t *)malloc(5LL + optlen);

	// Allocated Cancel Message Buffer
	if (cancel != NULL) {
		// Cancel Opcode
		cancel[0] = PSP_ADHOC_MATCHING_PACKET_CANCEL;

		// Optional Data Length
		memcpy(cancel + 1, &optlen, sizeof(optlen));

		// Copy Optional Data
		if (optlen > 0) memcpy(cancel + 5, opt, optlen);

		// Send Data
		context->socketlock->lock();
		hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)mac, (*context->peerPort)[*mac], cancel, 5 + optlen, 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();

		// Free Memory
		free(cancel);
	}

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	if (peer == NULL) {
		// Peer not found
		return;
	}

	// Child Mode Fallback - Delete All
	if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) {
		// Delete Peer List
		clearPeerList(context);
	}

	// Delete Peer
	else {
		// Instead of removing peer immediately, We should give a little time before removing the peer and let it timed out? so it can send the BYE packet when stopping AdhocMatching after Canceling it
		peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
	}
}

/**
* Send Bulk Data Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param datalen Data Length
* @param data Data
*/
void sendBulkDataPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int datalen, void * data) {
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	if (peer == NULL) {
		// Invalid Peer
		return;
	}

	// Don't send if it's aborted
	//if (peer->sending == 0) return;

	// Allocate Send Message Buffer
	uint8_t * send = (uint8_t *)malloc(5LL + datalen);

	if (send == NULL) {
		// Failed to allocate the Send Message Buffer
		return;
	}

	// Send Opcode
	send[0] = PSP_ADHOC_MATCHING_PACKET_BULK;

	// Data Length
	memcpy(send + 1, &datalen, sizeof(datalen));

	// Copy Data
	memcpy(send + 5, data, datalen);

	// Send Data
	context->socketlock->lock();
	hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)mac, (*context->peerPort)[*mac], send, 5 + datalen, 0, ADHOC_F_NONBLOCK);
	context->socketlock->unlock();

	// Free Memory
	free(send);

	// Remove Busy Bit from Peer
	peer->sending = 0;

	// Spawn Data Event
	spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_DATA_ACK, mac, 0, NULL);
}

/**
* Tell Established Peers of new Child
* @param context Matching Context Pointer
* @param mac New Child's MAC
*/
void sendBirthPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac) {
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Newborn Child
	SceNetAdhocMatchingMemberInternal * newborn = findPeer(context, mac);

	if (newborn == NULL) {
		// Did not find Newborn Child
		return;
	}

	// Packet Buffer
	uint8_t packet[7];

	// Set Opcode
	packet[0] = PSP_ADHOC_MATCHING_PACKET_BIRTH;

	// Set Newborn MAC
	memcpy(packet + 1, mac, sizeof(SceNetEtherAddr));

	// Iterate Peers
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		// Skip Newborn Child
		if (peer == newborn) continue;

		// Send only to children
		if (peer->state != PSP_ADHOC_MATCHING_PEER_CHILD) {
			continue;
		}

		// Send Packet
		context->socketlock->lock();
		int sent = hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], packet, (u32)sizeof(packet), 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();

		// Log Send Success
		if (sent >= 0)
			INFO_LOG(Log::sceNet, "InputLoop: Sending BIRTH [%s] to %s", mac2str(mac).c_str(), mac2str(&peer->mac).c_str());
		else
			WARN_LOG(Log::sceNet, "InputLoop: Failed to Send BIRTH [%s] to %s", mac2str(mac).c_str(), mac2str(&peer->mac).c_str());
	}
}

/**
* Tell Established Peers of abandoned Child
* @param context Matching Context Pointer
* @param mac Dead Child's MAC
*/
void sendDeathPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac) {
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find abandoned Child
	SceNetAdhocMatchingMemberInternal * deadkid = findPeer(context, mac);

	if (deadkid == NULL) {
		// Did not find abandoned Child
		return;
	}

	// Packet Buffer
	uint8_t packet[7];

	// Set abandoned Child MAC
	memcpy(packet + 1, mac, sizeof(SceNetEtherAddr));

	// Iterate Peers
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		// Skip dead Child? Or May be we should also tells the disconnected Child, that they have been disconnected from the Host (in the case they were disconnected because they went to PPSSPP settings for too long)
		if (peer == deadkid) {
			// Set Opcode
			packet[0] = PSP_ADHOC_MATCHING_PACKET_BYE;

			// Send Bye Packet
			context->socketlock->lock();
			hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], packet, (u32)sizeof(packet[0]), 0, ADHOC_F_NONBLOCK);
			context->socketlock->unlock();
		}
		else {
			// Send to other children
			if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) {
				// Set Opcode
				packet[0] = PSP_ADHOC_MATCHING_PACKET_DEATH;

				// Send Death Packet
				context->socketlock->lock();
				hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], packet, (u32)sizeof(packet), 0, ADHOC_F_NONBLOCK);
				context->socketlock->unlock();
			}
		}
	}

	// Delete Peer
	deletePeer(context, deadkid);
}

/**
* Tell Established Peers that we're shutting the Networking Layer down
* @param context Matching Context Pointer
*/
void sendByePacket(SceNetAdhocMatchingContext * context) {
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Iterate Peers
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist;
	for (; peer != NULL; peer = peer->next) {
		// Peer of Interest
		if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT || peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_P2P || peer->state == PSP_ADHOC_MATCHING_PEER_CANCEL_IN_PROGRESS) {
			// Bye Opcode
			uint8_t opcode = PSP_ADHOC_MATCHING_PACKET_BYE;

			// Send Bye Packet
			context->socketlock->lock();
			hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], &opcode, (u32)sizeof(opcode), 0, ADHOC_F_NONBLOCK);
			context->socketlock->unlock();
		}
	}
}

/**
* Handle Ping Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
*/
void actOnPingPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac) {
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// Found Peer
	if (peer != NULL) {
		// Update Receive Timer
		peer->lastping = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0;
	}
}

/**
* Handle Hello Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnHelloPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length) {
	// Interested in Hello Data
	if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && findParent(context) == NULL) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL)) {
		if (length < 5) {
			// Incomplete Packet Header
			return;
		}

		// Extract Optional Data Length
		int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

		if (optlen < 0 || length < (5 + optlen)) {
			// Invalid packet
			return;
		}

		// Set Default Null Data
		void * opt = NULL;

		// Extract Optional Data Pointer
		if (optlen > 0) opt = context->rxbuf + 5;

		// Find Peer
		SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

		// Peer not found
		if (peer == NULL) {
			// Allocate Memory
			peer = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

			// Allocated Memory
			if (peer != NULL) {
				// Clear Memory
				memset(peer, 0, sizeof(SceNetAdhocMatchingMemberInternal));

				// Copy Sender MAC
				peer->mac = *sendermac;

				// Set Peer State
				peer->state = PSP_ADHOC_MATCHING_PEER_OFFER;

				// Initialize Ping Timer
				peer->lastping = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0;

				peerlock.lock();
				// Link Peer into List
				peer->next = context->peerlist;
				context->peerlist = peer;
				peerlock.unlock();
			}
		}

		// Peer available now
		if (peer != NULL && peer->state != PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST && peer->state != PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST) {
			std::string hellohex;
			DataToHexString(10, 0, (u8*)opt, optlen, &hellohex);
			DEBUG_LOG(Log::sceNet, "HELLO Dump (%d bytes):\n%s", optlen, hellohex.c_str());

			// Spawn Hello Event. FIXME: HELLO event should not be triggered in the middle of joining? This will cause Bleach 7 to Cancel the join request
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_HELLO, sendermac, optlen, opt);
		}
	}
}

/**
* Handle Join Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnJoinPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length) {
	if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) {
		// Child mode context
		return;
	}

	// We still got a unoccupied slot in our room (Parent / P2P)
	if ((context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && countChildren(context) < (context->maxpeers - 1)) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL)) {
		// Complete Packet Header available
		if (length >= 5) {
			// Extract Optional Data Length
			int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

			// Complete Valid Packet available
			if (optlen >= 0 && length >= (5 + optlen)) {
				// Set Default Null Data
				void * opt = NULL;

				// Extract Optional Data Pointer
				if (optlen > 0) opt = context->rxbuf + 5;

				// Find Peer
				SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

				// If we got the peer in the table already and are a parent, there is nothing left to be done.
				// This is because the only way a parent can know of a child is via a join request...
				// If we thus know of a possible child, then we already had a previous join request thus no need for double tapping.
				if (peer != NULL && peer->lastping != 0 && context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
					WARN_LOG(Log::sceNet, "Join Event(2) Ignored");
					return;
				}

				// New Peer
				if (peer == NULL) {
					// Allocate Memory
					peer = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

					// Allocated Memory
					if (peer != NULL) {
						// Clear Memory
						memset(peer, 0, sizeof(SceNetAdhocMatchingMemberInternal));

						// Copy Sender MAC
						peer->mac = *sendermac;

						// Set Peer State
						peer->state = PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST;

						// Initialize Ping Timer
						peer->lastping = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0;

						peerlock.lock();
						// Link Peer into List
						peer->next = context->peerlist;
						context->peerlist = peer;
						peerlock.unlock();

						// Spawn Request Event
						spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_REQUEST, sendermac, optlen, opt);

						// Return Success
						return;
					}
				}

				// Existing Peer (this case is only reachable for P2P mode)
				else {
					// Set Peer State
					peer->state = PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST;

					// Initialize Ping Timer
					peer->lastping = CoreTiming::GetGlobalTimeUsScaled();

					// Spawn Request Event
					spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_REQUEST, sendermac, optlen, opt);

					// Return Success
					return;
				}
			}
		}
	}

	WARN_LOG(Log::sceNet, "Join Event(2) Rejected");
	// Auto-Reject Player
	sendCancelPacket(context, sendermac, 0, NULL);
}

/**
* Handle Accept Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnAcceptPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, uint32_t length) {
	if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
		// Parent context
		return;
	}

	// Don't have a master yet
	if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && findParent(context) == NULL) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL)) {
		if (length < 9) {
			// Incomplete Packet Header
			return;
		}

		// Extract Optional Data Length
		int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

		// Extract Sibling Count
		int siblingcount = 0; memcpy(&siblingcount, context->rxbuf + 5, sizeof(siblingcount));

		if (optlen < 0 || length < (9LL + optlen + static_cast<long long>(sizeof(SceNetEtherAddr)) * siblingcount)) {
			// Invalid packet
			return;
		}

		// Set Default Null Data
		void * opt = NULL;

		// Extract Optional Data Pointer
		if (optlen > 0) opt = context->rxbuf + 9;

		// Sibling MAC Array Null Data
		SceNetEtherAddr * siblings = NULL;

		// Extract Optional Sibling MAC Array
		if (siblingcount > 0) siblings = (SceNetEtherAddr *)(context->rxbuf + 9 + optlen);

		// Find Outgoing Request
		SceNetAdhocMatchingMemberInternal * request = findOutgoingRequest(context);

		// We are waiting for an answer to our request...
		if (request == NULL) {
			return;
		}

		// Find Peer
		SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

		if (request != peer) {
			// It's not the answer we wanted!
			return;
		}

		// Change Peer State
		peer->state = (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) ? (PSP_ADHOC_MATCHING_PEER_PARENT) : (PSP_ADHOC_MATCHING_PEER_P2P);

		// Remove Unneeded Peer Information
		postAcceptCleanPeerList(context);

		// Add Sibling Peers
		if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) {
			// Add existing siblings
			postAcceptAddSiblings(context, siblingcount, siblings);

			// Add Self Peer to the following position (using peer->state = 0 to identify as Self)
			addMember(context, &context->mac);
		}

		// IMPORTANT! The Event Order here is ok!
		// Internally the Event Stack appends to the front, so the order will be switched around.

		// Spawn Established Event
		spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, sendermac, 0, NULL);

		// Spawn Accept Event
		spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ACCEPT, sendermac, optlen, opt);
	}
}

/**
* Handle Cancel Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnCancelPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length) {
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	if (peer == NULL) {
		// Interest Condition not fulfilled
		return;
	}

	if (length < 5) {
		// Incomplete Packet Header
		return;
	}

	// Extract Optional Data Length
	int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

	if (optlen < 0 || length < (5 + optlen)) {
		// Invalid packet
		return;
	}

	// Set Default Null Data
	void * opt = NULL;

	// Extract Optional Data Pointer
	if (optlen > 0) opt = context->rxbuf + 5;

	// Get Outgoing Join Request
	SceNetAdhocMatchingMemberInternal* request = findOutgoingRequest(context);

	// Child Mode
	if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) {
		// Get Parent
		SceNetAdhocMatchingMemberInternal* parent = findParent(context);

		// Join Request denied
		if (request == peer) {
			// Spawn Deny Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_DENY, sendermac, optlen, opt);

			// Delete Peer from List
			//deletePeer(context, peer);
			peer->lastping = 0;
		}

		// Kicked from Room
		else if (parent == peer) {
			// Iterate Peers
			SceNetAdhocMatchingMemberInternal * item = context->peerlist;
			for (; item != NULL; item = item->next) {
				// Established Peer
				if (item->state == PSP_ADHOC_MATCHING_PEER_CHILD || item->state == PSP_ADHOC_MATCHING_PEER_PARENT) {
					// Spawn Leave / Kick Event
					spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, &item->mac, optlen, opt);
				}
			}

			// Delete Peer from List
			clearPeerList(context);
		}
	}

	// Parent Mode
	else if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
		// Cancel Join Request
		if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST) {
			// Spawn Request Cancel Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_CANCEL, sendermac, optlen, opt);

			// Delete Peer from List
			//deletePeer(context, peer);
			peer->lastping = 0;
		}

		// Leave Room
		else if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) {
			// Spawn Leave / Kick Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, sendermac, optlen, opt);

			// Delete Peer from List
			//deletePeer(context, peer);
			peer->lastping = 0;
		}
	}

	// P2P Mode
	else {
		// Get P2P Partner
		SceNetAdhocMatchingMemberInternal* p2p = findP2P(context);

		// Join Request denied
		if (request == peer) {
			// Spawn Deny Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_DENY, sendermac, optlen, opt);

			// FIXME: Delete Peer from List?
			// Instead of removing the peer immediately, we should let it timedout, otherwise inviter in Crazy Taxi will wait forever without getting timedout, since handleTimeout need the peer data to exist.
			peer->lastping = 0;
		}

		// Kicked from Room
		else if (p2p == peer) {
			// Spawn Leave / Kick Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, sendermac, optlen, opt);

			// Delete Peer from List
			//deletePeer(context, peer);
			peer->lastping = 0;
		}

		// Cancel Join Request
		else if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST) {
			// Spawn Request Cancel Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_CANCEL, sendermac, optlen, opt);

			// Delete Peer from List
			//deletePeer(context, peer);
			peer->lastping = 0;
		}
	}
}

/**
* Handle Bulk Data Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnBulkDataPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length) {
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// Established Peer
	if (peer != NULL && (
		(context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && peer->state == PSP_ADHOC_MATCHING_PEER_P2P)))
	{
		// Complete Packet Header available
		if (length > 5) {
			// Extract Data Length
			int datalen = 0; memcpy(&datalen, context->rxbuf + 1, sizeof(datalen));

			// Complete Valid Packet available
			if (datalen > 0 && length >= (5 + datalen)) {
				// Extract Data
				void * data = context->rxbuf + 5;

				// Spawn Data Event
				spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_DATA, sendermac, datalen, data);
			}
		}
	}
}

/**
* Handle Birth Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnBirthPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, uint32_t length) {
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	if (peer == NULL || context->mode != PSP_ADHOC_MATCHING_MODE_CHILD || peer != findParent(context)) {
		// Invalid Circumstances
		return;
	}
	if (length < (1 + sizeof(SceNetEtherAddr))) {
		// Incomplete packet
		return;
	}

	// Extract Child MAC
	SceNetEtherAddr mac;
	memcpy(&mac, context->rxbuf + 1, sizeof(SceNetEtherAddr));

	// Allocate Memory
	SceNetAdhocMatchingMemberInternal * sibling = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

	if (sibling == NULL) {
		// Failed to allocate memory
		return;
	}

	// Clear Memory
	memset(sibling, 0, sizeof(SceNetAdhocMatchingMemberInternal));

	// Save MAC Address
	sibling->mac = mac;

	// Set Peer State
	sibling->state = PSP_ADHOC_MATCHING_PEER_CHILD;

	// Initialize Ping Timer
	sibling->lastping = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0;

	peerlock.lock();

	// Link Peer
	sibling->next = context->peerlist;
	context->peerlist = sibling;

	peerlock.unlock();

	// Spawn Established Event. FIXME: ESTABLISHED event should only be triggered for Parent/P2P peer?
	//spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, &sibling->mac, 0, NULL);
}

/**
* Handle Death Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnDeathPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, uint32_t length) {
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	if (peer == NULL || context->mode != PSP_ADHOC_MATCHING_MODE_CHILD || peer != findParent(context)) {
		// Invalid Circumstances
		return;
	}

	if (length < (1 + sizeof(SceNetEtherAddr))) {
		// Incomplete packet
		return;
	}

	// Extract Child MAC
	SceNetEtherAddr mac;
	memcpy(&mac, context->rxbuf + 1, sizeof(SceNetEtherAddr));

	// Find Peer
	SceNetAdhocMatchingMemberInternal * deadkid = findPeer(context, &mac);

	if (deadkid->state != PSP_ADHOC_MATCHING_PEER_CHILD) {
		// Invalid Sibling
		return;
	}

	// Spawn Leave Event
	spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, &mac, 0, NULL);

	// Delete Peer
	deletePeer(context, deadkid);
}

/**
* Handle Bye Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
*/
void actOnByePacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac) {
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	if (peer == NULL) {
		// We don't know this guy
		return;
	}

	// P2P or Child Bye. FIXME: Should we allow BYE Event to intervene joining process of Parent-Child too just like P2P Mode? (ie. Crazy Taxi uses P2P Mode)
	if ((context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_P2P &&
		(peer->state == PSP_ADHOC_MATCHING_PEER_P2P || peer->state == PSP_ADHOC_MATCHING_PEER_OFFER || peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST || peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST || peer->state == PSP_ADHOC_MATCHING_PEER_CANCEL_IN_PROGRESS)))
	{
		if (context->mode != PSP_ADHOC_MATCHING_MODE_CHILD) {
			// Spawn Leave / Kick Event. FIXME: DISCONNECT event should only be triggered on Parent/P2P mode and for Parent/P2P peer?
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_BYE, sendermac, 0, NULL);
		}

		// Delete Peer
		deletePeer(context, peer);
		// Instead of removing peer immediately, We should give a little time before removing the peer and let it timed out? just in case the game is in the middle of communicating with the peer on another thread so it won't recognize it as Unknown peer
		//peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
	}

	// Parent Bye
	else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_PARENT) {
		// Spawn Leave / Kick Event. FIXME: DISCONNECT event should only be triggered on Parent/P2P mode and for Parent/P2P peer?
		spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_BYE, sendermac, 0, NULL);

		// Delete Peer from List
		clearPeerList(context);
	}
}


/**
* TODO: This really should be a callback or event!
* Matching Event Dispatcher Thread
* @param args sizeof(SceNetAdhocMatchingContext *)
* @param argp SceNetAdhocMatchingContext *
* @return Exit Point is never reached...
*/
int matchingEventThread(int matchingId) {
	SetCurrentThreadName("MatchingEvent");
	// Multithreading Lock
	peerlock.lock();
	// Cast Context
	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);
	// Multithreading Unlock
	peerlock.unlock();

	// Log Startup
	INFO_LOG(Log::sceNet, "EventLoop: Begin of EventLoop[%i] Thread", matchingId);

	// Run while needed...
	if (context != NULL) {
		u32 bufLen = context->rxbuflen; //0;
		u32 bufAddr = 0; //= userMemory.Alloc(bufLen); //context->rxbuf;
		u32_le * args = context->handlerArgs; //MatchingArgs

		while (contexts != NULL && context->eventRunning) {
			// Multithreading Lock
			peerlock.lock();
			// Cast Context
			context = findMatchingContext(matchingId);
			// Multithreading Unlock
			peerlock.unlock();

			// Messages on Stack ready for processing
			while (context != NULL && context->event_stack != NULL) {
				// Claim Stack
				context->eventlock->lock();

				// Iterate Message List
				ThreadMessage * msg = context->event_stack;
				if (msg != NULL) {
					// Default Optional Data
					void* opt = NULL;

					// Grab Optional Data
					if (msg->optlen > 0) opt = ((u8*)msg) + sizeof(ThreadMessage); //&msg[1]

					// Log Matching Events
					INFO_LOG(Log::sceNet, "EventLoop[%d]: Matching Event [%d=%s][%s] OptSize=%d", matchingId, msg->opcode, getMatchingEventStr(msg->opcode), mac2str(&msg->mac).c_str(), msg->optlen);

					// Unlock to prevent race-condition with other threads due to recursive lock
					//context->eventlock->unlock();
					// Call Event Handler
					//context->handler(context->id, msg->opcode, &msg->mac, msg->optlen, opt);
					// Notify Event Handlers
					notifyMatchingHandler(context, msg, opt, bufAddr, bufLen, args); // If we're using shared Buffer & Args for All Events We should wait for the Mipscall to be fully executed before processing the next event. GTA VCS need this delay/sleep.

					// Give some time before executing the next mipscall to prevent event ACCEPT(6)->ESTABLISH(7) getting reversed After Action ESTABLISH(7)->ACCEPT(6)
					// Must Not be delayed too long to prevent desync/disconnect. Not longer than the delays on callback's HLE?
					//sleep_ms(10); //sceKernelDelayThread(10000);

					// Lock again
					//context->eventlock->lock();

					// Pop event stack from front (this should be queue instead of stack?)
					context->event_stack = msg->next;
					free(msg);
					msg = NULL;
				}

				// Unlock Stack
				context->eventlock->unlock();
			}

			// Share CPU Time
			sleep_ms(10, "pro-adhoc-poll-3"); //1 //sceKernelDelayThread(10000);

			// Don't do anything if it's paused, otherwise the log will be flooded
			while (Core_IsStepping() && coreState != CORE_POWERDOWN && contexts != NULL && context->eventRunning)
				sleep_ms(10, "pro-adhoc-event-poll-3");
		}

		// Process Last Messages
		if (contexts != NULL && context->event_stack != NULL) {
			// Claim Stack
			context->eventlock->lock();

			// Iterate Message List
			int msg_count = 0;
			ThreadMessage * msg = context->event_stack;
			for (; msg != NULL; msg = msg->next) {
				// Default Optional Data
				void * opt = NULL;

				// Grab Optional Data
				if (msg->optlen > 0) opt = ((u8 *)msg) + sizeof(ThreadMessage); //&msg[1]

				INFO_LOG(Log::sceNet, "EventLoop[%d]: Matching Event [EVENT=%d]\n", matchingId, msg->opcode);

				//context->eventlock->unlock();
				// Original Call Event Handler
				//context->handler(context->id, msg->opcode, &msg->mac, msg->optlen, opt);
				// Notify Event Handlers
				notifyMatchingHandler(context, msg, opt, bufAddr, bufLen, args);
				//context->eventlock->lock();
				msg_count++;
			}

			// Clear Event Message Stack
			clearStack(context, PSP_ADHOC_MATCHING_EVENT_STACK);

			// Free Stack
			context->eventlock->unlock();
			INFO_LOG(Log::sceNet, "EventLoop[%d]: Finished (%d msg)", matchingId, msg_count);
		}

		// Free memory
		//if (Memory::IsValidAddress(bufAddr)) userMemory.Free(bufAddr);

		// Delete Pointer Reference (and notify caller about finished cleanup)
		//context->eventThread = NULL;
	}

	// Log Shutdown
	INFO_LOG(Log::sceNet, "EventLoop: End of EventLoop[%i] Thread", matchingId);

	// Return Zero to shut up Compiler
	return 0;
}

/**
* Matching IO Handler Thread
* @param args sizeof(SceNetAdhocMatchingContext *)
* @param argp SceNetAdhocMatchingContext *
* @return Exit Point is never reached...
*/
int matchingInputThread(int matchingId) { // TODO: The MatchingInput thread is using sceNetAdhocPdpRecv & sceNetAdhocPdpSend functions so it might be better to run this on PSP thread instead of real thread
	SetCurrentThreadName("MatchingInput");
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	// Multithreading Lock
	peerlock.lock();
	// Cast Context
	SceNetAdhocMatchingContext* context = findMatchingContext(matchingId);
	// Multithreading Unlock
	peerlock.unlock();

	// Last Ping
	u64_le lastping = 0;

	// Last Hello
	u64_le lasthello = 0;

	u64_le now;

	static SceNetEtherAddr sendermac;
	static u32_le senderport;
	static int rxbuflen;

	// Log Startup
	INFO_LOG(Log::sceNet, "InputLoop: Begin of InputLoop[%i] Thread", matchingId);

	// Run while needed...
	if (context != NULL) {
		while (contexts != NULL && context->inputRunning) {
			// Multithreading Lock
			peerlock.lock();
			// Cast Context
			context = findMatchingContext(matchingId);
			// Multithreading Unlock
			peerlock.unlock();

			while (context != NULL && context->inputRunning && !Core_IsStepping()) {
				now = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0;

				// Hello Message Sending Context with unoccupied Slots
				if ((context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && (countChildren(context) < (context->maxpeers - 1))) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL)) {
					// Hello Message Broadcast necessary because of Hello Interval
					if (context->hello_int > 0)
						if (static_cast<s64>(now - lasthello) >= static_cast<s64>(context->hello_int)) {
							// Broadcast Hello Message
							broadcastHelloMessage(context);

							// Update Hello Timer
							lasthello = now;
						}
				}

				// Ping Required
				if (context->keepalive_int > 0) {
					if (static_cast<s64>(now - lastping) >= static_cast<s64>(context->keepalive_int)) {
						// Handle Peer Timeouts
						handleTimeout(context);

						// Broadcast Ping Message
						broadcastPingMessage(context);

						// Update Ping Timer
						lastping = now;
					}
				}
				else {
					// FIXME: Should we checks for Timeout too when the game doesn't set the keep alive interval?
					handleTimeout(context);
				}

				// Messages on Stack ready for processing
				if (context->input_stack != NULL) {
					// Claim Stack
					context->inputlock->lock();

					// Iterate Message List
					ThreadMessage* msg = context->input_stack;
					while (msg != NULL) {
						// Default Optional Data
						void* opt = NULL;

						// Grab Optional Data
						if (msg->optlen > 0) opt = ((u8*)msg) + sizeof(ThreadMessage);

						//context->inputlock->unlock(); // Unlock to prevent race condition when locking peerlock

						// Send Accept Packet
						if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_ACCEPT) sendAcceptPacket(context, &msg->mac, msg->optlen, opt);

						// Send Join Packet
						else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_JOIN) sendJoinPacket(context, &msg->mac, msg->optlen, opt);

						// Send Cancel Packet
						else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_CANCEL) sendCancelPacket(context, &msg->mac, msg->optlen, opt);

						// Send Bulk Data Packet
						else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_BULK) sendBulkDataPacket(context, &msg->mac, msg->optlen, opt);

						// Send Birth Packet
						else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_BIRTH) sendBirthPacket(context, &msg->mac);

						// Send Death Packet
						else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_DEATH) sendDeathPacket(context, &msg->mac);

						// Cancel Bulk Data Transfer (does nothing as of now as we fire and forget anyway) // Do we need to check DeathPacket and ByePacket here?
						//else if(msg->opcode == PSP_ADHOC_MATCHING_PACKET_BULK_ABORT) sendAbortBulkDataPacket(context, &msg->mac, msg->optlen, opt);

						//context->inputlock->lock(); // Lock again

						// Pop input stack from front (this should be queue instead of stack?)
						context->input_stack = msg->next;
						free(msg);
						msg = context->input_stack;
					}

					// Free Stack
					context->inputlock->unlock();
				}

				// Receive PDP Datagram
				// FIXME: When using JPCSP + prx files, the "SceNetAdhocMatchingInput" thread is using blocking PdpRecv with infinite(0) timeout, which can be stopped/aborted using SetSocketAlert, while "SceNetAdhocMatchingEvent" thread is using non-blocking for sending
				rxbuflen = context->rxbuflen;
				senderport = 0;
				// Lock the peer first before locking the socket to avoid race condiion
				peerlock.lock();
				context->socketlock->lock();
				int recvresult = sceNetAdhocPdpRecv(context->socket, &sendermac, &senderport, context->rxbuf, &rxbuflen, 0, ADHOC_F_NONBLOCK);
				context->socketlock->unlock();
				peerlock.unlock();

				// Received Data from a Sender that interests us
				// Note: There are cases where the sender port might be re-mapped by router or ISP, so we shouldn't check the source port.
				if (recvresult == 0 && rxbuflen > 0) {
					// Log Receive Success
					if (context->rxbuf[0] > 1) {
						INFO_LOG(Log::sceNet, "InputLoop[%d]: Received %d Bytes (Opcode[%d]=%s)", matchingId, rxbuflen, context->rxbuf[0], getMatchingOpcodeStr(context->rxbuf[0]));
					}

					// Update Peer Timestamp
					peerlock.lock();
					SceNetAdhocctlPeerInfo* peer = findFriend(&sendermac);
					if (peer != NULL) {
						now = CoreTiming::GetGlobalTimeUsScaled();
						s64 delta = now - peer->last_recv;
						DEBUG_LOG(Log::sceNet, "Timestamp LastRecv Delta: %lld (%llu - %llu) from %s", delta, now, peer->last_recv, mac2str(&sendermac).c_str());
						if (peer->last_recv != 0) peer->last_recv = std::max(peer->last_recv, now - defaultLastRecvDelta);
					}
					else {
						WARN_LOG(Log::sceNet, "InputLoop[%d]: Unknown Peer[%s:%u] (Recved=%i, Length=%i)", matchingId, mac2str(&sendermac).c_str(), senderport, recvresult, rxbuflen);
					}

					// Show a warning if other player is having their port being re-mapped, thus that other player may have issue with the communication. 
					// Note: That other player may need to switch side between host and join, or reboot their router to solve this issue.
					if (context->port != senderport && senderport != (*context->peerPort)[sendermac]) {
						char name[9] = {};
						if (peer != NULL)
							truncate_cpy(name, sizeof(name), (const char*)peer->nickname.data);
						WARN_LOG(Log::sceNet, "InputLoop[%d]: Unknown Source Port from [%s][%s:%u -> %u] (Recved=%i, Length=%i)", matchingId, name, mac2str(&sendermac).c_str(), senderport, context->port, recvresult, rxbuflen);
						g_OSD.Show(OSDType::MESSAGE_WARNING, std::string(n->T("AM: Data from Unknown Port")) + std::string(" [") + std::string(name) + std::string("]:") + std::to_string(senderport) + std::string(" -> ") + std::to_string(context->port) + std::string(" (") + std::to_string(portOffset) + std::string(")"), 0.0f, "unknowndata");
					}
					// Keep tracks of re-mapped peer's ports for further communication. 
					// Note: This will only works if this player were able to receives data on normal port from other players (ie. this player's port wasn't remapped)
					(*context->peerPort)[sendermac] = senderport;
					peerlock.unlock();

					// Ping Packet
					if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_PING) actOnPingPacket(context, &sendermac);

					// Hello Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_HELLO) actOnHelloPacket(context, &sendermac, rxbuflen);

					// Join Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_JOIN) actOnJoinPacket(context, &sendermac, rxbuflen);

					// Accept Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_ACCEPT) actOnAcceptPacket(context, &sendermac, rxbuflen);

					// Cancel Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_CANCEL) actOnCancelPacket(context, &sendermac, rxbuflen);

					// Bulk Data Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_BULK) actOnBulkDataPacket(context, &sendermac, rxbuflen);

					// Abort Bulk Data Packet
					//else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_BULK_ABORT) actOnAbortBulkDataPacket(context, &sendermac, rxbuflen);

					// Birth Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_BIRTH) actOnBirthPacket(context, &sendermac, rxbuflen);

					// Death Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_DEATH) actOnDeathPacket(context, &sendermac, rxbuflen);

					// Bye Packet
					else if (context->rxbuf[0] == PSP_ADHOC_MATCHING_PACKET_BYE) actOnByePacket(context, &sendermac);

					// Ignore Incoming Trash Data
				}
				else
					break;
			}
			// Share CPU Time
			sleep_ms(10, "pro-adhoc-4"); //1 //sceKernelDelayThread(10000);

			// Don't do anything if it's paused, otherwise the log will be flooded
			while (Core_IsStepping() && coreState != CORE_POWERDOWN && contexts != NULL && context->inputRunning)
				sleep_ms(10, "pro-adhoc-input-4");
		}

		if (contexts != NULL) {
			// Process Last Messages
			if (context->input_stack != NULL) {
				// Claim Stack
				context->inputlock->lock();

				// Iterate Message List
				int msg_count = 0;
				ThreadMessage* msg = context->input_stack;
				while (msg != NULL) {
					// Default Optional Data
					void* opt = NULL;

					// Grab Optional Data
					if (msg->optlen > 0) opt = ((u8*)msg) + sizeof(ThreadMessage);

					// Send Accept Packet
					if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_ACCEPT) sendAcceptPacket(context, &msg->mac, msg->optlen, opt);

					// Send Join Packet
					else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_JOIN) sendJoinPacket(context, &msg->mac, msg->optlen, opt);

					// Send Cancel Packet
					else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_CANCEL) sendCancelPacket(context, &msg->mac, msg->optlen, opt);

					// Send Bulk Data Packet
					else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_BULK) sendBulkDataPacket(context, &msg->mac, msg->optlen, opt);

					// Send Birth Packet
					else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_BIRTH) sendBirthPacket(context, &msg->mac);

					// Send Death Packet
					else if (msg->opcode == PSP_ADHOC_MATCHING_PACKET_DEATH) sendDeathPacket(context, &msg->mac);

					// Cancel Bulk Data Transfer (does nothing as of now as we fire and forget anyway) // Do we need to check DeathPacket and ByePacket here?
					//else if(msg->opcode == PSP_ADHOC_MATCHING_PACKET_BULK_ABORT) sendAbortBulkDataPacket(context, &msg->mac, msg->optlen, opt);

					// Pop input stack from front (this should be queue instead of stack?)
					context->input_stack = msg->next;
					free(msg);
					msg = context->input_stack;
					msg_count++;
				}

				// Free Stack
				context->inputlock->unlock();
				INFO_LOG(Log::sceNet, "InputLoop[%d]: Finished (%d msg)", matchingId, msg_count);
			}

			// Clear IO Message Stack
			clearStack(context, PSP_ADHOC_MATCHING_INPUT_STACK);

			// Send Bye Messages. FIXME: Official prx seems to be sending DEATH instead of BYE packet during MatchingStop? But DEATH packet doesn't works with DBZ Team Tag
			sendByePacket(context);

			// Free Peer List Buffer
			clearPeerList(context); //deleteAllMembers(context);

			// Delete Pointer Reference (and notify caller about finished cleanup)
			//context->inputThread = NULL;
		}
	}

	// Log Shutdown
	INFO_LOG(Log::sceNet, "InputLoop: End of InputLoop[%i] Thread", matchingId);

	// Terminate Thread
	//sceKernelExitDeleteThread(0);

	// Return Zero to shut up Compiler
	return 0;
}


void netAdhocMatchingValidateLoopMemory() {
	if (!matchingThreadHackAddr || (matchingThreadHackAddr && strcmp("matchingThreadHack", kernelMemory.GetBlockTag(matchingThreadHackAddr)) != 0)) {
		u32 blockSize = sizeof(matchingThreadCode);
		matchingThreadHackAddr = kernelMemory.Alloc(blockSize, false, "matchingThreadHack");
		if (matchingThreadHackAddr) Memory::Memcpy(matchingThreadHackAddr, matchingThreadCode, sizeof(matchingThreadCode));
	}
}

int NetAdhocMatching_Stop(int matchingId) {
	SceNetAdhocMatchingContext* item = findMatchingContext(matchingId);
	if (item == NULL) {
		return 0;
	}

	// This will cause using PdpRecv on this socket to return ERROR_NET_ADHOC_SOCKET_ALERTED (Based on Ys vs. Sora no Kiseki when tested with JPCSP + prx files). Is this used to abort inprogress socket activity?
	NetAdhoc_SetSocketAlert(item->socket, ADHOC_F_ALERTRECV);

	item->inputRunning = false;
	if (item->inputThread.joinable()) {
		item->inputThread.join();
	}

	item->eventRunning = false;
	if (item->eventThread.joinable()) {
		item->eventThread.join();
	}

	// Stop fake PSP Thread.
	// kernelObjects may already been cleared early during a Shutdown, thus trying to access it may generates Warning/Error in the log
	if (matchingThreads[item->matching_thid] > 0 && strcmp(__KernelGetThreadName(matchingThreads[item->matching_thid]), "ERROR") != 0) {
		__KernelStopThread(matchingThreads[item->matching_thid], SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocMatching stopped");
		__KernelDeleteThread(matchingThreads[item->matching_thid], SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocMatching deleted");
	}
	matchingThreads[item->matching_thid] = 0;

	// Make sure nobody locking/using the socket
	item->socketlock->lock();
	// Delete the socket
	NetAdhocPdp_Delete(item->socket, 0); // item->connected = (sceNetAdhocPdpDelete(item->socket, 0) < 0);
	item->socketlock->unlock();

	// Multithreading Lock
	peerlock.lock();

	// Remove your own MAC, or All members, or don't remove at all or we should do this on MatchingDelete ?
	clearPeerList(item); //deleteAllMembers(item);

	item->running = 0;
	netAdhocMatchingStarted--;

	// Multithreading Unlock
	peerlock.unlock();

	return 0;
}

int sceNetAdhocMatchingStop(int matchingId) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingStop(%i) at %08x", matchingId, currentMIPS->pc);

	return NetAdhocMatching_Stop(matchingId);
}

int NetAdhocMatching_Delete(int matchingId) {
	// Multithreading Lock
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Previous Context Reference
	SceNetAdhocMatchingContext* prev = NULL;

	// Context Pointer
	SceNetAdhocMatchingContext* item = contexts;

	// Iterate contexts
	for (; item != NULL; item = item->next) {
		// Found matching ID
		if (item->id == matchingId) {
			// Unlink Left (Beginning)
			if (prev == NULL) contexts = item->next;

			// Unlink Left (Other)
			else prev->next = item->next;

			// Stop it first if it's still running
			if (item->running) {
				NetAdhocMatching_Stop(matchingId);
			}
			// Delete the Fake PSP Thread
			//__KernelDeleteThread(item->matching_thid, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocMatching deleted");
			//delete item->matchingThread;

			// Free allocated memories
			free(item->hello);
			free(item->rxbuf);
			clearPeerList(item); //deleteAllMembers(item);
			(*item->peerPort).clear();
			delete item->peerPort;
			// Destroy locks
			item->eventlock->lock(); // Make sure it's not locked when being deleted
			item->eventlock->unlock();
			delete item->eventlock;
			item->inputlock->lock(); // Make sure it's not locked when being deleted
			item->inputlock->unlock();
			delete item->inputlock;
			item->socketlock->lock(); // Make sure it's not locked when being deleted
			item->socketlock->unlock();
			delete item->socketlock;
			// Free item context memory
			free(item);
			item = NULL;

			// Making sure there are no leftover matching events from this session which could cause a crash on the next session
			deleteMatchingEvents(matchingId);

			// Stop Search
			break;
		}

		// Set Previous Reference
		prev = item;
	}

	return 0;
}

int sceNetAdhocMatchingDelete(int matchingId) {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?

	NetAdhocMatching_Delete(matchingId);

	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingDelete(%i) at %08x", matchingId, currentMIPS->pc);

	// Give a little time to make sure everything are cleaned up before the following AdhocMatchingCreate, Not too long tho, otherwise Naruto Ultimate Ninja Heroes 3 will have an issue
	//hleDelayResult(0, "give time to init/cleanup", adhocExtraPollDelayMS * 1000);
	return 0;
}

int sceNetAdhocMatchingInit(u32 memsize) {
	WARN_LOG_REPORT_ONCE(sceNetAdhocMatchingInit, Log::sceNet, "sceNetAdhocMatchingInit(%d) at %08x", memsize, currentMIPS->pc);

	// Uninitialized Library
	if (netAdhocMatchingInited)
		return ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED;

	// Save Fake Pool Size
	fakePoolSize = memsize;

	// Initialize Library
	deleteMatchingEvents();
	netAdhocMatchingInited = true;

	// Return Success
	return 0;
}

int NetAdhocMatching_Term() {
	if (netAdhocMatchingInited) {
		// Delete all Matching contexts
		SceNetAdhocMatchingContext* next = NULL;
		SceNetAdhocMatchingContext* context = contexts;
		while (context != NULL) {
			next = context->next;
			//if (context->running) NetAdhocMatching_Stop(context->id);
			NetAdhocMatching_Delete(context->id);
			context = next;
		}
		contexts = NULL;
		matchingThreads.clear();
	}

	return 0;
}

int sceNetAdhocMatchingTerm() {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingTerm() at %08x", currentMIPS->pc);
	// Should we cleanup all created matching contexts first? just in case there are games that doesn't delete them before calling this
	NetAdhocMatching_Term();

	netAdhocMatchingInited = false;
	return 0;
}


// Presumably returns a "matchingId".
static int sceNetAdhocMatchingCreate(int mode, int maxnum, int port, int rxbuflen, int hello_int, int keepalive_int, int init_count, int rexmt_int, u32 callbackAddr) {
	WARN_LOG(Log::sceNet, "sceNetAdhocMatchingCreate(mode=%i, maxnum=%i, port=%i, rxbuflen=%i, hello=%i, keepalive=%i, initcount=%i, rexmt=%i, callbackAddr=%08x) at %08x", mode, maxnum, port, rxbuflen, hello_int, keepalive_int, init_count, rexmt_int, callbackAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	SceNetAdhocMatchingHandler handler;
	handler.entryPoint = callbackAddr;

	if (!netAdhocMatchingInited) {
		// Uninitialized Library
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhoc matching not initialized");
	}

	if (maxnum <= 1 || maxnum > 16) {
		// Invalid Member Limit
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_MAXNUM, "adhoc matching invalid maxnum");
	}

	if (rxbuflen < 1) {
		// Invalid Receive Buffer Size
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_RXBUF_TOO_SHORT, "adhoc matching rxbuf too short");
	}

	if (mode < 1 || mode > 3) {
		// InvalidERROR_NET_Arguments
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhoc matching invalid arg");
	}

	// Iterate Matching Contexts
	SceNetAdhocMatchingContext * item = contexts;
	for (; item != NULL; item = item->next) {
		// Port Match found
		if (item->port == port)
			return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_PORT_IN_USE, "adhoc matching port in use");
	}

	// Allocate Context Memory
	SceNetAdhocMatchingContext * context = (SceNetAdhocMatchingContext *)malloc(sizeof(SceNetAdhocMatchingContext));

	// Allocated Memory
	if (context != NULL) {
		// Create PDP Socket
		SceNetEtherAddr localmac;
		getLocalMac(&localmac);

		// Clear Memory
		memset(context, 0, sizeof(SceNetAdhocMatchingContext));

		// Allocate Receive Buffer
		context->rxbuf = (uint8_t*)malloc(rxbuflen);

		// Allocated Memory
		if (context->rxbuf != NULL) {
			// Clear Memory
			memset(context->rxbuf, 0, rxbuflen);

			// Fill in Context Data
			context->id = findFreeMatchingID();
			context->mode = mode;
			context->maxpeers = maxnum;
			context->port = port;
			context->rxbuflen = rxbuflen;
			context->resendcounter = init_count;
			context->resend_int = rexmt_int; // used as ack timeout on lost packet (ie. not receiving anything after sending)?
			context->hello_int = hello_int; // client might set this to 0
			if (keepalive_int < 1) context->keepalive_int = PSP_ADHOCCTL_PING_TIMEOUT; else context->keepalive_int = keepalive_int; // client might set this to 0
			context->keepalivecounter = init_count; // used to multiply keepalive_int as timeout
			context->timeout = (((u64)(keepalive_int)+(u64)rexmt_int) * (u64)init_count);
			context->timeout += 500000; // For internet play we need higher timeout than what the game wanted
			context->handler = handler;
			context->peerPort = new std::map<SceNetEtherAddr, u16_le>();

			// Fill in Selfpeer
			context->mac = localmac;

			// Create locks
			context->socketlock = new std::recursive_mutex;
			context->eventlock = new std::recursive_mutex;
			context->inputlock = new std::recursive_mutex;

			// Multithreading Lock
			peerlock.lock(); //contextlock.lock();

			// Add Callback Handler
			context->handler.entryPoint = callbackAddr;
			context->matching_thid = static_cast<int>(matchingThreads.size());
			matchingThreads.push_back(0);

			// Link Context
			//context->connected = true;
			context->next = contexts;
			contexts = context;

			// Multithreading UnLock
			peerlock.unlock(); //contextlock.unlock();

			// Just to make sure Adhoc is already connected
			//hleDelayResult(context->id, "give time to init/cleanup", adhocEventDelayMS * 1000);

			// Return Matching ID
			return hleLogDebug(Log::sceNet, context->id, "success");
		}

		// Free Memory
		free(context);
	}

	// Out of Memory
	return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NO_SPACE, "adhoc matching no space");
}

int NetAdhocMatching_Start(int matchingId, int evthPri, int evthPartitionId, int evthStack, int inthPri, int inthPartitionId, int inthStack, int optLen, u32 optDataAddr) {
	// Multithreading Lock
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	SceNetAdhocMatchingContext* item = findMatchingContext(matchingId);

	if (item == NULL) {
		// return ERROR_NET_ADHOC_MATCHING_INVALID_ID; //Faking success to prevent GTA:VCS from stuck unable to choose host/join menu
		return hleLogDebug(Log::sceNet, 0);
	}

	//sceNetAdhocMatchingSetHelloOpt(matchingId, optLen, optDataAddr); //SetHelloOpt only works when context is running
	if ((optLen > 0) && Memory::IsValidAddress(optDataAddr)) {
		// Allocate the memory and copy the content
		free(item->hello);
		item->hello = (uint8_t*)malloc(optLen);
		if (item->hello != NULL) {
			Memory::Memcpy(item->hello, optDataAddr, optLen);
			item->hellolen = optLen;
			item->helloAddr = optDataAddr;
		}
		//else return ERROR_NET_ADHOC_MATCHING_NO_SPACE; //Faking success to prevent GTA:VCS from stuck unable to choose host/join menu
	}
	//else return ERROR_NET_ADHOC_MATCHING_INVALID_ARG; // ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN; // Returning Not Success will cause GTA:VC stuck unable to choose host/join menu

	// Create PDP Socket
	int sock = hleCall(sceNetAdhoc, int, sceNetAdhocPdpCreate, (const char*)&item->mac, static_cast<int>(item->port), item->rxbuflen, 0);
	item->socket = sock;
	if (sock < 1) {
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_PORT_IN_USE, "adhoc matching port in use");
	}

	// Create & Start the Fake PSP Thread ("matching_ev%d" and "matching_io%d")
	netAdhocMatchingValidateLoopMemory();
	std::string thrname = std::string("MatchingThr") + std::to_string(matchingId);
	matchingThreads[item->matching_thid] = hleCall(ThreadManForUser, int, sceKernelCreateThread, thrname.c_str(), matchingThreadHackAddr, evthPri, evthStack, 0, 0);
	//item->matchingThread = new HLEHelperThread(thrname.c_str(), "sceNetAdhocMatching", "__NetMatchingCallbacks", inthPri, inthStack);
	if (matchingThreads[item->matching_thid] > 0) {
		hleCall(ThreadManForUser, int, sceKernelStartThread, matchingThreads[item->matching_thid], 0, 0); //sceKernelStartThread(context->event_thid, sizeof(context), &context);
		//item->matchingThread->Start(matchingId, 0);
	}

	//Create the threads
	if (!item->eventRunning) {
		item->eventRunning = true;
		item->eventThread = std::thread(matchingEventThread, matchingId);
	}
	if (!item->inputRunning) {
		item->inputRunning = true;
		item->inputThread = std::thread(matchingInputThread, matchingId);
	}

	item->running = 1;
	netAdhocMatchingStarted++;

	return hleLogDebug(Log::sceNet, 0);
}

#define KERNEL_PARTITION_ID  1
#define USER_PARTITION_ID  2
#define VSHELL_PARTITION_ID  5
// This should be similar with sceNetAdhocMatchingStart2 but using USER_PARTITION_ID (2) for PartitionId params
static int sceNetAdhocMatchingStart(int matchingId, int evthPri, int evthStack, int inthPri, int inthStack, int optLen, u32 optDataAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingStart(%i, %i, %i, %i, %i, %i, %08x) at %08x", matchingId, evthPri, evthStack, inthPri, inthStack, optLen, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	int retval = NetAdhocMatching_Start(matchingId, evthPri, USER_PARTITION_ID, evthStack, inthPri, USER_PARTITION_ID, inthStack, optLen, optDataAddr);
	// Give a little time to make sure matching Threads are ready before the game use the next sceNet functions, should've checked for status instead of guessing the time?
	hleEatMicro(adhocMatchingEventDelay);
	return retval;
}

// With params for Partition ID for the event & input handler stack
static int sceNetAdhocMatchingStart2(int matchingId, int evthPri, int evthPartitionId, int evthStack, int inthPri, int inthPartitionId, int inthStack, int optLen, u32 optDataAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingStart2(%i, %i, %i, %i, %i, %i, %i, %i, %08x) at %08x", matchingId, evthPri, evthPartitionId, evthStack, inthPri, inthPartitionId, inthStack, optLen, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	int retval = NetAdhocMatching_Start(matchingId, evthPri, evthPartitionId, evthStack, inthPri, inthPartitionId, inthStack, optLen, optDataAddr);
	// Give a little time to make sure matching Threads are ready before the game use the next sceNet functions, should've checked for status instead of guessing the time?
	hleEatMicro(adhocMatchingEventDelay);
	return retval;
}

static int sceNetAdhocMatchingSelectTarget(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingSelectTarget(%i, %s, %i, %08x) at %08x", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str(), optLen, optDataPtr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	if (!netAdhocMatchingInited) {
		// Uninitialized Library
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
	}

	if (macAddress == NULL) {
		// Invalid Arguments
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	SceNetEtherAddr * target = (SceNetEtherAddr *)macAddress;

	// Find Matching Context for ID
	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);

	if (context == NULL) {
		// Invalid Matching ID
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");
	}

	if (!context->running) {
		// Idle Context
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");
	}

	// Search Result
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, (SceNetEtherAddr *)target);

	if (peer == NULL) {
		// Peer not found
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "adhocmatching unknown target");
	}

	if ((optLen != 0) && (optLen <= 0 || optDataPtr == 0)) {
		// Invalid Optional Data Length
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN, "adhocmatching invalid optlen");
	}

	void * opt = NULL;
	if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointerWriteUnchecked(optDataPtr);
	// Host Mode
	if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
		// Already Connected
		if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED, "adhocmatching already established");

		// Not enough space
		if (countChildren(context) == (context->maxpeers - 1)) return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_EXCEED_MAXNUM, "adhocmatching exceed maxnum");

		// Requesting Peer
		if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST) {
			// Accept Peer in Group
			peer->state = PSP_ADHOC_MATCHING_PEER_CHILD;

			// Sending order may need to be reversed since Stack appends to the front, so the order will be switched around.

			// Tell Children about new Sibling
			sendBirthMessage(context, peer);

			// Spawn Established Event
			//spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, target, 0, NULL);

			// Send Accept Confirmation to Peer
			sendAcceptMessage(context, peer, optLen, opt);

			// Return Success
			return hleLogDebug(Log::sceNet, 0);
		}
	}

	// Client Mode
	else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) {
		// Already connected
		if (findParent(context) != NULL) return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED, "adhocmatching already established");

		// Outgoing Request in Progress
		if (findOutgoingRequest(context) != NULL) return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS, "adhocmatching request in progress");

		// Valid Offer
		if (peer->state == PSP_ADHOC_MATCHING_PEER_OFFER) {
			// Switch into Join Request Mode
			peer->state = PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST;

			// Send Join Request to Peer
			sendJoinRequest(context, peer, optLen, opt);

			// Return Success
			return hleLogDebug(Log::sceNet, 0);
		}
	}

	// P2P Mode
	else {
		// Already connected
		if (findP2P(context) != NULL) return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED, "adhocmatching already established");

		// Outgoing Request in Progress
		if (findOutgoingRequest(context) != NULL) return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS, "adhocmatching request in progress");

		// Join Request Mode
		if (peer->state == PSP_ADHOC_MATCHING_PEER_OFFER) {
			// Switch into Join Request Mode
			peer->state = PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST;

			// Send Join Request to Peer
			sendJoinRequest(context, peer, optLen, opt);

			// Return Success
			return hleLogDebug(Log::sceNet, 0);
		}

		// Requesting Peer
		else if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST) {
			// Accept Peer in Group
			peer->state = PSP_ADHOC_MATCHING_PEER_P2P;

			// Tell Children about new Sibling
			//sendBirthMessage(context, peer);
			// Send Accept Confirmation to Peer
			sendAcceptMessage(context, peer, optLen, opt);

			// Return Success
			return hleLogDebug(Log::sceNet, 0);
		}
	}

	// How did this happen?! It shouldn't!
	return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_TARGET_NOT_READY, "adhocmatching target not ready");
}

int NetAdhocMatching_CancelTargetWithOpt(int matchingId, const char* macAddress, int optLen, u32 optDataPtr) {
	if (!netAdhocMatchingInited) {
		// Uninitialized Library
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
	}

	SceNetEtherAddr* target = (SceNetEtherAddr*)macAddress;
	void* opt = NULL;
	if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointerWriteUnchecked(optDataPtr);

	if (target == NULL || ((optLen != 0) && (optLen <= 0 || opt == NULL))) {
		// Invalid Arguments
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	// Find Matching Context
	SceNetAdhocMatchingContext* context = findMatchingContext(matchingId);

	if (context == NULL) {
		// Invalid Matching ID
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");
	}

	if (!context->running) {
		// Context not running
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");
	}

	// Find Peer
	SceNetAdhocMatchingMemberInternal* peer = findPeer(context, (SceNetEtherAddr*)target);

	if (peer == NULL) {
		// Peer not found
		//return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "adhocmatching unknown target");
		// Faking success to prevent the game (ie. Soul Calibur) to repeatedly calling this function when the other player is disconnected
		return hleLogDebug(Log::sceNet, 0);
	}

	// Valid Peer Mode
	if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT || peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST)) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && (peer->state == PSP_ADHOC_MATCHING_PEER_P2P || peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)))
	{
		// Notify other Children of Death
		if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD && countConnectedPeers(context) > 1) {
			// Send Death Message
			sendDeathMessage(context, peer);
		}

		// Mark Peer as Canceled
		peer->state = PSP_ADHOC_MATCHING_PEER_CANCEL_IN_PROGRESS;

		// Send Cancel Event to Peer
		sendCancelMessage(context, peer, optLen, opt);

		// Delete Peer from List
		// Can't delete here, Threads still need this data.
		// deletePeer(context, peer);
		// Marking peer to be timedout instead of deleting immediately
		peer->lastping = 0;

		hleEatCycles(adhocDefaultDelay);
		// Return Success
		return hleLogDebug(Log::sceNet, 0);
	}

	// Peer not found
	//return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "adhocmatching unknown target");
	// Faking success to prevent the game (ie. Soul Calibur) to repeatedly calling this function when the other player is disconnected
	return hleLogDebug(Log::sceNet, 0);
}

int sceNetAdhocMatchingCancelTargetWithOpt(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingCancelTargetWithOpt(%i, %s, %i, %08x) at %08x", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str(), optLen, optDataPtr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}
	return NetAdhocMatching_CancelTargetWithOpt(matchingId, macAddress, optLen, optDataPtr);
}

int sceNetAdhocMatchingCancelTarget(int matchingId, const char *macAddress) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingCancelTarget(%i, %s)", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str());
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}
	return NetAdhocMatching_CancelTargetWithOpt(matchingId, macAddress, 0, 0);
}

int sceNetAdhocMatchingGetHelloOpt(int matchingId, u32 optLenAddr, u32 optDataAddr) {
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	if (!Memory::IsValidAddress(optLenAddr)) {
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG);
	}

	s32_le *optlen = PSPPointer<s32_le>::Create(optLenAddr);

	// Multithreading Lock
	peerlock.lock();

	SceNetAdhocMatchingContext * item = findMatchingContext(matchingId);

	if (item != NULL) {
		// Get OptData
		*optlen = item->hellolen;
		if ((*optlen > 0) && Memory::IsValidAddress(optDataAddr)) {
			uint8_t * optdata = Memory::GetPointerWriteUnchecked(optDataAddr);
			memcpy(optdata, item->hello, *optlen);
		}
		//else return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
	}
	//else return ERROR_NET_ADHOC_MATCHING_INVALID_ID;

	// Multithreading Unlock
	peerlock.unlock();

	return hleLogDebug(Log::sceNet, 0);
}

int sceNetAdhocMatchingSetHelloOpt(int matchingId, int optLenAddr, u32 optDataAddr) {
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	if (!netAdhocMatchingInited)
		return hleLogDebug(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");

	// Multithreading Lock
	peerlock.lock();

	SceNetAdhocMatchingContext* context = findMatchingContext(matchingId);

	// Multithreading Unlock
	peerlock.unlock();

	// Context not found
	if (context == NULL)
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");

	// Invalid Matching Mode (Child)
	if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
		return hleLogDebug(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_MODE, "adhocmatching invalid mode");

	// Context not running
	if (!context->running)
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");

	// Invalid Optional Data Length
	if ((optLenAddr != 0) && (optDataAddr == 0))
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN, "adhocmatching invalid optlen"); //ERROR_NET_ADHOC_MATCHING_INVALID_ARG

	// Grab Existing Hello Data
	void* hello = context->hello;

	// Free Previous Hello Data, or Reuse it
	//free(hello);

	// Allocation Required
	if (optLenAddr > 0) {
		// Allocate Memory
		if (optLenAddr > context->hellolen) {
			hello = realloc(hello, optLenAddr);
		}

		// Out of Memory
		if (hello == NULL) {
			context->hellolen = 0;
			return ERROR_NET_ADHOC_MATCHING_NO_SPACE;
		}

		// Clone Hello Data
		Memory::Memcpy(hello, optDataAddr, optLenAddr);

		// Set Hello Data
		context->hello = (uint8_t*)hello;
		context->hellolen = optLenAddr;
		context->helloAddr = optDataAddr;
	}
	else {
		// Delete Hello Data
		context->hellolen = 0;
		context->helloAddr = 0;
		//free(context->hello); // Doesn't need to free it since it will be reused later
		//context->hello = NULL;
	}

	// Return Success
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetAdhocMatchingGetMembers(int matchingId, u32 sizeAddr, u32 buf) {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingGetMembers(%i, [%08x]=%i, %08x) at %08x", matchingId, sizeAddr, Memory::Read_U32(sizeAddr), buf, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	if (!netAdhocMatchingInited)
		return hleLogDebug(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");

	// Minimum Argument
	if (!Memory::IsValidAddress(sizeAddr))
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");

	// Multithreading Lock
	peerlock.lock();
	// Find Matching Context
	SceNetAdhocMatchingContext* context = findMatchingContext(matchingId);
	// Multithreading Unlock
	peerlock.unlock();

	// Context not found
	if (context == NULL)
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");

	// Context not running
	if (!context->running)
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");

	// Buffer Length not available
	if (!Memory::IsValidAddress(sizeAddr))
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");

	int* buflen = (int*)Memory::GetPointer(sizeAddr);
	SceNetAdhocMatchingMemberInfoEmu* buf2 = NULL;
	if (Memory::IsValidAddress(buf)) {
		buf2 = (SceNetAdhocMatchingMemberInfoEmu*)Memory::GetPointer(buf);
	}

	// Number of Connected Peers, should we exclude timeout members?
	bool excludeTimedout = false; // false;
	uint32_t peercount = countConnectedPeers(context, excludeTimedout);

	// Calculate Connected Peer Bytesize
	int available = sizeof(SceNetAdhocMatchingMemberInfoEmu) * peercount;

	// Length Returner Mode
	if (buf == 0) {
		// Get Connected Peer Count
		*buflen = available;
		DEBUG_LOG(Log::sceNet, "MemberList [Connected: %i]", peercount);
	}

	// Normal Mode
	else {
		// Fix Negative Length
		if ((*buflen) < 0) *buflen = 0;

		// Fix Oversize Request
		if ((*buflen) > available) *buflen = available;

		// Clear Memory
		memset(buf2, 0, *buflen);

		// Calculate Requested Peer Count
		int requestedpeers = (*buflen) / sizeof(SceNetAdhocMatchingMemberInfoEmu);

		// Filled Request Counter
		int filledpeers = 0;

		if (requestedpeers > 0) {
			// Add Self-Peer first, unless if there is existing Parent/P2P peer
			if (peercount == 1 || context->mode != PSP_ADHOC_MATCHING_MODE_CHILD) {
				// Add Local MAC
				buf2[filledpeers++].mac_addr = context->mac;

				DEBUG_LOG(Log::sceNet, "MemberSelf [%s]", mac2str(&context->mac).c_str());
			}

			// Room for more than local peer
			if (requestedpeers > 1) {
				// P2P Mode
				if (context->mode == PSP_ADHOC_MATCHING_MODE_P2P) {
					// Find P2P Brother
					SceNetAdhocMatchingMemberInternal* p2p = findP2P(context, excludeTimedout);

					// P2P Brother found
					if (p2p != NULL) {
						// Faking lastping
						auto friendpeer = findFriend(&p2p->mac);
						if (p2p->lastping != 0 && friendpeer != NULL && friendpeer->last_recv != 0)
							p2p->lastping = std::max(p2p->lastping, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);
						else
							p2p->lastping = 0;

						// Add P2P Brother MAC
						buf2[filledpeers++].mac_addr = p2p->mac;

						DEBUG_LOG(Log::sceNet, "MemberP2P [%s]", mac2str(&p2p->mac).c_str());
					}
				}

				// Parent or Child Mode
				else {
					// Add Parent first
					SceNetAdhocMatchingMemberInternal* parentpeer = findParent(context);
					if (parentpeer != NULL) {
						// Faking lastping
						auto friendpeer = findFriend(&parentpeer->mac);
						if (parentpeer->lastping != 0 && friendpeer != NULL && friendpeer->last_recv != 0)
							parentpeer->lastping = std::max(parentpeer->lastping, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);
						else
							parentpeer->lastping = 0;

						// Add Parent MAC
						buf2[filledpeers++].mac_addr = parentpeer->mac;

						DEBUG_LOG(Log::sceNet, "MemberParent [%s]", mac2str(&parentpeer->mac).c_str());
					}

					// We may need to rearrange children where last joined player placed last
					std::deque<SceNetAdhocMatchingMemberInternal*> sortedPeers;

					// Iterate Peer List
					SceNetAdhocMatchingMemberInternal* peer = context->peerlist;
					for (; peer != NULL && filledpeers < requestedpeers; peer = peer->next) {
						// Should we exclude timedout members?
						if (!excludeTimedout || peer->lastping != 0) {
							// Faking lastping
							auto friendpeer = findFriend(&peer->mac);
							if (peer->lastping != 0 && friendpeer != NULL && friendpeer->last_recv != 0)
								peer->lastping = std::max(peer->lastping, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);
							else
								peer->lastping = 0;

							// Add Peer MAC
							sortedPeers.push_front(peer);
						}
					}

					// Iterate rearranged peers
					for (const auto& peer : sortedPeers) {
						// Parent Mode
						if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
							// Interested in Children
							if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) {
								// Add Child MAC
								buf2[filledpeers++].mac_addr = peer->mac;

								DEBUG_LOG(Log::sceNet, "MemberChild [%s]", mac2str(&peer->mac).c_str());
							}
						}

						// Child Mode
						else {
							// Interested in Siblings
							if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) {
								// Add Peer MAC
								buf2[filledpeers++].mac_addr = peer->mac;

								DEBUG_LOG(Log::sceNet, "MemberSibling [%s]", mac2str(&peer->mac).c_str());
							}
							// Self Peer
							else if (peer->state == 0) {
								// Add Local MAC
								buf2[filledpeers++].mac_addr = peer->mac;

								DEBUG_LOG(Log::sceNet, "MemberSelf [%s]", mac2str(&peer->mac).c_str());
							}

						}
					}
					sortedPeers.clear();
				}
			}

			// Link Result List
			for (int i = 0; i < filledpeers - 1; i++) {
				// Link Next Element
				//buf2[i].next = &buf2[i + 1];
				buf2[i].next = buf + (sizeof(SceNetAdhocMatchingMemberInfoEmu) * (i + 1LL));
			}
			// Fix Last Element
			if (filledpeers > 0) buf2[filledpeers - 1].next = 0;
		}

		// Fix Buffer Size
		*buflen = sizeof(SceNetAdhocMatchingMemberInfoEmu) * filledpeers;
		DEBUG_LOG(Log::sceNet, "MemberList [Requested: %i][Discovered: %i]", requestedpeers, filledpeers);
	}

	// Return Success
	return hleDelayResult(hleLogDebug(Log::sceNet, 0), "delay 100 ~ 1000us", 100); // seems to have different thread running within the delay duration
}

// Gran Turismo may replace the 1st bit of the 1st byte of MAC address's OUI with 0 (unicast bit), or replace the whole 6-bytes of MAC address with all 00 (invalid mac) for unknown reason
int sceNetAdhocMatchingSendData(int matchingId, const char *mac, int dataLen, u32 dataAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingSendData(%i, %s, %i, %08x) at %08x", matchingId, mac2str((SceNetEtherAddr*)mac).c_str(), dataLen, dataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	if (!netAdhocMatchingInited) {
		// Uninitialized Library
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "not initialized");
	}

	if (mac == NULL) {
		// Invalid Arguments
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "invalid arg");
	}

	// Find Matching Context
	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);

	if (context == NULL) {
		// Invalid Matching ID
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "invalid id");
	}

	if (!context->running) {
		// Context not running
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "not running");
	}

	// Invalid Data Length
	if (dataLen <= 0 || dataAddr == 0) {
		// Invalid Data Length
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_DATALEN, "invalid datalen");
	}

	void* data = NULL;
	if (Memory::IsValidAddress(dataAddr)) data = Memory::GetPointerWriteUnchecked(dataAddr);

	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Target Peer
	SceNetAdhocMatchingMemberInternal* peer = findPeer(context, (SceNetEtherAddr*)mac);

	if (peer == NULL) {
		// Peer not found
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "unknown target");
	}

	if (peer->state != PSP_ADHOC_MATCHING_PEER_PARENT && peer->state != PSP_ADHOC_MATCHING_PEER_CHILD && peer->state != PSP_ADHOC_MATCHING_PEER_P2P) {
		// Not connected / accepted
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_ESTABLISHED, "not established");
	}

	// Send in Progress
	if (peer->sending)
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_DATA_BUSY, "data busy");

	// Mark Peer as Sending
	peer->sending = 1;

	// Send Data to Peer
	sendBulkDataPacket(context, &peer->mac, dataLen, data);

	// Return Success
	return hleLogDebug(Log::sceNet, 0);
}

int sceNetAdhocMatchingAbortSendData(int matchingId, const char *mac) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocMatchingAbortSendData(%i, %s)", matchingId, mac2str((SceNetEtherAddr*)mac).c_str());
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	if (!netAdhocMatchingInited) {
		// Uninitialized Library
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
	}

	if (mac == NULL) {
		// Invalid Arguments
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	// Find Matching Context
	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);

	if (context == NULL) {
		// Invalid Matching ID
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");
	}

	if (!context->running) {
		// Context not running
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");
	}

	// Find Target Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, (SceNetEtherAddr *)mac);

	if (peer == NULL) {
		// Peer not found
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "adhocmatching unknown target");
	}

	// Peer is sending
	if (peer->sending) {
		// Set Peer as Bulk Idle
		peer->sending = 0;

		// Stop Bulk Data Sending (if in progress)
		abortBulkTransfer(context, peer);
	}

	// Return Success
	return hleLogDebug(Log::sceNet, 0);
}

// Get the maximum memory usage by the matching library
static int sceNetAdhocMatchingGetPoolMaxAlloc() {
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	// Lazy way out - hardcoded return value
	return hleLogDebug(Log::sceNet, fakePoolSize / 2, "faked value");
}

int sceNetAdhocMatchingGetPoolStat(u32 poolstatPtr) {
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	if (!netAdhocMatchingInited) {
		// Uninitialized Library
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
	}

	SceNetMallocStat * poolstat = NULL;
	if (Memory::IsValidAddress(poolstatPtr)) poolstat = (SceNetMallocStat *)Memory::GetPointer(poolstatPtr);

	if (poolstat == NULL) {
		// Invalid Argument
		return hleLogError(Log::sceNet, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	// Fill Poolstat with Fake Data
	poolstat->pool = fakePoolSize;
	poolstat->maximum = fakePoolSize / 2; // Max usage faked to halt the pool
	poolstat->free = fakePoolSize - poolstat->maximum;

	// Return Success
	return hleLogDebug(Log::sceNet, 0);
}

void __NetMatchingCallbacks() { //(int matchingId)
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	hleSkipDeadbeef();
	// Note: Super Pocket Tennis / Thrillville Off the Rails seems to have a very short timeout (ie. ~5ms) while waiting for the event to arrived on the callback handler, but Lord of Arcana may not work well with 5ms (~3m or ~10ms seems to be good)
	// Games with 4-players or more (ie. Gundam: Senjou No Kizuna Portable) will also need lower delay/latency (ie. ~3ms seems to be good, 2ms or lower doesn't work well) so MatchingEvents can be processed faster, thus won't be piling up in the queue.
	// Using 3ms seems to fix Player list issue on StarWars The Force Unleashed.
	int delayus = 3000;

	auto params = matchingEvents.begin();
	if (params != matchingEvents.end()) {
		u32_le args[6];
		memcpy(args, params->data, sizeof(args));
		auto context = findMatchingContext(args[0]);

		if (actionAfterMatchingMipsCall < 0) {
			actionAfterMatchingMipsCall = __KernelRegisterActionType(AfterMatchingMipsCall::Create);
		}
		DEBUG_LOG(Log::sceNet, "AdhocMatching - Remaining Events: %zu", matchingEvents.size());
		auto peer = findPeer(context, (SceNetEtherAddr*)Memory::GetPointer(args[2]));
		// Discard HELLO Events when in the middle of joining, as some games (ie. Super Pocket Tennis) might tried to join again (TODO: Need to confirm whether sceNetAdhocMatchingSelectTarget supposed to be blocking the current thread or not)
		if (peer == NULL || (args[1] != PSP_ADHOC_MATCHING_EVENT_HELLO || (peer->state != PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST && peer->state != PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST && peer->state != PSP_ADHOC_MATCHING_PEER_CANCEL_IN_PROGRESS))) {
			DEBUG_LOG(Log::sceNet, "AdhocMatchingCallback: [ID=%i][EVENT=%i][%s]", args[0], args[1], mac2str((SceNetEtherAddr *)Memory::GetPointer(args[2])).c_str());

			AfterMatchingMipsCall* after = (AfterMatchingMipsCall*)__KernelCreateAction(actionAfterMatchingMipsCall);
			after->SetData(args[0], args[1], args[2]);
			hleEnqueueCall(args[5], 5, args, after);
			matchingEvents.pop_front();
		}
		else {
			DEBUG_LOG(Log::sceNet, "AdhocMatching - Discarding Callback: [ID=%i][EVENT=%i][%s]", args[0], args[1], mac2str((SceNetEtherAddr*)Memory::GetPointer(args[2])).c_str());
			matchingEvents.pop_front();
		}
	}

	// Must be delayed long enough whenever there is a pending callback. Should it be 10-100ms for Matching Events? or Not Less than the delays on sceNetAdhocMatching HLE?
	hleCall(ThreadManForUser, int, sceKernelDelayThread, delayus);

	hleNoLogVoid();
}


const HLEFunction sceNetAdhocMatching[] = {
	{0X2A2A1E07, &WrapI_U<sceNetAdhocMatchingInit>,                    "sceNetAdhocMatchingInit",                'i', "x"        },
	{0X7945ECDA, &WrapI_V<sceNetAdhocMatchingTerm>,                    "sceNetAdhocMatchingTerm",                'i', ""         },
	{0XCA5EDA6F, &WrapI_IIIIIIIIU<sceNetAdhocMatchingCreate>,          "sceNetAdhocMatchingCreate",              'i', "iiiiiiiix"},
	{0X93EF3843, &WrapI_IIIIIIU<sceNetAdhocMatchingStart>,             "sceNetAdhocMatchingStart",               'i', "iiiiiix"  },
	{0xE8454C65, &WrapI_IIIIIIIIU<sceNetAdhocMatchingStart2>,          "sceNetAdhocMatchingStart2",              'i', "iiiiiiiix"},
	{0X32B156B3, &WrapI_I<sceNetAdhocMatchingStop>,                    "sceNetAdhocMatchingStop",                'i', "i"        },
	{0XF16EAF4F, &WrapI_I<sceNetAdhocMatchingDelete>,                  "sceNetAdhocMatchingDelete",              'i', "i"        },
	{0X5E3D4B79, &WrapI_ICIU<sceNetAdhocMatchingSelectTarget>,         "sceNetAdhocMatchingSelectTarget",        'i', "isix"     },
	{0XEA3C6108, &WrapI_IC<sceNetAdhocMatchingCancelTarget>,           "sceNetAdhocMatchingCancelTarget",        'i', "is"       },
	{0X8F58BEDF, &WrapI_ICIU<sceNetAdhocMatchingCancelTargetWithOpt>,  "sceNetAdhocMatchingCancelTargetWithOpt", 'i', "isix"     },
	{0XB5D96C2A, &WrapI_IUU<sceNetAdhocMatchingGetHelloOpt>,           "sceNetAdhocMatchingGetHelloOpt",         'i', "ixx"      },
	{0XB58E61B7, &WrapI_IIU<sceNetAdhocMatchingSetHelloOpt>,           "sceNetAdhocMatchingSetHelloOpt",         'i', "iix"      },
	{0XC58BCD9E, &WrapI_IUU<sceNetAdhocMatchingGetMembers>,            "sceNetAdhocMatchingGetMembers",          'i', "ixx"      },
	{0XF79472D7, &WrapI_ICIU<sceNetAdhocMatchingSendData>,             "sceNetAdhocMatchingSendData",            'i', "isix"     },
	{0XEC19337D, &WrapI_IC<sceNetAdhocMatchingAbortSendData>,          "sceNetAdhocMatchingAbortSendData",       'i', "is"       },
	{0X40F8F435, &WrapI_V<sceNetAdhocMatchingGetPoolMaxAlloc>,         "sceNetAdhocMatchingGetPoolMaxAlloc",     'i', ""         },
	{0X9C5CFB7D, &WrapI_U<sceNetAdhocMatchingGetPoolStat>,             "sceNetAdhocMatchingGetPoolStat",         'i', "x"        },
	// Fake function for PPSSPP's use.
	{0X756E6F00, &WrapV_V<__NetMatchingCallbacks>,                     "__NetMatchingCallbacks",                 'v', ""         },
};


void Register_sceNetAdhocMatching() {
	RegisterHLEModule("sceNetAdhocMatching", ARRAY_SIZE(sceNetAdhocMatching), sceNetAdhocMatching);
}

void __NetAdhocMatchingInit() {
	netAdhocMatchingInited = false;
}

void __NetAdhocMatchingShutdown() {
	NetAdhocMatching_Term();
}

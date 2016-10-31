// Copyright (c) 2013- PPSSPP Project.

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


// proAdhoc

// This is a direct port of Coldbird's code from http://code.google.com/p/aemu/
// All credit goes to him!

#include <cstring>
#include "util/text/parsers.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelMemory.h"
#include "proAdhoc.h" 
#include "i18n/i18n.h"

uint16_t portOffset = g_Config.iPortOffset;
uint32_t fakePoolSize                 = 0;
SceNetAdhocMatchingContext * contexts = NULL;
int one                               = 1;
bool friendFinderRunning              = false;
SceNetAdhocctlPeerInfo * friends      = NULL;
SceNetAdhocctlScanInfo * networks     = NULL;
SceNetAdhocctlScanInfo * newnetworks  = NULL;
int threadStatus                      = ADHOCCTL_STATE_DISCONNECTED;

bool IsAdhocctlInCB = false;
int actionAfterMatchingMipsCall;

// Broadcast MAC
uint8_t broadcastMAC[ETHER_ADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

int metasocket;
SceNetAdhocctlParameter parameter;
SceNetAdhocctlAdhocId product_code;
std::thread friendFinderThread;
std::recursive_mutex peerlock;
SceNetAdhocPdpStat * pdp[255];
SceNetAdhocPtpStat * ptp[255];
uint32_t localip;
std::vector<std::string> chatLog;
std::string name = "";
std::string incoming = "";
std::string message = "";
bool chatScreenVisible = false;
bool updateChatScreen = false;
int newChat = 0;

int isLocalMAC(const SceNetEtherAddr * addr) {
	SceNetEtherAddr saddr;
	getLocalMac(&saddr);

	// Compare MAC Addresses
	int match = memcmp((const void *)addr, (const void *)&saddr, ETHER_ADDR_LEN);

	// Return Result
	return (match == 0);
}

int isPDPPortInUse(uint16_t port) {
	// Iterate Elements
	int i = 0; for (; i < 255; i++) if (pdp[i] != NULL && pdp[i]->lport == port) return 1;

	// Unused Port
	return 0;
}

int isPTPPortInUse(uint16_t port) {
	// Iterate Sockets
	int i = 0; for(; i < 255; i++) if(ptp[i] != NULL && ptp[i]->lport == port) return 1;
	
	// Unused Port
	return 0;
}

SceNetAdhocMatchingMemberInternal* addMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac) {
	if (context == NULL || mac == NULL) return NULL;
	
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);
	// Member is not added yet
	if (peer == NULL) { 
		peer = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));
		if (peer != NULL) {
			memset(peer, 0, sizeof(SceNetAdhocMatchingMemberInternal));
			peer->mac = *mac;
			peer->next = context->peerlist;
			context->peerlist = peer;
		}
	}
	return peer;
}

void addFriend(SceNetAdhocctlConnectPacketS2C * packet) {
	if (packet == NULL) return;

	// Multithreading Lock
	std::lock_guard<std::recursive_mutex> guard(peerlock);

	SceNetAdhocctlPeerInfo * peer = findFriend(&packet->mac);
	// Already existed
	if (peer != NULL) {
		peer->nickname = packet->name;
		peer->mac_addr = packet->mac;
		peer->ip_addr = packet->ip;
		// Update TimeStamp
		peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
	}
	else
	{
		// Allocate Structure
		peer = (SceNetAdhocctlPeerInfo *)malloc(sizeof(SceNetAdhocctlPeerInfo));
		// Allocated Structure
		if (peer != NULL) {
			// Clear Memory
			memset(peer, 0, sizeof(SceNetAdhocctlPeerInfo));

			// Save Nickname
			peer->nickname = packet->name;

			// Save MAC Address
			peer->mac_addr = packet->mac;

			// Save IP Address
			peer->ip_addr = packet->ip;

			// TimeStamp
			peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();

			// Link to existing Peers
			peer->next = friends;

			// Link into Peerlist
			friends = peer;
		}
	}
}

SceNetAdhocctlPeerInfo * findFriend(SceNetEtherAddr * MAC) {
	if (MAC == NULL) return NULL;

	// Friends Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Friends
	for (; peer != NULL; peer = peer->next) {
		if (IsMatch(peer->mac_addr, *MAC)) break;
	}

	// Return found friend
	return peer;
}

void changeBlockingMode(int fd, int nonblocking) {
	unsigned long on = 1;
	unsigned long off = 0;
#ifdef _MSC_VER
	if (nonblocking){
		// Change to Non-Blocking Mode
		ioctlsocket(fd, FIONBIO, &on);
	}
	else {
		// Change to Blocking Mode
		ioctlsocket(fd, FIONBIO, &off);
	}
#else
	if(nonblocking == 1) fcntl(fd, F_SETFL, O_NONBLOCK);
	else {
		// Get Flags
		int flags = fcntl(fd, F_GETFL);
		// Remove Non-Blocking Flag
		fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	}
#endif
}

int countAvailableNetworks(void) {
	// Network Count
	int count = 0;

	// Group Reference
	SceNetAdhocctlScanInfo * group = networks;

	// Count Groups
	for (; group != NULL; group = group->next) count++;

	// Return Network Count
	return count;
}

SceNetAdhocctlScanInfo * findGroup(SceNetEtherAddr * MAC) {
	if (MAC == NULL) return NULL;

	// Groups Reference
	SceNetAdhocctlScanInfo * group = networks;

	// Iterate Groups
	for (; group != NULL; group = group->next) {
		if (IsMatch(group->bssid.mac_addr, *MAC)) break;
	}

	// Return found group
	return group;
}

void freeGroupsRecursive(SceNetAdhocctlScanInfo * node) {
	// End of List
	if (node == NULL) return;

	// Increase Recursion Depth
	freeGroupsRecursive(node->next);

	// Free Memory
	free(node);
}

void deleteAllPDP(void) {
	// Iterate Element
	int i = 0; for (; i < 255; i++) {
		// Active Socket
		if (pdp[i] != NULL) {
			// Close Socket
			closesocket(pdp[i]->id);

			// Free Memory
			free(pdp[i]);

			// Delete Reference
			pdp[i] = NULL;
		}
	}
}

void deleteAllPTP(void) {
	// Iterate Element
	int i = 0; for (; i < 255; i++) {
		// Active Socket
		if (ptp[i] != NULL) {
			// Close Socket
			closesocket(ptp[i]->id);

			// Free Memory
			free(ptp[i]);

			// Delete Reference
			ptp[i] = NULL;
		}
	}
}

void deleteFriendByIP(uint32_t ip) {
	// Previous Peer Reference
	SceNetAdhocctlPeerInfo * prev = NULL;

	// Peer Pointer
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Found Peer
		if (peer->ip_addr == ip) {
			// Instead of removing it from the list we'll make it timeout since most Matching games are moving group and may still need the peer data
			peer->last_recv = 0;

			// Multithreading Lock
			peerlock.lock();

			// Unlink Left (Beginning)
			if(prev == NULL)friends = peer->next;

			// Unlink Left (Other)
			else prev->next = peer->next;

			// Multithreading Unlock
			peerlock.unlock();

			// Free Memory
			free(peer);
			peer = NULL;

			// Stop Search
			break;
		}

		// Set Previous Reference
		// TODO: Should this be used by something?
		prev = peer;
	}
}

int findFreeMatchingID(void) {
	// Minimum Matching ID
	int min = 1;

	// Maximum Matching ID
	int max = 0;

	// Find highest Matching ID
	SceNetAdhocMatchingContext * item = contexts; for (; item != NULL; item = item->next) {
		// New Maximum
		if (max < item->id) max = item->id;
	}

	// Find unoccupied ID
	int i = min; for (; i < max; i++) {
		// Found unoccupied ID
		if (findMatchingContext(i) == NULL) return i;
	}

	// Append at virtual end
	return max + 1;
}

SceNetAdhocMatchingContext * findMatchingContext(int id) {
	// Iterate Matching Context List
	SceNetAdhocMatchingContext * item = contexts; for (; item != NULL; item = item->next) { // Found Matching ID
		if (item->id == id) return item;
	}

	// Context not found
	return NULL;
}

/**
* Find Outgoing Request Target Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findOutgoingRequest(SceNetAdhocMatchingContext * context)
{
	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL; peer = peer->next)
	{
		// Found Peer in List
		if (peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST) return peer;
	}

	// Peer not found
	return NULL;
}

/**
* Remove unneeded Peer Data after being accepted to a match
* @param context Matching Context Pointer
*/
void postAcceptCleanPeerList(SceNetAdhocMatchingContext * context)
{
	// Acquire Peer Lock
	peerlock.lock();

	// Iterate Peer List
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	while (peer != NULL)
	{
		// Save next Peer just in case we have to delete this one
		SceNetAdhocMatchingMemberInternal * next = peer->next;

		// Unneeded Peer
		if (peer->state != PSP_ADHOC_MATCHING_PEER_CHILD && peer->state != PSP_ADHOC_MATCHING_PEER_P2P && peer->state != PSP_ADHOC_MATCHING_PEER_PARENT) deletePeer(context, peer);

		// Move to Next Peer
		peer = next;
	}

	// Free Peer Lock
	peerlock.unlock();
}

/**
* Add Sibling-Data that was sent with Accept-Datagram
* @param context Matching Context Pointer
* @param siblingcount Number of Siblings
* @param siblings Sibling MAC Array
*/
void postAcceptAddSiblings(SceNetAdhocMatchingContext * context, int siblingcount, SceNetEtherAddr * siblings)
{
	// Cast Sibling MAC Array to uint8_t
	// PSP CPU has a problem with non-4-byte aligned Pointer Access.
	// As the buffer of "siblings" isn't properly aligned I don't want to risk a crash.
	uint8_t * siblings_u8 = (uint8_t *)siblings;

	// Iterate Siblings
	int i = 0; for (; i < siblingcount; i++)
	{
		// Allocate Memory
		SceNetAdhocMatchingMemberInternal * sibling = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

		// Allocated Memory
		if (sibling != NULL)
		{
			// Clear Memory
			memset(sibling, 0, sizeof(SceNetAdhocMatchingMemberInternal));

			// Save MAC Address
			memcpy(&sibling->mac, siblings_u8 + sizeof(SceNetEtherAddr) * i, sizeof(SceNetEtherAddr));

			// Set Peer State
			sibling->state = PSP_ADHOC_MATCHING_PEER_CHILD;

			// Initialize Ping Timer
			sibling->lastping = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0;

			// Link Peer, should check whether it's already added before
			sibling->next = context->peerlist;
			context->peerlist = sibling;

			// Spawn Established Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, &sibling->mac, 0, NULL);

			INFO_LOG(SCENET, "Accepting Peer %02X:%02X:%02X:%02X:%02X:%02X", sibling->mac.data[0], sibling->mac.data[1], sibling->mac.data[2], sibling->mac.data[3], sibling->mac.data[4], sibling->mac.data[5]);
		}
	}
}

/**
* Count Children Peers (for Parent)
* @param context Matching Context Pointer
* @return Number of Children
*/
s32_le countChildren(SceNetAdhocMatchingContext * context)
{
	// Children Counter
	s32_le count = 0;

	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL; peer = peer->next)
	{
		// Increase Children Counter
		if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) count++;
	}

	// Return Children Count
	return count;
}

/**
* Find Peer in Context by MAC
* @param context Matching Context Pointer
* @param mac Peer MAC Address
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findPeer(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac)
{
	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL; peer = peer->next)
	{
		// Found Peer in List
		if (memcmp(&peer->mac, mac, sizeof(SceNetEtherAddr)) == 0)
		{
			// Return Peer Pointer
			return peer;
		}
	}

	// Peer not found
	return NULL;
}

/**
* Find Parent Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findParent(SceNetAdhocMatchingContext * context)
{
	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL; peer = peer->next)
	{
		// Found Peer in List
		if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT) return peer;
	}

	// Peer not found
	return NULL;
}

/**
* Find P2P Buddy Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findP2P(SceNetAdhocMatchingContext * context)
{
	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	for (; peer != NULL; peer = peer->next)
	{
		// Found Peer in List
		if (peer->state == PSP_ADHOC_MATCHING_PEER_P2P) return peer;
	}

	// Peer not found
	return NULL;
}

/**
* Delete Peer from List
* @param context Matching Context Pointer
* @param peer Internal Peer Reference
*/
void deletePeer(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer)
{
	// Valid Arguments
	if (context != NULL && peer != NULL)
	{
		peerlock.lock();

		// Previous Peer Reference
		SceNetAdhocMatchingMemberInternal * previous = NULL;

		// Iterate Peer List
		SceNetAdhocMatchingMemberInternal * item = context->peerlist; 
		for (; item != NULL; item = item->next)
		{
			// Found Peer Match
			if (item == peer) break;

			// Set Previous Peer
			previous = item;
		}

		if (item != NULL) {
			// Middle Item
			if (previous != NULL) previous->next = item->next;

			// Beginning Item
			else context->peerlist = item->next;

			INFO_LOG(SCENET, "Removing Peer %02X:%02X:%02X:%02X:%02X:%02X", peer->mac.data[0], peer->mac.data[1], peer->mac.data[2], peer->mac.data[3], peer->mac.data[4], peer->mac.data[5]);
		}

		// Free Peer Memory
		free(peer);
		peer = NULL;

		peerlock.unlock();
	}
}

/**
* Safely Link Thread Message to Event Thread Stack
* @param context Matching Context Pointer
* @param message Thread Message Pointer
*/
void linkEVMessage(SceNetAdhocMatchingContext * context, ThreadMessage * message)
{
	// Lock Access
	context->eventlock->lock();

	// Link Message
	message->next = context->event_stack;
	context->event_stack = message;

	// Unlock Access
	context->eventlock->unlock();
}

/**
* Safely Link Thread Message to IO Thread Stack
* @param context Matching Context Pointer
* @param message Thread Message Pointer
*/
void linkIOMessage(SceNetAdhocMatchingContext * context, ThreadMessage * message)
{
	// Lock Access
	context->inputlock->lock();

	// Link Message
	message->next = context->input_stack;
	context->input_stack = message;

	// Unlock Access
	context->inputlock->unlock();
}

/**
* Send Generic Thread Message
* @param context Matching Context Pointer
* @param stack ADHOC_MATCHING_EVENT_STACK or ADHOC_MATCHING_INPUT_STACK
* @param mac Target MAC
* @param opcode Message Opcode
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendGenericMessage(SceNetAdhocMatchingContext * context, int stack, SceNetEtherAddr * mac, int opcode, int optlen, const void * opt)
{
	// Calculate Required Memory Size
	uint32_t size = sizeof(ThreadMessage) + optlen;

	// Allocate Memory
	uint8_t * memory = (uint8_t *)malloc(size);

	// Allocated Memory
	if (memory != NULL)
	{
		// Clear Memory
		memset(memory, 0, size);

		// Cast Header
		ThreadMessage * header = (ThreadMessage *)memory;

		// Set Message Opcode
		header->opcode = opcode;

		// Set Peer MAC Address
		header->mac = *mac;

		// Set Optional Data Length
		header->optlen = optlen;

		// Set Optional Data
		memcpy(memory + sizeof(ThreadMessage), opt, optlen);

		// Link Thread Message
		if (stack == PSP_ADHOC_MATCHING_EVENT_STACK) linkEVMessage(context, header);

		// Link Thread Message to Input Stack
		else linkIOMessage(context, header);

		// Exit Function
		return;
	}

	peerlock.lock();
	// Out of Memory Emergency Delete
	deletePeer(context, findPeer(context, mac));
	peerlock.unlock();
}

/**
* Send Accept Message from P2P -> P2P or Parent -> Children
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendAcceptMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt)
{
	// Send Accept Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_ACCEPT, optlen, opt);
}

/**
* Send Join Request from P2P -> P2P or Children -> Parent
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendJoinRequest(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt)
{
	// Send Join Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_JOIN, optlen, opt);
}

/**
* Send Cancel Message to Peer (has various effects)
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendCancelMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt)
{
	// Send Cancel Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_CANCEL, optlen, opt);
}

/**
* Send Bulk Data to Peer
* @param context Matching Context Pointer
* @param peer Target Peer
* @param datalen Data Length
* @param data Data
*/
void sendBulkData(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int datalen, const void * data)
{
	// Send Bulk Data Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BULK, datalen, data);
}

/**
* Abort Bulk Data Transfer (if in progress)
* @param context Matching Context Pointer
* @param peer Target Peer
*/
void abortBulkTransfer(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer)
{
	// Send Bulk Data Abort Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BULK_ABORT, 0, NULL);
}

/**
* Notify all established Peers about new Kid in the Neighborhood
* @param context Matching Context Pointer
* @param peer New Kid
*/
void sendBirthMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer)
{
	// Send Birth Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BIRTH, 0, NULL);
}

/**
* Notify all established Peers about abandoned Child
* @param context Matching Context Pointer
* @param peer Abandoned Child
*/
void sendDeathMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer)
{
	// Send Death Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_DEATH, 0, NULL);
}

/**
* Return Number of Connected Peers
* @param context Matching Context Pointer
* @return Number of Connected Peers
*/
uint32_t countConnectedPeers(SceNetAdhocMatchingContext * context)
{
	// Peer Count
	uint32_t count = 0;

	// Parent Mode
	if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT)
	{
		// Number of Children + 1 Parent (Self)
		count = countChildren(context) + 1;
	}

	// Child Mode
	else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
	{
		// Default to 1 Child (Self)
		count = 1;

		// Connected to Parent
		if (findParent(context) != NULL)
		{
			// Add Number of Siblings + 1 Parents
			count += countChildren(context) + 1;
		}
	}

	// P2P Mode
	else
	{
		// Default to 1 P2P Client (Self)
		count = 1;

		// Connected to another P2P Client
		if (findP2P(context) != NULL)
		{
			// Add P2P Brother
			count++;
		}
	}

	// Return Peer Count
	return count;
}

/**
* Spawn Local Event for Event Thread
* @param context Matching Context Pointer
* @param event Event ID
* @param mac Event Source MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void spawnLocalEvent(SceNetAdhocMatchingContext * context, int event, SceNetEtherAddr * mac, int optlen, void * opt)
{
	// Spawn Local Event
	sendGenericMessage(context, PSP_ADHOC_MATCHING_EVENT_STACK, mac, event, optlen, opt);
}

/**
* Handle Timeouts in Matching Context
* @param context Matchi]ng Context Pointer
*/
void handleTimeout(SceNetAdhocMatchingContext * context)
{
	peerlock.lock();
	// Iterate Peer List
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; while (peer != NULL)
	{
		// Get Next Pointer (to avoid crash on memory freeing)
		SceNetAdhocMatchingMemberInternal * next = peer->next;

		u64_le now = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0
		// Timeout!
		if ((now - peer->lastping) >= context->timeout) 
		{
			// Spawn Timeout Event
			if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)) ||
				(context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
				(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && peer->state == PSP_ADHOC_MATCHING_PEER_P2P))
				spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_TIMEOUT, &peer->mac, 0, NULL);

			INFO_LOG(SCENET, "TimedOut Peer %02X:%02X:%02X:%02X:%02X:%02X (%lldms)", peer->mac.data[0], peer->mac.data[1], peer->mac.data[2], peer->mac.data[3], peer->mac.data[4], peer->mac.data[5], (context->timeout/1000));

			// Delete Peer from List
			deletePeer(context, peer);
		}

		// Move Pointer
		peer = next;
	}
	peerlock.unlock();
}

/**
* Recursive Stack Cleaner
* @param node Current Thread Message Node
*/
void clearStackRecursive(ThreadMessage * node)
{
	// Not End of List
	if (node != NULL) clearStackRecursive(node->next);

	// Free Last Existing Node of List (NULL is handled in _free)
	free(node);
}

/**
* Clear Thread Stack
* @param context Matching Context Pointer
* @param stack ADHOC_MATCHING_EVENT_STACK or ADHOC_MATCHING_INPUT_STACK
*/
void clearStack(SceNetAdhocMatchingContext * context, int stack)
{
	if (context == NULL) return;

	// Clear Event Stack
	if (stack == PSP_ADHOC_MATCHING_EVENT_STACK)
	{
		context->eventlock->lock();
		// Free Memory Recursively
		clearStackRecursive(context->event_stack);

		// Destroy Reference
		context->event_stack = NULL;
		
		context->eventlock->unlock();
	}

	// Clear IO Stack
	else
	{
		context->inputlock->lock();
		// Free Memory Recursively
		clearStackRecursive(context->input_stack);

		// Destroy Reference
		context->input_stack = NULL;

		context->inputlock->unlock();
	}
}

/**
* Clear Peer List
* @param context Matching Context Pointer
*/
void clearPeerList(SceNetAdhocMatchingContext * context)
{
	// Acquire Peer Lock
	peerlock.lock();

	// Iterate Peer List
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	while (peer != NULL)
	{
		// Grab Next Pointer
		context->peerlist = peer->next; //SceNetAdhocMatchingMemberInternal * next = peer->next;

		// Delete Peer
		free(peer); //deletePeer(context, peer);

		// Move Pointer
		peer = context->peerlist; //peer = next;
	}

	// Free Peer Lock
	peerlock.unlock();
}

bool IsMatchingInCallback(SceNetAdhocMatchingContext * context) {
	bool inCB = false;
	if (context == NULL) return inCB; 
	context->eventlock->lock(); //peerlock.lock();
	inCB = (/*context != NULL &&*/ context->IsMatchingInCB);
	context->eventlock->unlock(); //peerlock.unlock();
	return inCB;
}

void AfterMatchingMipsCall::run(MipsCall &call) {
	if (context == NULL) return;
	DEBUG_LOG(SCENET, "Entering AfterMatchingMipsCall::run [ID=%i][Event=%d] [cbId: %u]", context->id, EventID, call.cbId);
	//u32 v0 = currentMIPS->r[MIPS_REG_V0];
	if (__IsInInterrupt()) ERROR_LOG(SCENET, "AfterMatchingMipsCall::run [ID=%i][Event=%d] is Returning Inside an Interrupt!", context->id, EventID);
	//while (__IsInInterrupt()) sleep_ms(1); // Must not sleep inside callback handler
	context->eventlock->lock();  //peerlock.lock();
	//SceNetAdhocMatchingContext * context = findMatchingContext(ID);
	//if (context != NULL) 
	{
		context->IsMatchingInCB = false;
	}
	context->eventlock->unlock();  //peerlock.unlock();
	//call.setReturnValue(v0);
	DEBUG_LOG(SCENET, "Leaving AfterMatchingMipsCall::run [ID=%i][Event=%d] [retV0: %08x]", context->id, EventID, currentMIPS->r[MIPS_REG_V0]);
}

void AfterMatchingMipsCall::SetContextID(u32 ContextID, u32 eventId) {
	EventID = eventId;
	peerlock.lock();
	context = findMatchingContext(ContextID);
	peerlock.unlock();
}

// Make sure MIPS calls have been fully executed before the next notifyAdhocctlHandlers
void notifyAdhocctlHandlers(u32 flag, u32 error) {
	__UpdateAdhocctlHandlers(flag, error);
	// TODO: We should use after action instead of guessing the time like this
	sleep_ms(20); // Ugly workaround to give time for the mips callback to fully executed, usually only need <16ms
}

// Matching callback is void function: typedef void(*SceNetAdhocMatchingHandler)(int id, int event, SceNetEtherAddr * peer, int optlen, void * opt);
// Important! The MIPS call need to be fully executed before the next MIPS call invoked, as the game (ie. DBZ Tag Team) may need to prepare something for the next callback event to use
// Note: Must not lock peerlock within this function to prevent race-condition with other thread whos owning peerlock and trying to lock context->eventlock owned by this thread
void notifyMatchingHandler(SceNetAdhocMatchingContext * context, ThreadMessage * msg, void * opt, u32 &bufAddr, u32 &bufLen, u32_le * args) {
	//u32_le args[5] = { 0, 0, 0, 0, 0 };
	if ((s32)bufLen < (msg->optlen + 8)) {
		bufLen = msg->optlen + 8;
		if (Memory::IsValidAddress(bufAddr)) userMemory.Free(bufAddr);
		bufAddr = userMemory.Alloc(bufLen);
		INFO_LOG(SCENET, "MatchingHandler: Alloc(%i -> %i) = %08x", msg->optlen + 8, bufLen, bufAddr);
	}
	u8 * optPtr = Memory::GetPointer(bufAddr);
	memcpy(optPtr, &msg->mac, sizeof(msg->mac));
	if (msg->optlen > 0) memcpy(optPtr + 8, opt, msg->optlen);
	args[0] = context->id;
	args[1] = msg->opcode;
	args[2] = bufAddr; // PSP_GetScratchpadMemoryBase() + 0x6000; 
	args[3] = msg->optlen;
	args[4] = args[2] + 8;
	args[5] = context->handler.entryPoint; //not part of callback argument, just borrowing a space to store callback address so i don't need to search the context first later
	
	context->IsMatchingInCB = true;
	// ScheduleEvent_Threadsafe_Immediate seems to get mixed up with interrupt (returning from mipscall inside an interrupt) and getting invalid address before returning from interrupt
	__UpdateMatchingHandler((u64) args);

	// Make sure MIPS call have been fully executed before the next notifyMatchingHandler
	int count = 0;
	while (/*(after != NULL) &&*/ IsMatchingInCallback(context) && (count < 250)) {
		sleep_ms(1);
		count++;
	}
	if (count >= 250) ERROR_LOG(SCENET, "MatchingHandler: Callback Failed to Return within %dms!", count);
	//sleep_ms(20); // Wait a little more (for context switching may be?) to prevent DBZ Tag Team from getting connection lost, but this will cause lags on Lord of Arcana
}

void freeFriendsRecursive(SceNetAdhocctlPeerInfo * node) {
	// End of List
	if (node == NULL) return;

	// Increase Recursion Depth
	freeFriendsRecursive(node->next);

	// Free Memory
	free(node);
}

void sendChat(std::string chatString) {
	SceNetAdhocctlChatPacketC2S chat;
	I18NCategory *n = GetI18NCategory("Networking");
	chat.base.opcode = OPCODE_CHAT;
	//TODO check network inited, check send success or not, chatlog.pushback error on failed send, pushback error on not connected
	if (friendFinderRunning)
	{
		// Send Chat to Server 
		if (!chatString.empty()) {
		//maximum char allowed is 64 character for compability with original server (pro.coldbird.net)
		message = chatString.substr(0, 60); // 64 return chat variable corrupted is it out of memory?
		strcpy(chat.message, message.c_str());
		//Send Chat Messages
		int chatResult = send(metasocket, (const char *)&chat, sizeof(chat), 0);
		NOTICE_LOG(SCENET, "Send Chat %s to Adhoc Server", chat.message);
		name = g_Config.sNickName.c_str();
		chatLog.push_back(name.substr(0, 8) + ": " + chat.message);
			if (chatScreenVisible) {
				updateChatScreen = true;
			}
		}
	}
	else {
		chatLog.push_back(n->T("You're in Offline Mode, go to lobby or online hall"));
		if (chatScreenVisible) {
			updateChatScreen = true;
		}
	}
}

std::vector<std::string> getChatLog() {
	// this log used by chat screen
	if (chatLog.size() > 50) {
		//erase the first 40 element limit the chatlog size
		chatLog.erase(chatLog.begin(), chatLog.begin() + 40);
	}
	return chatLog;
}

int friendFinder(){
	// Receive Buffer
	int rxpos = 0;
	uint8_t rx[1024];

	// Chat Packet
	SceNetAdhocctlChatPacketC2S chat;
	chat.base.opcode = OPCODE_CHAT;

	// Last Ping Time
	uint64_t lastping = 0;

	// Last Time Reception got updated
	uint64_t lastreceptionupdate = 0;

	uint64_t now;

	// Log Startup
	INFO_LOG(SCENET, "FriendFinder: Begin of Friend Finder Thread");

	// Finder Loop
	while (friendFinderRunning) {
		// Acquire Network Lock
		//_acquireNetworkLock();

		// Ping Server
		now = real_time_now()*1000000.0; // should be in microseconds, but it seems real_time_now() returns in seconds
		if (now - lastping >= PSP_ADHOCCTL_PING_TIMEOUT) { //100 // We need to use lower interval to prevent getting timeout at Pro Adhoc Server through internet
			// original code : ((sceKernelGetSystemTimeWide() - lastping) >= ADHOCCTL_PING_TIMEOUT)
			// Update Ping Time
			lastping = now;

			// Prepare Packet
			uint8_t opcode = OPCODE_PING;

			// Send Ping to Server, may failed with socket error 10054/10053 if someone else with the same IP already connected to AdHoc Server (the server might need to be modified to differentiate MAC instead of IP)
			int iResult = send(metasocket, (const char *)&opcode, 1, 0);
			/*if (iResult == SOCKET_ERROR) {
			ERROR_LOG(SCENET, "FriendFinder: Socket Error (%i) when sending OPCODE_PING", errno);
			//friendFinderRunning = false;
			}*/
		}

		// Wait for Incoming Data
		int received = recv(metasocket, (char *)(rx + rxpos), sizeof(rx) - rxpos, 0);

		// Free Network Lock
		//_freeNetworkLock();

		// Received Data
		if (received > 0) {
			// Fix Position
			rxpos += received;

			// Log Incoming Traffic
			//printf("Received %d Bytes of Data from Server\n", received);
			INFO_LOG(SCENET, "Received %d Bytes of Data from Adhoc Server", received);
		}

		// Handle Packets
		if (rxpos > 0) {
			// BSSID Packet
			if (rx[0] == OPCODE_CONNECT_BSSID) {
				INFO_LOG(SCENET, "FriendFinder: Incoming OPCODE_CONNECT_BSSID");
				// Enough Data available
				if (rxpos >= (int)sizeof(SceNetAdhocctlConnectBSSIDPacketS2C)) {
					// Cast Packet
					SceNetAdhocctlConnectBSSIDPacketS2C * packet = (SceNetAdhocctlConnectBSSIDPacketS2C *)rx;
					// Update BSSID
					parameter.bssid.mac_addr = packet->mac;
					// Change State
					threadStatus = ADHOCCTL_STATE_CONNECTED;
					// Notify Event Handlers
					notifyAdhocctlHandlers(ADHOCCTL_EVENT_CONNECT, 0);

					// Move RX Buffer
					memmove(rx, rx + sizeof(SceNetAdhocctlConnectBSSIDPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlConnectBSSIDPacketS2C));

					// Fix RX Buffer Length
					rxpos -= sizeof(SceNetAdhocctlConnectBSSIDPacketS2C);
				}
			}

			// Chat Packet
			else if (rx[0] == OPCODE_CHAT) {
				INFO_LOG(SCENET, "FriendFinder: Incoming OPCODE_CHAT");
				// Enough Data available
				if (rxpos >= (int)sizeof(SceNetAdhocctlChatPacketS2C)) {
					// Cast Packet
					SceNetAdhocctlChatPacketS2C * packet = (SceNetAdhocctlChatPacketS2C *)rx;
					// Add Incoming Chat to HUD
					NOTICE_LOG(SCENET, "Received chat message %s", packet->base.message);
					incoming = "";
					name = (char *)packet->name.data;
					incoming.append(name.substr(0, 8));
					incoming.append(": ");
					incoming.append((char *)packet->base.message);
					chatLog.push_back(incoming);
					//im new to pointer btw :( doesn't know its safe or not this should update the chat screen when data coming
					if (chatScreenVisible) {
						updateChatScreen = true;
					}
					else {
						if (newChat < 50) {
							newChat += 1;
						}
					}
					// Move RX Buffer
					memmove(rx, rx + sizeof(SceNetAdhocctlChatPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlChatPacketS2C));

					// Fix RX Buffer Length
					rxpos -= sizeof(SceNetAdhocctlChatPacketS2C);
				}
			}

			// Connect Packet
			else if (rx[0] == OPCODE_CONNECT) {
				DEBUG_LOG(SCENET, "FriendFinder: OPCODE_CONNECT");
				// Enough Data available
				if (rxpos >= (int)sizeof(SceNetAdhocctlConnectPacketS2C)) {
					// Log Incoming Peer
					INFO_LOG(SCENET, "Incoming Peer Data...");

					// Cast Packet
					SceNetAdhocctlConnectPacketS2C * packet = (SceNetAdhocctlConnectPacketS2C *)rx;

					// Add User
					addFriend(packet);
					incoming = "";
					incoming.append((char *)packet->name.data);
					incoming.append(" Joined ");
					//do we need ip?
					//joined.append((char *)packet->ip);
					chatLog.push_back(incoming);
					//im new to pointer btw :( doesn't know its safe or not this should update the chat screen when data coming
					if (chatScreenVisible) {
						updateChatScreen = true;
					}
					// Update HUD User Count
#ifdef LOCALHOST_AS_PEER
					setUserCount(getActivePeerCount());
#else
					// setUserCount(getActivePeerCount()+1);
#endif

					// Move RX Buffer
					memmove(rx, rx + sizeof(SceNetAdhocctlConnectPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlConnectPacketS2C));

					// Fix RX Buffer Length
					rxpos -= sizeof(SceNetAdhocctlConnectPacketS2C);
				}
			}

			// Disconnect Packet
			else if (rx[0] == OPCODE_DISCONNECT) {
				DEBUG_LOG(SCENET, "FriendFinder: OPCODE_DISCONNECT");
				// Enough Data available
				if (rxpos >= (int)sizeof(SceNetAdhocctlDisconnectPacketS2C)) {
					// Log Incoming Peer Delete Request
					INFO_LOG(SCENET, "FriendFinder: Incoming Peer Data Delete Request...");

					// Cast Packet
					SceNetAdhocctlDisconnectPacketS2C * packet = (SceNetAdhocctlDisconnectPacketS2C *)rx;

					// Delete User by IP, should delete by MAC since IP can be shared (behind NAT) isn't?
					deleteFriendByIP(packet->ip);

					// Update HUD User Count
#ifdef LOCALHOST_AS_PEER
					setUserCount(_getActivePeerCount());
#else
					//setUserCount(_getActivePeerCount()+1);
#endif

					// Move RX Buffer
					memmove(rx, rx + sizeof(SceNetAdhocctlDisconnectPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlDisconnectPacketS2C));

					// Fix RX Buffer Length
					rxpos -= sizeof(SceNetAdhocctlDisconnectPacketS2C);
				}
			}

			// Scan Packet
			else if (rx[0] == OPCODE_SCAN) {
				DEBUG_LOG(SCENET, "FriendFinder: OPCODE_SCAN");
				// Enough Data available
				if (rxpos >= (int)sizeof(SceNetAdhocctlScanPacketS2C)) {
					// Log Incoming Network Information
					INFO_LOG(SCENET, "Incoming Group Information...");
					// Cast Packet
					SceNetAdhocctlScanPacketS2C * packet = (SceNetAdhocctlScanPacketS2C *)rx;

					// Multithreading Lock
					peerlock.lock();

					// Should only add non-existing group (or replace an existing group) to prevent Ford Street Racing from showing a strange game session list
					SceNetAdhocctlScanInfo * group = findGroup(&packet->mac);

					if (group != NULL) {
					// Copy Group Name
					group->group_name = packet->group;

					// Set Group Host
					group->bssid.mac_addr = packet->mac;
					}
					else
					{
						// Allocate Structure Data
						SceNetAdhocctlScanInfo * group = (SceNetAdhocctlScanInfo *)malloc(sizeof(SceNetAdhocctlScanInfo));

						// Allocated Structure Data
						if (group != NULL)
						{
							// Clear Memory, should this be done only when allocating new group?
							memset(group, 0, sizeof(SceNetAdhocctlScanInfo));

							// Link to existing Groups
							group->next = newnetworks;

							// Copy Group Name
							group->group_name = packet->group;

							// Set Group Host
							group->bssid.mac_addr = packet->mac;

							// Link into Group List
							newnetworks = group;
						}
					}

					// Multithreading Unlock
					peerlock.unlock();

					// Move RX Buffer
					memmove(rx, rx + sizeof(SceNetAdhocctlScanPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlScanPacketS2C));

					// Fix RX Buffer Length
					rxpos -= sizeof(SceNetAdhocctlScanPacketS2C);
				}
			}

			// Scan Complete Packet
			else if (rx[0] == OPCODE_SCAN_COMPLETE) {
				DEBUG_LOG(SCENET, "FriendFinder: OPCODE_SCAN_COMPLETE");
				// Log Scan Completion
				INFO_LOG(SCENET, "FriendFinder: Incoming Scan complete response...");

				// Reset current networks to prevent leaving host to be listed again
				peerlock.lock();
				freeGroupsRecursive(networks);
				networks = newnetworks;
				newnetworks = NULL;
				peerlock.unlock();

				// Change State
				threadStatus = ADHOCCTL_STATE_DISCONNECTED;

				// Notify Event Handlers
				notifyAdhocctlHandlers(ADHOCCTL_EVENT_SCAN, 0);
				//int i = 0; for(; i < ADHOCCTL_MAX_HANDLER; i++)
				//{
				//        // Active Handler
				//        if(_event_handler[i] != NULL) _event_handler[i](ADHOCCTL_EVENT_SCAN, 0, _event_args[i]);
				//}

				// Move RX Buffer
				memmove(rx, rx + 1, sizeof(rx) - 1);

				// Fix RX Buffer Length
				rxpos -= 1;
			}
		}
		// Original value was 10 ms, I think 100 is just fine
		sleep_ms(1); // Using 1ms for faster response just like AdhocServer

		// Don't do anything if it's paused, otherwise the log will be flooded
		while (Core_IsStepping() && friendFinderRunning) sleep_ms(1);
	}

	// Groups/Networks should be deallocated isn't?

	// Prevent the games from having trouble to reInitiate Adhoc (the next NetInit -> PdpCreate after NetTerm)
	threadStatus = ADHOCCTL_STATE_DISCONNECTED;

	// Log Shutdown
	INFO_LOG(SCENET, "FriendFinder: End of Friend Finder Thread");

	// Return Success
	return 0;
}

int getActivePeerCount(void) {
	// Counter
	int count = 0;

	// #ifdef LOCALHOST_AS_PEER
	// // Increase for Localhost
	// count++;
	// #endif

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Increase Counter
		count++;
	}

	// Return Result
	return count;
}

int getLocalIp(sockaddr_in * SocketAddress){
#if defined(_MSC_VER)
	// Get local host name
	char szHostName[128] = "";

	if(::gethostname(szHostName, sizeof(szHostName))) {
		// Error handling 
	}
	// Get local IP addresses
	struct hostent     *pHost        = 0;
	pHost = ::gethostbyname(szHostName);
	if(pHost) {
		memcpy(&SocketAddress->sin_addr, pHost->h_addr_list[0], pHost->h_length);
		return 0;
	}
	return -1;
#else
	memcpy(&SocketAddress->sin_addr, &localip, sizeof(uint32_t));
	return 0;
#endif
}

uint32_t getLocalIp(int sock) {
	struct sockaddr_in localAddr;
	localAddr.sin_addr.s_addr = INADDR_ANY;
	socklen_t addrLen = sizeof(localAddr);
	getsockname(sock, (struct sockaddr*)&localAddr, &addrLen);
	return localAddr.sin_addr.s_addr;
}

void getLocalMac(SceNetEtherAddr * addr){
	// Read MAC Address from config
	uint8_t mac[ETHER_ADDR_LEN] = {0};
	if (!ParseMacAddress(g_Config.sMACAddress.c_str(), mac)) {
		ERROR_LOG(SCENET, "Error parsing mac address %s", g_Config.sMACAddress.c_str());
	}
	memcpy(addr, mac, ETHER_ADDR_LEN);
}

uint16_t getLocalPort(int sock) {
	struct sockaddr_in localAddr;
	localAddr.sin_port = 0;
	socklen_t addrLen = sizeof(localAddr);
	getsockname(sock, (struct sockaddr*)&localAddr, &addrLen);
	return ntohs(localAddr.sin_port);
}

int getSockBufferSize(int sock, int opt) { // opt = SO_RCVBUF/SO_SNDBUF
	int n = 16384;
	socklen_t m = sizeof(n);
	getsockopt(sock, SOL_SOCKET, opt, (char *)&n, &m); // in linux the value is twice of the value being set using setsockopt
	return (n/2);
}

int setSockBufferSize(int sock, int opt, int size) { // opt = SO_RCVBUF/SO_SNDBUF
	int n = size; // 8192; //16384
	return setsockopt(sock, SOL_SOCKET, opt, (char *)&n, sizeof(n));
}

/**
* Return the Number of Players with the chosen Nickname in the Local Users current Network
* @param nickname To-be-searched Nickname
* @return Number of matching Players
*/
int getNicknameCount(const char * nickname)
{
	// Counter
	int count = 0;

	// Local Nickname Matches
	if (strcmp((char *)parameter.nickname.data, nickname) == 0) count++;

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next)
	{
		// Match found
		if (strcmp((char *)peer->nickname.data, nickname) == 0) count++;
	}

	// Return Result
	return count;
}

/**
* PDP Socket Counter
* @return Number of internal PDP Sockets
*/
int getPDPSocketCount(void)
{
	// Socket Counter
	int counter = 0;

	// Count Sockets
	int i = 0; for (; i < 255; i++) if (pdp[i] != NULL) counter++;

	// Return Socket Count
	return counter;
}

int getPTPSocketCount(void) {
	// Socket Counter
	int counter = 0;

	// Count Sockets
	int i = 0; for (; i < 255; i++) if (ptp[i] != NULL) counter++;

	// Return Socket Count
	return counter;
}

int initNetwork(SceNetAdhocctlAdhocId *adhoc_id){
	int iResult = 0;
	metasocket = (int)INVALID_SOCKET;
	metasocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (metasocket == INVALID_SOCKET){
		ERROR_LOG(SCENET, "Invalid socket");
		return -1;
	}
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT); //27312 // Maybe read this from config too

	// Resolve dns
	addrinfo * resultAddr;
	addrinfo * ptr;
	in_addr serverIp;
	serverIp.s_addr = INADDR_NONE;

	iResult = getaddrinfo(g_Config.proAdhocServer.c_str(),0,NULL,&resultAddr);
	if (iResult != 0) {
		ERROR_LOG(SCENET, "DNS Error (%s)\n", g_Config.proAdhocServer.c_str());
		host->NotifyUserMessage("DNS Error connecting to " + g_Config.proAdhocServer, 8.0f);
		return iResult;
	}
	for (ptr = resultAddr; ptr != NULL; ptr = ptr->ai_next) {
		switch (ptr->ai_family) {
		case AF_INET:
			serverIp = ((sockaddr_in *)ptr->ai_addr)->sin_addr;
			break;
		}
	}
	
	memset(&parameter, 0, sizeof(parameter));
	strcpy((char *)&parameter.nickname.data, g_Config.sNickName.c_str());
	parameter.channel = 1; // Fake Channel 1
	getLocalMac(&parameter.bssid.mac_addr);

	server_addr.sin_addr = serverIp;
	iResult = connect(metasocket,(sockaddr *)&server_addr,sizeof(server_addr));
	if (iResult == SOCKET_ERROR) {
		uint8_t * sip = (uint8_t *)&server_addr.sin_addr.s_addr;
		char buffer[512];
		snprintf(buffer, sizeof(buffer), "Socket error (%i) when connecting to %s/%u.%u.%u.%u:%u", errno, g_Config.proAdhocServer.c_str(), sip[0], sip[1], sip[2], sip[3], ntohs(server_addr.sin_port));
		ERROR_LOG(SCENET, "%s", buffer);
		host->NotifyUserMessage(buffer, 8.0f);
		return iResult;
	}
	//grab local ip for later use better than constant ip on non windows platform
	localip = getLocalIp(metasocket);

	// Prepare Login Packet
	SceNetAdhocctlLoginPacketC2S packet;
	packet.base.opcode = OPCODE_LOGIN;
	SceNetEtherAddr addres;
	getLocalMac(&addres);
	packet.mac = addres;
	strcpy((char *)packet.name.data, g_Config.sNickName.c_str());
	memcpy(packet.game.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);
	int sent = send(metasocket, (char*)&packet, sizeof(packet), 0);
	changeBlockingMode(metasocket, 1); // Change to non-blocking
	if (sent > 0) {
		I18NCategory *n = GetI18NCategory("Networking");
		host->NotifyUserMessage(n->T("Network Initialized"), 1.0);
		return 0;
	}
	else{
		return -1;
	}
}

bool isBroadcastMAC(const SceNetEtherAddr * addr) {
	// Broadcast MAC
	if (memcmp(addr->data, "\xFF\xFF\xFF\xFF\xFF\xFF", ETHER_ADDR_LEN) == 0) return true;
	// Normal MAC
	return false;
}

bool resolveIP(uint32_t ip, SceNetEtherAddr * mac) {
	sockaddr_in addr;
	getLocalIp(&addr);
	uint32_t localIp = addr.sin_addr.s_addr;

	if (ip == localIp){
		getLocalMac(mac);
		return true;
	}

	// Multithreading Lock
	peerlock.lock();

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Found Matching Peer
		if (peer->ip_addr == ip) {
			// Copy Data
			*mac = peer->mac_addr;

			// Multithreading Unlock
			peerlock.unlock();

			// Return Success
			return true;
		}
	}

	// Multithreading Unlock
	peerlock.unlock();

	// Peer not found
	return false;
}

bool resolveMAC(SceNetEtherAddr * mac, uint32_t * ip) {
	// Get Local MAC Address
	SceNetEtherAddr localMac;
	getLocalMac(&localMac);
	// Local MAC Requested
	if (memcmp(&localMac, mac, sizeof(SceNetEtherAddr)) == 0) {
		// Get Local IP Address
		sockaddr_in sockAddr;
		getLocalIp(&sockAddr);
		*ip = sockAddr.sin_addr.s_addr;
		return true; // return succes
	}

	// Multithreading Lock
	std::lock_guard<std::recursive_mutex> guard(peerlock);

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Found Matching Peer
		if (memcmp(&peer->mac_addr, mac, sizeof(SceNetEtherAddr)) == 0) {
			// Copy Data
			*ip = peer->ip_addr;

			// Return Success
			return true;
		}
	}

	// Peer not found
	return false;
}

bool validNetworkName(const SceNetAdhocctlGroupName * group_name) {
	// Result
	bool valid = true;

	// Name given
	if (group_name != NULL) {
		// Iterate Name Characters
		int i = 0; for (; i < ADHOCCTL_GROUPNAME_LEN && valid; i++) {
			// End of Name
			if (group_name->data[i] == 0) break;

			// Not a digit
			if (group_name->data[i] < '0' || group_name->data[i] > '9') {
				// Not 'A' to 'Z'
				if (group_name->data[i] < 'A' || group_name->data[i] > 'Z') {
					// Not 'a' to 'z'
					if (group_name->data[i] < 'a' || group_name->data[i] > 'z') {
						// Invalid Name
						valid = false;
					}
				}
			}
		}
	}
	// Return Result
	return valid;
}

u64 join32(u32 num1, u32 num2){
	return (u64)num2 << 32 | num1;
}

void split64(u64 num, int buff[]){
	int num1 = (int)(num&firstMask);
	int num2 = (int)((num&secondMask) >> 32);
	buff[0] = num1;
	buff[1] = num2;
}

const char* getMatchingEventStr(int code) {
	const char *buf = NULL;
	switch (code) {
	case PSP_ADHOC_MATCHING_EVENT_HELLO:
		buf = "HELLO"; break;
	case PSP_ADHOC_MATCHING_EVENT_REQUEST:
		buf = "JOIN"; break;
	case PSP_ADHOC_MATCHING_EVENT_LEAVE:
		buf = "LEAVE"; break;
	case PSP_ADHOC_MATCHING_EVENT_DENY:
		buf = "REJECT"; break;
	case PSP_ADHOC_MATCHING_EVENT_CANCEL:
		buf = "CANCEL"; break;
	case PSP_ADHOC_MATCHING_EVENT_ACCEPT:
		buf = "ACCEPT"; break;
	case PSP_ADHOC_MATCHING_EVENT_ESTABLISHED:
		buf = "ESTABLISHED"; break;
	case PSP_ADHOC_MATCHING_EVENT_TIMEOUT:
		buf = "TIMEOUT"; break;
	case PSP_ADHOC_MATCHING_EVENT_ERROR:
		buf = "ERROR"; break;
	case PSP_ADHOC_MATCHING_EVENT_BYE:
		buf = "DISCONNECT"; break;
	case PSP_ADHOC_MATCHING_EVENT_DATA:
		buf = "DATA"; break;
	case PSP_ADHOC_MATCHING_EVENT_DATA_ACK:
		buf = "DATA_ACK"; break;
	case PSP_ADHOC_MATCHING_EVENT_DATA_TIMEOUT:
		buf = "DATA_TIMEOUT"; break;
	case PSP_ADHOC_MATCHING_EVENT_INTERNAL_PING:
		buf = "INTERNAL_PING"; break;
	default:
		buf = "UNKNOWN";
	}
	return buf;
}

const char* getMatchingOpcodeStr(int code) {
	const char *buf = NULL;
	switch (code) {
	case PSP_ADHOC_MATCHING_PACKET_PING:
		buf = "PING"; break;
	case PSP_ADHOC_MATCHING_PACKET_HELLO:
		buf = "HELLO"; break;
	case PSP_ADHOC_MATCHING_PACKET_JOIN:
		buf = "JOIN"; break;
	case PSP_ADHOC_MATCHING_PACKET_ACCEPT:
		buf = "ACCEPT"; break;
	case PSP_ADHOC_MATCHING_PACKET_CANCEL:
		buf = "CANCEL"; break;
	case PSP_ADHOC_MATCHING_PACKET_BULK:
		buf = "BULK"; break;
	case PSP_ADHOC_MATCHING_PACKET_BULK_ABORT:
		buf = "BULK_ABORT"; break;
	case PSP_ADHOC_MATCHING_PACKET_BIRTH:
		buf = "BIRTH"; break;
	case PSP_ADHOC_MATCHING_PACKET_DEATH:
		buf = "DEATH"; break;
	case PSP_ADHOC_MATCHING_PACKET_BYE:
		buf = "BYE"; break;
	default:
		buf = "UNKNOWN";
	}
	return buf;
}


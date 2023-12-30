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

#if defined(_WIN32)
#include <WinSock2.h>
#include "Common/CommonWindows.h"
#endif

#if !defined(_WIN32)
#include <sys/types.h>
#include <netinet/tcp.h>
#endif

#ifndef MSG_NOSIGNAL
// Default value to 0x00 (do nothing) in systems where it's not supported.
#define MSG_NOSIGNAL 0x00
#endif

#include <mutex>
// sceNetAdhoc

// This is a direct port of Coldbird's code from http://code.google.com/p/aemu/
// All credit goes to him!
#include "Common/Data/Text/I18n.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/System/OSD.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"

#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Util/PortManager.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Reporting.h"
#include "Core/MemMapHelpers.h"

#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/proAdhocServer.h"
#include "Core/HLE/KernelWaitHelpers.h"


// shared in sceNetAdhoc.h since it need to be used from sceNet.cpp also
// TODO: Make accessor functions instead, and throw all this state in a struct.
bool netAdhocInited;
bool netAdhocctlInited;
bool networkInited = false;

#define DISCOVER_DURATION_US	2000000 // 2 seconds is probably the normal time it takes for PSP to connect to a group (ie. similar to NetconfigDialog time)
u64 netAdhocDiscoverStartTime = 0;
s32 netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_NONE;
bool netAdhocDiscoverIsStopping = false;
SceNetAdhocDiscoverParam* netAdhocDiscoverParam = nullptr;
u32 netAdhocDiscoverBufAddr = 0;

bool netAdhocGameModeEntered = false;
int netAdhocEnterGameModeTimeout = 15000000; // 15 sec as default timeout, to wait for all players to join

bool netAdhocMatchingInited;
int netAdhocMatchingStarted = 0;
int adhocDefaultTimeout = 5000000; //2000000 usec // For some unknown reason, sometimes it tooks more than 2 seconds for Adhocctl Init to connect to AdhocServer on localhost (normally only 10 ms), and sometimes it tooks more than 1 seconds for built-in AdhocServer to be ready (normally only 1 ms)
int adhocDefaultDelay = 10000; //10000
int adhocExtraDelay = 20000; //20000
int adhocEventPollDelay = 100000; //100000; // Same timings with PSP_ADHOCCTL_RECV_TIMEOUT ?
int adhocMatchingEventDelay = 30000; //30000
int adhocEventDelay = 2000000; //2000000 on real PSP ?
u32 defaultLastRecvDelta = 10000; //10000 usec worked well for games published by Falcom (ie. Ys vs Sora Kiseki, Vantage Master Portable)

SceUID threadAdhocID;

std::recursive_mutex adhocEvtMtx;
std::deque<std::pair<u32, u32>> adhocctlEvents;
std::deque<MatchingArgs> matchingEvents;
std::map<int, AdhocctlHandler> adhocctlHandlers;
std::vector<SceUID> matchingThreads;
int IsAdhocctlInCB = 0;

int adhocctlNotifyEvent = -1;
int adhocctlStateEvent = -1;
int adhocSocketNotifyEvent = -1;
std::map<int, AdhocctlRequest> adhocctlRequests;
std::map<u64, AdhocSocketRequest> adhocSocketRequests;
std::map<u64, AdhocSendTargets> sendTargetPeers;

int gameModeNotifyEvent = -1;

u32 dummyThreadHackAddr = 0;
u32_le dummyThreadCode[3];
u32 matchingThreadHackAddr = 0;
u32_le matchingThreadCode[3];

int matchingEventThread(int matchingId); 
int matchingInputThread(int matchingId); 
void sendBulkDataPacket(SceNetAdhocMatchingContext* context, SceNetEtherAddr* mac, int datalen, void* data);
int AcceptPtpSocket(int ptpId, int newsocket, sockaddr_in& peeraddr, SceNetEtherAddr* addr, u16_le* port);
int PollAdhocSocket(SceNetAdhocPollSd* sds, int count, int timeout, int nonblock);
int FlushPtpSocket(int socketId);
int RecreatePtpSocket(int ptpId);
int NetAdhocGameMode_DeleteMaster();
int NetAdhocctl_ExitGameMode();
int NetAdhocPtp_Connect(int id, int timeout, int flag, bool allowForcedConnect = true);
static int sceNetAdhocPdpCreate(const char* mac, int port, int bufferSize, u32 flag);
static int sceNetAdhocPdpSend(int id, const char* mac, u32 port, void* data, int len, int timeout, int flag);
static int sceNetAdhocPdpRecv(int id, void* addr, void* port, void* buf, void* dataLength, u32 timeout, int flag);

bool __NetAdhocConnected() {
	return netAdhocInited && netAdhocctlInited && (adhocctlState == ADHOCCTL_STATE_CONNECTED || adhocctlState == ADHOCCTL_STATE_GAMEMODE);
}

void __NetAdhocShutdown() {
	// Kill AdhocServer Thread
	adhocServerRunning = false;
	if (adhocServerThread.joinable()) {
		adhocServerThread.join();
	}

	// Checks to avoid confusing logspam
	if (netAdhocMatchingInited) {
		NetAdhocMatching_Term();
	}
	if (netAdhocctlInited) {
		NetAdhocctl_Term();
	}
	if (netAdhocInited) {
		NetAdhoc_Term();
	}
	if (dummyThreadHackAddr) {
		kernelMemory.Free(dummyThreadHackAddr);
		dummyThreadHackAddr = 0;
	}
	if (matchingThreadHackAddr) {
		kernelMemory.Free(matchingThreadHackAddr);
		matchingThreadHackAddr = 0;
	}
}

bool IsGameModeActive() {
	return netAdhocGameModeEntered && masterGameModeArea.data;
}

static void __GameModeNotify(u64 userdata, int cyclesLate) {
	SceUID threadID = userdata >> 32;
	u32 error;

	if (IsGameModeActive()) {
		// Need to make sure all replicas have been created before we start syncing data
		if (replicaGameModeAreas.size() == (gameModeMacs.size() - 1)) {
			// Socket's buffer size should fit the largest size from master/replicas, so we waited until Master & all Replicas to be created first before creating the socket, since there are games (ie. Fading Shadows) that use different buffer size for Master and Replica.
			if (gameModeSocket < 0 && !isZeroMAC(&masterGameModeArea.mac)) {
				u8* buf = (u8*)realloc(gameModeBuffer, gameModeBuffSize);
				if (buf)
					gameModeBuffer = buf;

				if ((gameModeSocket = sceNetAdhocPdpCreate((const char*)&masterGameModeArea.mac, ADHOC_GAMEMODE_PORT, gameModeBuffSize, 0)) < 0) {
					ERROR_LOG(SCENET, "GameMode: Failed to create socket (Error %08x)", gameModeSocket);
					__KernelResumeThreadFromWait(threadID, gameModeSocket);
					return;
				}
				else 
					INFO_LOG(SCENET, "GameMode: Synchronizer (%d, %d) has started", gameModeSocket, gameModeBuffSize);
			}
			if (gameModeSocket < 0) {
				// ReSchedule
				CoreTiming::ScheduleEvent(usToCycles(GAMEMODE_UPDATE_INTERVAL) - cyclesLate, gameModeNotifyEvent, userdata);
				return;
			}
			auto sock = adhocSockets[gameModeSocket - 1];
			if (!sock) {
				WARN_LOG(SCENET, "GameMode: Socket (%d) got deleted", gameModeSocket);
				u32 waitVal = __KernelGetWaitValue(threadID, error);
				if (error == 0) {
					__KernelResumeThreadFromWait(threadID, waitVal);
				}
				return;
			}

			// Send Master data
			if (masterGameModeArea.dataUpdated) {
				int sentcount = 0;
				for (auto& gma : replicaGameModeAreas) {
					if (!gma.dataSent && IsSocketReady(sock->data.pdp.id, false, true) > 0) {
						u16_le port = ADHOC_GAMEMODE_PORT;
						auto it = gameModePeerPorts.find(gma.mac);
						if (it != gameModePeerPorts.end())
							port = it->second;
							
						int sent = sceNetAdhocPdpSend(gameModeSocket, (const char*)&gma.mac, port, masterGameModeArea.data, masterGameModeArea.size, 0, ADHOC_F_NONBLOCK);
						if (sent != ERROR_NET_ADHOC_WOULD_BLOCK) {
							gma.dataSent = 1;
							DEBUG_LOG(SCENET, "GameMode: Master data Sent %d bytes to Area #%d [%s]", masterGameModeArea.size, gma.id, mac2str(&gma.mac).c_str());
							sentcount++;
						}
					}
					else if (gma.dataSent) sentcount++;
				}
				if (sentcount == replicaGameModeAreas.size()) 
					masterGameModeArea.dataUpdated = 0;
			}
			// Need to sync (send + recv) all players initial data (data from CreateMaster) after Master + All Replicas are created, and before the first UpdateMaster / UpdateReplica is called for Star Wars The Force Unleashed to show the correct players color on minimap (also prevent Starting issue on other GameMode games)
			else {
				SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_NET, error);
				if (error == 0 && waitID == GAMEMODE_WAITID) {
					// Resume thread after all replicas data have been received
					int recvd = 0;
					for (auto& gma : replicaGameModeAreas) {
						// Either replicas new data has been received or that player has been disconnected
						if (gma.dataUpdated || gma.updateTimestamp == 0) {
							recvd++;
							// Since we're able to receive data, now we're certain that remote player is listening and ready to receive data, so we send initial data one more time in case they're not listening yet on previous attempt (ie. Pocket Pool)
							if (gma.dataUpdated) {
								u16_le port = ADHOC_GAMEMODE_PORT;
								auto it = gameModePeerPorts.find(gma.mac);
								if (it != gameModePeerPorts.end())
									port = it->second;

								sceNetAdhocPdpSend(gameModeSocket, (const char*)&gma.mac, port, masterGameModeArea.data, masterGameModeArea.size, 0, ADHOC_F_NONBLOCK);
							}
						}
					}
					// Resume blocked thread
					u64 now = CoreTiming::GetGlobalTimeUsScaled();
					if (recvd == replicaGameModeAreas.size()) {
						u32 waitVal = __KernelGetWaitValue(threadID, error);
						if (error == 0) {
							DEBUG_LOG(SCENET, "GameMode: Resuming Thread %d after Master data Synced (Result = %08x)", threadID, waitVal);
							__KernelResumeThreadFromWait(threadID, waitVal);
						}
						else
							ERROR_LOG(SCENET, "GameMode: Error (%08x) on WaitValue %d ThreadID %d", error, waitVal, threadID);
					}
					// Attempt to Re-Send initial Master data (in case previous packets were lost)
					else if (static_cast<s64>(now - masterGameModeArea.updateTimestamp) > GAMEMODE_SYNC_TIMEOUT) {
						DEBUG_LOG(SCENET, "GameMode: Attempt to Re-Send Master data after Sync Timeout (%d us)", GAMEMODE_SYNC_TIMEOUT);
						// Reset Sent marker on players who haven't replied yet (except disconnected players)
						for (auto& gma : replicaGameModeAreas)
							if (!gma.dataUpdated && gma.updateTimestamp != 0)
								gma.dataSent = 0;
						masterGameModeArea.updateTimestamp = now;
						masterGameModeArea.dataUpdated = 1;
					}
				}
			}

			// Recv new Replica data when available
			if (IsSocketReady(sock->data.pdp.id, true, false) > 0) {
				SceNetEtherAddr sendermac;
				s32_le senderport = ADHOC_GAMEMODE_PORT;
				s32_le bufsz = gameModeBuffSize;
				int ret = sceNetAdhocPdpRecv(gameModeSocket, &sendermac, &senderport, gameModeBuffer, &bufsz, 0, ADHOC_F_NONBLOCK);
				if (ret >= 0 && bufsz > 0) {
					// Shows a warning if the sender/source port is different than what it supposed to be.
					if (senderport != ADHOC_GAMEMODE_PORT && senderport != gameModePeerPorts[sendermac]) {
						char name[9] = {};
						auto n = GetI18NCategory(I18NCat::NETWORKING);
						peerlock.lock();
						SceNetAdhocctlPeerInfo* peer = findFriend(&sendermac);
						if (peer != NULL)
							truncate_cpy(name, sizeof(name), (const char*)peer->nickname.data);
						WARN_LOG(SCENET, "GameMode: Unknown Source Port from [%s][%s:%u -> %u] (Result=%i, Size=%i)", name, mac2str(&sendermac).c_str(), senderport, ADHOC_GAMEMODE_PORT, ret, bufsz);
						g_OSD.Show(OSDType::MESSAGE_WARNING, std::string(n->T("GM: Data from Unknown Port")) + std::string(" [") + std::string(name) + std::string("]:") + std::to_string(senderport) + std::string(" -> ") + std::to_string(ADHOC_GAMEMODE_PORT) + std::string(" (") + std::to_string(portOffset) + std::string(")"));
						peerlock.unlock();
					}
					// Keeping track of the source port for further communication, in case it was re-mapped by router or ISP for some reason.
					gameModePeerPorts[sendermac] = senderport;

					for (auto& gma : replicaGameModeAreas) {
						if (IsMatch(gma.mac, sendermac)) {
							DEBUG_LOG(SCENET, "GameMode: Replica data Received %d bytes for Area #%d [%s]", bufsz, gma.id, mac2str(&sendermac).c_str());
							memcpy(gma.data, gameModeBuffer, std::min(gma.size, bufsz));
							gma.dataUpdated = 1;
							gma.updateTimestamp = CoreTiming::GetGlobalTimeUsScaled();
							break;
						}
					}
				}
			}
		}

		// ReSchedule
		CoreTiming::ScheduleEvent(usToCycles(GAMEMODE_UPDATE_INTERVAL) - cyclesLate, gameModeNotifyEvent, userdata);
		return;
	}
	INFO_LOG(SCENET, "GameMode Scheduler (%d, %d) has ended", gameModeSocket, gameModeBuffSize);
	u32 waitVal = __KernelGetWaitValue(threadID, error);
	if (error == 0) {
		DEBUG_LOG(SCENET, "GameMode: Resuming Thread %d after Master Deleted (Result = %08x)", threadID, waitVal);
		__KernelResumeThreadFromWait(threadID, waitVal);
	}
}

static void __AdhocctlNotify(u64 userdata, int cyclesLate) {
	SceUID threadID = userdata >> 32;
	int uid = (int)(userdata & 0xFFFFFFFF);

	s64 result = 0;
	u32 error = 0;

	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_NET, error);
	if (waitID == 0 || error != 0) {
		WARN_LOG(SCENET, "sceNetAdhocctl Socket WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
		return;
	}

	// Socket not found?! Should never happen! But if it ever happened (ie. loaded from SaveState where adhocctlRequests got cleared) return BUSY and let the game try again.
	if (adhocctlRequests.find(uid) == adhocctlRequests.end()) {
		WARN_LOG(SCENET, "sceNetAdhocctl Socket WaitID(%i) not found!", uid);
		__KernelResumeThreadFromWait(threadID, ERROR_NET_ADHOCCTL_BUSY);
		return;
	}

	AdhocctlRequest& req = adhocctlRequests[uid];
	int len = 0;

	SceNetAdhocctlConnectPacketC2S packet;
	memset(&packet, 0, sizeof(packet));
	packet.base.opcode = req.opcode;
	packet.group = req.group;

	// Don't send any packets not in these cases (by setting the len to 0)
	switch (req.opcode)
	{
	case OPCODE_CONNECT:
		len = sizeof(packet);
		break;
	case OPCODE_SCAN:
	case OPCODE_DISCONNECT:
		len = 1;
		break;
	}

	if (g_Config.bEnableWlan) {
		// Send Packet if it wasn't succesfully sent before
		int ret = 0;
		int sockerr = 0;
		if (len > 0) {
			ret = SOCKET_ERROR;
			sockerr = EAGAIN;
			// Don't send anything yet if connection to Adhoc Server is still in progress
			if (!isAdhocctlNeedLogin && IsSocketReady((int)metasocket, false, true) > 0) {
				ret = send((int)metasocket, (const char*)&packet, len, MSG_NOSIGNAL);
				sockerr = errno;
				// Successfully Sent or Connection has been closed or Connection failure occurred
				if (ret >= 0 || (ret == SOCKET_ERROR && sockerr != EAGAIN && sockerr != EWOULDBLOCK)) {
					// Prevent from sending again
					req.opcode = 0;
					if (ret == SOCKET_ERROR)
						DEBUG_LOG(SCENET, "sceNetAdhocctl[%i]: Socket Error (%i)", uid, sockerr);
				}
			}
		}

		// Retry until successfully sent. Login packet sent after successfully connected to Adhoc Server (indicated by networkInited), so we're not sending Login again here
		if ((req.opcode == OPCODE_LOGIN && !networkInited) || (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK))) {
			u64 now = (u64)(time_now_d() * 1000000.0);
			if (now - adhocctlStartTime <= static_cast<u64>(adhocDefaultTimeout) + 500) {
				// Try again in another 0.5ms until timedout.
				CoreTiming::ScheduleEvent(usToCycles(500) - cyclesLate, adhocctlNotifyEvent, userdata);
				return;
			}
			else if (req.opcode != OPCODE_LOGIN)
				result = ERROR_NET_ADHOCCTL_BUSY;
		}
	}
	else
		result = ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF;

	u32 waitVal = __KernelGetWaitValue(threadID, error);
	__KernelResumeThreadFromWait(threadID, result);
	DEBUG_LOG(SCENET, "Returning (WaitID: %d, error: %08x) Result (%08x) of sceNetAdhocctl - Opcode: %d, State: %d", waitID, error, (int)result, waitVal, adhocctlState);

	// We are done with this request
	adhocctlRequests.erase(uid);
}

static void __AdhocctlState(u64 userdata, int cyclesLate) {
	SceUID threadID = userdata >> 32;
	int uid = (int)(userdata & 0xFFFFFFFF);
	int event = uid - 1;

	s64 result = 0;
	u32 error = 0;

	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_NET, error);
	if (waitID == 0 || error != 0) {
		WARN_LOG(SCENET, "sceNetAdhocctl State WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
		return;
	}

	u32 waitVal = __KernelGetWaitValue(threadID, error);
	if (error == 0) {
		adhocctlState = waitVal;
		// FIXME: It seems Adhocctl is still busy within the Adhocctl Handler function (ie. during callbacks), 
		// so we should probably set isAdhocctlBusy to false after mispscall are fully executed (ie. in afterAction).
		// But since Adhocctl Handler is optional, there might be cases where there are no handler thus no callback/mipcall being triggered,
		// so we should probably need to set isAdhocctlBusy to false here too as a workaround (or may be there is internal handler by default?)
		if (adhocctlHandlers.empty())
			isAdhocctlBusy = false;
	}

	__KernelResumeThreadFromWait(threadID, result);
	DEBUG_LOG(SCENET, "Returning (WaitID: %d, error: %08x) Result (%08x) of sceNetAdhocctl - Event: %d, State: %d", waitID, error, (int)result, event, adhocctlState);
}

// Used to simulate blocking on metasocket when send OP code to AdhocServer
int WaitBlockingAdhocctlSocket(AdhocctlRequest request, int usec, const char* reason) {
	int uid = (metasocket <= 0) ? 1 : (int)metasocket;

	if (adhocctlRequests.find(uid) != adhocctlRequests.end()) {
		WARN_LOG(SCENET, "sceNetAdhocctl - WaitID[%d] already existed, Socket is busy!", uid);
		return ERROR_NET_ADHOCCTL_BUSY;
	}

	u64 param = ((u64)__KernelGetCurThread()) << 32 | uid;
	adhocctlStartTime = (u64)(time_now_d() * 1000000.0);
	adhocctlRequests[uid] = request;
	CoreTiming::ScheduleEvent(usToCycles(usec), adhocctlNotifyEvent, param);
	__KernelWaitCurThread(WAITTYPE_NET, uid, request.opcode, 0, false, reason);

	// Always returning a success when waiting for callback, since error code returned via callback?
	return 0;
}

// Used to change Adhocctl State after a delay and before executing callback mipscall (since we don't have beforeAction)
int ScheduleAdhocctlState(int event, int newState, int usec, const char* reason) {
	int uid = event + 1;

	u64 param = ((u64)__KernelGetCurThread()) << 32 | uid;
	CoreTiming::ScheduleEvent(usToCycles(usec), adhocctlStateEvent, param);
	__KernelWaitCurThread(WAITTYPE_NET, uid, newState, 0, false, reason);

	return 0;
}

int StartGameModeScheduler() {
	INFO_LOG(SCENET, "Initiating GameMode Scheduler");
	if (CoreTiming::IsScheduled(gameModeNotifyEvent)) {
		WARN_LOG(SCENET, "GameMode Scheduler is already running!");
		return -1;
	}
	u64 param = ((u64)__KernelGetCurThread()) << 32;
	CoreTiming::ScheduleEvent(usToCycles(GAMEMODE_INIT_DELAY), gameModeNotifyEvent, param);
	return 0;
}

int DoBlockingPdpRecv(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = ERROR_NET_ADHOC_SOCKET_DELETED;
		return 0;
	}
	auto& pdpsocket = sock->data.pdp;
	if (sock->flags & ADHOC_F_ALERTRECV) {
		result = ERROR_NET_ADHOC_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTRECV;
		return 0;
	}

	int ret;
	int sockerr;
	SceNetEtherAddr mac;
	struct sockaddr_in sin;
	socklen_t sinlen;

	sinlen = sizeof(sin);
	memset(&sin, 0, sinlen);

	// On Windows: MSG_TRUNC are not supported on recvfrom (socket error WSAEOPNOTSUPP), so we use dummy buffer as an alternative
	ret = recvfrom(pdpsocket.id, dummyPeekBuf64k, dummyPeekBuf64kSize, MSG_PEEK | MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
	sockerr = errno;

	// Discard packets from IP that can't be translated into MAC address to prevent confusing the game, since the sender MAC won't be updated and may contains invalid/undefined value.
	// TODO: In order to discard packets from unresolvable IP (can't be translated into player's MAC) properly, we'll need to manage the socket buffer ourself, 
	//       by reading the whole available data, separates each datagram and discard unresolvable one, so we can calculate the correct number of available data to recv on GetPdpStat too.
	//       We may also need to implement encryption (or a simple checksum will do) in order to validate the packet to findout whether it came from PPSSPP or a different App that may be sending/broadcasting data to the same port being used by a game 
	//       (in case the IP was resolvable but came from a different App, which will need to be discarded too)
	if (ret != SOCKET_ERROR && !resolveIP(sin.sin_addr.s_addr, &mac)) {
		// Remove the packet from socket buffer
		sinlen = sizeof(sin);
		memset(&sin, 0, sinlen);
		recvfrom(pdpsocket.id, dummyPeekBuf64k, dummyPeekBuf64kSize, MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
		// Try again later, until timeout reached
		u64 now = (u64)(time_now_d() * 1000000.0);
		if (req.timeout != 0 && now - req.startTime > req.timeout) {
			result = ERROR_NET_ADHOC_TIMEOUT;
			DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i]: Discard Timeout", req.id);
			return 0;
		}
		else
			return -1;
	}

	// At this point we assumed that the packet is a valid PPSSPP packet
	if (ret > 0 && *req.length > 0)
		memcpy(req.buffer, dummyPeekBuf64k, std::min(ret, *req.length));

	// Note: UDP must not be received partially, otherwise leftover data in socket's buffer will be discarded
	if (ret >= 0 && ret <= *req.length) {
		sinlen = sizeof(sin);
        memset(&sin, 0, sinlen);
		ret = recvfrom(pdpsocket.id, (char*)req.buffer, std::max(0, *req.length), MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
		// UDP can also receives 0 data, while on TCP receiving 0 data = connection gracefully closed, but not sure whether PDP can send/recv 0 data or not tho
		*req.length = 0;
		if (ret >= 0) {
			DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %u bytes from %s:%u\n", req.id, getLocalPort(pdpsocket.id), ret, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));

			// Find Peer MAC
			if (resolveIP(sin.sin_addr.s_addr, &mac)) {
				// Provide Sender Information
				*req.remoteMAC = mac;
				*req.remotePort = ntohs(sin.sin_port) - portOffset;

				// Save Length
				*req.length = ret;

				// Update last recv timestamp
				peerlock.lock();
				auto peer = findFriend(&mac);
				if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
				peerlock.unlock();
			}
			// Unknown Peer
			else {
				*req.length = ret;
				*req.remotePort = ntohs(sin.sin_port) - portOffset;

				WARN_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %i bytes from Unknown Peer %s:%u", req.id, getLocalPort(pdpsocket.id), ret, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));
			}
		}
		result = 0;
	}
	// On Windows: recvfrom on UDP can get error WSAECONNRESET when previous sendto's destination is unreachable (or destination port is not bound yet), may need to disable SIO_UDP_CONNRESET error
	else if (sockerr == EAGAIN || sockerr == EWOULDBLOCK || sockerr == ECONNRESET) {
		u64 now = (u64)(time_now_d() * 1000000.0);
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			// Try again later
			return -1;
		}
		else
			result = ERROR_NET_ADHOC_TIMEOUT;
	}
	// Returning required buffer size when available data in recv buffer is larger than provided buffer size
	else if (ret > *req.length) {
		WARN_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Peeked %u/%u bytes from %s:%u\n", req.id, getLocalPort(pdpsocket.id), ret, *req.length, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));
		*req.length = ret;

		// Find Peer MAC
		if (resolveIP(sin.sin_addr.s_addr, &mac)) {
			// Provide Sender Information
			*req.remoteMAC = mac;
			*req.remotePort = ntohs(sin.sin_port) - portOffset;

			// FIXME: Do we need to update last recv timestamp? eventhough data hasn't been retrieved yet (ie. peeked)
			peerlock.lock();
			auto peer = findFriend(&mac);
			if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
			peerlock.unlock();
		}
		result = ERROR_NET_ADHOC_NOT_ENOUGH_SPACE;
	}
	// FIXME: Blocking operation with infinite timeout(0) should never get a TIMEOUT error, right? May be we should return INVALID_ARG instead if it was infinite timeout (0)?
	else
		result = ERROR_NET_ADHOC_TIMEOUT; // ERROR_NET_ADHOC_INVALID_ARG; // ERROR_NET_ADHOC_DISCONNECTED

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int DoBlockingPdpSend(AdhocSocketRequest& req, s64& result, AdhocSendTargets& targetPeers) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = ERROR_NET_ADHOC_SOCKET_DELETED;
		return 0;
	}
	auto& pdpsocket = sock->data.pdp;
	if (sock->flags & ADHOC_F_ALERTSEND) {
		result = ERROR_NET_ADHOC_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTSEND;
		return 0;
	}

	result = 0;
	bool retry = false;
	for (auto peer = targetPeers.peers.begin(); peer != targetPeers.peers.end(); ) {
		// Fill in Target Structure
		struct sockaddr_in target {};
		target.sin_family = AF_INET;
		target.sin_addr.s_addr = peer->ip;
		target.sin_port = htons(peer->port + peer->portOffset);

		int ret = sendto(pdpsocket.id, (const char*)req.buffer, targetPeers.length, MSG_NOSIGNAL, (struct sockaddr*)&target, sizeof(target));
		int sockerr = errno;

		if (ret >= 0) {
			DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u](B): Sent %u bytes to %s:%u\n", req.id, getLocalPort(pdpsocket.id), ret, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));
			// Remove successfully sent to peer to prevent sending the same data again during a retry
			peer = targetPeers.peers.erase(peer);
		}
		else {
			if (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK)) {
				u64 now = (u64)(time_now_d() * 1000000.0);
				if (req.timeout == 0 || now - req.startTime <= req.timeout) {
					retry = true;
				}
				else
					// FIXME: Does Broadcast always success? even with timeout/blocking?
					result = ERROR_NET_ADHOC_TIMEOUT;
			}
			++peer;
		}

		if (ret == SOCKET_ERROR)
			DEBUG_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u](B) [size=%i]", sockerr, req.id, getLocalPort(pdpsocket.id), ntohs(target.sin_port), targetPeers.length);
	}

	if (retry)
		return -1;

	return 0;
}

int DoBlockingPtpSend(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = ERROR_NET_ADHOC_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTSEND) {
		result = ERROR_NET_ADHOC_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTSEND;
		return 0;
	}

	// Send Data
	int ret = send(ptpsocket.id, (const char*)req.buffer, *req.length, MSG_NOSIGNAL);
	int sockerr = errno;

	// Success
	if (ret > 0) {
		// Save Length
		*req.length = ret;

		DEBUG_LOG(SCENET, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u\n", req.id, ptpsocket.lport, ret, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

		// Set to Established on successful Send when an attempt to Connect was initiated
		if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
			ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;

		// Return Success
		result = 0;
	}
	else if (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (sockerr == ENOTCONN || connectInProgress(sockerr))))) {
		u64 now = (u64)(time_now_d() * 1000000.0);
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else
			result = ERROR_NET_ADHOC_TIMEOUT;
	}
	else {
		// Change Socket State. // FIXME: Does Alerted Socket should be closed too?
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

		// Disconnected
		result = ERROR_NET_ADHOC_DISCONNECTED;
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(SCENET, "sceNetAdhocPtpSend[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int DoBlockingPtpRecv(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = ERROR_NET_ADHOC_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTRECV) {
		result = ERROR_NET_ADHOC_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTRECV;
		return 0;
	}

	int ret = recv(ptpsocket.id, (char*)req.buffer, std::max(0, *req.length), MSG_NOSIGNAL);
	int sockerr = errno;

	// Received Data. POSIX: May received 0 bytes when the remote peer already closed the connection.
	if (ret > 0) {
		DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %s:%u\n", req.id, ptpsocket.lport, ret, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);
		// Save Length
		*req.length = ret;

		// Update last recv timestamp
		peerlock.lock();
		auto peer = findFriend(&ptpsocket.paddr);
		if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
		peerlock.unlock();

		// Set to Established on successful Recv when an attempt to Connect was initiated
		if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
			ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;

		result = 0;
	}
	else if (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (sockerr == ENOTCONN || connectInProgress(sockerr))))) {
		u64 now = (u64)(time_now_d() * 1000000.0);
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else
			result = ERROR_NET_ADHOC_TIMEOUT;
	}
	else {
		// Change Socket State. // FIXME: Does Alerted Socket should be closed too?
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

		// Disconnected
		result = ERROR_NET_ADHOC_DISCONNECTED; // ERROR_NET_ADHOC_INVALID_ARG
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int DoBlockingPtpAccept(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = ERROR_NET_ADHOC_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTACCEPT) {
		result = ERROR_NET_ADHOC_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTACCEPT;
		return 0;
	}

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	socklen_t sinlen = sizeof(sin);
	int ret, sockerr;

	// Check if listening socket is ready to accept
	ret = IsSocketReady(ptpsocket.id, true, false, &sockerr);
	if (ret > 0) {
		// Accept Connection
		ret = accept(ptpsocket.id, (struct sockaddr*)&sin, &sinlen);
		sockerr = errno;
	}

	// Accepted New Connection
	if (ret > 0) {
		int newid = AcceptPtpSocket(req.id, ret, sin, req.remoteMAC, req.remotePort);
		if (newid > 0)
			result = newid;
	}
	else if (ret == 0 || (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK))) {
		u64 now = (u64)(time_now_d() * 1000000.0);
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else {
				result = ERROR_NET_ADHOC_TIMEOUT;
		}
	}
	else
		result = ERROR_NET_ADHOC_INVALID_ARG; //ERROR_NET_ADHOC_TIMEOUT

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int DoBlockingPtpConnect(AdhocSocketRequest& req, s64& result, AdhocSendTargets& targetPeer) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = ERROR_NET_ADHOC_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTCONNECT) {
		result = ERROR_NET_ADHOC_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTCONNECT;
		return 0;
	}

	int sockerr = 0, ret;
	struct sockaddr_in sin;
	// Try to connect again if the first attempt failed due to remote side was not listening yet (ie. ECONNREFUSED or ETIMEDOUT)
	if (ptpsocket.state == ADHOC_PTP_STATE_CLOSED) {
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = targetPeer.peers[0].ip;
		sin.sin_port = htons(ptpsocket.pport + targetPeer.peers[0].portOffset);

		ret = connect(ptpsocket.id, (struct sockaddr*)&sin, sizeof(sin));
		sockerr = errno;
		if (sockerr != 0)
			DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: connect(%i) error = %i", req.id, ptpsocket.lport, ptpsocket.id, sockerr);
		else
			ret = 1; // Ensure returned success value from connect to be compatible with returned success value from select (ie. positive value)
	}
	// Check the connection state (assuming "connect" has been called before and is in-progress)
	// Note: On Linux "select" can return > 0 (with SO_ERROR = 0) even when the connection is not accepted yet, thus need "getpeername" to ensure
	else {
		ret = IsSocketReady(ptpsocket.id, false, true, &sockerr);
		DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Select(%i) = %i, error = %i", req.id, ptpsocket.lport, ptpsocket.id, ret, sockerr);
		if (sockerr != 0) {
			DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: SelectError(%i) = %i", req.id, ptpsocket.lport, ptpsocket.id, sockerr);
			ret = SOCKET_ERROR; // Ensure returned value from select to be negative when the socket has error (the socket may need to be recreated again)
		}

		if (ret <= 0) {
			if (sockerr == 0)
				sockerr = EAGAIN;
			ret = SOCKET_ERROR; // Ensure returned value from select to be negative when the socket is not ready yet, due to a possibility for "getpeername" to succeed on Windows even when "connect" hasn't been accepted yet
		}
	}

	// Check whether the connection has been established or not
	if (ret != SOCKET_ERROR) {
		socklen_t sinlen = sizeof(sin);
		memset(&sin, 0, sinlen);
		// Note: "getpeername" shouldn't failed if the connection has been established, but on Windows it may succeed even when "connect" is still in-progress and not accepted yet (ie. "Tales of VS" on Windows)
		ret = getpeername(ptpsocket.id, (struct sockaddr*)&sin, &sinlen);
		if (ret == SOCKET_ERROR) {
			int err = errno;
			VERBOSE_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: getpeername(%i) error %i, sockerr = %i", req.id, ptpsocket.lport, ptpsocket.id, err, sockerr);
			sockerr = err;
		}
	}

	// Update Adhoc Socket state
	if (ret != SOCKET_ERROR || sockerr == EISCONN) {
		ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;
		INFO_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Established (%s:%u)", req.id, ptpsocket.lport, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);

		// Done
		result = 0;
	}
	else if (connectInProgress(sockerr) /* || sockerr == 0*/) {
		ptpsocket.state = ADHOC_PTP_STATE_SYN_SENT;
	}
	// On Windows you can call connect again using the same socket after ECONNREFUSED/ETIMEDOUT/ENETUNREACH error, but on non-Windows you'll need to recreate the socket first
	else {
		// Only recreate the socket once per frame (just like most adhoc games that tried to PtpConnect once per frame when using non-blocking mode)
		if (/*sockerr == ECONNREFUSED ||*/ static_cast<s64>(CoreTiming::GetGlobalTimeUsScaled() - sock->internalLastAttempt) > 16666) {
			DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Recreating Socket %i, errno = %i, state = %i, attempt = %i", req.id, ptpsocket.lport, ptpsocket.id, sockerr, ptpsocket.state, sock->attemptCount);
			if (RecreatePtpSocket(req.id) < 0) {
				WARN_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: RecreatePtpSocket error %i", req.id, ptpsocket.lport, errno);
			}
			ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
			sock->internalLastAttempt = CoreTiming::GetGlobalTimeUsScaled();
		}
	}

	// Still in progress, try again next time until Timedout
	if (ptpsocket.state != ADHOC_PTP_STATE_ESTABLISHED) {
		u64 now = (u64)(time_now_d() * 1000000.0);
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			// Try again later
			return -1;
		}
		else {
			// Handle Workaround that force the first Connect to be blocking for issue related to lobby or high latency networks
			if (sock->nonblocking)
				result = ERROR_NET_ADHOC_WOULD_BLOCK;
			else
				result = ERROR_NET_ADHOC_TIMEOUT; // FIXME: PSP never returned ERROR_NET_ADHOC_TIMEOUT on PtpConnect? or only returned ERROR_NET_ADHOC_TIMEOUT when the host is too busy? Seems to be returning ERROR_NET_ADHOC_CONNECTION_REFUSED on timedout instead (if the other side in not listening yet, which is similar to BSD).

			// Done
			return 0;
		}
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int DoBlockingPtpFlush(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = ERROR_NET_ADHOC_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTFLUSH) {
		result = ERROR_NET_ADHOC_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTFLUSH;
		return 0;
	}

	// Try Sending Empty Data
	int sockerr = FlushPtpSocket(ptpsocket.id);
	result = 0;

	if (sockerr == EAGAIN || sockerr == EWOULDBLOCK) {
		u64 now = (u64)(time_now_d() * 1000000.0);
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else
			result = ERROR_NET_ADHOC_TIMEOUT;
	}
	
	if (sockerr != 0) {
		DEBUG_LOG(SCENET, "sceNetAdhocPtpFlush[%i]: Socket Error (%i)", req.id, sockerr);
	}

	return 0;
}

int DoBlockingAdhocPollSocket(AdhocSocketRequest& req, s64& result) {
	SceNetAdhocPollSd* sds = (SceNetAdhocPollSd*)req.buffer;
	int ret = PollAdhocSocket(sds, req.id, 0, 0);
	if (ret <= 0) {
		u64 now = (u64)(time_now_d() * 1000000.0);
		// POSIX poll using negative timeout for indefinitely blocking, not sure about PSP's AdhocPollSocket tho since most of PSP's sceNet API using 0 for indefinitely blocking.
		if (static_cast<int>(req.timeout) <= 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		else if (ret < 0)
			ret = ERROR_NET_ADHOC_EXCEPTION_EVENT;
		// FIXME: Does AdhocPollSocket can return any error code other than ERROR_NET_ADHOC_EXCEPTION_EVENT?
		//else
		//	ret = ERROR_NET_ADHOC_TIMEOUT;
	}
	result = ret;

	if (ret > 0) {
		for (int i = 0; i < req.id; i++) {
			if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
				auto sock = adhocSockets[sds[i].id - 1];
				if (sock->type == SOCK_PTP)
					VERBOSE_LOG(SCENET, "Poll PTP Socket Id: %d (%d), events: %08x, revents: %08x - state: %d", sds[i].id, sock->data.ptp.id, sds[i].events, sds[i].revents, sock->data.ptp.state);
				else
					VERBOSE_LOG(SCENET, "Poll PDP Socket Id: %d (%d), events: %08x, revents: %08x", sds[i].id, sock->data.pdp.id, sds[i].events, sds[i].revents);
			}
		}
	}

	return 0;
}

static void __AdhocSocketNotify(u64 userdata, int cyclesLate) {
	SceUID threadID = userdata >> 32;
	int uid = (int)(userdata & 0xFFFFFFFF); // fd/socket id

	s64 result = -1;
	u32 error = 0;
	int delayUS = 500;

	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_NET, error);
	if (waitID == 0 || error != 0) {
		WARN_LOG(SCENET, "sceNetAdhoc Socket WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
		return;
	}

	// Socket not found?! Should never happened! But if it ever happened (ie. loaded from SaveState where adhocSocketRequests got cleared) return TIMEOUT and let the game try again.
	if (adhocSocketRequests.find(userdata) == adhocSocketRequests.end()) {
		WARN_LOG(SCENET, "sceNetAdhoc Socket WaitID(%i) on Thread(%i) not found!", uid, threadID);
		__KernelResumeThreadFromWait(threadID, ERROR_NET_ADHOC_TIMEOUT);
		return;
	}

	AdhocSocketRequest req = adhocSocketRequests[userdata];

	switch (req.type) {
	case PDP_SEND:
		if (sendTargetPeers.find(userdata) == sendTargetPeers.end()) {
			// No destination peers?
			result = 0;
			break;
		}
		if (DoBlockingPdpSend(req, result, sendTargetPeers[userdata])) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		sendTargetPeers.erase(userdata);
		break;

	case PDP_RECV:
		if (DoBlockingPdpRecv(req, result)) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		break;

	case PTP_SEND:
		if (DoBlockingPtpSend(req, result)) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		break;

	case PTP_RECV:
		if (DoBlockingPtpRecv(req, result)) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		break;

	case PTP_ACCEPT:
		if (DoBlockingPtpAccept(req, result)) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		break;

	case PTP_CONNECT:
		if (DoBlockingPtpConnect(req, result, sendTargetPeers[userdata])) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		break;

	case PTP_FLUSH:
		if (DoBlockingPtpFlush(req, result)) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		break;

	case ADHOC_POLL_SOCKET:
		if (DoBlockingAdhocPollSocket(req, result)) {
			// Try again in another 0.5ms until data available or timedout.
			CoreTiming::ScheduleEvent(usToCycles(delayUS) - cyclesLate, adhocSocketNotifyEvent, userdata);
			return;
		}
		break;
	}

	__KernelResumeThreadFromWait(threadID, result);
	DEBUG_LOG(SCENET, "Returning (ThreadId: %d, WaitID: %d, error: %08x) Result (%08x) of sceNetAdhoc[%d] - SocketID: %d", threadID, waitID, error, (int)result, req.type, req.id);

	// We are done with this socket
	adhocSocketRequests.erase(userdata);
}

// input threadSocketId = ((u64)__KernelGetCurThread()) << 32 | socketId;
int WaitBlockingAdhocSocket(u64 threadSocketId, int type, int pspSocketId, void* buffer, s32_le* len, u32 timeoutUS, SceNetEtherAddr* remoteMAC, u16_le* remotePort, const char* reason) {
	int uid = (int)(threadSocketId & 0xFFFFFFFF);
	if (adhocSocketRequests.find(threadSocketId) != adhocSocketRequests.end()) {
		WARN_LOG(SCENET, "sceNetAdhoc[%d] - ThreadID[%d] WaitID[%d] already existed, Socket[%d] is busy!", type, static_cast<int>(threadSocketId >> 32), uid, pspSocketId);
		// FIXME: Not sure if Adhoc Socket can return ADHOC_BUSY or not (assuming it's similar to EINPROGRESS for Adhoc Socket), or may be we should return TIMEOUT instead?
		return ERROR_NET_ADHOC_BUSY; // ERROR_NET_ADHOC_TIMEOUT
	}

	//changeBlockingMode(socketId, 1);

	u32 tmout = timeoutUS;
	if (tmout > 0)
		tmout = std::max(tmout, minSocketTimeoutUS);

	u64 startTime = (u64)(time_now_d() * 1000000.0);
	adhocSocketRequests[threadSocketId] = { type, pspSocketId, buffer, len, tmout, startTime, remoteMAC, remotePort };
	// Some games (ie. Hitman Reborn Battle Arena 2) are using as small as 50 usec timeout
	CoreTiming::ScheduleEvent(usToCycles(1), adhocSocketNotifyEvent, threadSocketId);
	__KernelWaitCurThread(WAITTYPE_NET, uid, 0, 0, false, reason);

	// Fallback return value
	return ERROR_NET_ADHOC_TIMEOUT;
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

void netAdhocValidateLoopMemory() {
	// Allocate Memory if it wasn't valid/allocated after loaded from old SaveState
	if (!dummyThreadHackAddr || (dummyThreadHackAddr && strcmp("dummythreadhack", kernelMemory.GetBlockTag(dummyThreadHackAddr)) != 0)) {
		u32 blockSize = sizeof(dummyThreadCode);
		dummyThreadHackAddr = kernelMemory.Alloc(blockSize, false, "dummythreadhack");
		if (dummyThreadHackAddr) Memory::Memcpy(dummyThreadHackAddr, dummyThreadCode, sizeof(dummyThreadCode));
	}
	if (!matchingThreadHackAddr || (matchingThreadHackAddr && strcmp("matchingThreadHack", kernelMemory.GetBlockTag(matchingThreadHackAddr)) != 0)) {
		u32 blockSize = sizeof(matchingThreadCode);
		matchingThreadHackAddr = kernelMemory.Alloc(blockSize, false, "matchingThreadHack");
		if (matchingThreadHackAddr) Memory::Memcpy(matchingThreadHackAddr, matchingThreadCode, sizeof(matchingThreadCode));
	}
}

void __NetAdhocDoState(PointerWrap &p) {
	auto s = p.Section("sceNetAdhoc", 1, 8);
	if (!s)
		return;

	auto cur_netAdhocInited = netAdhocInited;
	auto cur_netAdhocctlInited = netAdhocctlInited;
	auto cur_netAdhocMatchingInited = netAdhocMatchingInited;

	Do(p, netAdhocInited);
	Do(p, netAdhocctlInited);
	Do(p, netAdhocMatchingInited);
	Do(p, adhocctlHandlers);

	if (s >= 2) {
		Do(p, actionAfterMatchingMipsCall);
		if (actionAfterMatchingMipsCall != -1) {
			__KernelRestoreActionType(actionAfterMatchingMipsCall, AfterMatchingMipsCall::Create);
		}

		Do(p, dummyThreadHackAddr);
	}
	else {
		actionAfterMatchingMipsCall = -1;
		dummyThreadHackAddr = 0;
	}
	if (s >= 3) {
		Do(p, actionAfterAdhocMipsCall);
		if (actionAfterAdhocMipsCall != -1) {
			__KernelRestoreActionType(actionAfterAdhocMipsCall, AfterAdhocMipsCall::Create);
		}

		Do(p, matchingThreadHackAddr);
	}
	else {
		actionAfterAdhocMipsCall = -1;
		matchingThreadHackAddr = 0;
	}
	if (s >= 4) {
		Do(p, threadAdhocID);
		Do(p, matchingThreads);
	}
	else {
		threadAdhocID = 0;
		for (auto& it : matchingThreads) {
			it = 0;
		}
	}
	if (s >= 5) {
		Do(p, adhocConnectionType);
		Do(p, adhocctlState);
		Do(p, adhocctlNotifyEvent);
		Do(p, adhocSocketNotifyEvent);
	} else {
		adhocConnectionType = ADHOC_CONNECT;
		adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
		adhocctlNotifyEvent = -1;
		adhocSocketNotifyEvent = -1;
	}
	CoreTiming::RestoreRegisterEvent(adhocctlNotifyEvent, "__AdhocctlNotify", __AdhocctlNotify);
	CoreTiming::RestoreRegisterEvent(adhocSocketNotifyEvent, "__AdhocSocketNotify", __AdhocSocketNotify);
	if (s >= 6) {
		Do(p, gameModeNotifyEvent);
	} else {
		gameModeNotifyEvent = -1;
	}
	CoreTiming::RestoreRegisterEvent(gameModeNotifyEvent, "__GameModeNotify", __GameModeNotify);
	if (s >= 7) {
		Do(p, adhocctlStateEvent);
	} else {
		adhocctlStateEvent = -1;
	}
	CoreTiming::RestoreRegisterEvent(adhocctlStateEvent, "__AdhocctlState", __AdhocctlState);
	if (s >= 8) {
		Do(p, isAdhocctlBusy);
		Do(p, netAdhocGameModeEntered);
		Do(p, netAdhocEnterGameModeTimeout);
	}
	else {
		isAdhocctlBusy = false;
		netAdhocGameModeEntered = false;
		netAdhocEnterGameModeTimeout = 15000000;
	}
	
	if (p.mode == p.MODE_READ) {
		// Discard leftover events
		adhocctlEvents.clear();
		adhocctlRequests.clear();
		adhocSocketRequests.clear();
		sendTargetPeers.clear();
		deleteAllAdhocSockets();
		deleteMatchingEvents();
		
		// Let's not change "Inited" value when Loading SaveState to prevent memory & port leaks
		netAdhocMatchingInited = cur_netAdhocMatchingInited;
		netAdhocctlInited = cur_netAdhocctlInited;
		netAdhocInited = cur_netAdhocInited;

		isAdhocctlNeedLogin = false;
	}
}

void __UpdateAdhocctlHandlers(u32 flag, u32 error) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	adhocctlEvents.push_back({ flag, error });
}

void __UpdateMatchingHandler(const MatchingArgs &ArgsPtr) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	matchingEvents.push_back(ArgsPtr);
}

u32_le __CreateHLELoop(u32_le *loopAddr, const char *sceFuncName, const char *hleFuncName, const char *tagName) {
	if (loopAddr == NULL || sceFuncName == NULL || hleFuncName == NULL)
		return 0;

	loopAddr[0] = MIPS_MAKE_SYSCALL(sceFuncName, hleFuncName);
	loopAddr[1] = MIPS_MAKE_B(-2);
	loopAddr[2] = MIPS_MAKE_NOP();
	u32 blockSize = sizeof(u32_le)*3;
	u32_le dummyThreadHackAddr = kernelMemory.Alloc(blockSize, false, tagName); // blockSize will be rounded to 256 granularity
	Memory::Memcpy(dummyThreadHackAddr, loopAddr, sizeof(u32_le) * 3); // This area will be cleared again after loading an old savestate :(
	return dummyThreadHackAddr;
}

void __AdhocNotifInit() {
	adhocctlNotifyEvent = CoreTiming::RegisterEvent("__AdhocctlNotify", __AdhocctlNotify);
	adhocSocketNotifyEvent = CoreTiming::RegisterEvent("__AdhocSocketNotify", __AdhocSocketNotify);
	gameModeNotifyEvent = CoreTiming::RegisterEvent("__GameModeNotify", __GameModeNotify);
	adhocctlStateEvent = CoreTiming::RegisterEvent("__AdhocctlState", __AdhocctlState);

	adhocctlRequests.clear();
	adhocSocketRequests.clear();
	sendTargetPeers.clear();
}

void __NetAdhocInit() {
	friendFinderRunning = false;
	netAdhocInited = false;
	netAdhocctlInited = false;
	netAdhocMatchingInited = false;
	adhocctlHandlers.clear();
	__AdhocNotifInit();
	__AdhocServerInit();

	// Create built-in AdhocServer Thread
	adhocServerRunning = false;
	if (g_Config.bEnableWlan && g_Config.bEnableAdhocServer) {
		adhocServerThread = std::thread(proAdhocServerThread, SERVER_PORT);
	}
}

u32 sceNetAdhocInit() {
	if (!netAdhocInited) {
		// Library initialized
		netAdhocInited = true;
		isAdhocctlBusy = false;

		// FIXME: It seems official prx is using sceNetAdhocGameModeDeleteMaster in here?
		NetAdhocGameMode_DeleteMaster();
		// Since we are deleting GameMode Master here, we should probably need to make sure GameMode resources all cleared too.
		deleteAllGMB();

		// Return Success
		return hleLogSuccessInfoI(SCENET, 0, "at %08x", currentMIPS->pc);
	}
	// Already initialized
	return hleLogWarning(SCENET, ERROR_NET_ADHOC_ALREADY_INITIALIZED, "already initialized");
}

static u32 sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlInit(%i, %i, %08x) at %08x", stackSize, prio, productAddr, currentMIPS->pc);
	
	// FIXME: Returning 0x8002013a (SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED) without adhoc module loaded first?
	// FIXME: Sometimes returning 0x80410601 (ERROR_NET_ADHOC_AUTH_ALREADY_INITIALIZED / Library module is already initialized ?) when AdhocctlTerm is not fully done?

	if (netAdhocctlInited)
		return ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED;

	auto product = PSPPointer<SceNetAdhocctlAdhocId>::Create(productAddr);
	if (product.IsValid()) {
		product_code = *product;
		product.NotifyRead("NetAdhocctlInit");
	}

	adhocctlEvents.clear();
	netAdhocctlInited = true; //needed for cleanup during AdhocctlTerm even when it failed to connect to Adhoc Server (since it's being faked as success)
	isAdhocctlNeedLogin = true;

	// Create fake PSP Thread for callback
	// TODO: Should use a separated threads for friendFinder, matchingEvent, and matchingInput and created on AdhocctlInit & AdhocMatchingStart instead of here
	netAdhocValidateLoopMemory();
	threadAdhocID = __KernelCreateThread("AdhocThread", __KernelGetCurThreadModuleId(), dummyThreadHackAddr, prio, stackSize, PSP_THREAD_ATTR_USER, 0, true);
	if (threadAdhocID > 0) {
		__KernelStartThread(threadAdhocID, 0, 0);
	}

	// TODO: Merging friendFinder (real) thread to AdhocThread (fake) thread on PSP side
	if (!friendFinderRunning) {
		friendFinderThread = std::thread(friendFinder);
	}
	
	// Need to make sure to be connected to Adhoc Server (indicated by networkInited) before returning to prevent GTA VCS failed to create/join a group and unable to see any game room
	int us = adhocDefaultDelay;
	if (g_Config.bEnableWlan && !networkInited) {
		AdhocctlRequest dummyreq = { OPCODE_LOGIN, {0} };
		return WaitBlockingAdhocctlSocket(dummyreq, us, "adhocctl init");
	}
	// Give a little time for friendFinder thread to be ready before the game use the next sceNet functions, should've checked for friendFinderRunning status instead of guessing the time?
	hleEatMicro(us);

	return 0;
}

int NetAdhocctl_GetState() {
	return adhocctlState;
}

int sceNetAdhocctlGetState(u32 ptrToStatus) {
	// Library uninitialized
	if (!netAdhocctlInited)
		return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;

	// Invalid Arguments
	if (!Memory::IsValidAddress(ptrToStatus))
		return ERROR_NET_ADHOCCTL_INVALID_ARG;

	int state = NetAdhocctl_GetState();
	// Output Adhocctl State
	Memory::Write_U32(state, ptrToStatus);

	// Return Success
	return hleLogSuccessVerboseI(SCENET, 0, "state = %d", state);
}

/**
 * Adhoc Emulator PDP Socket Creator
 * @param saddr Local MAC (Unused)
 * @param sport Local Binding Port
 * @param bufsize Socket Buffer Size
 * @param flag Bitflags (Unused)
 * @return Socket ID > 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_SOCKET_ID_NOT_AVAIL, ADHOC_INVALID_ADDR, ADHOC_PORT_NOT_AVAIL, ADHOC_INVALID_PORT, ADHOC_PORT_IN_USE, NET_NO_SPACE
 */
// When choosing AdHoc menu in Wipeout Pulse sometimes it's saying that "WLAN is turned off" on game screen and getting "kUnityCommandCode_MediaDisconnected" error in the Log Console when calling sceNetAdhocPdpCreate, probably it needed to wait something from the thread before calling this (ie. need to receives 7 bytes from adhoc server 1st?)
static int sceNetAdhocPdpCreate(const char *mac, int port, int bufferSize, u32 flag) {
	INFO_LOG(SCENET, "sceNetAdhocPdpCreate(%s, %u, %u, %u) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), port, bufferSize, flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	if (!netInited)
		return 0x800201CA; //PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX;

	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)mac;
	bool isClient = false;
	if (netAdhocInited) {
		// Valid Arguments are supplied
		if (mac != NULL && bufferSize > 0) {
			// Port is in use by another PDP Socket. 
			if (isPDPPortInUse(port)) {
				// FIXME: When PORT_IN_USE error occured it seems the index to the socket id also increased, which means it tries to create & bind the socket first and then closes it due to failed to bind
				return hleLogDebug(SCENET, ERROR_NET_ADHOC_PORT_IN_USE, "port in use");
			}

			//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
			if (port == 0) {
				isClient = true;
				port = -static_cast<int>(portOffset);
			}
			// Some games (ie. DBZ Shin Budokai 2) might be getting the saddr/srcmac content from SaveState and causing problems :( So we try to fix it here
			if (saddr != NULL) {
				getLocalMac(saddr);
			}
			// Valid MAC supplied. FIXME: MAC only valid after successful attempt to Create/Connect/Join a Group? (ie. adhocctlCurrentMode != ADHOCCTL_MODE_NONE)
			if ((adhocctlCurrentMode != ADHOCCTL_MODE_NONE) && isLocalMAC(saddr)) {
				// Create Internet UDP Socket
				int usocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				// Valid Socket produced
				if (usocket != INVALID_SOCKET) {
					// Change socket buffer size to be consistent on all platforms.
					// Send Buffer should be smaller than Recv Buffer to prevent faster device from flooding slower device too much.
					setSockBufferSize(usocket, SO_SNDBUF, bufferSize*5); //PSP_ADHOC_PDP_MFS
					// Recv Buffer should be equal or larger than Send Buffer. Using larger Recv Buffer might helped reduces dropped packets during a slowdown, but too large may cause slow performance on Warriors Orochi 2.
					setSockBufferSize(usocket, SO_RCVBUF, bufferSize*10); //PSP_ADHOC_PDP_MFS*10

					// Ignore SIGPIPE when supported (ie. BSD/MacOS)
					setSockNoSIGPIPE(usocket, 1);

					// Enable Port Re-use, this will allow binding to an already used port, but only one of them can read the data (shared receive buffer?)
					setSockReuseAddrPort(usocket);

					// Disable Connection Reset error on UDP to avoid strange behavior https://stackoverflow.com/questions/34242622/windows-udp-sockets-recvfrom-fails-with-error-10054
					setUDPConnReset(usocket, false);

					// Binding Information for local Port
					struct sockaddr_in addr {};
					addr.sin_family = AF_INET;
					addr.sin_addr.s_addr = INADDR_ANY;
					if (isLocalServer) {
						getLocalIp(&addr);
					}
					uint16_t requestedport = static_cast<int>(port + static_cast<int>(portOffset));
					// Avoid getting random port due to port offset when original port wasn't 0 (ie. original_port + port_offset = 65536 = 0)
					if (requestedport == 0 && port > 0)
						requestedport = 65535; // Hopefully it will be safe to default it to 65535 since there can't be more than one port that can bumped into 65536
					// Show a warning about privileged ports
					if (requestedport != 0 && requestedport < 1024) {
						WARN_LOG(SCENET, "sceNetAdhocPdpCreate - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
					}
					addr.sin_port = htons(requestedport);
					
					// Bound Socket to local Port
					int iResult = bind(usocket, (struct sockaddr*)&addr, sizeof(addr));

					if (iResult == 0) {
						// Workaround: Send a dummy 0 size message to AdhocServer IP to make sure the socket actually bound to an address when binded with INADDR_ANY before using getsockname, seems to fix sending from incorrect port issue on MGS:PW on Android
						addr.sin_addr.s_addr = g_adhocServerIP.in.sin_addr.s_addr;
						addr.sin_port = 0;
						sendto(usocket, dummyPeekBuf64k, 0, MSG_NOSIGNAL, (struct sockaddr*)&addr, sizeof(addr));
						// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
						socklen_t len = sizeof(addr);
						if (getsockname(usocket, (struct sockaddr*)&addr, &len) == 0) {
							uint16_t boundport = ntohs(addr.sin_port);
							if (port + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
								WARN_LOG(SCENET, "sceNetAdhocPdpCreate - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", port, requestedport, boundport, boundport - portOffset);
							port = boundport - portOffset;
						}

						// Allocate Memory for Internal Data
						AdhocSocket * internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));

						// Allocated Memory
						if (internal != NULL) {
							// Find Free Translator Index
							// FIXME: We should probably use an increasing index instead of looking for an empty slot from beginning if we want to simulate a real socket id
							int i = 0; 
							for (; i < MAX_SOCKET; i++) if (adhocSockets[i] == NULL) break;

							// Found Free Translator Index
							if (i < MAX_SOCKET) {
								// Clear Memory
								memset(internal, 0, sizeof(AdhocSocket));

								// Socket Type
								internal->type = SOCK_PDP;
								internal->nonblocking = flag;
								internal->buffer_size = bufferSize;
								internal->isClient = isClient;

								// Fill in Data
								internal->data.pdp.id = usocket;
								internal->data.pdp.laddr = *saddr;
								internal->data.pdp.lport = port; //getLocalPort(usocket) - portOffset;

								// Link Socket to Translator ID
								adhocSockets[i] = internal;

								// Forward Port on Router
								//sceNetPortOpen("UDP", port);
								UPnP_Add(IP_PROTOCOL_UDP, isOriPort ? port : port + portOffset, port + portOffset); // g_PortManager.Add(IP_PROTOCOL_UDP, isOriPort ? port : port + portOffset, port + portOffset);
								
								// Switch to non-blocking for futher usage
								changeBlockingMode(usocket, 1);

								// Success
								INFO_LOG(SCENET, "sceNetAdhocPdpCreate - PSP Socket id: %i, Host Socket id: %i", i + 1, usocket);
								return i + 1;
							} 

							// Free Memory for Internal Data
							free(internal);
						}
					}

					// Close Socket
					closesocket(usocket);

					// Port not available (exclusively in use?)
					if (iResult == SOCKET_ERROR) {
						ERROR_LOG(SCENET, "Socket error (%i) when binding port %u", errno, ntohs(addr.sin_port));
						auto n = GetI18NCategory(I18NCat::NETWORKING);
						g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Port")) + " " + std::to_string(port + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")));
						
						return hleLogDebug(SCENET, ERROR_NET_ADHOC_PORT_NOT_AVAIL, "port not available");
					}
				}

				// Default to No-Space Error
				return hleLogDebug(SCENET, ERROR_NET_NO_SPACE, "net no space");
			}

			// Invalid MAC supplied
			return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ADDR, "invalid address");
		}

		// Invalid Arguments were supplied
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
	}
	// Library is uninitialized
	return hleLogDebug(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "adhoc not initialized");
}

/**
 * Get Adhoc Parameter
 * @param parameter OUT: Adhoc Parameter
 * @return 0 on success or... ADHOCCTL_NOT_INITIALIZED, ADHOCCTL_INVALID_ARG
 */
static int sceNetAdhocctlGetParameter(u32 paramAddr) {
	char grpName[9] = { 0 };
	memcpy(grpName, parameter.group_name.data, ADHOCCTL_GROUPNAME_LEN);
	parameter.nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0;
	DEBUG_LOG(SCENET, "sceNetAdhocctlGetParameter(%08x) [Ch=%i][Group=%s][BSSID=%s][name=%s]", paramAddr, parameter.channel, grpName, mac2str(&parameter.bssid.mac_addr).c_str(), parameter.nickname.data);
	if (!g_Config.bEnableWlan) {
		return ERROR_NET_ADHOCCTL_DISCONNECTED;
	}

	// Library initialized
	if (!netAdhocctlInited) {
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED);
	}

	auto ptr = PSPPointer<SceNetAdhocctlParameter>::Create(paramAddr);
	if (!ptr.IsValid())
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_INVALID_ARG);

	*ptr = parameter;
	ptr.NotifyWrite("NetAdhocctlGetParameter");
	return 0;
}

/**
 * Adhoc Emulator PDP Send Call
 * @param id Socket File Descriptor
 * @param daddr Target MAC Address
 * @param dport Target Port
 * @param data Data Payload
 * @param len Payload Length
 * @param timeout Send Timeout (microseconds)
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_INVALID_ADDR, ADHOC_INVALID_PORT, ADHOC_INVALID_DATALEN, ADHOC_SOCKET_ALERTED, ADHOC_TIMEOUT, ADHOC_THREAD_ABORTED, ADHOC_WOULD_BLOCK, NET_NO_SPACE, NET_INTERNAL
 */
static int sceNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag) {
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(SCENET, "sceNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i) at %08x", id, mac2str((SceNetEtherAddr*)mac).c_str(), port, data, len, timeout, flag, currentMIPS->pc);
	} else {
		VERBOSE_LOG(SCENET, "sceNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i) at %08x", id, mac2str((SceNetEtherAddr*)mac).c_str(), port, data, len, timeout, flag, currentMIPS->pc);
	}
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	SceNetEtherAddr * daddr = (SceNetEtherAddr *)mac;
	uint16_t dport = (uint16_t)port;
	
	//if (dport < 7) dport += 1341;

	// Really should flatten this with early outs, all this indentation is making me dizzy.

	// Library is initialized
	if (netAdhocInited) {
		// Valid Port
		if (dport != 0) {
			// Valid Data Length
			if (len >= 0) { // should we allow 0 size packet (for ping) ?
				// Valid Socket ID
				if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
					// Cast Socket
					auto socket = adhocSockets[id - 1];
					auto& pdpsocket = socket->data.pdp;
					socket->nonblocking = flag;

					// Valid Data Buffer
					if (data != NULL) {
						// Valid Destination Address
						if (daddr != NULL && !isZeroMAC(daddr)) {
							// Log Destination
							// Schedule Timeout Removal
							//if (flag) timeout = 0;

							// Apply Send Timeout Settings to Socket
							if (timeout > 0) 
								setSockTimeout(pdpsocket.id, SO_SNDTIMEO, timeout);

							if (socket->flags & ADHOC_F_ALERTSEND) {
								socket->alerted_flags |= ADHOC_F_ALERTSEND;

								return hleLogError(SCENET, ERROR_NET_ADHOC_SOCKET_ALERTED, "socket alerted");
							}

							// Single Target
							if (!isBroadcastMAC(daddr)) {
								// Fill in Target Structure
								struct sockaddr_in target {};
								target.sin_family = AF_INET;
								target.sin_port = htons(dport + portOffset);
								u16 finalPortOffset;

								// Get Peer IP. Some games (ie. Vulcanus Seek and Destroy) seems to try to send to zero-MAC (ie. 00:00:00:00:00:00) first before sending to the actual destination MAC.. So may be sending to zero-MAC has a special meaning? (ie. to peek send buffer availability may be?)
								if (resolveMAC((SceNetEtherAddr *)daddr, (uint32_t *)&target.sin_addr.s_addr, &finalPortOffset)) {
									// Some games (ie. PSP2) might try to talk to it's self, not sure if they talked through WAN or LAN when using public Adhoc Server tho
									target.sin_port = htons(dport + finalPortOffset);

									// Acquire Network Lock
									//_acquireNetworkLock();

									// Send Data. UDP are guaranteed to be sent as a whole or nothing(failed if len > SO_MAX_MSG_SIZE), and never be partially sent/recv
									int sent = sendto(pdpsocket.id, (const char *)data, len, MSG_NOSIGNAL, (struct sockaddr*)&target, sizeof(target));
									int error = errno;

									if (sent == SOCKET_ERROR) {
										// Simulate blocking behaviour with non-blocking socket
										if (!flag && (error == EAGAIN || error == EWOULDBLOCK)) {
											u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
											if (sendTargetPeers.find(threadSocketId) != sendTargetPeers.end()) {
												DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Socket(%d) is Busy!", id, getLocalPort(pdpsocket.id), pdpsocket.id);
												return hleLogError(SCENET, ERROR_NET_ADHOC_BUSY, "busy?");
											}

											AdhocSendTargets dest = { len, {}, false };
											dest.peers.push_back({ target.sin_addr.s_addr, dport, finalPortOffset });
											sendTargetPeers[threadSocketId] = dest;
											return WaitBlockingAdhocSocket(threadSocketId, PDP_SEND, id, data, nullptr, timeout, nullptr, nullptr, "pdp send");
										}

										DEBUG_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u] (size=%i)", error, id, getLocalPort(pdpsocket.id), ntohs(target.sin_port), len);
									}
									//changeBlockingMode(socket->id, 0);

									// Free Network Lock
									//_freeNetworkLock();

									hleEatMicro(50); // Can be longer than 1ms tho
									// Sent Data
									if (sent >= 0) {
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Sent %u bytes to %s:%u\n", id, getLocalPort(pdpsocket.id), sent, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));

										// Success
										return 0; // sent; // MotorStorm will try to resend if return value is not 0
									}

									// Non-Blocking
									if (flag) 
										return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block");

									// Does PDP can Timeout? There is no concept of Timeout when sending UDP due to no ACK, but might happen if the socket buffer is full, not sure about PDP since some games did use the timeout arg
									return hleLogDebug(SCENET, ERROR_NET_ADHOC_TIMEOUT, "timeout?"); // ERROR_NET_ADHOC_INVALID_ADDR;
								}
								VERBOSE_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Unknown Target Peer %s:%u (faking success)\n", id, getLocalPort(pdpsocket.id), mac2str(daddr).c_str(), ntohs(target.sin_port));
								return 0; // faking success
							}

							// Broadcast Target
							else {
								// Acquire Network Lock
								//_acquireNetworkLock();

#ifdef BROADCAST_TO_LOCALHOST
								//// Get Local IP Address
								//union SceNetApctlInfo info; if (sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) == 0) {
								//	// Fill in Target Structure
								//	SceNetInetSockaddrIn target;
								//	target.sin_family = AF_INET;
								//	sceNetInetInetAton(info.ip, &target.sin_addr);
								//	target.sin_port = sceNetHtons(dport);
								//	
								//	// Send Data
								//	sceNetInetSendto(socket->id, data, len, ((flag != 0) ? (INET_MSG_DONTWAIT) : (0)), (SceNetInetSockaddr *)&target, sizeof(target));
								//}
#endif

								// Acquire Peer Lock
								peerlock.lock();
								AdhocSendTargets dest = { len, {}, true };
								// Iterate Peers
								SceNetAdhocctlPeerInfo* peer = friends;
								for (; peer != NULL; peer = peer->next) {
									// Does Skipping sending to timed out friends could cause desync when players moving group at the time MP game started?
									if (peer->last_recv == 0)
										continue;

									dest.peers.push_back({ peer->ip_addr, dport, peer->port_offset });
								}
								// Free Peer Lock
								peerlock.unlock();

								// Send Data
								// Simulate blocking behaviour with non-blocking socket
								if (!flag) {
									u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
									if (sendTargetPeers.find(threadSocketId) != sendTargetPeers.end()) {
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u](BC): Socket(%d) is Busy!", id, getLocalPort(pdpsocket.id), pdpsocket.id);
										return hleLogError(SCENET, ERROR_NET_ADHOC_BUSY, "busy?");
									}

									sendTargetPeers[threadSocketId] = dest;
									return WaitBlockingAdhocSocket(threadSocketId, PDP_SEND, id, data, nullptr, timeout, nullptr, nullptr, "pdp send broadcast");
								}
								// Non-blocking
								else {
									// Iterate Peers
									for (auto& peer : dest.peers) {
										// Fill in Target Structure
										struct sockaddr_in target {};
										target.sin_family = AF_INET;
										target.sin_addr.s_addr = peer.ip;
										target.sin_port = htons(dport + peer.portOffset);

										int sent = sendto(pdpsocket.id, (const char*)data, len, MSG_NOSIGNAL, (struct sockaddr*)&target, sizeof(target));
										int error = errno;
										if (sent == SOCKET_ERROR) {
											DEBUG_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u](BC) [size=%i]", error, id, getLocalPort(pdpsocket.id), ntohs(target.sin_port), len);
										}

										if (sent >= 0) {
											DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u](BC): Sent %u bytes to %s:%u\n", id, getLocalPort(pdpsocket.id), sent, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));
										}
									}
								}

								//changeBlockingMode(socket->id, 0);

								// Free Network Lock
								//_freeNetworkLock();

								hleEatMicro(50);
								// Success, Broadcast never fails!
								return 0; // len;
							}
						}

						// Invalid Destination Address
						return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_ADDR, "invalid address");
					}

					// Invalid Argument
					return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
				}

				// Invalid Socket ID
				return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
			}

			// Invalid Data Length
			return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_DATALEN, "invalid data length");
		}

		// Invalid Destination Port
		return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_PORT, "invalid port");
	}

	// Library is uninitialized
	return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized");
}

/**
 * Adhoc Emulator PDP Receive Call
 * @param id Socket File Descriptor
 * @param saddr OUT: Source MAC Address
 * @param sport OUT: Source Port
 * @param buf OUT: Received Data. The caller has to provide enough space (the whole socket buffer size?) to fully read the available packet.
 * @param len IN: Buffer Size OUT: Received Data Length
 * @param timeout Receive Timeout
 * @param flag Nonblocking Flag
 * @return 0 (or Number of bytes received?) on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_NOT_ENOUGH_SPACE, ADHOC_THREAD_ABORTED, NET_INTERNAL
 */
static int sceNetAdhocPdpRecv(int id, void *addr, void * port, void *buf, void *dataLength, u32 timeout, int flag) {
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, currentMIPS->pc);
	} else {
		VERBOSE_LOG(SCENET, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, currentMIPS->pc);
	}
 
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	SceNetEtherAddr *saddr = (SceNetEtherAddr *)addr;
	uint16_t * sport = (uint16_t *)port; //Looking at Quake3 sourcecode (net_adhoc.c) this is an "int" (32bit) but changing here to 32bit will cause FF-Type0 to see duplicated Host (thinking it was from a different host)
	int * len = (int *)dataLength;
	if (netAdhocInited) {
		// Valid Socket ID
		if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
			// Cast Socket
			auto socket = adhocSockets[id - 1];
			auto& pdpsocket = socket->data.pdp;
			socket->nonblocking = flag;

			// Valid Arguments
			if (saddr != NULL && port != NULL && buf != NULL && len != NULL) { 
#ifndef PDP_DIRTY_MAGIC
				// Schedule Timeout Removal
				//if (flag == 1) timeout = 0;
#else
				// Nonblocking Simulator
				int wouldblock = 0;

				// Minimum Timeout
				uint32_t mintimeout = minSocketTimeoutUS; // 250000;

				// Nonblocking Call
				if (flag == 1) {
					// Erase Nonblocking Flag
					flag = 0;

					// Set Wouldblock Behaviour
					wouldblock = 1;

					// Set Minimum Timeout (250ms)
					if (timeout < mintimeout) timeout = mintimeout;
				}
#endif

				// Apply Receive Timeout Settings to Socket. Let's not wait forever (0 = indefinitely)
				if (timeout > 0) 
					setSockTimeout(pdpsocket.id, SO_RCVTIMEO, timeout);

				if (socket->flags & ADHOC_F_ALERTRECV) {
					socket->alerted_flags |= ADHOC_F_ALERTRECV;

					return hleLogError(SCENET, ERROR_NET_ADHOC_SOCKET_ALERTED, "socket alerted");
				}

				// Sender Address
				struct sockaddr_in sin;
				socklen_t sinlen;

				// Acquire Network Lock
				//_acquireNetworkLock();

				SceNetEtherAddr mac;
				int received = 0;
				int error;
				
				int disCnt = 16;
				while (--disCnt > 0)
				{
					// Receive Data. PDP always sent in full size or nothing(failed), recvfrom will always receive in full size as requested (blocking) or failed (non-blocking). If available UDP data is larger than buffer, excess data is lost.
					// Should peek first for the available data size if it's more than len return ERROR_NET_ADHOC_NOT_ENOUGH_SPACE along with required size in len to prevent losing excess data
					// On Windows: MSG_TRUNC are not supported on recvfrom (socket error WSAEOPNOTSUPP), so we use dummy buffer as an alternative
					sinlen = sizeof(sin);
					memset(&sin, 0, sinlen);
					received = recvfrom(pdpsocket.id, dummyPeekBuf64k, dummyPeekBuf64kSize, MSG_PEEK | MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
					error = errno;
					// Discard packets from IP that can't be translated into MAC address to prevent confusing the game, since the sender MAC won't be updated and may contains invalid/undefined value.
					// TODO: In order to discard packets from unresolvable IP (can't be translated into player's MAC) properly, we'll need to manage the socket buffer ourself, 
					//       by reading the whole available data, separates each datagram and discard unresolvable one, so we can calculate the correct number of available data to recv on GetPdpStat too.
					//       We may also need to implement encryption (or a simple checksum will do) in order to validate the packet to findout whether it came from PPSSPP or a different App that may be sending/broadcasting data to the same port being used by a game 
					//       (in case the IP was resolvable but came from a different App, which will need to be discarded too)
					// Note: Looping to check too many packets (ie. contiguous) to discard per one non-blocking PdpRecv syscall may cause a slow down
					if (received != SOCKET_ERROR && !resolveIP(sin.sin_addr.s_addr, &mac)) {
						// Remove the packet from socket buffer
						sinlen = sizeof(sin);
						memset(&sin, 0, sinlen);
						recvfrom(pdpsocket.id, dummyPeekBuf64k, dummyPeekBuf64kSize, MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
						if (flag) {
							VERBOSE_LOG(SCENET, "%08x=sceNetAdhocPdpRecv: would block (disc)", ERROR_NET_ADHOC_WOULD_BLOCK); // Temporary fix to avoid a crash on the Logs due to trying to Logs syscall's argument from another thread (ie. AdhocMatchingInput thread)
							return ERROR_NET_ADHOC_WOULD_BLOCK; // hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block (disc)");
						}
						else {
							// Simulate blocking behaviour with non-blocking socket, and discard more unresolvable packets until timeout reached
							u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
							return WaitBlockingAdhocSocket(threadSocketId, PDP_RECV, id, buf, len, timeout, saddr, sport, "pdp recv (disc)");
						}
					}
					else
						break;
				}

				// At this point we assumed that the packet is a valid PPSSPP packet
				if (received != SOCKET_ERROR && *len < received) {
					INFO_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Peeked %u/%u bytes from %s:%u\n", id, getLocalPort(pdpsocket.id), received, *len, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));

					if (received > 0 && *len > 0)
						memcpy(buf, dummyPeekBuf64k, std::min(received, *len));

					// Return the actual available data size
					*len = received;

					// Provide Sender Information
					*saddr = mac;
					*sport = ntohs(sin.sin_port) - portOffset;

					// Update last recv timestamp, may cause disconnection not detected properly tho
					peerlock.lock();
					auto peer = findFriend(&mac);
					if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
					peerlock.unlock();

					return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_NOT_ENOUGH_SPACE, "not enough space");
				}

				sinlen = sizeof(sin);
				memset(&sin, 0, sinlen);
				// On Windows: Socket Error 10014 may happen when buffer size is less than the minimum allowed/required (ie. negative number on Vulcanus Seek and Destroy), the address is not a valid part of the user address space (ie. on the stack or when buffer overflow occurred), or the address is not properly aligned (ie. multiple of 4 on 32bit and multiple of 8 on 64bit) https://stackoverflow.com/questions/861154/winsock-error-code-10014
				received = recvfrom(pdpsocket.id, (char*)buf, std::max(0, *len), MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
				error = errno;

				// On Windows: recvfrom on UDP can get error WSAECONNRESET when previous sendto's destination is unreachable (or destination port is not bound), may need to disable SIO_UDP_CONNRESET
				if (received == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || error == ECONNRESET)) {
					if (flag == 0) {
						// Simulate blocking behaviour with non-blocking socket
						u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
						return WaitBlockingAdhocSocket(threadSocketId, PDP_RECV, id, buf, len, timeout, saddr, sport, "pdp recv");
					}

					VERBOSE_LOG(SCENET, "%08x=sceNetAdhocPdpRecv: would block", ERROR_NET_ADHOC_WOULD_BLOCK); // Temporary fix to avoid a crash on the Logs due to trying to Logs syscall's argument from another thread (ie. AdhocMatchingInput thread)
					return ERROR_NET_ADHOC_WOULD_BLOCK; // hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block");
				}
				
				hleEatMicro(50);
				// Received Data. UDP can also receives 0 data, while on TCP 0 data = connection gracefully closed, but not sure about PDP tho
				if (received >= 0) {
					DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %u bytes from %s:%u\n", id, getLocalPort(pdpsocket.id), received, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));

					// Find Peer MAC
					if (resolveIP(sin.sin_addr.s_addr, &mac)) {
						// Provide Sender Information
						*saddr = mac;
						*sport = ntohs(sin.sin_port) - portOffset;

						// Save Length
						*len = received; // Kurok homebrew seems to use the new value of len than returned value as data length

						// Update last recv timestamp, may cause disconnection not detected properly tho
						peerlock.lock();
						auto peer = findFriend(&mac);
						if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
						peerlock.unlock();

						// Free Network Lock
						//_freeNetworkLock();

						// Return Success. According to pspsdk-1.0+beta2 returned value is Number of bytes received, but JPCSP returning 0?
						return 0; //received; // Returning number of bytes received will cause KH BBS unable to see the game event/room
					}

					// Free Network Lock
					//_freeNetworkLock();

					//free(tmpbuf);

					// Receiving data from unknown peer? Should never reached here! Unless the Peeked's packet was different than the Recved one (which mean there is a problem)
					WARN_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %i bytes from Unknown Peer %s:%u", id, getLocalPort(pdpsocket.id), received, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));
					if (flag) {
						VERBOSE_LOG(SCENET, "%08x=sceNetAdhocPdpRecv: would block (problem)", ERROR_NET_ADHOC_WOULD_BLOCK); // Temporary fix to avoid a crash on the Logs due to trying to Logs syscall's argument from another thread (ie. AdhocMatchingInput thread)
						return ERROR_NET_ADHOC_WOULD_BLOCK; // hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block (problem)");
					}
				}

				// Free Network Lock
				//_freeNetworkLock();

#ifdef PDP_DIRTY_MAGIC
				// Restore Nonblocking Flag for Return Value
				if (wouldblock) flag = 1;
#endif

				DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Result:%i (Error:%i)", id, pdpsocket.lport, received, error);

				// Unexpected error (other than EAGAIN/EWOULDBLOCK/ECONNRESET) or in case the Peeked's packet was different than Recved one, treated as Timeout?
				return hleLogError(SCENET, ERROR_NET_ADHOC_TIMEOUT, "timeout?");
			}

			// Invalid Argument
			return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
		}

		// Invalid Socket ID
		return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
	}

	// Library is uninitialized
	return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized");
}

int NetAdhoc_SetSocketAlert(int id, s32_le flag) {
	if (id < 1 || id > MAX_SOCKET || adhocSockets[id - 1] == NULL)
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;

	// FIXME: Should we check for valid Alert Flags and/or Mask them? Should we return an error if we found an invalid flag?
	s32_le flg = flag & ADHOC_F_ALERTALL;

	adhocSockets[id - 1]->flags = flg;
	adhocSockets[id - 1]->alerted_flags = 0;

	return 0;
}

// Flags seems to be bitmasks of ADHOC_F_ALERT... (need more games to test this)
int sceNetAdhocSetSocketAlert(int id, int flag) {
 	WARN_LOG_REPORT_ONCE(sceNetAdhocSetSocketAlert, SCENET, "UNTESTED sceNetAdhocSetSocketAlert(%d, %08x) at %08x", id, flag, currentMIPS->pc);

	int retval = NetAdhoc_SetSocketAlert(id, flag);
	hleDelayResult(retval, "set socket alert delay", 1000);
	return hleLogDebug(SCENET, retval, "");
}

int PollAdhocSocket(SceNetAdhocPollSd* sds, int count, int timeout, int nonblock) {
	//WSAPoll only available for Vista or newer, so we'll use an alternative way for XP since Windows doesn't have poll function like *NIX
	fd_set readfds, writefds, exceptfds;
	int fd;
	int maxfd = 0;
	FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);
	
	for (int i = 0; i < count; i++) {
		sds[i].revents = 0;
		// Fill in Socket ID
		if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
			auto sock = adhocSockets[sds[i].id - 1];
			if (!sock) {
				return ERROR_NET_ADHOC_SOCKET_DELETED;
			}
			if (sock->type == SOCK_PTP) {
				fd = sock->data.ptp.id;
			}
			else {
				fd = sock->data.pdp.id;
			}
			if (fd > maxfd) maxfd = fd;
			FD_SET(fd, &readfds); 
			FD_SET(fd, &writefds);
			FD_SET(fd, &exceptfds);
		}
	}
	timeval tmout;
	tmout.tv_sec = timeout / 1000000; // seconds
	tmout.tv_usec = (timeout % 1000000); // microseconds
	int affectedsockets = select(maxfd + 1, &readfds, &writefds, &exceptfds, &tmout);
	if (affectedsockets >= 0) {
		affectedsockets = 0;
		for (int i = 0; i < count; i++) {
			if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
				auto sock = adhocSockets[sds[i].id - 1];
				if (sock->type == SOCK_PTP) {
					fd = sock->data.ptp.id;					
				}
				else {
					fd = sock->data.pdp.id;
				}
				if ((sds[i].events & ADHOC_EV_RECV) && FD_ISSET(fd, &readfds))
					sds[i].revents |= ADHOC_EV_RECV;
				if ((sds[i].events & ADHOC_EV_SEND) && FD_ISSET(fd, &writefds))
					sds[i].revents |= ADHOC_EV_SEND;				
				if (sock->alerted_flags)
					sds[i].revents |= ADHOC_EV_ALERT;
				// Mask certain revents bits with events bits
				sds[i].revents &= sds[i].events;
				
				if (sock->type == SOCK_PTP) {
					// FIXME: Should we also make use "retry_interval" for ADHOC_EV_ACCEPT, similar to ADHOC_EV_CONNECT ?
					if (sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN && (sds[i].events & ADHOC_EV_ACCEPT) && FD_ISSET(fd, &readfds)) {
						sds[i].revents |= ADHOC_EV_ACCEPT;
					}
					// Fate Unlimited Codes and Carnage Heart EXA relies on AdhocPollSocket in order to retry a failed PtpConnect, but the interval must not be too long (about 1 frame before state became Established by GetPtpStat) for Bleach Heat the Soul 7 to work properly.
					else if ((sds[i].events & ADHOC_EV_CONNECT) && ((sock->data.ptp.state == ADHOC_PTP_STATE_CLOSED && sock->attemptCount == 0) ||
						(sock->data.ptp.state == ADHOC_PTP_STATE_SYN_SENT && (static_cast<s64>(CoreTiming::GetGlobalTimeUsScaled() - sock->lastAttempt) > 1000/*std::max(1000, sock->retry_interval - 60000)*/)))) {

						sds[i].revents |= ADHOC_EV_CONNECT;
					}
					// Check for socket state (already disconnected/closed by remote peer, already closed/deleted, not a socket or not opened/connected yet?)
					// Raise ADHOC_EV_DISCONNECT, ADHOC_EV_DELETE, ADHOC_EV_INVALID on revents regardless of events as needed (similar to POLLHUP, POLLERR, and POLLNVAL on posix poll)
					if (sock->data.ptp.state == ADHOC_PTP_STATE_CLOSED) {
						if (sock->attemptCount > 0) {
							sds[i].revents |= ADHOC_EV_DISCONNECT; // remote peer has closed the socket
						}
					}
				}

				if (sock->flags & ADHOC_F_ALERTPOLL) {
					sock->alerted_flags |= ADHOC_F_ALERTPOLL;

					return ERROR_NET_ADHOC_SOCKET_ALERTED;
				}
			}
			else {
				sds[i].revents |= ADHOC_EV_INVALID;
			}
			if (sds[i].revents) affectedsockets++;
		}
	}
	else {
		// FIXME: Does AdhocPollSocket can return any error code other than ERROR_NET_ADHOC_EXCEPTION_EVENT on blocking/non-blocking mode?
		/*if (nonblock)
			affectedsockets = ERROR_NET_ADHOC_WOULD_BLOCK;
		else
			affectedsockets = ERROR_NET_ADHOC_TIMEOUT;
		*/
		affectedsockets = ERROR_NET_ADHOC_EXCEPTION_EVENT;
	}
	return affectedsockets;
}

int sceNetAdhocPollSocket(u32 socketStructAddr, int count, int timeout, int nonblock) { // timeout in microseconds
	DEBUG_LOG_REPORT_ONCE(sceNetAdhocPollSocket, SCENET, "UNTESTED sceNetAdhocPollSocket(%08x, %i, %i, %i) at %08x", socketStructAddr, count, timeout, nonblock, currentMIPS->pc);
	// Library is initialized
	if (netAdhocInited)
	{
		SceNetAdhocPollSd * sds = NULL;
		if (Memory::IsValidAddress(socketStructAddr)) sds = (SceNetAdhocPollSd *)Memory::GetPointer(socketStructAddr);

		// Valid Arguments
		if (sds != NULL && count > 0)
		{
			// Socket Check
			for (int i = 0; i < count; i++) {
				// Invalid Socket
				if (sds[i].id < 1 || sds[i].id > MAX_SOCKET || adhocSockets[sds[i].id - 1] == NULL)
					return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
			}

			// Nonblocking Mode
			if (nonblock)
				timeout = 0;

			if (count > (int)FD_SETSIZE) 
				count = FD_SETSIZE; // return 0; //ERROR_NET_ADHOC_INVALID_ARG

			// Acquire Network Lock
			//acquireNetworkLock();

			// Poll Sockets
			//int affectedsockets = sceNetInetPoll(isds, count, timeout);
			int affectedsockets = 0;
			if (nonblock)
				affectedsockets = PollAdhocSocket(sds, count, 0, nonblock);
			else {
				// Simulate blocking behaviour with non-blocking socket
				// Borrowing some arguments to pass some parameters. The dummy WaitID(count+1) might not be unique thus have duplicate possibilities if there are multiple thread trying to poll the same numbers of socket at the same time
				u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | (count + 1ULL);
				return WaitBlockingAdhocSocket(threadSocketId, ADHOC_POLL_SOCKET, count, sds, nullptr, timeout, nullptr, nullptr, "adhoc pollsocket");
			}

			// Free Network Lock
			//freeNetworkLock();

			// No Events generated
			// Non-blocking mode
			// Bleach 7 seems to use nonblocking and check the return value > 0, or 0x80410709 (ERROR_NET_ADHOC_WOULD_BLOCK), also 0x80410717 (ERROR_NET_ADHOC_EXCEPTION_EVENT), when using prx files on JPCSP it can return 0
			if (affectedsockets >= 0) {
				if (affectedsockets > 0) {
					for (int i = 0; i < count; i++) {
						if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
							auto sock = adhocSockets[sds[i].id - 1];
							if (sock->type == SOCK_PTP)
								VERBOSE_LOG(SCENET, "Poll PTP Socket Id: %d (%d), events: %08x, revents: %08x - state: %d", sds[i].id, sock->data.ptp.id, sds[i].events, sds[i].revents, sock->data.ptp.state);
							else
								VERBOSE_LOG(SCENET, "Poll PDP Socket Id: %d (%d), events: %08x, revents: %08x", sds[i].id, sock->data.pdp.id, sds[i].events, sds[i].revents);
						}
					}
				}
				// Workaround to get 30 FPS instead of the too fast 60 FPS on Fate Unlimited Codes, it's abit absurd for a non-blocking call to have this much delay tho, and hleDelayResult doesn't works as good as hleEatMicro for this workaround.
				hleEatMicro(50); // hleEatMicro(7500); // normally 1ms, but using 7.5ms here seems to show better result for Bleach Heat the Soul 7 and other games with too high FPS, but may have a risk of slowing down games that already runs at normal FPS? (need more games to test this)
				return hleLogDebug(SCENET, affectedsockets, "success");
			}
			//else if (nonblock && affectedsockets < 0)
			//	return hleLogDebug(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block"); // Is this error code valid for PollSocket? as it always returns 0 even when nonblock flag is set

			return hleLogDebug(SCENET, ERROR_NET_ADHOC_EXCEPTION_EVENT, "exception event");
		}

		// Invalid Argument
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
	}

	// Library is uninitialized
	return hleLogDebug(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "adhoc not initialized");
}

int NetAdhocPdp_Delete(int id, int unknown) {
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (id > 0 && id <= MAX_SOCKET) {
			// Cast Socket
			auto sock = adhocSockets[id - 1];

			// Valid Socket
			if (sock != NULL && sock->type == SOCK_PDP) {
				// Close Connection
				struct linger sl {};
				sl.l_onoff = 1;		// non-zero value enables linger option in kernel
				sl.l_linger = 0;	// timeout interval in seconds
				setsockopt(sock->data.pdp.id, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
				shutdown(sock->data.pdp.id, SD_BOTH);
				closesocket(sock->data.pdp.id);

				// Remove Port Forward from Router
				//sceNetPortClose("UDP", sock->lport);
				//g_PortManager.Remove(IP_PROTOCOL_UDP, isOriPort ? sock->lport : sock->lport + portOffset); // Let's not remove mapping in real-time as it could cause lags/disconnection when joining a room with slow routers

				// Free Memory
				free(sock);

				// Free Translation Slot
				adhocSockets[id - 1] = NULL;

				// Success
				return 0;
			}

			// Invalid Socket ID
			return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
		}

		// Invalid Argument
		return ERROR_NET_ADHOC_INVALID_ARG;
	}

	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

/**
 * Adhoc Emulator PDP Socket Delete
 * @param id Socket File Descriptor
 * @param flag Bitflags (Unused)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID
 */
static int sceNetAdhocPdpDelete(int id, int unknown) {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(SCENET, "sceNetAdhocPdpDelete(%d, %d) at %08x", id, unknown, currentMIPS->pc);
	/*
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	*/

	return NetAdhocPdp_Delete(id, unknown);
}

static int sceNetAdhocctlGetAdhocId(u32 productStructAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlGetAdhocId(%08x) at %08x", productStructAddr, currentMIPS->pc);
	
	if (!netAdhocctlInited)
		return hleLogDebug(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");

	auto productStruct = PSPPointer<SceNetAdhocctlAdhocId>::Create(productStructAddr);
	if (!productStruct.IsValid())
		return hleLogDebug(SCENET, ERROR_NET_ADHOCCTL_INVALID_ARG, "invalid arg");

	*productStruct = product_code;
	productStruct.NotifyWrite("NetAdhocctlGetAdhocId");

	return hleLogDebug(SCENET, 0, "type = %d, code = %s", product_code.type, product_code.data);
}

// FIXME: Scan probably not a blocking function since there is ADHOCCTL_STATE_SCANNING state that can be polled by the game, right? But apparently it need to be delayed for Naruto Shippuden Ultimate Ninja Heroes 3
int sceNetAdhocctlScan() {
	INFO_LOG(SCENET, "sceNetAdhocctlScan() at %08x", currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	// Library initialized
	if (netAdhocctlInited) {
		int us = adhocDefaultDelay;
		// FIXME: When tested with JPCSP + official prx files it seems when adhocctl in a connected state (ie. joined to a group) attempting to create/connect/join/scan will return a success (without doing anything?)
		if ((adhocctlState == ADHOCCTL_STATE_CONNECTED) || (adhocctlState == ADHOCCTL_STATE_GAMEMODE)) {
			// TODO: Valhalla Knights 2 need handler notification, but need to test this on games that doesn't use Adhocctl Handler too (not sure if there are games like that tho)
			notifyAdhocctlHandlers(ADHOCCTL_EVENT_ERROR, ERROR_NET_ADHOCCTL_ALREADY_CONNECTED);
			hleEatMicro(500);
			return 0;
		}

		// Only scan when in Disconnected state, otherwise AdhocServer will kick you out
		if (adhocctlState == ADHOCCTL_STATE_DISCONNECTED && !isAdhocctlBusy) {
			isAdhocctlBusy = true;
			isAdhocctlNeedLogin = true;
			adhocctlState = ADHOCCTL_STATE_SCANNING;
			adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;

			// Reset Networks/Group list to prevent other threads from using these soon to be replaced networks
			peerlock.lock();
			freeGroupsRecursive(networks);
			networks = NULL;
			peerlock.unlock();

			if (friendFinderRunning) {
				AdhocctlRequest req = { OPCODE_SCAN, {0} };
				return WaitBlockingAdhocctlSocket(req, us, "adhocctl scan");
			}
			else {
				adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
			}

			// Return Success and let friendFinder thread to notify the handler when scan completed
			// Not delaying here may cause Naruto Shippuden Ultimate Ninja Heroes 3 to get disconnected when the mission started
			hleEatMicro(us);
			// FIXME: When tested using JPCSP + official prx files it seems sceNetAdhocctlScan switching to a different thread for at least 100ms after returning success and before executing the next line?
			return hleDelayResult(0, "scan delay", adhocEventPollDelay);
		}
		
		// FIXME: Returning BUSY when previous adhocctl handler's callback is not fully executed yet, But returning success and notifying handler's callback with error (ie. ALREADY_CONNECTED) when previous adhocctl handler's callback is fully executed? Is there a case where error = BUSY sent through handler's callback?
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_BUSY, "busy");
	}

	// Library uninitialized
	return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");
}

int sceNetAdhocctlGetScanInfo(u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);
	SceNetAdhocctlScanInfoEmu *buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlScanInfoEmu *)Memory::GetPointer(bufAddr);

	INFO_LOG(SCENET, "sceNetAdhocctlGetScanInfo([%08x]=%i, %08x) at %08x", sizeAddr, Memory::Read_U32(sizeAddr), bufAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return 0;
	}

	// Library initialized
	if (netAdhocctlInited) {
		// Minimum Argument
		if (buflen == NULL) return ERROR_NET_ADHOCCTL_INVALID_ARG;

		// Minimum Argument Requirements
		if (buflen != NULL) {
			// FIXME: Do we need to exclude Groups created by this device it's self?
			bool excludeSelf = false;

			// Multithreading Lock
			peerlock.lock();

			// FIXME: When already connected to a group GetScanInfo will return size = 0 ? or may be only hides the group created by it's self?
			if (adhocctlState == ADHOCCTL_STATE_CONNECTED || adhocctlState == ADHOCCTL_STATE_GAMEMODE) { 
				*buflen = 0;
				DEBUG_LOG(SCENET, "NetworkList [Available: 0] Already in a Group");
			}
			// Length Returner Mode
			else if (buf == NULL) {
				int availNetworks = countAvailableNetworks(excludeSelf);
				*buflen = availNetworks * sizeof(SceNetAdhocctlScanInfoEmu);
				DEBUG_LOG(SCENET, "NetworkList [Available: %i]", availNetworks);
			}
			// Normal Information Mode
			else {
				// Clear Memory
				memset(buf, 0, *buflen);

				// Network Discovery Counter
				int discovered = 0;

				// Count requested Networks
				int requestcount = *buflen / sizeof(SceNetAdhocctlScanInfoEmu);

				// Minimum Argument Requirements
				if (requestcount > 0) {
					// Group List Element
					SceNetAdhocctlScanInfo * group = networks;

					// Iterate Group List
					for (; group != NULL && (!excludeSelf || !isLocalMAC(&group->bssid.mac_addr)) && discovered < requestcount; group = group->next) {
						// Copy Group Information
						//buf[discovered] = *group;
						buf[discovered].group_name = group->group_name;
						buf[discovered].bssid = group->bssid;
						buf[discovered].mode = group->mode;

						// Exchange Adhoc Channel
						// sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL, &buf[discovered].channel);

						// Fake Channel Number 1 on Automatic Channel (JPCSP use 11 as default). Ridge Racer 2 will ignore any groups with channel 0 or that doesn't matched with channel value returned from sceUtilityGetSystemParamInt (which mean sceUtilityGetSystemParamInt must not return channel 0 when connected to a network?)
						buf[discovered].channel = group->channel; //parameter.channel

						// Increase Discovery Counter
						discovered++;
					}

					// Link List
					for (int i = 0; i < discovered - 1; i++) {
						// Link Network
						buf[i].next = bufAddr + (sizeof(SceNetAdhocctlScanInfoEmu)*i) + sizeof(SceNetAdhocctlScanInfoEmu); // buf[i].next = &buf[i + 1];
					}

					// Fix Last Element
					if (discovered > 0) buf[discovered - 1].next = 0;
				}

				// Fix Size
				*buflen = discovered * sizeof(SceNetAdhocctlScanInfoEmu);
				DEBUG_LOG(SCENET, "NetworkList [Requested: %i][Discovered: %i]", requestcount, discovered);
			}

			// Multithreading Unlock
			peerlock.unlock();

			hleEatMicro(200);
			// Return Success
			return 0;
		}

		// Generic Error
		return -1;
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

// TODO: How many handlers can the PSP actually have for Adhocctl?
// TODO: Should we allow the same handler to be added more than once?
// FIXME: Do all Adhocctl HLE returning 0 and expecting error code through callback handler if there were error, instead of returning error code through the HLE ?
static u32 sceNetAdhocctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = 0;
	AdhocctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	while (adhocctlHandlers.find(retval) != adhocctlHandlers.end())
		++retval;

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for (auto it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
		if (it->second.entryPoint == handlerPtr) {
			foundHandler = true;
			break;
		}
	}

	if (!foundHandler && Memory::IsValidAddress(handlerPtr)) {
		if (adhocctlHandlers.size() >= MAX_ADHOCCTL_HANDLERS) {
			ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS;
			return retval;
		}
		adhocctlHandlers[retval] = handler;
		WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, retval);
	} else if(foundHandler) {
		ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);
		retval = 0; //Faking success
	} else {
		ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Invalid handler", handlerPtr, handlerArg);
		retval = ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// The id to return is the number of handlers currently registered
	return retval;
}

u32 NetAdhocctl_Disconnect() {
	// Library initialized
	if (netAdhocctlInited) {
		int iResult, error;
		// We might need to have at least 16ms (1 frame?) delay before the game calls the next Adhocctl syscall for Tekken 6 not to stuck when exiting Lobby
		hleEatMicro(16667);

		if (isAdhocctlBusy && CoreTiming::IsScheduled(adhocctlNotifyEvent)) {
			return ERROR_NET_ADHOCCTL_BUSY;
		}

		// Connected State (Adhoc Mode). Attempting to leave a group while not in a group will be kicked out by Adhoc Server (ie. some games tries to disconnect more than once within a short time)
		if (adhocctlState != ADHOCCTL_STATE_DISCONNECTED) { 
			isAdhocctlBusy = true;

			// Clear Network Name
			memset(&parameter.group_name, 0, sizeof(parameter.group_name));

			// Set HUD Connection Status
			//setConnectionStatus(0);

			// Prepare Packet
			uint8_t opcode = OPCODE_DISCONNECT;

			// Acquire Network Lock
			//_acquireNetworkLock();

			// Send Disconnect Request Packet
			iResult = send((int)metasocket, (const char*)&opcode, 1, MSG_NOSIGNAL);
			error = errno;

			// Sending may get socket error 10053 if the AdhocServer is already shutted down
			if (iResult == SOCKET_ERROR) {
				if (error != EAGAIN && error != EWOULDBLOCK) {
					ERROR_LOG(SCENET, "Socket error (%i) when sending", error);
					// Set Disconnected State
					adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
				} 
				else if (friendFinderRunning) {
					AdhocctlRequest req = { OPCODE_DISCONNECT, {0} };
					WaitBlockingAdhocctlSocket(req, 0, "adhocctl disconnect");
				}
				else {
					// Set Disconnected State
					return ERROR_NET_ADHOCCTL_BUSY;
				}
			}

			// Free Network Lock
			//_freeNetworkLock();
		}

		// Multithreading Lock
		//peerlock.lock();

		// Clear Peer List, since games are moving to a different a group when the mission started may be we shouldn't free all peers yet
		int32_t peercount = 0;
		timeoutFriendsRecursive(friends, &peercount);
		INFO_LOG(SCENET, "Marked for Timedout Peer List (%i)", peercount);
		// Delete Peer Reference
		//friends = NULL;

		// Clear Group List
		//freeGroupsRecursive(networks);

		// Delete Group Reference
		//networks = NULL;

		// Multithreading Unlock
		//peerlock.unlock();

		adhocctlCurrentMode = ADHOCCTL_MODE_NONE;
		// Notify Event Handlers (even if we weren't connected, not doing this will freeze games like God Eater, which expect this behaviour)
		// FIXME: When there are no handler the state will immediately became ADHOCCTL_STATE_DISCONNECTED ?
		// Note: Metal Gear Acid [2] never register a handler until it's successfully connected to a group and have a connected socket to other player, thus adhocctlHandlers is always empty here.
		notifyAdhocctlHandlers(ADHOCCTL_EVENT_DISCONNECT, 0);

		// Return Success, some games might ignore returned value and always treat it as success, otherwise repeatedly calling this function
		return 0;
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

static u32 sceNetAdhocctlDisconnect() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	char grpName[9] = { 0 };
	memcpy(grpName, parameter.group_name.data, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	int ret = NetAdhocctl_Disconnect();
	INFO_LOG(SCENET, "%08x=sceNetAdhocctlDisconnect() at %08x [group=%s]", ret, currentMIPS->pc, grpName);

	return ret;
}

static u32 sceNetAdhocctlDelHandler(u32 handlerID) {
	if (!netAdhocctlInited)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "adhocctl not initialized");

	if (adhocctlHandlers.find(handlerID) != adhocctlHandlers.end()) {
		adhocctlHandlers.erase(handlerID);
		INFO_LOG(SCENET, "sceNetAdhocctlDelHandler(%d) at %08x", handlerID, currentMIPS->pc);
	} else {
		WARN_LOG(SCENET, "sceNetAdhocctlDelHandler(%d): Invalid Handler ID", handlerID);
	}

	return 0;
}

int NetAdhocctl_Term() {
	if (netAdhocctlInited) {
		if (adhocctlState != ADHOCCTL_STATE_DISCONNECTED) {
			// Note: This might block current thread if the first attempt to send OPCODE_DISCONNECT to AdhocServer failed with EAGAIN error
			if (netAdhocGameModeEntered)
				NetAdhocctl_ExitGameMode();
			else
				NetAdhocctl_Disconnect();
		}

		// Terminate Adhoc Threads
		friendFinderRunning = false;
		if (friendFinderThread.joinable()) {
			friendFinderThread.join();
		}

		// TODO: May need to block current thread to make sure all Adhocctl callbacks have been fully executed before terminating Adhoc PSPThread (ie. threadAdhocID).

		// Clear GameMode resources
		NetAdhocGameMode_DeleteMaster();
		deleteAllGMB();

		// Clear Peer List
		int32_t peercount = 0;
		freeFriendsRecursive(friends, &peercount);
		INFO_LOG(SCENET, "Cleared Peer List (%i)", peercount);
		// Delete Peer Reference
		friends = NULL;
		//May also need to clear Handlers
		adhocctlHandlers.clear();
		// Free stuff here
		networkInited = false;
		shutdown((int)metasocket, SD_BOTH);
		closesocket((int)metasocket);
		metasocket = (int)INVALID_SOCKET;
		// Delete fake PSP Thread. 
		// kernelObjects may already been cleared early during a Shutdown, thus trying to access it may generates Warning/Error in the log
		if (threadAdhocID > 0 && strcmp(__KernelGetThreadName(threadAdhocID), "ERROR") != 0) {
			__KernelStopThread(threadAdhocID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocThread stopped");
			__KernelDeleteThread(threadAdhocID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocThread deleted");
		}
		threadAdhocID = 0;
		adhocctlCurrentMode = ADHOCCTL_MODE_NONE;
		isAdhocctlBusy = false;
		netAdhocctlInited = false;
	}

	return 0;
}

int sceNetAdhocctlTerm() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(SCENET, "sceNetAdhocctlTerm() at %08x", currentMIPS->pc);

	//if (netAdhocMatchingInited) NetAdhocMatching_Term();
	int retval = NetAdhocctl_Term();

	hleEatMicro(adhocDefaultDelay);
	return retval;
}

static int sceNetAdhocctlGetNameByAddr(const char *mac, u32 nameAddr) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocctlGetNameByAddr(%s, %08x) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), nameAddr, currentMIPS->pc);
	
	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Arguments
		if (mac != NULL && Memory::IsValidAddress(nameAddr))
		{
			SceNetAdhocctlNickname * nickname = (SceNetAdhocctlNickname *)Memory::GetPointer(nameAddr);
			// Get Local MAC Address
			SceNetEtherAddr localmac; 
			getLocalMac(&localmac);

			// Local MAC Matches
			if (isMacMatch(&localmac, (const SceNetEtherAddr*)mac))
			{
				// Write Data
				*nickname = parameter.nickname;

				DEBUG_LOG(SCENET, "sceNetAdhocctlGetNameByAddr - [PlayerName:%s]", (char*)nickname);

				// Return Success
				return 0;
			}

			// Multithreading Lock
			peerlock.lock(); 

			// Peer Reference
			SceNetAdhocctlPeerInfo * peer = friends;

			// Iterate Peers
			for (; peer != NULL; peer = peer->next)
			{
				// Match found
				if (peer->last_recv != 0 && isMacMatch(&peer->mac_addr, (const SceNetEtherAddr*)mac))
				{
					// Write Data
					*nickname = peer->nickname;

					// Multithreading Unlock
					peerlock.unlock(); 

					DEBUG_LOG(SCENET, "sceNetAdhocctlGetNameByAddr - [PeerName:%s]", (char*)nickname);

					// Return Success
					return 0;
				}
			}

			// Multithreading Unlock
			peerlock.unlock(); 

			DEBUG_LOG(SCENET, "sceNetAdhocctlGetNameByAddr - PlayerName not found");
			// Player not found
			return ERROR_NET_ADHOC_NO_ENTRY;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

int sceNetAdhocctlGetPeerInfo(const char *mac, int size, u32 peerInfoAddr) {
	VERBOSE_LOG(SCENET, "sceNetAdhocctlGetPeerInfo(%s, %i, %08x) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), size, peerInfoAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	SceNetEtherAddr * maddr = (SceNetEtherAddr *)mac;
	SceNetAdhocctlPeerInfoEmu * buf = NULL;
	if (Memory::IsValidAddress(peerInfoAddr)) {
		buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointer(peerInfoAddr);
	}
	// Library initialized
	if (netAdhocctlInited) {
		if ((size < (int)sizeof(SceNetAdhocctlPeerInfoEmu)) || (buf == NULL)) return ERROR_NET_ADHOCCTL_INVALID_ARG;
		
		int retval = ERROR_NET_ADHOC_NO_ENTRY; // -1;

		// Local MAC
		if (isLocalMAC(maddr)) {
			SceNetAdhocctlNickname nickname;

			truncate_cpy((char*)&nickname.data, ADHOCCTL_NICKNAME_LEN, g_Config.sNickName.c_str());
			buf->next = 0;
			buf->nickname = nickname;
			buf->nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
			buf->mac_addr = *maddr;
			buf->flags = 0x0400;
			buf->padding = 0;
			buf->last_recv = std::max(0LL, static_cast<s64>(CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta));

			// Success
			retval = 0;
		}
		// Find Peer by MAC
		else 
		{
			// Multithreading Lock
			peerlock.lock();

			SceNetAdhocctlPeerInfo * peer = findFriend(maddr);
			if (peer != NULL && peer->last_recv != 0) {
				// Fake Receive Time
				peer->last_recv = std::max(peer->last_recv, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);

				buf->next = 0;
				buf->nickname = peer->nickname;
				buf->nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
				buf->mac_addr = *maddr;
				buf->flags = 0x0400; //peer->ip_addr;
				buf->padding = 0;
				buf->last_recv = peer->last_recv; //CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0; //(uint64_t)time(NULL); //This timestamp is important issue on Dissidia 012

				// Success
				retval = 0;
			}

			// Multithreading Unlock
			peerlock.unlock();
		}
		hleEatMicro(50);
		return retval;
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

int NetAdhocctl_Create(const char* groupName) {
	const SceNetAdhocctlGroupName* groupNameStruct = (const SceNetAdhocctlGroupName*)groupName;
	// Library initialized
	if (netAdhocctlInited) {
		// Valid Argument
		if (validNetworkName(groupNameStruct)) {
			// FIXME: When tested with JPCSP + official prx files it seems when adhocctl in a connected state (ie. joined to a group) attempting to create/connect/join/scan will return a success (without doing anything?)
			if ((adhocctlState == ADHOCCTL_STATE_CONNECTED) || (adhocctlState == ADHOCCTL_STATE_GAMEMODE)) {
				// TODO: Need to test this on games that doesn't use Adhocctl Handler too (not sure if there are games like that tho)
				notifyAdhocctlHandlers(ADHOCCTL_EVENT_ERROR, ERROR_NET_ADHOCCTL_ALREADY_CONNECTED);
				hleEatMicro(500);
				return 0;
			}

			// Disconnected State
			if (adhocctlState == ADHOCCTL_STATE_DISCONNECTED && !isAdhocctlBusy) {
				isAdhocctlBusy = true;
				isAdhocctlNeedLogin = true;

				// Set Network Name
				if (groupNameStruct != NULL) 
					parameter.group_name = *groupNameStruct;

				// Reset Network Name
				else 
					memset(&parameter.group_name, 0, sizeof(parameter.group_name));

				// Set HUD Connection Status
				//setConnectionStatus(1);

				// Wait for Status to be connected to prevent Ford Street Racing from Failed to create game session
				int us = adhocDefaultDelay;
				if (friendFinderRunning) {
					AdhocctlRequest req = { OPCODE_CONNECT, parameter.group_name };
					return WaitBlockingAdhocctlSocket(req, us, "adhocctl connect");
				}
				//Faking success, to prevent Full Auto 2 from freezing while Initializing Network
				else {
					adhocctlStartTime = (u64)(time_now_d() * 1000000.0);
					if (adhocctlCurrentMode == ADHOCCTL_MODE_GAMEMODE) {
						adhocctlState = ADHOCCTL_STATE_GAMEMODE;
						notifyAdhocctlHandlers(ADHOCCTL_EVENT_GAME, 0);
					}
					else {
						adhocctlState = ADHOCCTL_STATE_CONNECTED;
						// Notify Event Handlers, Needed for the Nickname to be shown on the screen when success is faked
						// Connected Event's mipscall need be executed before returning from sceNetAdhocctlCreate (or before the next sceNet function?)
						notifyAdhocctlHandlers(ADHOCCTL_EVENT_CONNECT, 0); //CoreTiming::ScheduleEvent_Threadsafe_Immediate(eventAdhocctlHandlerUpdate, join32(ADHOCCTL_EVENT_CONNECT, 0)); 
					}
				}

				hleEatMicro(us);
				// Return Success
				// FIXME: When tested using JPCSP + official prx files it seems sceNetAdhocctlCreate switching to a different thread for at least 100ms after returning success and before executing the next line.
				return 0;
			}

			// Connected State
			return ERROR_NET_ADHOCCTL_BUSY; // ERROR_NET_ADHOCCTL_BUSY may trigger the game (ie. Ford Street Racing) to call sceNetAdhocctlDisconnect
		}

		// Invalid Argument
		return ERROR_NET_ADHOC_INVALID_ARG;
	}
	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

/**
 * Create and / or Join a Virtual Network of the specified Name
 * @param group_name Virtual Network Name
 * @return 0 on success or... ADHOCCTL_NOT_INITIALIZED, ADHOCCTL_INVALID_ARG, ADHOCCTL_BUSY
 */
int sceNetAdhocctlCreate(const char *groupName) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (groupName)
		memcpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	INFO_LOG(SCENET, "sceNetAdhocctlCreate(%s) at %08x", grpName, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
	adhocConnectionType = ADHOC_CREATE;
	return hleLogDebug(SCENET, NetAdhocctl_Create(groupName), "");
}

int sceNetAdhocctlConnect(const char* groupName) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (groupName)
		memcpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	INFO_LOG(SCENET, "sceNetAdhocctlConnect(%s) at %08x", grpName, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
	adhocConnectionType = ADHOC_CONNECT;
	return hleLogDebug(SCENET, NetAdhocctl_Create(groupName), "");
}

int sceNetAdhocctlJoin(u32 scanInfoAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlJoin(%08x) at %08x", scanInfoAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Argument
		if (Memory::IsValidAddress(scanInfoAddr))
		{
			SceNetAdhocctlScanInfoEmu* sinfo = (SceNetAdhocctlScanInfoEmu*)Memory::GetPointer(scanInfoAddr);
			char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
			memcpy(grpName, sinfo->group_name.data, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
			DEBUG_LOG(SCENET, "sceNetAdhocctlJoin - Group: %s", grpName);

			// We can ignore minor connection process differences here
			// TODO: Adhoc Server may need to be changed to differentiate between Host/Create and Join, otherwise it can't support multiple Host using the same Group name, thus causing one of the Host to be confused being treated as Join.
			adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
			adhocConnectionType = ADHOC_JOIN;
			return hleLogDebug(SCENET, NetAdhocctl_Create(grpName), "");
		}

		// Invalid Argument
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

int NetAdhocctl_CreateEnterGameMode(const char* group_name, int game_type, int num_members, u32 membersAddr, u32 timeout, int flag) {
	if (!netAdhocctlInited)
		return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;

	if (!Memory::IsValidAddress(membersAddr))
		return ERROR_NET_ADHOCCTL_INVALID_ARG;

	if (game_type < ADHOCCTL_GAMETYPE_1A || game_type > ADHOCCTL_GAMETYPE_2A || num_members < 2 || num_members > 16 || (game_type == ADHOCCTL_GAMETYPE_1A && num_members > 4))
		return ERROR_NET_ADHOCCTL_INVALID_ARG;

	deleteAllGMB();
	gameModePeerPorts.clear();

	SceNetEtherAddr* addrs = PSPPointer<SceNetEtherAddr>::Create(membersAddr); // List of participating MAC addresses (started from host)
	for (int i = 0; i < num_members; i++) {
		requiredGameModeMacs.push_back(*addrs);
		DEBUG_LOG(SCENET, "GameMode macAddress#%d=%s", i, mac2str(addrs).c_str());
		addrs++;
	}
	// Add local MAC (Host) first
	SceNetEtherAddr localMac;
	getLocalMac(&localMac);
	gameModeMacs.push_back(localMac);

	// FIXME: There seems to be an internal Adhocctl Handler on official prx (running on "SceNetAdhocctl" thread) that will try to sync GameMode timings, by using blocking PTP socket:
	// 1). PtpListen (srcMacAddress=0x09F20CB4, srcPort=0x8001, bufSize=0x2000, retryDelay=0x30D40, retryCount=0x33, queue=0x1, unk1=0x0)
	// 2). PtpAccpet (peerMacAddr=0x09FE2020, peerPortAddr=0x09FE2010, timeout=0x765BB0, nonblock=0x0) - probably for each clients
	// 3). PtpSend (data=0x09F20E18, dataSizeAddr=0x09FE2094, timeout=0x627EDA, nonblock=0x0) - not sure what kind of data nor the size (more than 6 bytes)
	// 4). PtpFlush (timeout=0x2DC6C0, nonblock=0x0)
	// 5 & 6). PtpClose (accepted socket & listen socket)
	// When timeout reached, notify user-defined Adhocctl Handlers with ERROR event (ERROR_NET_ADHOC_TIMEOUT) instead of GAMEMODE event

	// We have to wait for all the MACs to have joined to go into CONNECTED state
	adhocctlCurrentMode = ADHOCCTL_MODE_GAMEMODE;
	adhocConnectionType = ADHOC_CREATE;
	netAdhocGameModeEntered = true;
	netAdhocEnterGameModeTimeout = timeout;
	return NetAdhocctl_Create(group_name);
}

/**
* Connect to the Adhoc control game mode (as a host)
*
* @param group_name - The name of the connection (maximum 8 alphanumeric characters).
* @param game_type - Pass 1.
* @param num_members - The total number of players (including the host).
* @param membersmacAddr - A pointer to a list of the participating mac addresses, host first, then clients.
* @param timeout - Timeout in microseconds.
* @param flag - pass 0.
*
* @return 0 on success, < 0 on error.
*/
static int sceNetAdhocctlCreateEnterGameMode(const char * group_name, int game_type, int num_members, u32 membersAddr, int timeout, int flag) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (group_name)
		memcpy(grpName, group_name, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlCreateEnterGameMode, SCENET, "UNTESTED sceNetAdhocctlCreateEnterGameMode(%s, %i, %i, %08x, %i, %i) at %08x", grpName, game_type, num_members, membersAddr, timeout, flag, currentMIPS->pc);

	return hleLogDebug(SCENET, NetAdhocctl_CreateEnterGameMode(group_name, game_type, num_members, membersAddr, timeout, flag), "");
}

/**
* Connect to the Adhoc control game mode (as a client)
*
* @param group_name - The name of the connection (maximum 8 alphanumeric characters).
* @param hostmacAddr - The mac address of the host.
* @param timeout - Timeout in microseconds.
* @param flag - pass 0.
*
* @return 0 on success, < 0 on error.
*/
static int sceNetAdhocctlJoinEnterGameMode(const char * group_name, const char *hostMac, int timeout, int flag) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (group_name)
		memcpy(grpName, group_name, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlJoinEnterGameMode, SCENET, "UNTESTED sceNetAdhocctlJoinEnterGameMode(%s, %s, %i, %i) at %08x", grpName, mac2str((SceNetEtherAddr*)hostMac).c_str(), timeout, flag, currentMIPS->pc);

	if (!netAdhocctlInited)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");

	if (!hostMac)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_INVALID_ARG, "invalid arg");

	deleteAllGMB();

	// Add host mac first
	gameModeMacs.push_back(*(SceNetEtherAddr*)hostMac);

	// FIXME: There seems to be an internal Adhocctl Handler on official prx (running on "SceNetAdhocctl" thread) that will try to sync GameMode timings, by using blocking PTP socket:
	// 1). PtpOpen (srcMacAddress=0x09FE2080, srcPort=0x8001, destMacAddress=0x09F20CB4, destPort=0x8001, bufSize=0x2000, retryDelay=0x30D40, retryCount=0x33, unk1=0x0)
	// 2). PtpConnect (timeout=0x874CAC, nonblock=0x0) - to host/creator
	// 3). PtpRecv (data=0x09F20E18, dataSizeAddr=0x09FE2044, timeout=0x647553, nonblock=0x0) - repeated until data fully received with data address/offset adjusted (increased) and timeout adjusted (decreased), probably also adjusted data size (decreased) on each call
	// 4). PtpClose
	// When timeout reached, notify user-defined Adhocctl Handlers with ERROR event (ERROR_NET_ADHOC_TIMEOUT) instead of GAMEMODE event

	adhocctlCurrentMode = ADHOCCTL_MODE_GAMEMODE;
	adhocConnectionType = ADHOC_JOIN;
	netAdhocGameModeEntered = true;
	netAdhocEnterGameModeTimeout = timeout;
	return hleLogDebug(SCENET, NetAdhocctl_Create(group_name), "");
}

/**
* Create and Join a GameMode Network as Host (with Minimum Peer Check)
* @param group_name Virtual Network Name
* @param game_type Network Type (1A, 1B, 2A)
* @param min_members Minimum Number of Peers
* @param num_members Total Number of Peers (including Host)
* @param members MAC Address List of Peers (own MAC at Index 0)
* @param timeout Timeout Value (in Microseconds)
* @param flag Unused Bitflags
* @return 0 on success or... ADHOCCTL_NOT_INITIALIZED, ADHOCCTL_INVALID_ARG, ADHOCCTL_BUSY, ADHOCCTL_CHANNEL_NOT_AVAILABLE
*/
int sceNetAdhocctlCreateEnterGameModeMin(const char *group_name, int game_type, int min_members, int num_members, u32 membersAddr, u32 timeout, int flag) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (group_name)
		memcpy(grpName, group_name, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlCreateEnterGameModeMin, SCENET, "UNTESTED sceNetAdhocctlCreateEnterGameModeMin(%s, %i, %i, %i, %08x, %d, %i) at %08x", grpName, game_type, min_members, num_members, membersAddr, timeout, flag, currentMIPS->pc);
	// We don't really need the Minimum User Check
	return hleLogDebug(SCENET, NetAdhocctl_CreateEnterGameMode(group_name, game_type, num_members, membersAddr, timeout, flag), "");
}

int NetAdhoc_Term() {
	// Since Adhocctl & AdhocMatching uses Sockets & Threads we should terminate them also to release their resources
	if (netAdhocMatchingInited) 
		NetAdhocMatching_Term();
	if (netAdhocctlInited)
		NetAdhocctl_Term();

	// Library is initialized
	if (netAdhocInited) {
		// Delete GameMode Buffers
		deleteAllGMB();

		// Delete Adhoc Sockets
		deleteAllAdhocSockets();

		// Terminate Internet Library
		//sceNetInetTerm();

		// Unload Internet Modules (Just keep it in memory... unloading crashes?!)
		// if (_manage_modules != 0) sceUtilityUnloadModule(PSP_MODULE_NET_INET);
		// Library shutdown

		netAdhocInited = false;
		//return hleLogSuccessInfoI(SCENET, 0);
	}
	/*else {
		// TODO: Reportedly returns SCE_KERNEL_ERROR_LWMUTEX_NOT_FOUND in some cases?
		// Only seen returning 0 in tests.
		return hleLogWarning(SCENET, 0, "already uninitialized");
	}*/

	return 0;
}

int sceNetAdhocTerm() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup all the sockets right?
	int retval = NetAdhoc_Term();

	hleEatMicro(adhocDefaultDelay);
	return hleLogSuccessInfoI(SCENET, retval);
}

static int sceNetAdhocGetPdpStat(u32 structSize, u32 structAddr) {
	VERBOSE_LOG(SCENET, "sceNetAdhocGetPdpStat(%08x, %08x) at %08x", structSize, structAddr, currentMIPS->pc);
	
	// Library is initialized
	if (netAdhocInited)
	{
		s32_le *buflen = NULL;
		if (Memory::IsValidAddress(structSize)) buflen = (s32_le *)Memory::GetPointer(structSize);
		SceNetAdhocPdpStat *buf = NULL;
		if (Memory::IsValidAddress(structAddr)) buf = (SceNetAdhocPdpStat *)Memory::GetPointer(structAddr);

		// Socket Count
		int socketcount = getPDPSocketCount();

		// Length Returner Mode
		if (buflen != NULL && buf == NULL)
		{
			// Return Required Size
			*buflen = sizeof(SceNetAdhocPdpStat) * socketcount;
			VERBOSE_LOG(SCENET, "Stat PDP Socket Count: %d", socketcount);

			// Success
			return 0;
		}

		// Status Returner Mode
		else if (buflen != NULL && buf != NULL)
		{
			// Figure out how many Sockets we will return
			int count = *buflen / sizeof(SceNetAdhocPdpStat);
			if (count > socketcount) count = socketcount;

			// Copy Counter
			int i = 0;

			// Iterate Translation Table
			for (int j = 0; j < MAX_SOCKET && i < count; j++)
			{
				// Valid Socket Entry
				auto sock = adhocSockets[j];
				if (sock != NULL && sock->type == SOCK_PDP) {
					// Set available bytes to be received. With FIONREAD There might be ghosting 1 byte in recv buffer when remote peer's socket got closed (ie. Warriors Orochi 2) Attempting to recv this ghost 1 byte will result to socket error 10054 (may need to disable SIO_UDP_CONNRESET error)
					// It seems real PSP respecting the socket buffer size arg, so we may need to cap the value up to the buffer size arg since we use larger buffer, for PDP/UDP the total size must not contains partial/truncated message to avoid data loss.
					// TODO: We may need to manage PDP messages ourself by reading each msg 1-by-1 and moving it to our internal buffer(msg array) in order to calculate the correct messages size that can fit into buffer size when there are more than 1 messages in the recv buffer (simulate FIONREAD)
					sock->data.pdp.rcv_sb_cc = getAvailToRecv(sock->data.pdp.id, sock->buffer_size);
					// There might be a possibility for the data to be taken by the OS, thus FIONREAD returns 0, but can be Received
					if (sock->data.pdp.rcv_sb_cc == 0) {
						// Let's try to peek the data size
						// TODO: May need to filter out packets from an IP that can't be translated to MAC address
						struct sockaddr_in sin;
						socklen_t sinlen;
						sinlen = sizeof(sin);
						memset(&sin, 0, sinlen);
						int received = recvfrom(sock->data.pdp.id, dummyPeekBuf64k, std::min((u32)dummyPeekBuf64kSize, sock->buffer_size), MSG_PEEK | MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
						if (received > 0)
							sock->data.pdp.rcv_sb_cc = received;
					}

					// Copy Socket Data from Internal Memory
					memcpy(&buf[i], &sock->data.pdp, sizeof(SceNetAdhocPdpStat));

					// Fix Client View Socket ID
					buf[i].id = j + 1;

					// Write End of List Reference
					buf[i].next = 0;

					// Link Previous Element
					if (i > 0) 
						buf[i - 1].next = structAddr + (i * sizeof(SceNetAdhocPdpStat));

					VERBOSE_LOG(SCENET, "Stat PDP Socket Id: %d (%d), LPort: %d, RecvSbCC: %d", buf[i].id, sock->data.pdp.id, buf[i].lport, buf[i].rcv_sb_cc);

					// Increment Counter
					i++;
				}
			}

			// Update Buffer Length
			*buflen = i * sizeof(SceNetAdhocPdpStat);

			hleEatMicro(50); // Not sure how long it supposed to take
			// Success
			return 0;
		}

		// Invalid Arguments
		return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg, at %08x", currentMIPS->pc);
	}

	// Library is uninitialized
	return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized, at %08x", currentMIPS->pc);
}


/**
 * Adhoc Emulator PTP Socket List Getter
 * @param buflen IN: Length of Buffer in Bytes OUT: Required Length of Buffer in Bytes
 * @param buf PTP Socket List Buffer (can be NULL if you wish to receive Required Length)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED
 */
static int sceNetAdhocGetPtpStat(u32 structSize, u32 structAddr) {
	// Spams a lot 
	VERBOSE_LOG(SCENET,"sceNetAdhocGetPtpStat(%08x, %08x) at %08x",structSize,structAddr,currentMIPS->pc);

	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(structSize)) buflen = (s32_le *)Memory::GetPointer(structSize);
	SceNetAdhocPtpStat *buf = NULL;
	if (Memory::IsValidAddress(structAddr)) buf = (SceNetAdhocPtpStat *)Memory::GetPointer(structAddr);

	// Library is initialized
	if (netAdhocInited) {
		// Socket Count
		int socketcount = getPTPSocketCount();

		// Length Returner Mode
		if (buflen != NULL && buf == NULL) {
			// Return Required Size
			*buflen = sizeof(SceNetAdhocPtpStat) * socketcount;
			VERBOSE_LOG(SCENET, "Stat PTP Socket Count: %d", socketcount);
			
			// Success
			return 0;
		}
		
		// Status Returner Mode
		else if (buflen != NULL && buf != NULL) {
			// Figure out how many Sockets we will return
			int count = *buflen / sizeof(SceNetAdhocPtpStat);
			if (count > socketcount) count = socketcount;
			
			// Copy Counter
			int i = 0;
			
			// Iterate Sockets
			for (int j = 0; j < MAX_SOCKET && i < count; j++) {
				// Valid Socket Entry
				auto sock = adhocSockets[j];
				if ( sock != NULL && sock->type == SOCK_PTP) {
					// Update connection state. 
					// GvG Next Plus relies on GetPtpStat to determine if Connection has been Established or not, but should not be updated too long for GvG to work, and should not be updated too fast(need to be 1 frame after PollSocket checking for ADHOC_EV_CONNECT) for Bleach Heat the Soul 7 to work properly.
					if ((sock->data.ptp.state == ADHOC_PTP_STATE_SYN_SENT || sock->data.ptp.state == ADHOC_PTP_STATE_SYN_RCVD) && (static_cast<s64>(CoreTiming::GetGlobalTimeUsScaled() - sock->lastAttempt) > 33333/*sock->retry_interval*/)) {
						// FIXME: May be we should poll all of them together on a single poll call instead of each socket separately?
						if (IsSocketReady(sock->data.ptp.id, true, true) > 0) {
							struct sockaddr_in sin;
							socklen_t sinlen = sizeof(sin);
							memset(&sin, 0, sinlen);
							// Ensure that the connection really established or not, since "select" alone can't accurately detects it
							if (getpeername(sock->data.ptp.id, (struct sockaddr*)&sin, &sinlen) != SOCKET_ERROR) {
								sock->data.ptp.state = ADHOC_PTP_STATE_ESTABLISHED;
							}
						}
					}

					// Set available bytes to be received
					sock->data.ptp.rcv_sb_cc = getAvailToRecv(sock->data.ptp.id);
					// It seems real PSP respecting the socket buffer size arg, so we may need to cap the value to the buffer size arg since we use larger buffer
					sock->data.ptp.rcv_sb_cc = std::min(sock->data.ptp.rcv_sb_cc, (u32_le)sock->buffer_size);
					// There might be a possibility for the data to be taken by the OS, thus FIONREAD returns 0, but can be Received
					if (sock->data.ptp.rcv_sb_cc == 0) {
						// Let's try to peek the data size
						int received = recv(sock->data.ptp.id, dummyPeekBuf64k, std::min((u32)dummyPeekBuf64kSize, sock->buffer_size), MSG_PEEK | MSG_NOSIGNAL);
						if (received > 0)
							sock->data.ptp.rcv_sb_cc = received;
					}

					// Copy Socket Data from internal Memory
					memcpy(&buf[i], &sock->data.ptp, sizeof(SceNetAdhocPtpStat));
					
					// Fix Client View Socket ID
					buf[i].id = j + 1;

					// Write End of List Reference
					buf[i].next = 0;
					
					// Link previous Element to this one
					if (i > 0)
						buf[i - 1].next = structAddr + (i * sizeof(SceNetAdhocPtpStat));

					VERBOSE_LOG(SCENET, "Stat PTP Socket Id: %d (%d), LPort: %d, RecvSbCC: %d, State: %d", buf[i].id, sock->data.ptp.id, buf[i].lport, buf[i].rcv_sb_cc, buf[i].state);
					
					// Increment Counter
					i++;
				}
			}
			
			// Update Buffer Length
			*buflen = i * sizeof(SceNetAdhocPtpStat);
			
			hleEatMicro(50); // Not sure how long it takes, since GetPtpStat didn't get logged when using prx files on JPCSP
			// Success
			return 0;
		}
		
		// Invalid Arguments
		return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg, at %08x", currentMIPS->pc);
	}
	
	// Library is uninitialized
	return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized, at %08x", currentMIPS->pc);
}


int RecreatePtpSocket(int ptpId) {
	auto sock = adhocSockets[ptpId - 1];
	if (!sock) {
		return ERROR_NET_ADHOC_SOCKET_ID_NOT_AVAIL;
	}

	// Close old socket
	struct linger sl {};
	sl.l_onoff = 1;		// non-zero value enables linger option in kernel
	sl.l_linger = 0;	// timeout interval in seconds
	setsockopt(sock->data.ptp.id, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
	closesocket(sock->data.ptp.id);

	// Create a new socket
	int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Valid Socket produced
	if (tcpsocket < 0)
		return ERROR_NET_ADHOC_SOCKET_ID_NOT_AVAIL;

	// Update posix socket fd
	sock->data.ptp.id = tcpsocket;

	// Change socket MSS
	setSockMSS(tcpsocket, PSP_ADHOC_PTP_MSS);

	// Change socket buffer size to be consistent on all platforms.
	setSockBufferSize(tcpsocket, SO_SNDBUF, sock->buffer_size * 5); //PSP_ADHOC_PTP_MSS
	setSockBufferSize(tcpsocket, SO_RCVBUF, sock->buffer_size * 10); //PSP_ADHOC_PTP_MSS*10

	// Enable KeepAlive
	setSockKeepAlive(tcpsocket, true, sock->retry_interval / 1000000L, sock->retry_count);

	// Ignore SIGPIPE when supported (ie. BSD/MacOS)
	setSockNoSIGPIPE(tcpsocket, 1);

	// Enable Port Re-use
	setSockReuseAddrPort(tcpsocket);

	// Apply Default Send Timeout Settings to Socket
	setSockTimeout(tcpsocket, SO_SNDTIMEO, sock->retry_interval);

	// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
	setSockNoDelay(tcpsocket, 1);

	// Binding Information for local Port
	struct sockaddr_in addr {};
	// addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	if (isLocalServer) {
		getLocalIp(&addr);
	}
	uint16_t requestedport = static_cast<int>(sock->data.ptp.lport + static_cast<int>(portOffset));
	// Avoid getting random port due to port offset when original port wasn't 0 (ie. original_port + port_offset = 65536 = 0)
	if (requestedport == 0 && sock->data.ptp.lport > 0)
		requestedport = 65535; // Hopefully it will be safe to default it to 65535 since there can't be more than one port that can bumped into 65536
	addr.sin_port = htons(requestedport);

	// Bound Socket to local Port
	if (bind(tcpsocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		ERROR_LOG(SCENET, "RecreatePtpSocket(%i) - Socket error (%i) when binding port %u", ptpId, errno, ntohs(addr.sin_port));
	}
	else {
		// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
		socklen_t len = sizeof(addr);
		if (getsockname(tcpsocket, (struct sockaddr*)&addr, &len) == 0) {
			uint16_t boundport = ntohs(addr.sin_port);
			if (sock->data.ptp.lport + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
				WARN_LOG(SCENET, "RecreatePtpSocket(%i) - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", ptpId, sock->data.ptp.lport, requestedport, boundport, boundport - portOffset);
			u16 newlport = boundport - portOffset;
			if (newlport != sock->data.ptp.lport) {
				WARN_LOG(SCENET, "RecreatePtpSocket(%i) - Old and New LPort is different! The port may need to be reforwarded", ptpId);
				if (!sock->isClient)
					UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? newlport : newlport + portOffset, newlport + portOffset);
			}
			sock->data.ptp.lport = newlport;
		}
		else {
			WARN_LOG(SCENET, "RecreatePtpSocket(%i): getsockname error %i", ptpId, errno);
		}
	}

	// Switch to non-blocking for further usage
	changeBlockingMode(tcpsocket, 1);

	return 0;
}

/**
 * Adhoc Emulator PTP Active Socket Creator
 * @param saddr Local MAC (Unused)
 * @param sport Local Binding Port
 * @param daddr Target MAC
 * @param dport Target Port
 * @param bufsize Socket Buffer Size
 * @param rexmt_int Retransmit Interval (in Microseconds)
 * @param rexmt_cnt Retransmit Count
 * @param flag Bitflags (Unused)
 * @return Socket ID > 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_ADDR, ADHOC_INVALID_PORT
 */
static int sceNetAdhocPtpOpen(const char *srcmac, int sport, const char *dstmac, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) {
	INFO_LOG(SCENET, "sceNetAdhocPtpOpen(%s, %d, %s, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport, mac2str((SceNetEtherAddr*)dstmac).c_str(),dport,bufsize, rexmt_int, rexmt_cnt, flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	SceNetEtherAddr* saddr = (SceNetEtherAddr*)srcmac;
	SceNetEtherAddr* daddr = (SceNetEtherAddr*)dstmac;
	bool isClient = false;
	// Library is initialized
	if (netAdhocInited) {
		// Some games (ie. DBZ Shin Budokai 2) might be getting the saddr/srcmac content from SaveState and causing problems if current MAC is different :( So we try to fix it here
		if (saddr != NULL) {
			getLocalMac(saddr);
		}
		// Valid Addresses. FIXME: MAC only valid after successful attempt to Create/Connect/Join a Group? (ie. adhocctlCurrentMode != ADHOCCTL_MODE_NONE)
		if ((adhocctlCurrentMode != ADHOCCTL_MODE_NONE) && saddr != NULL && isLocalMAC(saddr) && daddr != NULL && !isBroadcastMAC(daddr) && !isZeroMAC(daddr)) {
			// Dissidia 012 will try to reOpen the port without Closing the old one first when PtpConnect failed to try again.
			if (isPTPPortInUse(sport, false, daddr, dport)) {
				// FIXME: When PORT_IN_USE error occured it seems the index to the socket id also increased, which means it tries to create & bind the socket first and then closes it due to failed to bind
				return hleLogDebug(SCENET, ERROR_NET_ADHOC_PORT_IN_USE, "port in use");
			}

			// Random Port required
			if (sport == 0) {
				isClient = true;
				//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
				sport = -static_cast<int>(portOffset);
			}
			
			// Valid Arguments
			if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0) {
				// Create Infrastructure Socket
				int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

				// Valid Socket produced
				if (tcpsocket > 0) {
					// Change socket MSS
					setSockMSS(tcpsocket, PSP_ADHOC_PTP_MSS);

					// Change socket buffer size to be consistent on all platforms.
					setSockBufferSize(tcpsocket, SO_SNDBUF, bufsize*5); //PSP_ADHOC_PTP_MSS
					setSockBufferSize(tcpsocket, SO_RCVBUF, bufsize*10); //PSP_ADHOC_PTP_MSS*10

					// Enable KeepAlive
					setSockKeepAlive(tcpsocket, true, rexmt_int / 1000000L, rexmt_cnt);

					// Ignore SIGPIPE when supported (ie. BSD/MacOS)
					setSockNoSIGPIPE(tcpsocket, 1);

					// Enable Port Re-use
					setSockReuseAddrPort(tcpsocket);

					// Apply Default Send Timeout Settings to Socket
					setSockTimeout(tcpsocket, SO_SNDTIMEO, rexmt_int);

					// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
					setSockNoDelay(tcpsocket, 1);

					// Binding Information for local Port
					struct sockaddr_in addr {};
					// addr.sin_len = sizeof(addr);
					addr.sin_family = AF_INET;
					addr.sin_addr.s_addr = INADDR_ANY;
					if (isLocalServer) {
						getLocalIp(&addr);
					}
					uint16_t requestedport = static_cast<int>(sport + static_cast<int>(portOffset));
					// Avoid getting random port due to port offset when original port wasn't 0 (ie. original_port + port_offset = 65536 = 0)
					if (requestedport == 0 && sport > 0)
						requestedport = 65535; // Hopefully it will be safe to default it to 65535 since there can't be more than one port that can bumped into 65536
					// Show a warning about privileged ports
					if (requestedport != 0 && requestedport < 1024) {
						WARN_LOG(SCENET, "sceNetAdhocPtpOpen - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
					}
					addr.sin_port = htons(requestedport);

					// Bound Socket to local Port
					if (bind(tcpsocket, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
						// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
						socklen_t len = sizeof(addr);
						if (getsockname(tcpsocket, (struct sockaddr*)&addr, &len) == 0) {
							uint16_t boundport = ntohs(addr.sin_port);
							if (sport + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
								WARN_LOG(SCENET, "sceNetAdhocPtpOpen - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", sport, requestedport, boundport, boundport - portOffset);
							sport = boundport - portOffset;
						}

						// Allocate Memory
						AdhocSocket* internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));

						// Allocated Memory
						if (internal != NULL) {
							// Find Free Translator ID
							// FIXME: We should probably use an increasing index instead of looking for an empty slot from beginning if we want to simulate a real socket id
							int i = 0;
							for (; i < MAX_SOCKET; i++) if (adhocSockets[i] == NULL) break;

							// Found Free Translator ID
							if (i < MAX_SOCKET) {
								// Clear Memory
								memset(internal, 0, sizeof(AdhocSocket));

								// Socket Type
								internal->type = SOCK_PTP;
								internal->retry_interval = rexmt_int;
								internal->retry_count = rexmt_cnt;
								internal->nonblocking = flag;
								internal->buffer_size = bufsize;
								internal->isClient = isClient;

								// Copy Infrastructure Socket ID
								internal->data.ptp.id = tcpsocket;

								// Copy Address & Port Information
								internal->data.ptp.laddr = *saddr;
								internal->data.ptp.paddr = *daddr;
								internal->data.ptp.lport = sport;
								internal->data.ptp.pport = dport;

								// Link PTP Socket
								adhocSockets[i] = internal;

								// Add Port Forward to Router. We may not even need to forward this local port, since PtpOpen usually have port 0 (any port) as source port and followed by PtpConnect (which mean acting as Client), right?
								//sceNetPortOpen("TCP", sport);
								if (!isClient)
									UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset); 

								// Switch to non-blocking for futher usage
								changeBlockingMode(tcpsocket, 1);

								// Initiate PtpConnect (ie. The Warrior seems to try to PtpSend right after PtpOpen without trying to PtpConnect first)
								// TODO: Need to handle ECONNREFUSED better on non-Windows, if there are games that never called PtpConnect and only relies on [blocking?] PtpOpen to get connected
								NetAdhocPtp_Connect(i + 1, rexmt_int, 1, false);

								// Workaround to give some time to get connected before returning from PtpOpen over high latency
								if (g_Config.bForcedFirstConnect && internal->attemptCount == 1)
									hleDelayResult(i + 1, "delayed ptpopen", rexmt_int);

								// Return PTP Socket id
								INFO_LOG(SCENET, "sceNetAdhocPtpOpen - PSP Socket id: %i, Host Socket id: %i", i + 1, tcpsocket);
								return i + 1;
							}

							// Free Memory
							free(internal);
						}
					}
					else {
						ERROR_LOG(SCENET, "Socket error (%i) when binding port %u", errno, ntohs(addr.sin_port));
						auto n = GetI18NCategory(I18NCat::NETWORKING);
						g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")));
					}

					// Close Socket
					closesocket(tcpsocket);

					// Port not available (exclusively in use?)
					return hleLogDebug(SCENET, ERROR_NET_ADHOC_PORT_NOT_AVAIL, "port not available"); // ERROR_NET_ADHOC_PORT_IN_USE; // ERROR_NET_ADHOC_INVALID_PORT;
				}
			}

			// Invalid Arguments
			return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
		}
		
		// Invalid Addresses
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ADDR, "invalid address"); // ERROR_NET_ADHOC_INVALID_ARG;
	}
	
	// Library is uninitialized
	return hleLogDebug(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "adhoc not initialized");
}

// On a POSIX accept, returned socket may inherits properties from the listening socket, does PtpAccept also have similar behavior?
int AcceptPtpSocket(int ptpId, int newsocket, sockaddr_in& peeraddr, SceNetEtherAddr* addr, u16_le* port) {
	// Cast Socket
	auto socket = adhocSockets[ptpId - 1];
	auto& ptpsocket = socket->data.ptp;

	// Ignore SIGPIPE when supported (ie. BSD/MacOS)
	setSockNoSIGPIPE(newsocket, 1);

	// Enable Port Re-use
	setSockReuseAddrPort(newsocket);

	// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
	setSockNoDelay(newsocket, 1);

	// Local Address Information
	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	socklen_t locallen = sizeof(local);

	// Grab Local Address
	if (getsockname(newsocket, (struct sockaddr*)&local, &locallen) == 0) {
		// Peer MAC
		SceNetEtherAddr mac;

		// Find Peer MAC
		if (resolveIP(peeraddr.sin_addr.s_addr, &mac)) {
			// Allocate Memory
			AdhocSocket* internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));

			// Allocated Memory
			if (internal != NULL) {
				// Find Free Translator ID
				// FIXME: We should probably use an increasing index instead of looking for an empty slot from beginning if we want to simulate a real socket id
				int i = 0;
				for (; i < MAX_SOCKET; i++) if (adhocSockets[i] == NULL) break;

				// Found Free Translator ID
				if (i < MAX_SOCKET) {
					// Clear Memory
					memset(internal, 0, sizeof(AdhocSocket));

					// Inherits some of Listening socket's properties
					// Socket Type
					internal->type = SOCK_PTP;
					internal->nonblocking = socket->nonblocking; 
					internal->attemptCount = 1; // Used to differentiate between closed state of disconnected socket and not connected yet.
					internal->retry_interval = socket->retry_interval;
					internal->retry_count = socket->retry_count;
					internal->isClient = true;

					// Enable KeepAlive
					setSockKeepAlive(newsocket, true, internal->retry_interval / 1000000L, internal->retry_count);

					// Copy Socket Descriptor to Structure
					internal->data.ptp.id = newsocket;

					// Change socket MSS
					setSockMSS(newsocket, PSP_ADHOC_PTP_MSS);

					// Set Default Buffer Size or inherit the size?
					internal->buffer_size = socket->buffer_size;
					setSockBufferSize(newsocket, SO_SNDBUF, internal->buffer_size*5); //PSP_ADHOC_PTP_MSS
					setSockBufferSize(newsocket, SO_RCVBUF, internal->buffer_size*10); //PSP_ADHOC_PTP_MSS*10

					// Copy Local Address Data to Structure
					getLocalMac(&internal->data.ptp.laddr);
					internal->data.ptp.lport = ntohs(local.sin_port) - portOffset;

					// Copy Peer Address Data to Structure
					internal->data.ptp.paddr = mac;
					internal->data.ptp.pport = ntohs(peeraddr.sin_port) - portOffset;

					// Set Connection State
					internal->data.ptp.state = ADHOC_PTP_STATE_ESTABLISHED;

					// Return Peer Address & Port Information
					if (addr != NULL) 
						*addr = internal->data.ptp.paddr;
					if (port != NULL) 
						*port = internal->data.ptp.pport;

					// Link PTP Socket
					adhocSockets[i] = internal;

					// Add Port Forward to Router. Or may be doesn't need to be forwarded since local port already accessible from outside if others were able to connect & get accepted at this point, right?
					//sceNetPortOpen("TCP", internal->lport);
					//g_PortManager.Add(IP_PROTOCOL_TCP, internal->lport + portOffset);

					// Switch to non-blocking for futher usage
					changeBlockingMode(newsocket, 1);

					INFO_LOG(SCENET, "sceNetAdhocPtpAccept[%i->%i(%i):%u]: Established (%s:%u) - state: %d", ptpId, i + 1, newsocket, internal->data.ptp.lport, ip2str(peeraddr.sin_addr).c_str(), internal->data.ptp.pport, internal->data.ptp.state);

					// Return Socket
					return i + 1;
				}

				// Free Memory
				free(internal);
			}
		}
	}

	// Close Socket
	closesocket(newsocket);

	ERROR_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Failed (Socket Closed)", ptpId);
	return -1;
}

/**
 * Adhoc Emulator PTP Connection Acceptor
 * @param id Socket File Descriptor
 * @param addr OUT: Peer MAC Address
 * @param port OUT: Peer Port
 * @param timeout Accept Timeout (in Microseconds)
 * @param flag Nonblocking Flag
 * @return Socket ID >= 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_SOCKET_ID_NOT_AVAIL, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_NOT_LISTENED, ADHOC_THREAD_ABORTED, NET_INTERNAL
 */
static int sceNetAdhocPtpAccept(int id, u32 peerMacAddrPtr, u32 peerPortPtr, int timeout, int flag) {

	SceNetEtherAddr * addr = NULL;
	if (Memory::IsValidAddress(peerMacAddrPtr)) {
		addr = PSPPointer<SceNetEtherAddr>::Create(peerMacAddrPtr);
	}
	uint16_t * port = NULL; //
	if (Memory::IsValidAddress(peerPortPtr)) {
		port = (uint16_t *)Memory::GetPointer(peerPortPtr);
	}
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(SCENET, "sceNetAdhocPtpAccept(%d, [%08x]=%s, [%08x]=%u, %d, %u) at %08x", id, peerMacAddrPtr, mac2str(addr).c_str(), peerPortPtr, port ? *port : -1, timeout, flag, currentMIPS->pc);
	} else {
		VERBOSE_LOG(SCENET, "sceNetAdhocPtpAccept(%d, [%08x]=%s, [%08x]=%u, %d, %u) at %08x", id, peerMacAddrPtr, mac2str(addr).c_str(), peerPortPtr, port ? *port : -1, timeout, flag, currentMIPS->pc);
	}
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	// Library is initialized
	if (netAdhocInited) {
		// TODO: Validate Arguments. GTA:VCS seems to use 0/null for the peerPortPtr, and Bomberman Panic Bomber is using null/0 on both peerMacAddrPtr & peerPortPtr, so i guess it's optional.
		if (true) { // FIXME: Not sure what kind of arguments considered as invalid (need to be tested on a homebrew), might be the flag?
			// Valid Socket
			if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
				// Cast Socket
				auto socket = adhocSockets[id - 1];
				auto& ptpsocket = socket->data.ptp;
				socket->nonblocking = flag;

				if (socket->flags & ADHOC_F_ALERTACCEPT) {
					socket->alerted_flags |= ADHOC_F_ALERTACCEPT;

					return hleLogError(SCENET, ERROR_NET_ADHOC_SOCKET_ALERTED, "socket alerted");
				}

				// Listener Socket
				if (ptpsocket.state == ADHOC_PTP_STATE_LISTEN) {
					hleEatMicro(50);
					// Address Information
					struct sockaddr_in peeraddr;
					memset(&peeraddr, 0, sizeof(peeraddr));
					socklen_t peeraddrlen = sizeof(peeraddr);
					int error;

					// Check if listening socket is ready to accept
					int newsocket = IsSocketReady(ptpsocket.id, true, false, &error);
					if (newsocket > 0) {
						// Accept Connection
						newsocket = accept(ptpsocket.id, (struct sockaddr*)&peeraddr, &peeraddrlen);
						error = errno;
					}

					if (newsocket == 0 || (newsocket == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK))) {
						if (flag == 0) {
							// Simulate blocking behaviour with non-blocking socket
							u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
							return WaitBlockingAdhocSocket(threadSocketId, PTP_ACCEPT, id, nullptr, nullptr, timeout, addr, port, "ptp accept");
						}
						// Prevent spamming Debug Log with retries of non-bocking socket
						else {
							VERBOSE_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", id, error);
						}
					}

					// Accepted New Connection
					if (newsocket > 0) {
						int newid = AcceptPtpSocket(id, newsocket, peeraddr, addr, port);
						if (newid >= 0)
							return newid;
					}

					// Action would block
					if (flag)
						return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block");

					// Timeout
					return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_TIMEOUT, "timeout");
				}

				// Client Socket
				return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_NOT_LISTENED, "not listened");
			}

			// Invalid Socket
			return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
		}

		// Invalid Arguments
		return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
	}
	
	// Library is uninitialized
	return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized");
}

int NetAdhocPtp_Connect(int id, int timeout, int flag, bool allowForcedConnect) {
	// Library is initialized
	if (netAdhocInited)
	{
		// Valid Socket
		if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
			// Cast Socket
			auto socket = adhocSockets[id - 1];
			auto& ptpsocket = socket->data.ptp;
			socket->nonblocking = flag;

			if (socket->flags & ADHOC_F_ALERTCONNECT) {
				socket->alerted_flags |= ADHOC_F_ALERTCONNECT;

				return hleLogError(SCENET, ERROR_NET_ADHOC_SOCKET_ALERTED, "socket alerted");
			}

			// Phantasy Star Portable 2 will try to reconnect even when previous connect already success, so we should return success too if it's already connected
			if (ptpsocket.state == ADHOC_PTP_STATE_ESTABLISHED)
				return 0;

			// Valid Client Socket
			if (ptpsocket.state == ADHOC_PTP_STATE_CLOSED || ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT) {
				hleEatMicro(50);
				// Target Address
				struct sockaddr_in sin;
				memset(&sin, 0, sizeof(sin));

				// Setup Target Address
				// sin.sin_len = sizeof(sin);
				sin.sin_family = AF_INET;
				sin.sin_port = htons(ptpsocket.pport + portOffset);
				u16 finalPortOffset;

				// Grab Peer IP
				if (resolveMAC(&ptpsocket.paddr, (uint32_t*)&sin.sin_addr.s_addr, &finalPortOffset)) {
					// Some games (ie. PSP2) might try to talk to it's self, not sure if they talked through WAN or LAN when using public Adhoc Server tho
					sin.sin_port = htons(ptpsocket.pport + finalPortOffset);

					// Connect Socket to Peer
					// NOTE: Based on what i read at stackoverflow, The First Non-blocking POSIX connect will always returns EAGAIN/EWOULDBLOCK because it returns without waiting for ACK/handshake, But GvG Next Plus is treating non-blocking PtpConnect just like blocking connect, May be on a real PSP the first non-blocking sceNetAdhocPtpConnect can be successfull?
					int connectresult = connect(ptpsocket.id, (struct sockaddr*)&sin, sizeof(sin));

					// Grab Error Code
					int errorcode = errno;

					if (connectresult == SOCKET_ERROR) {
						if (errorcode == EAGAIN || errorcode == EWOULDBLOCK || errorcode == EALREADY || errorcode == EISCONN)
							DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i]: Socket Error (%i) to %s:%u", id, errorcode, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);
						else
							ERROR_LOG(SCENET, "sceNetAdhocPtpConnect[%i]: Socket Error (%i) to %s:%u", id, errorcode, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);
					}

					// Instant Connection (Lucky!)
					if (connectresult != SOCKET_ERROR || errorcode == EISCONN) {
						socket->attemptCount++;
						socket->lastAttempt = CoreTiming::GetGlobalTimeUsScaled();
						socket->internalLastAttempt = socket->lastAttempt;
						// Set Connected State
						ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;

						INFO_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Already Connected to %s:%u", id, ptpsocket.lport, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);
						// Success
						return 0;
					}

					// Error handling
					else if (connectresult == SOCKET_ERROR) {
						// Connection in Progress, or
						// ECONNREFUSED = No connection could be made because the target device actively refused it (on Windows/Linux/Android), or no one listening on the remote address (on Linux/Android) thus should try to connect again later (treated similarly to ETIMEDOUT/ENETUNREACH).
						if (connectInProgress(errorcode) || errorcode == ECONNREFUSED) {
							if (connectInProgress(errorcode))
							{
								ptpsocket.state = ADHOC_PTP_STATE_SYN_SENT;
							}
							// On Windows you can call connect again using the same socket after ECONNREFUSED/ETIMEDOUT/ENETUNREACH error, but on non-Windows you'll need to recreate the socket first
							else {
								DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Recreating Socket %i, errno = %i, state = %i, attempt = %i", id, ptpsocket.lport, ptpsocket.id, errorcode, ptpsocket.state, socket->attemptCount);
								if (RecreatePtpSocket(id) < 0) {
									WARN_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Failed to Recreate Socket", id, ptpsocket.lport);
								}
								ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
							}
							socket->attemptCount++;
							socket->lastAttempt = CoreTiming::GetGlobalTimeUsScaled();
							socket->internalLastAttempt = socket->lastAttempt;
							// Blocking Mode
							// Workaround: Forcing first attempt to be blocking to prevent issue related to lobby or high latency networks. (can be useful for GvG Next Plus, Dissidia 012, and Fate Unlimited Codes)
							if (!flag || (allowForcedConnect && g_Config.bForcedFirstConnect && socket->attemptCount <= 1)) {
								// Simulate blocking behaviour with non-blocking socket
								u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
								if (sendTargetPeers.find(threadSocketId) != sendTargetPeers.end()) {
									DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Socket(%d) is Busy!", id, ptpsocket.lport, ptpsocket.id);
									return hleLogError(SCENET, ERROR_NET_ADHOC_BUSY, "busy?");
								}

								AdhocSendTargets dest = { 0, {}, false };
								dest.peers.push_back({ sin.sin_addr.s_addr, ptpsocket.pport, finalPortOffset });
								sendTargetPeers[threadSocketId] = dest;
								return WaitBlockingAdhocSocket(threadSocketId, PTP_CONNECT, id, nullptr, nullptr, (flag) ? std::max((int)socket->retry_interval, timeout) : timeout, nullptr, nullptr, "ptp connect");
							}
							// NonBlocking Mode
							else {
								// Returning WOULD_BLOCK as Workaround for ERROR_NET_ADHOC_CONNECTION_REFUSED to be more cross-platform, since there is no way to simulate ERROR_NET_ADHOC_CONNECTION_REFUSED properly on Windows
								return hleLogDebug(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block");
							}
						}
					}
				}

				// Peer not found
				return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ADDR, "invalid address"); // ERROR_NET_ADHOC_WOULD_BLOCK / ERROR_NET_ADHOC_TIMEOUT
			}

			// Not a valid Client Socket
			return hleLogDebug(SCENET, ERROR_NET_ADHOC_NOT_OPENED, "not opened");
		}

		// Invalid Socket
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
	}

	// Library is uninitialized
	return hleLogDebug(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized");
}

/**
 * Adhoc Emulator PTP Connection Opener
 * @param id Socket File Descriptor
 * @param timeout Connect Timeout (in Microseconds)
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_CONNECTION_REFUSED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_NOT_OPENED, ADHOC_THREAD_ABORTED, NET_INTERNAL
 */
static int sceNetAdhocPtpConnect(int id, int timeout, int flag) {
	INFO_LOG(SCENET, "sceNetAdhocPtpConnect(%i, %i, %i) at %08x", id, timeout, flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	return NetAdhocPtp_Connect(id, timeout, flag);
}

int NetAdhocPtp_Close(int id, int unknown) {
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (id > 0 && id <= MAX_SOCKET) {
			// Cast Socket
			auto socket = adhocSockets[id - 1];

			// Valid Socket
			if (socket != NULL && socket->type == SOCK_PTP) {
				// Close Connection
				struct linger sl {};
				sl.l_onoff = 1;		// non-zero value enables linger option in kernel
				sl.l_linger = 0;	// timeout interval in seconds
				setsockopt(socket->data.ptp.id, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
				shutdown(socket->data.ptp.id, SD_BOTH);
				closesocket(socket->data.ptp.id);

				// Remove Port Forward from Router
				//sceNetPortClose("TCP", socket->lport);
				//g_PortManager.Remove(IP_PROTOCOL_TCP, isOriPort ? socket->lport : socket->lport + portOffset); // Let's not remove mapping in real-time as it could cause lags/disconnection when joining a room with slow routers

				// Free Memory
				free(socket);

				// Free Reference
				adhocSockets[id - 1] = NULL;

				// Success
				return 0;
			}

			return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
		}

		// Invalid Argument
		return ERROR_NET_ADHOC_INVALID_ARG;
	}

	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

/**
 * Adhoc Emulator PTP Socket Closer
 * @param id Socket File Descriptor
 * @param flag Bitflags (Unused)
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED
 */
static int sceNetAdhocPtpClose(int id, int unknown) {
	INFO_LOG(SCENET,"sceNetAdhocPtpClose(%d,%d) at %08x",id,unknown,currentMIPS->pc);
	/*if (!g_Config.bEnableWlan) {
		return 0;
	}*/
	
	return NetAdhocPtp_Close(id, unknown);
}


/**
 * Adhoc Emulator PTP Passive Socket Creator
 * @param saddr Local MAC (Unused)
 * @param sport Local Binding Port
 * @param bufsize Socket Buffer Size
 * @param rexmt_int Retransmit Interval (in Microseconds)
 * @param rexmt_cnt Retransmit Count
 * @param backlog Size of Connection Queue
 * @param flag Bitflags (Unused)
 * @return Socket ID > 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_ADDR, ADHOC_INVALID_PORT, ADHOC_SOCKET_ID_NOT_AVAIL, ADHOC_PORT_NOT_AVAIL, ADHOC_PORT_IN_USE, NET_NO_SPACE
 */
static int sceNetAdhocPtpListen(const char *srcmac, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, int flag) {
	INFO_LOG(SCENET, "sceNetAdhocPtpListen(%s, %d, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport,bufsize,rexmt_int,rexmt_cnt,backlog,flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)srcmac;
	bool isClient = false;
	if (netAdhocInited) {
		// Some games (ie. DBZ Shin Budokai 2) might be getting the saddr/srcmac content from SaveState and causing problems :( So we try to fix it here
		if (saddr != NULL) {
			getLocalMac(saddr);
		}
		// Valid Address. FIXME: MAC only valid after successful attempt to Create/Connect/Join a Group? (ie. adhocctlCurrentMode != ADHOCCTL_MODE_NONE)
		if ((adhocctlCurrentMode != ADHOCCTL_MODE_NONE) && saddr != NULL && isLocalMAC(saddr)) {
			// It's allowed to Listen and Open the same PTP port, But it's not allowed to Listen or Open the same PTP port twice.
			if (isPTPPortInUse(sport, true)) {
				// FIXME: When PORT_IN_USE error occured it seems the index to the socket id also increased, which means it tries to create & bind the socket first and then closes it due to failed to bind
				return hleLogDebug(SCENET, ERROR_NET_ADHOC_PORT_IN_USE, "port in use");
			}

			// Random Port required
			if (sport == 0) {
				isClient = true;
				//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
				sport = -static_cast<int>(portOffset);
			}
			
			// Valid Arguments
			if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0 && backlog > 0)
			{
				// Create Infrastructure Socket
				int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

				// Valid Socket produced
				if (tcpsocket > 0) {
					// Change socket MSS
					setSockMSS(tcpsocket, PSP_ADHOC_PTP_MSS);

					// Change socket buffer size to be consistent on all platforms.
					setSockBufferSize(tcpsocket, SO_SNDBUF, bufsize*5); //PSP_ADHOC_PTP_MSS
					setSockBufferSize(tcpsocket, SO_RCVBUF, bufsize*10); //PSP_ADHOC_PTP_MSS*10

					// Enable KeepAlive
					setSockKeepAlive(tcpsocket, true, rexmt_int / 1000000L, rexmt_cnt);

					// Ignore SIGPIPE when supported (ie. BSD/MacOS)
					setSockNoSIGPIPE(tcpsocket, 1);

					// Enable Port Re-use
					setSockReuseAddrPort(tcpsocket);

					// Apply Default Receive Timeout Settings to Socket
					setSockTimeout(tcpsocket, SO_RCVTIMEO, rexmt_int);

					// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
					setSockNoDelay(tcpsocket, 1);

					// Binding Information for local Port
					struct sockaddr_in addr {};
					addr.sin_family = AF_INET;
					addr.sin_addr.s_addr = INADDR_ANY;
					if (isLocalServer) {
						getLocalIp(&addr);
					}
					uint16_t requestedport = static_cast<int>(sport + static_cast<int>(portOffset));
					// Avoid getting random port due to port offset when original port wasn't 0 (ie. original_port + port_offset = 65536 = 0)
					if (requestedport == 0 && sport > 0)
						requestedport = 65535; // Hopefully it will be safe to default it to 65535 since there can't be more than one port that can bumped into 65536
					// Show a warning about privileged ports
					if (requestedport != 0 && requestedport < 1024) {
						WARN_LOG(SCENET, "sceNetAdhocPtpListen - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
					}
					addr.sin_port = htons(requestedport);

					int iResult = 0;
					// Bound Socket to local Port
					if ((iResult = bind(tcpsocket, (struct sockaddr*)&addr, sizeof(addr))) == 0) {
						// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
						socklen_t len = sizeof(addr);
						if (getsockname(tcpsocket, (struct sockaddr*)&addr, &len) == 0) {
							uint16_t boundport = ntohs(addr.sin_port);
							if (sport + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
								WARN_LOG(SCENET, "sceNetAdhocPtpListen - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", sport, requestedport, boundport, boundport - portOffset);
							sport = boundport - portOffset;
						}
						// Switch into Listening Mode
						if ((iResult = listen(tcpsocket, backlog)) == 0) {
							// Allocate Memory
							AdhocSocket* internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));

							// Allocated Memory
							if (internal != NULL) {
								// Find Free Translator ID
								// FIXME: We should probably use an increasing index instead of looking for an empty slot from beginning if we want to simulate a real socket id
								int i = 0;
								for (; i < MAX_SOCKET; i++) if (adhocSockets[i] == NULL) break;

								// Found Free Translator ID
								if (i < MAX_SOCKET) {
									// Clear Memory
									memset(internal, 0, sizeof(AdhocSocket));

									// Socket Type
									internal->type = SOCK_PTP;
									internal->retry_interval = rexmt_int;
									internal->retry_count = rexmt_cnt;
									internal->nonblocking = flag;
									internal->buffer_size = bufsize;
									internal->isClient = isClient;

									// Copy Infrastructure Socket ID
									internal->data.ptp.id = tcpsocket;

									// Copy Address & Port Information
									internal->data.ptp.laddr = *saddr;
									internal->data.ptp.lport = sport;

									// Flag Socket as Listener
									internal->data.ptp.state = ADHOC_PTP_STATE_LISTEN;

									// Link PTP Socket
									adhocSockets[i] = internal;

									// Add Port Forward to Router
									//sceNetPortOpen("TCP", sport);
									UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset);

									// Switch to non-blocking for futher usage
									changeBlockingMode(tcpsocket, 1);

									// Return PTP Socket id
									INFO_LOG(SCENET, "sceNetAdhocPtpListen - PSP Socket id: %i, Host Socket id: %i", i + 1, tcpsocket);
									return i + 1;
								}

								// Free Memory
								free(internal);
							}
						}
					}
					else {
						auto n = GetI18NCategory(I18NCat::NETWORKING);
						g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")));
					}

					if (iResult == SOCKET_ERROR) {
						int error = errno;
						ERROR_LOG(SCENET, "sceNetAdhocPtpListen[%i]: Socket Error (%i)", sport, error);
					}

					// Close Socket
					closesocket(tcpsocket);

					// Port not available (exclusively in use?)
					return hleLogDebug(SCENET, ERROR_NET_ADHOC_PORT_NOT_AVAIL, "port not available"); //ERROR_NET_ADHOC_PORT_IN_USE; // ERROR_NET_ADHOC_INVALID_PORT;
				}

				// Socket not available
				return hleLogDebug(SCENET, ERROR_NET_ADHOC_SOCKET_ID_NOT_AVAIL, "socket id not available");
			}

			// Invalid Arguments
			return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
		}
		
		// Invalid Addresses
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ADDR, "invalid address");
	}
	
	// Library is uninitialized
	return hleLogDebug(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "adhoc not initialized");
}

/**
 * Adhoc Emulator PTP Sender
 * @param id Socket File Descriptor
 * @param data Data Payload
 * @param len IN: Length of Payload OUT: Sent Data (in Bytes)
 * @param timeout Send Timeout (in Microseconds)
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_NOT_CONNECTED, ADHOC_THREAD_ABORTED, ADHOC_INVALID_DATALEN, ADHOC_DISCONNECTED, NET_INTERNAL, NET_NO_SPACE
 */
static int sceNetAdhocPtpSend(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPtpSend(%d,%08x,%08x,%d,%d) at %08x", id, dataAddr, dataSizeAddr, timeout, flag, currentMIPS->pc);

	int * len = (int *)Memory::GetPointer(dataSizeAddr);
	const char * data = Memory::GetCharPointer(dataAddr);
	// Library is initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
			// Cast Socket
			auto socket = adhocSockets[id - 1];
			auto& ptpsocket = socket->data.ptp;
			socket->nonblocking = flag;
			
			// Connected Socket
			if (ptpsocket.state == ADHOC_PTP_STATE_ESTABLISHED || ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT) {
				// Valid Arguments
				if (data != NULL && len != NULL && *len > 0) {
					// Schedule Timeout Removal
					//if (flag) timeout = 0; // JPCSP seems to always Send PTP as blocking, also a possibility to send to multiple destination?
					
					// Apply Send Timeout Settings to Socket
					if (timeout > 0) 
						setSockTimeout(ptpsocket.id, SO_SNDTIMEO, timeout);

					if (socket->flags & ADHOC_F_ALERTSEND) {
						socket->alerted_flags |= ADHOC_F_ALERTSEND;

						return hleLogError(SCENET, ERROR_NET_ADHOC_SOCKET_ALERTED, "socket alerted");
					}
					
					// Acquire Network Lock
					// _acquireNetworkLock();
					
					// Send Data
					int sent = send(ptpsocket.id, data, *len, MSG_NOSIGNAL);
					int error = errno;
					
					// Free Network Lock
					// _freeNetworkLock();
					
					// Success
					if (sent > 0) {
						hleEatMicro(50); // mostly 1ms, sometimes 1~10ms ? doesn't seems to be switching to a different thread during this duration
						// Save Length
						*len = sent;

						DEBUG_LOG(SCENET, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u\n", id, ptpsocket.lport, sent, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);
						
						// Set to Established on successful Send when an attempt to Connect was initiated
						if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
							ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;

						// Return Success
						return 0;
					}
					
					// Non-Critical Error
					else if (sent == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (error == ENOTCONN || connectInProgress(error))))) {
						// Non-Blocking
						if (flag) 
							return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block");
						
						// Simulate blocking behaviour with non-blocking socket
						u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
						return WaitBlockingAdhocSocket(threadSocketId, PTP_SEND, id, (void*)data, len, timeout, nullptr, nullptr, "ptp send");
					}

					DEBUG_LOG(SCENET, "sceNetAdhocPtpSend[%i:%u -> %s:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport, sent, error);
					
					// Change Socket State
					ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
					
					// Disconnected
					return hleLogError(SCENET, ERROR_NET_ADHOC_DISCONNECTED, "disconnected");
				}
				
				// Invalid Arguments
				return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");
			}
			
			// Not Connected
			return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_CONNECTED, "not connected");
		}
		
		// Invalid Socket
		return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
	}
	
	// Library is uninitialized
	return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized");
}

/**
 * Adhoc Emulator PTP Receiver
 * @param id Socket File Descriptor
 * @param buf Data Buffer
 * @param len IN: Buffersize OUT: Received Data (in Bytes)
 * @param timeout Receive Timeout (in Microseconds)
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_THREAD_ABORTED, ADHOC_DISCONNECTED, NET_INTERNAL
 */
static int sceNetAdhocPtpRecv(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv(%d,%08x,%08x,%d,%d) at %08x", id, dataAddr, dataSizeAddr, timeout, flag, currentMIPS->pc);

	void * buf = (void *)Memory::GetPointer(dataAddr);
	int * len = (int *)Memory::GetPointer(dataSizeAddr);
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (buf != NULL && len != NULL && *len > 0) {
			// Valid Socket
			if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
				// Cast Socket
				auto socket = adhocSockets[id - 1];
				auto& ptpsocket = socket->data.ptp;
				socket->nonblocking = flag;

				if (ptpsocket.state == ADHOC_PTP_STATE_ESTABLISHED || ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT) {
					// Schedule Timeout Removal
					//if (flag) timeout = 0;

					// Apply Receive Timeout Settings to Socket. Let's not wait forever (0 = indefinitely)
					if (timeout > 0)
						setSockTimeout(ptpsocket.id, SO_RCVTIMEO, timeout);

					if (socket->flags & ADHOC_F_ALERTRECV) {
						socket->alerted_flags |= ADHOC_F_ALERTRECV;

						return hleLogError(SCENET, ERROR_NET_ADHOC_SOCKET_ALERTED, "socket alerted");
					}

					// Acquire Network Lock
					// _acquireNetworkLock();

					// TODO: Use a different thread (similar to sceIo) for recvfrom, recv & accept to prevent blocking-socket from blocking emulation
					int received = 0;
					int error = 0;

					// Receive Data. POSIX: May received 0 bytes when the remote peer already closed the connection.
					received = recv(ptpsocket.id, (char*)buf, std::max(0, *len), MSG_NOSIGNAL);
					error = errno;

					if (received == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (error == ENOTCONN || connectInProgress(error))))) {
						if (flag == 0) {
							// Simulate blocking behaviour with non-blocking socket
							u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
							return WaitBlockingAdhocSocket(threadSocketId, PTP_RECV, id, buf, len, timeout, nullptr, nullptr, "ptp recv");
						}

						return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block");
					}

					// Free Network Lock
					// _freeNetworkLock();

					hleEatMicro(50); 

					// Received Data
					if (received > 0) {
						// Save Length
						*len = received;

						// Update last recv timestamp, may cause disconnection not detected properly tho
						peerlock.lock();
						auto peer = findFriend(&ptpsocket.paddr);
						if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
						peerlock.unlock();

						DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %s:%u\n", id, ptpsocket.lport, received, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

						// Set to Established on successful Recv when an attempt to Connect was initiated
						if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
							ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;

						// Return Success
						return 0;
					}

					DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv[%i:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, received, error);

					if (*len == 0)
						return 0;

					// Change Socket State
					ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

					// Disconnected
					return hleLogError(SCENET, ERROR_NET_ADHOC_DISCONNECTED, "disconnected");
				}

				// Not Connected
				return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_CONNECTED, "not connected");
			}

			// Invalid Socket
			return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
		}

		// Invalid Arguments
		return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid socket arg");
	}
	
	// Library is uninitialized
	return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized");
}

int FlushPtpSocket(int socketId) {
	// Get original Nagle algo value
	int n = getSockNoDelay(socketId);

	// Disable Nagle Algo to send immediately
	setSockNoDelay(socketId, 1);

	// Send Empty Data just to trigger Nagle on/off effect to flush the send buffer, Do we need to trigger this at all or is it automatically flushed?
	//changeBlockingMode(socket->id, nonblock);
	int ret = send(socketId, nullptr, 0, MSG_NOSIGNAL);
	if (ret == SOCKET_ERROR) ret = errno;
	//changeBlockingMode(socket->id, 1);

	// Restore/Enable Nagle Algo
	setSockNoDelay(socketId, n);

	return ret;
}

/**
 * Adhoc Emulator PTP Flusher
 * @param id Socket File Descriptor
 * @param timeout Flush Timeout (in Microseconds)
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_THREAD_ABORTED, ADHOC_DISCONNECTED, ADHOC_NOT_CONNECTED, NET_INTERNAL
 */
static int sceNetAdhocPtpFlush(int id, int timeout, int nonblock) {
	DEBUG_LOG(SCENET,"sceNetAdhocPtpFlush(%d,%d,%d) at %08x", id, timeout, nonblock, currentMIPS->pc);

	// Library initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
			// Cast Socket
			auto socket = adhocSockets[id - 1];
			auto& ptpsocket = socket->data.ptp;
			socket->nonblocking = nonblock;

			if (socket->flags & ADHOC_F_ALERTFLUSH) {
				socket->alerted_flags |= ADHOC_F_ALERTFLUSH;

				return hleLogError(SCENET, ERROR_NET_ADHOC_SOCKET_ALERTED, "socket alerted");
			}

			// Connected Socket
			if (ptpsocket.state == ADHOC_PTP_STATE_ESTABLISHED) {
				hleEatMicro(50);
				// There are two ways to flush, you can either set TCP_NODELAY to 1 or TCP_CORK to 0.
				// Apply Send Timeout Settings to Socket
				setSockTimeout(ptpsocket.id, SO_SNDTIMEO, timeout);

				int error = FlushPtpSocket(ptpsocket.id);

				if (error == EAGAIN || error == EWOULDBLOCK) {
					// Non-Blocking
					if (nonblock)
						return hleLogSuccessVerboseX(SCENET, ERROR_NET_ADHOC_WOULD_BLOCK, "would block");

					// Simulate blocking behaviour with non-blocking socket
					u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
					return WaitBlockingAdhocSocket(threadSocketId, PTP_FLUSH, id, nullptr, nullptr, timeout, nullptr, nullptr, "ptp flush");
				}

				if (error != 0)
					DEBUG_LOG(SCENET, "sceNetAdhocPtpFlush[%i:%u -> %s:%u]: Error:%i", id, ptpsocket.lport, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport, error);
			}

			// Dummy Result, Always success?
			return 0;
		}
		
		// Invalid Socket
		return hleLogError(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");
	}
	// Library uninitialized
	return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_INITIALIZED, "not initialized");
}

/**
* Create own game object type data.
*
* @param dataAddr - A pointer to the game object data.
* @param size - Size of the game data.
*
* @return 0 on success, < 0 on error.
*/
static int sceNetAdhocGameModeCreateMaster(u32 dataAddr, int size) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocGameModeCreateMaster(%08x, %i) at %08x", dataAddr, size, currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_ENTER_GAMEMODE, "not enter gamemode");

	if (size < 0 || !Memory::IsValidAddress(dataAddr))
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_INVALID_ARG, "invalid arg");

	if (masterGameModeArea.data)
		return hleLogError(SCENET, ERROR_NET_ADHOC_ALREADY_CREATED, "already created"); // FIXME: Should we return a success instead? (need to test this on a homebrew)

	hleEatMicro(1000);
	SceNetEtherAddr localMac;
	getLocalMac(&localMac);
	gameModeBuffSize = std::max(gameModeBuffSize, size);
	u8* buf = (u8*)realloc(gameModeBuffer, gameModeBuffSize);
	if (buf)
		gameModeBuffer = buf;

	u8* data = (u8*)malloc(size);
	if (data) {
		Memory::Memcpy(data, dataAddr, size);
		masterGameModeArea = { 0, size, dataAddr, CoreTiming::GetGlobalTimeUsScaled(), 1, 0, localMac, data };
		StartGameModeScheduler();

		// Block current thread to sync initial master data after Master and all Replicas have been created
		if (replicaGameModeAreas.size() == (gameModeMacs.size() - 1)) {
			if (CoreTiming::IsScheduled(gameModeNotifyEvent)) {
				__KernelWaitCurThread(WAITTYPE_NET, GAMEMODE_WAITID, 0, 0, false, "syncing master data");
				DEBUG_LOG(SCENET, "GameMode: Blocking Thread %d to Sync initial Master data", __KernelGetCurThread());
			}
		}
		return hleLogDebug(SCENET, 0, "success"); // returned an id just like CreateReplica? always return 0?
	}
	
	return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_CREATED, "not created");
}

/**
* Create peer game object type data.
*
* @param mac - The mac address of the peer.
* @param dataAddr - A pointer to the game object data.
* @param size - Size of the game data.
*
* @return The id of the replica on success, < 0 on error.
*/
static int sceNetAdhocGameModeCreateReplica(const char *mac, u32 dataAddr, int size) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocGameModeCreateReplica(%s, %08x, %i) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), dataAddr, size, currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_ENTER_GAMEMODE, "not enter gamemode");

	if (mac == nullptr || size < 0 || !Memory::IsValidAddress(dataAddr))
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_INVALID_ARG, "invalid arg");

	hleEatMicro(1000);
	int maxid = 0;
	auto it = std::find_if(replicaGameModeAreas.begin(), replicaGameModeAreas.end(),
		[mac, &maxid](GameModeArea const& e) {
			if (e.id > maxid) maxid = e.id;
			return IsMatch(e.mac, mac);
		});
	// MAC address already existed!
	if (it != replicaGameModeAreas.end()) {
		WARN_LOG(SCENET, "sceNetAdhocGameModeCreateReplica - [%s] is already existed (id: %d)", mac2str((SceNetEtherAddr*)mac).c_str(), it->id);
		return it->id; // ERROR_NET_ADHOC_ALREADY_CREATED
	}

	int ret = 0;
	gameModeBuffSize = std::max(gameModeBuffSize, size);
	u8* buf = (u8*)realloc(gameModeBuffer, gameModeBuffSize);
	if (buf)
		gameModeBuffer = buf;

	u8* data = (u8*)malloc(size);
	if (data) {
		Memory::Memcpy(data, dataAddr, size);
		//int sock = sceNetAdhocPdpCreate(mac, ADHOC_GAMEMODE_PORT, size, 0);
		GameModeArea gma = { maxid + 1, size, dataAddr, CoreTiming::GetGlobalTimeUsScaled(), 0, 0, *(SceNetEtherAddr*)mac, data };
		replicaGameModeAreas.push_back(gma);
		ret = gma.id; // Valid id for replica is higher than 0?

		// Block current thread to sync initial master data after Master and all Replicas have been created
		if (masterGameModeArea.data != NULL && replicaGameModeAreas.size() == (gameModeMacs.size() - 1)) {
			if (CoreTiming::IsScheduled(gameModeNotifyEvent)) {
				__KernelWaitCurThread(WAITTYPE_NET, GAMEMODE_WAITID, ret, 0, false, "syncing master data");
				DEBUG_LOG(SCENET, "GameMode: Blocking Thread %d to Sync initial Master data", __KernelGetCurThread());
			}
		}
		return hleLogSuccessInfoI(SCENET, ret, "success");
	}

	return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_CREATED, "not created");
}

static int sceNetAdhocGameModeUpdateMaster() {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocGameModeUpdateMaster() at %08x", currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_ENTER_GAMEMODE, "not enter gamemode");

	if (masterGameModeArea.data) {
		Memory::Memcpy(masterGameModeArea.data, masterGameModeArea.addr, masterGameModeArea.size);
		masterGameModeArea.dataUpdated = 1;
		masterGameModeArea.updateTimestamp = CoreTiming::GetGlobalTimeUsScaled();
		// Reset sent marker
		for (auto& gma : replicaGameModeAreas)
			gma.dataSent = 0;
	}
	
	hleEatMicro(100);
	return 0;
}

int NetAdhocGameMode_DeleteMaster() {
	if (CoreTiming::IsScheduled(gameModeNotifyEvent)) {
		__KernelWaitCurThread(WAITTYPE_NET, GAMEMODE_WAITID, 0, 0, false, "deleting master data");
		DEBUG_LOG(SCENET, "GameMode: Blocking Thread %d to End GameMode Scheduler", __KernelGetCurThread());
	}

	if (masterGameModeArea.data) {
		free(masterGameModeArea.data);
		masterGameModeArea.data = nullptr;
	}
	//NetAdhocPdp_Delete(masterGameModeArea.socket, 0);
	gameModePeerPorts.erase(masterGameModeArea.mac);
	masterGameModeArea = { 0 };

	if (replicaGameModeAreas.size() <= 0) {
		NetAdhocPdp_Delete(gameModeSocket, 0);
		gameModeSocket = (int)INVALID_SOCKET;
	}

	return 0;
}

static int sceNetAdhocGameModeDeleteMaster() {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocGameModeDeleteMaster() at %08x", currentMIPS->pc);
	if (isZeroMAC(&masterGameModeArea.mac))
		return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_CREATED, "not created");

	return NetAdhocGameMode_DeleteMaster();
}

static int sceNetAdhocGameModeUpdateReplica(int id, u32 infoAddr) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocGameModeUpdateReplica(%i, %08x) at %08x", id, infoAddr, currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_ENTER_GAMEMODE, "not enter gamemode");

	auto it = std::find_if(replicaGameModeAreas.begin(), replicaGameModeAreas.end(),
		[id](GameModeArea const& e) {
			return e.id == id;
		});

	if (it == replicaGameModeAreas.end())
		return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_CREATED, "not created");

	// Bomberman Panic Bomber is using 0/null on infoAddr, so i guess it's optional.
	GameModeUpdateInfo* gmuinfo = NULL;
	if (Memory::IsValidAddress(infoAddr)) {
		gmuinfo = (GameModeUpdateInfo*)Memory::GetPointer(infoAddr);
	}

	for (auto& gma : replicaGameModeAreas) {
		if (gma.id == id) {
			if (gma.data && gma.dataUpdated) {
				Memory::Memcpy(gma.addr, gma.data, gma.size);
				gma.dataUpdated = 0;
				if (gmuinfo != NULL) {
					gmuinfo->length = sizeof(GameModeUpdateInfo);
					gmuinfo->updated = 1;
					gmuinfo->timeStamp = std::max(gma.updateTimestamp, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);				
				}
			}
			else {
				if (gmuinfo != NULL) {
					gmuinfo->updated = 0;
				}
			}
			break;
		}
	}

	hleEatMicro(100);
	return 0;
}

static int sceNetAdhocGameModeDeleteReplica(int id) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocGameModeDeleteReplica(%i) at %08x", id, currentMIPS->pc);
	auto it = std::find_if(replicaGameModeAreas.begin(), replicaGameModeAreas.end(),
		[id](GameModeArea const& e) {
			return e.id == id;
		});

	if (it == replicaGameModeAreas.end())
		return hleLogError(SCENET, ERROR_NET_ADHOC_NOT_CREATED, "not created");

	if (it->data) {
		free(it->data);
		it->data = nullptr;
	}
	//sceNetAdhocPdpDelete(it->socket, 0);
	gameModePeerPorts.erase(it->mac);
	replicaGameModeAreas.erase(it);

	if (replicaGameModeAreas.size() <= 0 && isZeroMAC(&masterGameModeArea.mac)) {
		//sceNetAdhocPdpDelete(gameModeSocket, 0);
		//gameModeSocket = (int)INVALID_SOCKET;
	}

	return 0;
}

int sceNetAdhocGetSocketAlert(int id, u32 flagPtr) {
	WARN_LOG_REPORT_ONCE(sceNetAdhocGetSocketAlert, SCENET, "UNTESTED sceNetAdhocGetSocketAlert(%i, %08x) at %08x", id, flagPtr, currentMIPS->pc);
	if (!Memory::IsValidAddress(flagPtr))
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_ARG, "invalid arg");

	if (id < 1 || id > MAX_SOCKET || adhocSockets[id - 1] == NULL)
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_INVALID_SOCKET_ID, "invalid socket id");

	s32_le flg = adhocSockets[id - 1]->flags;	
	Memory::Write_U32(flg, flagPtr);

	return hleLogDebug(SCENET, 0, "flags = %08x", flg);
}

int NetAdhocMatching_Stop(int matchingId) {
	SceNetAdhocMatchingContext* item = findMatchingContext(matchingId);

	if (item != NULL) {
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

	}

	return 0;
}

int sceNetAdhocMatchingStop(int matchingId) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingStop(%i) at %08x", matchingId, currentMIPS->pc);

	return NetAdhocMatching_Stop(matchingId);
}

int NetAdhocMatching_Delete(int matchingId) {
	// Previous Context Reference
	SceNetAdhocMatchingContext* prev = NULL;

	// Multithreading Lock
	peerlock.lock(); //contextlock.lock();

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

	// Multithreading Unlock
	peerlock.unlock(); //contextlock.unlock();

	return 0;
}

int sceNetAdhocMatchingDelete(int matchingId) {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?

	NetAdhocMatching_Delete(matchingId);

	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingDelete(%i) at %08x", matchingId, currentMIPS->pc);

	// Give a little time to make sure everything are cleaned up before the following AdhocMatchingCreate, Not too long tho, otherwise Naruto Ultimate Ninja Heroes 3 will have an issue
	//hleDelayResult(0, "give time to init/cleanup", adhocExtraPollDelayMS * 1000);
	return 0;
}

int sceNetAdhocMatchingInit(u32 memsize) {
	WARN_LOG_REPORT_ONCE(sceNetAdhocMatchingInit, SCENET, "sceNetAdhocMatchingInit(%d) at %08x", memsize, currentMIPS->pc);
	
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
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingTerm() at %08x", currentMIPS->pc);
	// Should we cleanup all created matching contexts first? just in case there are games that doesn't delete them before calling this
	NetAdhocMatching_Term();
	
	netAdhocMatchingInited = false;
	return 0;
}


// Presumably returns a "matchingId".
static int sceNetAdhocMatchingCreate(int mode, int maxnum, int port, int rxbuflen, int hello_int, int keepalive_int, int init_count, int rexmt_int, u32 callbackAddr) {
	WARN_LOG(SCENET, "sceNetAdhocMatchingCreate(mode=%i, maxnum=%i, port=%i, rxbuflen=%i, hello=%i, keepalive=%i, initcount=%i, rexmt=%i, callbackAddr=%08x) at %08x", mode, maxnum, port, rxbuflen, hello_int, keepalive_int, init_count, rexmt_int, callbackAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	
	SceNetAdhocMatchingHandler handler;
	handler.entryPoint = callbackAddr;

	// Library initialized
	if (netAdhocMatchingInited) {
		// Valid Member Limit
		if (maxnum > 1 && maxnum <= 16) {
			// Valid Receive Buffer size
			if (rxbuflen >= 1) { //1024 //200 on DBZ Shin Budokai 2
				// Valid Arguments
				if (mode >= 1 && mode <= 3) {

					// Iterate Matching Contexts
					SceNetAdhocMatchingContext * item = contexts; 
					for (; item != NULL; item = item->next) {
						// Port Match found
						if (item->port == port) 
							return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_PORT_IN_USE, "adhoc matching port in use");
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
							context->timeout += adhocDefaultTimeout; // For internet play we need higher timeout than what the game wanted
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
							return hleLogDebug(SCENET, context->id, "success");
						}

						// Free Memory
						free(context);
					}

					// Out of Memory
					return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NO_SPACE, "adhoc matching no space");
				}

				// InvalidERROR_NET_Arguments
				return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhoc matching invalid arg");
			}

			// Invalid Receive Buffer Size
			return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_RXBUF_TOO_SHORT, "adhoc matching rxbuf too short");
		}

		// Invalid Member Limit
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_MAXNUM, "adhoc matching invalid maxnum");
	}
	// Uninitialized Library
	return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhoc matching not initialized");
}

int NetAdhocMatching_Start(int matchingId, int evthPri, int evthPartitionId, int evthStack, int inthPri, int inthPartitionId, int inthStack, int optLen, u32 optDataAddr) {
	// Multithreading Lock
	peerlock.lock();

	SceNetAdhocMatchingContext* item = findMatchingContext(matchingId);

	if (item != NULL) {
		//sceNetAdhocMatchingSetHelloOpt(matchingId, optLen, optDataAddr); //SetHelloOpt only works when context is running
		if ((optLen > 0) && Memory::IsValidAddress(optDataAddr)) {
			// Allocate the memory and copy the content
			if (item->hello != NULL) free(item->hello);
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
		int sock = sceNetAdhocPdpCreate((const char*)&item->mac, static_cast<int>(item->port), item->rxbuflen, 0);
		item->socket = sock;
		if (sock < 1) {
			peerlock.unlock();
			return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_PORT_IN_USE, "adhoc matching port in use");
		}

		// Create & Start the Fake PSP Thread ("matching_ev%d" and "matching_io%d")
		netAdhocValidateLoopMemory();
		std::string thrname = std::string("MatchingThr") + std::to_string(matchingId);
		matchingThreads[item->matching_thid] = sceKernelCreateThread(thrname.c_str(), matchingThreadHackAddr, evthPri, evthStack, 0, 0);
		//item->matchingThread = new HLEHelperThread(thrname.c_str(), "sceNetAdhocMatching", "__NetMatchingCallbacks", inthPri, inthStack);
		if (matchingThreads[item->matching_thid] > 0) {
			sceKernelStartThread(matchingThreads[item->matching_thid], 0, 0); //sceKernelStartThread(context->event_thid, sizeof(context), &context);
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
	}
	//else return ERROR_NET_ADHOC_MATCHING_INVALID_ID; //Faking success to prevent GTA:VCS from stuck unable to choose host/join menu

	// Multithreading Unlock
	peerlock.unlock();

	return 0;
}

#define KERNEL_PARTITION_ID  1
#define USER_PARTITION_ID  2
#define VSHELL_PARTITION_ID  5
// This should be similar with sceNetAdhocMatchingStart2 but using USER_PARTITION_ID (2) for PartitionId params
static int sceNetAdhocMatchingStart(int matchingId, int evthPri, int evthStack, int inthPri, int inthStack, int optLen, u32 optDataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingStart(%i, %i, %i, %i, %i, %i, %08x) at %08x", matchingId, evthPri, evthStack, inthPri, inthStack, optLen, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	int retval = NetAdhocMatching_Start(matchingId, evthPri, USER_PARTITION_ID, evthStack, inthPri, USER_PARTITION_ID, inthStack, optLen, optDataAddr);
	// Give a little time to make sure matching Threads are ready before the game use the next sceNet functions, should've checked for status instead of guessing the time?
	hleEatMicro(adhocMatchingEventDelay);
	return retval;
}

// With params for Partition ID for the event & input handler stack
static int sceNetAdhocMatchingStart2(int matchingId, int evthPri, int evthPartitionId, int evthStack, int inthPri, int inthPartitionId, int inthStack, int optLen, u32 optDataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingStart2(%i, %i, %i, %i, %i, %i, %i, %i, %08x) at %08x", matchingId, evthPri, evthPartitionId, evthStack, inthPri, inthPartitionId, inthStack, optLen, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	int retval = NetAdhocMatching_Start(matchingId, evthPri, evthPartitionId, evthStack, inthPri, inthPartitionId, inthStack, optLen, optDataAddr);
	// Give a little time to make sure matching Threads are ready before the game use the next sceNet functions, should've checked for status instead of guessing the time?
	hleEatMicro(adhocMatchingEventDelay);
	return retval;
}


static int sceNetAdhocMatchingSelectTarget(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSelectTarget(%i, %s, %i, %08x) at %08x", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str(), optLen, optDataPtr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Initialized Library
	if (netAdhocMatchingInited)
	{
		// Valid Arguments
		if (macAddress != NULL)
		{
			SceNetEtherAddr * target = (SceNetEtherAddr *)macAddress;

			// Find Matching Context for ID
			SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);

			// Found Matching Context
			if (context != NULL)
			{
				// Running Context
				if (context->running)
				{
					// Search Result
					SceNetAdhocMatchingMemberInternal * peer = findPeer(context, (SceNetEtherAddr *)target);

					// Found Peer in List
					if (peer != NULL)
					{
						// Valid Optional Data Length
						if ((optLen == 0) || (optLen > 0 && optDataPtr != 0))
						{
							void * opt = NULL;
							if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointerWriteUnchecked(optDataPtr);
							// Host Mode
							if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT)
							{
								// Already Connected
								if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED, "adhocmatching already established");

								// Not enough space
								if (countChildren(context) == (context->maxpeers - 1)) return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_EXCEED_MAXNUM, "adhocmatching exceed maxnum");

								// Requesting Peer
								if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)
								{
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
									return 0;
								}
							}

							// Client Mode
							else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
							{
								// Already connected
								if (findParent(context) != NULL) return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED, "adhocmatching already established");

								// Outgoing Request in Progress
								if (findOutgoingRequest(context) != NULL) return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS, "adhocmatching request in progress");

								// Valid Offer
								if (peer->state == PSP_ADHOC_MATCHING_PEER_OFFER)
								{
									// Switch into Join Request Mode
									peer->state = PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST;

									// Send Join Request to Peer
									sendJoinRequest(context, peer, optLen, opt);

									// Return Success
									return 0;
								}
							}

							// P2P Mode
							else
							{
								// Already connected
								if (findP2P(context) != NULL) return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED, "adhocmatching already established");

								// Outgoing Request in Progress
								if (findOutgoingRequest(context) != NULL) return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS, "adhocmatching request in progress");

								// Join Request Mode
								if (peer->state == PSP_ADHOC_MATCHING_PEER_OFFER)
								{
									// Switch into Join Request Mode
									peer->state = PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST;

									// Send Join Request to Peer
									sendJoinRequest(context, peer, optLen, opt);

									// Return Success
									return 0;
								}

								// Requesting Peer
								else if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)
								{
									// Accept Peer in Group
									peer->state = PSP_ADHOC_MATCHING_PEER_P2P;

									// Tell Children about new Sibling
									//sendBirthMessage(context, peer);
									// Send Accept Confirmation to Peer
									sendAcceptMessage(context, peer, optLen, opt);

									// Return Success
									return 0;
								}
							}

							// How did this happen?! It shouldn't!
							return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_TARGET_NOT_READY, "adhocmatching target not ready");
						}

						// Invalid Optional Data Length
						return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN, "adhocmatching invalid optlen");
					}

					// Peer not found
					return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "adhocmatching unknown target");
				}

				// Idle Context
				return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");
			}

			// Invalid Matching ID
			return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");
		}

		// Invalid Arguments
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	// Uninitialized Library
	return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
}

int NetAdhocMatching_CancelTargetWithOpt(int matchingId, const char* macAddress, int optLen, u32 optDataPtr) {
	// Initialized Library
	if (netAdhocMatchingInited)
	{
		SceNetEtherAddr* target = (SceNetEtherAddr*)macAddress;
		void* opt = NULL;
		if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointerWriteUnchecked(optDataPtr);

		// Valid Arguments
		if (target != NULL && ((optLen == 0) || (optLen > 0 && opt != NULL)))
		{
			// Find Matching Context
			SceNetAdhocMatchingContext* context = findMatchingContext(matchingId);

			// Found Matching Context
			if (context != NULL)
			{
				// Running Context
				if (context->running)
				{
					// Find Peer
					SceNetAdhocMatchingMemberInternal* peer = findPeer(context, (SceNetEtherAddr*)target);

					// Found Peer
					if (peer != NULL)
					{
						// Valid Peer Mode
						if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT || peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST)) ||
							(context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)) ||
							(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && (peer->state == PSP_ADHOC_MATCHING_PEER_P2P || peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)))
						{
							// Notify other Children of Death
							if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD && countConnectedPeers(context) > 1)
							{
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
							return 0;
						}
					}

					// Peer not found
					//return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "adhocmatching unknown target");
					// Faking success to prevent the game (ie. Soul Calibur) to repeatedly calling this function when the other player is disconnected
					return 0;
				}

				// Context not running
				return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");
			}

			// Invalid Matching ID
			return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");
		}

		// Invalid Arguments
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	// Uninitialized Library
	return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
}

int sceNetAdhocMatchingCancelTargetWithOpt(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingCancelTargetWithOpt(%i, %s, %i, %08x) at %08x", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str(), optLen, optDataPtr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;	
	return NetAdhocMatching_CancelTargetWithOpt(matchingId, macAddress, optLen, optDataPtr);
}

int sceNetAdhocMatchingCancelTarget(int matchingId, const char *macAddress) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingCancelTarget(%i, %s)", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str());
	if (!g_Config.bEnableWlan)
		return -1;
	return NetAdhocMatching_CancelTargetWithOpt(matchingId, macAddress, 0, 0);
}

int sceNetAdhocMatchingGetHelloOpt(int matchingId, u32 optLenAddr, u32 optDataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingGetHelloOpt(%i, %08x, %08x)", matchingId, optLenAddr, optDataAddr);
	if (!g_Config.bEnableWlan)
		return -1;

	if (!Memory::IsValidAddress(optLenAddr)) return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;

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

	return 0;
}

int sceNetAdhocMatchingSetHelloOpt(int matchingId, int optLenAddr, u32 optDataAddr) {
	VERBOSE_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSetHelloOpt(%i, %i, %08x) at %08x", matchingId, optLenAddr, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	if (!netAdhocMatchingInited)
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");

	// Multithreading Lock
	peerlock.lock();

	SceNetAdhocMatchingContext* context = findMatchingContext(matchingId);

	// Multithreading Unlock
	peerlock.unlock();

	// Context not found
	if (context == NULL)
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");

	// Invalid Matching Mode (Child)
	if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_MODE, "adhocmatching invalid mode");

	// Context not running
	if (!context->running)
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");

	// Invalid Optional Data Length
	if ((optLenAddr != 0) && (optDataAddr == 0))
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN, "adhocmatching invalid optlen"); //ERROR_NET_ADHOC_MATCHING_INVALID_ARG

	// Grab Existing Hello Data
	void* hello = context->hello;

	// Free Previous Hello Data, or Reuse it
	//free(hello);

	// Allocation Required
	if (optLenAddr > 0)
	{
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
	else
	{
		// Delete Hello Data
		context->hellolen = 0;
		context->helloAddr = 0;
		//free(context->hello); // Doesn't need to free it since it will be reused later
		//context->hello = NULL;
	}

	// Return Success
	return 0;
}

static int sceNetAdhocMatchingGetMembers(int matchingId, u32 sizeAddr, u32 buf) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocMatchingGetMembers(%i, [%08x]=%i, %08x) at %08x", matchingId, sizeAddr, Memory::Read_U32(sizeAddr), buf, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	if (!netAdhocMatchingInited)
		return hleLogDebug(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");

	// Minimum Argument
	if (!Memory::IsValidAddress(sizeAddr))
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");

	// Multithreading Lock
	peerlock.lock();
	// Find Matching Context
	SceNetAdhocMatchingContext* context = findMatchingContext(matchingId);
	// Multithreading Unlock
	peerlock.unlock();

	// Context not found
	if (context == NULL)
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");

	// Context not running
	if (!context->running)
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");

	// Buffer Length not available
	if (!Memory::IsValidAddress(sizeAddr))
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");

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
	if (buf == 0)
	{
		// Get Connected Peer Count
		*buflen = available;
		DEBUG_LOG(SCENET, "MemberList [Connected: %i]", peercount);
	}

	// Normal Mode
	else
	{
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

		if (requestedpeers > 0)
		{
			// Add Self-Peer first, unless if there is existing Parent/P2P peer
			if (peercount == 1 || context->mode != PSP_ADHOC_MATCHING_MODE_CHILD) {
				// Add Local MAC
				buf2[filledpeers++].mac_addr = context->mac;

				DEBUG_LOG(SCENET, "MemberSelf [%s]", mac2str(&context->mac).c_str());
			}

			// Room for more than local peer
			if (requestedpeers > 1)
			{
				// P2P Mode
				if (context->mode == PSP_ADHOC_MATCHING_MODE_P2P)
				{
					// Find P2P Brother
					SceNetAdhocMatchingMemberInternal* p2p = findP2P(context, excludeTimedout);

					// P2P Brother found
					if (p2p != NULL)
					{
						// Faking lastping
						auto friendpeer = findFriend(&p2p->mac);
						if (p2p->lastping != 0 && friendpeer != NULL && friendpeer->last_recv != 0)
							p2p->lastping = std::max(p2p->lastping, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);
						else
							p2p->lastping = 0;

						// Add P2P Brother MAC
						buf2[filledpeers++].mac_addr = p2p->mac;

						DEBUG_LOG(SCENET, "MemberP2P [%s]", mac2str(&p2p->mac).c_str());
					}
				}

				// Parent or Child Mode
				else
				{
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

						DEBUG_LOG(SCENET, "MemberParent [%s]", mac2str(&parentpeer->mac).c_str());
					}

					// We may need to rearrange children where last joined player placed last
					std::deque<SceNetAdhocMatchingMemberInternal*> sortedPeers;

					// Iterate Peer List
					SceNetAdhocMatchingMemberInternal* peer = context->peerlist;
					for (; peer != NULL && filledpeers < requestedpeers; peer = peer->next)
					{
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

								DEBUG_LOG(SCENET, "MemberChild [%s]", mac2str(&peer->mac).c_str());
							}
						}

						// Child Mode
						else {
							// Interested in Siblings
							if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) {
								// Add Peer MAC
								buf2[filledpeers++].mac_addr = peer->mac;

								DEBUG_LOG(SCENET, "MemberSibling [%s]", mac2str(&peer->mac).c_str());
							}
							// Self Peer
							else if (peer->state == 0) {
								// Add Local MAC
								buf2[filledpeers++].mac_addr = peer->mac;

								DEBUG_LOG(SCENET, "MemberSelf [%s]", mac2str(&peer->mac).c_str());
							}

						}
					}
					sortedPeers.clear();
				}
			}

			// Link Result List
			for (int i = 0; i < filledpeers - 1; i++)
			{
				// Link Next Element
				//buf2[i].next = &buf2[i + 1];
				buf2[i].next = buf + (sizeof(SceNetAdhocMatchingMemberInfoEmu) * (i + 1LL));
			}
			// Fix Last Element
			if (filledpeers > 0) buf2[filledpeers - 1].next = 0;
		}

		// Fix Buffer Size
		*buflen = sizeof(SceNetAdhocMatchingMemberInfoEmu) * filledpeers;
		DEBUG_LOG(SCENET, "MemberList [Requested: %i][Discovered: %i]", requestedpeers, filledpeers);
	}

	// Return Success
	return hleDelayResult(0, "delay 100 ~ 1000us", 100); // seems to have different thread running within the delay duration
}

// Gran Turismo may replace the 1st bit of the 1st byte of MAC address's OUI with 0 (unicast bit), or replace the whole 6-bytes of MAC address with all 00 (invalid mac) for unknown reason
int sceNetAdhocMatchingSendData(int matchingId, const char *mac, int dataLen, u32 dataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSendData(%i, %s, %i, %08x) at %08x", matchingId, mac2str((SceNetEtherAddr*)mac).c_str(), dataLen, dataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Initialized Library
	if (netAdhocMatchingInited)
	{
		// Valid Arguments
		if (mac != NULL)
		{
			// Find Matching Context
			SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);

			// Found Context
			if (context != NULL)
			{
				// Running Context
				if (context->running)
				{
					// Invalid Data Length
					if (dataLen <=0 || dataAddr == 0)
						// Invalid Data Length
						return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_DATALEN, "invalid datalen");

					void* data = NULL;
					if (Memory::IsValidAddress(dataAddr)) data = Memory::GetPointerWriteUnchecked(dataAddr);

					// Lock the peer
					std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

					// Find Target Peer
					SceNetAdhocMatchingMemberInternal* peer = findPeer(context, (SceNetEtherAddr*)mac);

					// Found Peer
					if (peer != NULL)
					{
						// Valid Peer Connection State
						if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT || peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_P2P)
						{
							// Send in Progress
							if (peer->sending)
								return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_DATA_BUSY, "data busy");

							// Mark Peer as Sending
							peer->sending = 1;

							// Send Data to Peer
							sendBulkDataPacket(context, &peer->mac, dataLen, data);

							// Return Success
							return 0;
						}

						// Not connected / accepted
						return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_ESTABLISHED, "not established");
					}

					// Peer not found
					return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "unknown target");
				}

				// Context not running
				return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "not running");
			}

			// Invalid Matching ID
			return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "invalid id");
		}

		// Invalid Arguments
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "invalid arg");
	}

	// Uninitialized Library
	return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "not initialized");
}

int sceNetAdhocMatchingAbortSendData(int matchingId, const char *mac) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingAbortSendData(%i, %s)", matchingId, mac2str((SceNetEtherAddr*)mac).c_str());
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Initialized Library
	if (netAdhocMatchingInited)
	{
		// Valid Arguments
		if (mac != NULL)
		{
			// Find Matching Context
			SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);

			// Found Context
			if (context != NULL)
			{
				// Running Context
				if (context->running)
				{
					// Find Target Peer
					SceNetAdhocMatchingMemberInternal * peer = findPeer(context, (SceNetEtherAddr *)mac);

					// Found Peer
					if (peer != NULL)
					{
						// Peer is sending
						if (peer->sending)
						{
							// Set Peer as Bulk Idle
							peer->sending = 0;

							// Stop Bulk Data Sending (if in progress)
							abortBulkTransfer(context, peer);
						}

						// Return Success
						return 0;
					}

					// Peer not found
					return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET, "adhocmatching unknown target");
				}

				// Context not running
				return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");
			}

			// Invalid Matching ID
			return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");
		}

		// Invalid Arguments
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	// Uninitialized Library
	return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
}

// Get the maximum memory usage by the matching library
static int sceNetAdhocMatchingGetPoolMaxAlloc() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetPoolMaxAlloc() at %08x", currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Lazy way out - hardcoded return value
	return hleLogDebug(SCENET, fakePoolSize/2, "faked value");
}

int sceNetAdhocMatchingGetPoolStat(u32 poolstatPtr) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocMatchingGetPoolStat(%08x) at %08x", poolstatPtr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Initialized Library
	if (netAdhocMatchingInited)
	{
		SceNetMallocStat * poolstat = NULL;
		if (Memory::IsValidAddress(poolstatPtr)) poolstat = (SceNetMallocStat *)Memory::GetPointer(poolstatPtr);

		// Valid Argument
		if (poolstat != NULL)
		{
			// Fill Poolstat with Fake Data
			poolstat->pool = fakePoolSize;
			poolstat->maximum = fakePoolSize / 2; // Max usage faked to halt the pool
			poolstat->free = fakePoolSize - poolstat->maximum;

			// Return Success
			return 0;
		}

		// Invalid Argument
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
	}

	// Uninitialized Library
	return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED, "adhocmatching not initialized");
}

void __NetTriggerCallbacks()
{
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	hleSkipDeadbeef();
	int delayus = adhocDefaultDelay;

	auto params = adhocctlEvents.begin();
	if (params != adhocctlEvents.end())
	{
		int newState = adhocctlState;
		u32 flags = params->first;
		u32 error = params->second;
		u32_le args[3] = { 0, 0, 0 };
		args[0] = flags;
		args[1] = error;
		u64 now = (u64)(time_now_d() * 1000000.0);

		// FIXME: When Joining a group, Do we need to wait for group creator's peer data before triggering the callback to make sure the game not to thinks we're the group creator?
		if ((flags != ADHOCCTL_EVENT_CONNECT && flags != ADHOCCTL_EVENT_GAME) || adhocConnectionType != ADHOC_JOIN || getActivePeerCount() > 0 || static_cast<s64>(now - adhocctlStartTime) > adhocDefaultTimeout)
		{
			// Since 0 is a valid index to types_ we use -1 to detects if it was loaded from an old save state
			if (actionAfterAdhocMipsCall < 0) {
				actionAfterAdhocMipsCall = __KernelRegisterActionType(AfterAdhocMipsCall::Create);
			}

			delayus = adhocEventPollDelay; // May need to add an extra delay if a certain I/O Timing method causing disconnection issue
			switch (flags) {
			case ADHOCCTL_EVENT_CONNECT:
				newState = ADHOCCTL_STATE_CONNECTED;
				if (adhocConnectionType == ADHOC_CREATE)
					delayus = adhocEventDelay; // May affects Dissidia 012 and GTA VCS
				else if (adhocConnectionType == ADHOC_CONNECT)
					delayus = adhocEventDelay / 2;
				break;
			case ADHOCCTL_EVENT_SCAN: // notified only when scan completed?
				newState = ADHOCCTL_STATE_DISCONNECTED;
				//delayus = adhocEventDelay / 2;
				break;
			case ADHOCCTL_EVENT_DISCONNECT:
				newState = ADHOCCTL_STATE_DISCONNECTED;
				delayus = adhocDefaultDelay; // Tekken 5 expects AdhocctlDisconnect to be done within ~17ms (a frame?)
				break;
			case ADHOCCTL_EVENT_GAME: 
			{
				newState = ADHOCCTL_STATE_GAMEMODE;
				delayus = adhocEventDelay;
				// TODO: Use blocking PTP connection to sync the timing just like official prx did (which is done before notifying user-defined Adhocctl Handlers)
				// Workaround: Extra delay to prevent Joining player to progress faster than the Creator on Pocket Pool, but unbalanced delays could cause an issue on Shaun White Snowboarding :(
				if (adhocConnectionType == ADHOC_JOIN) 
					delayus += adhocExtraDelay * 3;
				// Shows player list
				INFO_LOG(SCENET, "GameMode - All players have joined:");
				int i = 0;
				for (auto& mac : gameModeMacs) {
					INFO_LOG(SCENET, "GameMode macAddress#%d=%s", i++, mac2str(&mac).c_str());
					if (i >= ADHOCCTL_GAMEMODE_MAX_MEMBERS)
						break;
				}
			}
			break;
			case ADHOCCTL_EVENT_DISCOVER:
				newState = ADHOCCTL_STATE_DISCOVER;
				break;
			case ADHOCCTL_EVENT_WOL_INTERRUPT:
				newState = ADHOCCTL_STATE_WOL;
				break;
			case ADHOCCTL_EVENT_ERROR:
				delayus = adhocDefaultDelay * 3;
				break;
			}

			for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
				DEBUG_LOG(SCENET, "AdhocctlCallback: [ID=%i][EVENT=%i][Error=%08x]", it->first, flags, error);
				args[2] = it->second.argument;
				AfterAdhocMipsCall* after = (AfterAdhocMipsCall*)__KernelCreateAction(actionAfterAdhocMipsCall);
				after->SetData(it->first, flags, args[2]);
				hleEnqueueCall(it->second.entryPoint, 3, args, after);
			}
			adhocctlEvents.pop_front();
			// Since we don't have beforeAction, simulate it using ScheduleEvent
			ScheduleAdhocctlState(flags, newState, delayus, "adhocctl callback state");
			return;
		}
	}

	// Must be delayed long enough whenever there is a pending callback. Should it be 100-500ms for Adhocctl Events? or Not Less than the delays on sceNetAdhocctl HLE?
	sceKernelDelayThread(adhocDefaultDelay);
}

void __NetMatchingCallbacks() //(int matchingId)
{
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
		DEBUG_LOG(SCENET, "AdhocMatching - Remaining Events: %zu", matchingEvents.size());
		auto peer = findPeer(context, (SceNetEtherAddr*)Memory::GetPointer(args[2]));
		// Discard HELLO Events when in the middle of joining, as some games (ie. Super Pocket Tennis) might tried to join again (TODO: Need to confirm whether sceNetAdhocMatchingSelectTarget supposed to be blocking the current thread or not)
		if (peer == NULL || (args[1] != PSP_ADHOC_MATCHING_EVENT_HELLO || (peer->state != PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST && peer->state != PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST))) {
			DEBUG_LOG(SCENET, "AdhocMatchingCallback: [ID=%i][EVENT=%i][%s]", args[0], args[1], mac2str((SceNetEtherAddr *)Memory::GetPointer(args[2])).c_str());
		
			AfterMatchingMipsCall* after = (AfterMatchingMipsCall*)__KernelCreateAction(actionAfterMatchingMipsCall);
			after->SetData(args[0], args[1], args[2]);
			hleEnqueueCall(args[5], 5, args, after);
			matchingEvents.pop_front();
		}
		else {
			DEBUG_LOG(SCENET, "AdhocMatching - Discarding Callback: [ID=%i][EVENT=%i][%s]", args[0], args[1], mac2str((SceNetEtherAddr*)Memory::GetPointer(args[2])).c_str());
			matchingEvents.pop_front();
		}
	}

	// Must be delayed long enough whenever there is a pending callback. Should it be 10-100ms for Matching Events? or Not Less than the delays on sceNetAdhocMatching HLE?
	sceKernelDelayThread(delayus);
}

const HLEFunction sceNetAdhoc[] = {
	{0XE1D621D7, &WrapU_V<sceNetAdhocInit>,                            "sceNetAdhocInit",                        'x', ""         },
	{0XA62C6F57, &WrapI_V<sceNetAdhocTerm>,                            "sceNetAdhocTerm",                        'i', ""         },
	{0X0AD043ED, &WrapI_C<sceNetAdhocctlConnect>,                      "sceNetAdhocctlConnect",                  'i', "s"        },
	{0X6F92741B, &WrapI_CIIU<sceNetAdhocPdpCreate>,                    "sceNetAdhocPdpCreate",                   'i', "siix"     },
	{0XABED3790, &WrapI_ICUVIII<sceNetAdhocPdpSend>,                   "sceNetAdhocPdpSend",                     'i', "isxpiii"  },
	{0XDFE53E03, &WrapI_IVVVVUI<sceNetAdhocPdpRecv>,                   "sceNetAdhocPdpRecv",                     'i', "ippppxi"  },
	{0X7F27BB5E, &WrapI_II<sceNetAdhocPdpDelete>,                      "sceNetAdhocPdpDelete",                   'i', "ii"       },
	{0XC7C1FC57, &WrapI_UU<sceNetAdhocGetPdpStat>,                     "sceNetAdhocGetPdpStat",                  'i', "xx"       },
	{0X157E6225, &WrapI_II<sceNetAdhocPtpClose>,                       "sceNetAdhocPtpClose",                    'i', "ii"       },
	{0X4DA4C788, &WrapI_IUUII<sceNetAdhocPtpSend>,                     "sceNetAdhocPtpSend",                     'i', "ixxii"    },
	{0X877F6D66, &WrapI_CICIIIII<sceNetAdhocPtpOpen>,                  "sceNetAdhocPtpOpen",                     'i', "sisiiiii" },
	{0X8BEA2B3E, &WrapI_IUUII<sceNetAdhocPtpRecv>,                     "sceNetAdhocPtpRecv",                     'i', "ixxii"    },
	{0X9DF81198, &WrapI_IUUII<sceNetAdhocPtpAccept>,                   "sceNetAdhocPtpAccept",                   'i', "ixxii"    },
	{0XE08BDAC1, &WrapI_CIIIIII<sceNetAdhocPtpListen>,                 "sceNetAdhocPtpListen",                   'i', "siiiiii"  },
	{0XFC6FC07B, &WrapI_III<sceNetAdhocPtpConnect>,                    "sceNetAdhocPtpConnect",                  'i', "iii"      },
	{0X9AC2EEAC, &WrapI_III<sceNetAdhocPtpFlush>,                      "sceNetAdhocPtpFlush",                    'i', "iii"      },
	{0XB9685118, &WrapI_UU<sceNetAdhocGetPtpStat>,                     "sceNetAdhocGetPtpStat",                  'i', "xx"       },
	{0X3278AB0C, &WrapI_CUI<sceNetAdhocGameModeCreateReplica>,         "sceNetAdhocGameModeCreateReplica",       'i', "sxi"      },
	{0X98C204C8, &WrapI_V<sceNetAdhocGameModeUpdateMaster>,            "sceNetAdhocGameModeUpdateMaster",        'i', ""         },
	{0XFA324B4E, &WrapI_IU<sceNetAdhocGameModeUpdateReplica>,          "sceNetAdhocGameModeUpdateReplica",       'i', "ix"       },
	{0XA0229362, &WrapI_V<sceNetAdhocGameModeDeleteMaster>,            "sceNetAdhocGameModeDeleteMaster",        'i', ""         },
	{0X0B2228E9, &WrapI_I<sceNetAdhocGameModeDeleteReplica>,           "sceNetAdhocGameModeDeleteReplica",       'i', "i"        },
	{0X7F75C338, &WrapI_UI<sceNetAdhocGameModeCreateMaster>,           "sceNetAdhocGameModeCreateMaster",        'i', "xi"       },
	{0X73BFD52D, &WrapI_II<sceNetAdhocSetSocketAlert>,                 "sceNetAdhocSetSocketAlert",              'i', "ii"       },
	{0X4D2CE199, &WrapI_IU<sceNetAdhocGetSocketAlert>,                 "sceNetAdhocGetSocketAlert",              'i', "ix"       },
	{0X7A662D6B, &WrapI_UIII<sceNetAdhocPollSocket>,                   "sceNetAdhocPollSocket",                  'i', "xiii"     },
	// Fake function for PPSSPP's use.
	{0X756E6E6F, &WrapV_V<__NetTriggerCallbacks>,                      "__NetTriggerCallbacks",                  'v', ""         },
};							

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

int NetAdhocctl_ExitGameMode() {
	if (gameModeSocket > 0) {
		NetAdhocPdp_Delete(gameModeSocket, 0);
		gameModeSocket = (int)INVALID_SOCKET;
	}

	deleteAllGMB();
	gameModePeerPorts.clear();

	adhocctlCurrentMode = ADHOCCTL_MODE_NONE;
	netAdhocGameModeEntered = false;
	return NetAdhocctl_Disconnect();
}

static int sceNetAdhocctlExitGameMode() {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlExitGameMode() at %08x", currentMIPS->pc);
	
	return NetAdhocctl_ExitGameMode();
}

static int sceNetAdhocctlGetGameModeInfo(u32 infoAddr) {
	DEBUG_LOG(SCENET, "sceNetAdhocctlGetGameModeInfo(%08x)", infoAddr);
	if (!netAdhocctlInited)
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");

	if (!Memory::IsValidAddress(infoAddr))
		return hleLogError(SCENET, ERROR_NET_ADHOCCTL_INVALID_ARG, "invalid arg");

	SceNetAdhocctlGameModeInfo* gmInfo = (SceNetAdhocctlGameModeInfo*)Memory::GetPointer(infoAddr);
	// Writes number of participants and each participating MAC address into infoAddr/gmInfo
	gmInfo->num = static_cast<s32_le>(gameModeMacs.size());
	int i = 0;
	for (auto& mac : gameModeMacs) {
		VERBOSE_LOG(SCENET, "GameMode macAddress#%d=%s", i, mac2str(&mac).c_str());
		gmInfo->members[i++] = mac;
		if (i >= ADHOCCTL_GAMEMODE_MAX_MEMBERS) 
			break;
	}

	hleEatMicro(100);
	return 0;
}

static int sceNetAdhocctlGetPeerList(u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);
	SceNetAdhocctlPeerInfoEmu *buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointer(bufAddr);

	DEBUG_LOG(SCENET, "sceNetAdhocctlGetPeerList([%08x]=%i, %08x) at %08x", sizeAddr, /*buflen ? *buflen : -1*/Memory::Read_U32(sizeAddr), bufAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	// Initialized Library
	if (netAdhocctlInited) {
		// Minimum Arguments
		if (buflen != NULL) {
			// FIXME: Sometimes returing 0x80410682 when Adhocctl is still BUSY or before AdhocctlGetState became ADHOCCTL_STATE_CONNECTED or related to Auth/Library ?

			// Multithreading Lock
			peerlock.lock();

			bool excludeTimedout = true;
			// Length Calculation Mode
			if (buf == NULL) {
				int activePeers = getActivePeerCount(excludeTimedout);
				*buflen = activePeers * sizeof(SceNetAdhocctlPeerInfoEmu);
				DEBUG_LOG(SCENET, "PeerList [Active: %i]", activePeers);
			}
			// Normal Mode
			else {
				// Discovery Counter
				int discovered = 0;

				// Calculate Request Count
				int requestcount = *buflen / sizeof(SceNetAdhocctlPeerInfoEmu);

				// FIXME: When bufAddr is not null but buffer size is smaller than activePeers * sizeof(SceNetAdhocctlPeerInfoEmu), simply return buffer size = 0 without filling the buffer?

				// Clear Memory
				memset(buf, 0, *buflen);

				// Minimum Arguments
				if (requestcount > 0) {
					// Peer Reference
					SceNetAdhocctlPeerInfo * peer = friends;

					// Iterate Peers
					for (; peer != NULL && discovered < requestcount; peer = peer->next) {
						// Exclude Soon to be timedout peers?
						if (!excludeTimedout || peer->last_recv != 0) {
							// Faking Last Receive Time
							if (peer->last_recv != 0) 
								peer->last_recv = std::max(peer->last_recv, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);

							// Copy Peer Info
							buf[discovered].nickname = peer->nickname;
							buf[discovered].mac_addr = peer->mac_addr;
							buf[discovered].flags = 0x0400;
							buf[discovered].last_recv = peer->last_recv;
							discovered++;

							u32_le ipaddr = peer->ip_addr;
							DEBUG_LOG(SCENET, "Peer [%s][%s][%s][%llu]", mac2str(&peer->mac_addr).c_str(), ip2str(*(in_addr*)&ipaddr).c_str(), (const char*)&peer->nickname.data, peer->last_recv);
						}
					}

					// Link List
					for (int i = 0; i < discovered - 1; i++) {
						// Link Network
						buf[i].next = bufAddr+(sizeof(SceNetAdhocctlPeerInfoEmu)*i) + sizeof(SceNetAdhocctlPeerInfoEmu);
					}
					// Fix Last Element
					if (discovered > 0) buf[discovered - 1].next = 0;
				}

				// Fix Size
				*buflen = discovered * sizeof(SceNetAdhocctlPeerInfoEmu);
				DEBUG_LOG(SCENET, "PeerList [Requested: %i][Discovered: %i]", requestcount, discovered);
			}

			// Multithreading Unlock
			peerlock.unlock();

			// Return Success
			return hleDelayResult(0, "delay 100 ~ 1000us", 100); // seems to have different thread running within the delay duration
		}

		// Invalid Arguments
		return hleLogDebug(SCENET, ERROR_NET_ADHOCCTL_INVALID_ARG, "invalid arg");
	}

	// Uninitialized Library
	return hleLogDebug(SCENET, ERROR_NET_ADHOCCTL_NOT_INITIALIZED, "not initialized");
}

static int sceNetAdhocctlGetAddrByName(const char *nickName, u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL; //int32_t
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);

	char nckName[ADHOCCTL_NICKNAME_LEN];
	memcpy(nckName, nickName, ADHOCCTL_NICKNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	nckName[ADHOCCTL_NICKNAME_LEN - 1] = 0;
	
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlGetAddrByName, SCENET, "UNTESTED sceNetAdhocctlGetAddrByName(%s, [%08x]=%d/%zu, %08x) at %08x", nckName, sizeAddr, buflen ? *buflen : -1, sizeof(SceNetAdhocctlPeerInfoEmu), bufAddr, currentMIPS->pc);
	
	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Arguments
		if (nickName != NULL && buflen != NULL)
		{
			SceNetAdhocctlPeerInfoEmu *buf = NULL;
			if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointer(bufAddr);

			// Multithreading Lock
			peerlock.lock();

			// Length Calculation Mode
			if (buf == NULL) {
				int foundName = getNicknameCount(nickName);
				*buflen = foundName * sizeof(SceNetAdhocctlPeerInfoEmu);
				DEBUG_LOG(SCENET, "PeerNameList [%s: %i]", nickName, foundName);
			}
			// Normal Information Mode
			else
			{
				// Clear Memory
				memset(buf, 0, *buflen);

				// Discovered Player Count
				int discovered = 0;

				// Calculate Requested Elements
				int requestcount = *buflen / sizeof(SceNetAdhocctlPeerInfoEmu);

				// Minimum Space available
				if (requestcount > 0)
				{
					// Local Nickname Matches
					if (strncmp((char *)&parameter.nickname.data, nickName, ADHOCCTL_NICKNAME_LEN) == 0)
					{
						// Get Local IP Address
						sockaddr_in addr;
						SceNetEtherAddr mac;

						getLocalIp(&addr);
						buf[discovered].nickname = parameter.nickname;
						buf[discovered].nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
						getLocalMac(&mac);
						buf[discovered].mac_addr = mac;
						buf[discovered].flags = 0x0400;
						u64 lastrecv = std::max(0LL, static_cast<s64>(CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta));
						buf[discovered++].last_recv = lastrecv;

						DEBUG_LOG(SCENET, "Peer [%s][%s][%s][%llu]", mac2str(&mac).c_str(), ip2str(addr.sin_addr).c_str(), nickName, lastrecv);
					}

					// Peer Reference
					SceNetAdhocctlPeerInfo * peer = friends;

					// Iterate Peers
					for (; peer != NULL && discovered < requestcount; peer = peer->next)
					{
						// Match found
						if (peer->last_recv != 0 && strncmp((char *)&peer->nickname.data, nickName, ADHOCCTL_NICKNAME_LEN) == 0)
						{
							// Fake Receive Time
							peer->last_recv = std::max(peer->last_recv, CoreTiming::GetGlobalTimeUsScaled() - defaultLastRecvDelta);

							// Copy Peer Info
							buf[discovered].nickname = peer->nickname;
							buf[discovered].nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
							buf[discovered].mac_addr = peer->mac_addr;
							buf[discovered].flags = 0x0400;
							buf[discovered++].last_recv = peer->last_recv;

							u32_le ipaddr = peer->ip_addr;
							DEBUG_LOG(SCENET, "Peer [%s][%s][%s][%llu]", mac2str(&peer->mac_addr).c_str(), ip2str(*(in_addr*)&ipaddr).c_str(), (const char*)&peer->nickname.data, peer->last_recv);
						}
					}

					// Link List
					for (int i = 0; i < discovered - 1; i++)
					{
						// Link Network
						buf[i].next = bufAddr + (sizeof(SceNetAdhocctlPeerInfoEmu)*i) + sizeof(SceNetAdhocctlPeerInfoEmu);
					}

					// Fix Last Element
					if (discovered > 0) buf[discovered - 1].next = 0;
				}

				// Fix Buffer Size
				*buflen = discovered * sizeof(SceNetAdhocctlPeerInfoEmu);
				DEBUG_LOG(SCENET, "PeerNameList [%s][Requested: %i][Discovered: %i]", nickName, requestcount, discovered);
			}

			// Multithreading Unlock
			peerlock.unlock();

			// Return Success
			return hleLogDebug(SCENET, hleDelayResult(0, "delay 100 ~ 1000us", 100), "success"); // FIXME: Might have similar delay with GetPeerList? need to know which games using this tho
		}

		// Invalid Arguments
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

const HLEFunction sceNetAdhocctl[] = {
	{0XE26F226E, &WrapU_IIU<sceNetAdhocctlInit>,                       "sceNetAdhocctlInit",                     'x', "iix"      },
	{0X9D689E13, &WrapI_V<sceNetAdhocctlTerm>,                         "sceNetAdhocctlTerm",                     'i', ""         },
	{0X20B317A0, &WrapU_UU<sceNetAdhocctlAddHandler>,                  "sceNetAdhocctlAddHandler",               'x', "xx"       },
	{0X6402490B, &WrapU_U<sceNetAdhocctlDelHandler>,                   "sceNetAdhocctlDelHandler",               'x', "x"        },
	{0X34401D65, &WrapU_V<sceNetAdhocctlDisconnect>,                   "sceNetAdhocctlDisconnect",               'x', ""         },
	{0X0AD043ED, &WrapI_C<sceNetAdhocctlConnect>,                      "sceNetAdhocctlConnect",                  'i', "s"        },
	{0X08FFF7A0, &WrapI_V<sceNetAdhocctlScan>,                         "sceNetAdhocctlScan",                     'i', ""         },
	{0X75ECD386, &WrapI_U<sceNetAdhocctlGetState>,                     "sceNetAdhocctlGetState",                 'i', "x"        },
	{0X8916C003, &WrapI_CU<sceNetAdhocctlGetNameByAddr>,               "sceNetAdhocctlGetNameByAddr",            'i', "sx"       },
	{0XDED9D28E, &WrapI_U<sceNetAdhocctlGetParameter>,                 "sceNetAdhocctlGetParameter",             'i', "x"        },
	{0X81AEE1BE, &WrapI_UU<sceNetAdhocctlGetScanInfo>,                 "sceNetAdhocctlGetScanInfo",              'i', "xx"       },
	{0X5E7F79C9, &WrapI_U<sceNetAdhocctlJoin>,                         "sceNetAdhocctlJoin",                     'i', "x"        },
	{0X8DB83FDC, &WrapI_CIU<sceNetAdhocctlGetPeerInfo>,                "sceNetAdhocctlGetPeerInfo",              'i', "six"      },
	{0XEC0635C1, &WrapI_C<sceNetAdhocctlCreate>,                       "sceNetAdhocctlCreate",                   'i', "s"        },
	{0XA5C055CE, &WrapI_CIIUII<sceNetAdhocctlCreateEnterGameMode>,     "sceNetAdhocctlCreateEnterGameMode",      'i', "siixii"   },
	{0X1FF89745, &WrapI_CCII<sceNetAdhocctlJoinEnterGameMode>,         "sceNetAdhocctlJoinEnterGameMode",        'i', "ssii"     },
	{0XCF8E084D, &WrapI_V<sceNetAdhocctlExitGameMode>,                 "sceNetAdhocctlExitGameMode",             'i', ""         },
	{0XE162CB14, &WrapI_UU<sceNetAdhocctlGetPeerList>,                 "sceNetAdhocctlGetPeerList",              'i', "xx"       },
	{0X362CBE8F, &WrapI_U<sceNetAdhocctlGetAdhocId>,                   "sceNetAdhocctlGetAdhocId",               'i', "x"        },
	{0X5A014CE0, &WrapI_U<sceNetAdhocctlGetGameModeInfo>,              "sceNetAdhocctlGetGameModeInfo",          'i', "x"        },
	{0X99560ABE, &WrapI_CUU<sceNetAdhocctlGetAddrByName>,              "sceNetAdhocctlGetAddrByName",            'i', "sxx"      },
	{0XB0B80E80, &WrapI_CIIIUUI<sceNetAdhocctlCreateEnterGameModeMin>, "sceNetAdhocctlCreateEnterGameModeMin",   'i', "siiixxi"  }, // ??
};

// Return value: 0/0x80410005/0x80411301/error returned from sceNetAdhocctl_lib_F8BABD85[/error returned from sceUtilityGetSystemParamInt?]
int sceNetAdhocDiscoverInitStart(u32 paramAddr) {
	WARN_LOG_REPORT_ONCE(sceNetAdhocDiscoverInitStart, SCENET, "UNIMPL sceNetAdhocDiscoverInitStart(%08x) at %08x", paramAddr, currentMIPS->pc);
	// FIXME: Most AdhocDiscover syscalls will return 0x80410005 if (sceKernelCheckThreadStack_user() < 0x00000FE0), AdhocDiscover seems to be storing some data in the stack, while we use global variables for these
	if (sceKernelCheckThreadStack() < 0x00000FE0)
		return 0x80410005;
	// TODO: Allocate internal buffer/struct (on the stack?) to be returned on sceNetAdhocDiscoverUpdate (the struct may contains WLAN channel from sceUtilityGetSystemParamInt at offset 0xA0 ?), setup adhocctl state callback handler to detects state change (using sceNetAdhocctl_lib_F8BABD85(stateCallbackFunction=0x09F436F8, adhocctlStateCallbackArg=0x0) on JPCSP+prx)
	u32 bufSize = 256; // dummy size, not sure how large it supposed to be, may be at least 0x3c bytes like in param->unknown2 ?
	if (netAdhocDiscoverBufAddr == 0) {
		netAdhocDiscoverBufAddr = userMemory.Alloc(bufSize, true, "AdhocDiscover"); // The address returned on DiscoverUpdate seems to be much higher than the param address, closer to the internal stateCallbackFunction address
		if (!Memory::IsValidAddress(netAdhocDiscoverBufAddr))
			return 0x80410005;
		Memory::Memset(netAdhocDiscoverBufAddr, 0, bufSize);
	}
	// FIME: Not sure what is this address 0x000010B0 used for (current Step may be?), but return 0x80411301 if (*((int *) 0x000010B0) != 0)
	//if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) != 0) //if (*((int*)Memory::GetPointer(0x000010B0)) != 0)
	//	return 0x80411301; // Already Initialized/Started?
	// TODO: Need to findout whether using invalid params or param address will return an error code or not
	netAdhocDiscoverParam = (SceNetAdhocDiscoverParam*)Memory::GetPointer(paramAddr);
	if (!netAdhocDiscoverParam)
		return hleLogError(SCENET, -1, "invalid param?");
	// FIXME: paramAddr seems to be stored at 0x000010D8 without validating the value first
	//*((int*)Memory::GetPointer(0x000010D8)) = paramAddr;
	
	// Based on Legend Of The Dragon: 
	// The 1st 24 x 32bit(addr) seems to be pointers to subroutine containings sceNetAdhocctlCreate/sceNetAdhocctlJoin/sceNetAdhocctlDisconnect/sceNetAdhocctlScan/sceNetRand/sceKernelGetSystemTimeWide/etc.
	// Offset 0x60: 10 00 06 06 
	// Offset 0x70: FF FF FF FF (before Init) -> 00 00 00 00 (after Init) -> FF FF FF FF (after Term) // Seems to be value returned from sceNetAdhocctl_lib_F8BABD85, and the address (0x000010A0) seems to be the lowest one for storing data
	// Offset 0x80: 00 -> 0B/0C/0D/13 -> 00 // This seems to be (current step?) at address at 0x000010B0 (ie. *((int *) 0x000010B0) = 0x0000000B), something todo with param->unknown1(sleep mode?)
	// Offset 0x84: 00 -> 03 -> 03 // somekind of State? Something todo with param->unknown1(sleep mode?) along with data at 0x000010B0 (current step?) 
	// Offset 0x98: 0000 -> 0000/0200 -> 0000 // This seems to be somekind of flags at 0x000010C8 (ie. *((int *) 0x000010C8) = (var4 | 0x00000080)), something todo with data at 0x000010B0 (current step?) 
	// Offset 0xA0: WLAN channel from sceUtilityGetSystemParamInt (ie. sceUtilityGetSystemParamInt(0x00000002, 0x000010D0) on decompiled prx, but on JPCSP+prx Logs it's sceUtilityGetSystemParamInt(0x00000002, 0x09F43FD0))
	// Offset 0xA4: Seems to be at 0x000010D4 and related to RequestSuspend
	// Offset 0xA8: paramAddr // This seems to be a fixed address at 0x000010D8 (ie. *((int *) 0x000010D8) = paramAddr)
	// The rest are zeroed
	Memory::Write_U32(0x06060010, netAdhocDiscoverBufAddr + 0x60);
	Memory::Write_U32(0xffffffff, netAdhocDiscoverBufAddr + 0x70);
	if (netAdhocDiscoverParam->unknown1 == 0) {
		Memory::Write_U32(0x0B, netAdhocDiscoverBufAddr + 0x80);
		Memory::Write_U32(0x03, netAdhocDiscoverBufAddr + 0x84);
	}
	else if (netAdhocDiscoverParam->unknown1 == 1) {
		Memory::Write_U32(0x0F, netAdhocDiscoverBufAddr + 0x80);
		Memory::Write_U32(0x04, netAdhocDiscoverBufAddr + 0x84);
	}
	Memory::Write_U32(0, netAdhocDiscoverBufAddr + 0x98);
	Memory::Write_U32(g_Config.iWlanAdhocChannel, netAdhocDiscoverBufAddr + 0xA0);
	Memory::Write_U32(0, netAdhocDiscoverBufAddr + 0xA4);
	Memory::Write_U32(paramAddr, netAdhocDiscoverBufAddr + 0xA8);

	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	memcpy(grpName, netAdhocDiscoverParam->groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	DEBUG_LOG(SCENET, "sceNetAdhocDiscoverInitStart - Param.Unknown1 : %08x", netAdhocDiscoverParam->unknown1);
	DEBUG_LOG(SCENET, "sceNetAdhocDiscoverInitStart - Param.GroupName: [%s]", grpName);
	DEBUG_LOG(SCENET, "sceNetAdhocDiscoverInitStart - Param.Unknown2 : %08x", netAdhocDiscoverParam->unknown2);
	DEBUG_LOG(SCENET, "sceNetAdhocDiscoverInitStart - Param.Result   : %08x", netAdhocDiscoverParam->result);

	// TODO: Check whether we're already in the correct group and change the status and result accordingly
	netAdhocDiscoverIsStopping = false;
	netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS;
	netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND;
	netAdhocDiscoverStartTime = CoreTiming::GetGlobalTimeUsScaled();
	return hleLogSuccessInfoX(SCENET, 0);
}

// Note1: When canceling the progress, Legend Of The Dragon will use DiscoverStop -> AdhocctlDisconnect -> DiscoverTerm (when status changed to 2)
// Note2: When result = NO_PEER_FOUND or PEER_FOUND the progress can no longer be canceled on Legend Of The Dragon
int sceNetAdhocDiscoverStop() {
	WARN_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverStop()");
	if (sceKernelCheckThreadStack() < 0x00000FF0)
		return 0x80410005;

	if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) > 0 && (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80)^0x13) > 0) {
		Memory::Write_U32(Memory::Read_U32(netAdhocDiscoverBufAddr + 0x98) | 0x20, netAdhocDiscoverBufAddr + 0x98);
		Memory::Write_U32(0, netAdhocDiscoverBufAddr + 0xA4);
	}
	// FIXME: Doesn't seems to be immediately changed the status, may be waiting until Disconnected from Adhocctl before changing the status to Completed?
	netAdhocDiscoverIsStopping = true;
	//netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_COMPLETED;
	//if (netAdhocDiscoverParam) netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_CANCELED;
	return 0;
}

int sceNetAdhocDiscoverTerm() {
	WARN_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverTerm() at %08x", currentMIPS->pc);
	/*
	if (sceKernelCheckThreadStack() < 0x00000FF0)
		return 0x80410005;

	if (!(Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) > 0 && (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) ^ 0x13) > 0))
		return 0x80411301; // Not Initialized/Started yet?
	*/
	// TODO: Use sceNetAdhocctl_lib_1C679240 to remove adhocctl state callback handler setup in sceNetAdhocDiscoverInitStart
	/*if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x70) >= 0) {
		LinkDiscoverSkip(Memory::Read_U32(netAdhocDiscoverBufAddr + 0x70)); //sceNetAdhocctl_lib_1C679240
		Memory::Write_U32(0xffffffff, netAdhocDiscoverBufAddr + 0x70);
	}
	Memory::Write_U32(0, netAdhocDiscoverBufAddr + 0x80);
	Memory::Write_U32(0, netAdhocDiscoverBufAddr + 0xA8);
	*/
	netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_NONE;
	//if (netAdhocDiscoverParam) netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND; // Test: Using result = NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND will trigger Legend Of The Dragon to call sceNetAdhocctlGetPeerList after DiscoverTerm
	if (Memory::IsValidAddress(netAdhocDiscoverBufAddr)) {
		userMemory.Free(netAdhocDiscoverBufAddr);
		netAdhocDiscoverBufAddr = 0;
	}
	netAdhocDiscoverIsStopping = false;
	return 0;
}

int sceNetAdhocDiscoverGetStatus() {
	DEBUG_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverGetStatus() at %08x", currentMIPS->pc);
	if (sceKernelCheckThreadStack() < 0x00000FF0)
		return 0x80410005;
	/*
	if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) <= 0)
		return 0;
	if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) <= 0x13)
		return 1;
	if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) == 0x13)
		return 2;
	*/
	return hleLogDebug(SCENET, netAdhocDiscoverStatus); // Returning 2 will trigger Legend Of The Dragon to call sceNetAdhocctlGetPeerList (only happened if it was the first sceNetAdhocDiscoverGetStatus after sceNetAdhocDiscoverInitStart)
}

int sceNetAdhocDiscoverRequestSuspend()
{
	ERROR_LOG_REPORT_ONCE(sceNetAdhocDiscoverRequestSuspend, SCENET, "UNIMPL sceNetAdhocDiscoverRequestSuspend() at %08x", currentMIPS->pc);
	// FIXME: Not sure what is this syscall used for, may be related to Sleep Mode and can be triggered by using Power/Hold Switch? (based on what's written on Dissidia 012)
	if (sceKernelCheckThreadStack() < 0x00000FF0)
		return 0x80410005;
	/*
	if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0xA4) == 0)
		return 0x80411303; // Already Suspended?
	if (Memory::Read_U32(netAdhocDiscoverBufAddr + 0x80) != 0)
		return 0x80411303; // Already Suspended?
	int ret = sceNetAdhocctl_lib_1572422C();
	if (ret >= 0)
		Memory::Write_U32(0, netAdhocDiscoverBufAddr + 0xA4);
	return ret;
	*/
	// Since we don't know what this supposed to do, and we currently don't have a working AdhocDiscover yet, may be we should cancel the progress for now?
	netAdhocDiscoverIsStopping = true;
	return hleLogError(SCENET, 0);
}

int sceNetAdhocDiscoverUpdate() {
	DEBUG_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverUpdate() at %08x", currentMIPS->pc);
	if (sceKernelCheckThreadStack() < 0x00000FF0)
		return 0x80410005;

	// TODO: Use switch case for each Step
	if (netAdhocDiscoverStatus == NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS) {
		//u64 now = CoreTiming::GetGlobalTimeUsScaled();
		if (netAdhocDiscoverIsStopping /*|| now >= netAdhocDiscoverStartTime + DISCOVER_DURATION_US*/) {
			// Fake a successful completion after some time (or when detecting another player in the same Group?)
			netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_COMPLETED;
			if (netAdhocDiscoverParam)
				netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_CANCELED; // netAdhocDiscoverIsStopping ? NET_ADHOC_DISCOVER_RESULT_CANCELED : NET_ADHOC_DISCOVER_RESULT_PEER_FOUND;
		}
	}
	return hleDelayResult(hleLogDebug(SCENET, 0/*netAdhocDiscoverBufAddr*/), "adhoc discover update", 300); // FIXME: Based on JPCSP+prx, it seems to be returning a pointer to the internal buffer/struct (only when status = 1 ?), But when i stepped the code it returns 0 (might be a bug on JPCSP LLE Logging?)
}

const HLEFunction sceNetAdhocDiscover[] = {
	{0X941B3877, &WrapI_U<sceNetAdhocDiscoverInitStart>,               "sceNetAdhocDiscoverInitStart",           'i', "x"        },
	{0X52DE1B97, &WrapI_V<sceNetAdhocDiscoverUpdate>,                  "sceNetAdhocDiscoverUpdate",              'i', ""         },
	{0X944DDBC6, &WrapI_V<sceNetAdhocDiscoverGetStatus>,               "sceNetAdhocDiscoverGetStatus",           'i', ""         },
	{0XA2246614, &WrapI_V<sceNetAdhocDiscoverTerm>,                    "sceNetAdhocDiscoverTerm",                'i', ""         },
	{0XF7D13214, &WrapI_V<sceNetAdhocDiscoverStop>,                    "sceNetAdhocDiscoverStop",                'i', ""         },
	{0XA423A21B, &WrapI_V<sceNetAdhocDiscoverRequestSuspend>,          "sceNetAdhocDiscoverRequestSuspend",      'i', ""         },
};

void Register_sceNetAdhoc() {
	RegisterModule("sceNetAdhoc", ARRAY_SIZE(sceNetAdhoc), sceNetAdhoc);
	RegisterModule("sceNetAdhocMatching", ARRAY_SIZE(sceNetAdhocMatching), sceNetAdhocMatching);
	RegisterModule("sceNetAdhocDiscover", ARRAY_SIZE(sceNetAdhocDiscover), sceNetAdhocDiscover);
	RegisterModule("sceNetAdhocctl", ARRAY_SIZE(sceNetAdhocctl), sceNetAdhocctl);
}

/**
* Broadcast Ping Message to other Matching Users
* @param context Matching Context Pointer
*/
void broadcastPingMessage(SceNetAdhocMatchingContext * context)
{
	// Ping Opcode
	uint8_t ping = PSP_ADHOC_MATCHING_PACKET_PING;

	// Send Broadcast
	// FIXME: Not sure whether this PING supposed to be sent only to AdhocMatching members or to everyone in Adhocctl Group, since we already pinging the AdhocServer to avoid getting kicked out of Adhocctl Group
	peerlock.lock();
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
		sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac_addr, port, &ping, sizeof(ping), 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();
	}
	peerlock.unlock();
}

/**
* Broadcast Hello Message to other Matching Users
* @param context Matching Context Pointer
*/
void broadcastHelloMessage(SceNetAdhocMatchingContext * context)
{
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

	// Allocated Hello Message Buffer
	if (hello != NULL)
	{
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
		DEBUG_LOG(SCENET, "HELLO Dump (%d bytes):\n%s", context->hellolen, hellohex.c_str());

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
			sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac_addr, port, hello, 5 + context->hellolen, 0, ADHOC_F_NONBLOCK);
			context->socketlock->unlock();
		}
		peerlock.unlock();

		// Free Memory, not needed since it may be reused again later
		//free(hello);
	}
}

/**
* Send Accept Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendAcceptPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int optlen, void * opt)
{
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	// Found Peer
	if (peer != NULL && (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_P2P))
	{
		// Required Sibling Buffer
		uint32_t siblingbuflen = 0;

		// Parent Mode
		if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) siblingbuflen = (u32)sizeof(SceNetEtherAddr) * (countConnectedPeers(context) - 2);

		// Sibling Count
		int siblingcount = siblingbuflen / sizeof(SceNetEtherAddr);

		// Allocate Accept Message Buffer
		uint8_t * accept = (uint8_t *)malloc(9LL + optlen + siblingbuflen);

		// Allocated Accept Message Buffer
		if (accept != NULL)
		{
			// Accept Opcode
			accept[0] = PSP_ADHOC_MATCHING_PACKET_ACCEPT;

			// Optional Data Length
			memcpy(accept + 1, &optlen, sizeof(optlen));

			// Sibling Count
			memcpy(accept + 5, &siblingcount, sizeof(siblingcount));

			// Copy Optional Data
			if (optlen > 0) memcpy(accept + 9, opt, optlen);

			// Parent Mode Extra Data required
			if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && siblingcount > 0)
			{
				// Create MAC Array Pointer
				uint8_t * siblingmacs = (uint8_t *)(accept + 9 + optlen);

				// MAC Writing Pointer
				int i = 0;

				// Iterate Peer List
				SceNetAdhocMatchingMemberInternal * item = context->peerlist; 
				for (; item != NULL; item = item->next)
				{
					// Ignore Target
					if (item == peer) continue;

					// Copy Child MAC
					if (item->state == PSP_ADHOC_MATCHING_PEER_CHILD)
					{
						// Clone MAC the stupid memcpy way to shut up PSP CPU
						memcpy(siblingmacs + sizeof(SceNetEtherAddr) * i++, &item->mac, sizeof(SceNetEtherAddr));
					}
				}
			}

			// Send Data
			context->socketlock->lock();
			sceNetAdhocPdpSend(context->socket, (const char*)mac, (*context->peerPort)[*mac], accept, 9 + optlen + siblingbuflen, 0, ADHOC_F_NONBLOCK);
			context->socketlock->unlock();

			// Free Memory
			free(accept);

			// Spawn Local Established Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, mac, 0, NULL);
		}
	}
}

/**
* Send Join Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendJoinPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int optlen, void * opt)
{
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	// Valid Peer
	if (peer != NULL && peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST)
	{
		// Allocate Join Message Buffer
		uint8_t * join = (uint8_t *)malloc(5LL + optlen);

		// Allocated Join Message Buffer
		if (join != NULL)
		{
			// Join Opcode
			join[0] = PSP_ADHOC_MATCHING_PACKET_JOIN;

			// Optional Data Length
			memcpy(join + 1, &optlen, sizeof(optlen));

			// Copy Optional Data
			if (optlen > 0) memcpy(join + 5, opt, optlen);

			// Send Data
			context->socketlock->lock();
			sceNetAdhocPdpSend(context->socket, (const char*)mac, (*context->peerPort)[*mac], join, 5 + optlen, 0, ADHOC_F_NONBLOCK);
			context->socketlock->unlock();

			// Free Memory
			free(join);
		}
	}
}

/**
* Send Cancel Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendCancelPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int optlen, void * opt)
{
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Allocate Cancel Message Buffer
	uint8_t * cancel = (uint8_t *)malloc(5LL + optlen);

	// Allocated Cancel Message Buffer
	if (cancel != NULL)
	{
		// Cancel Opcode
		cancel[0] = PSP_ADHOC_MATCHING_PACKET_CANCEL;

		// Optional Data Length
		memcpy(cancel + 1, &optlen, sizeof(optlen));

		// Copy Optional Data
		if (optlen > 0) memcpy(cancel + 5, opt, optlen);

		// Send Data
		context->socketlock->lock();
		sceNetAdhocPdpSend(context->socket, (const char*)mac, (*context->peerPort)[*mac], cancel, 5 + optlen, 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();

		// Free Memory
		free(cancel);
	}

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	// Found Peer
	if (peer != NULL)
	{
		// Child Mode Fallback - Delete All
		if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
		{
			// Delete Peer List
			clearPeerList(context);
		}

		// Delete Peer
		else deletePeer(context, peer);
	}
}

/**
* Send Bulk Data Packet to Player
* @param context Matching Context Pointer
* @param mac Target Player MAC
* @param datalen Data Length
* @param data Data
*/
void sendBulkDataPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac, int datalen, void * data)
{
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	// Valid Peer (rest is already checked in send.c)
	if (peer != NULL)
	{
		// Don't send if it's aborted
		//if (peer->sending == 0) return;

		// Allocate Send Message Buffer
		uint8_t * send = (uint8_t *)malloc(5LL + datalen);

		// Allocated Send Message Buffer
		if (send != NULL)
		{
			// Send Opcode
			send[0] = PSP_ADHOC_MATCHING_PACKET_BULK;

			// Data Length
			memcpy(send + 1, &datalen, sizeof(datalen));

			// Copy Data
			memcpy(send + 5, data, datalen);

			// Send Data
			context->socketlock->lock();
			sceNetAdhocPdpSend(context->socket, (const char*)mac, (*context->peerPort)[*mac], send, 5 + datalen, 0, ADHOC_F_NONBLOCK);
			context->socketlock->unlock();

			// Free Memory
			free(send);

			// Remove Busy Bit from Peer
			peer->sending = 0;

			// Spawn Data Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_DATA_ACK, mac, 0, NULL);
		}
	}
}

/**
* Tell Established Peers of new Child
* @param context Matching Context Pointer
* @param mac New Child's MAC
*/
void sendBirthPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac)
{
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find Newborn Child
	SceNetAdhocMatchingMemberInternal * newborn = findPeer(context, mac);

	// Found Newborn Child
	if (newborn != NULL)
	{
		// Packet Buffer
		uint8_t packet[7];

		// Set Opcode
		packet[0] = PSP_ADHOC_MATCHING_PACKET_BIRTH;

		// Set Newborn MAC
		memcpy(packet + 1, mac, sizeof(SceNetEtherAddr));

		// Iterate Peers
		SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
		for (; peer != NULL; peer = peer->next)
		{
			// Skip Newborn Child
			if (peer == newborn) continue;

			// Send only to children
			if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD)
			{
				// Send Packet
				context->socketlock->lock();
				int sent = sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], packet, sizeof(packet), 0, ADHOC_F_NONBLOCK);
				context->socketlock->unlock();

				// Log Send Success
				if (sent >= 0) 
					INFO_LOG(SCENET, "InputLoop: Sending BIRTH [%s] to %s", mac2str(mac).c_str(), mac2str(&peer->mac).c_str());
				else
					WARN_LOG(SCENET, "InputLoop: Failed to Send BIRTH [%s] to %s", mac2str(mac).c_str(), mac2str(&peer->mac).c_str());
			}
		}
	}
}

/**
* Tell Established Peers of abandoned Child
* @param context Matching Context Pointer
* @param mac Dead Child's MAC
*/
void sendDeathPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac)
{
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Find abandoned Child
	SceNetAdhocMatchingMemberInternal * deadkid = findPeer(context, mac);

	// Found abandoned Child
	if (deadkid != NULL)
	{
		// Packet Buffer
		uint8_t packet[7];

		// Set abandoned Child MAC
		memcpy(packet + 1, mac, sizeof(SceNetEtherAddr));

		// Iterate Peers
		SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
		for (; peer != NULL; peer = peer->next)
		{
			// Skip dead Child? Or May be we should also tells the disconnected Child, that they have been disconnected from the Host (in the case they were disconnected because they went to PPSSPP settings for too long)
			if (peer == deadkid) {
				// Set Opcode
				packet[0] = PSP_ADHOC_MATCHING_PACKET_BYE;

				// Send Bye Packet
				context->socketlock->lock();
				sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], packet, sizeof(packet[0]), 0, ADHOC_F_NONBLOCK);
				context->socketlock->unlock();
			}
			else
			// Send to other children
			if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD)
			{
				// Set Opcode
				packet[0] = PSP_ADHOC_MATCHING_PACKET_DEATH;

				// Send Death Packet
				context->socketlock->lock();
				sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], packet, sizeof(packet), 0, ADHOC_F_NONBLOCK);
				context->socketlock->unlock();
			}
		}

		// Delete Peer
		deletePeer(context, deadkid);
	}
}

/**
* Tell Established Peers that we're shutting the Networking Layer down
* @param context Matching Context Pointer
*/
void sendByePacket(SceNetAdhocMatchingContext * context)
{
	// Lock the peer
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Iterate Peers
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	for (; peer != NULL; peer = peer->next)
	{
		// Peer of Interest
		if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT || peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_P2P)
		{
			// Bye Opcode
			uint8_t opcode = PSP_ADHOC_MATCHING_PACKET_BYE;

			// Send Bye Packet
			context->socketlock->lock();
			sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac, (*context->peerPort)[peer->mac], &opcode, sizeof(opcode), 0, ADHOC_F_NONBLOCK);
			context->socketlock->unlock();
		}
	}
}

/**
* Handle Ping Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
*/
void actOnPingPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac)
{
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// Found Peer
	if (peer != NULL)
	{
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
void actOnHelloPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length)
{
	// Interested in Hello Data
	if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && findParent(context) == NULL) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL))
	{
		// Complete Packet Header available
		if (length >= 5)
		{
			// Extract Optional Data Length
			int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

			// Complete Valid Packet available
			if (optlen >= 0 && length >= (5 + optlen))
			{
				// Set Default Null Data
				void * opt = NULL;

				// Extract Optional Data Pointer
				if (optlen > 0) opt = context->rxbuf + 5; 

				// Find Peer
				SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

				// Peer not found
				if (peer == NULL)
				{
					// Allocate Memory
					peer = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

					// Allocated Memory
					if (peer != NULL)
					{
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
				if (peer != NULL && peer->state != PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST && peer->state != PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)
				{
					std::string hellohex;
					DataToHexString(10, 0, (u8*)opt, optlen, &hellohex);
					DEBUG_LOG(SCENET, "HELLO Dump (%d bytes):\n%s", optlen, hellohex.c_str());

					// Spawn Hello Event. FIXME: HELLO event should not be triggered in the middle of joining? This will cause Bleach 7 to Cancel the join request
					spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_HELLO, sendermac, optlen, opt);
				}
			}
		}
	}
}

/**
* Handle Join Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnJoinPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length)
{
	// Not a child mode context
	if (context->mode != PSP_ADHOC_MATCHING_MODE_CHILD)
	{
		// We still got a unoccupied slot in our room (Parent / P2P)
		if ((context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && countChildren(context) < (context->maxpeers - 1)) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL))
		{
			// Complete Packet Header available
			if (length >= 5)
			{
				// Extract Optional Data Length
				int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

				// Complete Valid Packet available
				if (optlen >= 0 && length >= (5 + optlen))
				{
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
						WARN_LOG(SCENET, "Join Event(2) Ignored");
						return;
					}

					// New Peer
					if (peer == NULL)
					{
						// Allocate Memory
						peer = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

						// Allocated Memory
						if (peer != NULL)
						{
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
					else
					{
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
		WARN_LOG(SCENET, "Join Event(2) Rejected");
		// Auto-Reject Player
		sendCancelPacket(context, sendermac, 0, NULL);
	}
}

/**
* Handle Accept Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnAcceptPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, uint32_t length)
{
	// Not a parent context
	if (context->mode != PSP_ADHOC_MATCHING_MODE_PARENT)
	{
		// Don't have a master yet
		if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && findParent(context) == NULL) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL))
		{
			// Complete Packet Header available
			if (length >= 9)
			{
				// Extract Optional Data Length
				int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

				// Extract Sibling Count
				int siblingcount = 0; memcpy(&siblingcount, context->rxbuf + 5, sizeof(siblingcount));

				// Complete Valid Packet available
				if (optlen >= 0 && length >= (9LL + optlen + static_cast<long long>(sizeof(SceNetEtherAddr)) * siblingcount))
				{
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

					// We are waiting for a answer to our request...
					if (request != NULL)
					{
						// Find Peer
						SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

						// It's the answer we wanted!
						if (request == peer)
						{
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
				}
			}
		}
	}
}

/**
* Handle Cancel Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnCancelPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length)
{
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// Interest Condition fulfilled
	if (peer != NULL)
	{
		// Complete Packet Header available
		if (length >= 5)
		{
			// Extract Optional Data Length
			int optlen = 0; memcpy(&optlen, context->rxbuf + 1, sizeof(optlen));

			// Complete Valid Packet available
			if (optlen >= 0 && length >= (5 + optlen))
			{
				// Set Default Null Data
				void * opt = NULL;

				// Extract Optional Data Pointer
				if (optlen > 0) opt = context->rxbuf + 5;

				// Get Outgoing Join Request
				SceNetAdhocMatchingMemberInternal* request = findOutgoingRequest(context);

				// Child Mode
				if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
				{
					// Get Parent
					SceNetAdhocMatchingMemberInternal* parent = findParent(context);

					// Join Request denied
					if (request == peer)
					{
						// Spawn Deny Event
						spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_DENY, sendermac, optlen, opt);

						// Delete Peer from List
						deletePeer(context, peer);
					}

					// Kicked from Room
					else if (parent == peer)
					{
						// Iterate Peers
						SceNetAdhocMatchingMemberInternal * item = context->peerlist; 
						for (; item != NULL; item = item->next)
						{
							// Established Peer
							if (item->state == PSP_ADHOC_MATCHING_PEER_CHILD || item->state == PSP_ADHOC_MATCHING_PEER_PARENT)
							{
								// Spawn Leave / Kick Event
								spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, &item->mac, optlen, opt);
							}
						}

						// Delete Peer from List
						clearPeerList(context);
					}
				}

				// Parent Mode
				else if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT)
				{
					// Cancel Join Request
					if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)
					{
						// Spawn Request Cancel Event
						spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_CANCEL, sendermac, optlen, opt);

						// Delete Peer from List
						deletePeer(context, peer);
					}

					// Leave Room
					else if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD)
					{
						// Spawn Leave / Kick Event
						spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, sendermac, optlen, opt);

						// Delete Peer from List
						deletePeer(context, peer);
					}
				}

				// P2P Mode
				else
				{
					// Get P2P Partner
					SceNetAdhocMatchingMemberInternal* p2p = findP2P(context);

					// Join Request denied
					if (request == peer)
					{
						// Spawn Deny Event
						spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_DENY, sendermac, optlen, opt);

						// Delete Peer from List
						deletePeer(context, peer);
					}

					// Kicked from Room
					else if (p2p == peer)
					{
						// Spawn Leave / Kick Event
						spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, sendermac, optlen, opt);

						// Delete Peer from List
						deletePeer(context, peer);
					}

					// Cancel Join Request
					else if (peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST)
					{
						// Spawn Request Cancel Event
						spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_CANCEL, sendermac, optlen, opt);

						// Delete Peer from List
						deletePeer(context, peer);
					}
				}
			}
		}
	}
}

/**
* Handle Bulk Data Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnBulkDataPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, int32_t length)
{
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// Established Peer
	if (peer != NULL && (
		(context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)) ||
		(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && peer->state == PSP_ADHOC_MATCHING_PEER_P2P)))
	{
		// Complete Packet Header available
		if (length > 5)
		{
			// Extract Data Length
			int datalen = 0; memcpy(&datalen, context->rxbuf + 1, sizeof(datalen));

			// Complete Valid Packet available
			if (datalen > 0 && length >= (5 + datalen))
			{
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
void actOnBirthPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, uint32_t length)
{
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// Valid Circumstances
	if (peer != NULL && context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer == findParent(context))
	{
		// Complete Packet available
		if (length >= (1 + sizeof(SceNetEtherAddr)))
		{
			// Extract Child MAC
			SceNetEtherAddr mac;
			memcpy(&mac, context->rxbuf + 1, sizeof(SceNetEtherAddr));

			// Allocate Memory
			SceNetAdhocMatchingMemberInternal * sibling = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

			// Allocated Memory
			if (sibling != NULL)
			{
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
		}
	}
}

/**
* Handle Death Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
* @param length Packet Length
*/
void actOnDeathPacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac, uint32_t length)
{
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// Valid Circumstances
	if (peer != NULL && context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer == findParent(context))
	{
		// Complete Packet available
		if (length >= (1 + sizeof(SceNetEtherAddr)))
		{
			// Extract Child MAC
			SceNetEtherAddr mac;
			memcpy(&mac, context->rxbuf + 1, sizeof(SceNetEtherAddr));

			// Find Peer
			SceNetAdhocMatchingMemberInternal * deadkid = findPeer(context, &mac);

			// Valid Sibling
			if (deadkid->state == PSP_ADHOC_MATCHING_PEER_CHILD)
			{
				// Spawn Leave Event
				spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_LEAVE, &mac, 0, NULL);

				// Delete Peer
				deletePeer(context, deadkid);
			}
		}
	}
}

/**
* Handle Bye Packet
* @param context Matching Context Pointer
* @param sendermac Packet Sender MAC
*/
void actOnByePacket(SceNetAdhocMatchingContext * context, SceNetEtherAddr * sendermac)
{
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, sendermac);

	// We know this guy
	if (peer != NULL)
	{
		// P2P or Child Bye
		if ((context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
			(context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
			(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && peer->state == PSP_ADHOC_MATCHING_PEER_P2P))
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
		else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)
		{
			// Spawn Leave / Kick Event. FIXME: DISCONNECT event should only be triggered on Parent/P2P mode and for Parent/P2P peer?
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_BYE, sendermac, 0, NULL);

			// Delete Peer from List
			clearPeerList(context);
		}
	}
}


/**
* Matching Event Dispatcher Thread
* @param args sizeof(SceNetAdhocMatchingContext *)
* @param argp SceNetAdhocMatchingContext *
* @return Exit Point is never reached...
*/
int matchingEventThread(int matchingId) 
{
	SetCurrentThreadName("MatchingEvent");
	// Multithreading Lock
	peerlock.lock();
	// Cast Context
	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);
	// Multithreading Unlock
	peerlock.unlock();

	// Log Startup
	INFO_LOG(SCENET, "EventLoop: Begin of EventLoop[%i] Thread", matchingId);

	// Run while needed...
	if (context != NULL) {
		u32 bufLen = context->rxbuflen; //0;
		u32 bufAddr = 0; //= userMemory.Alloc(bufLen); //context->rxbuf;
		u32_le * args = context->handlerArgs; //MatchingArgs

		while (contexts != NULL && context->eventRunning)
		{
			// Multithreading Lock
			peerlock.lock();
			// Cast Context
			context = findMatchingContext(matchingId);
			// Multithreading Unlock
			peerlock.unlock();

			// Messages on Stack ready for processing
			if (context != NULL && context->event_stack != NULL)
			{
				// Claim Stack
				context->eventlock->lock();

				// Iterate Message List
				ThreadMessage * msg = context->event_stack; 
				if (msg != NULL)
				{
					// Default Optional Data
					void* opt = NULL;

					// Grab Optional Data
					if (msg->optlen > 0) opt = ((u8*)msg) + sizeof(ThreadMessage); //&msg[1]

					// Log Matching Events
					INFO_LOG(SCENET, "EventLoop[%d]: Matching Event [%d=%s][%s] OptSize=%d", matchingId, msg->opcode, getMatchingEventStr(msg->opcode), mac2str(&msg->mac).c_str(), msg->optlen);

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
			sleep_ms(10); //1 //sceKernelDelayThread(10000);

			// Don't do anything if it's paused, otherwise the log will be flooded
			while (Core_IsStepping() && coreState != CORE_POWERDOWN && contexts != NULL && context->eventRunning) sleep_ms(10);
		}

		// Process Last Messages
		if (contexts != NULL && context->event_stack != NULL)
		{
			// Claim Stack
			context->eventlock->lock();

			// Iterate Message List
			int msg_count = 0;
			ThreadMessage * msg = context->event_stack; 
			for (; msg != NULL; msg = msg->next)
			{
				// Default Optional Data
				void * opt = NULL;

				// Grab Optional Data
				if (msg->optlen > 0) opt = ((u8 *)msg) + sizeof(ThreadMessage); //&msg[1]

				INFO_LOG(SCENET, "EventLoop[%d]: Matching Event [EVENT=%d]\n", matchingId, msg->opcode);

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
			INFO_LOG(SCENET, "EventLoop[%d]: Finished (%d msg)", matchingId, msg_count);
		}

		// Free memory
		//if (Memory::IsValidAddress(bufAddr)) userMemory.Free(bufAddr);

		// Delete Pointer Reference (and notify caller about finished cleanup)
		//context->eventThread = NULL;
	}

	// Log Shutdown
	INFO_LOG(SCENET, "EventLoop: End of EventLoop[%i] Thread", matchingId);

	// Return Zero to shut up Compiler
	return 0;
}

/**
* Matching IO Handler Thread
* @param args sizeof(SceNetAdhocMatchingContext *)
* @param argp SceNetAdhocMatchingContext *
* @return Exit Point is never reached...
*/
int matchingInputThread(int matchingId) // TODO: The MatchingInput thread is using sceNetAdhocPdpRecv & sceNetAdhocPdpSend functions so it might be better to run this on PSP thread instead of real thread
{
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
	INFO_LOG(SCENET, "InputLoop: Begin of InputLoop[%i] Thread", matchingId);

	// Run while needed...
	if (context != NULL) {
		while (contexts != NULL && context->inputRunning)
		{
			// Multithreading Lock
			peerlock.lock();
			// Cast Context
			context = findMatchingContext(matchingId);
			// Multithreading Unlock
			peerlock.unlock();

			if (context != NULL) {
				now = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0;

				// Hello Message Sending Context with unoccupied Slots
				if ((context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && (countChildren(context) < (context->maxpeers - 1))) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL))
				{
					// Hello Message Broadcast necessary because of Hello Interval
					if (context->hello_int > 0)
						if (static_cast<s64>(now - lasthello) >= static_cast<s64>(context->hello_int))
						{
							// Broadcast Hello Message
							broadcastHelloMessage(context);

							// Update Hello Timer
							lasthello = now;
						}
				}

				// Ping Required
				if (context->keepalive_int > 0)
					if (static_cast<s64>(now - lastping) >= static_cast<s64>(context->keepalive_int))
					{
						// Broadcast Ping Message
						broadcastPingMessage(context);

						// Update Ping Timer
						lastping = now;
					}

				// Messages on Stack ready for processing
				if (context->input_stack != NULL)
				{
					// Claim Stack
					context->inputlock->lock();

					// Iterate Message List
					ThreadMessage* msg = context->input_stack;
					while (msg != NULL)
					{
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
				if (recvresult == 0 && rxbuflen > 0)
				{
					// Log Receive Success
					if (context->rxbuf[0] > 1) {
						INFO_LOG(SCENET, "InputLoop[%d]: Received %d Bytes (Opcode[%d]=%s)", matchingId, rxbuflen, context->rxbuf[0], getMatchingOpcodeStr(context->rxbuf[0]));
					}

					// Update Peer Timestamp
					peerlock.lock();
					SceNetAdhocctlPeerInfo* peer = findFriend(&sendermac);
					if (peer != NULL) {
						now = CoreTiming::GetGlobalTimeUsScaled();
						s64 delta = now - peer->last_recv;
						DEBUG_LOG(SCENET, "Timestamp LastRecv Delta: %lld (%llu - %llu) from %s", delta, now, peer->last_recv, mac2str(&sendermac).c_str());
						if (peer->last_recv != 0) peer->last_recv = std::max(peer->last_recv, now - defaultLastRecvDelta);
					}
					else {
						WARN_LOG(SCENET, "InputLoop[%d]: Unknown Peer[%s:%u] (Recved=%i, Length=%i)", matchingId, mac2str(&sendermac).c_str(), senderport, recvresult, rxbuflen);
					}

					// Show a warning if other player is having their port being re-mapped, thus that other player may have issue with the communication. 
					// Note: That other player may need to switch side between host and join, or reboot their router to solve this issue.
					if (context->port != senderport && senderport != (*context->peerPort)[sendermac]) {
						char name[9] = {};
						if (peer != NULL)
							truncate_cpy(name, sizeof(name), (const char*)peer->nickname.data);
						WARN_LOG(SCENET, "InputLoop[%d]: Unknown Source Port from [%s][%s:%u -> %u] (Recved=%i, Length=%i)", matchingId, name, mac2str(&sendermac).c_str(), senderport, context->port, recvresult, rxbuflen);
						g_OSD.Show(OSDType::MESSAGE_WARNING, std::string(n->T("AM: Data from Unknown Port")) + std::string(" [") + std::string(name) + std::string("]:") + std::to_string(senderport) + std::string(" -> ") + std::to_string(context->port) + std::string(" (") + std::to_string(portOffset) + std::string(")"));
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

				// Handle Peer Timeouts
				handleTimeout(context);
			}
			// Share CPU Time
			sleep_ms(10); //1 //sceKernelDelayThread(10000);

			// Don't do anything if it's paused, otherwise the log will be flooded
			while (Core_IsStepping() && coreState != CORE_POWERDOWN && contexts != NULL && context->inputRunning) sleep_ms(10);
		}

		if (contexts != NULL) {
			// Process Last Messages
			if (context->input_stack != NULL)
			{
				// Claim Stack
				context->inputlock->lock();

				// Iterate Message List
				int msg_count = 0;
				ThreadMessage* msg = context->input_stack;
				while (msg != NULL)
				{
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
				INFO_LOG(SCENET, "InputLoop[%d]: Finished (%d msg)", matchingId, msg_count);
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
	INFO_LOG(SCENET, "InputLoop: End of InputLoop[%i] Thread", matchingId);

	// Terminate Thread
	//sceKernelExitDeleteThread(0);

	// Return Zero to shut up Compiler
	return 0;
}

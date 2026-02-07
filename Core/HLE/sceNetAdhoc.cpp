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
// sceNetAdhoc

// This is a direct port of Coldbird's code from http://code.google.com/p/aemu/
// All credit goes to him!


#include <mutex>
#include <string>

#include "Common/Net/SocketCompat.h"
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
#include "Core/CoreTiming.h"
#include "Core/Core.h"
#include "Core/Reporting.h"
#include "Core/MemMapHelpers.h"

#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/ErrorCodes.h"
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
#include "Core/HLE/NetAdhocCommon.h"

#include "ext/aemu_postoffice/client/postoffice_client.h"

#ifdef _WIN32
#undef errno
#define errno WSAGetLastError()
#endif

// shared in sceNetAdhoc.h since it need to be used from sceNet.cpp also
// TODO: Make accessor functions instead, and throw all this state in a struct.
bool netAdhocInited;
bool netAdhocctlInited;

#define DISCOVER_DURATION_US	2000000 // 2 seconds is probably the normal time it takes for PSP to connect to a group (ie. similar to NetconfigDialog time)
u64 netAdhocDiscoverStartTime = 0;
s32 netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_NONE;
bool netAdhocDiscoverIsStopping = false;
SceNetAdhocDiscoverParam* netAdhocDiscoverParam = nullptr;
u32 netAdhocDiscoverBufAddr = 0;

bool netAdhocGameModeEntered = false;

SceUID threadAdhocID;

std::deque<std::pair<u32, u32>> adhocctlEvents;
std::map<int, AdhocctlHandler> adhocctlHandlers;
int adhocctlNotifyEvent = -1;
int adhocctlStateEvent = -1;
int adhocSocketNotifyEvent = -1;
std::map<int, AdhocctlRequest> adhocctlRequests;
std::map<u64, AdhocSocketRequest> adhocSocketRequests;
std::map<u64, AdhocSendTargets> sendTargetPeers;
bool serverHasRelay = false;

int gameModeNotifyEvent = -1;

#define AEMU_POSTOFFICE_PORT 27313
#define AEMU_POSTOFFICE_ID_BASE 2048

int AcceptPtpSocket(int ptpId, int newsocket, sockaddr_in& peeraddr, SceNetEtherAddr* addr, u16_le* port);
int PollAdhocSocket(SceNetAdhocPollSd* sds, int count, int timeout, int nonblock);
int FlushPtpSocket(int socketId);
int RecreatePtpSocket(int ptpId);
int NetAdhocGameMode_DeleteMaster();
int NetAdhocctl_ExitGameMode();
int NetAdhocPtp_Connect(int id, int timeout, int flag, bool allowForcedConnect = true);

// Forward declarations for the savestate mechanism (the matching is sadly not inside its own section)
void deleteMatchingEvents(const int matchingId = -1);
void DoNetAdhocMatchingInited(PointerWrap &p);
void DoNetAdhocMatchingThreads(PointerWrap &p);
void ZeroNetAdhocMatchingThreads();
void SaveNetAdhocMatchingInited();
void RestoreNetAdhocMatchingInited();

bool __NetAdhocConnected() {
	return netAdhocInited && netAdhocctlInited && (adhocctlState == ADHOCCTL_STATE_CONNECTED || adhocctlState == ADHOCCTL_STATE_GAMEMODE);
}

void __NetAdhocShutdown() {
	// Kill AdhocServer Thread
	adhocServerRunning = false;
	if (adhocServerThread.joinable()) {
		adhocServerThread.join();
	}

	NetAdhocctl_Term();

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
					ERROR_LOG(Log::sceNet, "GameMode: Failed to create socket (Error %08x)", gameModeSocket);
					__KernelResumeThreadFromWait(threadID, gameModeSocket);
					return;
				}
				else
					INFO_LOG(Log::sceNet, "GameMode: Synchronizer (%d, %d) has started", gameModeSocket, gameModeBuffSize);
			}
			if (gameModeSocket < 0) {
				// ReSchedule
				CoreTiming::ScheduleEvent(usToCycles(GAMEMODE_UPDATE_INTERVAL) - cyclesLate, gameModeNotifyEvent, userdata);
				return;
			}
			auto sock = adhocSockets[gameModeSocket - 1];
			if (!sock) {
				WARN_LOG(Log::sceNet, "GameMode: Socket (%d) got deleted", gameModeSocket);
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
					if (!gma.dataSent && (serverHasRelay || IsSocketReady(sock->data.pdp.id, false, true) > 0)) {
						u16_le port = ADHOC_GAMEMODE_PORT;
						auto it = gameModePeerPorts.find(gma.mac);
						if (it != gameModePeerPorts.end())
							port = it->second;

						int sent = hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, gameModeSocket, (const char*)&gma.mac, port, masterGameModeArea.data, masterGameModeArea.size, 0, ADHOC_F_NONBLOCK);
						if (sent != SCE_NET_ADHOC_ERROR_WOULD_BLOCK) {
							gma.dataSent = 1;
							DEBUG_LOG(Log::sceNet, "GameMode: Master data Sent %d bytes to Area #%d [%s]", masterGameModeArea.size, gma.id, mac2str(&gma.mac).c_str());
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

								hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, gameModeSocket, (const char*)&gma.mac, port, masterGameModeArea.data, masterGameModeArea.size, 0, ADHOC_F_NONBLOCK);
							}
						}
					}
					// Resume blocked thread
					u64 now = CoreTiming::GetGlobalTimeUsScaled();
					if (recvd == replicaGameModeAreas.size()) {
						u32 waitVal = __KernelGetWaitValue(threadID, error);
						if (error == 0) {
							DEBUG_LOG(Log::sceNet, "GameMode: Resuming Thread %d after Master data Synced (Result = %08x)", threadID, waitVal);
							__KernelResumeThreadFromWait(threadID, waitVal);
						}
						else
							ERROR_LOG(Log::sceNet, "GameMode: Error (%08x) on WaitValue %d ThreadID %d", error, waitVal, threadID);
					}
					// Attempt to Re-Send initial Master data (in case previous packets were lost)
					else if (static_cast<s64>(now - masterGameModeArea.updateTimestamp) > GAMEMODE_SYNC_TIMEOUT) {
						DEBUG_LOG(Log::sceNet, "GameMode: Attempt to Re-Send Master data after Sync Timeout (%d us)", GAMEMODE_SYNC_TIMEOUT);
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
			if (serverHasRelay || IsSocketReady(sock->data.pdp.id, true, false) > 0) {
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
						WARN_LOG(Log::sceNet, "GameMode: Unknown Source Port from [%s][%s:%u -> %u] (Result=%i, Size=%i)", name, mac2str(&sendermac).c_str(), senderport, ADHOC_GAMEMODE_PORT, ret, bufsz);
						g_OSD.Show(OSDType::MESSAGE_WARNING, std::string(n->T("GM: Data from Unknown Port")) + std::string(" [") + std::string(name) + std::string("]:") + std::to_string(senderport) + std::string(" -> ") + std::to_string(ADHOC_GAMEMODE_PORT) + std::string(" (") + std::to_string(portOffset) + std::string(")"), 0.0f, "unknownport");
						peerlock.unlock();
					}
					// Keeping track of the source port for further communication, in case it was re-mapped by router or ISP for some reason.
					gameModePeerPorts[sendermac] = senderport;

					for (auto& gma : replicaGameModeAreas) {
						if (IsMatch(gma.mac, sendermac)) {
							DEBUG_LOG(Log::sceNet, "GameMode: Replica data Received %d bytes for Area #%d [%s]", bufsz, gma.id, mac2str(&sendermac).c_str());
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
	INFO_LOG(Log::sceNet, "GameMode Scheduler (%d, %d) has ended", gameModeSocket, gameModeBuffSize);
	u32 waitVal = __KernelGetWaitValue(threadID, error);
	if (error == 0) {
		DEBUG_LOG(Log::sceNet, "GameMode: Resuming Thread %d after Master Deleted (Result = %08x)", threadID, waitVal);
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
		WARN_LOG(Log::sceNet, "sceNetAdhocctl Socket WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
		return;
	}

	// Socket not found?! Should never happen! But if it ever happened (ie. loaded from SaveState where adhocctlRequests got cleared) return BUSY and let the game try again.
	if (adhocctlRequests.find(uid) == adhocctlRequests.end()) {
		WARN_LOG(Log::sceNet, "sceNetAdhocctl Socket WaitID(%i) not found!", uid);
		__KernelResumeThreadFromWait(threadID, SCE_NET_ADHOCCTL_ERROR_BUSY);
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
				sockerr = socket_errno;
				// Successfully Sent or Connection has been closed or Connection failure occurred
				if (ret >= 0 || (ret == SOCKET_ERROR && sockerr != EAGAIN && sockerr != EWOULDBLOCK)) {
					// Prevent from sending again
					req.opcode = 0;
					if (ret == SOCKET_ERROR)
						DEBUG_LOG(Log::sceNet, "sceNetAdhocctl[%i]: Socket Error (%i)", uid, sockerr);
				}
			}
		}

		// Retry until successfully sent. Login packet sent after successfully connected to Adhoc Server (indicated by networkInited), so we're not sending Login again here
		if ((req.opcode == OPCODE_LOGIN && !g_adhocServerConnected) || (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK))) {
			u64 now = (u64)(time_now_d() * 1000000.0);
			if (now - adhocctlStartTime <= static_cast<u64>(adhocDefaultTimeout) + 500) {
				// Try again in another 0.5ms until timedout.
				CoreTiming::ScheduleEvent(usToCycles(500) - cyclesLate, adhocctlNotifyEvent, userdata);
				return;
			}
			else if (req.opcode != OPCODE_LOGIN)
				result = SCE_NET_ADHOCCTL_ERROR_BUSY;
		}
	}
	else
		result = SCE_NET_ADHOCCTL_ERROR_WLAN_SWITCH_OFF;

	u32 waitVal = __KernelGetWaitValue(threadID, error);
	__KernelResumeThreadFromWait(threadID, result);
	DEBUG_LOG(Log::sceNet, "Returning (WaitID: %d, error: %08x) Result (%08x) of sceNetAdhocctl - Opcode: %d, State: %d", waitID, error, (int)result, waitVal, adhocctlState);

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
		WARN_LOG(Log::sceNet, "sceNetAdhocctl State WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
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
	DEBUG_LOG(Log::sceNet, "Returning (WaitID: %d, error: %08x) Result (%08x) of sceNetAdhocctl - Event: %d, State: %d", waitID, error, (int)result, event, adhocctlState);
}

// Used to simulate blocking on metasocket when send OP code to AdhocServer
int WaitBlockingAdhocctlSocket(AdhocctlRequest request, int usec, const char* reason) {
	int uid = (metasocket <= 0) ? 1 : (int)metasocket;

	if (adhocctlRequests.find(uid) != adhocctlRequests.end()) {
		WARN_LOG(Log::sceNet, "sceNetAdhocctl - WaitID[%d] already existed, Socket is busy!", uid);
		return SCE_NET_ADHOCCTL_ERROR_BUSY;
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
	INFO_LOG(Log::sceNet, "Initiating GameMode Scheduler");
	if (CoreTiming::IsScheduled(gameModeNotifyEvent)) {
		WARN_LOG(Log::sceNet, "GameMode Scheduler is already running!");
		return -1;
	}
	u64 param = ((u64)__KernelGetCurThread()) << 32;
	CoreTiming::ScheduleEvent(usToCycles(GAMEMODE_INIT_DELAY), gameModeNotifyEvent, param);
	return 0;
}

static uint16_t offset_port_simple(uint16_t port) {
	if (port == 0) {
		return 0;
	}
	port += portOffset;
	if (port == 0) {
		return 65535;
	}
	return port;
}

static uint16_t reverse_port_simple(uint16_t port) {
	return port - portOffset;
}

static void *pdp_postoffice_recover(int idx) {
	AdhocSocket *internal = adhocSockets[idx];
	if (internal->postofficeHandle != NULL) {
		return internal->postofficeHandle;
	}

	INFO_LOG(Log::sceNet, "%s: recovering pdp socket id %d", __func__, idx + 1);

	struct aemu_post_office_sock_addr addr;
	addr.addr = g_adhocServerIP.in.sin_addr.s_addr;
	addr.port = htons(AEMU_POSTOFFICE_PORT);

	SceNetEtherAddr local_mac;
	getLocalMac(&local_mac);

	int state;
	internal->postofficeHandle = pdp_create_v4(&addr, (const char *)&local_mac, offset_port_simple(internal->data.pdp.lport), &state);
	if (state != AEMU_POSTOFFICE_CLIENT_OK) {
		ERROR_LOG(Log::sceNet, "%s: failed creating pdp socket on aemu postoffice library, %d", __func__, state);
	}

	INFO_LOG(Log::sceNet, "%s: pdp recovery for id %d: %p", __func__, idx + 1, internal->postofficeHandle);
	return internal->postofficeHandle;
}

static int pdp_peek_next_size_postoffice(int idx) {
	AdhocSocket *internal = adhocSockets[idx];
	void *pdp_sock = pdp_postoffice_recover(idx);
	if (pdp_sock == NULL) {
		return 0;
	}
	int peek_status = pdp_peek_next_size(pdp_sock);
	if (peek_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		pdp_delete(internal->postofficeHandle);
		internal->postofficeHandle = NULL;
		return 0;
	}
	return peek_status;
}

static int pdp_recv_postoffice(int idx, SceNetEtherAddr *saddr, uint16_t *sport, void *data, int *len) {
	AdhocSocket *internal = adhocSockets[idx];
	void *pdp_sock = pdp_postoffice_recover(idx);
	if (pdp_sock == NULL) {
		return SOCKET_ERROR;
	}

	int sport_copy;
	SceNetEtherAddr saddr_copy;
	int len_copy = *len;

	if (len_copy > AEMU_POSTOFFICE_PDP_BLOCK_MAX) {
		// trim, library limites pdp packets
		// some games just provide amazingly huge buffer sizes during recv
		// if a huge packet cannot be sent, it is logged on the sender side
		len_copy = AEMU_POSTOFFICE_PDP_BLOCK_MAX;
	}

	int pdp_recv_status = pdp_recv(pdp_sock, (char *)&saddr_copy, &sport_copy, (char *)data, &len_copy, true);
	if (pdp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		pdp_delete(internal->postofficeHandle);
		internal->postofficeHandle = NULL;
		return SOCKET_ERROR;
	}
	if (pdp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK) {
		return SOCKET_ERROR;
	}
	if (pdp_recv_status == AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY) {
		// this is pretty critical
		ERROR_LOG(Log::sceNet, "%s: critical: huge client buf %d what is going on please fix", __func__, *len);
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

int DoBlockingPdpRecv(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	auto& pdpsocket = sock->data.pdp;
	if (sock->flags & ADHOC_F_ALERTRECV) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
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

	if (serverHasRelay) {
		while(1) {
			int next_size = pdp_peek_next_size_postoffice(req.id - 1);
			if (next_size == 0) {
				// no next packet
				ret = SOCKET_ERROR;
				sockerr = EAGAIN;
				break;
			}
			if (next_size > *req.length) {
				// next is larger than current buffer
				result = SCE_NET_ADHOC_ERROR_NOT_ENOUGH_SPACE;
				return 0;
			}
			ret = pdp_recv_postoffice(req.id - 1, req.remoteMAC, req.remotePort, req.buffer, req.length);
			if (ret == 0) {
				// we got data into the request
				result = 0;
				return 0;
			}
			ret = SOCKET_ERROR;
			sockerr = EAGAIN;
			break;
		}
	} else {
		// On Windows: MSG_TRUNC are not supported on recvfrom (socket error WSAEOPNOTSUPP), so we use dummy buffer as an alternative
		ret = recvfrom(pdpsocket.id, dummyPeekBuf64k, dummyPeekBuf64kSize, MSG_PEEK | MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
		sockerr = socket_errno;
	}

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
			result = SCE_NET_ADHOC_ERROR_TIMEOUT;
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i]: Discard Timeout", req.id);
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
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i:%u]: Received %u bytes from %s:%u\n", req.id, getLocalPort(pdpsocket.id), ret, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));

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

				WARN_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i:%u]: Received %i bytes from Unknown Peer %s:%u", req.id, getLocalPort(pdpsocket.id), ret, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));
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
			result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	}
	// Returning required buffer size when available data in recv buffer is larger than provided buffer size
	else if (ret > *req.length) {
		WARN_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i:%u]: Peeked %u/%u bytes from %s:%u\n", req.id, getLocalPort(pdpsocket.id), ret, *req.length, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));
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
		result = SCE_NET_ADHOC_ERROR_NOT_ENOUGH_SPACE;
	}
	// FIXME: Blocking operation with infinite timeout(0) should never get a TIMEOUT error, right? May be we should return INVALID_ARG instead if it was infinite timeout (0)?
	else
		result = SCE_NET_ADHOC_ERROR_TIMEOUT; // SCE_NET_ADHOC_ERROR_INVALID_ARG; // SCE_NET_ADHOC_ERROR_DISCONNECTED

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

static int pdp_send_postoffice(int idx, const SceNetEtherAddr *daddr, uint16_t dport, const void *data, int len) {
	AdhocSocket *internal = adhocSockets[idx];
	void *pdp_sock = pdp_postoffice_recover(idx);
	if (pdp_sock == NULL) {
		return SOCKET_ERROR;
	}

	int pdp_send_status = pdp_send(pdp_sock, (const char *)daddr, offset_port_simple(dport), (char *)data, len, true);
	if (pdp_send_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
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

int DoBlockingPdpSend(AdhocSocketRequest& req, s64& result, AdhocSendTargets& targetPeers) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	auto& pdpsocket = sock->data.pdp;
	if (sock->flags & ADHOC_F_ALERTSEND) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
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

		int ret = 0;
		int sockerr = 0;
		if (serverHasRelay) {
			ret = pdp_send_postoffice(req.id - 1, &peer->mac, peer->port, req.buffer, targetPeers.length);
			if (ret == 0) {
				ret = targetPeers.length;
			} else {
				sockerr = EAGAIN;
			}
		} else {
			ret = sendto(pdpsocket.id, (const char*)req.buffer, targetPeers.length, MSG_NOSIGNAL, (struct sockaddr*)&target, sizeof(target));
			sockerr = socket_errno;
		}

		if (ret >= 0) {
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u](B): Sent %u bytes to %s:%u\n", req.id, getLocalPort(pdpsocket.id), ret, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));
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
					result = SCE_NET_ADHOC_ERROR_TIMEOUT;
			}
			++peer;
		}

		if (ret == SOCKET_ERROR)
			DEBUG_LOG(Log::sceNet, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u](B) [size=%i]", sockerr, req.id, getLocalPort(pdpsocket.id), ntohs(target.sin_port), targetPeers.length);
	}

	if (retry)
		return -1;

	return 0;
}

static int ptp_send_postoffice(int idx, const void *data, int *len) {
	AdhocSocket *internal = adhocSockets[idx];

	if (*len > AEMU_POSTOFFICE_PTP_BLOCK_MAX) {
		// force fragmentation for giant sends
		*len = AEMU_POSTOFFICE_PTP_BLOCK_MAX;
	}

	int ptp_send_status = ptp_send(internal->postofficeHandle, (const char *)data, *len, true);
	if (ptp_send_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		// the session is dead, need to be reflected to the other side
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

int DoBlockingPtpSend(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTSEND) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTSEND;
		return 0;
	}

	// Send Data
	int ret = 0;
	int sockerr = 0;
	if (serverHasRelay) {
		ret = ptp_send_postoffice(req.id - 1, req.buffer, req.length);
		if (ret == 0) {
			// sent
			result = 0;
			return 0;
		}
		if (ret == SOCKET_ERROR) {
			ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
			result = SCE_NET_ADHOC_ERROR_DISCONNECTED;
			return 0;
		}
		// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
		ret = SOCKET_ERROR;
		sockerr = EAGAIN;
	}else{
		ret = send(ptpsocket.id, (const char*)req.buffer, *req.length, MSG_NOSIGNAL);
		sockerr = socket_errno;
	}

	// Success
	if (ret > 0) {
		// Save Length
		*req.length = ret;

		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u\n", req.id, ptpsocket.lport, ret, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

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
			result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	}
	else {
		// Change Socket State. // FIXME: Does Alerted Socket should be closed too?
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

		// Disconnected
		result = SCE_NET_ADHOC_ERROR_DISCONNECTED;
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

static int ptp_recv_postoffice(int idx, void *data, int *len) {
	AdhocSocket *internal = adhocSockets[idx];

	int len_copy = *len;
	if (len_copy > AEMU_POSTOFFICE_PTP_BLOCK_MAX) {
		// trim, library limit
		// some games just provide amazingly huge buffer sizes during recv
		// if a huge burst cannot be sent, it is logged on the sender side
		len_copy = AEMU_POSTOFFICE_PTP_BLOCK_MAX;
	}

	int ptp_recv_status = ptp_recv(internal->postofficeHandle, (char *)data, &len_copy, true);
	if (ptp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
		// the session is dead, need to be reflected to the other side
		return SOCKET_ERROR;
	}
	if (ptp_recv_status == AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}
	if (ptp_recv_status == AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY) {
		// this is pretty critical
		ERROR_LOG(Log::sceNet, "%s: critical: huge client buf %d what is going on please fix", __func__, *len);
	}

	// AEMU_POSTOFFICE_CLIENT_SESSION_DATA_TRUNC is okay, it just means it has data in it's user space buffer

	*len = len_copy;
	return 0;
}

int DoBlockingPtpRecv(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTRECV) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTRECV;
		return 0;
	}

	int ret = 0;
	int sockerr = 0;
	if (serverHasRelay) {
		ret = ptp_recv_postoffice(req.id - 1, req.buffer, req.length);
		if (ret == 0){
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
		ret = SOCKET_ERROR;
		sockerr = EAGAIN;
	} else {
		ret = recv(ptpsocket.id, (char*)req.buffer, std::max(0, *req.length), MSG_NOSIGNAL);
		sockerr = socket_errno;
	}

	// Received Data. POSIX: May received 0 bytes when the remote peer already closed the connection.
	if (ret > 0) {
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %s:%u\n", req.id, ptpsocket.lport, ret, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);
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
			result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	}
	else {
		// Change Socket State. // FIXME: Does Alerted Socket should be closed too?
		ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

		// Disconnected
		result = SCE_NET_ADHOC_ERROR_DISCONNECTED; // SCE_NET_ADHOC_ERROR_INVALID_ARG
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

static void *ptp_listen_postoffice_recover(int idx) {
	AdhocSocket *internal = adhocSockets[idx];
	if (internal->postofficeHandle != NULL) {
		return internal->postofficeHandle;
	}

	INFO_LOG(Log::sceNet, "%s: recovering ptp listen socket id %d", __func__, idx + 1);

	struct aemu_post_office_sock_addr addr;
	addr.addr = g_adhocServerIP.in.sin_addr.s_addr;
	addr.port = htons(AEMU_POSTOFFICE_PORT);

	int state;
	internal->postofficeHandle = ptp_listen_v4(&addr, (const char*)&internal->data.ptp.laddr, internal->data.ptp.lport + portOffset, &state);

	if (state != AEMU_POSTOFFICE_CLIENT_OK) {
		ERROR_LOG(Log::sceNet, "%s: failed recovering ptp listen socket, %d", __func__, state);
	}

	INFO_LOG(Log::sceNet, "%s: ptp listen recovery for id %d: %p", __func__, idx + 1, internal->postofficeHandle);

	return internal->postofficeHandle;
}

static int ptp_accept_postoffice(int idx, SceNetEtherAddr *saddr, uint16_t *sport) {
	void *ptp_listen_socket = ptp_listen_postoffice_recover(idx);

	if (ptp_listen_socket == NULL) {
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	int state;
	int port_cpy;
	SceNetEtherAddr mac_cpy;
	void *new_ptp_socket = ptp_accept(ptp_listen_socket, (char *)&mac_cpy, &port_cpy, true, &state);
	if (new_ptp_socket == NULL) {
		if (state == AEMU_POSTOFFICE_CLIENT_SESSION_DEAD) {
			ptp_listen_close(adhocSockets[idx]->postofficeHandle);
			adhocSockets[idx]->postofficeHandle = NULL;
			return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
		}
		if (state == AEMU_POSTOFFICE_CLIENT_OUT_OF_MEMORY) {
			// pretty critical here
			ERROR_LOG(Log::sceNet, "%s: aemu_postoffice ran out of memory to accept a new connection", __func__);
		}
		// AEMU_POSTOFFICE_CLIENT_SESSION_WOULD_BLOCK
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

    // we have a new socket
	AdhocSocket *internal = (AdhocSocket *)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		ERROR_LOG(Log::sceNet, "%s: critical: ran out of heap memory while accepting new connection", __func__);
		ptp_close(new_ptp_socket);
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

	internal->type = SOCK_PTP;
	internal->postofficeHandle = new_ptp_socket;
	internal->data.ptp.laddr = adhocSockets[idx]->data.ptp.laddr;
	internal->data.ptp.lport = adhocSockets[idx]->data.ptp.lport;
	internal->data.ptp.paddr = mac_cpy;
	internal->data.ptp.pport = port_cpy;
	internal->data.ptp.state = ADHOC_PTP_STATE_ESTABLISHED;
	internal->data.ptp.rcv_sb_cc = adhocSockets[idx]->data.ptp.rcv_sb_cc;
	internal->data.ptp.snd_sb_cc = adhocSockets[idx]->data.ptp.snd_sb_cc;
	internal->data.ptp.id = AEMU_POSTOFFICE_ID_BASE + idx;
	internal->flags = 0;
	internal->connectThread = NULL;

	AdhocSocket **slot = NULL;
	int i;
	for (i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] == NULL) {
			slot = &adhocSockets[i];
			break;
		}
	}

	if (slot == NULL) {
		ptp_close(new_ptp_socket);
		free(internal);
		ERROR_LOG(Log::sceNet, "%s: critical: cannot find an empty mapper slot", __func__);
		return SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
	}

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

int DoBlockingPtpAccept(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTACCEPT) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTACCEPT;
		return 0;
	}

	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	socklen_t sinlen = sizeof(sin);
	int ret, sockerr;

	if (serverHasRelay) {
		ret = ptp_accept_postoffice(req.id - 1, req.remoteMAC, req.remotePort);
		if (ret >= 0) {
			result = ret;
			return 0;
		} else {
			ret = SOCKET_ERROR;
			sockerr = EAGAIN;
		}
	} else {
		// Check if listening socket is ready to accept
		ret = IsSocketReady(ptpsocket.id, true, false, &sockerr);
		if (ret > 0) {
			// Accept Connection
			ret = accept(ptpsocket.id, (struct sockaddr*)&sin, &sinlen);
			sockerr = socket_errno;
		}
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
				result = SCE_NET_ADHOC_ERROR_TIMEOUT;
		}
	}
	else
		result = SCE_NET_ADHOC_ERROR_INVALID_ARG; //SCE_NET_ADHOC_ERROR_TIMEOUT

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

static int ptp_connect_postoffice(int idx, const char *caller) {
	AdhocSocket *internal = adhocSockets[idx];

	struct aemu_post_office_sock_addr addr;
	addr.addr = g_adhocServerIP.in.sin_addr.s_addr;
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

		internal->connectThread = new std::thread([internal, addr, idx] {
			int state;
			void *ptp_socket = ptp_connect_v4(&addr, (const char *)&internal->data.ptp.laddr, offset_port_simple(internal->data.ptp.lport), (const char *)&internal->data.ptp.paddr, offset_port_simple(internal->data.ptp.pport), &state);
			if (ptp_socket == NULL) {
				internal->connectThreadResult = SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED;
				ERROR_LOG(Log::sceNet, "%s: failed connecting to ptp socket, %d", __func__, state);
				internal->connectThreadDone = true;
				return;
			}
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

int DoBlockingPtpConnect(AdhocSocketRequest& req, s64& result, AdhocSendTargets& targetPeer) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTCONNECT) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTCONNECT;
		return 0;
	}

	int sockerr = 0, ret;
	struct sockaddr_in sin{};
	// Try to connect again if the first attempt failed due to remote side was not listening yet (ie. ECONNREFUSED or ETIMEDOUT)
	if (ptpsocket.state == ADHOC_PTP_STATE_CLOSED) {
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = targetPeer.peers[0].ip;
		sin.sin_port = htons(ptpsocket.pport + targetPeer.peers[0].portOffset);

		if (serverHasRelay) {
			ret = ptp_connect_postoffice(req.id - 1, __func__);
			if (ret != 0) {
				ret = SOCKET_ERROR;
				sockerr = EAGAIN;
			}
		} else {
			ret = connect(ptpsocket.id, (struct sockaddr*)&sin, sizeof(sin));
			sockerr = socket_errno;
		}
		if (sockerr != 0)
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: connect(%i) error = %i", req.id, ptpsocket.lport, ptpsocket.id, sockerr);
		else
			ret = 1; // Ensure returned success value from connect to be compatible with returned success value from select (ie. positive value)
	}
	// Check the connection state (assuming "connect" has been called before and is in-progress)
	// Note: On Linux "select" can return > 0 (with SO_ERROR = 0) even when the connection is not accepted yet, thus need "getpeername" to ensure
	else {
		if (serverHasRelay) {
			if (sock->postofficeHandle == NULL) {
				ret = SOCKET_ERROR;
				if (sock->connectThreadDone && sock->connectThreadResult == SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED){
					sockerr = ECONNREFUSED;
				} else {
					sockerr = EAGAIN;
				}
			} else {
				// the handle is there, we're ready to go
				ret = 1;
				sockerr = 0;
			}
		} else {
			ret = IsSocketReady(ptpsocket.id, false, true, &sockerr);
		}
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: Select(%i) = %i, error = %i", req.id, ptpsocket.lport, ptpsocket.id, ret, sockerr);
		if (sockerr != 0) {
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: SelectError(%i) = %i", req.id, ptpsocket.lport, ptpsocket.id, sockerr);
			ret = SOCKET_ERROR; // Ensure returned value from select to be negative when the socket has error (the socket may need to be recreated again)
		}

		if (ret <= 0) {
			if (sockerr == 0)
				sockerr = EAGAIN;
			ret = SOCKET_ERROR; // Ensure returned value from select to be negative when the socket is not ready yet, due to a possibility for "getpeername" to succeed on Windows even when "connect" hasn't been accepted yet
		}
	}

	// Check whether the connection has been established or not
	if (!serverHasRelay && ret != SOCKET_ERROR) {
		socklen_t sinlen = sizeof(sin);
		// Note: "getpeername" shouldn't failed if the connection has been established, but on Windows it may succeed even when "connect" is still in-progress and not accepted yet (ie. "Tales of VS" on Windows)
		ret = getpeername(ptpsocket.id, (struct sockaddr*)&sin, &sinlen);
		if (ret == SOCKET_ERROR) {
			int err = socket_errno;
			VERBOSE_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: getpeername(%i) error %i, sockerr = %i", req.id, ptpsocket.lport, ptpsocket.id, err, sockerr);
			sockerr = err;
		}
	}

	// Update Adhoc Socket state
	if (ret != SOCKET_ERROR || sockerr == EISCONN) {
		ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;
		INFO_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: Established (%s:%u)", req.id, ptpsocket.lport, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);

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
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: Recreating Socket %i, errno = %i, state = %i, attempt = %i", req.id, ptpsocket.lport, ptpsocket.id, sockerr, ptpsocket.state, sock->attemptCount);
			if (RecreatePtpSocket(req.id) < 0) {
				WARN_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: RecreatePtpSocket error %i", req.id, ptpsocket.lport, socket_errno);
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
				result = SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
			else
				result = SCE_NET_ADHOC_ERROR_TIMEOUT; // FIXME: PSP never returned SCE_NET_ADHOC_ERROR_TIMEOUT on PtpConnect? or only returned SCE_NET_ADHOC_ERROR_TIMEOUT when the host is too busy? Seems to be returning SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED on timedout instead (if the other side in not listening yet, which is similar to BSD).

			// Done
			return 0;
		}
	}

	if (ret == SOCKET_ERROR)
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i]: Socket Error (%i)", req.id, sockerr);

	return 0;
}

int DoBlockingPtpFlush(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	auto& ptpsocket = sock->data.ptp;
	if (sock->flags & ADHOC_F_ALERTFLUSH) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
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
			result = SCE_NET_ADHOC_ERROR_TIMEOUT;
	}

	if (sockerr != 0) {
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpFlush[%i]: Socket Error (%i)", req.id, sockerr);
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
			ret = SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT;
		// FIXME: Does AdhocPollSocket can return any error code other than SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT?
		//else
		//	ret = SCE_NET_ADHOC_ERROR_TIMEOUT;
	}
	result = ret;

	if (ret > 0) {
		for (int i = 0; i < req.id; i++) {
			if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
				auto sock = adhocSockets[sds[i].id - 1];
				if (sock->type == SOCK_PTP)
					VERBOSE_LOG(Log::sceNet, "Poll PTP Socket Id: %d (%d), events: %08x, revents: %08x - state: %d", sds[i].id, sock->data.ptp.id, sds[i].events, sds[i].revents, sock->data.ptp.state);
				else
					VERBOSE_LOG(Log::sceNet, "Poll PDP Socket Id: %d (%d), events: %08x, revents: %08x", sds[i].id, sock->data.pdp.id, sds[i].events, sds[i].revents);
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
		WARN_LOG(Log::sceNet, "sceNetAdhoc Socket WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
		return;
	}

	// Socket not found?! Should never happened! But if it ever happened (ie. loaded from SaveState where adhocSocketRequests got cleared) return TIMEOUT and let the game try again.
	if (adhocSocketRequests.find(userdata) == adhocSocketRequests.end()) {
		WARN_LOG(Log::sceNet, "sceNetAdhoc Socket WaitID(%i) on Thread(%i) not found!", uid, threadID);
		__KernelResumeThreadFromWait(threadID, SCE_NET_ADHOC_ERROR_TIMEOUT);
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
	DEBUG_LOG(Log::sceNet, "Returning (ThreadId: %d, WaitID: %d, error: %08x) Result (%08x) of sceNetAdhoc[%d] - SocketID: %d", threadID, waitID, error, (int)result, req.type, req.id);

	// We are done with this socket
	adhocSocketRequests.erase(userdata);
}

// input threadSocketId = ((u64)__KernelGetCurThread()) << 32 | socketId;
int WaitBlockingAdhocSocket(u64 threadSocketId, int type, int pspSocketId, void* buffer, s32_le* len, u32 timeoutUS, SceNetEtherAddr* remoteMAC, u16_le* remotePort, const char* reason) {
	int uid = (int)(threadSocketId & 0xFFFFFFFF);
	if (adhocSocketRequests.find(threadSocketId) != adhocSocketRequests.end()) {
		WARN_LOG(Log::sceNet, "sceNetAdhoc[%d] - ThreadID[%d] WaitID[%d] already existed, Socket[%d] is busy!", type, static_cast<int>(threadSocketId >> 32), uid, pspSocketId);
		// FIXME: Not sure if Adhoc Socket can return ADHOC_BUSY or not (assuming it's similar to EINPROGRESS for Adhoc Socket), or may be we should return TIMEOUT instead?
		return SCE_NET_ADHOC_ERROR_BUSY; // SCE_NET_ADHOC_ERROR_TIMEOUT
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
	return SCE_NET_ADHOC_ERROR_TIMEOUT;
}

void __NetAdhocDoState(PointerWrap &p) {
	auto s = p.Section("sceNetAdhoc", 1, 8);
	if (!s)
		return;

	auto cur_netAdhocInited = netAdhocInited;
	auto cur_netAdhocctlInited = netAdhocctlInited;
	SaveNetAdhocMatchingInited();

	Do(p, netAdhocInited);
	Do(p, netAdhocctlInited);
	DoNetAdhocMatchingInited(p);
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
		DoNetAdhocMatchingThreads(p);
	}
	else {
		threadAdhocID = 0;
		ZeroNetAdhocMatchingThreads();
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
		RestoreNetAdhocMatchingInited();

		netAdhocctlInited = cur_netAdhocctlInited;
		netAdhocInited = cur_netAdhocInited;

		isAdhocctlNeedLogin = false;
	}
}

void __UpdateAdhocctlHandlers(u32 flag, u32 error) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	adhocctlEvents.push_back({ flag, error });
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

		serverHasRelay = g_Config.bUseServerRelay;

		if (serverHasRelay) {
			aemu_post_office_init();
		}

		// Return Success
		return hleLogInfo(Log::sceNet, 0, "at %08x", currentMIPS->pc);
	}
	// Already initialized
	return hleLogWarning(Log::sceNet, SCE_NET_ADHOC_ERROR_ALREADY_INITIALIZED, "already initialized");
}

int sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlInit(%i, %i, %08x) at %08x", stackSize, prio, productAddr, currentMIPS->pc);

	// FIXME: Returning 0x8002013a (SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED) without adhoc module loaded first?
	// FIXME: Sometimes returning 0x80410601 (SCE_NET_ADHOC_ERROR_AUTH_ALREADY_INITIALIZED / Library module is already initialized ?) when AdhocctlTerm is not fully done?

	if (netAdhocctlInited) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_ALREADY_INITIALIZED);
	}

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
	if (g_Config.bEnableWlan && !g_adhocServerConnected) {
		AdhocctlRequest dummyreq = { OPCODE_LOGIN, {0} };
		return hleLogDebugOrWarn(Log::sceNet, WaitBlockingAdhocctlSocket(dummyreq, us, "adhocctl init"));
	}
	// Give a little time for friendFinder thread to be ready before the game use the next sceNet functions, should've checked for friendFinderRunning status instead of guessing the time?
	hleEatMicro(us);

	return hleLogDebug(Log::sceNet, 0);
}

int NetAdhocctl_GetState() {
	return adhocctlState;
}

int sceNetAdhocctlGetState(u32 ptrToStatus) {
	// Library uninitialized
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED);

	// Invalid Arguments
	if (!Memory::IsValidAddress(ptrToStatus))
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG);

	int state = NetAdhocctl_GetState();
	// Output Adhocctl State
	Memory::Write_U32(state, ptrToStatus);

	// Return Success
	return hleLogVerbose(Log::sceNet, 0, "state = %d", state);
}

static int pdp_create_postoffice(const SceNetEtherAddr *saddr, int sport, int bufsize) {
	AdhocSocket *internal = (AdhocSocket*)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		return hleLogDebug(Log::sceNet, ERROR_NET_NO_SPACE, "net no space");
	}

	internal->type = SOCK_PDP;
	internal->postofficeHandle = NULL;
	internal->data.pdp.laddr = *saddr;
	internal->data.pdp.lport = sport;
	internal->data.pdp.rcv_sb_cc = bufsize;
	internal->flags = 0;
	internal->lastAttempt = 0;
	internal->internalLastAttempt = 0;

	AdhocSocket **free_slot = NULL;
	int i;
	for (i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] == NULL) {
			free_slot = &adhocSockets[i];
			break;
		}
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

/**
 * Adhoc Emulator PDP Socket Creator
 * @param saddr Local MAC (Unused)
 * @param sport Local Binding Port
 * @param bufsize Socket Buffer Size
 * @param flag Bitflags (Unused)
 * @return Socket ID > 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_SOCKET_ID_NOT_AVAIL, ADHOC_INVALID_ADDR, ADHOC_PORT_NOT_AVAIL, ADHOC_INVALID_PORT, ADHOC_PORT_IN_USE, NET_NO_SPACE
 */
// When choosing AdHoc menu in Wipeout Pulse sometimes it's saying that "WLAN is turned off" on game screen and getting "kUnityCommandCode_MediaDisconnected" error in the Log Console when calling sceNetAdhocPdpCreate, probably it needed to wait something from the thread before calling this (ie. need to receives 7 bytes from adhoc server 1st?)
int sceNetAdhocPdpCreate(const char *mac, int port, int bufferSize, u32 flag) {
	INFO_LOG(Log::sceNet, "sceNetAdhocPdpCreate(%s, %u, %u, %u) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), port, bufferSize, flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN");
	}

	if (!g_netInited)
		return hleLogError(Log::sceNet, 0x800201CA); //PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX;

	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)mac;
	bool isClient = false;
	if (netAdhocInited) {
		// Valid Arguments are supplied
		if (mac != NULL && bufferSize > 0) {
			// Port is in use by another PDP Socket.
			if (isPDPPortInUse(port)) {
				// FIXME: When PORT_IN_USE error occured it seems the index to the socket id also increased, which means it tries to create & bind the socket first and then closes it due to failed to bind
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_IN_USE, "port in use");
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
				if (serverHasRelay)
					return pdp_create_postoffice(saddr, port, bufferSize);

				// Create Internet UDP Socket
				// Socket is remapped through adhocSockets
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
						WARN_LOG(Log::sceNet, "sceNetAdhocPdpCreate - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
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
								WARN_LOG(Log::sceNet, "sceNetAdhocPdpCreate - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", port, requestedport, boundport, boundport - portOffset);
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
								INFO_LOG(Log::sceNet, "sceNetAdhocPdpCreate - PSP Socket id: %i, Host Socket id: %i", i + 1, usocket);
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
						ERROR_LOG(Log::sceNet, "Socket error (%i) when binding port %u", socket_errno, ntohs(addr.sin_port));
						auto n = GetI18NCategory(I18NCat::NETWORKING);
						g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Port")) + " " + std::to_string(port + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 0.0f, "portbindfail");

						return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_NOT_AVAIL, "port not available");
					}
				}

				// Default to No-Space Error
				return hleLogDebug(Log::sceNet, ERROR_NET_NO_SPACE, "net no space");
			}

			// Invalid MAC supplied
			return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address");
		}

		// Invalid Arguments were supplied
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}
	// Library is uninitialized
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "adhoc not initialized");
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
	DEBUG_LOG(Log::sceNet, "sceNetAdhocctlGetParameter(%08x) [Ch=%i][Group=%s][BSSID=%s][name=%s]", paramAddr, parameter.channel, grpName, mac2str(&parameter.bssid.mac_addr).c_str(), parameter.nickname.data);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_DISCONNECTED);
	}

	// Library initialized
	if (!netAdhocctlInited) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED);
	}

	auto ptr = PSPPointer<SceNetAdhocctlParameter>::Create(paramAddr);
	if (!ptr.IsValid())
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG);

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
int sceNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag) {
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i) at %08x", id, mac2str((SceNetEtherAddr*)mac).c_str(), port, data, len, timeout, flag, currentMIPS->pc);
	} else {
		VERBOSE_LOG(Log::sceNet, "sceNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i) at %08x", id, mac2str((SceNetEtherAddr*)mac).c_str(), port, data, len, timeout, flag, currentMIPS->pc);
	}
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1);
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

								return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
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
									int sent = 0;
									int error = 0;
									if (serverHasRelay) {
										sent = pdp_send_postoffice(id - 1, daddr, dport, data, len);
										if (sent == 0) {
											sent = len;
										} else {
											sent = SOCKET_ERROR;
											error = EAGAIN;
										}
									} else {
										sent = sendto(pdpsocket.id, (const char *)data, len, MSG_NOSIGNAL, (struct sockaddr*)&target, sizeof(target));
										error = socket_errno;
									}

									if (sent == SOCKET_ERROR) {
										// Simulate blocking behaviour with non-blocking socket
										if (!flag && (error == EAGAIN || error == EWOULDBLOCK)) {
											u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
											if (sendTargetPeers.find(threadSocketId) != sendTargetPeers.end()) {
												DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u]: Socket(%d) is Busy!", id, getLocalPort(pdpsocket.id), pdpsocket.id);
												return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_BUSY, "busy?");
											}

											AdhocSendTargets dest = { len, {}, false };
											dest.peers.push_back({ target.sin_addr.s_addr, dport, finalPortOffset, *daddr });
											sendTargetPeers[threadSocketId] = dest;
											return WaitBlockingAdhocSocket(threadSocketId, PDP_SEND, id, data, nullptr, timeout, nullptr, nullptr, "pdp send");
										}

										DEBUG_LOG(Log::sceNet, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u] (size=%i)", error, id, getLocalPort(pdpsocket.id), ntohs(target.sin_port), len);
									}
									//changeBlockingMode(socket->id, 0);

									// Free Network Lock
									//_freeNetworkLock();

									hleEatMicro(50); // Can be longer than 1ms tho
									// Sent Data
									if (sent >= 0) {
										DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u]: Sent %u bytes to %s:%u\n", id, getLocalPort(pdpsocket.id), sent, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));

										// Success
										return 0; // sent; // MotorStorm will try to resend if return value is not 0
									}

									// Non-Blocking
									if (flag)
										return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

									// Does PDP can Timeout? There is no concept of Timeout when sending UDP due to no ACK, but might happen if the socket buffer is full, not sure about PDP since some games did use the timeout arg
									return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_TIMEOUT, "timeout?"); // SCE_NET_ADHOC_ERROR_INVALID_ADDR;
								}
								VERBOSE_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u]: Unknown Target Peer %s:%u (faking success)\n", id, getLocalPort(pdpsocket.id), mac2str(daddr).c_str(), ntohs(target.sin_port));
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

									dest.peers.push_back({ peer->ip_addr, dport, peer->port_offset, peer->mac_addr });
								}
								// Free Peer Lock
								peerlock.unlock();

								// Send Data
								// Simulate blocking behaviour with non-blocking socket
								if (!flag) {
									u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
									if (sendTargetPeers.find(threadSocketId) != sendTargetPeers.end()) {
										DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u](BC): Socket(%d) is Busy!", id, getLocalPort(pdpsocket.id), pdpsocket.id);
										return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_BUSY, "busy?");
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

										int sent = 0;
										int error = 0;
										if (serverHasRelay) {
											sent = pdp_send_postoffice(id - 1, &peer.mac, dport, data, len);
											if (sent == 0){
												sent = len;
											} else {
												sent = SOCKET_ERROR;
												error = EAGAIN;
											}
										} else {
											sent = sendto(pdpsocket.id, (const char *)data, len, MSG_NOSIGNAL, (struct sockaddr*)&target, sizeof(target));
											error = socket_errno;
										}

										if (sent == SOCKET_ERROR) {
											DEBUG_LOG(Log::sceNet, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u](BC) [size=%i]", error, id, getLocalPort(pdpsocket.id), ntohs(target.sin_port), len);
										}

										if (sent >= 0) {
											DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u](BC): Sent %u bytes to %s:%u\n", id, getLocalPort(pdpsocket.id), sent, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));
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
						return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address");
					}

					// Invalid Argument
					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
				}

				// Invalid Socket ID
				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
			}

			// Invalid Data Length
			return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_DATALEN, "invalid data length");
		}

		// Invalid Destination Port
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_PORT, "invalid port");
	}

	// Library is uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
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
int sceNetAdhocPdpRecv(int id, void *addr, void * port, void *buf, void *dataLength, u32 timeout, int flag) {
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, currentMIPS->pc);
	} else {
		VERBOSE_LOG(Log::sceNet, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, currentMIPS->pc);
	}

	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1);
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

					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
				}

				// Sender Address
				struct sockaddr_in sin;
				socklen_t sinlen;

				// Acquire Network Lock
				//_acquireNetworkLock();

				SceNetEtherAddr mac;
				int received = 0;
				int error = 0;

				if (serverHasRelay) {
					while(1) {
						int next_size = pdp_peek_next_size_postoffice(id - 1);
						if (next_size == 0) {
							// no next packet
							received = SOCKET_ERROR;
							error = EAGAIN;
							break;
						}
						if (next_size > *len) {
							// next is larger than current buffer
							return SCE_NET_ADHOC_ERROR_NOT_ENOUGH_SPACE;
						}
						received = pdp_recv_postoffice(id - 1, saddr, sport, buf, len);
						if (received == 0) {
							// we got data
							hleEatMicro(50);
							return 0;
						}
						received = SOCKET_ERROR;
						error = EAGAIN;
						break;
					}
				} else {
					int disCnt = 16;
					while (--disCnt > 0)
					{
						// Receive Data. PDP always sent in full size or nothing(failed), recvfrom will always receive in full size as requested (blocking) or failed (non-blocking). If available UDP data is larger than buffer, excess data is lost.
						// Should peek first for the available data size if it's more than len return SCE_NET_ADHOC_ERROR_NOT_ENOUGH_SPACE along with required size in len to prevent losing excess data
						// On Windows: MSG_TRUNC are not supported on recvfrom (socket error WSAEOPNOTSUPP), so we use dummy buffer as an alternative
						sinlen = sizeof(sin);
						memset(&sin, 0, sinlen);
						received = recvfrom(pdpsocket.id, dummyPeekBuf64k, dummyPeekBuf64kSize, MSG_PEEK | MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
						error = socket_errno;
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
								VERBOSE_LOG(Log::sceNet, "%08x=sceNetAdhocPdpRecv: would block (disc)", SCE_NET_ADHOC_ERROR_WOULD_BLOCK); // Temporary fix to avoid a crash on the Logs due to trying to Logs syscall's argument from another thread (ie. AdhocMatchingInput thread)
								return SCE_NET_ADHOC_ERROR_WOULD_BLOCK; // hleLogSuccessVerboseX(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block (disc)");
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
				}

				// At this point we assumed that the packet is a valid PPSSPP packet
				if (received != SOCKET_ERROR && *len < received) {
					INFO_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i:%u]: Peeked %u/%u bytes from %s:%u\n", id, getLocalPort(pdpsocket.id), received, *len, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));

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

					return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_ENOUGH_SPACE, "not enough space");
				}

				if (!serverHasRelay) {
					sinlen = sizeof(sin);
					memset(&sin, 0, sinlen);
					// On Windows: Socket Error 10014 may happen when buffer size is less than the minimum allowed/required (ie. negative number on Vulcanus Seek and Destroy), the address is not a valid part of the user address space (ie. on the stack or when buffer overflow occurred), or the address is not properly aligned (ie. multiple of 4 on 32bit and multiple of 8 on 64bit) https://stackoverflow.com/questions/861154/winsock-error-code-10014
					received = recvfrom(pdpsocket.id, (char*)buf, std::max(0, *len), MSG_NOSIGNAL, (struct sockaddr*)&sin, &sinlen);
					error = socket_errno;
				}

				// On Windows: recvfrom on UDP can get error WSAECONNRESET when previous sendto's destination is unreachable (or destination port is not bound), may need to disable SIO_UDP_CONNRESET
				if (received == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || error == ECONNRESET)) {
					if (flag == 0) {
						// Simulate blocking behaviour with non-blocking socket
						u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
						return WaitBlockingAdhocSocket(threadSocketId, PDP_RECV, id, buf, len, timeout, saddr, sport, "pdp recv");
					}

					VERBOSE_LOG(Log::sceNet, "%08x=sceNetAdhocPdpRecv: would block", SCE_NET_ADHOC_ERROR_WOULD_BLOCK); // Temporary fix to avoid a crash on the Logs due to trying to Logs syscall's argument from another thread (ie. AdhocMatchingInput thread)
					return SCE_NET_ADHOC_ERROR_WOULD_BLOCK; // hleLogSuccessVerboseX(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");
				}

				hleEatMicro(50);
				// Received Data. UDP can also receives 0 data, while on TCP 0 data = connection gracefully closed, but not sure about PDP tho
				if (received >= 0) {
					DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i:%u]: Received %u bytes from %s:%u\n", id, getLocalPort(pdpsocket.id), received, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));

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
					WARN_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i:%u]: Received %i bytes from Unknown Peer %s:%u", id, getLocalPort(pdpsocket.id), received, ip2str(sin.sin_addr).c_str(), ntohs(sin.sin_port));
					if (flag) {
						VERBOSE_LOG(Log::sceNet, "%08x=sceNetAdhocPdpRecv: would block (problem)", SCE_NET_ADHOC_ERROR_WOULD_BLOCK); // Temporary fix to avoid a crash on the Logs due to trying to Logs syscall's argument from another thread (ie. AdhocMatchingInput thread)
						return SCE_NET_ADHOC_ERROR_WOULD_BLOCK; // hleLogSuccessVerboseX(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block (problem)");
					}
				}

				// Free Network Lock
				//_freeNetworkLock();

#ifdef PDP_DIRTY_MAGIC
				// Restore Nonblocking Flag for Return Value
				if (wouldblock) flag = 1;
#endif

				DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpRecv[%i:%u]: Result:%i (Error:%i)", id, pdpsocket.lport, received, error);

				// Unexpected error (other than EAGAIN/EWOULDBLOCK/ECONNRESET) or in case the Peeked's packet was different than Recved one, treated as Timeout?
				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_TIMEOUT, "timeout?");
			}

			// Invalid Argument
			return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
		}

		// Invalid Socket ID
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
	}

	// Library is uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
}

int NetAdhoc_SetSocketAlert(int id, s32_le flag) {
	if (id < 1 || id > MAX_SOCKET || adhocSockets[id - 1] == NULL)
		return SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID;

	// FIXME: Should we check for valid Alert Flags and/or Mask them? Should we return an error if we found an invalid flag?
	s32_le flg = flag & ADHOC_F_ALERTALL;

	adhocSockets[id - 1]->flags = flg;
	adhocSockets[id - 1]->alerted_flags = 0;

	return 0;
}

// Flags seems to be bitmasks of ADHOC_F_ALERT... (need more games to test this)
int sceNetAdhocSetSocketAlert(int id, int flag) {
 	WARN_LOG_REPORT_ONCE(sceNetAdhocSetSocketAlert, Log::sceNet, "UNTESTED sceNetAdhocSetSocketAlert(%d, %08x) at %08x", id, flag, currentMIPS->pc);

	int retval = NetAdhoc_SetSocketAlert(id, flag);
	return hleDelayResult(hleLogDebug(Log::sceNet, retval), "set socket alert delay", 1000);
}

static int get_postoffice_fd(int idx) {
	AdhocSocket *internal = adhocSockets[idx];
	if (internal->type == SOCK_PTP) {
		if (internal->data.ptp.state == ADHOC_PTP_STATE_LISTEN) {
			void *socket = ptp_listen_postoffice_recover(idx);
			if (socket != NULL) {
				return ptp_listen_get_native_sock(socket);
			}
		} else {
			void *socket = internal->postofficeHandle;
			if (socket != NULL) {
				return ptp_get_native_sock(socket);
			}
		}
	} else {
		void *socket = pdp_postoffice_recover(idx);
		if (socket != NULL){
			return pdp_get_native_sock(socket);
		}
	}
	return -1;
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
				return SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
			}

			if (serverHasRelay) {
				int postoffice_fd = get_postoffice_fd(sds[i].id - 1);
				if (postoffice_fd == -1) {
					continue;
				}
				fd = postoffice_fd;
			} else {
				if (sock->type == SOCK_PTP) {
					fd = sock->data.ptp.id;
				}
				else {
					fd = sock->data.pdp.id;
				}
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

				if (serverHasRelay) {
					int postoffice_fd = get_postoffice_fd(sds[i].id - 1);
					if (postoffice_fd == -1) {
						continue;
					}
					fd = postoffice_fd;
				} else {
					if (sock->type == SOCK_PTP) {
						fd = sock->data.ptp.id;
					}
					else {
						fd = sock->data.pdp.id;
					}
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

					return SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
				}
			}
			else {
				sds[i].revents |= ADHOC_EV_INVALID;
			}
			if (sds[i].revents) affectedsockets++;
		}
	}
	else {
		// FIXME: Does AdhocPollSocket can return any error code other than SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT on blocking/non-blocking mode?
		/*if (nonblock)
			affectedsockets = SCE_NET_ADHOC_ERROR_WOULD_BLOCK;
		else
			affectedsockets = SCE_NET_ADHOC_ERROR_TIMEOUT;
		*/
		affectedsockets = SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT;
	}
	return affectedsockets;
}

int sceNetAdhocPollSocket(u32 socketStructAddr, int count, int timeout, int nonblock) { // timeout in microseconds
	DEBUG_LOG_REPORT_ONCE(sceNetAdhocPollSocket, Log::sceNet, "UNTESTED sceNetAdhocPollSocket(%08x, %i, %i, %i) at %08x", socketStructAddr, count, timeout, nonblock, currentMIPS->pc);
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
					return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
			}

			// Nonblocking Mode
			if (nonblock)
				timeout = 0;

			if (count > (int)FD_SETSIZE)
				count = FD_SETSIZE; // return 0; //SCE_NET_ADHOC_ERROR_INVALID_ARG

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
			// Bleach 7 seems to use nonblocking and check the return value > 0, or 0x80410709 (SCE_NET_ADHOC_ERROR_WOULD_BLOCK), also 0x80410717 (SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT), when using prx files on JPCSP it can return 0
			if (affectedsockets >= 0) {
				if (affectedsockets > 0) {
					for (int i = 0; i < count; i++) {
						if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
							auto sock = adhocSockets[sds[i].id - 1];
							if (sock->type == SOCK_PTP)
								VERBOSE_LOG(Log::sceNet, "Poll PTP Socket Id: %d (%d), events: %08x, revents: %08x - state: %d", sds[i].id, sock->data.ptp.id, sds[i].events, sds[i].revents, sock->data.ptp.state);
							else
								VERBOSE_LOG(Log::sceNet, "Poll PDP Socket Id: %d (%d), events: %08x, revents: %08x", sds[i].id, sock->data.pdp.id, sds[i].events, sds[i].revents);
						}
					}
				}
				// Workaround to get 30 FPS instead of the too fast 60 FPS on Fate Unlimited Codes, it's abit absurd for a non-blocking call to have this much delay tho, and hleDelayResult doesn't works as good as hleEatMicro for this workaround.
				hleEatMicro(50); // hleEatMicro(7500); // normally 1ms, but using 7.5ms here seems to show better result for Bleach Heat the Soul 7 and other games with too high FPS, but may have a risk of slowing down games that already runs at normal FPS? (need more games to test this)
				return hleLogDebug(Log::sceNet, affectedsockets, "success");
			}
			//else if (nonblock && affectedsockets < 0)
			//	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block"); // Is this error code valid for PollSocket? as it always returns 0 even when nonblock flag is set

			return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT, "exception event");
		}

		// Invalid Argument
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}

	// Library is uninitialized
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "adhoc not initialized");
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

int NetAdhocPdp_Delete(int id, int unknown) {
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (id > 0 && id <= MAX_SOCKET) {
			// Cast Socket
			auto sock = adhocSockets[id - 1];

			// Valid Socket
			if (sock != NULL && sock->type == SOCK_PDP) {
				if (serverHasRelay)
					return pdp_delete_postoffice(id - 1);

				// Close Connection
				shutdown(sock->data.pdp.id, SD_RECEIVE);
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
			return SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID;
		}

		// Invalid Argument
		return SCE_NET_ADHOC_ERROR_INVALID_ARG;
	}

	// Library is uninitialized
	return SCE_NET_ADHOC_ERROR_NOT_INITIALIZED;
}

/**
 * Adhoc Emulator PDP Socket Delete
 * @param id Socket File Descriptor
 * @param flag Bitflags (Unused)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID
 */
static int sceNetAdhocPdpDelete(int id, int unknown) {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(Log::sceNet, "sceNetAdhocPdpDelete(%d, %d) at %08x", id, unknown, currentMIPS->pc);
	/*
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	*/

	return NetAdhocPdp_Delete(id, unknown);
}

static int sceNetAdhocctlGetAdhocId(u32 productStructAddr) {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlGetAdhocId(%08x) at %08x", productStructAddr, currentMIPS->pc);

	if (!netAdhocctlInited)
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	auto productStruct = PSPPointer<SceNetAdhocctlAdhocId>::Create(productStructAddr);
	if (!productStruct.IsValid())
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	*productStruct = product_code;
	productStruct.NotifyWrite("NetAdhocctlGetAdhocId");

	return hleLogDebug(Log::sceNet, 0, "type = %d, code = %s", product_code.type, product_code.data);
}

// FIXME: Scan probably not a blocking function since there is ADHOCCTL_STATE_SCANNING state that can be polled by the game, right? But apparently it need to be delayed for Naruto Shippuden Ultimate Ninja Heroes 3
int sceNetAdhocctlScan() {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlScan() at %08x", currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	// Library initialized
	if (netAdhocctlInited) {
		int us = adhocDefaultDelay;
		// FIXME: When tested with JPCSP + official prx files it seems when adhocctl in a connected state (ie. joined to a group) attempting to create/connect/join/scan will return a success (without doing anything?)
		if ((adhocctlState == ADHOCCTL_STATE_CONNECTED) || (adhocctlState == ADHOCCTL_STATE_GAMEMODE)) {
			// TODO: Valhalla Knights 2 need handler notification, but need to test this on games that doesn't use Adhocctl Handler too (not sure if there are games like that tho)
			notifyAdhocctlHandlers(ADHOCCTL_EVENT_ERROR, SCE_NET_ADHOCCTL_ERROR_ALREADY_CONNECTED);
			hleEatMicro(500);
			return hleLogDebug(Log::sceNet, 0);
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
				return hleLogDebugOrError(Log::sceNet, WaitBlockingAdhocctlSocket(req, us, "adhocctl scan"));
			}
			else {
				adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
			}

			// Return Success and let friendFinder thread to notify the handler when scan completed
			// Not delaying here may cause Naruto Shippuden Ultimate Ninja Heroes 3 to get disconnected when the mission started
			hleEatMicro(us);
			// FIXME: When tested using JPCSP + official prx files it seems sceNetAdhocctlScan switching to a different thread for at least 100ms after returning success and before executing the next line?
			return hleDelayResult(hleLogDebug(Log::sceNet, 0), "scan delay", adhocEventPollDelay);
		}

		// FIXME: Returning BUSY when previous adhocctl handler's callback is not fully executed yet, But returning success and notifying handler's callback with error (ie. ALREADY_CONNECTED) when previous adhocctl handler's callback is fully executed? Is there a case where error = BUSY sent through handler's callback?
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_BUSY, "busy");
	}

	// Library uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");
}

int sceNetAdhocctlGetScanInfo(u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);
	SceNetAdhocctlScanInfoEmu *buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlScanInfoEmu *)Memory::GetPointer(bufAddr);

	INFO_LOG(Log::sceNet, "sceNetAdhocctlGetScanInfo([%08x]=%i, %08x) at %08x", sizeAddr, Memory::Read_U32(sizeAddr), bufAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogWarning(Log::sceNet, 0, "WLAN off");
	}

	// Library initialized
	if (netAdhocctlInited) {
		// Minimum Argument
		if (buflen == NULL) return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;

		// Minimum Argument Requirements
		if (buflen != NULL) {
			// FIXME: Do we need to exclude Groups created by this device it's self?
			bool excludeSelf = false;

			// Multithreading Lock
			peerlock.lock();

			// FIXME: When already connected to a group GetScanInfo will return size = 0 ? or may be only hides the group created by it's self?
			if (adhocctlState == ADHOCCTL_STATE_CONNECTED || adhocctlState == ADHOCCTL_STATE_GAMEMODE) {
				*buflen = 0;
				DEBUG_LOG(Log::sceNet, "NetworkList [Available: 0] Already in a Group");
			}
			// Length Returner Mode
			else if (buf == NULL) {
				int availNetworks = countAvailableNetworks(excludeSelf);
				*buflen = availNetworks * sizeof(SceNetAdhocctlScanInfoEmu);
				DEBUG_LOG(Log::sceNet, "NetworkList [Available: %i]", availNetworks);
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
				DEBUG_LOG(Log::sceNet, "NetworkList [Requested: %i][Discovered: %i]", requestcount, discovered);
			}

			// Multithreading Unlock
			peerlock.unlock();

			hleEatMicro(200);
			// Return Success
			return hleLogDebug(Log::sceNet, 0);
		}

		// Generic Error
		return hleLogError(Log::sceNet, -1);
	}

	// Library uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED);
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
			ERROR_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = SCE_NET_ADHOCCTL_ERROR_TOO_MANY_HANDLERS;
			return retval;
		}
		adhocctlHandlers[retval] = handler;
		INFO_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, retval);
	} else if(foundHandler) {
		ERROR_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);
		retval = 0; //Faking success
	} else {
		ERROR_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Invalid handler", handlerPtr, handlerArg);
		retval = SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;
	}

	// The id to return is the number of handlers currently registered
	return hleNoLog(retval);
}

u32 NetAdhocctl_Disconnect() {
	// Library initialized
	if (netAdhocctlInited) {
		int iResult, error;
		// We might need to have at least 16ms (1 frame?) delay before the game calls the next Adhocctl syscall for Tekken 6 not to stuck when exiting Lobby
		hleEatMicro(16667);

		if (isAdhocctlBusy && CoreTiming::IsScheduled(adhocctlNotifyEvent)) {
			return SCE_NET_ADHOCCTL_ERROR_BUSY;
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
			error = socket_errno;

			// Sending may get socket error 10053 if the AdhocServer is already shutted down
			if (iResult == SOCKET_ERROR) {
				if (error != EAGAIN && error != EWOULDBLOCK) {
					ERROR_LOG(Log::sceNet, "Socket error (%i) when sending", error);
					// Set Disconnected State
					adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
				}
				else if (friendFinderRunning) {
					AdhocctlRequest req = { OPCODE_DISCONNECT, {0} };
					WaitBlockingAdhocctlSocket(req, 0, "adhocctl disconnect");
				}
				else {
					// Set Disconnected State
					return SCE_NET_ADHOCCTL_ERROR_BUSY;
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
		INFO_LOG(Log::sceNet, "Marked for Timedout Peer List (%i)", peercount);
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
	return SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED;
}

int sceNetAdhocctlDisconnect() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	char grpName[9] = { 0 };
	memcpy(grpName, parameter.group_name.data, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	int ret = NetAdhocctl_Disconnect();
	return hleLogDebug(Log::sceNet, ret, "group=%s", grpName);
}

static u32 sceNetAdhocctlDelHandler(u32 handlerID) {
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "adhocctl not initialized");

	if (adhocctlHandlers.find(handlerID) != adhocctlHandlers.end()) {
		adhocctlHandlers.erase(handlerID);
		return hleLogInfo(Log::sceNet, 0);
	} else {
		return hleLogWarning(Log::sceNet, 0, "Invalid Handler ID");
	}
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
		INFO_LOG(Log::sceNet, "Cleared Peer List (%i)", peercount);
		// Delete Peer Reference
		friends = NULL;
		//May also need to clear Handlers
		adhocctlHandlers.clear();
		// Free stuff here
		g_adhocServerConnected = false;
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

	//if (netAdhocMatchingInited) NetAdhocMatching_Term();
	int retval = NetAdhocctl_Term();

	hleEatMicro(adhocDefaultDelay);
	return hleLogInfo(Log::sceNet, retval);
}

static int sceNetAdhocctlGetNameByAddr(const char *mac, u32 nameAddr) {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlGetNameByAddr(%s, %08x) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), nameAddr, currentMIPS->pc);

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

				DEBUG_LOG(Log::sceNet, "sceNetAdhocctlGetNameByAddr - [PlayerName:%s]", (char*)nickname);

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

					DEBUG_LOG(Log::sceNet, "sceNetAdhocctlGetNameByAddr - [PeerName:%s]", (char*)nickname);

					// Return Success
					return hleLogDebug(Log::sceNet, 0);
				}
			}

			// Multithreading Unlock
			peerlock.unlock();

			// Player not found
			return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NO_ENTRY, "PlayerName not found");
		}

		// Invalid Arguments
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG);
	}

	// Library uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED);
}

int sceNetAdhocctlGetPeerInfo(const char *mac, int size, u32 peerInfoAddr) {
	VERBOSE_LOG(Log::sceNet, "sceNetAdhocctlGetPeerInfo(%s, %i, %08x) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), size, peerInfoAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1);
	}

	SceNetEtherAddr * maddr = (SceNetEtherAddr *)mac;
	SceNetAdhocctlPeerInfoEmu * buf = NULL;
	if (Memory::IsValidAddress(peerInfoAddr)) {
		buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointer(peerInfoAddr);
	}
	// Library initialized
	if (netAdhocctlInited) {
		if ((size < (int)sizeof(SceNetAdhocctlPeerInfoEmu)) || (buf == NULL)) return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;

		int retval = SCE_NET_ADHOC_ERROR_NO_ENTRY; // -1;

		// Local MAC
		if (isLocalMAC(maddr)) {
			SceNetAdhocctlNickname nickname;

			truncate_cpy((char*)&nickname.data, ADHOCCTL_NICKNAME_LEN, g_Config.sNickName);
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
		return hleNoLog(retval);
	}

	// Library uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED);
}

int NetAdhocctl_Create(const char *groupName) {
	// Library initialized
	if (netAdhocctlInited) {
		// Valid Argument
		if (validNetworkName(groupName)) {
			// FIXME: When tested with JPCSP + official prx files it seems when adhocctl in a connected state (ie. joined to a group) attempting to create/connect/join/scan will return a success (without doing anything?)
			if ((adhocctlState == ADHOCCTL_STATE_CONNECTED) || (adhocctlState == ADHOCCTL_STATE_GAMEMODE)) {
				// TODO: Need to test this on games that doesn't use Adhocctl Handler too (not sure if there are games like that tho)
				notifyAdhocctlHandlers(ADHOCCTL_EVENT_ERROR, SCE_NET_ADHOCCTL_ERROR_ALREADY_CONNECTED);
				hleEatMicro(500);
				return 0;
			}

			// Disconnected State
			if (adhocctlState == ADHOCCTL_STATE_DISCONNECTED && !isAdhocctlBusy) {
				isAdhocctlBusy = true;
				isAdhocctlNeedLogin = true;

				// Set Network Name
				if (groupName) {
					strncpy((char *)parameter.group_name.data, groupName, sizeof(parameter.group_name.data));
				} else {
					memset(&parameter.group_name, 0, sizeof(parameter.group_name));
				}

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
			return SCE_NET_ADHOCCTL_ERROR_BUSY; // SCE_NET_ADHOCCTL_ERROR_BUSY may trigger the game (ie. Ford Street Racing) to call sceNetAdhocctlDisconnect
		}

		// Invalid Argument
		return SCE_NET_ADHOC_ERROR_INVALID_ARG;
	}
	// Library uninitialized
	return SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED;
}

/**
 * Create and / or Join a Virtual Network of the specified Name
 * @param group_name Virtual Network Name
 * @return 0 on success or... ADHOCCTL_NOT_INITIALIZED, ADHOCCTL_INVALID_ARG, ADHOCCTL_BUSY
 */
int sceNetAdhocctlCreate(const char *groupName) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (groupName)
		strncpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	INFO_LOG(Log::sceNet, "sceNetAdhocctlCreate(%s) at %08x", grpName, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		ERROR_LOG(Log::sceNet, "sceNetAdhocctlCreate(%s) failed: WLAN off", grpName);
		return -1;  // Not sure about the correct error code here.
	}

	adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
	adhocConnectionType = ADHOC_CREATE;
	return NetAdhocctl_Create(groupName);
}

int sceNetAdhocctlConnect(const char* groupName) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (groupName)
		strncpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	INFO_LOG(Log::sceNet, "sceNetAdhocctlConnect(%s) at %08x", grpName, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		ERROR_LOG(Log::sceNet, "sceNetAdhocctlConnect(%s) failed: WLAN off", grpName);
		return -1;  // Not sure about the correct error code here.
	}

	adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
	adhocConnectionType = ADHOC_CONNECT;
	return NetAdhocctl_Create(groupName);
}

int sceNetAdhocctlJoin(u32 scanInfoAddr) {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlJoin(%08x) at %08x", scanInfoAddr, currentMIPS->pc);
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
			DEBUG_LOG(Log::sceNet, "sceNetAdhocctlJoin - Group: %s", grpName);

			// We can ignore minor connection process differences here
			// TODO: Adhoc Server may need to be changed to differentiate between Host/Create and Join, otherwise it can't support multiple Host using the same Group name, thus causing one of the Host to be confused being treated as Join.
			adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
			adhocConnectionType = ADHOC_JOIN;
			return hleLogDebug(Log::sceNet, NetAdhocctl_Create(grpName));
		}

		// Invalid Argument
		return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;
	}

	// Uninitialized Library
	return SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED;
}

int NetAdhocctl_CreateEnterGameMode(const char* group_name, int game_type, int num_members, u32 membersAddr, u32 timeout, int flag) {
	if (!netAdhocctlInited)
		return SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED;

	if (!Memory::IsValidAddress(membersAddr))
		return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;

	if (game_type < ADHOCCTL_GAMETYPE_1A || game_type > ADHOCCTL_GAMETYPE_2A || num_members < 2 || num_members > 16 || (game_type == ADHOCCTL_GAMETYPE_1A && num_members > 4))
		return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;

	deleteAllGMB();
	gameModePeerPorts.clear();

	SceNetEtherAddr* addrs = PSPPointer<SceNetEtherAddr>::Create(membersAddr); // List of participating MAC addresses (started from host)
	for (int i = 0; i < num_members; i++) {
		requiredGameModeMacs.push_back(*addrs);
		DEBUG_LOG(Log::sceNet, "GameMode macAddress#%d=%s", i, mac2str(addrs).c_str());
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
	// When timeout reached, notify user-defined Adhocctl Handlers with ERROR event (SCE_NET_ADHOC_ERROR_TIMEOUT) instead of GAMEMODE event

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
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlCreateEnterGameMode, Log::sceNet, "UNTESTED sceNetAdhocctlCreateEnterGameMode(%s, %i, %i, %08x, %i, %i) at %08x", grpName, game_type, num_members, membersAddr, timeout, flag, currentMIPS->pc);

	return hleLogDebug(Log::sceNet, NetAdhocctl_CreateEnterGameMode(group_name, game_type, num_members, membersAddr, timeout, flag));
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
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlJoinEnterGameMode, Log::sceNet, "UNTESTED sceNetAdhocctlJoinEnterGameMode(%s, %s, %i, %i) at %08x", grpName, mac2str((SceNetEtherAddr*)hostMac).c_str(), timeout, flag, currentMIPS->pc);

	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (!hostMac)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	deleteAllGMB();

	// Add host mac first
	gameModeMacs.push_back(*(SceNetEtherAddr*)hostMac);

	// FIXME: There seems to be an internal Adhocctl Handler on official prx (running on "SceNetAdhocctl" thread) that will try to sync GameMode timings, by using blocking PTP socket:
	// 1). PtpOpen (srcMacAddress=0x09FE2080, srcPort=0x8001, destMacAddress=0x09F20CB4, destPort=0x8001, bufSize=0x2000, retryDelay=0x30D40, retryCount=0x33, unk1=0x0)
	// 2). PtpConnect (timeout=0x874CAC, nonblock=0x0) - to host/creator
	// 3). PtpRecv (data=0x09F20E18, dataSizeAddr=0x09FE2044, timeout=0x647553, nonblock=0x0) - repeated until data fully received with data address/offset adjusted (increased) and timeout adjusted (decreased), probably also adjusted data size (decreased) on each call
	// 4). PtpClose
	// When timeout reached, notify user-defined Adhocctl Handlers with ERROR event (SCE_NET_ADHOC_ERROR_TIMEOUT) instead of GAMEMODE event

	adhocctlCurrentMode = ADHOCCTL_MODE_GAMEMODE;
	adhocConnectionType = ADHOC_JOIN;
	netAdhocGameModeEntered = true;
	netAdhocEnterGameModeTimeout = timeout;
	return hleLogDebug(Log::sceNet, NetAdhocctl_Create(group_name));
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
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlCreateEnterGameModeMin, Log::sceNet, "UNTESTED sceNetAdhocctlCreateEnterGameModeMin(%s, %i, %i, %i, %08x, %d, %i) at %08x", grpName, game_type, min_members, num_members, membersAddr, timeout, flag, currentMIPS->pc);
	// We don't really need the Minimum User Check
	return hleLogDebug(Log::sceNet, NetAdhocctl_CreateEnterGameMode(group_name, game_type, num_members, membersAddr, timeout, flag));
}

int NetAdhoc_Term() {
	// Since Adhocctl & AdhocMatching uses Sockets & Threads we should terminate them also to release their resources
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
		//return hleLogSuccessInfoI(Log::sceNet, 0);
	}
	/*else {
		// TODO: Reportedly returns SCE_KERNEL_ERROR_LWMUTEX_NOT_FOUND in some cases?
		// Only seen returning 0 in tests.
		return hleLogWarning(Log::sceNet, 0, "already uninitialized");
	}*/

	return 0;
}

int sceNetAdhocTerm() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup all the sockets right?
	int retval = NetAdhoc_Term();

	hleEatMicro(adhocDefaultDelay);
	return hleLogInfo(Log::sceNet, retval);
}

static int sceNetAdhocGetPdpStat(u32 structSize, u32 structAddr) {
	VERBOSE_LOG(Log::sceNet, "sceNetAdhocGetPdpStat(%08x, %08x) at %08x", structSize, structAddr, currentMIPS->pc);

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
			VERBOSE_LOG(Log::sceNet, "Stat PDP Socket Count: %d", socketcount);

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
					if (serverHasRelay) {
						void *postofficeHandle = pdp_postoffice_recover(j);
						if (postofficeHandle != NULL) {
							sock->data.pdp.rcv_sb_cc = pdp_peek_next_size(postofficeHandle);
						}
					} else {
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

					VERBOSE_LOG(Log::sceNet, "Stat PDP Socket Id: %d (%d), LPort: %d, RecvSbCC: %d", buf[i].id, sock->data.pdp.id, buf[i].lport, buf[i].rcv_sb_cc);

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
		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg, at %08x", currentMIPS->pc);
	}

	// Library is uninitialized
	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized, at %08x", currentMIPS->pc);
}


/**
 * Adhoc Emulator PTP Socket List Getter
 * @param buflen IN: Length of Buffer in Bytes OUT: Required Length of Buffer in Bytes
 * @param buf PTP Socket List Buffer (can be NULL if you wish to receive Required Length)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED
 */
static int sceNetAdhocGetPtpStat(u32 structSize, u32 structAddr) {
	// Spams a lot
	VERBOSE_LOG(Log::sceNet,"sceNetAdhocGetPtpStat(%08x, %08x) at %08x",structSize,structAddr,currentMIPS->pc);

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
			VERBOSE_LOG(Log::sceNet, "Stat PTP Socket Count: %d", socketcount);

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
						if (serverHasRelay) {
							if (sock->postofficeHandle != NULL){
								// good to go
								sock->data.ptp.state = ADHOC_PTP_STATE_ESTABLISHED;
							}
						} else {
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
					}

					if (serverHasRelay) {
						if (sock->postofficeHandle != NULL) {
							sock->data.ptp.rcv_sb_cc = ptp_peek_next_size(sock->postofficeHandle);
						} else {
							sock->data.ptp.rcv_sb_cc = 0;
						}
					}else{
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

					VERBOSE_LOG(Log::sceNet, "Stat PTP Socket Id: %d (%d), LPort: %d, RecvSbCC: %d, State: %d", buf[i].id, sock->data.ptp.id, buf[i].lport, buf[i].rcv_sb_cc, buf[i].state);

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
		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg, at %08x", currentMIPS->pc);
	}

	// Library is uninitialized
	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized, at %08x", currentMIPS->pc);
}


int RecreatePtpSocket(int ptpId) {
	auto sock = adhocSockets[ptpId - 1];
	if (!sock) {
		return SCE_NET_ADHOC_ERROR_SOCKET_ID_NOT_AVAIL;
	}

	// Close old socket
	struct linger sl {};
	sl.l_onoff = 1;		// non-zero value enables linger option in kernel
	sl.l_linger = 0;	// timeout interval in seconds
	setsockopt(sock->data.ptp.id, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
	closesocket(sock->data.ptp.id);

	// Create a new socket
	// Socket is remapped through adhocSockets
	int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Valid Socket produced
	if (tcpsocket < 0)
		return SCE_NET_ADHOC_ERROR_SOCKET_ID_NOT_AVAIL;

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
		ERROR_LOG(Log::sceNet, "RecreatePtpSocket(%i) - Socket error (%i) when binding port %u", ptpId, socket_errno, ntohs(addr.sin_port));
	}
	else {
		// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
		socklen_t len = sizeof(addr);
		if (getsockname(tcpsocket, (struct sockaddr*)&addr, &len) == 0) {
			uint16_t boundport = ntohs(addr.sin_port);
			if (sock->data.ptp.lport + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
				WARN_LOG(Log::sceNet, "RecreatePtpSocket(%i) - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", ptpId, sock->data.ptp.lport, requestedport, boundport, boundport - portOffset);
			u16 newlport = boundport - portOffset;
			if (newlport != sock->data.ptp.lport) {
				WARN_LOG(Log::sceNet, "RecreatePtpSocket(%i) - Old and New LPort is different! The port may need to be reforwarded", ptpId);
				if (!sock->isClient)
					UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? newlport : newlport + portOffset, newlport + portOffset);
			}
			sock->data.ptp.lport = newlport;
		}
		else {
			WARN_LOG(Log::sceNet, "RecreatePtpSocket(%i): getsockname error %i", ptpId, socket_errno);
		}
	}

	// Switch to non-blocking for further usage
	changeBlockingMode(tcpsocket, 1);

	return 0;
}

static uint16_t get_random_unused_ptp_port() {
	uint16_t test_port = rand() % 65534 + 1;
	auto port_in_use = [&test_port] {
		for (int i = 0; i < MAX_SOCKET; i++) {
			if (adhocSockets[i] != NULL &&
				adhocSockets[i]->type == SOCK_PTP &&
				test_port == adhocSockets[i]->data.ptp.lport)
			{
				return true;
			}
		}
		return false;
	};

	while(port_in_use()) {
		test_port = rand() % 65534 + 1;
	}

	return test_port;
}

static int ptp_open_postoffice(const SceNetEtherAddr *saddr, uint16_t sport, const SceNetEtherAddr *daddr, uint16_t dport, uint32_t bufsize) {
	AdhocSocket *internal = (AdhocSocket *)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		ERROR_LOG(Log::sceNet, "%s: ran out of heap memory trying to open ptp socket", __func__);
		return ERROR_NET_NO_SPACE;
	}

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
	for (i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] == NULL) {
			slot = &adhocSockets[i];
			break;
		}
	}

	if (slot == NULL) {
		free(internal);
		ERROR_LOG(Log::sceNet, "%s: cannot find free socket mapper slot while opening ptp socket", __func__);
		return ERROR_NET_NO_SPACE;
	}

	internal->data.ptp.id = AEMU_POSTOFFICE_ID_BASE + i;

	*slot = internal;
	INFO_LOG(Log::sceNet, "%s: created ptp socket with id %d", __func__, i + 1);
	return i + 1;
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
	INFO_LOG(Log::sceNet, "sceNetAdhocPtpOpen(%s, %d, %s, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport, mac2str((SceNetEtherAddr*)dstmac).c_str(),dport,bufsize, rexmt_int, rexmt_cnt, flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
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
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_IN_USE, "port in use");
			}

			// Random Port required
			if (sport == 0 && !serverHasRelay) {
				isClient = true;
				//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
				sport = -static_cast<int>(portOffset);
			}

			// Valid Arguments
			if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0) {
				if (serverHasRelay)
					return ptp_open_postoffice(saddr, sport, daddr, dport, bufsize);

				// Create Infrastructure Socket (?)
				// Socket is remapped through adhocSockets
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
						WARN_LOG(Log::sceNet, "sceNetAdhocPtpOpen - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
					}
					addr.sin_port = htons(requestedport);

					// Bound Socket to local Port
					if (bind(tcpsocket, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
						// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
						socklen_t len = sizeof(addr);
						if (getsockname(tcpsocket, (struct sockaddr*)&addr, &len) == 0) {
							uint16_t boundport = ntohs(addr.sin_port);
							if (sport + static_cast<int>(portOffset) >= 65536 || static_cast<int>(boundport) - static_cast<int>(portOffset) <= 0)
								WARN_LOG(Log::sceNet, "sceNetAdhocPtpOpen - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", sport, requestedport, boundport, boundport - portOffset);
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
								INFO_LOG(Log::sceNet, "sceNetAdhocPtpOpen - PSP Socket id: %i, Host Socket id: %i", i + 1, tcpsocket);

								// Return PTP Socket id
								if (g_Config.bForcedFirstConnect && internal->attemptCount == 1) {
									return hleDelayResult(hleLogDebug(Log::sceNet, i + 1), "delayed ptpopen", rexmt_int);
								} else {
									return hleLogDebug(Log::sceNet, i + 1);
								}
							}

							// Free Memory
							free(internal);
						}
					}
					else {
						ERROR_LOG(Log::sceNet, "Socket error (%i) when binding port %u", socket_errno, ntohs(addr.sin_port));
						auto n = GetI18NCategory(I18NCat::NETWORKING);
						g_OSD.Show(OSDType::MESSAGE_ERROR,
							std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 0.0f, "portbindfail");
					}

					// Close Socket
					closesocket(tcpsocket);

					// Port not available (exclusively in use?)
					return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_NOT_AVAIL, "port not available"); // SCE_NET_ADHOC_ERROR_PORT_IN_USE; // SCE_NET_ADHOC_ERROR_INVALID_PORT;
				}
			}

			// Invalid Arguments
			return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
		}

		// Invalid Addresses
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address"); // SCE_NET_ADHOC_ERROR_INVALID_ARG;
	}

	// Library is uninitialized
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "adhoc not initialized");
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

					// Return Socket
					return hleLogDebug(Log::sceNet, i + 1, "Established (%s:%u) - state: %d", ip2str(peeraddr.sin_addr).c_str(), internal->data.ptp.pport, internal->data.ptp.state);
				}

				// Free Memory
				free(internal);
			}
		}
	}

	// Close Socket
	closesocket(newsocket);

	return hleLogError(Log::sceNet, -1, "sceNetAdhocPtpAccept[%i]: Failed (Socket Closed)", ptpId);
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
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpAccept(%d, [%08x]=%s, [%08x]=%u, %d, %u) at %08x", id, peerMacAddrPtr, mac2str(addr).c_str(), peerPortPtr, port ? *port : -1, timeout, flag, currentMIPS->pc);
	} else {
		VERBOSE_LOG(Log::sceNet, "sceNetAdhocPtpAccept(%d, [%08x]=%s, [%08x]=%u, %d, %u) at %08x", id, peerMacAddrPtr, mac2str(addr).c_str(), peerPortPtr, port ? *port : -1, timeout, flag, currentMIPS->pc);
	}
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
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

					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
				}

				// Listener Socket
				if (ptpsocket.state == ADHOC_PTP_STATE_LISTEN) {
					hleEatMicro(50);

					// Address Information
					struct sockaddr_in peeraddr;
					memset(&peeraddr, 0, sizeof(peeraddr));
					socklen_t peeraddrlen = sizeof(peeraddr);
					int error = 0;

					int newsocket = 0;
					if (serverHasRelay) {
						newsocket = ptp_accept_postoffice(id - 1, addr, port);
						if (newsocket >= 0) {
							return newsocket;
						} else {
							newsocket = SOCKET_ERROR;
							error = EAGAIN;
						}
					} else {
						// Check if listening socket is ready to accept
						newsocket = IsSocketReady(ptpsocket.id, true, false, &error);
						if (newsocket > 0) {
							// Accept Connection
							newsocket = accept(ptpsocket.id, (struct sockaddr*)&peeraddr, &peeraddrlen);
							error = socket_errno;
						}
					}

					if (newsocket == 0 || (newsocket == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK))) {
						if (flag == 0) {
							// Simulate blocking behaviour with non-blocking socket
							u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
							return WaitBlockingAdhocSocket(threadSocketId, PTP_ACCEPT, id, nullptr, nullptr, timeout, addr, port, "ptp accept");
						}
						// Prevent spamming Debug Log with retries of non-bocking socket
						else {
							VERBOSE_LOG(Log::sceNet, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", id, error);
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
						return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

					// Timeout
					return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_TIMEOUT, "timeout");
				}

				// Client Socket
				return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_LISTENED, "not listened");
			}

			// Invalid Socket
			return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
		}

		// Invalid Arguments
		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}

	// Library is uninitialized
	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
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

				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
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
					int connectresult = 0;
					int errorcode = 0;
					if (serverHasRelay) {
						connectresult = ptp_connect_postoffice(id - 1, __func__);
						if (connectresult != 0) {
							connectresult = SOCKET_ERROR;
							errorcode = EAGAIN;
						}
					} else {
						connectresult = connect(ptpsocket.id, (struct sockaddr*)&sin, sizeof(sin));
						errorcode = socket_errno;
					}

					if (connectresult == SOCKET_ERROR) {
						if (errorcode == EAGAIN || errorcode == EWOULDBLOCK || errorcode == EALREADY || errorcode == EISCONN)
							DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i]: Socket Error (%i) to %s:%u", id, errorcode, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);
						else
							ERROR_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i]: Socket Error (%i) to %s:%u", id, errorcode, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);
					}

					// Instant Connection (Lucky!)
					if (connectresult != SOCKET_ERROR || errorcode == EISCONN) {
						socket->attemptCount++;
						socket->lastAttempt = CoreTiming::GetGlobalTimeUsScaled();
						socket->internalLastAttempt = socket->lastAttempt;
						// Set Connected State
						ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;

						return hleLogDebug(Log::sceNet, 0, "sceNetAdhocPtpConnect[%i:%u]: Already Connected to %s:%u", id, ptpsocket.lport, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);
					}

					// Error handling
					else if (connectresult == SOCKET_ERROR) {
						// Connection in Progress, or
						// ECONNREFUSED = No connection could be made because the target device actively refused it (on Windows/Linux/Android), or no one listening on the remote address (on Linux/Android) thus should try to connect again later (treated similarly to ETIMEDOUT/ENETUNREACH).
						if (serverHasRelay || connectInProgress(errorcode) || errorcode == ECONNREFUSED) {
							if (serverHasRelay || connectInProgress(errorcode))
							{
								ptpsocket.state = ADHOC_PTP_STATE_SYN_SENT;
							}
							// On Windows you can call connect again using the same socket after ECONNREFUSED/ETIMEDOUT/ENETUNREACH error, but on non-Windows you'll need to recreate the socket first
							else {
								DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: Recreating Socket %i, errno = %i, state = %i, attempt = %i", id, ptpsocket.lport, ptpsocket.id, errorcode, ptpsocket.state, socket->attemptCount);
								if (RecreatePtpSocket(id) < 0) {
									WARN_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: Failed to Recreate Socket", id, ptpsocket.lport);
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
									return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_BUSY, "Socket %d is busy!", ptpsocket.id);
								}

								AdhocSendTargets dest = { 0, {}, false };
								dest.peers.push_back({ sin.sin_addr.s_addr, ptpsocket.pport, finalPortOffset });
								sendTargetPeers[threadSocketId] = dest;
								return WaitBlockingAdhocSocket(threadSocketId, PTP_CONNECT, id, nullptr, nullptr, (flag) ? std::max((int)socket->retry_interval, timeout) : timeout, nullptr, nullptr, "ptp connect");
							}
							// NonBlocking Mode
							else {
								// Returning WOULD_BLOCK as Workaround for SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED to be more cross-platform, since there is no way to simulate SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED properly on Windows
								return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");
							}
						}
					}
				}

				// Peer not found
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address"); // SCE_NET_ADHOC_ERROR_WOULD_BLOCK / SCE_NET_ADHOC_ERROR_TIMEOUT
			}

			// Not a valid Client Socket
			return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_OPENED, "not opened");
		}

		// Invalid Socket
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
	}

	// Library is uninitialized
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
}

/**
 * Adhoc Emulator PTP Connection Opener
 * @param id Socket File Descriptor
 * @param timeout Connect Timeout (in Microseconds)
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_CONNECTION_REFUSED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_NOT_OPENED, ADHOC_THREAD_ABORTED, NET_INTERNAL
 */
static int sceNetAdhocPtpConnect(int id, int timeout, int flag) {
	INFO_LOG(Log::sceNet, "sceNetAdhocPtpConnect(%i, %i, %i) at %08x", id, timeout, flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	return NetAdhocPtp_Connect(id, timeout, flag);
}

static int ptp_close_postoffice(int idx){
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
	adhocSockets[idx] = NULL;
	free(internal);
	INFO_LOG(Log::sceNet, "%s: closed ptp socket with id %d", __func__, idx + 1);
	return 0;
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
				if (serverHasRelay)
					return ptp_close_postoffice(id - 1);

				// Close Connection
				shutdown(socket->data.ptp.id, SD_RECEIVE);
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

			return SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID;
		}

		// Invalid Argument
		return SCE_NET_ADHOC_ERROR_INVALID_ARG;
	}

	// Library is uninitialized
	return SCE_NET_ADHOC_ERROR_NOT_INITIALIZED;
}

/**
 * Adhoc Emulator PTP Socket Closer
 * @param id Socket File Descriptor
 * @param flag Bitflags (Unused)
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED
 */
static int sceNetAdhocPtpClose(int id, int unknown) {
	INFO_LOG(Log::sceNet,"sceNetAdhocPtpClose(%d,%d) at %08x",id,unknown,currentMIPS->pc);
	/*if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}*/

	return NetAdhocPtp_Close(id, unknown);
}

static int ptp_listen_postoffice(const SceNetEtherAddr *saddr, uint16_t sport, uint32_t bufsize) {
	// server connect delegated to accept
	AdhocSocket *internal = (AdhocSocket *)malloc(sizeof(AdhocSocket));
	if (internal == NULL) {
		ERROR_LOG(Log::sceNet, "%s: out of heap memory when creating ptp listen socket", __func__);
		return ERROR_NET_NO_SPACE;
	}

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
	for (i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] == NULL) {
			slot = &adhocSockets[i];
			break;
		}
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
	INFO_LOG(Log::sceNet, "sceNetAdhocPtpListen(%s, %d, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport,bufsize,rexmt_int,rexmt_cnt,backlog,flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
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
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_IN_USE, "port in use");
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
				if (serverHasRelay)
					return ptp_listen_postoffice(saddr, sport, bufsize);

				// Create Infrastructure Socket (?)
				// Socket is remapped through adhocSockets
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
						WARN_LOG(Log::sceNet, "sceNetAdhocPtpListen - Ports below 1024(ie. %hu) may require Admin Privileges", requestedport);
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
								WARN_LOG(Log::sceNet, "sceNetAdhocPtpListen - Wrapped Port Detected: Original(%d) -> Requested(%d), Bound(%d) -> BoundOriginal(%d)", sport, requestedport, boundport, boundport - portOffset);
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
									return hleLogDebug(Log::sceNet, i + 1, "sceNetAdhocPtpListen - PSP Socket id: %i, Host Socket id: %i", i + 1, tcpsocket);
								}

								// Free Memory
								free(internal);
							}
						}
					}
					else {
						auto n = GetI18NCategory(I18NCat::NETWORKING);
						g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 0.0f, "portbindfail");
					}

					if (iResult == SOCKET_ERROR) {
						int error = socket_errno;
						ERROR_LOG(Log::sceNet, "sceNetAdhocPtpListen[%i]: Socket Error (%i)", sport, error);
					}

					// Close Socket
					closesocket(tcpsocket);

					// Port not available (exclusively in use?)
					return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_NOT_AVAIL, "port not available"); //SCE_NET_ADHOC_ERROR_PORT_IN_USE; // SCE_NET_ADHOC_ERROR_INVALID_PORT;
				}

				// Socket not available
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ID_NOT_AVAIL, "socket id not available");
			}

			// Invalid Arguments
			return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
		}

		// Invalid Addresses
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address");
	}

	// Library is uninitialized
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "adhoc not initialized");
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
	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend(%d,%08x,%08x,%d,%d) at %08x", id, dataAddr, dataSizeAddr, timeout, flag, currentMIPS->pc);

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

						return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
					}

					// Acquire Network Lock
					// _acquireNetworkLock();

					// Send Data
					int sent = 0;
					int error = 0;
					if (serverHasRelay) {
						sent = ptp_send_postoffice(id - 1, data, len);
						if (sent == 0) {
							// sent
							hleEatMicro(50);
							return 0;
						}
						if (sent == SOCKET_ERROR) {
							ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
							return SCE_NET_ADHOC_ERROR_DISCONNECTED;
						}
						// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
						sent = SOCKET_ERROR;
						error = EAGAIN;
					}else{
						sent = send(ptpsocket.id, data, *len, MSG_NOSIGNAL);
						error = socket_errno;
					}

					// Free Network Lock
					// _freeNetworkLock();

					// Success
					if (sent > 0) {
						hleEatMicro(50); // mostly 1ms, sometimes 1~10ms ? doesn't seems to be switching to a different thread during this duration
						// Save Length
						*len = sent;

						DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u\n", id, ptpsocket.lport, sent, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

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
							return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

						// Simulate blocking behaviour with non-blocking socket
						u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
						return WaitBlockingAdhocSocket(threadSocketId, PTP_SEND, id, (void*)data, len, timeout, nullptr, nullptr, "ptp send");
					}

					DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend[%i:%u -> %s:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport, sent, error);

					// Change Socket State
					ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

					// Disconnected
					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_DISCONNECTED, "disconnected");
				}

				// Invalid Arguments
				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
			}

			// Not Connected
			return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CONNECTED, "not connected");
		}

		// Invalid Socket
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
	}

	// Library is uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
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
	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv(%d,%08x,%08x,%d,%d) at %08x", id, dataAddr, dataSizeAddr, timeout, flag, currentMIPS->pc);

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

						return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
					}

					// Acquire Network Lock
					// _acquireNetworkLock();

					// TODO: Use a different thread (similar to sceIo) for recvfrom, recv & accept to prevent blocking-socket from blocking emulation
					int received = 0;
					int error = 0;

					if (serverHasRelay) {
						received = ptp_recv_postoffice(id - 1, buf, len);
						if (received == 0) {
							// we got data
							hleEatMicro(50);
							return 0;
						}
						if (received == SOCKET_ERROR) {
							// the socket died, let the game know
							ptpsocket.state = ADHOC_PTP_STATE_CLOSED;
							return SCE_NET_ADHOC_ERROR_DISCONNECTED;
						}
						// SCE_NET_ADHOC_ERROR_WOULD_BLOCK
						received = SOCKET_ERROR;
						error = EAGAIN;
					} else {
						// Receive Data. POSIX: May received 0 bytes when the remote peer already closed the connection.
						received = recv(ptpsocket.id, (char*)buf, std::max(0, *len), MSG_NOSIGNAL);
						error = socket_errno;
					}

					if (received == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && (error == ENOTCONN || connectInProgress(error))))) {
						if (flag == 0) {
							// Simulate blocking behaviour with non-blocking socket
							u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
							return WaitBlockingAdhocSocket(threadSocketId, PTP_RECV, id, buf, len, timeout, nullptr, nullptr, "ptp recv");
						}

						return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");
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

						DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %s:%u\n", id, ptpsocket.lport, received, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport);

						// Set to Established on successful Recv when an attempt to Connect was initiated
						if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT)
							ptpsocket.state = ADHOC_PTP_STATE_ESTABLISHED;

						// Return Success
						return 0;
					}

					DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv[%i:%u]: Result:%i (Error:%i)", id, ptpsocket.lport, received, error);

					if (*len == 0)
						return 0;

					// Change Socket State
					ptpsocket.state = ADHOC_PTP_STATE_CLOSED;

					// Disconnected
					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_DISCONNECTED, "disconnected");
				}

				// Not Connected
				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CONNECTED, "not connected");
			}

			// Invalid Socket
			return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
		}

		// Invalid Arguments
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid socket arg");
	}

	// Library is uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
}

int FlushPtpSocket(int socketId) {
	// Get original Nagle algo value
	int n = getSockNoDelay(socketId);

	// Disable Nagle Algo to send immediately
	setSockNoDelay(socketId, 1);

	// Send Empty Data just to trigger Nagle on/off effect to flush the send buffer, Do we need to trigger this at all or is it automatically flushed?
	//changeBlockingMode(socket->id, nonblock);
	int ret = send(socketId, "", 0, MSG_NOSIGNAL);
	if (ret == SOCKET_ERROR) ret = socket_errno;
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
	DEBUG_LOG(Log::sceNet,"sceNetAdhocPtpFlush(%d,%d,%d) at %08x", id, timeout, nonblock, currentMIPS->pc);

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

				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
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
						return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

					// Simulate blocking behaviour with non-blocking socket
					u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | ptpsocket.id;
					return WaitBlockingAdhocSocket(threadSocketId, PTP_FLUSH, id, nullptr, nullptr, timeout, nullptr, nullptr, "ptp flush");
				}

				if (error != 0)
					DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpFlush[%i:%u -> %s:%u]: Error:%i", id, ptpsocket.lport, mac2str(&ptpsocket.paddr).c_str(), ptpsocket.pport, error);
			}

			// Dummy Result, Always success?
			return 0;
		}

		// Invalid Socket
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
	}
	// Library uninitialized
	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
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
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeCreateMaster(%08x, %i) at %08x", dataAddr, size, currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_ENTER_GAMEMODE, "not enter gamemode");

	if (size < 0 || !Memory::IsValidAddress(dataAddr))
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	if (masterGameModeArea.data)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_ALREADY_CREATED, "already created"); // FIXME: Should we return a success instead? (need to test this on a homebrew)

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
				DEBUG_LOG(Log::sceNet, "GameMode: Blocking Thread %d to Sync initial Master data", __KernelGetCurThread());
			}
		}
		return hleLogDebug(Log::sceNet, 0, "success"); // returned an id just like CreateReplica? always return 0?
	}

	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CREATED, "not created");
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
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeCreateReplica(%s, %08x, %i) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), dataAddr, size, currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_ENTER_GAMEMODE, "not enter gamemode");

	if (mac == nullptr || size < 0 || !Memory::IsValidAddress(dataAddr))
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	hleEatMicro(1000);
	int maxid = 0;
	auto it = std::find_if(replicaGameModeAreas.begin(), replicaGameModeAreas.end(),
		[mac, &maxid](GameModeArea const& e) {
			if (e.id > maxid) maxid = e.id;
			return IsMatch(e.mac, mac);
		});
	// MAC address already existed!
	if (it != replicaGameModeAreas.end()) {
		WARN_LOG(Log::sceNet, "sceNetAdhocGameModeCreateReplica - [%s] is already existed (id: %d)", mac2str((SceNetEtherAddr*)mac).c_str(), it->id);
		return it->id; // SCE_NET_ADHOC_ERROR_ALREADY_CREATED
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
				DEBUG_LOG(Log::sceNet, "GameMode: Blocking Thread %d to Sync initial Master data", __KernelGetCurThread());
			}
		}
		return hleLogInfo(Log::sceNet, ret, "success");
	}

	return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CREATED, "not created");
}

static int sceNetAdhocGameModeUpdateMaster() {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeUpdateMaster() at %08x", currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_ENTER_GAMEMODE, "not enter gamemode");

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
		DEBUG_LOG(Log::sceNet, "GameMode: Blocking Thread %d to End GameMode Scheduler", __KernelGetCurThread());
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
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeDeleteMaster() at %08x", currentMIPS->pc);
	if (isZeroMAC(&masterGameModeArea.mac))
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CREATED, "not created");

	return NetAdhocGameMode_DeleteMaster();
}

static int sceNetAdhocGameModeUpdateReplica(int id, u32 infoAddr) {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeUpdateReplica(%i, %08x) at %08x", id, infoAddr, currentMIPS->pc);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_ENTER_GAMEMODE, "not enter gamemode");

	auto it = std::find_if(replicaGameModeAreas.begin(), replicaGameModeAreas.end(),
		[id](GameModeArea const& e) {
			return e.id == id;
		});

	if (it == replicaGameModeAreas.end())
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CREATED, "not created");

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
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetAdhocGameModeDeleteReplica(int id) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeDeleteReplica(%i) at %08x", id, currentMIPS->pc);
	auto it = std::find_if(replicaGameModeAreas.begin(), replicaGameModeAreas.end(),
		[id](GameModeArea const& e) {
			return e.id == id;
		});

	if (it == replicaGameModeAreas.end())
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CREATED, "not created");

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

	return hleLogDebug(Log::sceNet, 0);
}

int sceNetAdhocGetSocketAlert(int id, u32 flagPtr) {
	WARN_LOG_REPORT_ONCE(sceNetAdhocGetSocketAlert, Log::sceNet, "UNTESTED sceNetAdhocGetSocketAlert(%i, %08x) at %08x", id, flagPtr, currentMIPS->pc);
	if (!Memory::IsValidAddress(flagPtr))
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");

	if (id < 1 || id > MAX_SOCKET || adhocSockets[id - 1] == NULL)
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");

	s32_le flg = adhocSockets[id - 1]->flags;
	Memory::Write_U32(flg, flagPtr);

	return hleLogDebug(Log::sceNet, 0, "flags = %08x", flg);
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
				INFO_LOG(Log::sceNet, "GameMode - All players have joined:");
				int i = 0;
				for (auto& mac : gameModeMacs) {
					INFO_LOG(Log::sceNet, "GameMode macAddress#%d=%s", i++, mac2str(&mac).c_str());
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
				DEBUG_LOG(Log::sceNet, "AdhocctlCallback: [ID=%i][EVENT=%i][Error=%08x]", it->first, flags, error);
				args[2] = it->second.argument;
				AfterAdhocMipsCall* after = (AfterAdhocMipsCall*)__KernelCreateAction(actionAfterAdhocMipsCall);
				after->SetData(it->first, flags, args[2]);
				hleEnqueueCall(it->second.entryPoint, 3, args, after);
			}
			adhocctlEvents.pop_front();
			// Since we don't have beforeAction, simulate it using ScheduleEvent
			ScheduleAdhocctlState(flags, newState, delayus, "adhocctl callback state");
			hleNoLogVoid();
			return;
		}
	}

	// Must be delayed long enough whenever there is a pending callback. Should it be 100-500ms for Adhocctl Events? or Not Less than the delays on sceNetAdhocctl HLE?
	hleCall(ThreadManForUser, int, sceKernelDelayThread, adhocDefaultDelay);
	hleNoLogVoid();
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
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlExitGameMode() at %08x", currentMIPS->pc);

	return NetAdhocctl_ExitGameMode();
}

static int sceNetAdhocctlGetGameModeInfo(u32 infoAddr) {
	DEBUG_LOG(Log::sceNet, "sceNetAdhocctlGetGameModeInfo(%08x)", infoAddr);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (!Memory::IsValidAddress(infoAddr))
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	SceNetAdhocctlGameModeInfo* gmInfo = (SceNetAdhocctlGameModeInfo*)Memory::GetPointer(infoAddr);
	// Writes number of participants and each participating MAC address into infoAddr/gmInfo
	gmInfo->num = static_cast<s32_le>(gameModeMacs.size());
	int i = 0;
	for (auto& mac : gameModeMacs) {
		VERBOSE_LOG(Log::sceNet, "GameMode macAddress#%d=%s", i, mac2str(&mac).c_str());
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

	DEBUG_LOG(Log::sceNet, "sceNetAdhocctlGetPeerList([%08x]=%i, %08x) at %08x", sizeAddr, /*buflen ? *buflen : -1*/Memory::Read_U32(sizeAddr), bufAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
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
				DEBUG_LOG(Log::sceNet, "PeerList [Active: %i]", activePeers);
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
							DEBUG_LOG(Log::sceNet, "Peer [%s][%s][%s][%llu]", mac2str(&peer->mac_addr).c_str(), ip2str(*(in_addr*)&ipaddr).c_str(), (const char*)&peer->nickname.data, peer->last_recv);
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
				DEBUG_LOG(Log::sceNet, "PeerList [Requested: %i][Discovered: %i]", requestcount, discovered);
			}

			// Multithreading Unlock
			peerlock.unlock();

			// Return Success
			return hleDelayResult(0, "delay 100 ~ 1000us", 100); // seems to have different thread running within the delay duration
		}

		// Invalid Arguments
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");
	}

	// Uninitialized Library
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");
}

static int sceNetAdhocctlGetAddrByName(const char *nickName, u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL; //int32_t
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);

	if (!nickName || !buflen) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG);
	}

	char nckName[ADHOCCTL_NICKNAME_LEN];
	memcpy(nckName, nickName, ADHOCCTL_NICKNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	nckName[ADHOCCTL_NICKNAME_LEN - 1] = 0;

	WARN_LOG_REPORT_ONCE(sceNetAdhocctlGetAddrByName, Log::sceNet, "UNTESTED sceNetAdhocctlGetAddrByName(%s, [%08x]=%d/%zu, %08x) at %08x", nckName, sizeAddr, buflen ? *buflen : -1, sizeof(SceNetAdhocctlPeerInfoEmu), bufAddr, currentMIPS->pc);

	// Library initialized
	if (netAdhocctlInited)
	{
		{
			SceNetAdhocctlPeerInfoEmu *buf = NULL;
			if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointer(bufAddr);

			// Multithreading Lock
			peerlock.lock();

			// Length Calculation Mode
			if (buf == NULL) {
				int foundName = getNicknameCount(nickName);
				*buflen = foundName * sizeof(SceNetAdhocctlPeerInfoEmu);
				DEBUG_LOG(Log::sceNet, "PeerNameList [%s: %i]", nickName, foundName);
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

						DEBUG_LOG(Log::sceNet, "Peer [%s][%s][%s][%llu]", mac2str(&mac).c_str(), ip2str(addr.sin_addr).c_str(), nickName, lastrecv);
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
							DEBUG_LOG(Log::sceNet, "Peer [%s][%s][%s][%llu]", mac2str(&peer->mac_addr).c_str(), ip2str(*(in_addr*)&ipaddr).c_str(), (const char*)&peer->nickname.data, peer->last_recv);
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
				DEBUG_LOG(Log::sceNet, "PeerNameList [%s][Requested: %i][Discovered: %i]", nickName, requestcount, discovered);
			}

			// Multithreading Unlock
			peerlock.unlock();

			// Return Success
			return hleDelayResult(hleLogDebug(Log::sceNet, 0, "success"), "delay 100 ~ 1000us", 100); // FIXME: Might have similar delay with GetPeerList? need to know which games using this tho
		}

		// Invalid Arguments
		return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;
	}

	// Library uninitialized
	return SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED;
}

const HLEFunction sceNetAdhocctl[] = {
	{0XE26F226E, &WrapI_IIU<sceNetAdhocctlInit>,                       "sceNetAdhocctlInit",                     'x', "iix"      },
	{0X9D689E13, &WrapI_V<sceNetAdhocctlTerm>,                         "sceNetAdhocctlTerm",                     'i', ""         },
	{0X20B317A0, &WrapU_UU<sceNetAdhocctlAddHandler>,                  "sceNetAdhocctlAddHandler",               'x', "xx"       },
	{0X6402490B, &WrapU_U<sceNetAdhocctlDelHandler>,                   "sceNetAdhocctlDelHandler",               'x', "x"        },
	{0X34401D65, &WrapI_V<sceNetAdhocctlDisconnect>,                   "sceNetAdhocctlDisconnect",               'x', ""         },
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
	WARN_LOG_REPORT_ONCE(sceNetAdhocDiscoverInitStart, Log::sceNet, "UNIMPL sceNetAdhocDiscoverInitStart(%08x) at %08x", paramAddr, currentMIPS->pc);
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
		return hleLogError(Log::sceNet, -1, "invalid param?");
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
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.Unknown1 : %08x", netAdhocDiscoverParam->unknown1);
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.GroupName: [%s]", grpName);
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.Unknown2 : %08x", netAdhocDiscoverParam->unknown2);
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.Result   : %08x", netAdhocDiscoverParam->result);

	// TODO: Check whether we're already in the correct group and change the status and result accordingly
	netAdhocDiscoverIsStopping = false;
	netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS;
	netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND;
	netAdhocDiscoverStartTime = CoreTiming::GetGlobalTimeUsScaled();
	return hleLogInfo(Log::sceNet, 0);
}

// Note1: When canceling the progress, Legend Of The Dragon will use DiscoverStop -> AdhocctlDisconnect -> DiscoverTerm (when status changed to 2)
// Note2: When result = NO_PEER_FOUND or PEER_FOUND the progress can no longer be canceled on Legend Of The Dragon
int sceNetAdhocDiscoverStop() {
	WARN_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverStop()");
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
	WARN_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverTerm() at %08x", currentMIPS->pc);
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
	DEBUG_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverGetStatus() at %08x", currentMIPS->pc);
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
	return hleLogDebug(Log::sceNet, netAdhocDiscoverStatus); // Returning 2 will trigger Legend Of The Dragon to call sceNetAdhocctlGetPeerList (only happened if it was the first sceNetAdhocDiscoverGetStatus after sceNetAdhocDiscoverInitStart)
}

int sceNetAdhocDiscoverRequestSuspend()
{
	ERROR_LOG_REPORT_ONCE(sceNetAdhocDiscoverRequestSuspend, Log::sceNet, "UNIMPL sceNetAdhocDiscoverRequestSuspend() at %08x", currentMIPS->pc);
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
	return hleLogError(Log::sceNet, 0);
}

int sceNetAdhocDiscoverUpdate() {
	DEBUG_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverUpdate() at %08x", currentMIPS->pc);
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
	return hleDelayResult(hleLogDebug(Log::sceNet, 0/*netAdhocDiscoverBufAddr*/), "adhoc discover update", 300); // FIXME: Based on JPCSP+prx, it seems to be returning a pointer to the internal buffer/struct (only when status = 1 ?), But when i stepped the code it returns 0 (might be a bug on JPCSP LLE Logging?)
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
	RegisterHLEModule("sceNetAdhoc", ARRAY_SIZE(sceNetAdhoc), sceNetAdhoc);
}

void Register_sceNetAdhocDiscover() {
	RegisterHLEModule("sceNetAdhocDiscover", ARRAY_SIZE(sceNetAdhocDiscover), sceNetAdhocDiscover);
}

void Register_sceNetAdhocctl() {
	RegisterHLEModule("sceNetAdhocctl", ARRAY_SIZE(sceNetAdhocctl), sceNetAdhocctl);
}

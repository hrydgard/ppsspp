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


#include "ppsspp_config.h"

#include <mutex>
#include <string>

#include "Common/Net/SocketCompat.h"
#include "Common/Data/Format/JSONReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/FileUtil.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Common/Net/HTTPRequest.h"

#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Util/PortManager.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Core.h"
#include "Core/Instance.h"
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
#include "Core/HLE/AdhocTransport.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/proAdhocServer.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/HLE/NetAdhocCommon.h"

#include "ext/aemu_postoffice/client/postoffice_client.h"
#include "ext/aemu_postoffice/adhocctl/client/adhocctl.h"
#include "ext/aemu_postoffice/server_cpp/server.h"
#include "ext/aemu_postoffice/server_cpp/log.h"

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
// How many Updates have run since InitStart, and since Stop (pspautotests net/adhoc/discover).
static int netAdhocDiscoverUpdates = 0;
static int netAdhocDiscoverStopUpdates = 0;

bool netAdhocGameModeEntered = false;

SceUID threadAdhocID;

std::deque<std::pair<u32, u32>> adhocctlEvents;
std::map<int, AdhocctlHandler> adhocctlHandlers;
int adhocctlNotifyEvent = -1;
int adhocctlStateEvent = -1;
int adhocctlPollEvent = -1;
// When the game made its last request to the Adhoc Server, in emulated time. Its event isn't delivered
// earlier than on a PSP (see PspEventDelayUs).
static u64 adhocctlRequestTimeUs = 0;
// Whether the pending DISCONNECT comes from ExitGameMode, which takes longer.
static bool adhocctlDisconnectFromGameMode = false;
// Whether the server has us in a group. The state the game sees only changes when the event is
// delivered, so a Disconnect can come while a Create or Connect is still on its way.
static bool adhocctlInServerGroup = false;

// How long after the request a PSP delivers an event (pspautotests net/adhoc/timing and
// net/adhoc/pair/timing). The state changes along with it. Over the internet the server's answer can
// come later, and then the event waits for it.
static int PspEventDelayUs(u32 flags) {
	switch (flags) {
	case ADHOCCTL_EVENT_CONNECT:
		switch (adhocConnectionType) {
		case ADHOC_CREATE: return 1110000;
		case ADHOC_JOIN: return 530000;
		// Connect looks for the group first: about 3.5 s to join one, 4 s to create it.
		default: return getActivePeerCount() > 0 ? 3500000 : 4000000;
		}
	case ADHOCCTL_EVENT_SCAN: return 1015000;
	case ADHOCCTL_EVENT_DISCONNECT: return adhocctlDisconnectFromGameMode ? 650000 : 105000;
	// GAMEMODE comes 4.38 s after CreateEnterGameMode, or 1.35 s after JoinEnterGameMode, whichever is
	// later, to both at once (net/adhoc/pair/gamemodetiming).
	case ADHOCCTL_EVENT_GAME: return adhocConnectionType == ADHOC_JOIN ? 1350000 : 4380000;
	default: return 0;
	}
}

// A PSP returns from Create, Connect, Join and Scan within about 30 µs (pspautotests net/adhoc/timing).
// The request goes out to the server then, and the call returns once it's sent.
static const int ADHOCCTL_REQUEST_US = 30;

static int RemainingEventDelayUs(u32 flags) {
	s64 left = (s64)(adhocctlRequestTimeUs + PspEventDelayUs(flags)) - (s64)CoreTiming::GetGlobalTimeUs();
	return left > 0 ? (int)left : 0;
}

int adhocSocketNotifyEvent = -1;
std::map<int, AdhocctlRequest> adhocctlRequests;
std::map<u64, AdhocSocketRequest> adhocSocketRequests;
std::map<u64, AdhocSendTargets> sendTargetPeers;
bool serverHasRelay = false;
bool netAdhocUseUdp = false;
bool netAdhocRelayModeEnetPdpReliable = true;
std::chrono::time_point<std::chrono::steady_clock> relayLastFailure;
bool trackingRelayFailure = false;
bool relayDisabled = false;
bool relayFirstConnect = true;
bool g_serverListLoaded = false;
std::thread relayThread;

int gameModeNotifyEvent = -1;

std::thread aemuPostofficeEnetThread;
std::atomic<bool> aemuPostofficeEnetPumping = false;

// Without the local network permission, Android 17+ fails sends to LAN peers with EPERM.
static void WarnIfLocalNetworkBlocked(int error) {
#if PPSSPP_PLATFORM(ANDROID)
	static double lastWarning = -100.0;
	if (error != EPERM || time_now_d() - lastWarning < 10.0) {
		return;
	}
	if (System_GetPermissionStatus(SYSTEM_PERMISSION_LOCAL_NETWORK) != PERMISSION_STATUS_GRANTED) {
		lastWarning = time_now_d();
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		g_OSD.Show(OSDType::MESSAGE_ERROR, n->T("Local network permission is needed to play with nearby players"), 5.0f, "local_network_blocked");
	}
#endif
}

static const char *AdhocDataModeToString(AdhocDataMode mode) {
	switch (mode) {
	case AdhocDataMode::P2P:
		return "AdhocDataMode::P2P";
	case AdhocDataMode::AemuPostoffice:
		return "AdhocDataMode::AemuPostoffice";
	default:
		break;
	}
	return "unknown";
}

// We download the list and cache it on disk.
// The URL can also be a local file path, in which case the download doesn't happen. This is only
// for power users / debugging.
std::mutex g_proAdhocServerListMutex;
std::vector<AdhocServerListEntry> g_proAdhocServerList;

// TODO: Should convert this to use rapidjson
static bool ParseServerListEntriesJSON(std::string_view json) {
	using namespace json;

	// Use our JSONReader to parse json into a list of AdhocServerListEntry.
	json::JsonReader reader(json.data(), json.length());

	if (!reader.ok() || !reader.root()) {
		ERROR_LOG(Log::IO, "Error parsing adhoc server list JSON");
		return false;
	}

	const JsonGet root = reader.root();

	const JsonNode *servers = root.getArray("servers");

	std::vector<AdhocServerListEntry> newList;

	for (const JsonNode *iter : servers->value) {
		JsonGet server = iter->value;
		AdhocServerListEntry entry;
		entry.hidden = server.getBoolOr("hidden", false);
		entry.name = server.getStringOr("name", "");
		entry.discord = server.getStringOr("discord", "");
		entry.host = server.getStringOr("host", "");
		entry.web = server.getStringOr("web", "");
		entry.ip = server.getStringOr("ip", "");
		entry.location = server.getStringOr("location", "");
		if (entry.location == "Unknown") {
			entry.location.clear();
		}
		entry.description = server.getStringOr("description", "");
		entry.mode = equals(server.getStringOr("data_mode", ""), "AemuPostoffice") ? AdhocDataMode::AemuPostoffice : AdhocDataMode::P2P;
		entry.dataJsonUrl = server.getStringOr("status_data_json", "");
		if (entry.dataJsonUrl.empty()) {
			// This second field has a different name because it's more tolerant of int vs string in the json.
			// Allowing these to get into old clients causes a crash. This will be removed after a while.
			entry.dataJsonUrl = server.getStringOr("status_data_json_2", "");
		}
		entry.statusXmlUrl = server.getStringOr("status_xml", "");
		entry.statusWebUrl = server.getStringOr("status_web", "");

		if (entry.host.empty()) {
			// Skipping invalid entry.
			continue;
		}
		newList.push_back(entry);
	}

	{
		std::lock_guard<std::mutex> guard(g_proAdhocServerListMutex);
		g_proAdhocServerList = newList;
	}
	System_PostUIMessage(UIMessage::ADHOC_SERVER_LIST_CHANGED);
	return true;
}

static void LoadFallbackServerList() {
	// Fall back to the asset file. Don't bother doing it on a thread.
	size_t jsonSize;
	std::unique_ptr<uint8_t[]> jsonStr(g_VFS.ReadFile("adhoc-servers.json", &jsonSize));
	if (!jsonStr) {
		ERROR_LOG(Log::sceNet, "Failed to load adhoc server list from assets! Something is badly wrong.");
		_dbg_assert_(false);
		// Something went wrong. This shouldn't happen.
		return;
	}
	ParseServerListEntriesJSON(std::string_view((char*)jsonStr.get(), jsonSize));
	g_serverListLoaded = true;
}

void AdhocLoadServerList(AdhocLoadListMode loadMode) {
	if (loadMode == AdhocLoadListMode::CacheOnlySync) {
		std::lock_guard<std::mutex> guard(g_proAdhocServerListMutex);
		if (!g_proAdhocServerList.empty()) {
			return;
		}
	}

	// NOTE: If you want to load from a local file, set g_Config.sAdhocServerListUrl directly to the local path.
	// There's currently no file:// support, as far as I remember.

	if (startsWith(g_Config.sAdhocServerListUrl, "http")) {
		if (loadMode == AdhocLoadListMode::CacheOnlySync) {
			std::string json;
			if (g_DownloadManager.ReadFileFromCache(g_Config.sAdhocServerListUrl, &json)) {
				if (ParseServerListEntriesJSON(std::string_view(json.data(), json.size()))) {
					g_serverListLoaded = true;
					return;
				}
			}
			INFO_LOG(Log::sceNet, "Failed to load cached adhoc server list %s from cache, falling back.", g_Config.sAdhocServerListUrl.c_str());
			LoadFallbackServerList();
			return;
		}

		// Download the list.
		auto dl = g_DownloadManager.StartDownload(g_Config.sAdhocServerListUrl, Path(), http::RequestFlags::Cached24H, nullptr, "adhoc-servers", [url = g_Config.sAdhocServerListUrl](http::Request &request) {
			if (request.Failed()) {
				ERROR_LOG(Log::sceNet, "Failed to download adhoc server list from %s, falling back.", url.c_str());
				LoadFallbackServerList();
				return;
			}

			INFO_LOG(Log::sceNet, "Successfully downloaded adhoc server list from %s", url.c_str());

			std::string json;
			request.buffer().TakeAll(&json);
			if (!ParseServerListEntriesJSON(std::string_view(json.data(), json.size()))) {
				LoadFallbackServerList();
				return;
			}
		});
		g_serverListLoaded = true;
	} else if (!g_Config.sAdhocServerListUrl.empty()) {
		// Try to read local file.
		std::string json;
		Path path(g_Config.sAdhocServerListUrl);
		if (!File::ReadTextFileToString(path, &json)) {
			ERROR_LOG(Log::sceNet, "Failed to load list from %s, falling back.", path.ToVisualString().c_str());
			LoadFallbackServerList();
			return;
		}
		if (!ParseServerListEntriesJSON(std::string_view(json.data(), json.size()))) {
			LoadFallbackServerList();
		}
	} else {
		LoadFallbackServerList();
	}
}

std::vector<AdhocServerListEntry> AdhocGetServerList(AdhocLoadListMode loadMode) {
	if (!g_serverListLoaded) {
		// We're probably in-game - so we don't want to do a download and risk blocking.
		// Instead we read out of cache, it should be good enough.
		AdhocLoadServerList(loadMode);
	}

	std::lock_guard<std::mutex> guard(g_proAdhocServerListMutex);
	return g_proAdhocServerList;
}

bool AdhocGetServerByHost(std::string_view host, AdhocServerListEntry *dest) {
	std::vector<AdhocServerListEntry> entries = AdhocGetServerList(AdhocLoadListMode::CacheOnlySync);
	for (auto &entry : entries) {
		if (equals(host, entry.host)) {
			*dest = entry;
			return true;
		}
	}
	return false;
}

static AdhocDataMode AdhocGetServerDataMode(std::string_view server) {
	std::vector<AdhocServerListEntry> list = AdhocGetServerList(AdhocLoadListMode::CacheOnlySync);
	for (const auto &item : list) {
		if (equals(server, item.host)) {
			INFO_LOG(Log::sceNet, "server %.*s is in known list, using data mode %s", STR_VIEW(server), AdhocDataModeToString(item.mode));
			return item.mode;
		}
	}

	for (const auto &item : g_Config.vCustomAdhocServerList) {
		if (equals(server, item)) {
			INFO_LOG(Log::sceNet, "server %.*s is in custom list without relay, using data mode %s", STR_VIEW(server), AdhocDataModeToString(AdhocDataMode::P2P));
			return AdhocDataMode::P2P;
		}
	}

	for (const auto &item : g_Config.vCustomAdhocServerListWithRelay) {
		if (equals(server, item)) {
			INFO_LOG(Log::sceNet, "server %.*s is in custom list with relay, using data mode %s", STR_VIEW(server), AdhocDataModeToString(AdhocDataMode::AemuPostoffice));
			return AdhocDataMode::AemuPostoffice;
		}
	}

	INFO_LOG(Log::sceNet, "server %.*s is not in known list, using data mode %s", STR_VIEW(server), AdhocDataModeToString(AdhocDataMode::P2P));
	return AdhocDataMode::P2P;
}

int AcceptPtpSocket(int ptpId, int newsocket, sockaddr_in& peeraddr, SceNetEtherAddr* addr, u16_le* port);
int PollAdhocSocket(SceNetAdhocPollSd* sds, int count, int timeout, int nonblock);
// A PSP hands out socket IDs in increasing order (pspautotests net/adhoc/ptp), starting over from 1
// after sceNetAdhocTerm (pspautotests net/adhoc/socketids).
static int nextAdhocSocketSlot = 0;
int FlushPtpSocket(int socketId);
int RecreatePtpSocket(int ptpId);
int NetAdhocGameMode_DeleteMaster();
int NetAdhocctl_ExitGameMode();


// Only used on the CPU thread, between SetupAdhocRelayMode() and freeing it after the sockets.
static std::unique_ptr<AdhocTransport> transport;

AdhocTransport &Transport() {
	_dbg_assert_(transport);
	return *transport;
}

// Forward declarations for the savestate mechanism (the matching is sadly not inside its own section)
void deleteMatchingEvents(const int matchingId = -1);
void discardMatchingEvents();
void DoNetAdhocMatchingInited(PointerWrap &p);
void DoNetAdhocMatchingThreads(PointerWrap &p);
void ZeroNetAdhocMatchingThreads();
void DoNetAdhocMatchingPollEvent(PointerWrap &p, bool saved);

bool __NetAdhocConnected() {
	return netAdhocInited && netAdhocctlInited && (adhocctlState == ADHOCCTL_STATE_CONNECTED || adhocctlState == ADHOCCTL_STATE_GAMEMODE);
}

void __NetAdhocShutdown() {
	// Kill AdhocServer Thread
	adhocServerRunning = false;
	if (adhocServerThread.joinable()) {
		adhocServerThread.join();
	}

	if (relayThread.joinable()) {
		relayThread.join();
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

static bool pdpDatagramWaiting(int id);
static void pdpFillQueues();
static void pdpDropQueue(const AdhocSocket *sock);

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

				if ((gameModeSocket = sceNetAdhocPdpCreate(currentMIPS, (const char*)&masterGameModeArea.mac, ADHOC_GAMEMODE_PORT, gameModeBuffSize, 0)) < 0) {
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

			// After the initial sync, a PSP sends the master every period whether it changed or not, and the
			// peers' replicas report each one as an update (pspautotests net/adhoc/pair/gamemodesync).
			if (__KernelGetWaitID(threadID, WAITTYPE_NET, error) != GAMEMODE_WAITID || error != 0) {
				masterGameModeArea.dataUpdated = 1;
				for (auto &gma : replicaGameModeAreas)
					gma.dataSent = 0;
			}

			// Send Master data
			if (masterGameModeArea.dataUpdated) {
				int sentcount = 0;
				for (auto& gma : replicaGameModeAreas) {
					if (!gma.dataSent && Transport().PdpReadyToSend(sock)) {
						u16_le port = ADHOC_GAMEMODE_PORT;
						auto it = gameModePeerPorts.find(gma.mac);
						if (it != gameModePeerPorts.end())
							port = it->second;

						int sent = hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, currentMIPS, gameModeSocket, (const char*)&gma.mac, port, masterGameModeArea.data, masterGameModeArea.size, 0, ADHOC_F_NONBLOCK);
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

								hleCall(sceNetAdhoc, int, sceNetAdhocPdpSend, currentMIPS, gameModeSocket, (const char*)&gma.mac, port, masterGameModeArea.data, masterGameModeArea.size, 0, ADHOC_F_NONBLOCK);
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

			// Recv new Replica data when available. Every member sends every period, so take all that's there.
			for (int packets = 0; packets < ADHOCCTL_GAMEMODE_MAX_MEMBERS * 2 && pdpDatagramWaiting(gameModeSocket); ++packets) {
				SceNetEtherAddr sendermac;
				s32_le senderport = ADHOC_GAMEMODE_PORT;
				s32_le bufsz = gameModeBuffSize;
				int ret = sceNetAdhocPdpRecv(currentMIPS, gameModeSocket, &sendermac, &senderport, gameModeBuffer, &bufsz, 0, ADHOC_F_NONBLOCK);
				if (ret < 0 || bufsz <= 0) {
					break;
				}
				{
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

// How often the adhocctl client looks at what the Adhoc Server sent, in emulated time.
static const int ADHOCCTL_POLL_INTERVAL_US = 1000;

static void __AdhocctlPoll(u64 userdata, int cyclesLate) {
	friendFinderPoll();
	// Adhocctl stays connected through sceNetAdhocTerm, which frees the transport.
	if (transport) {
		transport->Poll();
	}
	pdpFillQueues();
	if (friendFinderRunning) {
		CoreTiming::ScheduleEvent(usToCycles(ADHOCCTL_POLL_INTERVAL_US) - cyclesLate, adhocctlPollEvent, 0);
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

	// Socket not found?! Should never happen! But if it ever happened (ie. loaded from an old SaveState, which didn't keep adhocctlRequests) return BUSY and let the game try again.
	if (adhocctlRequests.find(uid) == adhocctlRequests.end()) {
		WARN_LOG(Log::sceNet, "sceNetAdhocctl Socket WaitID(%i) not found!", uid);
		__KernelResumeThreadFromWait(threadID, SCE_NET_ADHOCCTL_ERROR_BUSY);
		return;
	}

	AdhocctlRequest& req = adhocctlRequests[uid];

	if (g_Config.bEnableWlan) {
		const std::lock_guard<std::mutex> adhocctl_guard(adhocctlMutex);
		enum adhocctl_call_status call_status = ADHOCCTL_CALL_WOULDBLOCK;
		if (adhocctlHandle) {
			switch(req.opcode) {
			case OPCODE_CONNECT:{
				call_status = adhocctl_connect(adhocctlHandle, (char *)req.group.data);
				if (call_status == ADHOCCTL_CALL_SUCCESS) {
					adhocctlInServerGroup = true;
					groupJoinInFlight = true;
				}
				break;
			}
			case OPCODE_SCAN:{
				call_status = adhocctl_scan(adhocctlHandle);
				break;
			}
			case OPCODE_DISCONNECT:{
				call_status = adhocctl_disconnect(adhocctlHandle);
				break;
			}
			case OPCODE_LOGIN:{
				call_status = ADHOCCTL_CALL_SUCCESS;
				break;
			}
			default:
				ERROR_LOG(Log::sceNet, "%s: unreachable codepath at %s %d", __func__, __FILE__, __LINE__);
				_dbg_assert_(false);
				call_status = ADHOCCTL_CALL_SUCCESS;
				break;
			}
		}

		if (call_status == ADHOCCTL_CALL_ERROR) {
			// drop the request, we need a new client
			DEBUG_LOG(Log::sceNet, "sceNetAdhocctl[%i]: adhocctl client error", uid);
			req.opcode = 0;
			killAdhocctlClient(true, adhocctl_guard);
		}

		if (call_status == ADHOCCTL_CALL_WOULDBLOCK) {
			bool retry = true;
			u64 now = CoreTiming::GetGlobalTimeUs();
			if (now - adhocctlStartTime > static_cast<u64>(adhocDefaultTimeout) + 500) {
				retry = false;
			}

			if (!isAdhocctlNeedLogin && !adhocctlHandle) {
				// login attempt in friendFinder thread triggered by setting isAdhocctlNeedLogin before WaitBlockingAdhocctlSocket has already failed
				// isAdhocctlNeedLogin was set to true during init/login, scan and connect
				retry = false;
			}

			if (retry) {
				// Try again in another 0.5ms until timedout.
				CoreTiming::ScheduleEvent(usToCycles(500) - cyclesLate, adhocctlNotifyEvent, userdata);
				return;
			} else {
				// we fake success for login (sceNetAdhocctlInit), for others we return busy
				// https://github.com/hrydgard/ppsspp/commit/23efd22efbc7a43522d8200f5a3ea7af809ffd06
				// https://github.com/hrydgard/ppsspp/issues/22806
				if (req.opcode != OPCODE_LOGIN) {
					result = SCE_NET_ADHOCCTL_ERROR_BUSY;
				}
			}
		}

		// ADHOCCTL_CALL_SUCCESS
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
	adhocctlMutex.lock();
	int uid = adhocctlHandle == NULL ? 1 : adhocctlId;
	adhocctlMutex.unlock();

	if (adhocctlRequests.find(uid) != adhocctlRequests.end()) {
		WARN_LOG(Log::sceNet, "sceNetAdhocctl - WaitID[%d] already existed, Socket is busy!", uid);
		return SCE_NET_ADHOCCTL_ERROR_BUSY;
	}

	u64 param = ((u64)__KernelGetCurThread()) << 32 | uid;
	adhocctlStartTime = CoreTiming::GetGlobalTimeUs();
	adhocctlRequestTimeUs = CoreTiming::GetGlobalTimeUs();
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

uint16_t offset_port_simple(uint16_t port) {
	if (port == 0) {
		return 0;
	}
	port += portOffset;
	if (port == 0) {
		return 65535;
	}
	return port;
}

uint16_t reverse_port_simple(uint16_t port) {
	return port - portOffset;
}

// Out of any group (from the moment Disconnect is called): a PSP's radio then reaches nobody, and nobody
// reaches it.
bool offAdhocNetwork() {
	return !adhocctlInServerGroup;
}

bool NetAdhocctl_InServerGroup() {
	return adhocctlInServerGroup;
}

void AbandonGroupJoin() {
	// A queued join completes like a login, sending nothing. (The requests are saved in savestates, so
	// it keeps the format.)
	for (auto &it : adhocctlRequests) {
		if (it.second.opcode == OPCODE_CONNECT) {
			it.second.opcode = OPCODE_LOGIN;
		}
	}
	if (groupJoinInFlight) {
		groupJoinInFlight = false;
		abandonedGroupJoins++;
	}
}

bool isLocalMac(const SceNetEtherAddr *mac) {
	SceNetEtherAddr localMac;
	getLocalMac(&localMac);
	return isMacMatch(&localMac, mac);
}

void ptpSetEstablished(AdhocSocket *sock) {
	sock->data.ptp.state = ADHOC_PTP_STATE_ESTABLISHED;
	sock->ptpEstablishedAt = CoreTiming::GetGlobalTimeUsScaled();
}

// A connect to a port nobody listens on is refused at once, and so is every later connect on the same
// socket, even once something listens there (pspautotests net/adhoc/pair/basic, pair/ptpclose).
void ptpSetRefused(AdhocSocket *sock) {
	sock->data.ptp.state = ADHOC_PTP_STATE_CLOSED;
	sock->ptpRefused = true;
}

static void ptpUpdateConnecting(AdhocSocket *sock) {
	if (sock->data.ptp.state != ADHOC_PTP_STATE_SYN_SENT && sock->data.ptp.state != ADHOC_PTP_STATE_SYN_RCVD) {
		return;
	}
	if (Transport().PtpConnectionUp(sock)) {
		ptpSetEstablished(sock);
	}
}

// A PSP checks the age of a connection's oldest unacknowledged data every rexmt_int, counting from when
// the connection was established, and drops the connection at the first check where that data is older
// than (rexmt_cnt + 1) * rexmt_int. A connection that only receives never fails
// (pspautotests net/adhoc/pair/vanishtiming).
bool ptpPeerGone(const AdhocSocket *sock) {
	u64 leftAt = peerLeftAt(&sock->data.ptp.paddr);
	if (leftAt == 0) {
		return false;
	}
	// The oldest data the peer can't have acknowledged: sent after it left, or so shortly before that the
	// acknowledgement couldn't come back (a round trip takes about 7 ms on a PSP).
	u64 unackedSince = sock->ptpSendAfterPeerLeftAt;
	if (sock->ptpLastSendAt != 0 && sock->ptpLastSendAt + 10000 >= leftAt && sock->ptpLastSendAt < leftAt) {
		unackedSince = sock->ptpLastSendAt;
	}
	if (unackedSince == 0) {
		return false;
	}
	u64 interval = (u64)std::max(1, sock->retry_interval);
	u64 limit = interval * (u64)(std::max(0, sock->retry_count) + 1);
	u64 now = CoreTiming::GetGlobalTimeUsScaled();
	if (now - unackedSince <= limit) {
		return false;
	}
	// The last check, at or before now, has to be past the limit.
	u64 base = std::min(sock->ptpEstablishedAt, unackedSince);
	u64 lastCheck = base + (now - base) / interval * interval;
	return lastCheck > unackedSince + limit;
}

void ptpNoteSend(AdhocSocket *sock) {
	u64 now = CoreTiming::GetGlobalTimeUsScaled();
	if (peerLeftAt(&sock->data.ptp.paddr) == 0) {
		sock->ptpSendAfterPeerLeftAt = 0;
	} else if (sock->ptpSendAfterPeerLeftAt == 0) {
		sock->ptpSendAfterPeerLeftAt = now;
	}
	sock->ptpLastSendAt = now;
}

// PDP receive queues. A PSP keeps what arrives in the socket's buffer, of the size given to PdpCreate, and
// drops a datagram that doesn't fit (pspautotests net/adhoc/pair/flow: a buffer of 0 gets nothing, one of
// 2048 bytes only some of ten 1000-byte datagrams). So what the transport receives goes into a queue of
// that many bytes, filled every millisecond and whenever the game looks, and the game reads from it.
struct PdpDatagram {
	SceNetEtherAddr mac;
	uint16_t port;
	std::vector<u8> data;
};

struct PdpQueue {
	std::deque<PdpDatagram> datagrams;
	u32 bytes = 0;
};

static std::map<const AdhocSocket *, PdpQueue> pdpQueues;

static void pdpQueueDatagram(AdhocSocket *sock, const SceNetEtherAddr &mac, uint16_t port, const u8 *data, size_t len) {
	PdpQueue &queue = pdpQueues[sock];
	// Game mode's socket is ours rather than the game's: on a PSP game mode doesn't go through it.
	bool unlimited = sock->data.pdp.lport == ADHOC_GAMEMODE_PORT;
	if (!unlimited && queue.bytes + len > sock->buffer_size) {
		VERBOSE_LOG(Log::sceNet, "PDP socket on port %d: %d-byte datagram from %s doesn't fit, dropped", sock->data.pdp.lport, (int)len, mac2str(&mac).c_str());
		return;
	}
	queue.datagrams.push_back({ mac, port, std::vector<u8>(data, data + len) });
	queue.bytes += (u32)len;
}

static void pdpFillQueue(int id) {
	AdhocSocket *sock = adhocSockets[id - 1];
	SceNetEtherAddr mac;
	uint16_t port;
	std::vector<u8> data;
	while (Transport().PdpTakeDatagram(id, &mac, &port, data)) {
		pdpQueueDatagram(sock, mac, port, data.data(), data.size());
	}
}

static void pdpFillQueues() {
	for (int i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] != NULL && adhocSockets[i]->type == SOCK_PDP) {
			pdpFillQueue(i + 1);
		}
	}
}

static void pdpDropQueue(const AdhocSocket *sock) {
	pdpQueues.erase(sock);
}

void pdpDropAllQueues() {
	pdpQueues.clear();
}

static bool pdpDatagramWaiting(int id) {
	pdpFillQueue(id);
	return !pdpQueues[adhocSockets[id - 1]].datagrams.empty();
}

// A datagram to ourselves never leaves the console: it's in the socket on its port at once
// (pspautotests net/adhoc/pdp, net/adhoc/term). Nobody on that port, and it's gone.
static void pdpDeliverLocally(uint16_t sport, uint16_t dport, const void *data, int len) {
	SceNetEtherAddr localMac;
	getLocalMac(&localMac);
	for (int i = 0; i < MAX_SOCKET; i++) {
		AdhocSocket *sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PDP && sock->data.pdp.lport == dport) {
			pdpQueueDatagram(sock, localMac, sport, (const u8 *)data, len);
			return;
		}
	}
}

// Takes the first queued datagram into buf, or reports its size and sender with NOT_ENOUGH_SPACE when it
// doesn't fit (it stays queued, pspautotests net/adhoc/pdp). 1 if there's none.
static int pdpTakeFromQueue(AdhocSocket *sock, SceNetEtherAddr *saddr, uint16_t *sport, void *buf, int *len) {
	PdpQueue &queue = pdpQueues[sock];
	if (queue.datagrams.empty()) {
		return 1;
	}
	PdpDatagram &datagram = queue.datagrams.front();
	*saddr = datagram.mac;
	*sport = datagram.port;
	int size = (int)datagram.data.size();
	if (size > *len) {
		*len = size;
		return SCE_NET_ADHOC_ERROR_NOT_ENOUGH_SPACE;
	}
	memcpy(buf, datagram.data.data(), size);
	*len = size;
	queue.bytes -= size;
	queue.datagrams.pop_front();

	peerlock.lock();
	SceNetAdhocctlPeerInfo *peer = findFriend(saddr);
	if (peer != NULL) {
		peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
	}
	peerlock.unlock();
	return 0;
}

int DoBlockingPdpRecv(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	if (sock->flags & ADHOC_F_ALERTRECV) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTRECV;
		return 0;
	}
	pdpFillQueue(req.id);
	uint16_t port = 0;
	int ret = pdpTakeFromQueue(sock, req.remoteMAC, &port, req.buffer, req.length);
	if (ret != 1) {
		*req.remotePort = port;
	}
	if (ret == 1) {
		u64 now = CoreTiming::GetGlobalTimeUs();
		if (req.timeout == 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		result = SCE_NET_ADHOC_ERROR_TIMEOUT;
		return 0;
	}
	result = ret;
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

		int sockerr = 0;
		int ret = Transport().PdpSendTo(req.id, &peer->mac, peer->port, target, req.buffer, targetPeers.length, &sockerr);

		if (ret >= 0) {
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u](B): Sent %u bytes to %s:%u\n", req.id, getLocalPort(pdpsocket.id), ret, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));
			// Remove successfully sent to peer to prevent sending the same data again during a retry
			peer = targetPeers.peers.erase(peer);
		}
		else {
			if (ret == SOCKET_ERROR && (sockerr == EAGAIN || sockerr == EWOULDBLOCK)) {
				u64 now = CoreTiming::GetGlobalTimeUs();
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

int DoBlockingPtpSend(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	if (sock->flags & ADHOC_F_ALERTSEND) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTSEND;
		return 0;
	}
	return Transport().DoBlockingPtpSend(req, result);
}

int DoBlockingPtpRecv(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	if (sock->flags & ADHOC_F_ALERTRECV) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTRECV;
		return 0;
	}
	return Transport().DoBlockingPtpRecv(req, result);
}

int DoBlockingPtpAccept(AdhocSocketRequest& req, s64& result) {
	auto sock = adhocSockets[req.id - 1];
	if (!sock) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_DELETED;
		return 0;
	}
	if (sock->flags & ADHOC_F_ALERTACCEPT) {
		result = SCE_NET_ADHOC_ERROR_SOCKET_ALERTED;
		sock->alerted_flags |= ADHOC_F_ALERTACCEPT;
		return 0;
	}
	return Transport().DoBlockingPtpAccept(req, result);
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

		ret = Transport().PtpConnect(req.id, sin, &sockerr);
		if (sockerr != 0)
			DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: connect(%i) error = %i", req.id, ptpsocket.lport, ptpsocket.id, sockerr);
		else
			ret = 1; // Ensure returned success value from connect to be compatible with returned success value from select (ie. positive value)
	}
	// Check the connection state (assuming "connect" has been called before and is in-progress)
	// Note: On Linux "select" can return > 0 (with SO_ERROR = 0) even when the connection is not accepted yet, thus need "getpeername" to ensure
	else {
		ret = Transport().PtpConnectPoll(sock, &sockerr);
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
	if (ret != SOCKET_ERROR) {
		ret = Transport().PtpConnectVerify(req.id, &sin, ret, &sockerr);
	}

	if (ret == SOCKET_ERROR && sockerr == ECONNREFUSED) {
		ptpSetRefused(sock);
		result = SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED;
		return 0;
	}

	// Update Adhoc Socket state
	if (ret != SOCKET_ERROR || sockerr == EISCONN) {
		ptpSetEstablished(sock);
		INFO_LOG(Log::sceNet, "sceNetAdhocPtpConnect[%i:%u]: Established (%s:%u)", req.id, ptpsocket.lport, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);

		// Done
		result = 0;
	}
	else if (Transport().PtpConnectPending(sockerr) /* || sockerr == 0*/) {
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
		u64 now = CoreTiming::GetGlobalTimeUs();
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
		u64 now = CoreTiming::GetGlobalTimeUs();
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
	if (ret < 0) {
		// A disconnected socket or a poll alert ends the wait at once.
		ret = SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT;
	} else if (ret == 0) {
		u64 now = CoreTiming::GetGlobalTimeUs();
		// A timeout of 0 waits forever, like the rest of sceNet.
		if (static_cast<int>(req.timeout) <= 0 || now - req.startTime <= req.timeout) {
			return -1;
		}
		// Nothing to report by the timeout (pspautotests net/adhoc/poll).
		ret = SCE_NET_ADHOC_ERROR_TIMEOUT;
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
		// Done with it, or the next blocking connect on a socket that reuses this id would be BUSY.
		sendTargetPeers.erase(userdata);
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

	// Timeouts are the PSP's, so they run on the emulated clock.
	u64 startTime = CoreTiming::GetGlobalTimeUs();
	adhocSocketRequests[threadSocketId] = { type, pspSocketId, buffer, len, tmout, startTime, remoteMAC, remotePort };
	// Some games (ie. Hitman Reborn Battle Arena 2) are using as small as 50 usec timeout
	CoreTiming::ScheduleEvent(usToCycles(1), adhocSocketNotifyEvent, threadSocketId);
	__KernelWaitCurThread(WAITTYPE_NET, uid, 0, 0, false, reason);

	// Fallback return value
	return SCE_NET_ADHOC_ERROR_TIMEOUT;
}

static void SetupAdhocRelayMode() {
	switch ((AdhocServerRelayMode)g_Config.iAdhocServerRelayMode) {
	case AdhocServerRelayMode::Auto:
		serverHasRelay = false;
		// Fetch data mode from server list
		if (AdhocGetServerDataMode(g_Config.sProAdhocServer) == AdhocDataMode::AemuPostoffice) {
			serverHasRelay = true;
			trackingRelayFailure = false;
			relayDisabled = false;
			relayFirstConnect = true;
		}
		break;
	case AdhocServerRelayMode::AlwaysOn:
		serverHasRelay = true;
		trackingRelayFailure = false;
		relayDisabled = false;
		relayFirstConnect = true;
		break;
	default:
		serverHasRelay = false;
		break;
	}

	if (serverHasRelay) {
		transport = std::make_unique<RelayTransport>();
	} else {
		transport = std::make_unique<P2PTransport>();
	}
	if (NetSimTransport::Enabled()) {
		// The random numbers are the same for a run each time, but not on every instance.
		transport = std::make_unique<NetSimTransport>(std::move(transport), PPSSPP_ID);
	}

	netAdhocUseUdp = g_Config.bAdhocUseUdp;
	netAdhocRelayModeEnetPdpReliable = g_Config.bAdhocRelayModeEnetPdpReliable;

	init_aemu_postoffice(netAdhocUseUdp);
	// Closing a session normally first sleeps 50 ms, in case another thread is about to use it. Here
	// everything but a PTP connect's own thread runs on the CPU thread, and that thread is joined
	// before its session is closed.
	set_close_delay_aemu_postoffice(0);

	if (netAdhocUseUdp) {
		aemuPostofficeEnetPumping = true;
		aemuPostofficeEnetThread = std::thread([] {
			while (aemuPostofficeEnetPumping) {
				// no std::chrono::high_resolution_clock::now() in msvc.....
				static const double target_interval = 1.0 / 120;
				double begin = time_now_d();
				int pump_status = pump_enet_aemu_postoffice();
				if (pump_status == 1) {
					double time_spent = time_now_d() - begin;
					if (target_interval > time_spent) {
						sleep_us((target_interval - time_spent) * 1000000, "adhoc enet pump");
					}
				}
				if (pump_status == -1) {
					ERROR_LOG(Log::sceNet, "adhoc enet pumping has failed");
					return;
				}
			}
		});
	}
}

static void StartFriendFinder() {
	if (!friendFinderRunning) {
		friendFinderStart();
		friendFinderRunning = true;
	}
	if (!CoreTiming::IsScheduled(adhocctlPollEvent)) {
		CoreTiming::ScheduleEvent(usToCycles(ADHOCCTL_POLL_INTERVAL_US), adhocctlPollEvent, 0);
	}
}

// Stops the adhocctl client and drops the connection to the Adhoc Server, leaving the PSP side
// (handlers, AdhocThread) alone.
static void StopFriendFinder() {
	friendFinderRunning = false;
	friendFinderStop();
	adhocctlInServerGroup = false;

	int32_t peercount = 0;
	freeFriendsRecursive(friends, &peercount);
	friends = NULL;

	const std::lock_guard<std::mutex> adhocctl_guard(adhocctlMutex);
	killAdhocctlClient(false, adhocctl_guard);
}

void __NetAdhocDoState(PointerWrap &p) {
	auto s = p.Section("sceNetAdhoc", 1, 13);
	if (!s)
		return;

	auto cur_netAdhocInited = netAdhocInited;
	auto cur_netAdhocctlInited = netAdhocctlInited;

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
		// Not saved: a game mode being entered gets its whole timeout again after a load.
		gameModeEnterTimeUs = CoreTiming::GetGlobalTimeUs();
	}
	else {
		isAdhocctlBusy = false;
		netAdhocGameModeEntered = false;
		netAdhocEnterGameModeTimeout = 15000000;
	}
	if (s >= 9) {
		// A thread may be waiting on one (sceNetAdhocctlInit waits for the login.) Without it, the
		// wait ends in an error, which for Init can't be retried: the library already counts as inited.
		Do(p, adhocctlRequests);
		if (p.mode == p.MODE_READ) {
			adhocctlStartTime = CoreTiming::GetGlobalTimeUs();
		}
	} else if (p.mode == p.MODE_READ) {
		adhocctlRequests.clear();
	}
	if (s >= 10) {
		// The login after a load sends it, so a state loaded into a fresh boot needs it.
		p.DoVoid(&product_code, (int)sizeof(product_code));
	}
	if (s >= 11) {
		Do(p, adhocctlPollEvent);
	} else {
		adhocctlPollEvent = -1;
	}
	CoreTiming::RestoreRegisterEvent(adhocctlPollEvent, "__AdhocctlPoll", __AdhocctlPoll);
	if (s >= 12) {
		Do(p, adhocctlRequestTimeUs);
	} else {
		adhocctlRequestTimeUs = 0;
	}
	DoNetAdhocMatchingPollEvent(p, s >= 13);

	if (p.mode == p.MODE_READ) {
		// The login after a load starts outside any group.
		adhocctlInServerGroup = false;
		// Discard leftover events
		{
			std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
			adhocctlEvents.clear();
		}
		adhocSocketRequests.clear();
		sendTargetPeers.clear();
		deleteAllAdhocSockets();
		discardMatchingEvents();

		// The game remembers which libraries it initialized, so the "Inited" flags have to come
		// from the state too. Bring the host side (relay, friendFinder, server login) in line.
		// Keeping the current flags instead left a state saved with adhocctl inited, loaded into a
		// fresh boot, failing every call with NOT_INITIALIZED; MGS Peace Walker retried
		// sceNetAdhocctlDisconnect forever when starting a cutscene.
		if (netAdhocInited && !cur_netAdhocInited) {
			// spin up adhoc
			SetupAdhocRelayMode();
		}
		if (netAdhocctlInited && !cur_netAdhocctlInited) {
			isAdhocctlNeedLogin = true;
			StartFriendFinder();
		} else {
			if (!netAdhocctlInited && cur_netAdhocctlInited) {
				// The thread sets adhocctlState on its way out, so keep the loaded one.
				const int loadedState = adhocctlState;
				StopFriendFinder();
				adhocctlState = loadedState;
			}
			isAdhocctlNeedLogin = false;
		}
		// A state from before the poll event, or one saved with the client stopped, has none queued.
		if (netAdhocctlInited) {
			StartFriendFinder();
		}
		if (!netAdhocInited && cur_netAdhocInited) {
			// cleanup adhoc (the sockets are gone already)
			transport.reset();
			if (aemuPostofficeEnetThread.joinable()) {
				aemuPostofficeEnetPumping = false;
				aemuPostofficeEnetThread.join();
				// one more pump to best effort send the final disconnects
				pump_enet_aemu_postoffice();
			}
			deinit_aemu_postoffice();
		}
	}
}

void __UpdateAdhocctlHandlers(u32 flag, u32 error) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	adhocctlEvents.push_back({ flag, error });
}

void NetAdhocctl_GameModeTimedOut() {
	AbandonGroupJoin();
	if (adhocctlInServerGroup) {
		const std::lock_guard<std::mutex> adhocctl_guard(adhocctlMutex);
		if (adhocctlHandle != NULL) {
			adhocctl_disconnect(adhocctlHandle);
		}
		adhocctlInServerGroup = false;
	}
	adhocctlCurrentMode = ADHOCCTL_MODE_NONE;
	isAdhocctlBusy = false;
}

void __DropAdhocctlEvents(u32 flag) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	adhocctlEvents.erase(std::remove_if(adhocctlEvents.begin(), adhocctlEvents.end(), [flag](const std::pair<u32, u32> &e) {
		return e.first == flag;
	}), adhocctlEvents.end());
}

void __AdhocNotifInit() {
	adhocctlNotifyEvent = CoreTiming::RegisterEvent("__AdhocctlNotify", __AdhocctlNotify);
	adhocSocketNotifyEvent = CoreTiming::RegisterEvent("__AdhocSocketNotify", __AdhocSocketNotify);
	gameModeNotifyEvent = CoreTiming::RegisterEvent("__GameModeNotify", __GameModeNotify);
	adhocctlStateEvent = CoreTiming::RegisterEvent("__AdhocctlState", __AdhocctlState);
	adhocctlPollEvent = CoreTiming::RegisterEvent("__AdhocctlPoll", __AdhocctlPoll);

	adhocctlRequests.clear();
	adhocSocketRequests.clear();
	sendTargetPeers.clear();
}

static void aemu_postoffice_server_log(const char *format, ...) {
	char log_buf[1024] = {0};
	va_list args;
	va_start(args, format);
	int len = vsnprintf(log_buf, sizeof(log_buf), format, args);
	va_end(args);
	if (len > 0 && len < (int)sizeof(log_buf) && log_buf[len - 1] == '\n') {
		log_buf[len - 1] = '\0';
	}
	INFO_LOG(Log::sceNet, "%s", log_buf);
}

void __NetAdhocInit() {
	nextAdhocSocketSlot = 0;
	friendFinderRunning = false;
	netAdhocInited = false;
	netAdhocctlInited = false;
	adhocctlHandlers.clear();
	__AdhocNotifInit();
	__AdhocServerInit();

	// Create built-in AdhocServer Thread. The flag is set here rather than by the thread, so that a
	// shutdown that clears it before the thread gets going can't be undone.
	adhocServerRunning = false;
	if (adhocServerThread.joinable()) {
		adhocServerThread.join();
	}
	if (relayThread.joinable()) {
		relayThread.join();
	}
	if (g_Config.bEnableWlan && g_Config.bEnableAdhocServer) {
		adhocServerRunning = true;
		adhocServerThread = std::thread(proAdhocServerThread, SERVER_PORT);

		// relay thread
		relayThread = std::thread([] () {
			SetCurrentThreadName("AEMUPostoffice");

			aemu_postoffice_server::LOG = aemu_postoffice_server_log;

			struct aemu_postoffice_server::config config;
			/*
			 * strict mode requires adhocctl data to be fed into the relay, either from
			 * aemu_postoffice's implementation of adhocctl, or by constructing the snapshot
			 * from ppsspp's adhocctl server
			 */
			config.strict_mode = false;
			// No worker threads: for a server that sees a handful of players, pumping on this thread
			// beats waking workers several times per pump.
			config.num_threads = 0;
			// The port the relay client connects to.
			config.port = AEMU_POSTOFFICE_PORT;
			// Disable ENet until the adhocctl side is ready
			config.enable_enet = false;

			aemu_postoffice_server::Server relay_server(config);
			if (relay_server.pump() == aemu_postoffice_server::ServerPumpStatus::LISTEN_SOCK_DEAD) {
				// Most likely another relay already has the port.
				WARN_LOG(Log::sceNet, "RelayServer: Couldn't listen on TCP port %u", config.port);
				return;
			}

			UPnP_Add(IP_PROTOCOL_TCP, config.port);

			INFO_LOG(Log::sceNet, "RelayServer: Listening for Connections on TCP Port %u", config.port);

			while (adhocServerRunning) {
				auto pump_status = relay_server.pump();
				if (pump_status == aemu_postoffice_server::ServerPumpStatus::LISTEN_SOCK_DEAD) {
					break;
				}
				// Pump again as soon as something arrives, so a message is relayed at once. The timeout
				// only bounds how long it takes to notice adhocServerRunning going false.
				relay_server.wait(10);
			}

			INFO_LOG(Log::sceNet, "RelayServer: Shutdown complete");

			UPnP_Remove(IP_PROTOCOL_TCP, config.port);
		});
	}
}

u32 sceNetAdhocInit(MIPSState *mips) {
	if (!netAdhocInited) {
		// Library initialized
		netAdhocInited = true;
		isAdhocctlBusy = false;

		// FIXME: It seems official prx is using sceNetAdhocGameModeDeleteMaster in here?
		NetAdhocGameMode_DeleteMaster();
		// Since we are deleting GameMode Master here, we should probably need to make sure GameMode resources all cleared too.
		deleteAllGMB();

		SetupAdhocRelayMode();

		auto n = GetI18NCategory(I18NCat::NETWORKING);
		if (g_Config.bAdhocUseUdp && g_Config.bEnableAdhocServer && isLocalServer) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, n->T("Built-in adhoc server does not currently support UDP"), 20.0f, "udp built-in server warning");
		}

		if (g_Config.bEnableWlan) {
			std::string_view modeStr = serverHasRelay ? n->T("Relay server mode") : n->T("P2P mode");
			g_OSD.Show(OSDType::MESSAGE_INFO, ApplySafeSubstitutions("%1: %2", n->T("Ad Hoc multiplayer"), modeStr), 0.0f, "adhoc started");
		}

		// Return Success
		return hleLogInfo(Log::sceNet, 0, "at %08x", mips->pc);
	}
	// Already initialized
	return hleLogWarning(Log::sceNet, SCE_NET_ADHOC_ERROR_ALREADY_INITIALIZED, "already initialized");
}

int sceNetAdhocctlInit(MIPSState *mips, int stackSize, int prio, u32 productAddr) {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlInit(%i, %i, %08x) at %08x", stackSize, prio, productAddr, mips->pc);

	// FIXME: Returning 0x8002013a (SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED) without adhoc module loaded first?
	// FIXME: Sometimes returning 0x80410601 (SCE_NET_ADHOC_ERROR_AUTH_ALREADY_INITIALIZED / Library module is already initialized ?) when AdhocctlTerm is not fully done?

	if (netAdhocctlInited) {
		// On a PSP this also leaves adhocctl uninitialized (GetState fails with NOT_INITIALIZED), and
		// a further Init fails with AUTH_ALREADY_INITIALIZED until Term (pspautotests net/adhoc/adhocctl).
		// Not emulated: a game doing this would lose its connection, and none has been seen to.
		ERROR_LOG_REPORT(Log::sceNet, "sceNetAdhocctlInit called while already initialized - a PSP would now be left uninitialized, which isn't emulated");
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_ALREADY_INITIALIZED);
	}

	// pspautotests net/adhoc/adhocctl. The priority error comes from creating the library's thread,
	// which the firmware does as a kernel caller (threads/threads/kernel/priority).
	if (stackSize < 0xC00) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_STACKSIZE_TOO_SHORT, "stack too small");
	}
	if (prio < 0x01 || prio > 0x7E) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_PRIORITY, "bad priority");
	}
	const auto adhocId = PSPPointer<SceNetAdhocctlAdhocId>::Create(productAddr);
	if (!adhocId.IsValid() || adhocId->type < 0 || adhocId->type > 2) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "bad adhoc id");
	}

	if (g_Config.bEnableWlan) {
		// Needed for peers and ad hoc servers on the same LAN (Android 17+).
		System_RequestLocalNetworkPermission();
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

	StartFriendFinder();

	// Init doesn't wait for the login to the Adhoc Server, and a Create, Connect or Scan right after it
	// works, as on a PSP (pspautotests net/adhoc/ctlinit): those requests wait for the login instead.
	hleEatMicro(adhocDefaultDelay);

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
	Memory::WriteOrException_U32(state, ptrToStatus);

	// Return Success
	return hleLogVerbose(Log::sceNet, 0, "state = %d", state);
}

// Out of socket IDs is its own error (pspautotests net/adhoc/limits).
// The search for a free slot starts after the last one handed out, and wraps around (see
// nextAdhocSocketSlot). Returns MAX_SOCKET when all are taken.
int FindFreeAdhocSocketSlot() {
	for (int n = 0; n < MAX_SOCKET; n++) {
		int i = (nextAdhocSocketSlot + n) % MAX_SOCKET;
		if (adhocSockets[i] == NULL) {
			nextAdhocSocketSlot = (i + 1) % MAX_SOCKET;
			return i;
		}
	}
	return MAX_SOCKET;
}

// A PSP uses up an ID on a create that fails its address or port check.
static void SkipAdhocSocketId() {
	nextAdhocSocketSlot = (nextAdhocSocketSlot + 1) % MAX_SOCKET;
}

static bool HasFreeAdhocSocketSlot() {
	for (int i = 0; i < MAX_SOCKET; i++) {
		if (adhocSockets[i] == NULL) {
			return true;
		}
	}
	return false;
}

// A free local port for a PDP socket created on port 0, in the range a PSP picks from (0x1000-0x7FFF).
uint16_t get_unused_pdp_port() {
	for (int tries = 0; tries < 1000; tries++) {
		uint16_t test_port = 0x1000 + rand() % 0x7000;
		bool used = false;
		for (int i = 0; i < MAX_SOCKET; i++) {
			if (adhocSockets[i] != NULL && adhocSockets[i]->type == SOCK_PDP && adhocSockets[i]->data.pdp.lport == test_port) {
				used = true;
				break;
			}
		}
		if (!used) {
			return test_port;
		}
	}
	return 0x1000;
}

int NetAdhocPdpCreate(const char *mac, int port, int bufferSize, u32 flag, bool postoffice_enet_pdp_reliable) {
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	if (!g_netInited) {
		return 0x800201CA; //PSP_LWMUTEX_ERROR_NO_SUCH_LWMUTEX
	}

	if (!netAdhocInited) {
		return SCE_NET_ADHOC_ERROR_NOT_INITIALIZED;
	}

	// Checked in this order on the PSP (pspautotests net/adhoc/pdp). Any buffer size is accepted.
	if (mac == NULL || flag != 0) {
		DEBUG_LOG(Log::sceNet, "%s: bad arg", __func__);
		return SCE_NET_ADHOC_ERROR_INVALID_ARG;
	}

	const SceNetEtherAddr *requestedMac = (const SceNetEtherAddr *)mac;
	// These three failures use up a socket ID on a PSP (pspautotests net/adhoc/pdp).
	if (adhocctlCurrentMode == ADHOCCTL_MODE_NONE || isZeroMAC(requestedMac) || (requestedMac->data[0] & 1) != 0) {
		DEBUG_LOG(Log::sceNet, "%s: invalid mac address supplied", __func__);
		SkipAdhocSocketId();
		return SCE_NET_ADHOC_ERROR_INVALID_ADDR;
	}

	if (port == 0xFFFF) {
		SkipAdhocSocketId();
		return SCE_NET_ADHOC_ERROR_INVALID_PORT;
	}

	// A buffer of 0 receives nothing (pspautotests net/adhoc/pair/flow), which the receive queue takes care
	// of. The host socket still gets one datagram's worth.
	int queueSize = std::max(0, bufferSize);
	if (bufferSize <= 0) {
		bufferSize = PSP_ADHOC_PDP_MFS;
	}

	if (!HasFreeAdhocSocketSlot()) {
		return SCE_NET_ADHOC_ERROR_SOCKET_ID_NOT_AVAIL;
	}

	if (isPDPPortInUse(port)) {
		DEBUG_LOG(Log::sceNet, "%s: port in-use", __func__);
		SkipAdhocSocketId();
		return SCE_NET_ADHOC_ERROR_PORT_IN_USE;
	}

	bool isClient = false;

	// Port 0 gets a free port in 0x1000-0x7FFF, as on a PSP (pspautotests net/adhoc/limits). Picking it
	// here rather than letting the host choose also keeps the port offset from turning it into a fixed
	// port (Phantasy Star Portable 2 creates several on port 0).
	if (port == 0) {
		isClient = true;
		port = get_unused_pdp_port();
	}

	SceNetEtherAddr * saddr = (SceNetEtherAddr *)mac;
	// Deliberately not like a PSP: whatever MAC the game passes as the local address, use ours. A PSP
	// rejects anything but its own MAC with INVALID_ADDR (pspautotests net/adhoc/pdp and ptp, "Other mac",
	// which is why they're in tests_next), but a PSP's MAC never changes and PPSSPP's can (another
	// install, a changed setting). A savestate made with another MAC brings the old one back in the
	// game's memory, and a game like DBZ Shin Budokai 2 then passes it and could never create a socket
	// (added for it in 2019). The same goes for PtpOpen and PtpListen. The game's copy isn't written to: it
	// may be a table entry the game reads later.
	SceNetEtherAddr localSrcMac;
	if (saddr != NULL) {
		getLocalMac(&localSrcMac);
		saddr = &localSrcMac;
	}


	if ((adhocctlCurrentMode == ADHOCCTL_MODE_NONE) || !isLocalMAC(saddr)) {
		DEBUG_LOG(Log::sceNet, "%s: invalid mac address supplied", __func__);
		return SCE_NET_ADHOC_ERROR_INVALID_ADDR;
	}

	int id = Transport().PdpCreate(saddr, port, bufferSize, flag, isClient, postoffice_enet_pdp_reliable);
	if (id > 0) {
		adhocSockets[id - 1]->buffer_size = queueSize;
	}
	return id;
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

int sceNetAdhocPdpCreate(MIPSState *mips, const char *mac, int port, int bufferSize, u32 flag) {
	INFO_LOG(Log::sceNet, "sceNetAdhocPdpCreate(%s, %u, %u, %u) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), port, bufferSize, flag, mips->pc);

	int ret = NetAdhocPdpCreate(mac, port, bufferSize, flag, netAdhocRelayModeEnetPdpReliable);
	if (ret < 0) {
		return hleLogDebug(Log::sceNet, ret);
	}
	return ret;
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

	// Only in a group (pspautotests net/adhoc/adhocctl).
	if (adhocctlState == ADHOCCTL_STATE_DISCONNECTED) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_DISCONNECTED, "not in a group");
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
int sceNetAdhocPdpSend(MIPSState *mips, int id, const char *mac, u32 port, void *data, int len, int timeout, int flag) {
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i) at %08x", id, mac2str((SceNetEtherAddr*)mac).c_str(), port, data, len, timeout, flag, mips->pc);
	} else {
		VERBOSE_LOG(Log::sceNet, "sceNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i) at %08x", id, mac2str((SceNetEtherAddr*)mac).c_str(), port, data, len, timeout, flag, mips->pc);
	}
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1);
	}
	SceNetEtherAddr * daddr = (SceNetEtherAddr *)mac;
	uint16_t dport = (uint16_t)port;

	// Checked in this order on the PSP (pspautotests net/adhoc/pdp).
	if (daddr == NULL || (flag & ~ADHOC_F_NONBLOCK) != 0) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}
	if (!netAdhocInited) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
	}
	if (id <= 0 || id > MAX_SOCKET || adhocSockets[id - 1] == NULL) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
	}
	// Broadcast is the only multicast address allowed.
	if (isZeroMAC(daddr) || ((daddr->data[0] & 1) != 0 && !isBroadcastMAC(daddr))) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address");
	}
	if (dport == 0 || dport == 0xFFFF) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_PORT, "invalid port");
	}
	// NULL data is only accepted with a length of 0.
	if (len < 0 || len > PSP_ADHOC_PDP_MTU || (data == NULL && len != 0)) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_DATALEN, "invalid data length");
	}

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
					if (data != NULL || len == 0) {
						// Valid Destination Address
						if (daddr != NULL && !isZeroMAC(daddr)) {
							// Log Destination
							// Schedule Timeout Removal
							//if (flag) timeout = 0;


							if (socket->flags & ADHOC_F_ALERTSEND) {
								socket->alerted_flags |= ADHOC_F_ALERTSEND;

								return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
							}

							// Single Target
							if (!isBroadcastMAC(daddr)) {
								if (isLocalMac(daddr)) {
									hleEatMicro(AdhocPdpSendUs(len));
									pdpDeliverLocally(pdpsocket.lport, dport, data, len);
									return 0;
								}
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
									int error = 0;
									int sent = Transport().PdpSendTo(id, daddr, dport, target, data, len, &error);

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
										WarnIfLocalNetworkBlocked(error);
									}
									//changeBlockingMode(socket->id, 0);

									// Free Network Lock
									//_freeNetworkLock();

									hleEatMicro(AdhocPdpSendUs(len));
									// Sent Data
									if (sent >= 0) {
										DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u]: Sent %u bytes to %s:%u\n", id, getLocalPort(pdpsocket.id), sent, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));

										// Success
										return 0; // 0, not the length, as on the PSP (pspautotests net/adhoc/pdp). MotorStorm resends otherwise.
									}

									// Non-Blocking
									if (flag)
										return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");

									// Does PDP can Timeout? There is no concept of Timeout when sending UDP due to no ACK, but might happen if the socket buffer is full, not sure about PDP since some games did use the timeout arg
									return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_TIMEOUT, "timeout?"); // SCE_NET_ADHOC_ERROR_INVALID_ADDR;
								}
								VERBOSE_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u]: Unknown Target Peer %s:%u (nobody there)\n", id, getLocalPort(pdpsocket.id), mac2str(daddr).c_str(), ntohs(target.sin_port));
								hleEatMicro(AdhocPdpSendUs(len));
								return 0; // The PSP sends it into the air and returns 0 (pspautotests net/adhoc/pdp).
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

										int error = 0;
										int sent = Transport().PdpSendTo(id, &peer.mac, dport, target, data, len, &error);

										if (sent == SOCKET_ERROR) {
											DEBUG_LOG(Log::sceNet, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u](BC) [size=%i]", error, id, getLocalPort(pdpsocket.id), ntohs(target.sin_port), len);
											WarnIfLocalNetworkBlocked(error);
										}

										if (sent >= 0) {
											DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpSend[%i:%u](BC): Sent %u bytes to %s:%u\n", id, getLocalPort(pdpsocket.id), sent, ip2str(target.sin_addr).c_str(), ntohs(target.sin_port));
										}
									}
								}

								//changeBlockingMode(socket->id, 0);

								// Free Network Lock
								//_freeNetworkLock();

								hleEatMicro(AdhocPdpSendUs(len));
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
int sceNetAdhocPdpRecv(MIPSState *mips, int id, void *addr, void * port, void *buf, void *dataLength, u32 timeout, int flag) {
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, mips->pc);
	} else {
		VERBOSE_LOG(Log::sceNet, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, mips->pc);
	}

	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1);
	}

	SceNetEtherAddr *saddr = (SceNetEtherAddr *)addr;
	uint16_t * sport = (uint16_t *)port; //Looking at Quake3 sourcecode (net_adhoc.c) this is an "int" (32bit) but changing here to 32bit will cause FF-Type0 to see duplicated Host (thinking it was from a different host)
	int * len = (int *)dataLength;

	// Checked in this order on the PSP (pspautotests net/adhoc/pdp).
	if (buf == NULL || len == NULL || (flag & ~ADHOC_F_NONBLOCK) != 0) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}
	if (!netAdhocInited) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized");
	}
	if (id <= 0 || id > MAX_SOCKET || adhocSockets[id - 1] == NULL) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
	}
	// The sender's address and port are optional. A blocking receive writes them later, so these
	// can't be locals.
	static SceNetEtherAddr discardedMac;
	static uint16_t discardedPort;
	if (saddr == NULL) {
		saddr = &discardedMac;
	}
	if (sport == NULL) {
		sport = &discardedPort;
		port = sport;
	}

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


				if (socket->flags & ADHOC_F_ALERTRECV) {
					socket->alerted_flags |= ADHOC_F_ALERTRECV;

					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
				}

				pdpFillQueue(id);
				int ret = pdpTakeFromQueue(socket, saddr, sport, buf, len);
				if (ret == 1) {
					if (flag) {
						return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_WOULD_BLOCK, "would block");
					}
					u64 threadSocketId = ((u64)__KernelGetCurThread()) << 32 | pdpsocket.id;
					return WaitBlockingAdhocSocket(threadSocketId, PDP_RECV, id, buf, len, timeout, saddr, sport, "pdp recv");
				}
				if (ret == SCE_NET_ADHOC_ERROR_NOT_ENOUGH_SPACE) {
					return hleLogVerbose(Log::sceNet, ret, "not enough space");
				}
				hleEatMicro(AdhocPdpRecvUs(*len));
				return hleLogDebug(Log::sceNet, 0, "received %d bytes from %s", *len, mac2str(saddr).c_str());
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
int sceNetAdhocSetSocketAlert(MIPSState *mips, int id, int flag) {
 	WARN_LOG_REPORT_ONCE(sceNetAdhocSetSocketAlert, Log::sceNet, "UNTESTED sceNetAdhocSetSocketAlert(%d, %08x) at %08x", id, flag, mips->pc);

	int retval = NetAdhoc_SetSocketAlert(id, flag);
	return hleDelayResult(hleLogDebug(Log::sceNet, retval), "set socket alert delay", 1000);
}

int PollAdhocSocket(SceNetAdhocPollSd* sds, int count, int timeout, int nonblock) {
	for (int i = 0; i < count; i++) {
		sds[i].revents = 0;
	}
	std::vector<AdhocPollState> states(std::max(count, 0));
	int affectedsockets = Transport().PollWait(sds, count, timeout, states.data());
	// A PDP socket has something to receive when its queue does.
	for (int i = 0; i < count; i++) {
		if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL && adhocSockets[sds[i].id - 1]->type == SOCK_PDP) {
			states[i].readable = pdpDatagramWaiting(sds[i].id);
		}
	}
	// A disconnected socket makes the call return EXCEPTION_EVENT, whatever else is ready.
	bool disconnected = false;
	if (affectedsockets >= 0) {
		affectedsockets = 0;
		for (int i = 0; i < count; i++) {
			if (sds[i].id > 0 && sds[i].id <= MAX_SOCKET && adhocSockets[sds[i].id - 1] != NULL) {
				auto sock = adhocSockets[sds[i].id - 1];
				const AdhocPollState &state = states[i];

				if ((sds[i].events & ADHOC_EV_RECV) && state.readable)
					sds[i].revents |= ADHOC_EV_RECV;
				if ((sds[i].events & ADHOC_EV_SEND) && state.writable)
					sds[i].revents |= ADHOC_EV_SEND;
				if (sock->alerted_flags)
					sds[i].revents |= ADHOC_EV_ALERT;
				// Mask certain revents bits with events bits
				sds[i].revents &= sds[i].events;

				// What listening and connected PTP sockets report on a PSP (pspautotests net/adhoc/poll):
				// SEND and FLUSH always, CONNECT once connected, and ACCEPT or RECV when there's something
				// to take. A connection whose peer has closed reports DISCONNECT alone.
				const bool readable = state.readable;
				if (sock->type == SOCK_PTP && sock->data.ptp.state == ADHOC_PTP_STATE_SYN_SENT) {
					Transport().PollCheckConnecting(sds[i].id, sock, state);
				}
				if (sock->type == SOCK_PTP && sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN) {
					sds[i].revents = (ADHOC_EV_SEND | ADHOC_EV_FLUSH | (readable ? ADHOC_EV_ACCEPT : 0)) & sds[i].events;
				} else if (sock->type == SOCK_PTP && sock->data.ptp.state == ADHOC_PTP_STATE_ESTABLISHED) {
					if (readable && Transport().PtpPeerClosed(sock)) {
						sds[i].revents = ADHOC_EV_DISCONNECT;
						disconnected = true;
					} else {
						sds[i].revents = (ADHOC_EV_SEND | ADHOC_EV_CONNECT | ADHOC_EV_FLUSH | (readable ? ADHOC_EV_RECV : 0)) & sds[i].events;
					}
				} else if (sock->type == SOCK_PTP) {
					ptpUpdateConnecting(sock);
					if ((sds[i].events & ADHOC_EV_CONNECT) && sock->data.ptp.state == ADHOC_PTP_STATE_ESTABLISHED) {
						sds[i].revents |= ADHOC_EV_CONNECT;
					}
					// Check for socket state (already disconnected/closed by remote peer, already closed/deleted, not a socket or not opened/connected yet?)
					// Raise ADHOC_EV_DISCONNECT, ADHOC_EV_DELETE, ADHOC_EV_INVALID on revents regardless of events as needed (similar to POLLHUP, POLLERR, and POLLNVAL on posix poll)
					if (sock->data.ptp.state == ADHOC_PTP_STATE_CLOSED) {
						if (sock->attemptCount > 0) {
							sds[i].revents = ADHOC_EV_DISCONNECT; // remote peer has closed the socket
							disconnected = true;
						}
					}
				}

				if (sock->flags & ADHOC_F_ALERTPOLL) {
					sock->alerted_flags |= ADHOC_F_ALERTPOLL;
					// Reported as ALERT alone (pspautotests net/adhoc/alerts).
					sds[i].revents = ADHOC_EV_ALERT;

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
	if (disconnected) {
		return SCE_NET_ADHOC_ERROR_EXCEPTION_EVENT;
	}
	return affectedsockets;
}

int sceNetAdhocPollSocket(MIPSState *mips, u32 socketStructAddr, int count, int timeout, int nonblock) { // timeout in microseconds
	DEBUG_LOG_REPORT_ONCE(sceNetAdhocPollSocket, Log::sceNet, "UNTESTED sceNetAdhocPollSocket(%08x, %i, %i, %i) at %08x", socketStructAddr, count, timeout, nonblock, mips->pc);
	// pspautotests net/adhoc/poll
	if ((nonblock & ~ADHOC_F_NONBLOCK) != 0) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid flag");
	}
	// Library is initialized
	if (netAdhocInited)
	{
		SceNetAdhocPollSd * sds = NULL;
		const u64 sdsSize = (u64)std::max(count, 0) * sizeof(SceNetAdhocPollSd);
		if (sdsSize <= 0x10000000 && Memory::IsValidRange(socketStructAddr, (u32)sdsSize)) sds = (SceNetAdhocPollSd *)Memory::GetPointerOrException(socketStructAddr);

		// Valid Arguments
		if (sds != NULL && count > 0)
		{
			// Socket Check
			for (int i = 0; i < count; i++) {
				// Invalid Socket. A PSP puts INVALID in the first entry, whichever one is invalid
				// (pspautotests net/adhoc/poll).
				if (sds[i].id < 1 || sds[i].id > MAX_SOCKET || adhocSockets[sds[i].id - 1] == NULL) {
					sds[0].revents = ADHOC_EV_INVALID;
					return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");
				}
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
				// About 20 us, and 1 more for each socket (pspautotests net/adhoc/sizes).
				hleEatMicro(20 + count);
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

int NetAdhocPdp_Delete(int id, int unknown) {
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (id > 0 && id <= MAX_SOCKET) {
			// Cast Socket
			auto sock = adhocSockets[id - 1];

			// Valid Socket
			if (sock != NULL && sock->type == SOCK_PDP) {
				pdpDropQueue(sock);
				return Transport().PdpDelete(id);
			}

			// Invalid Socket ID
			return SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID;
		}

		// Out of range (pspautotests net/adhoc/pdp)
		return SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID;
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
static int sceNetAdhocPdpDelete(MIPSState *mips, int id, int unknown) {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(Log::sceNet, "sceNetAdhocPdpDelete(%d, %d) at %08x", id, unknown, mips->pc);
	// The flag must be 0, checked first (pspautotests net/adhoc/pdp).
	if (unknown != 0) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid flag");
	}
	/*
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	*/

	return NetAdhocPdp_Delete(id, unknown);
}

static int sceNetAdhocctlGetAdhocId(MIPSState *mips, u32 productStructAddr) {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlGetAdhocId(%08x) at %08x", productStructAddr, mips->pc);

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
int sceNetAdhocctlScan(MIPSState *mips) {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlScan() at %08x", mips->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	// Library initialized
	if (netAdhocctlInited) {
		int us = adhocDefaultDelay;
		// FIXME: When tested with JPCSP + official prx files it seems when adhocctl in a connected state (ie. joined to a group) attempting to create/connect/join/scan will return a success (without doing anything?)
		if ((adhocctlState == ADHOCCTL_STATE_CONNECTED) || (adhocctlState == ADHOCCTL_STATE_GAMEMODE)) {
			// Returns 0 and reports ALREADY_CONNECTED through the handlers, as on the PSP (pspautotests
			// net/adhoc/adhocctl). Valhalla Knights 2 needs the event.
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
				return hleLogDebugOrError(Log::sceNet, WaitBlockingAdhocctlSocket(req, ADHOCCTL_REQUEST_US, "adhocctl scan"));
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

int sceNetAdhocctlGetScanInfo(MIPSState *mips, u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL;
	if (Memory::IsValid4AlignedAddress(sizeAddr)) {
		buflen = (s32_le *)Memory::GetPointerWriteUnchecked(sizeAddr);
	}
	SceNetAdhocctlScanInfoEmu *buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) {
		buf = (SceNetAdhocctlScanInfoEmu *)Memory::GetPointerOrException(bufAddr);
	}

	INFO_LOG(Log::sceNet, "sceNetAdhocctlGetScanInfo([%08x]=%i, %08x) at %08x", sizeAddr, buflen ? (int)*buflen : 0, bufAddr, mips->pc);
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
				// Only touch the part of the buffer that's valid memory.
				const u32 validLen = Memory::ClampValidSizeAt(bufAddr, std::max((s32)*buflen, 0));

				// Clear Memory
				memset(buf, 0, validLen);

				// Network Discovery Counter
				int discovered = 0;

				// Count requested Networks
				int requestcount = validLen / sizeof(SceNetAdhocctlScanInfoEmu);

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

			hleEatMicro(2);
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
	if (!netAdhocctlInited) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "adhocctl not initialized");
	}

	// The same function can be added more than once, and gets a new ID each time (pspautotests
	// net/adhoc/adhocctl).
	u32 retval = 0;
	AdhocctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	while (adhocctlHandlers.find(retval) != adhocctlHandlers.end())
		++retval;

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	if (Memory::IsValidAddress(handlerPtr)) {
		if (adhocctlHandlers.size() >= MAX_ADHOCCTL_HANDLERS) {
			ERROR_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = SCE_NET_ADHOCCTL_ERROR_TOO_MANY_HANDLERS;
			return retval;
		}
		adhocctlHandlers[retval] = handler;
		INFO_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, retval);
	} else {
		ERROR_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Invalid handler", handlerPtr, handlerArg);
		retval = SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;
	}

	// The id to return is the number of handlers currently registered
	return hleNoLog(retval);
}

static u32 NetAdhocctl_Disconnect(bool fromGameMode = false) {
	// Library initialized
	if (netAdhocctlInited) {
		if (isAdhocctlBusy && CoreTiming::IsScheduled(adhocctlNotifyEvent)) {
			return SCE_NET_ADHOCCTL_ERROR_BUSY;
		}
		// Until a Create or Connect has connected, it's BUSY, and the join goes on (pspautotests
		// net/adhoc/abandon).
		if (isAdhocctlBusy && adhocctlState == ADHOCCTL_STATE_DISCONNECTED && adhocctlCurrentMode != ADHOCCTL_MODE_NONE) {
			return SCE_NET_ADHOCCTL_ERROR_BUSY;
		}
		// Returns at once on a PSP, and DISCONNECT follows after about 105 ms (pspautotests net/adhoc/timing),
		// or 650 ms after ExitGameMode (net/adhoc/pair/gamemodetiming).
		adhocctlRequestTimeUs = CoreTiming::GetGlobalTimeUs();
		adhocctlDisconnectFromGameMode = fromGameMode;
		AbandonGroupJoin();

		// Connected State (Adhoc Mode). Attempting to leave a group while not in a group will be kicked out by Adhoc Server (ie. some games tries to disconnect more than once within a short time)
		if (adhocctlState != ADHOCCTL_STATE_DISCONNECTED || adhocctlInServerGroup) {
			adhocctlInServerGroup = false;
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
			adhocctlMutex.lock();
			enum adhocctl_call_status status = ADHOCCTL_CALL_SUCCESS;
			if (adhocctlHandle != NULL) {
				status = adhocctl_disconnect(adhocctlHandle);
			}
			adhocctlMutex.unlock();
			if (status == ADHOCCTL_CALL_ERROR) {
				ERROR_LOG(Log::sceNet, "adhocctl client error while sending disconnect");
				adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
				const std::lock_guard<std::mutex> adhocctl_guard(adhocctlMutex);
				killAdhocctlClient(true, adhocctl_guard);
			} else if (status == ADHOCCTL_CALL_WOULDBLOCK) {
				if (friendFinderRunning) {
					AdhocctlRequest req = { OPCODE_DISCONNECT, {0} };
					WaitBlockingAdhocctlSocket(req, 0, "adhocctl disconnect");
				} else {
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
		return hleLogWarning(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_ID_NOT_FOUND, "Invalid Handler ID");
	}
}

int NetAdhocctl_Term() {
	if (netAdhocctlInited) {
		if (adhocctlState != ADHOCCTL_STATE_DISCONNECTED || adhocctlInServerGroup) {
			// Note: This might block current thread if the first attempt to send OPCODE_DISCONNECT to AdhocServer failed with EAGAIN error
			if (netAdhocGameModeEntered)
				NetAdhocctl_ExitGameMode();
			else
				NetAdhocctl_Disconnect();
		}

		// Stop the adhocctl client
		friendFinderRunning = false;
		friendFinderStop();
		adhocctlInServerGroup = false;

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
		{
			const std::lock_guard<std::mutex> adhocctl_guard(adhocctlMutex);
			killAdhocctlClient(false, adhocctl_guard);
		}
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

	hleEatMicro(154);
	return hleLogInfo(Log::sceNet, retval);
}

static int sceNetAdhocctlGetNameByAddr(MIPSState *mips, const char *mac, u32 nameAddr) {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlGetNameByAddr(%s, %08x) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), nameAddr, mips->pc);

	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Arguments
		if (mac != NULL && Memory::IsValidRange(nameAddr, sizeof(SceNetAdhocctlNickname)))
		{
			SceNetAdhocctlNickname * nickname = (SceNetAdhocctlNickname *)Memory::GetPointerOrException(nameAddr);
			// Get Local MAC Address
			SceNetEtherAddr localmac;
			getLocalMac(&localmac);

			// The console itself isn't in the peer list: NO_ENTRY, and the name isn't written (pspautotests
			// net/adhoc/pair/basic).
			if (isMacMatch(&localmac, (const SceNetEtherAddr*)mac))
			{
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_NO_ENTRY, "local MAC");
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

int sceNetAdhocctlGetPeerInfo(MIPSState *mips, const char *mac, int size, u32 peerInfoAddr) {
	VERBOSE_LOG(Log::sceNet, "sceNetAdhocctlGetPeerInfo(%s, %i, %08x) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), size, peerInfoAddr, mips->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1);
	}

	SceNetEtherAddr * maddr = (SceNetEtherAddr *)mac;
	SceNetAdhocctlPeerInfoEmu * buf = NULL;
	if (Memory::IsValidRange(peerInfoAddr, sizeof(SceNetAdhocctlPeerInfoEmu))) {
		buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointerOrException(peerInfoAddr);
	}
	// Library initialized
	if (netAdhocctlInited) {
		if (buf == NULL) return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;
		// A size too small for the entry gives the ad hoc library's INVALID_BUFLEN (pspautotests net/adhoc/pair/basic).
		if (size < (int)sizeof(SceNetAdhocctlPeerInfoEmu)) return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_BUFLEN, "buffer too small");

		int retval = SCE_NET_ADHOC_ERROR_NO_ENTRY; // -1;

		// The local MAC isn't in the peer list, so NO_ENTRY (pspautotests net/adhoc/adhocctl).
		if (isLocalMAC(maddr)) {
			retval = SCE_NET_ADHOC_ERROR_NO_ENTRY;
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
				if (friendFinderRunning) {
					AdhocctlRequest req = { OPCODE_CONNECT, parameter.group_name };
					return WaitBlockingAdhocctlSocket(req, ADHOCCTL_REQUEST_US, "adhocctl connect");
				}
				//Faking success, to prevent Full Auto 2 from freezing while Initializing Network
				else {
					adhocctlStartTime = CoreTiming::GetGlobalTimeUs();
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

				hleEatMicro(adhocDefaultDelay);
				// Return Success
				// FIXME: When tested using JPCSP + official prx files it seems sceNetAdhocctlCreate switching to a different thread for at least 100ms after returning success and before executing the next line.
				return 0;
			}

			// Connected State
			return SCE_NET_ADHOCCTL_ERROR_BUSY; // SCE_NET_ADHOCCTL_ERROR_BUSY may trigger the game (ie. Ford Street Racing) to call sceNetAdhocctlDisconnect
		}

		// Invalid Argument
		return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;
	}
	// Library uninitialized
	return SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED;
}

/**
 * Create and / or Join a Virtual Network of the specified Name
 * @param group_name Virtual Network Name
 * @return 0 on success or... ADHOCCTL_NOT_INITIALIZED, ADHOCCTL_INVALID_ARG, ADHOCCTL_BUSY
 */
int sceNetAdhocctlCreate(MIPSState *mips, const char *groupName) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (groupName)
		strncpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	INFO_LOG(Log::sceNet, "sceNetAdhocctlCreate(%s) at %08x", grpName, mips->pc);
	if (!g_Config.bEnableWlan) {
		ERROR_LOG(Log::sceNet, "sceNetAdhocctlCreate(%s) failed: WLAN off", grpName);
		return -1;  // Not sure about the correct error code here.
	}

	adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
	adhocConnectionType = ADHOC_CREATE;
	return NetAdhocctl_Create(groupName);
}

int sceNetAdhocctlConnect(MIPSState *mips, const char* groupName) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (groupName)
		strncpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	INFO_LOG(Log::sceNet, "sceNetAdhocctlConnect(%s) at %08x", grpName, mips->pc);
	if (!g_Config.bEnableWlan) {
		ERROR_LOG(Log::sceNet, "sceNetAdhocctlConnect(%s) failed: WLAN off", grpName);
		return -1;  // Not sure about the correct error code here.
	}

	adhocctlCurrentMode = ADHOCCTL_MODE_NORMAL;
	adhocConnectionType = ADHOC_CONNECT;
	return NetAdhocctl_Create(groupName);
}

int sceNetAdhocctlJoin(MIPSState *mips, u32 scanInfoAddr) {
	INFO_LOG(Log::sceNet, "sceNetAdhocctlJoin(%08x) at %08x", scanInfoAddr, mips->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Argument
		if (Memory::IsValidRange(scanInfoAddr, sizeof(SceNetAdhocctlScanInfoEmu)))
		{
			SceNetAdhocctlScanInfoEmu* sinfo = (SceNetAdhocctlScanInfoEmu*)Memory::GetPointerOrException(scanInfoAddr);
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

	if (group_name == NULL || !Memory::IsValidAddress(membersAddr))
		return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;

	if (game_type < ADHOCCTL_GAMETYPE_1A || game_type > ADHOCCTL_GAMETYPE_2A || num_members < 2 || num_members > 16 || (game_type == ADHOCCTL_GAMETYPE_1A && num_members > 4))
		return SCE_NET_ADHOCCTL_ERROR_INVALID_ARG;

	// In a normal group, it reports ALREADY_CONNECTED through the handlers and stays in it
	// (pspautotests net/adhoc/gamemode).
	if (adhocctlState == ADHOCCTL_STATE_CONNECTED) {
		notifyAdhocctlHandlers(ADHOCCTL_EVENT_ERROR, SCE_NET_ADHOCCTL_ERROR_ALREADY_CONNECTED);
		return 0;
	}

	// The first member has to be this console: if it isn't, the call succeeds and the handlers get
	// ERROR with INVALID_ADDR (pspautotests net/adhoc/gamemode).
	SceNetEtherAddr firstMember = *PSPPointer<SceNetEtherAddr>::Create(membersAddr);
	if (!isLocalMAC(&firstMember)) {
		notifyAdhocctlHandlers(ADHOCCTL_EVENT_ERROR, SCE_NET_ADHOC_ERROR_INVALID_ADDR);
		return 0;
	}

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
	gameModeEnterTimeUs = CoreTiming::GetGlobalTimeUs();
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
static int sceNetAdhocctlCreateEnterGameMode(MIPSState *mips, const char * group_name, int game_type, int num_members, u32 membersAddr, int timeout, int flag) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (group_name)
		memcpy(grpName, group_name, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlCreateEnterGameMode, Log::sceNet, "UNTESTED sceNetAdhocctlCreateEnterGameMode(%s, %i, %i, %08x, %i, %i) at %08x", grpName, game_type, num_members, membersAddr, timeout, flag, mips->pc);

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
static int sceNetAdhocctlJoinEnterGameMode(MIPSState *mips, const char * group_name, const char *hostMac, int timeout, int flag) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (group_name)
		memcpy(grpName, group_name, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlJoinEnterGameMode, Log::sceNet, "UNTESTED sceNetAdhocctlJoinEnterGameMode(%s, %s, %i, %i) at %08x", grpName, mac2str((SceNetEtherAddr*)hostMac).c_str(), timeout, flag, mips->pc);

	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (!group_name || !hostMac)
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
	gameModeEnterTimeUs = CoreTiming::GetGlobalTimeUs();
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
int sceNetAdhocctlCreateEnterGameModeMin(MIPSState *mips, const char *group_name, int game_type, int min_members, int num_members, u32 membersAddr, u32 timeout, int flag) {
	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	if (group_name)
		memcpy(grpName, group_name, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	WARN_LOG_REPORT_ONCE(sceNetAdhocctlCreateEnterGameModeMin, Log::sceNet, "UNTESTED sceNetAdhocctlCreateEnterGameModeMin(%s, %i, %i, %i, %08x, %d, %i) at %08x", grpName, game_type, min_members, num_members, membersAddr, timeout, flag, mips->pc);
	// The minimum isn't enforced while waiting, but it is checked (pspautotests net/adhoc/gamemode).
	if (min_members < 2 || min_members > num_members) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "bad minimum");
	}
	return hleLogDebug(Log::sceNet, NetAdhocctl_CreateEnterGameMode(group_name, game_type, num_members, membersAddr, timeout, flag));
}

// Leaves adhocctl, the group and matching running: a PSP stays in its group through sceNetAdhocTerm
// and sceNetAdhocInit (pspautotests net/adhoc/term). The other callers terminate adhocctl first.
int NetAdhoc_Term() {
	// Library is initialized
	if (netAdhocInited) {
		// Delete GameMode Buffers
		deleteAllGMB();

		// Delete Adhoc Sockets
		deleteAllAdhocSockets();
		nextAdhocSocketSlot = 0;
		transport.reset();

		// Terminate Internet Library
		//sceNetInetTerm();

		// Unload Internet Modules (Just keep it in memory... unloading crashes?!)
		// if (_manage_modules != 0) sceUtilityUnloadModule(PSP_MODULE_NET_INET);
		// Library shutdown

		if (aemuPostofficeEnetThread.joinable()) {
			aemuPostofficeEnetPumping = false;
			aemuPostofficeEnetThread.join();
			// give enet one last pump so that sockets there were closed in deleteAllAdhocSockets sends at least one disconnect packet to the server
			pump_enet_aemu_postoffice();
		}

		deinit_aemu_postoffice();

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

	hleEatMicro(10);
	return hleLogInfo(Log::sceNet, retval);
}

static int sceNetAdhocGetPdpStat(MIPSState *mips, u32 structSize, u32 structAddr) {
	VERBOSE_LOG(Log::sceNet, "sceNetAdhocGetPdpStat(%08x, %08x) at %08x", structSize, structAddr, mips->pc);

	// Library is initialized
	if (netAdhocInited)
	{
		s32_le *buflen = NULL;
		if (Memory::IsValidAddress(structSize)) buflen = (s32_le *)Memory::GetPointerOrException(structSize);
		SceNetAdhocPdpStat *buf = NULL;
		if (Memory::IsValidAddress(structAddr)) buf = (SceNetAdhocPdpStat *)Memory::GetPointerOrException(structAddr);

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
			int count = Memory::ClampValidSizeAt(structAddr, std::max((s32)*buflen, 0)) / sizeof(SceNetAdhocPdpStat);
			if (count > socketcount) count = socketcount;

			// Copy Counter
			int i = 0;

			// Iterate Translation Table
			for (int j = 0; j < MAX_SOCKET && i < count; j++)
			{
				// Valid Socket Entry
				auto sock = adhocSockets[j];
				if (sock != NULL && sock->type == SOCK_PDP) {
					// The bytes waiting in the socket's queue.
					pdpFillQueue(j + 1);
					sock->data.pdp.rcv_sb_cc = pdpQueues[sock].bytes;

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

			hleEatMicro(1);
			// Success
			return 0;
		}

		// Invalid Arguments
		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg, at %08x", mips->pc);
	}

	// Library is uninitialized
	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized, at %08x", mips->pc);
}


/**
 * Adhoc Emulator PTP Socket List Getter
 * @param buflen IN: Length of Buffer in Bytes OUT: Required Length of Buffer in Bytes
 * @param buf PTP Socket List Buffer (can be NULL if you wish to receive Required Length)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED
 */

static int sceNetAdhocGetPtpStat(MIPSState *mips, u32 structSize, u32 structAddr) {
	// Spams a lot
	VERBOSE_LOG(Log::sceNet,"sceNetAdhocGetPtpStat(%08x, %08x) at %08x",structSize,structAddr,mips->pc);

	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(structSize)) buflen = (s32_le *)Memory::GetPointerOrException(structSize);
	SceNetAdhocPtpStat *buf = NULL;
	if (Memory::IsValidAddress(structAddr)) buf = (SceNetAdhocPtpStat *)Memory::GetPointerOrException(structAddr);

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
			int count = Memory::ClampValidSizeAt(structAddr, std::max((s32)*buflen, 0)) / sizeof(SceNetAdhocPtpStat);
			if (count > socketcount) count = socketcount;

			// Copy Counter
			int i = 0;

			// Iterate Sockets
			for (int j = 0; j < MAX_SOCKET && i < count; j++) {
				// Valid Socket Entry
				auto sock = adhocSockets[j];
				if ( sock != NULL && sock->type == SOCK_PTP) {
					ptpUpdateConnecting(sock);

					Transport().PtpUpdateRecvStat(sock);

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

			hleEatMicro(2);
			// Success
			return 0;
		}

		// Invalid Arguments
		return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg, at %08x", mips->pc);
	}

	// Library is uninitialized
	return hleLogVerbose(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_INITIALIZED, "not initialized, at %08x", mips->pc);
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

uint16_t get_random_unused_ptp_port() {
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

static int sceNetAdhocPtpOpen(MIPSState *mips, const char *srcmac, int sport, const char *dstmac, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int flag) {
	INFO_LOG(Log::sceNet, "sceNetAdhocPtpOpen(%s, %d, %s, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport, mac2str((SceNetEtherAddr*)dstmac).c_str(),dport,bufsize, rexmt_int, rexmt_cnt, flag, mips->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}
	SceNetEtherAddr* saddr = (SceNetEtherAddr*)srcmac;
	SceNetEtherAddr* daddr = (SceNetEtherAddr*)dstmac;

	// pspautotests net/adhoc/ptp. Zero sizes and retransmit settings are accepted.
	if (saddr == NULL || daddr == NULL || flag != 0) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}
	if (netAdhocInited && (isZeroMAC(saddr) || (saddr->data[0] & 1) != 0 || isZeroMAC(daddr))) {
		SkipAdhocSocketId();
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address");
	}
	if (netAdhocInited && (dport == 0 || dport == 0xFFFF)) {
		SkipAdhocSocketId();
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_PORT, "invalid port");
	}
	// What the PSP does with a buffer size under 1 isn't known; give the host one segment's worth.
	if (bufsize <= 0) {
		bufsize = PSP_ADHOC_PTP_MSS;
	}
	if (netAdhocInited && !HasFreeAdhocSocketSlot()) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ID_NOT_AVAIL, "out of socket IDs");
	}

	// Library is initialized
	if (netAdhocInited) {
		// Our own MAC whatever the game passes, deliberately unlike a PSP (see NetAdhocPdpCreate).
		SceNetEtherAddr localSrcMac;
		if (saddr != NULL) {
			getLocalMac(&localSrcMac);
			saddr = &localSrcMac;
		}
		// Valid Addresses. FIXME: MAC only valid after successful attempt to Create/Connect/Join a Group? (ie. adhocctlCurrentMode != ADHOCCTL_MODE_NONE)
		// The broadcast address is accepted, as on a PSP (pspautotests net/adhoc/ptp); connecting to it just never succeeds.
		if ((adhocctlCurrentMode != ADHOCCTL_MODE_NONE) && saddr != NULL && isLocalMAC(saddr) && daddr != NULL && !isZeroMAC(daddr)) {
			// Dissidia 012 will try to reOpen the port without Closing the old one first when PtpConnect failed to try again.
			if (isPTPPortInUse(sport, false, daddr, dport)) {
				// FIXME: When PORT_IN_USE error occured it seems the index to the socket id also increased, which means it tries to create & bind the socket first and then closes it due to failed to bind
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_PORT_IN_USE, "port in use");
			}

			// Valid Arguments
			if (bufsize > 0 && rexmt_int >= 0 && rexmt_cnt >= 0) {
				return Transport().PtpOpen(saddr, sport, daddr, dport, bufsize, rexmt_int, rexmt_cnt, flag);
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
				i = FindFreeAdhocSocketSlot();

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
					ptpSetEstablished(internal);

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

static int sceNetAdhocPtpAccept(MIPSState *mips, int id, u32 peerMacAddrPtr, u32 peerPortPtr, int timeout, int flag) {

	SceNetEtherAddr * addr = NULL;
	if (Memory::IsValidAddress(peerMacAddrPtr)) {
		addr = PSPPointer<SceNetEtherAddr>::Create(peerMacAddrPtr);
	}
	uint16_t * port = NULL; //
	if (Memory::IsValidAddress(peerPortPtr)) {
		port = (uint16_t *)Memory::GetPointerOrException(peerPortPtr);
	}
	if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
		DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpAccept(%d, [%08x]=%s, [%08x]=%u, %d, %u) at %08x", id, peerMacAddrPtr, mac2str(addr).c_str(), peerPortPtr, port ? *port : -1, timeout, flag, mips->pc);
	} else {
		VERBOSE_LOG(Log::sceNet, "sceNetAdhocPtpAccept(%d, [%08x]=%s, [%08x]=%u, %d, %u) at %08x", id, peerMacAddrPtr, mac2str(addr).c_str(), peerPortPtr, port ? *port : -1, timeout, flag, mips->pc);
	}
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	// Library is initialized
	if (netAdhocInited) {
		// The peer's address and port are both optional (pspautotests net/adhoc/alerts passes NULL for
		// both). GTA: VCS and Bomberman Panic Bomber leave them out.
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

					return Transport().PtpAccept(id, addr, port, timeout, flag);
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

			// A connection PtpOpen started may have come up since.
			if (ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT && Transport().PtpConnectedSinceOpen(socket)) {
				ptpSetEstablished(socket);
			}

			// Connecting a connected socket returns 0 on the PSP, even with the connect alert set (pspautotests
			// net/adhoc/alerts). Phantasy Star Portable 2 does that.
			if (ptpsocket.state == ADHOC_PTP_STATE_ESTABLISHED)
				return 0;

			if (socket->flags & ADHOC_F_ALERTCONNECT) {
				socket->alerted_flags |= ADHOC_F_ALERTCONNECT;

				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
			}

			if (socket->ptpRefused) {
				return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED, "refused before");
			}

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
					int errorcode = 0;
					int connectresult = Transport().PtpConnect(id, sin, &errorcode);

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
						ptpSetEstablished(socket);

						return hleLogDebug(Log::sceNet, 0, "sceNetAdhocPtpConnect[%i:%u]: Already Connected to %s:%u", id, ptpsocket.lport, ip2str(sin.sin_addr).c_str(), ptpsocket.pport);
					}

					// Error handling
					else if (connectresult == SOCKET_ERROR) {
						if (errorcode == ECONNREFUSED) {
							socket->attemptCount++;
							ptpSetRefused(socket);
							return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_CONNECTION_REFUSED, "refused");
						}
						if (Transport().PtpConnectPending(errorcode)) {
							ptpsocket.state = ADHOC_PTP_STATE_SYN_SENT;
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
static int sceNetAdhocPtpConnect(MIPSState *mips, int id, int timeout, int flag) {
	INFO_LOG(Log::sceNet, "sceNetAdhocPtpConnect(%i, %i, %i) at %08x", id, timeout, flag, mips->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
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
				return Transport().PtpClose(id);
			}

			return SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID;
		}

		// Out of range (pspautotests net/adhoc/ptp)
		return SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID;
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
static int sceNetAdhocPtpClose(MIPSState *mips, int id, int unknown) {
	INFO_LOG(Log::sceNet,"sceNetAdhocPtpClose(%d,%d) at %08x",id,unknown,mips->pc);
	// The flag must be 0 (pspautotests net/adhoc/ptp).
	if (unknown != 0) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid flag");
	}
	/*if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
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

static int sceNetAdhocPtpListen(MIPSState *mips, const char *srcmac, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, int flag) {
	INFO_LOG(Log::sceNet, "sceNetAdhocPtpListen(%s, %d, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport,bufsize,rexmt_int,rexmt_cnt,backlog,flag, mips->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}
	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)srcmac;
	bool isClient = false;

	// pspautotests net/adhoc/ptp. Zero sizes, retransmit settings and backlog are accepted.
	if (saddr == NULL || flag != 0) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}
	if (netAdhocInited && (isZeroMAC(saddr) || (saddr->data[0] & 1) != 0)) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ADDR, "invalid address");
	}
	// What the PSP does with a buffer size under 1 isn't known; give the host one segment's worth.
	if (bufsize <= 0) {
		bufsize = PSP_ADHOC_PTP_MSS;
	}
	if (netAdhocInited && !HasFreeAdhocSocketSlot()) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ID_NOT_AVAIL, "out of socket IDs");
	}

	if (netAdhocInited) {
		// Our own MAC whatever the game passes, deliberately unlike a PSP (see NetAdhocPdpCreate).
		SceNetEtherAddr localSrcMac;
		if (saddr != NULL) {
			getLocalMac(&localSrcMac);
			saddr = &localSrcMac;
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
			if (bufsize > 0 && rexmt_int >= 0 && rexmt_cnt >= 0 && backlog >= 0)
			{
				return Transport().PtpListen(saddr, sport, bufsize, rexmt_int, rexmt_cnt, backlog, isClient, flag);
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

static int sceNetAdhocPtpSend(MIPSState *mips, int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag) {
	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpSend(%d,%08x,%08x,%d,%d) at %08x", id, dataAddr, dataSizeAddr, timeout, flag, mips->pc);

	int * len = dataSizeAddr ? (int *)Memory::GetPointerOrException(dataSizeAddr) : nullptr;
	const char * data = Memory::GetCharPointer(dataAddr);
	if (len == NULL) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
	}
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
				// A length of 0 sends nothing; NULL data is a length error (pspautotests net/adhoc/ptp).
				if (*len == 0) {
					return hleLogDebug(Log::sceNet, 0, "nothing to send");
				}
				if (*len < 0 || data == NULL || !Memory::IsValidRange(dataAddr, *len)) {
					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_DATALEN, "invalid data length");
				}
				// Valid Arguments
				if (data != NULL && len != NULL && *len > 0) {
					// Schedule Timeout Removal
					//if (flag) timeout = 0; // JPCSP seems to always Send PTP as blocking, also a possibility to send to multiple destination?


					if (socket->flags & ADHOC_F_ALERTSEND) {
						socket->alerted_flags |= ADHOC_F_ALERTSEND;

						return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
					}

					return Transport().PtpSend(id, data, len, timeout, flag);
				}

				// Invalid Arguments
				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");
			}

			// A connection whose peer has gone stays DISCONNECTED (pspautotests net/adhoc/ptp).
			if (ptpsocket.state == ADHOC_PTP_STATE_CLOSED && socket->attemptCount > 0)
				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_DISCONNECTED, "disconnected");

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

static int sceNetAdhocPtpRecv(MIPSState *mips, int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag) {
	DEBUG_LOG(Log::sceNet, "sceNetAdhocPtpRecv(%d,%08x,%08x,%d,%d) at %08x", id, dataAddr, dataSizeAddr, timeout, flag, mips->pc);

	void * buf = (void *)Memory::GetPointerOrException(dataAddr);
	int * len = (int *)Memory::GetPointerOrException(dataSizeAddr);
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (buf != NULL && len != NULL && *len > 0 && Memory::IsValidRange(dataAddr, *len)) {
			// Valid Socket
			if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
				// Cast Socket
				auto socket = adhocSockets[id - 1];
				auto& ptpsocket = socket->data.ptp;
				socket->nonblocking = flag;

				if (ptpsocket.state == ADHOC_PTP_STATE_ESTABLISHED || ptpsocket.state == ADHOC_PTP_STATE_SYN_SENT) {
					// Schedule Timeout Removal
					//if (flag) timeout = 0;


					if (socket->flags & ADHOC_F_ALERTRECV) {
						socket->alerted_flags |= ADHOC_F_ALERTRECV;

						return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
					}

					return Transport().PtpRecv(id, buf, len, timeout, flag);
				}

				// A connection whose peer has gone stays DISCONNECTED (pspautotests net/adhoc/ptp).
				if (ptpsocket.state == ADHOC_PTP_STATE_CLOSED && socket->attemptCount > 0)
					return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_DISCONNECTED, "disconnected");

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
	return Transport().PtpFlush(socketId);
}

/**
 * Adhoc Emulator PTP Flusher
 * @param id Socket File Descriptor
 * @param timeout Flush Timeout (in Microseconds)
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_THREAD_ABORTED, ADHOC_DISCONNECTED, ADHOC_NOT_CONNECTED, NET_INTERNAL
 */
static int sceNetAdhocPtpFlush(MIPSState *mips, int id, int timeout, int nonblock) {
	DEBUG_LOG(Log::sceNet,"sceNetAdhocPtpFlush(%d,%d,%d) at %08x", id, timeout, nonblock, mips->pc);

	// Library initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= MAX_SOCKET && adhocSockets[id - 1] != NULL) {
			// Cast Socket
			auto socket = adhocSockets[id - 1];
			auto& ptpsocket = socket->data.ptp;
			socket->nonblocking = nonblock;

			// A flush with nothing left to send returns 0 even with the flush alert set (pspautotests
			// net/adhoc/alerts). Data sent on a connected socket is already with the host.
			if ((socket->flags & ADHOC_F_ALERTFLUSH) && ptpsocket.state != ADHOC_PTP_STATE_ESTABLISHED) {
				socket->alerted_flags |= ADHOC_F_ALERTFLUSH;

				return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_SOCKET_ALERTED, "socket alerted");
			}

			// Connected Socket
			if (ptpsocket.state == ADHOC_PTP_STATE_ESTABLISHED) {
				hleEatMicro(196);
				// There are two ways to flush, you can either set TCP_NODELAY to 1 or TCP_CORK to 0.

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
static int sceNetAdhocGameModeCreateMaster(MIPSState *mips, u32 dataAddr, int size) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeCreateMaster(%08x, %i) at %08x", dataAddr, size, mips->pc);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_ENTER_GAMEMODE, "not enter gamemode");

	if (size < 0 || !Memory::IsValidAddress(dataAddr))
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	// pspautotests net/adhoc/pair/gamemode
	if (size == 0)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_DATALEN, "invalid data length");

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
static int sceNetAdhocGameModeCreateReplica(MIPSState *mips, const char *mac, u32 dataAddr, int size) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeCreateReplica(%s, %08x, %i) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), dataAddr, size, mips->pc);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_IN_GAMEMODE, "not in gamemode");

	if (!netAdhocGameModeEntered)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_ENTER_GAMEMODE, "not enter gamemode");

	if (mac == nullptr || size < 0 || !Memory::IsValidAddress(dataAddr))
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	const SceNetEtherAddr &replicaMac = *(const SceNetEtherAddr *)mac;
	// The ID is the member's place in the member list (the host first), and a MAC that isn't a member is
	// refused. With two consoles, the host's replica of the client is 1 and the client's of the host 0
	// (pspautotests net/adhoc/pair/gamemode).
	auto member = std::find_if(gameModeMacs.begin(), gameModeMacs.end(), [&](const SceNetEtherAddr &m) { return IsMatch(m, replicaMac); });
	if (member == gameModeMacs.end())
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_GAMEMODE_MEMBER, "not a member");
	int replicaId = (int)(member - gameModeMacs.begin());

	hleEatMicro(1000);
	auto it = std::find_if(replicaGameModeAreas.begin(), replicaGameModeAreas.end(),
		[&](GameModeArea const& e) {
			return IsMatch(e.mac, replicaMac);
		});
	// pspautotests net/adhoc/pair/gamemode
	if (it != replicaGameModeAreas.end())
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_ALREADY_CREATED, "already created");

	int ret = 0;
	gameModeBuffSize = std::max(gameModeBuffSize, size);
	u8* buf = (u8*)realloc(gameModeBuffer, gameModeBuffSize);
	if (buf)
		gameModeBuffer = buf;

	u8* data = (u8*)malloc(size);
	if (data) {
		Memory::Memcpy(data, dataAddr, size);
		//int sock = sceNetAdhocPdpCreate(mac, ADHOC_GAMEMODE_PORT, size, 0);
		GameModeArea gma = { replicaId, size, dataAddr, CoreTiming::GetGlobalTimeUsScaled(), 0, 0, replicaMac, data };
		replicaGameModeAreas.push_back(gma);
		ret = gma.id;

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

static int sceNetAdhocGameModeUpdateMaster(MIPSState *mips) {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeUpdateMaster() at %08x", mips->pc);
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

	hleEatMicro(80);
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

static int sceNetAdhocGameModeDeleteMaster(MIPSState *mips) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeDeleteMaster() at %08x", mips->pc);
	if (isZeroMAC(&masterGameModeArea.mac))
		return hleLogError(Log::sceNet, SCE_NET_ADHOC_ERROR_NOT_CREATED, "not created");

	return NetAdhocGameMode_DeleteMaster();
}

static int sceNetAdhocGameModeUpdateReplica(MIPSState *mips, int id, u32 infoAddr) {
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeUpdateReplica(%i, %08x) at %08x", id, infoAddr, mips->pc);
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
	if (Memory::IsValidRange(infoAddr, sizeof(GameModeUpdateInfo))) {
		gmuinfo = (GameModeUpdateInfo*)Memory::GetPointerOrException(infoAddr);
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

	hleEatMicro(9);
	return hleLogDebug(Log::sceNet, 0);
}

static int sceNetAdhocGameModeDeleteReplica(MIPSState *mips, int id) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocGameModeDeleteReplica(%i) at %08x", id, mips->pc);
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

int sceNetAdhocGetSocketAlert(MIPSState *mips, int id, u32 flagPtr) {
	WARN_LOG_REPORT_ONCE(sceNetAdhocGetSocketAlert, Log::sceNet, "UNTESTED sceNetAdhocGetSocketAlert(%i, %08x) at %08x", id, flagPtr, mips->pc);
	if (!Memory::IsValidAddress(flagPtr))
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_ARG, "invalid arg");

	if (id < 1 || id > MAX_SOCKET || adhocSockets[id - 1] == NULL)
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOC_ERROR_INVALID_SOCKET_ID, "invalid socket id");

	s32_le flg = adhocSockets[id - 1]->flags;
	Memory::WriteOrException_U32(flg, flagPtr);

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
		u64 now = CoreTiming::GetGlobalTimeUs();

		// A PSP delivers CONNECT after a Join before the host shows up in its peer list (pspautotests
		// net/adhoc/pair/timing), so only GAME waits for a peer here.
		if (flags != ADHOCCTL_EVENT_GAME || adhocConnectionType != ADHOC_JOIN || getActivePeerCount() > 0 || static_cast<s64>(now - adhocctlStartTime) > adhocDefaultTimeout)
		{
			// Since 0 is a valid index to types_ we use -1 to detects if it was loaded from an old save state
			if (actionAfterAdhocMipsCall < 0) {
				actionAfterAdhocMipsCall = __KernelRegisterActionType(AfterAdhocMipsCall::Create);
			}

			delayus = adhocEventPollDelay; // May need to add an extra delay if a certain I/O Timing method causing disconnection issue
			switch (flags) {
			case ADHOCCTL_EVENT_CONNECT:
				newState = ADHOCCTL_STATE_CONNECTED;
				delayus = RemainingEventDelayUs(flags);
				break;
			case ADHOCCTL_EVENT_SCAN:
				newState = ADHOCCTL_STATE_DISCONNECTED;
				delayus = RemainingEventDelayUs(flags);
				break;
			case ADHOCCTL_EVENT_DISCONNECT:
				newState = ADHOCCTL_STATE_DISCONNECTED;
				delayus = RemainingEventDelayUs(flags);
				break;
			case ADHOCCTL_EVENT_GAME:
			{
				newState = ADHOCCTL_STATE_GAMEMODE;
				delayus = RemainingEventDelayUs(flags);
				// This is raised when the last member has arrived. The host's GAMEMODE also comes no sooner than
				// 1.35 s after that, as the member's own does. A member doesn't know when the host called
				// CreateEnterGameMode, so if it joins early it gets GAMEMODE before the host does.
				if (adhocConnectionType != ADHOC_JOIN)
					delayus = std::max(delayus, 1350000);
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

// The game's buffer, if all of it is valid memory, else NULL, which the calls treat as missing.
static void *GuestBuffer(u32 addr, int size) {
	return Memory::IsValidRange(addr, std::max(size, 1)) ? Memory::GetPointerWriteUnchecked(addr) : nullptr;
}

// The PDP syscalls check the game's buffers here, since game mode calls the functions with host ones.
static int sceNetAdhocPdpSendSyscall(MIPSState *mips, int id, u32 macAddr, u32 port, u32 dataAddr, u32 len, u32 timeout, u32 flag) {
	void *data = dataAddr ? GuestBuffer(dataAddr, (int)len) : nullptr;
	return sceNetAdhocPdpSend(mips, id, (const char *)GuestBuffer(macAddr, ETHER_ADDR_LEN), port, data, (int)len, (int)timeout, (int)flag);
}

static int sceNetAdhocPdpRecvSyscall(MIPSState *mips, int id, u32 macAddr, u32 portAddr, u32 bufAddr, u32 lenAddr, u32 timeout, u32 flag) {
	void *len = GuestBuffer(lenAddr, 4);
	void *buf = GuestBuffer(bufAddr, len ? *(s32_le *)len : 0);
	// NULL for the sender's MAC and port is fine.
	return sceNetAdhocPdpRecv(mips, id, macAddr ? GuestBuffer(macAddr, ETHER_ADDR_LEN) : nullptr, portAddr ? GuestBuffer(portAddr, 2) : nullptr, buf, len, timeout, (int)flag);
}

const HLEFunction sceNetAdhoc[] = {
	{0XE1D621D7, &WrapU_V<sceNetAdhocInit>,                            "sceNetAdhocInit",                        'x', ""         },
	{0XA62C6F57, &WrapI_V<sceNetAdhocTerm>,                            "sceNetAdhocTerm",                        'i', ""         },
	{0X0AD043ED, &WrapI_C<sceNetAdhocctlConnect>,                      "sceNetAdhocctlConnect",                  'i', "s"        },
	{0X6F92741B, &WrapI_CIIU<sceNetAdhocPdpCreate>,                    "sceNetAdhocPdpCreate",                   'i', "siix"     },
	{0XABED3790, &WrapI_IUUUUUU<sceNetAdhocPdpSendSyscall>,            "sceNetAdhocPdpSend",                     'i', "ixxxiii"  },
	{0XDFE53E03, &WrapI_IUUUUUU<sceNetAdhocPdpRecvSyscall>,            "sceNetAdhocPdpRecv",                     'i', "ixxxxxi"  },
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
	// Outside game mode, also after entering it timed out, it returns 0 with no event (pspautotests
	// net/adhoc/gamemode).
	if (adhocctlCurrentMode != ADHOCCTL_MODE_GAMEMODE)
		return adhocctlState == ADHOCCTL_STATE_CONNECTED ? SCE_NET_ADHOCCTL_ERROR_INVALID_IBSS : 0;

	if (gameModeSocket > 0) {
		NetAdhocPdp_Delete(gameModeSocket, 0);
		gameModeSocket = (int)INVALID_SOCKET;
	}

	deleteAllGMB();
	gameModePeerPorts.clear();

	adhocctlCurrentMode = ADHOCCTL_MODE_NONE;
	netAdhocGameModeEntered = false;
	return NetAdhocctl_Disconnect(true);
}

static int sceNetAdhocctlExitGameMode(MIPSState *mips) {
	WARN_LOG(Log::sceNet, "UNTESTED sceNetAdhocctlExitGameMode() at %08x", mips->pc);

	return NetAdhocctl_ExitGameMode();
}

static int sceNetAdhocctlGetGameModeInfo(u32 infoAddr) {
	DEBUG_LOG(Log::sceNet, "sceNetAdhocctlGetGameModeInfo(%08x)", infoAddr);
	if (!netAdhocctlInited)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");

	if (!Memory::IsValidRange(infoAddr, sizeof(SceNetAdhocctlGameModeInfo)))
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");

	// pspautotests net/adhoc/gamemode
	if (!netAdhocGameModeEntered)
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_ENTER_GAMEMODE, "not in game mode");

	SceNetAdhocctlGameModeInfo* gmInfo = (SceNetAdhocctlGameModeInfo*)Memory::GetPointerOrException(infoAddr);
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

static int sceNetAdhocctlGetPeerList(MIPSState *mips, u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(sizeAddr)) {
		buflen = (s32_le *)Memory::GetPointerOrException(sizeAddr);
	}
	SceNetAdhocctlPeerInfoEmu *buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) {
		buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointerOrException(bufAddr);
	}

	DEBUG_LOG(Log::sceNet, "sceNetAdhocctlGetPeerList([%08x]=%i, %08x) at %08x", sizeAddr, /*buflen ? *buflen : -1*/Memory::ReadUnchecked_U32(sizeAddr), bufAddr, mips->pc);
	if (!g_Config.bEnableWlan) {
		return hleLogError(Log::sceNet, -1, "WLAN off");
	}

	// Initialized Library
	if (netAdhocctlInited) {
		// Outside a group, an error from the auth library (pspautotests net/adhoc/adhocctl).
		if (adhocctlState == ADHOCCTL_STATE_DISCONNECTED) {
			if (buflen != NULL) {
				*buflen = 0;
			}
			return hleLogError(Log::sceNet, 0x80410682, "not in a group");
		}
		// Minimum Arguments
		if (buflen != NULL) {

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

				// Only the part of the buffer that's valid memory.
				const u32 validLen = Memory::ClampValidSizeAt(bufAddr, std::max((s32)*buflen, 0));

				// Calculate Request Count
				int requestcount = validLen / sizeof(SceNetAdhocctlPeerInfoEmu);

				// FIXME: When bufAddr is not null but buffer size is smaller than activePeers * sizeof(SceNetAdhocctlPeerInfoEmu), simply return buffer size = 0 without filling the buffer?

				// Clear Memory
				memset(buf, 0, validLen);

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

			hleEatMicro(4);
			return 0;
		}

		// Invalid Arguments
		return hleLogDebug(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG, "invalid arg");
	}

	// Uninitialized Library
	return hleLogDebug(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_NOT_INITIALIZED, "not initialized");
}

static int sceNetAdhocctlGetAddrByName(MIPSState *mips, const char *nickName, u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL; //int32_t
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointerOrException(sizeAddr);

	if (!nickName || !buflen) {
		return hleLogError(Log::sceNet, SCE_NET_ADHOCCTL_ERROR_INVALID_ARG);
	}

	char nckName[ADHOCCTL_NICKNAME_LEN];
	memcpy(nckName, nickName, ADHOCCTL_NICKNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	nckName[ADHOCCTL_NICKNAME_LEN - 1] = 0;

	WARN_LOG_REPORT_ONCE(sceNetAdhocctlGetAddrByName, Log::sceNet, "UNTESTED sceNetAdhocctlGetAddrByName(%s, [%08x]=%d/%zu, %08x) at %08x", nckName, sizeAddr, buflen ? *buflen : -1, sizeof(SceNetAdhocctlPeerInfoEmu), bufAddr, mips->pc);

	// Library initialized
	if (netAdhocctlInited)
	{
		{
			SceNetAdhocctlPeerInfoEmu *buf = NULL;
			if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointerOrException(bufAddr);

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
				// Only the part of the buffer that's valid memory.
				const u32 validLen = Memory::ClampValidSizeAt(bufAddr, std::max((s32)*buflen, 0));

				// Clear Memory
				memset(buf, 0, validLen);

				// Discovered Player Count
				int discovered = 0;

				// Calculate Requested Elements
				int requestcount = validLen / sizeof(SceNetAdhocctlPeerInfoEmu);

				// Minimum Space available
				if (requestcount > 0)
				{
					// The console itself isn't in the peer list, so it's never found here (pspautotests
					// net/adhoc/pair/basic).

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
int sceNetAdhocDiscoverInitStart(MIPSState *mips, u32 paramAddr) {
	WARN_LOG_REPORT_ONCE(sceNetAdhocDiscoverInitStart, Log::sceNet, "UNIMPL sceNetAdhocDiscoverInitStart(%08x) at %08x", paramAddr, mips->pc);
	// FIXME: Most AdhocDiscover syscalls will return 0x80410005 if (sceKernelCheckThreadStack_user() < 0x00000FE0), AdhocDiscover seems to be storing some data in the stack, while we use global variables for these
	if (sceKernelCheckThreadStack(mips) < 0x00000FE0)
		return 0x80410005;
	// Already running (pspautotests net/adhoc/discover).
	if (netAdhocDiscoverStatus == NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS)
		return hleLogError(Log::sceNet, 0x80411301, "already started");
	// TODO: Allocate internal buffer/struct (on the stack?) to be returned on sceNetAdhocDiscoverUpdate (the struct may contains WLAN channel from sceUtilityGetSystemParamInt at offset 0xA0 ?), setup adhocctl state callback handler to detects state change (using sceNetAdhocctl_lib_F8BABD85(stateCallbackFunction=0x09F436F8, adhocctlStateCallbackArg=0x0) on JPCSP+prx)
	u32 bufSize = 256; // dummy size, not sure how large it supposed to be, may be at least 0x3c bytes like in param->unknown2 ?
	if (netAdhocDiscoverBufAddr == 0) {
		u32 addr = userMemory.Alloc(bufSize, true, "AdhocDiscover"); // The address returned on DiscoverUpdate seems to be much higher than the param address, closer to the internal stateCallbackFunction address
		if (!Memory::IsValidAddress(addr))
			return 0x80410005;
		netAdhocDiscoverBufAddr = addr;
		Memory::Memset(netAdhocDiscoverBufAddr, 0, bufSize);
	}
	// FIME: Not sure what is this address 0x000010B0 used for (current Step may be?), but return 0x80411301 if (*((int *) 0x000010B0) != 0)
	//if (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x80) != 0) //if (*((int*)Memory::GetPointer(0x000010B0)) != 0)
	//	return 0x80411301; // Already Initialized/Started?
	// TODO: Need to findout whether using invalid params or param address will return an error code or not
	netAdhocDiscoverParam = (SceNetAdhocDiscoverParam*)Memory::GetPointerOrException(paramAddr);
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
	Memory::WriteUnchecked_U32(0x06060010, netAdhocDiscoverBufAddr + 0x60);
	Memory::WriteUnchecked_U32(0xffffffff, netAdhocDiscoverBufAddr + 0x70);
	if (netAdhocDiscoverParam->unknown1 == 0) {
		Memory::WriteUnchecked_U32(0x0B, netAdhocDiscoverBufAddr + 0x80);
		Memory::WriteUnchecked_U32(0x03, netAdhocDiscoverBufAddr + 0x84);
	}
	else if (netAdhocDiscoverParam->unknown1 == 1) {
		Memory::WriteUnchecked_U32(0x0F, netAdhocDiscoverBufAddr + 0x80);
		Memory::WriteUnchecked_U32(0x04, netAdhocDiscoverBufAddr + 0x84);
	}
	Memory::WriteUnchecked_U32(0, netAdhocDiscoverBufAddr + 0x98);
	Memory::WriteUnchecked_U32(g_Config.iWlanAdhocChannel, netAdhocDiscoverBufAddr + 0xA0);
	Memory::WriteUnchecked_U32(0, netAdhocDiscoverBufAddr + 0xA4);
	Memory::WriteUnchecked_U32(paramAddr, netAdhocDiscoverBufAddr + 0xA8);

	char grpName[ADHOCCTL_GROUPNAME_LEN + 1] = { 0 };
	memcpy(grpName, netAdhocDiscoverParam->groupName, ADHOCCTL_GROUPNAME_LEN); // For logging purpose, must not be truncated
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.Unknown1 : %08x", netAdhocDiscoverParam->unknown1);
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.GroupName: [%s]", grpName);
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.Unknown2 : %08x", netAdhocDiscoverParam->unknown2);
	DEBUG_LOG(Log::sceNet, "sceNetAdhocDiscoverInitStart - Param.Result   : %08x", netAdhocDiscoverParam->result);

	// TODO: Check whether we're already in the correct group and change the status and result accordingly
	// The result field is only written when it finishes (pspautotests net/adhoc/discover).
	netAdhocDiscoverIsStopping = false;
	netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS;
	netAdhocDiscoverUpdates = 0;
	netAdhocDiscoverStopUpdates = 0;
	netAdhocDiscoverStartTime = CoreTiming::GetGlobalTimeUsScaled();
	return hleLogInfo(Log::sceNet, 0);
}

// Note1: When canceling the progress, Legend Of The Dragon will use DiscoverStop -> AdhocctlDisconnect -> DiscoverTerm (when status changed to 2)
// Note2: When result = NO_PEER_FOUND or PEER_FOUND the progress can no longer be canceled on Legend Of The Dragon
int sceNetAdhocDiscoverStop(MIPSState *mips) {
	WARN_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverStop()");
	if (sceKernelCheckThreadStack(mips) < 0x00000FF0)
		return 0x80410005;

	if (Memory::IsValid4AlignedRange(netAdhocDiscoverBufAddr, 256)) {
		if (Memory::ReadUnchecked_U32(netAdhocDiscoverBufAddr + 0x80) > 0 && (Memory::ReadUnchecked_U32(netAdhocDiscoverBufAddr + 0x80) ^ 0x13) > 0) {
			Memory::WriteUnchecked_U32(Memory::ReadUnchecked_U32(netAdhocDiscoverBufAddr + 0x98) | 0x20, netAdhocDiscoverBufAddr + 0x98);
			Memory::WriteUnchecked_U32(0, netAdhocDiscoverBufAddr + 0xA4);
		}
	}
	// FIXME: Doesn't seems to be immediately changed the status, may be waiting until Disconnected from Adhocctl before changing the status to Completed?
	netAdhocDiscoverIsStopping = true;
	//netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_COMPLETED;
	//if (netAdhocDiscoverParam) netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_CANCELED;
	return 0;
}

int sceNetAdhocDiscoverTerm(MIPSState *mips) {
	WARN_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverTerm() at %08x", mips->pc);
	
	// if (sceKernelCheckThreadStack(mips) < 0x00000FF0)
	// 	return 0x80410005;
	// 
	// if (!(Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x80) > 0 && (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x80) ^ 0x13) > 0))
	// 	return 0x80411301; // Not Initialized/Started yet?
	
	// TODO: Use sceNetAdhocctl_lib_1C679240 to remove adhocctl state callback handler setup in sceNetAdhocDiscoverInitStart
	// if (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x70) >= 0) {
	// 	LinkDiscoverSkip(Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x70)); //sceNetAdhocctl_lib_1C679240
	// 	Memory::WriteOrException_U32(0xffffffff, netAdhocDiscoverBufAddr + 0x70);
	// }
	// Memory::WriteOrException_U32(0, netAdhocDiscoverBufAddr + 0x80);
	// Memory::WriteOrException_U32(0, netAdhocDiscoverBufAddr + 0xA8);

	netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_NONE;
	//if (netAdhocDiscoverParam) netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND; // Test: Using result = NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND will trigger Legend Of The Dragon to call sceNetAdhocctlGetPeerList after DiscoverTerm
	if (Memory::IsValidAddress(netAdhocDiscoverBufAddr)) {
		userMemory.Free(netAdhocDiscoverBufAddr);
		netAdhocDiscoverBufAddr = 0;
	}
	netAdhocDiscoverIsStopping = false;
	return 0;
}

int sceNetAdhocDiscoverGetStatus(MIPSState *mips) {
	DEBUG_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverGetStatus() at %08x", mips->pc);
	if (sceKernelCheckThreadStack(mips) < 0x00000FF0)
		return 0x80410005;
	// if (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x80) <= 0)
	// 	return 0;
	// if (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x80) <= 0x13)
	// 	return 1;
	// if (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x80) == 0x13)
	// 	return 2;
	return hleLogDebug(Log::sceNet, netAdhocDiscoverStatus); // Returning 2 will trigger Legend Of The Dragon to call sceNetAdhocctlGetPeerList (only happened if it was the first sceNetAdhocDiscoverGetStatus after sceNetAdhocDiscoverInitStart)
}

int sceNetAdhocDiscoverRequestSuspend(MIPSState *mips)
{
	ERROR_LOG_REPORT_ONCE(sceNetAdhocDiscoverRequestSuspend, Log::sceNet, "UNIMPL sceNetAdhocDiscoverRequestSuspend() at %08x", mips->pc);
	// FIXME: Not sure what is this syscall used for, may be related to Sleep Mode and can be triggered by using Power/Hold Switch? (based on what's written on Dissidia 012)
	if (sceKernelCheckThreadStack(mips) < 0x00000FF0)
		return 0x80410005;
	// if (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0xA4) == 0)
	// 	return 0x80411303; // Already Suspended?
	// if (Memory::ReadOrException_U32(netAdhocDiscoverBufAddr + 0x80) != 0)
	// 	return 0x80411303; // Already Suspended?
	// int ret = sceNetAdhocctl_lib_1572422C();
	// if (ret >= 0)
	// 	Memory::WriteOrException_U32(0, netAdhocDiscoverBufAddr + 0xA4);
	// return ret;
	// While discovering with nobody else around, a PSP refuses (pspautotests net/adhoc/discover).
	if (netAdhocDiscoverStatus == NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS)
		return hleLogError(Log::sceNet, 0x80411303, "can't suspend");
	return hleLogError(Log::sceNet, 0);
}

int sceNetAdhocDiscoverUpdate(MIPSState *mips) {
	DEBUG_LOG(Log::sceNet, "UNIMPL sceNetAdhocDiscoverUpdate() at %08x", mips->pc);
	if (sceKernelCheckThreadStack(mips) < 0x00000FF0)
		return 0x80410005;

	// TODO: Use switch case for each Step
	// What a PSP does with nobody to find (pspautotests net/adhoc/discover): adhocctl goes into the
	// DISCOVER state after the first Update. After a Stop, one Update later adhocctl is back to
	// DISCONNECTED and its handlers get DISCONNECT, and one more later the status is COMPLETED with the
	// result cancelled.
	if (netAdhocDiscoverStatus == NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS) {
		if (!netAdhocDiscoverIsStopping) {
			if (++netAdhocDiscoverUpdates == 2 && adhocctlState == ADHOCCTL_STATE_DISCONNECTED)
				adhocctlState = ADHOCCTL_STATE_DISCOVER;
		} else {
			netAdhocDiscoverStopUpdates++;
			if (netAdhocDiscoverStopUpdates == 2) {
				if (adhocctlState == ADHOCCTL_STATE_DISCOVER)
					adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
				notifyAdhocctlHandlers(ADHOCCTL_EVENT_DISCONNECT, 0);
			} else if (netAdhocDiscoverStopUpdates >= 3) {
				netAdhocDiscoverStatus = NET_ADHOC_DISCOVER_STATUS_COMPLETED;
				if (netAdhocDiscoverParam)
					netAdhocDiscoverParam->result = NET_ADHOC_DISCOVER_RESULT_CANCELED;
			}
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

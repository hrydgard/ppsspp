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

#include <mutex>
#include <string>
#include <algorithm>

#include "Common/Net/Resolve.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/System/OSD.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Data/Format/JSONReader.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/Util/PortManager.h"
#include "Core/CoreTiming.h"
#include "Core/Instance.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceNetAdhocMatching.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetApctl.h"
#include "Core/HLE/sceNp.h"
#include "Core/HLE/sceNp2.h"
#include "Core/HLE/sceNetInet.h"
#include "Core/HLE/sceNetResolver.h"

bool g_netInited;

u32 netDropRate = 0;
u32 netDropDuration = 0;
u32 netPoolAddr = 0;
u32 netThread1Addr = 0;
u32 netThread2Addr = 0;

static struct SceNetMallocStat netMallocStat;

static std::map<int, ApctlHandler> apctlHandlers;

const char * const defaultNetConfigName = "NetConf";
const char * const defaultNetSSID = "Wifi"; // fake AP/hotspot

int netApctlInfoId = 0;
SceNetApctlInfoInternal netApctlInfo;

bool g_netApctlInited;
u32 netApctlState;
u32 apctlProdCodeAddr = 0;
u32 apctlThreadHackAddr = 0;
u32_le apctlThreadCode[3];
SceUID apctlThreadID = 0;
int apctlStateEvent = -1;
int actionAfterApctlMipsCall;
std::recursive_mutex apctlEvtMtx;
std::deque<ApctlArgs> apctlEvents;

// Loaded auto-config
InfraDNSConfig g_infraDNSConfig;

u32 Net_Term();
int NetApctl_Term();
void NetApctl_InitDefaultInfo();
void NetApctl_InitInfo(int confId);

bool IsNetworkConnected() {
	// TODO: Tweak this.
	return __NetApctlConnected() || __NetAdhocConnected();
}

bool NetworkWarnUserIfOnlineAndCantSavestate() {
	if (IsNetworkConnected() && !g_Config.bAllowSavestateWhileConnected) {
		auto nw = GetI18NCategory(I18NCat::NETWORKING);
		g_OSD.Show(OSDType::MESSAGE_INFO, nw->T("Save states are not available when online"), 3.0f, "saveonline");
		return true;
	} else {
		return false;
	}
}

bool NetworkWarnUserIfOnlineAndCantSpeed() {
	if (IsNetworkConnected()) {
		auto nw = GetI18NCategory(I18NCat::NETWORKING);
		g_OSD.Show(OSDType::MESSAGE_INFO, nw->T("Speed controls are not available when online"), 3.0f, "speedonline");
		return true;
	} else {
		return false;
	}
}

bool NetworkAllowSpeedControl() {
	return !IsNetworkConnected();
}

bool NetworkAllowSaveState() {
	return !IsNetworkConnected() || g_Config.bAllowSavestateWhileConnected;
}

void AfterApctlMipsCall::DoState(PointerWrap & p) {
	auto s = p.Section("AfterApctlMipsCall", 1, 1);
	if (!s)
		return;
	// Just in case there are "s" corruption in the future where s.ver is a negative number
	if (s >= 1) {
		Do(p, handlerID);
		Do(p, oldState);
		Do(p, newState);
		Do(p, event);
		Do(p, error);
		Do(p, argsAddr);
	} else {
		handlerID = -1;
		oldState = 0;
		newState = 0;
		event = 0;
		error = 0;
		argsAddr = 0;
	}
}

void AfterApctlMipsCall::run(MipsCall& call) {
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	DEBUG_LOG(Log::sceNet, "AfterApctlMipsCall::run [ID=%i][OldState=%d][NewState=%d][Event=%d][Error=%d][ArgsPtr=%08x] [cbId: %u][retV0: %08x]", handlerID, oldState, newState, event, error, argsAddr, call.cbId, v0);
	//call.setReturnValue(v0);
}

void AfterApctlMipsCall::SetData(int HandlerID, int OldState, int NewState, int Event, int Error, u32_le ArgsAddr) {
	handlerID = HandlerID;
	oldState = OldState;
	newState = NewState;
	event = Event;
	error = Error;
	argsAddr = ArgsAddr;
}

bool LoadDNSForGameID(std::string_view gameID, std::string_view jsonStr, InfraDNSConfig *dns) {
	using namespace json;

	*dns = {};

	json::JsonReader reader(jsonStr.data(), jsonStr.length());
	if (!reader.ok() || !reader.root()) {
		ERROR_LOG(Log::IO, "Error parsing DNS JSON");
		return false;
	}

	const JsonGet root = reader.root();
	const JsonGet def = root.getDict("default");

	// Load the default DNS.
	if (def) {
		dns->dns = def.getStringOr("dns", "");
		if (def.hasChild("revival_credits", JSON_OBJECT)) {
			const JsonGet revived = def.getDict("revival_credits");
			if (revived) {
				dns->revivalTeam = revived.getStringOr("group", "");
				dns->revivalTeamURL = revived.getStringOr("url", "");
			}
		}
		dns->connectAdHocForGrouping = def.getBool("connect_adhoc_for_grouping", false);
	}

	const JsonNode *games = root.getArray("games");
	for (const JsonNode *iter : games->value) {
		JsonGet game = iter->value;
		// Goddamn I have to change the json reader we're using. So ugly.
		const JsonNode *workingIdsNode = game.getArray("known_working_ids");
		const JsonNode *otherIdsNode = game.getArray("other_ids");
		const JsonNode *notWorkingIdsNode = game.getArray("not_working_ids");
		if (!workingIdsNode && !otherIdsNode && !notWorkingIdsNode) {
			// We ignore this game.
			continue;
		}

		bool found = false;

		std::vector<std::string> workingIDs;

		std::vector<std::string> ids;
		if (workingIdsNode) {
			JsonGet(workingIdsNode->value).getStringVector(&ids);
			for (auto &id : ids) {
				workingIDs.push_back(id);
				if (id == gameID) {
					found = true;
					dns->state = InfraGameState::Working;
					break;
				}
			}
		}
		if (!found && notWorkingIdsNode) {
			// Check the non working array
			JsonGet(notWorkingIdsNode->value).getStringVector(&ids);
			for (auto &id : ids) {
				if (id == gameID) {
					found = true;
					dns->state = InfraGameState::NotWorking;
					break;
				}
			}
		}
		if (!found && otherIdsNode) {
			// Check the "other" array, we're gonna try this, but less confidently :P
			JsonGet(otherIdsNode->value).getStringVector(&ids);
			for (auto &id : ids) {
				if (id == gameID) {
					found = true;
					dns->state = InfraGameState::Unknown;
					break;
				}
			}
		}

		if (!found) {
			continue;
		}

		dns->workingIDs = workingIDs;
		dns->gameName = game.getStringOr("name", "");
		dns->dns = game.getStringOr("dns", dns->dns.c_str());
		dns->dyn_dns = game.getStringOr("dyn_dns", "");
		dns->connectAdHocForGrouping = game.getBool("connect_adhoc_for_grouping", dns->connectAdHocForGrouping);
		if (game.hasChild("domains", JSON_OBJECT)) {
			const JsonGet domains = game.getDict("domains");
			for (auto iter : domains.value_) {
				std::string domain = std::string(iter->key);
				std::string ipAddr = std::string(iter->value.toString());
				dns->fixedDNS[domain] = ipAddr;
			}
		}

		if (game.hasChild("revival_credits", JSON_OBJECT)) {
			const JsonGet revived = game.getDict("revival_credits");
			if (revived) {
				dns->revivalTeam = revived.getStringOr("group", "");
				dns->revivalTeamURL = revived.getStringOr("url", "");
			}
		}

		// TODO: Check for not working platforms
		break;
	}

	dns->loaded = true;
	return true;
}

bool LoadAutoDNS(std::string_view json) {
	if (!g_Config.bInfrastructureAutoDNS) {
		return true;
	}

	// Load the automatic DNS config for this game - or the defaults.
	std::string discID = g_paramSFO.GetDiscID();
	if (!LoadDNSForGameID(discID, json, &g_infraDNSConfig)) {
		return false;
	}

	// If dyn_dns is non-empty, try to use it to replace the specified DNS.
	// If fails, we just use the dns. TODO: Do this in the background somehow...
	const auto &dns = g_infraDNSConfig.dns;
	const auto &dyn_dns = g_infraDNSConfig.dyn_dns;
	if (!dyn_dns.empty()) {
		// Try to look it up in system DNS
		INFO_LOG(Log::sceNet, "DynDNS requested, trying to resolve '%s'...", dyn_dns.c_str());
		addrinfo *resolved = nullptr;
		std::string err;
		if (!net::DNSResolve(dyn_dns, "", &resolved, err)) {
			ERROR_LOG(Log::sceNet, "Error resolving, falling back to '%s'", dns.c_str());
		} else if (resolved) {
			bool found = false;
			for (auto ptr = resolved; ptr && !found; ptr = ptr->ai_next) {
				switch (ptr->ai_family) {
				case AF_INET:
				{
					char ipstr[256];
					if (inet_ntop(ptr->ai_family, &(((struct sockaddr_in*)ptr->ai_addr)->sin_addr), ipstr, sizeof(ipstr)) != 0) {
						INFO_LOG(Log::sceNet, "Successfully resolved '%s' to '%s', overriding DNS.", dyn_dns.c_str(), ipstr);
						if (g_infraDNSConfig.dns != ipstr) {
							WARN_LOG(Log::sceNet, "Replacing specified DNS IP %s with dyndns %s!", g_infraDNSConfig.dns.c_str(), ipstr);
							g_infraDNSConfig.dns = ipstr;
						} else {
							INFO_LOG(Log::sceNet, "DynDNS: %s already up to date", g_infraDNSConfig.dns.c_str());
						}
						found = true;
					}
					break;
				}
				}
			}
			net::DNSResolveFree(resolved);
		}
	}
	return true;
}

std::shared_ptr<http::Request> g_infraDL;

static const std::string_view jsonUrl = "http://metadata.ppsspp.org/infra-dns.json";

void DeleteAutoDNSCacheFile() {
	File::Delete(g_DownloadManager.UrlToCachePath(jsonUrl));
}

void StartInfraJsonDownload() {
	if (!g_Config.bInfrastructureAutoDNS) {
		return;
	}

	if (g_infraDL) {
		WARN_LOG(Log::sceNet, "json is already being downloaded. Still, starting a new download.");
	}

	const char *acceptMime = "application/json, text/*; q=0.9, */*; q=0.8";
	g_infraDL = g_DownloadManager.StartDownload(jsonUrl, Path(), http::RequestFlags::Cached24H, acceptMime);
}

bool PollInfraJsonDownload(std::string *jsonOutput) {
	if (!g_Config.bInfrastructureAutoDNS) {
		return true;
	}

	if (g_Config.bDontDownloadInfraJson) {
		NOTICE_LOG(Log::sceNet, "As specified by the ini setting DontDownloadInfraJson, using infra-dns.json from /assets");
		size_t jsonSize = 0;
		std::unique_ptr<uint8_t[]> jsonStr(g_VFS.ReadFile("infra-dns.json", &jsonSize));
		if (!jsonStr) {
			jsonOutput->clear();
			return true;  // A clear output but returning true means something vent very wrong.
		}
		*jsonOutput = std::string((const char *)jsonStr.get(), jsonSize);
		return true;
	}

	if (!g_infraDL) {
		INFO_LOG(Log::sceNet, "No json download going on");
		return false;
	}

	if (!g_infraDL->Done()) {
		return false;
	}

	// The request is done, but did it fail?
	if (g_infraDL->Failed()) {
		// First, fall back to cache if it exists. Could build this functionality into the download manager
		// but it would be a bit awkward.
		std::string json;
		if (File::ReadBinaryFileToString(g_DownloadManager.UrlToCachePath(jsonUrl), &json) && !json.empty()) {
			WARN_LOG(Log::sceNet, "Failed to download infra-dns.json, falling back to cached file");
			*jsonOutput = json;
			LoadAutoDNS(*jsonOutput);
			return true;
		}

		// If it doesn't, let's just grab the assets file. Because later this will mean that we didn't even get the cached copy.
		size_t jsonSize = 0;
		std::unique_ptr<uint8_t[]> jsonStr(g_VFS.ReadFile("infra-dns.json", &jsonSize));
		if (!jsonStr) {
			jsonOutput->clear();
			return true;  // A clear output but returning true means something vent very wrong.
		}
		*jsonOutput = std::string((const char *)jsonStr.get(), jsonSize);
		return true;
	}

	// OK, we actually got data. Load it!
	g_infraDL->buffer().TakeAll(jsonOutput);
	if (jsonOutput->empty()) {
		_dbg_assert_msg_(false, "Json output is empty!");
		ERROR_LOG(Log::sceNet, "JSON output is empty! Something went wrong.");
	}
	return true;
}

void InitLocalhostIP() {
	// The entire 127.*.*.* is reserved for loopback.
	uint32_t localIP = 0x7F000001 + PPSSPP_ID - 1;

	g_localhostIP.in.sin_family = AF_INET;
	g_localhostIP.in.sin_addr.s_addr = htonl(localIP);
	g_localhostIP.in.sin_port = 0;

	std::string serverStr = StripSpaces(g_Config.proAdhocServer);
	isLocalServer = (!strcasecmp(serverStr.c_str(), "localhost") || serverStr.find("127.") == 0);
}

static bool __PlatformNetInit() {
#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		// TODO: log
		return false;
	}
#endif
	return true;
}

static bool __PlatformNetShutdown() {
#ifdef _WIN32
	return WSACleanup() == 0;
#else
	return true;
#endif
}

static void __ApctlState(u64 userdata, int cyclesLate) {
	SceUID threadID = userdata >> 32;
	int uid = (int)(userdata & 0xFFFFFFFF);
	int event = uid - 1;

	s64 result = 0;
	u32 error = 0;

	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_NET, error);
	if (waitID == 0 || error != 0) {
		WARN_LOG(Log::sceNet, "sceNetApctl State WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
		return;
	}

	u32 waitVal = __KernelGetWaitValue(threadID, error);
	if (error == 0) {
		netApctlState = waitVal;
	}

	__KernelResumeThreadFromWait(threadID, result);
	DEBUG_LOG(Log::sceNet, "Returning (WaitID: %d, error: %08x) Result (%08x) of sceNetApctl - Event: %d, State: %d", waitID, error, (int)result, event, netApctlState);
}

// Used to change Apctl State after a delay and before executing callback mipscall (since we don't have beforeAction)
int ScheduleApctlState(int event, int newState, int usec, const char* reason) {
	int uid = event + 1;

	u64 param = ((u64)__KernelGetCurThread()) << 32 | uid;
	CoreTiming::ScheduleEvent(usToCycles(usec), apctlStateEvent, param);
	__KernelWaitCurThread(WAITTYPE_NET, uid, newState, 0, false, reason);

	return 0;
}

void __NetApctlInit() {
	g_netApctlInited = false;
	netApctlState = PSP_NET_APCTL_STATE_DISCONNECTED;
	apctlStateEvent = CoreTiming::RegisterEvent("__ApctlState", __ApctlState);
	apctlHandlers.clear();
	apctlEvents.clear();
	memset(&netApctlInfo, 0, sizeof(netApctlInfo));
}

static void __ResetInitNetLib() {
	g_netInited = false;
	netInetInited = false; // shouldn't this actually do something?

	memset(&netMallocStat, 0, sizeof(netMallocStat));
	memset(&parameter, 0, sizeof(parameter));
}

void __NetCallbackInit() {
	// Init Network Callbacks
	dummyThreadHackAddr = __CreateHLELoop(dummyThreadCode, "sceNetAdhoc", "__NetTriggerCallbacks", "dummythreadhack");
	matchingThreadHackAddr = __CreateHLELoop(matchingThreadCode, "sceNetAdhocMatching", "__NetMatchingCallbacks", "matchingThreadHack");
	apctlThreadHackAddr = __CreateHLELoop(apctlThreadCode, "sceNetApctl", "__NetApctlCallbacks", "apctlThreadHack");

	// Newer one should be placed last to prevent callbacks going to the wrong after action after loading from old save state
	actionAfterMatchingMipsCall = __KernelRegisterActionType(AfterMatchingMipsCall::Create);
	actionAfterAdhocMipsCall = __KernelRegisterActionType(AfterAdhocMipsCall::Create);
	actionAfterApctlMipsCall = __KernelRegisterActionType(AfterApctlMipsCall::Create);
}

void __NetInit() {
	// Windows: Assuming WSAStartup already called beforehand
	portOffset = g_Config.iPortOffset;
	isOriPort = g_Config.bEnableUPnP && g_Config.bUPnPUseOriginalPort;
	minSocketTimeoutUS = g_Config.iMinTimeout * 1000UL;

	// Init Default AdhocServer struct
	g_adhocServerIP.in.sin_family = AF_INET;
	g_adhocServerIP.in.sin_port = htons(SERVER_PORT); //27312 // Maybe read this from config too
	g_adhocServerIP.in.sin_addr.s_addr = INADDR_NONE;

	dummyPeekBuf64k = (char*)malloc(dummyPeekBuf64kSize);
	InitLocalhostIP();

	SceNetEtherAddr mac;
	getLocalMac(&mac);
	INFO_LOG(Log::sceNet, "LocalHost IP will be %s [%s]", ip2str(g_localhostIP.in.sin_addr).c_str(), mac2str(&mac).c_str());

	__PlatformNetInit();
	// TODO: May be we should initialize & cleanup somewhere else than here for PortManager to be used as general purpose for whatever port forwarding PPSSPP needed
	__UPnPInit();

	__ResetInitNetLib();
	__NetApctlInit();
	__NetCallbackInit();
}

void __NetApctlShutdown() {
	if (apctlThreadHackAddr) {
		kernelMemory.Free(apctlThreadHackAddr);
		apctlThreadHackAddr = 0;
	}
	apctlHandlers.clear();
	apctlEvents.clear();
}

void __NetShutdown() {
	// Network Cleanup
	Net_Term();

	__NetResolverShutdown();
	__NetInetShutdown();
	__NetApctlShutdown();
	__ResetInitNetLib();

	// Since PortManager supposed to be general purpose for whatever port forwarding PPSSPP needed, may be we shouldn't clear & restore ports in here? it will be cleared and restored by PortManager's destructor when exiting PPSSPP anyway
	__UPnPShutdown();

	__PlatformNetShutdown();

	free(dummyPeekBuf64k);
}

static void __UpdateApctlHandlers(u32 oldState, u32 newState, u32 flag, u32 error) {
	std::lock_guard<std::recursive_mutex> apctlGuard(apctlEvtMtx);
	apctlEvents.push_back({ oldState, newState, flag, error });
}

void netValidateLoopMemory() {
	// Allocate Memory if it wasn't valid/allocated after loaded from old SaveState
	if (!apctlThreadHackAddr || (apctlThreadHackAddr && strcmp("apctlThreadHack", kernelMemory.GetBlockTag(apctlThreadHackAddr)) != 0)) {
		u32 blockSize = sizeof(apctlThreadCode);
		apctlThreadHackAddr = kernelMemory.Alloc(blockSize, false, "apctlThreadHack");
		if (apctlThreadHackAddr) Memory::Memcpy(apctlThreadHackAddr, apctlThreadCode, sizeof(apctlThreadCode));
	}
}

// This feels like a dubious proposition, mostly...
void __NetDoState(PointerWrap &p) {
	auto s = p.Section("sceNet", 1, 6);
	if (!s)
		return;

	auto cur_netInited = g_netInited;
	auto cur_netInetInited = netInetInited;
	auto cur_netApctlInited = g_netApctlInited;

	Do(p, g_netInited);
	Do(p, netInetInited);
	Do(p, g_netApctlInited);
	Do(p, apctlHandlers);
	Do(p, netMallocStat);
	if (s < 2) {
		netDropRate = 0;
		netDropDuration = 0;
	} else {
		Do(p, netDropRate);
		Do(p, netDropDuration);
	}
	if (s < 3) {
		netPoolAddr = 0;
		netThread1Addr = 0;
		netThread2Addr = 0;
	} else {
		Do(p, netPoolAddr);
		Do(p, netThread1Addr);
		Do(p, netThread2Addr);
	}
	if (s >= 4) {
		Do(p, netApctlState);
		Do(p, netApctlInfo);
		Do(p, actionAfterApctlMipsCall);
		if (actionAfterApctlMipsCall != -1) {
			__KernelRestoreActionType(actionAfterApctlMipsCall, AfterApctlMipsCall::Create);
		}
		Do(p, apctlThreadHackAddr);
		Do(p, apctlThreadID);
	} else {
		actionAfterApctlMipsCall = -1;
		apctlThreadHackAddr = 0;
		apctlThreadID = 0;
	}
	if (s >= 5) {
		Do(p, apctlStateEvent);
	} else {
		apctlStateEvent = -1;
	}
	CoreTiming::RestoreRegisterEvent(apctlStateEvent, "__ApctlState", __ApctlState);
	if (s >= 6) {
		Do(p, netApctlInfoId);
		Do(p, netApctlInfo);
	} else {
		netApctlInfoId = 0;
		NetApctl_InitDefaultInfo();
	}

	if (p.mode == p.MODE_READ) {
		// Let's not change "Inited" value when Loading SaveState in the middle of multiplayer to prevent memory & port leaks
		g_netApctlInited = cur_netApctlInited;
		netInetInited = cur_netInetInited;
		g_netInited = cur_netInited;

		// Discard leftover events
		apctlEvents.clear();
		// Discard created resolvers for now (since i'm not sure whether the information in the struct is sufficient or not, and we don't support multi-threading yet anyway)
		__NetResolverShutdown();
	}
}

void __NetApctlCallbacks()
{
	std::lock_guard<std::recursive_mutex> apctlGuard(apctlEvtMtx);
	std::lock_guard<std::recursive_mutex> npAuthGuard(npAuthEvtMtx);
	std::lock_guard<std::recursive_mutex> npMatching2Guard(npMatching2EvtMtx);
	hleSkipDeadbeef();
	int delayus = 10000;

	// We are temporarily borrowing APctl thread for NpAuth callbacks for testing to simulate authentication
	if (NpAuthProcessEvents()) {
		delayus = (adhocEventDelay + adhocExtraDelay);
	}

	// Temporarily borrowing APctl thread for NpMatching2 callbacks for testing purpose
	if (NpMatching2ProcessEvents()) {
		delayus = (adhocEventDelay + adhocExtraDelay);
	}

	// How AP works probably like this: Game use sceNetApctl function -> sceNetApctl let the hardware know and do their's thing and have a new State -> Let the game know the resulting State through Event on their handler
	if (!apctlEvents.empty()) {
		auto& args = apctlEvents.front();
		auto& oldState = args.data[0];
		auto& newState = args.data[1];
		auto& event = args.data[2];
		auto& error = args.data[3];
		apctlEvents.pop_front();

		// Adjust delay according to current event.
		if (event == PSP_NET_APCTL_EVENT_CONNECT_REQUEST || event == PSP_NET_APCTL_EVENT_GET_IP || event == PSP_NET_APCTL_EVENT_SCAN_REQUEST || event == PSP_NET_APCTL_EVENT_ESTABLISHED)
			delayus = adhocEventDelay;
		else
			delayus = adhocEventPollDelay;

		// Do we need to change the oldState? even if there was error?
		if (error == 0)
		{
			//oldState = netApctlState;
			netApctlState = newState;
		}

		// Need to make sure netApctlState is updated before calling the callback's mipscall so the game can GetState()/GetInfo() within their handler's subroutine and make use the new State/Info
		// Should we update NewState & Error accordingly to Event before executing the mipscall ? sceNetApctl* functions might want to set the error value tho, so we probably should leave it untouched, right?
		//error = 0;
		switch (event) {
		case PSP_NET_APCTL_EVENT_CONNECT_REQUEST:
			newState = PSP_NET_APCTL_STATE_JOINING; // Should we set the State to PSP_NET_APCTL_STATE_DISCONNECTED if there was error?
			if (error != 0)
				apctlEvents.push_front({ oldState, PSP_NET_APCTL_STATE_DISCONNECTED, PSP_NET_APCTL_EVENT_ERROR, error });
			else
				apctlEvents.push_front({ oldState, newState, PSP_NET_APCTL_EVENT_ESTABLISHED, 0 }); // Should we use PSP_NET_APCTL_EVENT_EAP_AUTH if securityType is not NONE?
			break;

		case PSP_NET_APCTL_EVENT_ESTABLISHED:
			newState = PSP_NET_APCTL_STATE_GETTING_IP;
			// FIXME: Official prx seems to return ERROR 0x80410280 on the next event when using invalid connection profile to Connect?
			if (error != 0)
				apctlEvents.push_front({ oldState, PSP_NET_APCTL_STATE_DISCONNECTED, PSP_NET_APCTL_EVENT_ERROR, error });
			else
				apctlEvents.push_front({ oldState, newState, PSP_NET_APCTL_EVENT_GET_IP, 0 });
			break;

		case PSP_NET_APCTL_EVENT_GET_IP:
			newState = PSP_NET_APCTL_STATE_GOT_IP;
			NetApctl_InitInfo(netApctlInfoId);
			break;

		case PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			delayus = adhocDefaultDelay / 2; // FIXME: Similar to Adhocctl Disconnect, we probably need to change the state within a frame-time (or less to be safer)
			break;

		case PSP_NET_APCTL_EVENT_SCAN_REQUEST:
			newState = PSP_NET_APCTL_STATE_SCANNING;
			if (error != 0)
				apctlEvents.push_front({ oldState, PSP_NET_APCTL_STATE_DISCONNECTED, PSP_NET_APCTL_EVENT_ERROR, error });
			else
				apctlEvents.push_front({ oldState, newState, PSP_NET_APCTL_EVENT_SCAN_COMPLETE, 0 });
			break;

		case PSP_NET_APCTL_EVENT_SCAN_COMPLETE:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			if (error == 0)
				apctlEvents.push_front({ oldState, newState, PSP_NET_APCTL_EVENT_SCAN_STOP, 0 });
			break;

		case PSP_NET_APCTL_EVENT_SCAN_STOP:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			break;

		case PSP_NET_APCTL_EVENT_EAP_AUTH: // Is this suppose to happen between JOINING and ESTABLISHED ?
			newState = PSP_NET_APCTL_STATE_EAP_AUTH;
			if (error != 0)
				apctlEvents.push_front({ oldState, PSP_NET_APCTL_STATE_DISCONNECTED, PSP_NET_APCTL_EVENT_ERROR, error });
			else
				apctlEvents.push_front({ oldState, newState, PSP_NET_APCTL_EVENT_KEY_EXCHANGE, 0 }); // not sure if KEY_EXCHANGE is the next step after AUTH or not tho
			break;

		case PSP_NET_APCTL_EVENT_KEY_EXCHANGE: // Is this suppose to happen between JOINING and ESTABLISHED ?
			newState = PSP_NET_APCTL_STATE_KEY_EXCHANGE;
			if (error != 0)
				apctlEvents.push_front({ oldState, PSP_NET_APCTL_STATE_DISCONNECTED, PSP_NET_APCTL_EVENT_ERROR, error });
			else
				apctlEvents.push_front({ oldState, newState, PSP_NET_APCTL_EVENT_ESTABLISHED, 0 });
			break;

		case PSP_NET_APCTL_EVENT_RECONNECT:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			if (error != 0)
				apctlEvents.push_front({ oldState, PSP_NET_APCTL_STATE_DISCONNECTED, PSP_NET_APCTL_EVENT_ERROR, error });
			else
				apctlEvents.push_front({ oldState, newState, PSP_NET_APCTL_EVENT_CONNECT_REQUEST, 0 });
			break;

		case PSP_NET_APCTL_EVENT_ERROR:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			break;
		}
		// Do we need to change the newState? even if there were error?
		//if (error != 0)
		//	newState = netApctlState;

		// Since 0 is a valid index to types_ we use -1 to detects if it was loaded from an old save state
		if (actionAfterApctlMipsCall < 0) {
			actionAfterApctlMipsCall = __KernelRegisterActionType(AfterApctlMipsCall::Create);
		}

		// Run mipscall. Should we skipped executing the mipscall if oldState == newState? 
		for (std::map<int, ApctlHandler>::iterator it = apctlHandlers.begin(); it != apctlHandlers.end(); ++it) {
			DEBUG_LOG(Log::sceNet, "ApctlCallback [ID=%i][OldState=%d][NewState=%d][Event=%d][Error=%08x][ArgsPtr=%08x]", it->first, oldState, newState, event, error, it->second.argument);
			args.data[4] = it->second.argument;
			AfterApctlMipsCall* after = (AfterApctlMipsCall*)__KernelCreateAction(actionAfterApctlMipsCall);
			after->SetData(it->first, oldState, newState, event, error, it->second.argument);
			hleEnqueueCall(it->second.entryPoint, 5, args.data, after);
		}
		// Similar to Adhocctl, new State might need to be set after delayed, right before executing the mipscall (ie. simulated beforeAction)
		ScheduleApctlState(event, newState, delayus, "apctl callback state");
		hleNoLogVoid();
		return;
	}

	// Must be delayed long enough whenever there is a pending callback to make sure previous callback & it's afterAction are fully executed

	hleCall(ThreadManForUser, int, sceKernelDelayThread, delayus);

	hleNoLogVoid();
}

static inline u32 AllocUser(u32 size, bool fromTop, const char *name) {
	u32 addr = userMemory.Alloc(size, fromTop, name);
	if (addr == -1)
		return 0;
	return addr;
}

static inline void FreeUser(u32 &addr) {
	if (addr != 0)
		userMemory.Free(addr);
	addr = 0;
}

u32 Net_Term() {
	// May also need to Terminate netAdhocctl and netAdhoc to free some resources & threads, since the game (ie. GTA:VCS, Wipeout Pulse, etc) might not have called
	// them before calling sceNetTerm and causing them to behave strangely on the next sceNetInit & sceNetAdhocInit
	NetAdhocctl_Term();
	NetAdhocMatching_Term();
	NetAdhoc_Term();
	
	// TODO: Not implemented yet
	NetApctl_Term();
	//NetInet_Term();

	// Library is initialized
	if (g_netInited) {
		// Delete Adhoc Sockets
		deleteAllAdhocSockets();

		// Delete GameMode Buffer
		//deleteAllGMB();

		// Terminate Internet Library
		//sceNetInetTerm();

		// Unload Internet Modules (Just keep it in memory... unloading crashes?!)
		// if (_manage_modules != 0) sceUtilityUnloadModule(PSP_MODULE_NET_INET);
		// Library shutdown
	}

	FreeUser(netPoolAddr);
	FreeUser(netThread1Addr);
	FreeUser(netThread2Addr);
	g_netInited = false;

	g_infraDNSConfig = {};
	return 0;
}

static u32 sceNetTerm() {
	auto n = GetI18NCategory(I18NCat::NETWORKING);

	int retval = Net_Term();

	// Give time to make sure everything are cleaned up
	hleEatMicro(adhocDefaultDelay);
	return hleLogWarning(Log::sceNet, retval, "at %08x", currentMIPS->pc);
}

/*
Parameters:
	poolsize	- Memory pool size (appears to be for the whole of the networking library).
	calloutprio	- Priority of the SceNetCallout thread.
	calloutstack	- Stack size of the SceNetCallout thread (defaults to 4096 on non 1.5 firmware regardless of what value is passed).
	netintrprio	- Priority of the SceNetNetintr thread.
	netintrstack	- Stack size of the SceNetNetintr thread (defaults to 4096 on non 1.5 firmware regardless of what value is passed).
*/
static int sceNetInit(u32 poolSize, u32 calloutPri, u32 calloutStack, u32 netinitPri, u32 netinitStack)  {
	// TODO: Create Network Threads using given priority & stack
	// TODO: The correct behavior is actually to allocate more and leak the other threads/pool.
	// But we reset here for historic reasons (GTA:VCS potentially triggers this.)
	if (g_netInited) {
		// This cleanup attempt might not worked when SaveState were loaded in the middle of multiplayer game and re-entering multiplayer, thus causing memory leaks & wasting binded ports.
		// Maybe we shouldn't save/load "Inited" vars on SaveState?
		Net_Term();
	}

	if (poolSize == 0) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE, "invalid pool size");
	} else if (calloutPri < 0x08 || calloutPri > 0x77) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_PRIORITY, "invalid callout thread priority");
	} else if (netinitPri < 0x08 || netinitPri > 0x77) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_PRIORITY, "invalid init thread priority");
	}

	// TODO: Should also start the threads, probably?  For now, let's just allocate.
	// TODO: Respect the stack size if firmware set to 1.50?
	u32 stackSize = 4096;
	netThread1Addr = AllocUser(stackSize, true, "netstack1");
	if (netThread1Addr == 0) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_NO_MEMORY, "unable to allocate thread");
	}
	netThread2Addr = AllocUser(stackSize, true, "netstack2");
	if (netThread2Addr == 0) {
		FreeUser(netThread1Addr);
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_NO_MEMORY, "unable to allocate thread");
	}

	netPoolAddr = AllocUser(poolSize, false, "netpool");
	if (netPoolAddr == 0) {
		FreeUser(netThread1Addr);
		FreeUser(netThread2Addr);
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_NO_MEMORY, "unable to allocate pool");
	}

	WARN_LOG(Log::sceNet, "sceNetInit(poolsize=%d, calloutpri=%i, calloutstack=%d, netintrpri=%i, netintrstack=%d) at %08x", poolSize, calloutPri, calloutStack, netinitPri, netinitStack, currentMIPS->pc);
	
	netMallocStat.pool = poolSize - 0x20; // On Vantage Master Portable this is slightly (32 bytes) smaller than the poolSize arg when tested with JPCSP + prx files
	netMallocStat.maximum = 0x4050; // Dummy maximum foot print
	netMallocStat.free = netMallocStat.pool; // Dummy free size, we should set this high enough to prevent any issue (ie. Vantage Master Portable), this is probably the only field being checked by games?

	// Clear Socket Translator Memory
	memset(&adhocSockets, 0, sizeof(adhocSockets));

	g_netInited = true;

	auto n = GetI18NCategory(I18NCat::NETWORKING);

	return hleLogDebug(Log::sceNet, 0);
}

// Free(delete) thread info / data. 
// Normal usage: sceKernelDeleteThread followed by sceNetFreeThreadInfo with the same threadID as argument
static int sceNetFreeThreadinfo(SceUID thid) {
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

// Abort a thread.
static int sceNetThreadAbort(SceUID thid) {
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static u32 sceWlanGetEtherAddr(u32 addrAddr) {
	if (!Memory::IsValidRange(addrAddr, 6)) {
		// More correctly, it should crash.
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "illegal address");
	}

	u8 *addr = Memory::GetPointerWriteUnchecked(addrAddr);
	if (PPSSPP_ID > 1) {
		Memory::Memset(addrAddr, PPSSPP_ID, 6);
		// Making sure the 1st 2-bits on the 1st byte of OUI are zero to prevent issue with some games (ie. Gran Turismo)
		addr[0] &= 0xfc;
	} else {
		// Read MAC Address from config
		if (!ParseMacAddress(g_Config.sMACAddress, addr)) {
			ERROR_LOG(Log::sceNet, "Error parsing mac address %s", g_Config.sMACAddress.c_str());
			Memory::Memset(addrAddr, 0, 6);
		}
	}
	NotifyMemInfo(MemBlockFlags::WRITE, addrAddr, 6, "WlanEtherAddr");

	return hleDelayResult(hleLogDebug(Log::sceNet, 0), "get ether mac", 200);
}

static u32 sceNetGetLocalEtherAddr(u32 addrAddr) {
	// FIXME: Return 0x80410180 (pspnet[_core] error code?) before successful attempt to Create/Connect/Join a Group? (ie. adhocctlCurrentMode == ADHOCCTL_MODE_NONE)
	if (adhocctlCurrentMode == ADHOCCTL_MODE_NONE)
		return hleLogDebug(Log::sceNet, 0x80410180, "address not available?");

	return sceWlanGetEtherAddr(addrAddr);
}

static u32 sceWlanDevIsPowerOn() {
	return hleLogVerbose(Log::sceNet, g_Config.bEnableWlan ? 1 : 0);
}

static u32 sceWlanGetSwitchState() {
	return hleLogVerbose(Log::sceNet, g_Config.bEnableWlan ? 1 : 0);
}

// Probably a void function, but often returns a useful value.
static void sceNetEtherNtostr(u32 macPtr, u32 bufferPtr) {
	DEBUG_LOG(Log::sceNet, "sceNetEtherNtostr(%08x, %08x) at %08x", macPtr, bufferPtr, currentMIPS->pc);

	if (Memory::IsValidAddress(bufferPtr) && Memory::IsValidAddress(macPtr)) {
		char *buffer = (char *)Memory::GetPointerWriteUnchecked(bufferPtr);
		const u8 *mac = Memory::GetPointerUnchecked(macPtr);

		// MAC address is always 6 bytes / 48 bits.
		sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

		VERBOSE_LOG(Log::sceNet, "sceNetEtherNtostr - [%s]", buffer);
	}

	hleNoLogVoid();
}

static int hex_to_digit(int c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// Probably a void function, but sometimes returns a useful-ish value.
static void sceNetEtherStrton(u32 bufferPtr, u32 macPtr) {
	DEBUG_LOG(Log::sceNet, "sceNetEtherStrton(%08x, %08x)", bufferPtr, macPtr);

	if (Memory::IsValidAddress(bufferPtr) && Memory::IsValidAddress(macPtr)) {
		const char *buffer = (const char *)Memory::GetPointerUnchecked(bufferPtr);
		u8 *mac = Memory::GetPointerWrite(macPtr);

		// MAC address is always 6 pairs of hex digits.
		// TODO: Funny stuff happens if it's too short.
		u8 value = 0;
		for (int i = 0; i < 6 && *buffer != 0; ++i) {
			value = 0;

			int c = hex_to_digit(*buffer++);
			if (c != -1) {
				value |= c << 4;
			}
			c = hex_to_digit(*buffer++);
			if (c != -1) {
				value |= c;
			}

			*mac++ = value;

			// Skip a single character in between.
			// TODO: Strange behavior on the PSP, let's just null check.
			if (*buffer++ == 0) {
				break;
			}
		}

		VERBOSE_LOG(Log::sceNet, "sceNetEtherStrton - [%s]", mac2str((SceNetEtherAddr*)Memory::GetPointer(macPtr)).c_str());
		// Seems to maybe kinda return the last value.  Probably returns void.
		//return value;
	}
	hleNoLogVoid();
}

// Write static data since we don't actually manage any memory for sceNet* yet.
static int sceNetGetMallocStat(u32 statPtr) {
	auto stat = PSPPointer<SceNetMallocStat>::Create(statPtr);
	if (!stat.IsValid())
		return hleLogError(Log::sceNet, 0, "invalid address");

	*stat = netMallocStat;
	stat.NotifyWrite("sceNetGetMallocStat");
	return hleLogVerbose(Log::sceNet, 0);
}

void NetApctl_InitDefaultInfo() {
	memset(&netApctlInfo, 0, sizeof(netApctlInfo));
	// Set dummy/fake parameters, assuming this is the currently selected Network Configuration profile
	// FIXME: Some of these info supposed to be taken from netConfInfo
	int validConfId = std::max(1, netApctlInfoId); // Should be: sceUtilityGetNetParamLatestID(validConfId);
	truncate_cpy(netApctlInfo.name, sizeof(netApctlInfo.name), defaultNetConfigName + std::to_string(validConfId));
	truncate_cpy(netApctlInfo.ssid, sizeof(netApctlInfo.ssid), defaultNetSSID);
	// Default IP Address
	char ipstr[INET_ADDRSTRLEN] = "0.0.0.0"; // Driver 76 needs a dot formatted IP instead of a zeroed buffer
	truncate_cpy(netApctlInfo.ip, sizeof(netApctlInfo.ip), ipstr);
	truncate_cpy(netApctlInfo.gateway, sizeof(netApctlInfo.gateway), ipstr);
	truncate_cpy(netApctlInfo.primaryDns, sizeof(netApctlInfo.primaryDns), ipstr);
	truncate_cpy(netApctlInfo.secondaryDns, sizeof(netApctlInfo.secondaryDns), ipstr);
	truncate_cpy(netApctlInfo.subNetMask, sizeof(netApctlInfo.subNetMask), ipstr);
}

void NetApctl_InitInfo(int confId) {
	memset(&netApctlInfo, 0, sizeof(netApctlInfo));
	// Set dummy/fake values, some of these (ie. IP set to Auto) probably not suppose to have valid info before connected to an AP, right?
	// FIXME: Some of these info supposed to be taken from netConfInfo
	truncate_cpy(netApctlInfo.name, sizeof(netApctlInfo.name), defaultNetConfigName + std::to_string(confId));
	truncate_cpy(netApctlInfo.ssid, sizeof(netApctlInfo.ssid), defaultNetSSID);
	memcpy(netApctlInfo.bssid, "\1\1\2\2\3\3", sizeof(netApctlInfo.bssid)); // fake AP's mac address
	netApctlInfo.ssidLength = static_cast<unsigned int>(strlen(defaultNetSSID));
	netApctlInfo.strength = 99;
	netApctlInfo.channel = g_Config.iWlanAdhocChannel;
	if (netApctlInfo.channel == PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC) netApctlInfo.channel = defaultWlanChannel;
	// Get Local IP Address
	sockaddr_in sockAddr;
	getLocalIp(&sockAddr); // This will be valid IP, we probably not suppose to have a valid IP before connected to any AP, right?
	char ipstr[INET_ADDRSTRLEN] = "127.0.0.1"; // Patapon 3 seems to try to get current IP using ApctlGetInfo() right after ApctlInit(), what kind of IP should we use as default before ApctlConnect()? it shouldn't be a valid IP, right?
	inet_ntop(AF_INET, &sockAddr.sin_addr, ipstr, sizeof(ipstr));
	truncate_cpy(netApctlInfo.ip, sizeof(netApctlInfo.ip), ipstr);
	// Change the last number to 1 to indicate a common dns server/internet gateway
	((u8*)&sockAddr.sin_addr.s_addr)[3] = 1;
	inet_ntop(AF_INET, &sockAddr.sin_addr, ipstr, sizeof(ipstr));
	truncate_cpy(netApctlInfo.gateway, sizeof(netApctlInfo.gateway), ipstr);

	// We use the configured DNS server, whether manually or auto configured.
	// Games (for example, Wipeout Pulse) often use this to perform their own DNS lookups through UDP, so we need to pass in the configured server.
	// The reason we need autoconfig is that private Servers may need to use custom DNS Server - and most games are now
	// best played on private servers (only a few official ones remain, if any).
	if (g_Config.bInfrastructureAutoDNS) {
		INFO_LOG(Log::sceNet, "Responding to game query with AutoDNS address: %s", g_infraDNSConfig.dns.c_str());
		truncate_cpy(netApctlInfo.primaryDns, sizeof(netApctlInfo.primaryDns), g_infraDNSConfig.dns);
	} else {
		INFO_LOG(Log::sceNet, "Responding to game query with manual DNS address: %s", g_Config.sInfrastructureDNSServer.c_str());
		truncate_cpy(netApctlInfo.primaryDns, sizeof(netApctlInfo.primaryDns), g_Config.sInfrastructureDNSServer);
	}

	// We don't bother with a secondary DNS.
	truncate_cpy(netApctlInfo.secondaryDns, sizeof(netApctlInfo.secondaryDns), "0.0.0.0"); // Fireteam Bravo 2 seems to try to use secondary DNS too if it's not 0.0.0.0

	truncate_cpy(netApctlInfo.subNetMask, sizeof(netApctlInfo.subNetMask), "255.255.255.0");
}

static int sceNetApctlInit(int stackSize, int initPriority) {
	if (g_netApctlInited) {
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_ALREADY_INITIALIZED);
	}

	apctlEvents.clear();
	netApctlState = PSP_NET_APCTL_STATE_DISCONNECTED;

	// Set default value before connected to an AP
	NetApctl_InitDefaultInfo();

	// Create APctl fake-Thread
	netValidateLoopMemory();
	apctlThreadID = __KernelCreateThread("ApctlThread", __KernelGetCurThreadModuleId(), apctlThreadHackAddr, initPriority, stackSize, PSP_THREAD_ATTR_USER, 0, true);
	if (apctlThreadID > 0) {
		__KernelStartThread(apctlThreadID, 0, 0);
	}

	// Note: Borrowing AdhocServer for Grouping purpose
	u32 structsz = sizeof(SceNetAdhocctlAdhocId);
	if (apctlProdCodeAddr != 0) {
		userMemory.Free(apctlProdCodeAddr);
	}
	apctlProdCodeAddr = userMemory.Alloc(structsz, false, "ApctlAdhocId");
	SceNetAdhocctlAdhocId* prodCode = (SceNetAdhocctlAdhocId*)Memory::GetCharPointer(apctlProdCodeAddr);
	if (prodCode) {
		memset(prodCode, 0, structsz);
		// TODO: Use a 9-characters product id instead of disc id (ie. not null-terminated(VT_UTF8_SPE) and shouldn't be variable size?)
		std::string discID = g_paramSFO.GetDiscID();
		prodCode->type = 1; // VT_UTF8 since we're using DiscID instead of product id
		memcpy(prodCode->data, discID.c_str(), std::min(ADHOCCTL_ADHOCID_LEN, (int)discID.size()));
	}

	// Actually connect sceNetAdhocctl. TODO: Can we move this to sceNetApctlConnect? Would be good because a lot of games call Init but not Connect on startup.
	if (g_infraDNSConfig.connectAdHocForGrouping) {
		hleCall(sceNetAdhocctl, int, sceNetAdhocctlInit, stackSize, initPriority, apctlProdCodeAddr);
	}

	g_netApctlInited = true;
	return hleLogInfo(Log::sceNet, 0);
}

int NetApctl_Term() {
	// Note: Since we're borrowing AdhocServer for Grouping purpose, we should cleanup too
	// Should we check the g_infraDNSConfig.connectAdHocForGrouping flag here?
	if (g_netApctlInited) {
		hleCall(sceNetAdhocctl, int, sceNetAdhocctlTerm);
	}

	if (apctlProdCodeAddr != 0) {
		userMemory.Free(apctlProdCodeAddr);
		apctlProdCodeAddr = 0;
	}

	// Cleanup Apctl resources
	// Delete fake PSP Thread
	if (apctlThreadID != 0) {
		__KernelStopThread(apctlThreadID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "ApctlThread stopped");
		__KernelDeleteThread(apctlThreadID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "ApctlThread deleted");
		apctlThreadID = 0;
	}

	g_netApctlInited = false;
	netApctlState = PSP_NET_APCTL_STATE_DISCONNECTED;
	return 0;
}

static int sceNetApctlTerm() {
	int retval = NetApctl_Term();
	hleEatMicro(adhocDefaultDelay);
	return hleLogInfo(Log::sceNet, retval);
}

static int sceNetApctlGetInfo(int code, u32 pInfoAddr) {
	DEBUG_LOG(Log::sceNet, "sceNetApctlGetInfo(%i, %08x) at %08x", code, pInfoAddr, currentMIPS->pc);

	switch (code) {
	case PSP_NET_APCTL_INFO_PROFILE_NAME:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_PROFILENAME_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.name, APCTL_PROFILENAME_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_PROFILENAME_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - ProfileName: %s", netApctlInfo.name);
		break;
	case PSP_NET_APCTL_INFO_BSSID:
		if (!Memory::IsValidRange(pInfoAddr, ETHER_ADDR_LEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.bssid, ETHER_ADDR_LEN);
		DEBUG_LOG(Log::sceNet, "ApctlInfo - BSSID: %s", mac2str((SceNetEtherAddr*)&netApctlInfo.bssid).c_str());
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, ETHER_ADDR_LEN, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_SSID:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_SSID_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.ssid, APCTL_SSID_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_SSID_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - SSID: %s", netApctlInfo.ssid);
		break;
	case PSP_NET_APCTL_INFO_SSID_LENGTH:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.ssidLength, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_SECURITY_TYPE:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.securityType, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_STRENGTH:
		if (!Memory::IsValidRange(pInfoAddr, 1))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U8(netApctlInfo.strength, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 1, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_CHANNEL:
		if (!Memory::IsValidRange(pInfoAddr, 1))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U8(netApctlInfo.channel, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 1, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_POWER_SAVE:
		if (!Memory::IsValidRange(pInfoAddr, 1))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U8(netApctlInfo.powerSave, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 1, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_IP:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.ip, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - IP: %s", netApctlInfo.ip);
		break;
	case PSP_NET_APCTL_INFO_SUBNETMASK:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.subNetMask, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - SubNet Mask: %s", netApctlInfo.subNetMask);
		break;
	case PSP_NET_APCTL_INFO_GATEWAY:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.gateway, APCTL_IPADDR_MAXLEN);
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Gateway IP: %s", netApctlInfo.gateway);
		break;
	case PSP_NET_APCTL_INFO_PRIMDNS:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.primaryDns, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Primary DNS: %s", netApctlInfo.primaryDns);
		break;
	case PSP_NET_APCTL_INFO_SECDNS:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.secondaryDns, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Secondary DNS: %s", netApctlInfo.secondaryDns);
		break;
	case PSP_NET_APCTL_INFO_USE_PROXY:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.useProxy, pInfoAddr);
		// Memory::WriteUnchecked_U32(1, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_PROXY_URL:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_URL_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.proxyUrl, APCTL_URL_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_URL_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Proxy URL: %s", netApctlInfo.proxyUrl);
		break;
	case PSP_NET_APCTL_INFO_PROXY_PORT:
		if (!Memory::IsValidRange(pInfoAddr, 2))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U16(netApctlInfo.proxyPort, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 2, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_8021_EAP_TYPE:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.eapType, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_START_BROWSER:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.startBrowser, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_WIFISP:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.wifisp, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	default:
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_INVALID_CODE, "apctl invalid code");
	}

	return hleLogDebug(Log::sceNet, 0);
}

static int NetApctl_AddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = 0;
	struct ApctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	while (apctlHandlers.find(retval) != apctlHandlers.end())
		++retval;

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for (std::map<int, ApctlHandler>::iterator it = apctlHandlers.begin(); it != apctlHandlers.end(); it++) {
		if (it->second.entryPoint == handlerPtr) {
			foundHandler = true;
			break;
		}
	}

	if (!foundHandler && Memory::IsValidAddress(handlerPtr)) {
		if (apctlHandlers.size() >= MAX_APCTL_HANDLERS) {
			ERROR_LOG(Log::sceNet, "Failed to Add handler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS; // TODO: What's the proper error code for Apctl's TOO_MANY_HANDLERS?
			return retval;
		}
		apctlHandlers[retval] = handler;
		WARN_LOG(Log::sceNet, "Added Apctl handler(%x, %x): %d", handlerPtr, handlerArg, retval);
	}
	else {
		ERROR_LOG(Log::sceNet, "Existing Apctl handler(%x, %x)", handlerPtr, handlerArg);
	}

	// The id to return is the number of handlers currently registered
	return retval;
}

// TODO: How many handlers can the PSP actually have for Apctl?
// TODO: Should we allow the same handler to be added more than once?
static u32 sceNetApctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	INFO_LOG(Log::sceNet, "%s(%08x, %08x)", __FUNCTION__, handlerPtr, handlerArg);
	return NetApctl_AddHandler(handlerPtr, handlerArg);
}

// This one logs like a syscall because it's called at the end of some.
static int NetApctl_DelHandler(u32 handlerID) {
	auto iter = apctlHandlers.find(handlerID);
	if (iter != apctlHandlers.end()) {
		apctlHandlers.erase(iter);
		return hleLogInfo(Log::sceNet, 0, "Deleted Apctl handler: %d", handlerID);
	} else {
		return hleLogError(Log::sceNet, -1, "Invalid Apctl handler: %d", handlerID);
	}
}

static int sceNetApctlDelHandler(u32 handlerID) {
	INFO_LOG(Log::sceNet, "%s(%d)", __FUNCTION__, handlerID);
	return NetApctl_DelHandler(handlerID);
}

int sceNetApctlConnect(int confId) {
	if (!g_Config.bEnableWlan)
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_WLAN_SWITCH_OFF, "apctl wlan off");

	if (netApctlState != PSP_NET_APCTL_STATE_DISCONNECTED)
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_NOT_DISCONNECTED, "apctl not disconnected");

	// Is this confId is the index to the scanning's result data or sceNetApctlGetBSSDescIDListUser result?
	netApctlInfoId = confId;

	// Note: We're borrowing AdhocServer for Grouping purpose, so we can simulate Broadcast over the internet just like Adhoc's pro-online implementation
	int ret = 0;
	if (g_infraDNSConfig.connectAdHocForGrouping) {
		ret = hleCall(sceNetAdhocctl, int, sceNetAdhocctlConnect, "INFRA");
	}

	if (netApctlState == PSP_NET_APCTL_STATE_DISCONNECTED) {
		__UpdateApctlHandlers(0, PSP_NET_APCTL_STATE_JOINING, PSP_NET_APCTL_EVENT_CONNECT_REQUEST, 0);
	}

	// hleDelayResult(0, "give time to init/cleanup", adhocEventDelayMS * 1000);
	// TODO: Blocks current thread and wait for a state change to prevent user-triggered connection attempt from causing events to piles up
	return hleLogInfo(Log::sceNet, 0, "connect = %i", ret);
}

int sceNetApctlDisconnect() {
	// Like its 'sister' function sceNetAdhocctlDisconnect, we need to alert Apctl handlers that a disconnect took place
	// or else games like Phantasy Star Portable 2 will hang at certain points (e.g. returning to the main menu after trying to connect to PSN).
	// Note: Since we're borrowing AdhocServer for Grouping purpose, we should disconnect too
	if (g_infraDNSConfig.connectAdHocForGrouping) {
		hleCall(sceNetAdhocctl, int, sceNetAdhocctlDisconnect);
	}

	// Discards any pending events so we can disconnect immediately
	apctlEvents.clear();
	__UpdateApctlHandlers(netApctlState, PSP_NET_APCTL_STATE_DISCONNECTED, PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST, 0);
	// TODO: Blocks current thread and wait for a state change, but the state should probably need to be changed within 1 frame-time (~16ms) 
	return hleLogInfo(Log::sceNet, 0);
}

int NetApctl_GetState() {
	return netApctlState;
}

bool __NetApctlConnected() {
	return netApctlState >= PSP_NET_APCTL_STATE_GOT_IP;
}

static int sceNetApctlGetState(u32 pStateAddr) {
	//if (!netApctlInited) return hleLogError(Log::sceNet, ERROR_NET_APCTL_NOT_IN_BSS, "apctl not in bss");

	// Valid Arguments
	if (Memory::IsValidAddress(pStateAddr)) {
		// Return Thread Status
		Memory::Write_U32(NetApctl_GetState(), pStateAddr);
		// Return Success
		return hleLogDebug(Log::sceNet, 0);
	}

	return hleLogError(Log::sceNet, -1, "apctl invalid arg"); // 0x8002013A or ERROR_NET_WLAN_INVALID_ARG ?
}

// This one logs like a syscall because it's called at the end of some.
int NetApctl_ScanUser() {
	if (!g_Config.bEnableWlan)
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_WLAN_SWITCH_OFF, "apctl wlan off");

	// Scan probably only works when not in connected state, right?
	if (netApctlState != PSP_NET_APCTL_STATE_DISCONNECTED)
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_NOT_DISCONNECTED, "apctl not disconnected");

	__UpdateApctlHandlers(0, PSP_NET_APCTL_STATE_SCANNING, PSP_NET_APCTL_EVENT_SCAN_REQUEST, 0);
	return hleLogInfo(Log::sceNet, 0);
}

static int sceNetApctlScanUser() {
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	return NetApctl_ScanUser();
}

// This one logs like a syscall because it's called at the end of some.
int NetApctl_GetBSSDescIDListUser(u32 sizeAddr, u32 bufAddr) {
	const int userInfoSize = 8; // 8 bytes per entry (next address + entry id)
	// Faking 4 entries, games like MGS:PW Recruit will need to have a different AP for each entry
	int entries = 4;
	if (!Memory::IsValidAddress(sizeAddr) || !Memory::IsValidAddress(bufAddr))
		return hleLogError(Log::sceNet, -1, "apctl invalid arg"); // 0x8002013A or ERROR_NET_WLAN_INVALID_ARG ?

	int size = Memory::Read_U32(sizeAddr);
	// Return size required
	Memory::Write_U32(entries * userInfoSize, sizeAddr);

	if (bufAddr != 0 && Memory::IsValidAddress(sizeAddr)) {
		int offset = 0;
		for (int i = 0; i < entries; i++) {
			// Check if enough space available to write the next structure
			if (offset + userInfoSize > size) {
				break;
			}

			DEBUG_LOG(Log::sceNet, "%s writing ID#%d to %08x", __FUNCTION__, i, bufAddr + offset);

			// Pointer to next Network structure in list
			Memory::Write_U32((i + 1) * userInfoSize + bufAddr, bufAddr + offset);
			offset += 4;

			// Entry ID
			Memory::Write_U32(i, bufAddr + offset);
			offset += 4;
		}
		// Fix the last Pointer
		if (offset > 0)
			Memory::Write_U32(0, bufAddr + offset - userInfoSize);
	}

	return hleLogInfo(Log::sceNet, 0);
}

static int sceNetApctlGetBSSDescIDListUser(u32 sizeAddr, u32 bufAddr) {
	return NetApctl_GetBSSDescIDListUser(sizeAddr, bufAddr);
}

int NetApctl_GetBSSDescEntryUser(int entryId, int infoId, u32 resultAddr) {
	if (!Memory::IsValidAddress(resultAddr))
		return hleLogError(Log::sceNet, -1, "apctl invalid arg"); // 0x8002013A or ERROR_NET_WLAN_INVALID_ARG ?

	// Generate an SSID name
	char dummySSID[APCTL_SSID_MAXLEN] = "WifiAP0";
	dummySSID[6] += static_cast<char>(entryId);

	switch (infoId) {
	case PSP_NET_APCTL_DESC_IBSS: // IBSS, 6 bytes
		if (entryId == 0)
			Memory::Memcpy(resultAddr, netApctlInfo.bssid, sizeof(netApctlInfo.bssid), "GetBSSDescEntryUser");
		else {
			// Generate a BSSID/MAC address
			char dummyMAC[ETHER_ADDR_LEN];
			memset(dummyMAC, entryId, sizeof(dummyMAC));
			// Making sure the 1st 2-bits on the 1st byte of OUI are zero to prevent issue with some games (ie. Gran Turismo)
			dummyMAC[0] &= 0xfc;
			Memory::Memcpy(resultAddr, dummyMAC, sizeof(dummyMAC), "GetBSSDescEntryUser");
		}
		break;
	case PSP_NET_APCTL_DESC_SSID_NAME:
		// Return 32 bytes
		if (entryId == 0)
			Memory::Memcpy(resultAddr, netApctlInfo.ssid, sizeof(netApctlInfo.ssid), "GetBSSDescEntryUser");
		else {
			Memory::Memcpy(resultAddr, dummySSID, sizeof(dummySSID), "GetBSSDescEntryUser");
		}
		break;
	case PSP_NET_APCTL_DESC_SSID_NAME_LENGTH:
		// Return one 32-bit value
		if (entryId == 0)
			Memory::Write_U32(netApctlInfo.ssidLength, resultAddr);
		else {
			// Calculate the SSID length
			Memory::Write_U32((u32)strlen(dummySSID), resultAddr);
		}
		break;
	case PSP_NET_APCTL_DESC_CHANNEL:
		// FIXME: Return one 1 byte value or may be 32-bit if this is not a channel?
		if (entryId == 0)
			Memory::Write_U8(netApctlInfo.channel, resultAddr);
		else {
			// Generate channel for testing purposes, not even sure whether this is channel or not, MGS:PW seems to treat the data as u8
			Memory::Write_U8(entryId, resultAddr);
		}
		break;
	case PSP_NET_APCTL_DESC_SIGNAL_STRENGTH:
		// Return 1 byte
		if (entryId == 0)
			Memory::Write_U8(netApctlInfo.strength, resultAddr);
		else {
			// Randomize signal strength between 1%~99% since games like MGS:PW are using signal strength to determine the strength of the recruit
			Memory::Write_U8((int)(((float)rand() / (float)RAND_MAX) * 99.0 + 1.0), resultAddr);
		}
		break;
	case PSP_NET_APCTL_DESC_SECURITY:
		// Return one 32-bit value
		Memory::Write_U32(netApctlInfo.securityType, resultAddr);
		break;
	default:
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_INVALID_CODE, "unknown info id");
	}

	return 0;
}

static int sceNetApctlGetBSSDescEntryUser(int entryId, int infoId, u32 resultAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i, %i, %08x)", __FUNCTION__, entryId, infoId, resultAddr);
	return NetApctl_GetBSSDescEntryUser(entryId, infoId, resultAddr);
}

static int sceNetApctlScanSSID2() {
	return NetApctl_ScanUser();
}

/**************
* Arg1 = output buffer size being filled? (initially the same with Arg3 ?)
* Arg2 = output buffer? (linked list where the 1st 32-bit is the next address? followed by entry Id? ie. 8-bytes per entry?)
* Arg3 = max buffer size? (ie. 0x100 ?)
* Arg4 = input flag? (initially 0/1 ?)
***************/
static int sceNetApctlGetBSSDescIDList2(u32 Arg1, u32 Arg2, u32 Arg3, u32 Arg4) {
	return NetApctl_GetBSSDescIDListUser(Arg1, Arg2);
}

/**************
* Arg1 = a value returned from sceNetApctlGetBSSDescIDList2 ? entryId?
* Arg2 = input field type within the entry desc (ie. PSP_NET_APCTL_DESC_SSID_NAME ?)
* Arg3 = output buffer for retrieved entry data? (max size = 32 bytes? ie. APCTL_SSID_MAXLEN ? or similar to SceNetApctlInfoInternal union ?)
***************/
static int sceNetApctlGetBSSDescEntry2(int entryId, int infoId, u32 resultAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i, %i, %08x) at %08x", __FUNCTION__, entryId, infoId, resultAddr, currentMIPS->pc);
	return NetApctl_GetBSSDescEntryUser(entryId, infoId, resultAddr);
}

static int sceNetApctlAddInternalHandler(u32 handlerPtr, u32 handlerArg) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %08x)", __FUNCTION__, handlerPtr, handlerArg);
	// This seems to be a 2nd kind of handler
	return NetApctl_AddHandler(handlerPtr, handlerArg);
}

static int sceNetApctlDelInternalHandler(u32 handlerID) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i)", __FUNCTION__, handlerID);
	// This seems to be a 2nd kind of handler
	return NetApctl_DelHandler(handlerID);
}

static int sceNetApctl_A7BB73DF(u32 handlerPtr, u32 handlerArg) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %08x)", __FUNCTION__, handlerPtr, handlerArg);
	// This seems to be a 3rd kind of handler
	// Simple forward, don't need to use hleCall
	return sceNetApctlAddHandler(handlerPtr, handlerArg);
}

static int sceNetApctl_6F5D2981(u32 handlerID) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i)", __FUNCTION__, handlerID);
	// This seems to be a 3rd kind of handler
	// Simple forward, don't need to use hleCall
	return sceNetApctlDelHandler(handlerID);
}

static int sceNetApctl_lib2_69745F0A(int handlerId) {
	return hleLogError(Log::sceNet, 0, "unimplemented");
}

static int sceNetApctl_lib2_4C19731F(int code, u32 pInfoAddr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %08x)", __FUNCTION__, code, pInfoAddr);
	// Simple forward, don't need to use hleCall
	return sceNetApctlGetInfo(code, pInfoAddr);
}

static int sceNetApctlScan() {
	return NetApctl_ScanUser();
}

static int sceNetApctlGetBSSDescIDList(u32 sizeAddr, u32 bufAddr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %08x)", __FUNCTION__, sizeAddr, bufAddr);
	return sceNetApctlGetBSSDescIDListUser(sizeAddr, bufAddr);
}

static int sceNetApctlGetBSSDescEntry(int entryId, int infoId, u32 resultAddr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %i, %08x)", __FUNCTION__, entryId, infoId, resultAddr);
	return sceNetApctlGetBSSDescEntryUser(entryId, infoId, resultAddr);
}

static int sceNetApctl_lib2_C20A144C(int connIndex, u32 ps3MacAddressPtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %08x)", __FUNCTION__, connIndex, ps3MacAddressPtr);
	// Simple forward, don't need to use hleCall
	return sceNetApctlConnect(connIndex);
}

static int sceNetUpnpInit(int unknown1,int unknown2) {
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetUpnpStart() {
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetUpnpStop() {
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetUpnpTerm() {
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetUpnpGetNatInfo() {
	return hleLogError(Log::sceNet, 0, "UNIMPL");
}

static int sceNetGetDropRate(u32 dropRateAddr, u32 dropDurationAddr) {
	Memory::Write_U32(netDropRate, dropRateAddr);
	Memory::Write_U32(netDropDuration, dropDurationAddr);
	return hleLogInfo(Log::sceNet, 0);
}

static int sceNetSetDropRate(u32 dropRate, u32 dropDuration) {
	netDropRate = dropRate;
	netDropDuration = dropDuration;
	return hleLogInfo(Log::sceNet, 0);
}

const HLEFunction sceNet[] = {
	{0X39AF39A6, &WrapI_UUUUU<sceNetInit>,           "sceNetInit",                      'i', "xxxxx"},
	{0X281928A9, &WrapU_V<sceNetTerm>,               "sceNetTerm",                      'x', ""     },
	{0X89360950, &WrapV_UU<sceNetEtherNtostr>,       "sceNetEtherNtostr",               'v', "xx"   },
	{0XD27961C9, &WrapV_UU<sceNetEtherStrton>,       "sceNetEtherStrton",               'v', "xx"   },
	{0X0BF0A3AE, &WrapU_U<sceNetGetLocalEtherAddr>,  "sceNetGetLocalEtherAddr",         'x', "x"    },
	{0X50647530, &WrapI_I<sceNetFreeThreadinfo>,     "sceNetFreeThreadinfo",            'i', "i"    },
	{0XCC393E48, &WrapI_U<sceNetGetMallocStat>,      "sceNetGetMallocStat",             'i', "p"    },
	{0XAD6844C6, &WrapI_I<sceNetThreadAbort>,        "sceNetThreadAbort",               'i', "i"    },
};

const HLEFunction sceNetApctl[] = {
	{0XCFB957C6, &WrapI_I<sceNetApctlConnect>,       "sceNetApctlConnect",              'i', "i"    },
	{0X24FE91A1, &WrapI_V<sceNetApctlDisconnect>,    "sceNetApctlDisconnect",           'i', ""     },
	{0X5DEAC81B, &WrapI_U<sceNetApctlGetState>,      "sceNetApctlGetState",             'i', "x"    },
	{0X8ABADD51, &WrapU_UU<sceNetApctlAddHandler>,   "sceNetApctlAddHandler",           'x', "xx"   },
	{0XE2F91F9B, &WrapI_II<sceNetApctlInit>,          "sceNetApctlInit",                'i', "ii"   },
	{0X5963991B, &WrapI_U<sceNetApctlDelHandler>,    "sceNetApctlDelHandler",           'i', "x"    },
	{0XB3EDD0EC, &WrapI_V<sceNetApctlTerm>,          "sceNetApctlTerm",                 'i', ""     },
	{0X2BEFDF23, &WrapI_IU<sceNetApctlGetInfo>,      "sceNetApctlGetInfo",              'i', "ix"   },
	{0XA3E77E13, &WrapI_V<sceNetApctlScanSSID2>,     "sceNetApctlScanSSID2",            'i', ""     },
	{0XE9B2E5E6, &WrapI_V<sceNetApctlScanUser>,                 "sceNetApctlScanUser",             'i', ""     },
	{0XF25A5006, &WrapI_UUUU<sceNetApctlGetBSSDescIDList2>,     "sceNetApctlGetBSSDescIDList2",    'i', "xxxx" },
	{0X2935C45B, &WrapI_IIU<sceNetApctlGetBSSDescEntry2>,       "sceNetApctlGetBSSDescEntry2",     'i', "iix"  },
	{0X04776994, &WrapI_IIU<sceNetApctlGetBSSDescEntryUser>,    "sceNetApctlGetBSSDescEntryUser",  'i', "iix"  },
	{0X6BDDCB8C, &WrapI_UU<sceNetApctlGetBSSDescIDListUser>,    "sceNetApctlGetBSSDescIDListUser", 'i', "xx"   },
	{0X7CFAB990, &WrapI_UU<sceNetApctlAddInternalHandler>,      "sceNetApctlAddInternalHandler",   'i', "xx"   },
	{0XE11BAFAB, &WrapI_U<sceNetApctlDelInternalHandler>,       "sceNetApctlDelInternalHandler",   'i', "x"    },
	{0XA7BB73DF, &WrapI_UU<sceNetApctl_A7BB73DF>,               "sceNetApctl_A7BB73DF",            'i', "xx"   },
	{0X6F5D2981, &WrapI_U<sceNetApctl_6F5D2981>,                "sceNetApctl_6F5D2981",            'i', "x"    },
	{0X69745F0A, &WrapI_I<sceNetApctl_lib2_69745F0A>,           "sceNetApctl_lib2_69745F0A",       'i', "i"    },
	{0X4C19731F, &WrapI_IU<sceNetApctl_lib2_4C19731F>,          "sceNetApctl_lib2_4C19731F",       'i', "ix"   },
	{0XB3CF6849, &WrapI_V<sceNetApctlScan>,                     "sceNetApctlScan",                 'i', ""     },
	{0X0C7FFA5C, &WrapI_UU<sceNetApctlGetBSSDescIDList>,        "sceNetApctlGetBSSDescIDList",     'i', "xx"   },
	{0X96BEB231, &WrapI_IIU<sceNetApctlGetBSSDescEntry>,        "sceNetApctlGetBSSDescEntry",      'i', "iix"  },
	{0XC20A144C, &WrapI_IU<sceNetApctl_lib2_C20A144C>,          "sceNetApctl_lib2_C20A144C",       'i', "ix"   },
	// Fake function for PPSSPP's use.
	{0X756E6F10, &WrapV_V<__NetApctlCallbacks>,                 "__NetApctlCallbacks",             'v', ""     },
};

const HLEFunction sceWlanDrv[] = {
	{0XD7763699, &WrapU_V<sceWlanGetSwitchState>,    "sceWlanGetSwitchState",           'x', ""     },
	{0X0C622081, &WrapU_U<sceWlanGetEtherAddr>,      "sceWlanGetEtherAddr",             'x', "x"    },
	{0X93440B11, &WrapU_V<sceWlanDevIsPowerOn>,      "sceWlanDevIsPowerOn",             'x', ""     },
};

// see http://www.kingx.de/forum/showthread.php?tid=35164
const HLEFunction sceNetUpnp[] = {
	{0X27045362, &WrapI_V<sceNetUpnpGetNatInfo>,     "sceNetUpnpGetNatInfo",            'i', ""     },
	{0X3432B2E5, &WrapI_V<sceNetUpnpStart>,          "sceNetUpnpStart",                 'i', ""     },
	{0X3E32ED9E, &WrapI_V<sceNetUpnpStop>,           "sceNetUpnpStop",                  'i', ""     },
	{0X540491EF, &WrapI_V<sceNetUpnpTerm>,           "sceNetUpnpTerm",                  'i', ""     },
	{0XE24220B5, &WrapI_II<sceNetUpnpInit>,          "sceNetUpnpInit",                  'i', "ii"   },
};

const HLEFunction sceNetIfhandle[] = {
	{0xC80181A2, &WrapI_UU<sceNetGetDropRate>,     "sceNetGetDropRate",                 'i', "pp" },
	{0xFD8585E1, &WrapI_UU<sceNetSetDropRate>,     "sceNetSetDropRate",                 'i', "ii" },
};

void Register_sceNet() {
	RegisterModule("sceNet", ARRAY_SIZE(sceNet), sceNet);
}

void Register_sceNetApctl() {
	RegisterModule("sceNetApctl", ARRAY_SIZE(sceNetApctl), sceNetApctl);
}

void Register_sceWlanDrv() {
	RegisterModule("sceWlanDrv", ARRAY_SIZE(sceWlanDrv), sceWlanDrv);
}

void Register_sceNetUpnp() {
	RegisterModule("sceNetUpnp", ARRAY_SIZE(sceNetUpnp), sceNetUpnp);
}

void Register_sceNetIfhandle() {
	RegisterModule("sceNetIfhandle", ARRAY_SIZE(sceNetIfhandle), sceNetIfhandle);
}

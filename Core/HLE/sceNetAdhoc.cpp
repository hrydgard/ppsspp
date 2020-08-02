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

#if !defined(_WIN32)
#include <netinet/tcp.h>
#endif

#include <mutex>
#include "thread/threadutil.h"
// sceNetAdhoc

// This is a direct port of Coldbird's code from http://code.google.com/p/aemu/
// All credit goes to him!
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/MemMapHelpers.h"
#include "Common/ChunkFile.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Util/PortManager.h"

#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/proAdhocServer.h"
#include "i18n/i18n.h"

// shared in sceNetAdhoc.h since it need to be used from sceNet.cpp also
// TODO: Make accessor functions instead, and throw all this state in a struct.
bool netAdhocInited;
bool netAdhocctlInited;
bool networkInited = false;

static bool netAdhocMatchingInited;
int netAdhocMatchingStarted = 0;
int adhocDefaultTimeout = 2000; //5000
int adhocExtraPollDelayMS = 10; //10
int adhocEventPollDelayMS = 100; //100
int adhocMatchingEventDelayMS = 30; //30
int adhocEventDelayMS = 500; //500; This will affect the duration of "Connecting..." dialog/message box in .Hack//Link and Naruto Ultimate Ninja Heroes 3

SceUID threadAdhocID;

std::recursive_mutex adhocEvtMtx;
std::deque<std::pair<u32, u32>> adhocctlEvents;
std::deque<MatchingArgs> matchingEvents;
std::map<int, AdhocctlHandler> adhocctlHandlers;
std::vector<SceUID> matchingThreads;
int IsAdhocctlInCB = 0;

u32 dummyThreadHackAddr = 0;
u32_le dummyThreadCode[3];
u32 matchingThreadHackAddr = 0;
u32_le matchingThreadCode[3];

int matchingEventThread(int matchingId); 
int matchingInputThread(int matchingId); 

int sceNetAdhocTerm();
int sceNetAdhocctlTerm();
int sceNetAdhocMatchingTerm();
int sceNetAdhocMatchingSetHelloOpt(int matchingId, int optLenAddr, u32 optDataAddr);

void __NetAdhocShutdown() {
	// Kill AdhocServer Thread
	if (adhocServerRunning) {
		adhocServerRunning = false;
		if (adhocServerThread.joinable()) {
			adhocServerThread.join();
		}
	}
	// Checks to avoid confusing logspam
	if (netAdhocMatchingInited) {
		sceNetAdhocMatchingTerm();
	}
	if (netAdhocctlInited) {
		sceNetAdhocctlTerm();
	}
	if (netAdhocInited) {
		// Should not really call HLE funcs from shutdown, but we need to cleanup
		sceNetAdhocTerm();
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

void __NetAdhocDoState(PointerWrap &p) {
	auto s = p.Section("sceNetAdhoc", 1, 4);
	if (!s)
		return;

	auto cur_netAdhocInited = netAdhocInited;
	auto cur_netAdhocctlInited = netAdhocctlInited;
	auto cur_netAdhocMatchingInited = netAdhocMatchingInited;

	p.Do(netAdhocInited);
	p.Do(netAdhocctlInited);
	p.Do(netAdhocMatchingInited);
	p.Do(adhocctlHandlers);

	if (s >= 2) {
		p.Do(actionAfterMatchingMipsCall);
		__KernelRestoreActionType(actionAfterMatchingMipsCall, AfterMatchingMipsCall::Create);

		p.Do(dummyThreadHackAddr);
	}
	else {
		actionAfterMatchingMipsCall = -1;
		dummyThreadHackAddr = 0;
	}
	if (s >= 3) {
		p.Do(actionAfterAdhocMipsCall);
		__KernelRestoreActionType(actionAfterAdhocMipsCall, AfterAdhocMipsCall::Create);

		p.Do(matchingThreadHackAddr);
	}
	else {
		actionAfterAdhocMipsCall = -1;
		matchingThreadHackAddr = 0;
	}
	if (s >= 4) {
		p.Do(threadAdhocID);
		p.Do(matchingThreads);
	}
	else {
		threadAdhocID = 0;
		for (auto& it : matchingThreads) {
			it = 0;
		}
	}
	
	if (p.mode == p.MODE_READ) {
		// Previously, this wasn't being saved.  It needs its own space.
		if (!dummyThreadHackAddr || (dummyThreadHackAddr && strcmp("dummythreadhack", kernelMemory.GetBlockTag(dummyThreadHackAddr)) != 0)) {
			u32 blockSize = sizeof(dummyThreadCode);
			dummyThreadHackAddr = kernelMemory.Alloc(blockSize, false, "dummythreadhack");
		}
		if (!matchingThreadHackAddr || (matchingThreadHackAddr && strcmp("matchingThreadHack", kernelMemory.GetBlockTag(matchingThreadHackAddr)) != 0)) {
			u32 blockSize = sizeof(matchingThreadCode);
			matchingThreadHackAddr = kernelMemory.Alloc(blockSize, false, "matchingThreadHack");
		}
	
		// Restore dummy Loop MIPS code to prevent crashes after loading from SaveState
		if (dummyThreadHackAddr) Memory::Memcpy(dummyThreadHackAddr, dummyThreadCode, sizeof(dummyThreadCode));
		if (matchingThreadHackAddr) Memory::Memcpy(matchingThreadHackAddr, matchingThreadCode, sizeof(matchingThreadCode));
		
		// Let's not change "Inited" value when Loading SaveState to prevent memory & port leaks
		netAdhocMatchingInited = cur_netAdhocMatchingInited;
		netAdhocctlInited = cur_netAdhocctlInited;
		netAdhocInited = cur_netAdhocInited;
	}
}

void __UpdateAdhocctlHandlers(u32 flag, u32 error) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	adhocctlEvents.push_back({ flag, error });
}

void __UpdateMatchingHandler(MatchingArgs ArgsPtr) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	matchingEvents.push_back(ArgsPtr);
}

static int getBlockingFlag(int id) {
#ifdef _WIN32
	return 0;
#else
	int sockflag = fcntl(id, F_GETFL, O_NONBLOCK);
	return sockflag & O_NONBLOCK;
#endif
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

void __NetAdhocInit() {
	friendFinderRunning = false;
	netAdhocInited = false;
	netAdhocctlInited = false;
	netAdhocMatchingInited = false;
	adhocctlHandlers.clear();
	__AdhocServerInit();

	// Create built-in AdhocServer Thread
	if (g_Config.bEnableWlan && g_Config.bEnableAdhocServer) {
		adhocServerRunning = true;
		adhocServerThread = std::thread(proAdhocServerThread, SERVER_PORT);
	}
}

u32 sceNetAdhocInit() {
	if (!netAdhocInited) {
		// Library initialized
		netAdhocInited = true;

		// Return Success
		return hleLogSuccessInfoI(SCENET, 0, "at %08x", currentMIPS->pc);
	}
	// Already initialized
	return hleLogWarning(SCENET, ERROR_NET_ADHOC_ALREADY_INITIALIZED, "already initialized");
}

static u32 sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlInit(%i, %i, %08x) at %08x", stackSize, prio, productAddr, currentMIPS->pc);
	
	if (netAdhocctlInited)
		return ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED;

	if (Memory::IsValidAddress(productAddr)) {
		Memory::ReadStruct(productAddr, &product_code);
	}

	netAdhocctlInited = true; //needed for cleanup during AdhocctlTerm even when it failed to connect to Adhoc Server (since it's being faked as success)

	// Create fake PSP Thread for callback
	// TODO: Should use a separated threads for friendFinder, matchingEvent, and matchingInput and created on AdhocctlInit & AdhocMatchingStart instead of here
	threadAdhocID = __KernelCreateThread("AdhocThread", __KernelGetCurThreadModuleId(), dummyThreadHackAddr, prio, stackSize, PSP_THREAD_ATTR_USER, 0, true);
	if (threadAdhocID > 0) {
		__KernelStartThread(threadAdhocID, 0, 0);
	}

	// TODO: Merging friendFinder (real) thread to AdhocThread (fake) thread on PSP side
	if (!friendFinderRunning) {
		friendFinderRunning = true;
		friendFinderThread = std::thread(friendFinder);
	}
	
	// Need to make sure to be connected to adhoc server before returning to prevent GTA VCS failed to create/join a group and unable to see any game room
	int cnt = 0;
	while (g_Config.bEnableWlan && !networkInited && (cnt < adhocDefaultTimeout)) {
		sleep_ms(1);
		cnt++;
	}

	//hleDelayResult(0, "give some time", adhocEventPollDelayMS * 1000); // Give a little time for friendFinder thread to be ready before the game use the next sceNet functions, should've checked for friendFinderRunning status instead of guessing the time?
	return 0;
}

static int sceNetAdhocctlGetState(u32 ptrToStatus) {
	// Library initialized
	if (netAdhocctlInited) {
		// Valid Arguments
		if (Memory::IsValidAddress(ptrToStatus)) {
			// Return Thread Status
			Memory::Write_U32(threadStatus, ptrToStatus);
			// Return Success
			return 0;
		}
		
		// Invalid Arguments
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}
	
	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
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
static int sceNetAdhocPdpCreate(const char *mac, int port, int bufferSize, u32 unknown) {
	INFO_LOG(SCENET, "sceNetAdhocPdpCreate(%s, %u, %u, %u) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), port, bufferSize, unknown, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	int retval = ERROR_NET_ADHOC_NOT_INITIALIZED;
	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)mac;
	if (netAdhocInited) {
		// Valid Arguments are supplied
		if (mac != NULL && bufferSize > 0) {
			//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
			if (port == 0) port = -(int)portOffset;
			// Some games (ie. DBZ Shin Budokai 2) might be getting the saddr/srcmac content from SaveState and causing problems :( So we try to fix it here
			if (saddr != NULL) {
				getLocalMac(saddr);
			}
			// Valid MAC supplied
			if (isLocalMAC(saddr)) {
				//// Unused Port supplied
				//if (!_IsPDPPortInUse(port)) {} 
				//
				//// Port is in use by another PDP Socket
				//return ERROR_NET_ADHOC_PORT_IN_USE;

				// Create Internet UDP Socket
				int usocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				// Valid Socket produced
				if (usocket != INVALID_SOCKET) {
					// Change socket buffer size when necessary
					if (getSockBufferSize(usocket, SO_SNDBUF) < bufferSize) setSockBufferSize(usocket, SO_SNDBUF, bufferSize);
					if (getSockBufferSize(usocket, SO_RCVBUF) < bufferSize) setSockBufferSize(usocket, SO_RCVBUF, bufferSize);

					// Enable KeepAlive
					setSockKeepAlive(usocket, true);

					// Enable Port Re-use, this will allow binding to an already used port, but only one of them can read the data (shared receive buffer?)
					int one = 1;
					setsockopt(usocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one)); // NO idea if we need this

					// Binding Information for local Port
					sockaddr_in addr;
					addr.sin_family = AF_INET;
					addr.sin_addr.s_addr = INADDR_ANY;
					if (isLocalServer) {
						getLocalIp(&addr);
					}

					addr.sin_port = htons(port + portOffset);
					// The port might be under 1024 (ie. GTA:VCS use port 1, Ford Street Racing use port 0 (UNUSED_PORT), etc) and already used by other application/host OS, should we add 1024 to the port whenever it tried to use an already used port?

					// Bound Socket to local Port
					int iResult = bind(usocket, (sockaddr *)&addr, sizeof(addr));

					if (iResult == 0) {
						// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
						socklen_t len = sizeof(addr);
						if (getsockname(usocket, (sockaddr*)&addr, &len) == 0) {
							port = ntohs(addr.sin_port) - portOffset;
						}

						// Allocate Memory for Internal Data
						SceNetAdhocPdpStat * internal = (SceNetAdhocPdpStat *)malloc(sizeof(SceNetAdhocPdpStat));

						// Allocated Memory
						if (internal != NULL) {
							// Clear Memory
							memset(internal, 0, sizeof(SceNetAdhocPdpStat));

							// Find Free Translator Index
							int i = 0; 
							for (; i < 255; i++) if (pdp[i] == NULL) break;

							// Found Free Translator Index
							if (i < 255) {
								// Fill in Data
								internal->id = usocket;
								internal->laddr = *saddr;
								internal->lport = port; //getLocalPort(usocket) - portOffset;
								internal->rcv_sb_cc = bufferSize;

								// Link Socket to Translator ID
								pdp[i] = internal;

								// Forward Port on Router
								//sceNetPortOpen("UDP", port);
								UPnP_Add(IP_PROTOCOL_UDP, isOriPort ? port : port + portOffset, port + portOffset); // g_PortManager.Add(IP_PROTOCOL_UDP, isOriPort ? port : port + portOffset, port + portOffset);
								
								// Success
								return i + 1;
							} 

							// Free Memory for Internal Data
							free(internal);
						}
						retval = ERROR_NET_NO_SPACE;
					}
					else {
						retval = ERROR_NET_ADHOC_PORT_IN_USE;
						if (iResult == SOCKET_ERROR) {
							ERROR_LOG(SCENET, "Socket error (%i) when binding port %u", errno, ntohs(addr.sin_port));
							auto n = GetI18NCategory("Networking");
							host->NotifyUserMessage(std::string(n->T("Failed to Bind Port")) + " " + std::to_string(port + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 3.0, 0x0000ff);
						}
					}

					// Close Socket
					closesocket(usocket);
					return retval;
				}

				// Default to No-Space Error
				return ERROR_NET_NO_SPACE;
			}

			// Invalid MAC supplied
			//return ERROR_NET_ADHOC_INVALID_ADDR;
		}

		// Invalid Arguments were supplied
		return ERROR_NET_ADHOC_INVALID_ARG;
	}
	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
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
	if (netAdhocctlInited) {
		// Valid Arguments
		if (Memory::IsValidAddress(paramAddr)) {
			// Copy Parameter
			Memory::WriteStruct(paramAddr,&parameter);
			// Return Success
			return 0;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
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
				if (id > 0 && id <= 255 && pdp[id - 1] != NULL) {
					// Cast Socket
					SceNetAdhocPdpStat * socket = pdp[id - 1];

					// Valid Data Buffer
					if (data != NULL) {
						// Valid Destination Address
						if (daddr != NULL) {
							// Log Destination
							// Schedule Timeout Removal
							//if (flag) timeout = 0;

							// Apply Send Timeout Settings to Socket
							if (timeout > 0) 
								setSockTimeout(socket->id, SO_SNDTIMEO, timeout);

							int maxlen = getSockMaxSize(socket->id);

							// Single Target
							if (!isBroadcastMAC(daddr)) {
								// Fill in Target Structure
								sockaddr_in target;
								target.sin_family = AF_INET;
								target.sin_port = htons(dport + portOffset);

								// Get Peer IP
								if (resolveMAC((SceNetEtherAddr *)daddr, (uint32_t *)&target.sin_addr.s_addr)) {
									// Some games (ie. PSP2) might try to talk to it's self, not sure if they talked through WAN or LAN when using public Adhoc Server tho
									target.sin_port = htons(dport + ((isOriPort && !isPrivateIP(target.sin_addr.s_addr)) ? 0 : portOffset));

									// Acquire Network Lock
									//_acquireNetworkLock();

									// Send Data. UDP are guaranteed to be sent as a whole or nothing(failed if len > SO_MAX_MSG_SIZE), and never be partially sent/recv
									changeBlockingMode(socket->id, flag);
									int sent = sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
									int error = errno;
									if (sent == SOCKET_ERROR) {
										DEBUG_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u] (size=%i)", error, id, getLocalPort(socket->id), ntohs(target.sin_port), len);
									}
									changeBlockingMode(socket->id, 0);

									// Free Network Lock
									//_freeNetworkLock();

									// Sent Data
									if (sent >= 0) {
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Sent %u bytes to %s:%u\n", id, getLocalPort(socket->id), sent, inet_ntoa(target.sin_addr), ntohs(target.sin_port));

										// Success
										return 0; // sent; // MotorStorm will try to resend if return value is not 0
									}

									// Blocking Situation
									if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;

									// Timeout
									/*if (timeout > 0) 
										return ERROR_NET_ADHOC_TIMEOUT;*/
									return ERROR_NET_ADHOC_INVALID_ADDR; // There is no concept of Timeout when sending UDP due to no ACK, not sure about PDP since some games did use the timeout arg
								}
								//VERBOSE_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Unknown Target Peer %s:%u\n", id, getLocalPort(socket->id), mac2str(daddr, tmpmac), ntohs(target.sin_port));
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

								// Send Data
								changeBlockingMode(socket->id, flag); // Do we need to switched to blocking-mode to make sure the data are fully sent?

								// Iterate Peers
								SceNetAdhocctlPeerInfo * peer = friends;
								for (; peer != NULL; peer = peer->next) {
									// Does Skipping sending to timed out friends could cause desync when players moving group at the time MP game started?
									if (peer->last_recv == 0) 
										continue;

									// Fill in Target Structure
									sockaddr_in target;
									target.sin_family = AF_INET;
									target.sin_addr.s_addr = peer->ip_addr;
									target.sin_port = htons(dport + ((isOriPort && !isPrivateIP(peer->ip_addr)) ? 0 : portOffset));
									
									int sent = sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
									int error = errno;
									if (sent == SOCKET_ERROR) {
										DEBUG_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u](BC) [size=%i]", error, id, getLocalPort(socket->id), ntohs(target.sin_port), len);
									}
									
									if (sent >= 0) {
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u](BC): Sent %u bytes to %s:%u\n", id, getLocalPort(socket->id), sent, inet_ntoa(target.sin_addr), ntohs(target.sin_port));
									}
								}

								changeBlockingMode(socket->id, 0);

								// Free Peer Lock
								peerlock.unlock();

								// Free Network Lock
								//_freeNetworkLock();

								// Success, Broadcast never fails!
								return 0; // len;
							}
						}

						// Invalid Destination Address
						return ERROR_NET_ADHOC_INVALID_ADDR;
					}

					// Invalid Argument
					return ERROR_NET_ADHOC_INVALID_ARG;
				}

				// Invalid Socket ID
				return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
			}

			// Invalid Data Length
			return ERROR_NET_ADHOC_INVALID_DATALEN;
		}

		// Invalid Destination Port
		return ERROR_NET_ADHOC_INVALID_PORT;
	}

	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;

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
		if (id > 0 && id <= 255 && pdp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPdpStat * socket = pdp[id - 1];

			// Valid Arguments
			if (saddr != NULL && port != NULL && buf != NULL && len != NULL && *len > 0) { 
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
					setSockTimeout(socket->id, SO_RCVTIMEO, timeout);

				// Sender Address
				sockaddr_in sin;

				// Set Address Length (so we get the sender ip)
				socklen_t sinlen = sizeof(sin);
				//sin.sin_len = (uint8_t)sinlen;

				// Acquire Network Lock
				//_acquireNetworkLock();

				// TODO: Use a different thread (similar to sceIo) for recvfrom, recv & accept to prevent blocking-socket from blocking emulation (ie. Hot Shots Tennis:GaG)			
				int received = 0;
				int error = 0;
				
				
				// Receive Data. PDP always sent in full size or nothing(failed), recvfrom will always receive in full size as requested (blocking) or failed (non-blocking). If available UDP data is larger than buffer, excess data is lost.
				changeBlockingMode(socket->id, 1);
				// Should peek first for the available data size if it's more than len return ERROR_NET_ADHOC_NOT_ENOUGH_SPACE along with required size in len to prevent losing excess data
				received = recvfrom(socket->id, (char*)buf, *len, MSG_PEEK, (sockaddr*)&sin, &sinlen);
				if (received != SOCKET_ERROR && *len < received) {
					changeBlockingMode(socket->id, 0);
					WARN_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Peeked %u/%u bytes from %s:%u\n", id, getLocalPort(socket->id), received, *len, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
					*len = received;

					// Peer MAC
					SceNetEtherAddr mac;

					// Find Peer MAC
					if (resolveIP(sin.sin_addr.s_addr, &mac)) {
						// Provide Sender Information
						*saddr = mac;
						*sport = ntohs(sin.sin_port) - portOffset;

						// Update last recv timestamp, may cause disconnection not detected properly tho
						peerlock.lock();
						auto peer = findFriend(&mac);
						if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
						peerlock.unlock();
					}

					// Free Network Lock
					//_freeNetworkLock();

					return ERROR_NET_ADHOC_NOT_ENOUGH_SPACE; //received;
				}
				received = recvfrom(socket->id, (char*)buf, *len, 0, (sockaddr*)&sin, &sinlen);
				error = errno;

				if (flag == 0) {
					// Simulate blocking behaviour with non-blocking socket
					uint32_t starttime = (uint32_t)(real_time_now() * 1000000.0);
					// Wait for Connection. On Windows: recvfrom on UDP can get error WSAECONNRESET when previous sendto's destination is unreachable (or destination port is not bound), may need to disable SIO_UDP_CONNRESET
					while ((timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout) && (received == SOCKET_ERROR) && (connectInProgress(error) || error == ECONNRESET)) {
						received = recvfrom(socket->id, (char*)buf, *len, 0, (sockaddr*)&sin, &sinlen);
						error = errno;
						// Wait 1ms
						sleep_ms(1);
					}
				}

				if (received == SOCKET_ERROR) {
					VERBOSE_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpRecv[%i:%u] [size=%i]", error, id, socket->lport, *len);
				}
				changeBlockingMode(socket->id, 0);
				

				// Received Data. UDP can also receives 0 data, while on TCP 0 data = connection gracefully closed, but not sure about PDP tho
				if (received > 0) {
					DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %u bytes from %s:%u\n", id, getLocalPort(socket->id), received, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

					// Peer MAC
					SceNetEtherAddr mac;

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
					// Unknown Peer, let's not give any data
					else {
						//*len = 0; // received; // Is this going to be okay since saddr may not be valid?
						WARN_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %i bytes from Unknown Peer %s:%u", id, getLocalPort(socket->id), received, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
					}
					// Free Network Lock
					//_freeNetworkLock();

					//free(tmpbuf);

					//Receiving data from unknown peer, ignore it ?
					//return ERROR_NET_ADHOC_WOULD_BLOCK; //ERROR_NET_ADHOC_NO_DATA_AVAILABLE
				}

				// Free Network Lock
				//_freeNetworkLock();

#ifdef PDP_DIRTY_MAGIC
				// Restore Nonblocking Flag for Return Value
				if (wouldblock) flag = 1;
#endif

				// Nothing received. On Windows: recvfrom on UDP can get error WSAECONNRESET when previous sendto's destination is unreachable (or destination port is not bound), may need to disable SIO_UDP_CONNRESET
				if (received == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || error == ECONNRESET || error == ETIMEDOUT)) {
					// Blocking Situation
					if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;

					// Timeout
					return ERROR_NET_ADHOC_TIMEOUT;
				}

				// Disconnected
				return ERROR_NET_ADHOC_DISCONNECTED;
			}

			// Invalid Argument
			return ERROR_NET_ADHOC_INVALID_ARG;
		}

		// Invalid Socket ID
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
	}

	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

// Flags seems to be bitmasks of ADHOC_F_ALERT...
int sceNetAdhocSetSocketAlert(int id, int flag) {
 	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocSetSocketAlert(%d, %08x)", id, flag);

	return 0; //Dummy Result
}

int sceNetAdhocPollSocket(u32 socketStructAddr, int count, int timeout, int nonblock) { // timeout in microseconds
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocPollSocket(%08x, %i, %i, %i) at %08x", socketStructAddr, count, timeout, nonblock, currentMIPS->pc);
	
	// Library is initialized
	if (netAdhocInited)
	{
		SceNetAdhocPollSd * sds = NULL;
		if (Memory::IsValidAddress(socketStructAddr)) sds = (SceNetAdhocPollSd *)Memory::GetPointer(socketStructAddr);

		// Valid Arguments
		if (sds != NULL && count > 0)
		{
			// Socket Check
			for (int i = 0; i < count; i++)
			{
				// Invalid Socket
				if (sds[i].id < 1 || sds[i].id > 255 || (pdp[sds[i].id - 1] == NULL && ptp[sds[i].id - 1] == NULL)) return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
			}

			// Nonblocking Mode
			if (nonblock) timeout = 0;

			// Prevent Nonblocking Mode
			else 
			if (timeout == 0) timeout = 1;

			int affectedsockets = 0;
			if (count > (int)FD_SETSIZE) count = FD_SETSIZE; // return affectedsockets;

			// Acquire Network Lock
			//acquireNetworkLock();

			// Poll Sockets
			//int affectedsockets = sceNetInetPoll(isds, count, timeout);

			//WSAPoll only available for Vista or newer, so we'll use an alternative way for XP since Windows doesn't have poll function like *NIX
			fd_set readfds, writefds, exceptfds;
			int fd;
			FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);
			// TODO: PDP and PTP should share the same indexing to prevent identical PDP & PTP socket id
			for (int i = 0; i < count; i++) {
				sds[i].revents = 0;
				// Fill in Socket ID
				if (ptp[sds[i].id - 1] != NULL) {
					fd = ptp[sds[i].id - 1]->id;
					if (ptp[sds[i].id - 1]->state == ADHOC_PTP_STATE_LISTEN) sds[i].revents |= ADHOC_EV_ACCEPT;
					else
					if (ptp[sds[i].id - 1]->state == ADHOC_PTP_STATE_CLOSED) sds[i].revents |= ADHOC_EV_CONNECT;
				}
				else {
					fd = pdp[sds[i].id - 1]->id;
				}
				if (sds[i].events & ADHOC_EV_RECV) FD_SET(fd, &readfds);
				if (sds[i].events & ADHOC_EV_SEND) FD_SET(fd, &writefds); 
				//if (sds[i].events & ADHOC_EV_ALERT) 
				FD_SET(fd, &exceptfds);
			}
			timeval tmout;
			tmout.tv_sec = timeout / 1000000; // seconds
			tmout.tv_usec = (timeout % 1000000); // microseconds
			affectedsockets = select(count, &readfds, &writefds, &exceptfds, &tmout);
			if (affectedsockets > 0) {
				affectedsockets = 0;
				for (int i = 0; i < count; i++) {
					if (ptp[sds[i].id - 1] != NULL) {
						fd = ptp[sds[i].id - 1]->id;
					}
					else {
						fd = pdp[sds[i].id - 1]->id;
					}
					if (FD_ISSET(fd, &readfds)) sds[i].revents |= ADHOC_EV_RECV;
					if (FD_ISSET(fd, &writefds)) sds[i].revents |= ADHOC_EV_SEND; // Data can always be sent ?
					sds[i].revents &= sds[i].events;
					if (FD_ISSET(fd, &exceptfds)) sds[i].revents |= ADHOC_EV_ALERT; // can be raised on revents regardless of events bitmask?
					if (sds[i].revents) affectedsockets++;
				}
			}
			// Free Network Lock
			//freeNetworkLock();

			// Blocking Mode (Nonblocking Mode returns 0, even on Success)
			if (!nonblock)
			{
				// Success
				if (affectedsockets >= 0) return affectedsockets; // (affectedsockets > 0)

				// Timeout
				return ERROR_NET_ADHOC_TIMEOUT;
			}

			// No Events generated
			if (affectedsockets >= 0) return 0;

			return ERROR_NET_ADHOC_WOULD_BLOCK; // Bleach 7 seems to use nonblocking and check the return value against 0 and 0x80410709 (also 0x80410717 ?)
		}

		// Invalid Argument
		return ERROR_NET_ADHOC_INVALID_ARG;
	}

	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int NetAdhocPdp_Delete(int id, int unknown) {
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (id > 0 && id <= 255) {
			// Cast Socket
			SceNetAdhocPdpStat* sock = pdp[id - 1];

			// Valid Socket
			if (sock != NULL) {
				// Close Connection
				closesocket(sock->id);

				// Remove Port Forward from Router
				//sceNetPortClose("UDP", sock->lport);
				//g_PortManager.Remove(IP_PROTOCOL_UDP, isOriPort ? sock->lport : sock->lport + portOffset); // Let's not remove mapping in real-time as it could cause lags/disconnection when joining a room with slow routers

				// Free Memory
				free(sock);

				// Free Translation Slot
				pdp[id - 1] = NULL;

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
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetAdhocId(%08x)", productStructAddr);
	
	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Arguments
		if (Memory::IsValidAddress(productStructAddr))
		{
			SceNetAdhocctlAdhocId * adhoc_id = (SceNetAdhocctlAdhocId *)Memory::GetPointer(productStructAddr);
			// Copy Product ID
			*adhoc_id = product_code;
			//Memory::WriteStruct(productStructAddr, &product_code);

			// Return Success
			return 0;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

static int sceNetAdhocctlScan() {
	INFO_LOG(SCENET, "sceNetAdhocctlScan() at %08x", currentMIPS->pc);

	// Library initialized
	if (netAdhocctlInited) {
		// Wait until Not connected
		if (friendFinderRunning) {
			int cnt = 0;
			while ((threadStatus != ADHOCCTL_STATE_DISCONNECTED) && (cnt < adhocDefaultTimeout)) {
				sleep_ms(1);
				cnt++;
			}
		}
		
		if (threadStatus == ADHOCCTL_STATE_DISCONNECTED) {
			threadStatus = ADHOCCTL_STATE_SCANNING;

			// Reset Networks/Group list to prevent other threads from using these soon to be replaced networks
			peerlock.lock();
			freeGroupsRecursive(networks);
			networks = NULL;
			peerlock.unlock();

			// Prepare Scan Request Packet
			uint8_t opcode = OPCODE_SCAN;

			// Send Scan Request Packet, may failed with socket error 10054/10053 if someone else with the same IP already connected to AdHoc Server (the server might need to be modified to differentiate MAC instead of IP)
			int iResult = send(metasocket, (char *)&opcode, 1, 0);
			if (iResult == SOCKET_ERROR) {
				int error = errno;
				ERROR_LOG(SCENET, "Socket error (%i) when sending", error);
				threadStatus = ADHOCCTL_STATE_DISCONNECTED;
				//if (error == ECONNABORTED || error == ECONNRESET || error == ENOTCONN) return ERROR_NET_ADHOCCTL_NOT_INITIALIZED; // A case where it need to reconnect to AdhocServer
				return ERROR_NET_ADHOCCTL_DISCONNECTED; // ERROR_NET_ADHOCCTL_BUSY 
			}

			// Does Connected Event's mipscall need be executed after returning from sceNetAdhocctlScan ?
			notifyAdhocctlHandlers(ADHOCCTL_EVENT_SCAN, 0);
			hleCheckCurrentCallbacks();

			// Wait for Status to be connected to prevent Ford Street Racing from Failed to find game session
			// TODO: Do this async while Delaying HLE Result
			if (friendFinderRunning) {
				int cnt = 0;
				while ((threadStatus == ADHOCCTL_STATE_SCANNING) && (cnt < adhocDefaultTimeout)) {
					sleep_ms(1);
					cnt++;
				}
			}

			//sceKernelDelayThread(adhocEventPollDelayMS * 1000);
			//hleDelayResult(0, "give time to init/cleanup", adhocEventPollDelayMS * 1000);

			// Return Success
			return 0;
		}

		// Library is busy
		return ERROR_NET_ADHOCCTL_BUSY; // ERROR_NET_ADHOCCTL_BUSY may trigger the game (ie. Ford Street Racing) to call sceNetAdhocctlDisconnect
	}

	// Library uninitialized
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

static int sceNetAdhocctlGetScanInfo(u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);
	SceNetAdhocctlScanInfoEmu *buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlScanInfoEmu *)Memory::GetPointer(bufAddr);

	INFO_LOG(SCENET, "sceNetAdhocctlGetScanInfo([%08x]=%i, %08x)", sizeAddr, /*buflen ? *buflen : -1*/Memory::Read_U32(sizeAddr), bufAddr);
	if (!g_Config.bEnableWlan) {
		return 0;
	}

	// Library initialized
	if (netAdhocctlInited) {
		// Minimum Argument
		if (buflen == NULL) return ERROR_NET_ADHOCCTL_INVALID_ARG;

		// Minimum Argument Requirements
		if (buflen != NULL) {
			// Multithreading Lock
			peerlock.lock();

			// Length Returner Mode
			if (buf == NULL) {
				int availNetworks = countAvailableNetworks();
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
					for (; group != NULL && discovered < requestcount; group = group->next) {
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
		// Connected State (Adhoc Mode)
		if (threadStatus != ADHOCCTL_STATE_DISCONNECTED) { // (threadStatus == ADHOCCTL_STATE_CONNECTED) 
			// Clear Network Name
			memset(&parameter.group_name, 0, sizeof(parameter.group_name));

			// Set HUD Connection Status
			//setConnectionStatus(0);

			// Prepare Packet
			uint8_t opcode = OPCODE_DISCONNECT;

			// Acquire Network Lock
			//_acquireNetworkLock();

			// Send Disconnect Request Packet
			int iResult = send(metasocket, (const char*)&opcode, 1, 0);
			if (iResult == SOCKET_ERROR) {
				ERROR_LOG(SCENET, "Socket error (%i) when sending", errno);
			}

			// Set Disconnected State
			threadStatus = ADHOCCTL_STATE_DISCONNECTED;

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

		// Notify Event Handlers (even if we weren't connected, not doing this will freeze games like God Eater, which expect this behaviour)
		notifyAdhocctlHandlers(ADHOCCTL_EVENT_DISCONNECT, 0);
		hleCheckCurrentCallbacks();

		// Return Success, some games might ignore returned value and always treat it as success, otherwise repeatedly calling this function
		return 0;
	}

	// Library uninitialized
	return 0; //ERROR_NET_ADHOC_NOT_INITIALIZED; // Wipeout Pulse will repeatedly calling this function if returned value is ERROR_NET_ADHOC_NOT_INITIALIZED
}

static u32 sceNetAdhocctlDisconnect() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	char grpName[9] = { 0 };
	memcpy(grpName, parameter.group_name.data, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	INFO_LOG(SCENET, "sceNetAdhocctlDisconnect() at %08x [group=%s]", currentMIPS->pc, grpName);

	return NetAdhocctl_Disconnect();
}

static u32 sceNetAdhocctlDelHandler(u32 handlerID) {
	if (adhocctlHandlers.find(handlerID) != adhocctlHandlers.end()) {
		adhocctlHandlers.erase(handlerID);
		WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlDelHandler(%d)", handlerID);
	} else {
		ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlDelHandler(%d): Invalid Handler ID", handlerID);
	}

	return 0;
}

int NetAdhocctl_Term() {
	if (netAdhocctlInited) {
		if (threadStatus != ADHOCCTL_STATE_DISCONNECTED) 
			NetAdhocctl_Disconnect();

		// Terminate Adhoc Threads
		friendFinderRunning = false;
		if (friendFinderThread.joinable()) {
			friendFinderThread.join();
		}

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
		shutdown(metasocket, SD_BOTH);
		closesocket(metasocket);
		metasocket = (int)INVALID_SOCKET;
		// Delete fake PSP Thread
		if (threadAdhocID != 0) {
			__KernelStopThread(threadAdhocID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocThread stopped");
			__KernelDeleteThread(threadAdhocID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocThread deleted");
			threadAdhocID = 0;
		}
		netAdhocctlInited = false;
	}

	return 0;
}

int sceNetAdhocctlTerm() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(SCENET, "sceNetAdhocctlTerm()");

	//if (netAdhocMatchingInited) NetAdhocMatching_Term();

	return NetAdhocctl_Term();
}

static int sceNetAdhocctlGetNameByAddr(const char *mac, u32 nameAddr) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocctlGetNameByAddr(%s, %08x)", mac2str((SceNetEtherAddr*)mac).c_str(), nameAddr);
	
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
			if (memcmp(&localmac, mac, sizeof(SceNetEtherAddr)) == 0)
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
				if (memcmp(&peer->mac_addr, mac, sizeof(SceNetEtherAddr)) == 0)
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

static int sceNetAdhocctlJoin(u32 scanInfoAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlJoin(%08x) at %08x", scanInfoAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	
	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Argument
		if (Memory::IsValidAddress(scanInfoAddr))
		{
			SceNetAdhocctlScanInfoEmu * sinfo = (SceNetAdhocctlScanInfoEmu *)Memory::GetPointer(scanInfoAddr);
			//while (true) sleep_ms(1);

			// We can ignore minor connection process differences here
			// TODO: Adhoc Server may need to be changed to differentiate between Host/Create and Join, otherwise it can't support multiple Host using the same Group name, thus causing one of the Host to be confused being treated as Join.
			return sceNetAdhocctlCreate((const char*)&sinfo->group_name);
		}

		// Invalid Argument
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Uninitialized Library
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
		
		int retval = ERROR_NET_ADHOCCTL_INVALID_ARG; // -1;

		// Local MAC
		if (isLocalMAC(maddr)) {
			sockaddr_in addr;
			SceNetAdhocctlNickname nickname;

			getLocalIp(&addr);
			strncpy((char*)&nickname.data, g_Config.sNickName.c_str(), ADHOCCTL_NICKNAME_LEN);
			nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // making sure to be null-terminated since strncpy doesn't implicitly append null
			//buf->next = 0;
			buf->nickname = nickname;
			buf->nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
			buf->mac_addr = *maddr;
			buf->ip_addr = addr.sin_addr.s_addr; // 0x11111111;
			buf->padding = 0;
			buf->last_recv = CoreTiming::GetGlobalTimeUsScaled(); 

			// Success
			retval = 0;
		}
		// Find Peer by MAC
		else 
		{
			// Multithreading Lock
			peerlock.lock();

			SceNetAdhocctlPeerInfo * peer = findFriend(maddr);
			if (peer != NULL) {
				// Fake Receive Time
				if (peer->last_recv != 0) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();

				//buf->next = 0;
				buf->nickname = peer->nickname;
				buf->nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
				buf->mac_addr = *maddr;
				buf->ip_addr = peer->ip_addr; // 0x11111111;
				buf->padding = 0;
				buf->last_recv = peer->last_recv; //CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0; //(uint64_t)time(NULL); //This timestamp is important issue on Dissidia 012

				// Success
				retval = 0;
			}

			// Multithreading Unlock
			peerlock.unlock();
		}
		return retval;
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
	char grpName[9] = { 0 };
	memcpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	INFO_LOG(SCENET, "sceNetAdhocctlCreate(%s) at %08x", grpName, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	const SceNetAdhocctlGroupName * groupNameStruct = (const SceNetAdhocctlGroupName *)groupName;
	// Library initialized
	if (netAdhocctlInited) {
		// Valid Argument
		if (validNetworkName(groupNameStruct)) {
			// Wait until Not connected
			if (friendFinderRunning) {
				int cnt = 0;
				while ((threadStatus != ADHOCCTL_STATE_DISCONNECTED && threadStatus != ADHOCCTL_STATE_SCANNING) && (cnt < adhocDefaultTimeout)) {
					sleep_ms(1);
					cnt++;
				}
			}

			// Disconnected State, may also need to check for Scanning state to prevent some games from failing to host a game session
			if ((threadStatus == ADHOCCTL_STATE_DISCONNECTED) || (threadStatus == ADHOCCTL_STATE_SCANNING)) {
				// Set Network Name
				if (groupNameStruct != NULL) parameter.group_name = *groupNameStruct;

				// Reset Network Name
				else memset(&parameter.group_name, 0, sizeof(parameter.group_name));

				// Prepare Connect Packet
				SceNetAdhocctlConnectPacketC2S packet;

				// Clear Packet Memory
				memset(&packet, 0, sizeof(packet));

				// Set Packet Opcode
				packet.base.opcode = OPCODE_CONNECT;

				// Set Target Group
				if (groupNameStruct != NULL) packet.group = *groupNameStruct;

				// Acquire Network Lock

				// Send Packet
				int iResult = send(metasocket, (const char *)&packet, sizeof(packet), 0);
				if (iResult == SOCKET_ERROR) {
					ERROR_LOG(SCENET, "Socket error (%i) when sending", errno);
					//return ERROR_NET_ADHOCCTL_NOT_INITIALIZED; // ERROR_NET_ADHOCCTL_DISCONNECTED; // ERROR_NET_ADHOCCTL_BUSY;
					//Faking success, to prevent Full Auto 2 from freezing while Initializing Network
					threadStatus = ADHOCCTL_STATE_CONNECTED;
					// Notify Event Handlers, Needed for the Nickname to be shown on the screen when success is faked
					// Might be better not to notify the game when faking success (failed to connect to adhoc server), at least the player will know that it failed to connect 
					//__UpdateAdhocctlHandlers(ADHOCCTL_EVENT_CONNECT, 0); //CoreTiming::ScheduleEvent_Threadsafe_Immediate(eventAdhocctlHandlerUpdate, join32(ADHOCCTL_EVENT_CONNECT, 0)); 
				}

				// Free Network Lock

				// Set HUD Connection Status
				//setConnectionStatus(1);

				// Connected Event's mipscall need be executed before returning from sceNetAdhocctlCreate (or before the next sceNet function?)
				notifyAdhocctlHandlers(ADHOCCTL_EVENT_CONNECT, 0);

				// Wait for Status to be connected to prevent Ford Street Racing from Failed to create game session
				// TODO: Do this async while Delaying HLE Result
				if (friendFinderRunning) { // This is thread-unsafe
					int cnt = 0;
					while ((threadStatus != ADHOCCTL_STATE_CONNECTED) && (cnt < adhocDefaultTimeout)) { // This is thread-unsafe
						sleep_ms(1);
						cnt++;
					}
				}

				//sceKernelDelayThreadCB(adhocEventDelayMS * 1000);
				//hleCheckCurrentCallbacks();
				hleDelayResult(0, "give time to init/cleanup", adhocEventDelayMS * 1000);

				// Return Success
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

static int sceNetAdhocctlConnect(u32 ptrToGroupName) {
	if (Memory::IsValidAddress(ptrToGroupName)) {
		const char* groupName = Memory::GetCharPointer(ptrToGroupName);
		char grpName[9] = { 0 };
		memcpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs

		INFO_LOG(SCENET, "sceNetAdhocctlConnect(groupName=%s) at %08x", grpName, currentMIPS->pc);
		return sceNetAdhocctlCreate(groupName);
	} 
		
	return ERROR_NET_ADHOC_INVALID_ARG; // ERROR_NET_ADHOC_INVALID_ADDR;
}

// Connect to the Adhoc control game mode (as a Host)
static int sceNetAdhocctlCreateEnterGameMode(const char *groupName, int unknown, int playerNum, u32 macsAddr, int timeout, int unknown2) {
	char grpName[9] = { 0 };
	memcpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs

	SceNetEtherAddr* addrs = NULL; // List of participating MAC addresses (started from host)
	if (Memory::IsValidAddress(macsAddr)) {
		addrs = PSPPointer<SceNetEtherAddr>::Create(macsAddr);
	}

	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlCreateEnterGameMode(%s, %i, %i, %08x, %i, %i) at %08x", grpName, unknown, playerNum, macsAddr, timeout, unknown2, currentMIPS->pc);
	
	return 0;
}

// Connect to the Adhoc control game mode (as a Client)
static int sceNetAdhocctlJoinEnterGameMode(const char *groupName, const char *macAddr, int timeout, int unknown2) {
	char grpName[9] = { 0 };
	memcpy(grpName, groupName, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs

	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlJoinEnterGameMode(%s, %s, %i, %i) at %08x", grpName, mac2str((SceNetEtherAddr*)macAddr).c_str(), timeout, unknown2, currentMIPS->pc);

	return 0;
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
int sceNetAdhocctlCreateEnterGameModeMin(const char *group_name, int game_type, int min_members, int num_members, u32 membersAddr, u32 timeout, int flag)
{
	char grpName[9] = { 0 };
	memcpy(grpName, group_name, ADHOCCTL_GROUPNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlCreateEnterGameModeMin(%s, %i, %i, %i, %08x, %d, %i) at %08x", grpName, game_type, min_members, num_members, membersAddr, timeout, flag, currentMIPS->pc);
	// We don't really need the Minimum User Check
	return sceNetAdhocctlCreateEnterGameMode(group_name, game_type, num_members, membersAddr, timeout, flag); //0;
}

int NetAdhoc_Term() {
	// Since Adhocctl & AdhocMatching uses Sockets & Threads we should terminate them also to release their resources
	if (netAdhocMatchingInited) 
		NetAdhocMatching_Term();
	if (netAdhocctlInited)
		NetAdhocctl_Term();

	// Library is initialized
	if (netAdhocInited) {
		// Delete PDP Sockets
		deleteAllPDP();

		// Delete PTP Sockets
		deleteAllPTP();

		// Delete Gamemode Buffer
		//deleteAllGMB();

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

	return hleLogSuccessInfoI(SCENET, NetAdhoc_Term());
}

static int sceNetAdhocGetPdpStat(u32 structSize, u32 structAddr) {
	VERBOSE_LOG(SCENET, "UNTESTED sceNetAdhocGetPdpStat(%08x, %08x) at %08x", structSize, structAddr, currentMIPS->pc);
	
	// Library is initialized
	if (netAdhocInited)
	{
		s32_le *buflen = NULL;
		if (Memory::IsValidAddress(structSize)) buflen = (s32_le *)Memory::GetPointer(structSize);
		SceNetAdhocPdpStat *buf = NULL;
		if (Memory::IsValidAddress(structAddr)) buf = (SceNetAdhocPdpStat *)Memory::GetPointer(structAddr);

		// Length Returner Mode
		if (buflen != NULL && buf == NULL)
		{
			// Return Required Size
			*buflen = sizeof(SceNetAdhocPdpStat) * getPDPSocketCount();

			// Success
			return 0;
		}

		// Status Returner Mode
		else if (buflen != NULL && buf != NULL)
		{
			// Socket Count
			int socketcount = getPDPSocketCount();

			// Figure out how many Sockets we will return
			int count = *buflen / sizeof(SceNetAdhocPdpStat);
			if (count > socketcount) count = socketcount;

			// Copy Counter
			int i = 0;

			// Iterate Translation Table
			for (int j = 0; j < 255 && i < count; j++)
			{
				// Valid Socket Entry
				if (pdp[j] != NULL)
				{
					// Copy Socket Data from Internal Memory
					buf[i] = *pdp[j];

					// Fix Client View Socket ID
					buf[i].id = j + 1;

					// Write End of List Reference
					buf[i].next = 0;

					// Link Previous Element
					if (i > 0) 
						buf[i - 1].next = structAddr + ((i - 1LL) * sizeof(SceNetAdhocPdpStat)) + sizeof(SceNetAdhocPdpStat);

					// Increment Counter
					i++;
				}
			}

			// Update Buffer Length
			*buflen = i * sizeof(SceNetAdhocPdpStat);

			// Success
			return 0;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOC_INVALID_ARG;
	}

	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
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
		// Length Returner Mode
		if (buflen != NULL && buf == NULL) {
			// Return Required Size
			*buflen = sizeof(SceNetAdhocPtpStat) * getPTPSocketCount();
			
			// Success
			return 0;
		}
		
		// Status Returner Mode
		else if (buflen != NULL && buf != NULL) {
			// Socket Count
			int socketcount = getPTPSocketCount();
			
			// Figure out how many Sockets we will return
			int count = *buflen / sizeof(SceNetAdhocPtpStat);
			if (count > socketcount) count = socketcount;
			
			// Copy Counter
			int i = 0;
			
			// Iterate Sockets
			for (int j = 0; j < 255 && i < count; j++) {
				// Active Socket
				if (ptp[j] != NULL) {
					// Copy Socket Data from internal Memory
					buf[i] = *ptp[j];
					
					// Fix Client View Socket ID
					buf[i].id = j + 1;
					
					// Write End of List Reference
					buf[i].next = 0;
					
					// Link previous Element to this one
					if (i > 0)
						buf[i - 1].next = structAddr + ((i - 1LL) * sizeof(SceNetAdhocPtpStat)) + sizeof(SceNetAdhocPtpStat);
					
					// Increment Counter
					i++;
				}
			}
			
			// Update Buffer Length
			*buflen = i * sizeof(SceNetAdhocPtpStat);
			
			// Success
			return 0;
		}
		
		// Invalid Arguments
		return ERROR_NET_ADHOC_INVALID_ARG;
	}
	
	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
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
static int sceNetAdhocPtpOpen(const char *srcmac, int sport, const char *dstmac, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int unknown) {
	INFO_LOG(SCENET, "sceNetAdhocPtpOpen(%s, %d, %s, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport, mac2str((SceNetEtherAddr*)dstmac).c_str(),dport,bufsize, rexmt_int, rexmt_cnt, unknown, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return 0;
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
		// Valid Addresses
		if (saddr != NULL && isLocalMAC(saddr) && daddr != NULL && !isBroadcastMAC(daddr)) {
			// Random Port required
			if (sport == 0) {
				isClient = true;
				// Find unused Port
				// while (sport == 0 || _IsPTPPortInUse(sport)) {
				// 	// Generate Port Number
				// 	sport = (uint16_t)_getRandomNumber(65535);
				// }
			}
			
			// Valid Ports
			if (!isPTPPortInUse(sport) /*&& dport != 0*/) {
				//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
				if (sport == 0) sport = -(int)portOffset;
				// Valid Arguments
				if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0) {
					// Create Infrastructure Socket
					int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
					
					// Valid Socket produced
					if (tcpsocket > 0) {
						// Change socket buffer size when necessary
						if (getSockBufferSize(tcpsocket, SO_SNDBUF) < bufsize) setSockBufferSize(tcpsocket, SO_SNDBUF, bufsize);
						if (getSockBufferSize(tcpsocket, SO_RCVBUF) < bufsize) setSockBufferSize(tcpsocket, SO_RCVBUF, bufsize);

						// Enable KeepAlive
						setSockKeepAlive(tcpsocket, true, rexmt_int / 1000000L, rexmt_cnt);

						// Enable Port Re-use
						setsockopt(tcpsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

						// Apply Default Send Timeout Settings to Socket
						setSockTimeout(tcpsocket, SO_SNDTIMEO, rexmt_int);

						// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
						if (g_Config.bTCPNoDelay) setSockNoDelay(tcpsocket, 1);
						
						// Binding Information for local Port
						sockaddr_in addr;
						// addr.sin_len = sizeof(addr);
						addr.sin_family = AF_INET;
						addr.sin_addr.s_addr = INADDR_ANY;
						if (isLocalServer) {
							getLocalIp(&addr);
						}
						addr.sin_port = htons(sport + portOffset);
						
						// Bound Socket to local Port
						if (bind(tcpsocket, (sockaddr *)&addr, sizeof(addr)) == 0) {
							// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
							socklen_t len = sizeof(addr);
							if (getsockname(tcpsocket, (sockaddr *)&addr, &len) == 0) {
								sport = ntohs(addr.sin_port) - portOffset;
							}
							
							// Allocate Memory
							SceNetAdhocPtpStat * internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));
							
							// Allocated Memory
							if (internal != NULL) {
								// Find Free Translator ID
								int i = 0; 
								for (; i < 255; i++) if (ptp[i] == NULL) break;
								
								// Found Free Translator ID
								if (i < 255) {
									// Clear Memory
									memset(internal, 0, sizeof(SceNetAdhocPtpStat));
									
									// Copy Infrastructure Socket ID
									internal->id = tcpsocket;
									
									// Copy Address Information
									internal->laddr = *saddr;
									internal->paddr = *daddr;
									internal->lport = sport;
									internal->pport = dport;
									
									// Set Buffer Size
									internal->rcv_sb_cc = bufsize;
									
									// Link PTP Socket
									ptp[i] = internal;
									
									// Add Port Forward to Router. We may not even need to forward this local port, since PtpOpen usually have port 0 (any port) as source port and followed by PtpConnect (which mean acting as Client), right?
									//sceNetPortOpen("TCP", sport);
									if (!isClient)
										UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset); // g_PortManager.Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset);
									
									// Return PTP Socket Pointer
									return i + 1;
								}
								
								// Free Memory
								free(internal);
							}
						}
						else {
							ERROR_LOG(SCENET, "Socket error (%i) when binding port %u", errno, ntohs(addr.sin_port));
							auto n = GetI18NCategory("Networking");
							host->NotifyUserMessage(std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 3.0, 0x0000ff);
						}
						
						// Close Socket
						closesocket(tcpsocket);

						// Port not available (exclusively in use?)
						return ERROR_NET_ADHOC_PORT_IN_USE; // ERROR_NET_ADHOC_PORT_NOT_AVAIL;
					}
				}
				
				// Invalid Arguments
				return ERROR_NET_ADHOC_INVALID_ARG;
			}
			
			// Invalid Ports
			return ERROR_NET_ADHOC_PORT_IN_USE; // ERROR_NET_ADHOC_INVALID_PORT;
		}
		
		// Invalid Addresses
		return ERROR_NET_ADHOC_INVALID_ARG; // ERROR_NET_ADHOC_INVALID_ADDR;
	}
	
	return 0;
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
		return 0;
	}

	// Library is initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPtpStat * socket = ptp[id - 1];
			
			// Listener Socket
			if (socket->state == ADHOC_PTP_STATE_LISTEN) {
				// Valid Arguments
				if (addr != NULL /*&& port != NULL*/) { //GTA:VCS seems to use 0 for the portPtr
					// Address Information
					sockaddr_in peeraddr;
					memset(&peeraddr, 0, sizeof(peeraddr));
					socklen_t peeraddrlen = sizeof(peeraddr);
					// Local Address Information
					sockaddr_in local;
					memset(&local, 0, sizeof(local));
					socklen_t locallen = sizeof(local);
					
					// Grab Nonblocking Flag
					uint32_t nbio = getBlockingFlag(socket->id);
					// Switch to Nonblocking Behaviour
					if (nbio == 0) {
						// Overwrite Socket Option
						changeBlockingMode(socket->id, 1);
					}
					
					// TODO: Use a different thread (similar to sceIo) for recvfrom, recv & accept to prevent blocking-socket from blocking emulation
					// Accept Connection
					int newsocket = accept(socket->id, (sockaddr *)&peeraddr, &peeraddrlen);
					int error = errno;
					
					// Blocking Behaviour
					if (!flag && newsocket == SOCKET_ERROR) {
						// Get Start Time
						uint32_t starttime = (uint32_t)(real_time_now()*1000000.0);
						
						// Retry until Timeout hits
						while ((timeout == 0 || ((uint32_t)(real_time_now()*1000000.0) - starttime) < (uint32_t)timeout) && (newsocket == SOCKET_ERROR) && connectInProgress(error)) {
							// Accept Connection
							newsocket = accept(socket->id, (sockaddr *)&peeraddr, &peeraddrlen);
							error = errno;
							
							// Wait a bit...
							sleep_ms(1);
						}
					}

					if (newsocket == SOCKET_ERROR) {
						if (flag == 0) { // Prevent spamming Debug Log with retries of non-bocking socket
							DEBUG_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", id, error);
						}
						else {
							VERBOSE_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", id, error);
						}
					}
					
					// Restore Blocking Behaviour
					if (nbio == 0) {
						// Restore Socket Option
						changeBlockingMode(socket->id, 0);
					}
					
					// Accepted New Connection
					if (newsocket > 0) {
						// Enable Port Re-use
						setsockopt(newsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

						// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
						if (g_Config.bTCPNoDelay) setSockNoDelay(newsocket, 1);
						
						// Grab Local Address
						if (getsockname(newsocket, (sockaddr *)&local, &locallen) == 0) {
							// Peer MAC
							SceNetEtherAddr mac;
							
							// Find Peer MAC
							if (resolveIP(peeraddr.sin_addr.s_addr, &mac)) {
								// Allocate Memory
								SceNetAdhocPtpStat * internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));
								
								// Allocated Memory
								if (internal != NULL) {
									// Find Free Translator ID
									int i = 0; 
									for (; i < 255; i++) if (ptp[i] == NULL) break;
									
									// Found Free Translator ID
									if (i < 255) {
										// Clear Memory
										memset(internal, 0, sizeof(SceNetAdhocPtpStat));
										
										// Copy Socket Descriptor to Structure
										internal->id = newsocket;

										// Set Buffer Size
										if (getSockBufferSize(newsocket, SO_RCVBUF) < socket->rcv_sb_cc) setSockBufferSize(newsocket, SO_RCVBUF, socket->rcv_sb_cc);
										if (getSockBufferSize(newsocket, SO_SNDBUF) < socket->snd_sb_cc) setSockBufferSize(newsocket, SO_SNDBUF, socket->snd_sb_cc);
										internal->rcv_sb_cc = socket->rcv_sb_cc;
										internal->snd_sb_cc = socket->snd_sb_cc;
										
										// Copy Local Address Data to Structure
										getLocalMac(&internal->laddr);
										internal->lport = ntohs(local.sin_port) - portOffset;
										
										// Copy Peer Address Data to Structure
										internal->paddr = mac;
										internal->pport = ntohs(peeraddr.sin_port) - portOffset;
										
										// Set Connected State
										internal->state = ADHOC_PTP_STATE_ESTABLISHED;
										
										// Return Peer Address Information
										*addr = internal->paddr;
										if (port != NULL) *port = internal->pport;
										
										// Link PTP Socket
										ptp[i] = internal;
										
										// Add Port Forward to Router. Or may be doesn't need to be forwarded since local port already accessible from outside if others were able to connect & get accepted at this point, right?
										//sceNetPortOpen("TCP", internal->lport);
										//g_PortManager.Add(IP_PROTOCOL_TCP, internal->lport + portOffset);

										INFO_LOG(SCENET, "sceNetAdhocPtpAccept[%i->%i:%u]: Established (%s:%u)", id, i+1, internal->lport, inet_ntoa(peeraddr.sin_addr), internal->pport);
										
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

						ERROR_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Failed (Socket Closed)", id);
					}
					
					// Action would block
					if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;
					
					// Timeout
					return ERROR_NET_ADHOC_TIMEOUT;
				}
				
				// Invalid Arguments
				return ERROR_NET_ADHOC_INVALID_ARG;
			}
			
			// Client Socket
			return ERROR_NET_ADHOC_NOT_LISTENED;
		}
		
		// Invalid Socket
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
	}
	
	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
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
		return 0;
	}

	// Library is initialized
	if (netAdhocInited)
	{
		// Valid Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPtpStat * socket = ptp[id - 1];

			// Phantasy Star Portable 2 will try to reconnect even when previous connect already success, so we should return success too if it's already connected
			if (socket->state == ADHOC_PTP_STATE_ESTABLISHED)
				return 0;

			// Valid Client Socket
			if (socket->state == ADHOC_PTP_STATE_CLOSED) {
				// Target Address
				sockaddr_in sin;
				memset(&sin, 0, sizeof(sin));
				
				// Setup Target Address
				// sin.sin_len = sizeof(sin);
				sin.sin_family = AF_INET;
				sin.sin_port = htons(socket->pport + portOffset);
				
				// Grab Peer IP
				if (resolveMAC(&socket->paddr, (uint32_t *)&sin.sin_addr.s_addr)) {
					// Some games (ie. PSP2) might try to talk to it's self, not sure if they talked through WAN or LAN when using public Adhoc Server tho
					sin.sin_port = htons(socket->pport + ((isOriPort && !isPrivateIP(sin.sin_addr.s_addr)) ? 0 : portOffset));

					// Grab Nonblocking Flag
					uint32_t nbio = getBlockingFlag(socket->id);

					// Switch to Nonblocking Behaviour. Forcing blocking behaviour on the first connect may fix connection issue on GvG Next Plus, But i don't like using blocking socket with infinite timeout if the game it self were asking for non-blocking behaviour :(
					/*if (nbio == 0) 
					{
						// Overwrite Socket Option
						changeBlockingMode(socket->id, 1);
					}*/
					
					// Connect Socket to Peer (Nonblocking)
					// NOTE: Based on what i read at stackoverflow, The First Non-blocking POSIX connect will always returns EAGAIN/EWOULDBLOCK because it returns without waiting for ACK/handshake, But GvG Next Plus is treating non-blocking PtpConnect just like blocking connect, May be on a real PSP the first non-blocking sceNetAdhocPtpConnect can be successfull?
					int connectresult = connect(socket->id, (sockaddr *)&sin, sizeof(sin));
					
					// Grab Error Code
					int errorcode = errno;

					if (connectresult == SOCKET_ERROR) {
						ERROR_LOG(SCENET, "sceNetAdhocPtpConnect[%i]: Socket Error (%i) to %s:%u", id, errorcode, inet_ntoa(sin.sin_addr), socket->pport);
					}
					
					// Restore Blocking Behaviour
					if (nbio == 0) 
					{
						// Restore Socket Option
						changeBlockingMode(socket->id, 0);
					}
					
					// Instant Connection (Lucky!)
					if (connectresult == 0 || (connectresult == SOCKET_ERROR && (errorcode == EISCONN))) {
						// Set Connected State
						socket->state = ADHOC_PTP_STATE_ESTABLISHED;
						
						INFO_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Already Connected to %s:%u", id, socket->lport, inet_ntoa(sin.sin_addr), socket->pport);
						// Success
						return 0;
					}
					
					// Connection in Progress
					else if (connectresult == SOCKET_ERROR && connectInProgress(errorcode)) {
						// Nonblocking Mode
						if (flag) {
							//if (errorcode == EALREADY) return ERROR_NET_ADHOC_BUSY;
							return ERROR_NET_ADHOC_WOULD_BLOCK;
						}
						// Blocking Mode
						else {
							// Grab Connection Start Time
							uint32_t starttime = (uint32_t)(real_time_now()*1000000.0);
							
							// Peer Information (for Connection-Polling)
							sockaddr_in peer;
							memset(&peer, 0, sizeof(peer));
							socklen_t peerlen = sizeof(peer);
							// Wait for Connection
							while ((timeout == 0 || ( (uint32_t)(real_time_now()*1000000.0) - starttime) < (uint32_t)timeout) && getpeername(socket->id, (sockaddr *)&peer, &peerlen) != 0) {
								// Wait 1ms
								sleep_ms(1);
							}
							
							// Connected in Time
							if (sin.sin_addr.s_addr == peer.sin_addr.s_addr/* && sin.sin_port == peer.sin_port*/) {
								// Set Connected State
								socket->state = ADHOC_PTP_STATE_ESTABLISHED;

								INFO_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Established (%s:%u)", id, socket->lport, inet_ntoa(peer.sin_addr), socket->pport);
								
								// Success
								return 0;
							}
							
							// Timeout occured
							return ERROR_NET_ADHOC_CONNECTION_REFUSED; // ERROR_NET_ADHOC_TIMEOUT;
						}
					}
				}
				
				// Peer not found
				if (flag)
					return ERROR_NET_ADHOC_WOULD_BLOCK;
				return ERROR_NET_ADHOC_CONNECTION_REFUSED; // ERROR_NET_ADHOC_TIMEOUT;
			}
			
			// Not a valid Client Socket
			return ERROR_NET_ADHOC_NOT_OPENED;
		}
		
		// Invalid Socket
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
	}
	
	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int NetAdhocPtp_Close(int id, int unknown) {
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments & Atleast one Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPtpStat* socket = ptp[id - 1];

			// Close Connection
			closesocket(socket->id);

			// Remove Port Forward from Router
			//sceNetPortClose("TCP", socket->lport);
			//g_PortManager.Remove(IP_PROTOCOL_TCP, isOriPort ? socket->lport : socket->lport + portOffset); // Let's not remove mapping in real-time as it could cause lags/disconnection when joining a room with slow routers

			// Free Memory
			free(socket);

			// Free Reference
			ptp[id - 1] = NULL;

			// Success
			return 0;
		}

		// Invalid Argument
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
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
static int sceNetAdhocPtpListen(const char *srcmac, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, int unk) {
	INFO_LOG(SCENET, "sceNetAdhocPtpListen(%s, %d, %d, %d, %d, %d, %d) at %08x", mac2str((SceNetEtherAddr*)srcmac).c_str(), sport,bufsize,rexmt_int,rexmt_cnt,backlog,unk, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)srcmac;
	if (netAdhocInited) {
		// Some games (ie. DBZ Shin Budokai 2) might be getting the saddr/srcmac content from SaveState and causing problems :( So we try to fix it here
		if (saddr != NULL) {
			getLocalMac(saddr);
		}
		// Valid Address
		if (saddr != NULL && isLocalMAC(saddr)) {
			// Random Port required
			if (sport == 0) {
				// Find unused Port
				// while (sport == 0 || __IsPTPPortInUse(sport))
				// {
				// 	// Generate Port Number
				// 	sport = (uint16_t)_getRandomNumber(65535);
				// }
			}
			
			// Valid Ports
			if (!isPTPPortInUse(sport)) {
				//sport 0 should be shifted back to 0 when using offset Phantasy Star Portable 2 use this
				if (sport == 0) sport = -(int)portOffset;
				// Valid Arguments
				if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0 && backlog > 0)
				{
					// Create Infrastructure Socket
					int tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
					
					// Valid Socket produced
					if (tcpsocket > 0) {
						// Change socket buffer size when necessary
						if (getSockBufferSize(tcpsocket, SO_SNDBUF) < bufsize) setSockBufferSize(tcpsocket, SO_SNDBUF, bufsize);
						if (getSockBufferSize(tcpsocket, SO_RCVBUF) < bufsize) setSockBufferSize(tcpsocket, SO_RCVBUF, bufsize);

						// Enable KeepAlive
						setSockKeepAlive(tcpsocket, true, rexmt_int / 1000000L, rexmt_cnt);

						// Enable Port Re-use
						setsockopt(tcpsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

						// Apply Default Receive Timeout Settings to Socket
						setSockTimeout(tcpsocket, SO_RCVTIMEO, rexmt_int);

						// Disable Nagle Algo to send immediately. Or may be we shouldn't disable Nagle since there is PtpFlush function?
						if (g_Config.bTCPNoDelay) setSockNoDelay(tcpsocket, 1);
						
						// Binding Information for local Port
						sockaddr_in addr;
						addr.sin_family = AF_INET;
						addr.sin_addr.s_addr = INADDR_ANY;
						if (isLocalServer) {
							getLocalIp(&addr);
						}
						addr.sin_port = htons(sport + portOffset);
						
						int iResult = 0;
						// Bound Socket to local Port
						if ((iResult = bind(tcpsocket, (sockaddr *)&addr, sizeof(addr))) == 0) {
							// Update sport with the port assigned internal->lport = ntohs(local.sin_port)
							socklen_t len = sizeof(addr);
							if (getsockname(tcpsocket, (sockaddr*)&addr, &len) == 0) {
								sport = ntohs(addr.sin_port) - portOffset;
							}
							// Switch into Listening Mode
							if ((iResult = listen(tcpsocket, backlog)) == 0) {
								// Allocate Memory
								SceNetAdhocPtpStat * internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));
								
								// Allocated Memory
								if (internal != NULL) {
									// Find Free Translator ID
									int i = 0; 
									for (; i < 255; i++) if (ptp[i] == NULL) break;
									
									// Found Free Translator ID
									if (i < 255) {
										// Clear Memory
										memset(internal, 0, sizeof(SceNetAdhocPtpStat));
										
										// Copy Infrastructure Socket ID
										internal->id = tcpsocket;
										
										// Copy Address Information
										internal->laddr = *saddr;
										internal->lport = sport;
										
										// Flag Socket as Listener
										internal->state = ADHOC_PTP_STATE_LISTEN;
										
										// Set Buffer Size
										internal->rcv_sb_cc = bufsize;
										
										// Link PTP Socket
										ptp[i] = internal;
										
										// Add Port Forward to Router
										//sceNetPortOpen("TCP", sport);
										UPnP_Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset); // g_PortManager.Add(IP_PROTOCOL_TCP, isOriPort ? sport : sport + portOffset, sport + portOffset);
										
										// Return PTP Socket Pointer
										return i + 1;
									}
									
									// Free Memory
									free(internal);
								}
							}
						}
						else {
							auto n = GetI18NCategory("Networking");
							host->NotifyUserMessage(std::string(n->T("Failed to Bind Port")) + " " + std::to_string(sport + portOffset) + "\n" + std::string(n->T("Please change your Port Offset")), 3.0, 0x0000ff);
						}
						
						if (iResult == SOCKET_ERROR) {
							int error = errno;
							ERROR_LOG(SCENET, "sceNetAdhocPtpListen[%i]: Socket Error (%i)", sport, error);
						}

						// Close Socket
						closesocket(tcpsocket);

						// Port not available (exclusively in use?)
						return ERROR_NET_ADHOC_PORT_IN_USE; // ERROR_NET_ADHOC_PORT_NOT_AVAIL;
					}
					
					// Socket not available
					return ERROR_NET_ADHOC_SOCKET_ID_NOT_AVAIL;
				}
				
				// Invalid Arguments
				return ERROR_NET_ADHOC_INVALID_ARG;
			}
			
			// Invalid Ports
			return ERROR_NET_ADHOC_PORT_IN_USE;
		}
		
		// Invalid Addresses
		return ERROR_NET_ADHOC_INVALID_ADDR;
	}
	
	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
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
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	int * len = (int *)Memory::GetPointer(dataSizeAddr);
	const char * data = Memory::GetCharPointer(dataAddr);
	// Library is initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPtpStat * socket = ptp[id - 1];
			
			// Connected Socket
			if (socket->state == ADHOC_PTP_STATE_ESTABLISHED) {
				// Valid Arguments
				if (data != NULL && len != NULL && *len > 0) {
					// Schedule Timeout Removal
					//if (flag) timeout = 0; // JPCSP seems to always Send PTP as blocking, also a possibility to send to multiple destination?
					
					// Apply Send Timeout Settings to Socket
					if (timeout > 0) 
						setSockTimeout(socket->id, SO_SNDTIMEO, timeout);
					
					// Acquire Network Lock
					// _acquireNetworkLock();
					
					// Send Data
					//changeBlockingMode(socket->id, flag);
					int sent = send(socket->id, data, *len, 0);
					int error = errno;
					changeBlockingMode(socket->id, 0);
					
					// Free Network Lock
					// _freeNetworkLock();
					
					// Success
					if (sent > 0) {
						// Save Length
						*len = sent;

						DEBUG_LOG(SCENET, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %s:%u", id, socket->lport, sent, mac2str(&socket->paddr).c_str(), socket->pport);
						
						// Return Success
						return 0;
					}
					
					// Non-Critical Error
					else if (sent == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || error == ETIMEDOUT)) {
						// Blocking Situation
						if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;
						
						// Timeout
						return ERROR_NET_ADHOC_TIMEOUT;
					}
					
					// Change Socket State
					socket->state = ADHOC_PTP_STATE_CLOSED;
					
					// Disconnected
					return ERROR_NET_ADHOC_DISCONNECTED;
				}
				
				// Invalid Arguments
				return ERROR_NET_ADHOC_INVALID_ARG;
			}
			
			// Not connected
			return ERROR_NET_ADHOC_NOT_CONNECTED;
		}
		
		// Invalid Socket
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
	}
	
	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
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
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	void * buf = (void *)Memory::GetPointer(dataAddr);
	int * len = (int *)Memory::GetPointer(dataSizeAddr);
	// Library is initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL && ptp[id - 1]->state == ADHOC_PTP_STATE_ESTABLISHED) {
			// Cast Socket
			SceNetAdhocPtpStat * socket = ptp[id - 1];
			
			// Valid Arguments
			if (buf != NULL && len != NULL && *len > 0) {
				// Schedule Timeout Removal
				//if (flag) timeout = 0;
				
				// Apply Receive Timeout Settings to Socket. Let's not wait forever (0 = indefinitely)
				if (timeout > 0) 
					setSockTimeout(socket->id, SO_RCVTIMEO, timeout);
				
				// Acquire Network Lock
				// _acquireNetworkLock();
				
				// TODO: Use a different thread (similar to sceIo) for recvfrom, recv & accept to prevent blocking-socket from blocking emulation
				int received = 0;
				int error = 0;

				// Receive Data
				changeBlockingMode(socket->id, 1);
				received = recv(socket->id, (char*)buf, *len, 0);
				error = errno;

				if (flag == 0) {
					// Simulate blocking behaviour with non-blocking socket
					uint32_t starttime = (uint32_t)(real_time_now() * 1000000.0);
					// Wait for Connection
					while ((timeout == 0 || ((uint32_t)(real_time_now() * 1000000.0) - starttime) < (uint32_t)timeout) && (received == SOCKET_ERROR) && connectInProgress(error)) {
						received = recv(socket->id, (char*)buf, *len, 0);
						error = errno;
						// Wait 1ms
						sleep_ms(1);
					}
				}

				if (received == SOCKET_ERROR) {
					VERBOSE_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPtpRecv[%i:%u] [size=%i]", error, id, socket->lport, *len);
				}
				changeBlockingMode(socket->id, 0);
				
				// Free Network Lock
				// _freeNetworkLock();
				
				// Received Data
				if (received > 0) {
					// Save Length
					*len = received;

					// Update last recv timestamp, may cause disconnection not detected properly tho
					peerlock.lock();
					auto peer = findFriend(&socket->paddr);
					if (peer != NULL) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
					peerlock.unlock();

					DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %s:%u", id, socket->lport, received, mac2str(&socket->paddr).c_str(), socket->pport);
					
					// Return Success
					return 0;
				}
				
				// Non-Critical Error
				else if (received == SOCKET_ERROR && (error == EAGAIN || error == EWOULDBLOCK || error == ETIMEDOUT)) {
					// Blocking Situation
					if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;
					
					// Timeout
					return ERROR_NET_ADHOC_TIMEOUT;
				}
				
				// Change Socket State
				socket->state = ADHOC_PTP_STATE_CLOSED;
				
				// Disconnected
				return ERROR_NET_ADHOC_DISCONNECTED;
			}
			
			// Invalid Arguments
			return ERROR_NET_ADHOC_INVALID_ARG;
		}
		
		// Invalid Socket
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
	}
	
	// Library is uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
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
	if (!g_Config.bEnableWlan) {
		return 0;
	}

	// Library initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPtpStat* socket = ptp[id - 1];

			// Connected Socket
			if (socket->state == ADHOC_PTP_STATE_ESTABLISHED) {
				// There are two ways to flush, you can either set TCP_NODELAY to 1 or TCP_CORK to 0.
				// Apply Send Timeout Settings to Socket
				setSockTimeout(socket->id, SO_SNDTIMEO, timeout);

				// Get original Nagle algo value
				int n = getSockNoDelay(socket->id);

				// Disable Nagle Algo to send immediately
				setSockNoDelay(socket->id, 1);

				// Send Empty Data just to trigger Nagle on/off effect to flush the send buffer, Do we need to trigger this at all or is it automatically flushed?
				changeBlockingMode(socket->id, nonblock);
				int sent = send(socket->id, 0, 0, 0);
				int error = errno;
				changeBlockingMode(socket->id, 0);

				// Restore/Enable Nagle Algo
				setSockNoDelay(socket->id, n);
			}

			// Dummy Result, Always success
			return 0;
		}
		
		// Invalid Socket
		return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
	}
	// Library uninitialized
	return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

static int sceNetAdhocGameModeCreateMaster(u32 data, int size) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeCreateMaster(%08x, %i) at %08x", data, size, currentMIPS->pc);
	return 0;
}

static int sceNetAdhocGameModeCreateReplica(const char *mac, u32 data, int size) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeCreateReplica(%s, %08x, %i) at %08x", mac2str((SceNetEtherAddr*)mac).c_str(), data, size, currentMIPS->pc);
	return 0;
}

static int sceNetAdhocGameModeUpdateMaster() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateMaster()");
	return 0;
}

static int sceNetAdhocGameModeDeleteMaster() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteMaster()");
	return 0;
}

static int sceNetAdhocGameModeUpdateReplica(int id, u32 infoAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateReplica(%i, %08x)", id, infoAddr);
	return 0;
}

static int sceNetAdhocGameModeDeleteReplica(int id) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteReplica(%i)", id);
	return 0;
}

int sceNetAdhocGetSocketAlert(int id, u32 flagPtr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGetSocketAlert(%i, %08x)", id, flagPtr);
	
	// Dummy Value
	if (Memory::IsValidAddress(flagPtr)) {
		s32_le * flag = (s32_le*)Memory::GetPointer(flagPtr);
		*flag = 0;
	}

	// Dummy Result
	return 0;
}

int NetAdhocMatching_Stop(int matchingId) {
	SceNetAdhocMatchingContext* item = findMatchingContext(matchingId);

	if (item != NULL) {
		item->inputRunning = false;
		if (item->inputThread.joinable()) {
			item->inputThread.join();
		}

		item->eventRunning = false;
		if (item->eventThread.joinable()) {
			item->eventThread.join();
		}

		// Stop fake PSP Thread
		if (matchingThreads[item->matching_thid] > 0) {
			__KernelStopThread(matchingThreads[item->matching_thid], SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocMatching stopped");
			__KernelDeleteThread(matchingThreads[item->matching_thid], SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocMatching deleted");
			/*item->matchingThread->Terminate();
			if (item->matchingThread && item->matchingThread->Stopped()) {
				delete item->matchingThread;
				item->matchingThread = nullptr;
			}*/
		}
		matchingThreads[item->matching_thid] = 0;

		// Multithreading Lock
		peerlock.lock();

		// Remove your own MAC, or All memebers, or don't remove at all or we should do this on MatchingDelete ?
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
			// Make sure nobody locking/using the socket
			item->socketlock->lock();
			// Delete the socket
			NetAdhocPdp_Delete(item->socket, 0); // item->connected = (sceNetAdhocPdpDelete(item->socket, 0) < 0);
			item->socketlock->unlock();
			// Free allocated memories
			free(item->hello);
			free(item->rxbuf);
			clearPeerList(item); //deleteAllMembers(item);
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
	WARN_LOG(SCENET, "sceNetAdhocMatchingInit(%d) at %08x", memsize, currentMIPS->pc);
	
	// Uninitialized Library
	if (netAdhocMatchingInited) 
		return ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED;
		
	// Save Fake Pool Size
	fakePoolSize = memsize;

	// Initialize Library
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
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingTerm()");
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
					// Wait until Adhoc is fully connected (mipscall of ADHOCCTL_EVENT_CONNECT event is fully executed ?)
					/*if (friendFinderRunning) {
						int cnt = 0;
						while ((threadStatus != ADHOCCTL_STATE_CONNECTED) && (cnt < adhocDefaultTimeout)) {
							sleep_ms(1);
							cnt++;
						}
					}*/

					// Iterate Matching Contexts
					SceNetAdhocMatchingContext * item = contexts; 
					for (; item != NULL; item = item->next) {
						// Port Match found
						if (item->port == port) return ERROR_NET_ADHOC_MATCHING_PORT_IN_USE;
					}

					// Allocate Context Memory
					SceNetAdhocMatchingContext * context = (SceNetAdhocMatchingContext *)malloc(sizeof(SceNetAdhocMatchingContext));

					// Allocated Memory
					if (context != NULL) {
						// Create PDP Socket
						SceNetEtherAddr localmac; getLocalMac(&localmac);
						const char * mac = (const	char *)&localmac.data;
						int socket = sceNetAdhocPdpCreate(mac, (uint32_t)port, rxbuflen, 0);
						// Created PDP Socket
						if (socket > 0) {
							// Clear Memory
							memset(context, 0, sizeof(SceNetAdhocMatchingContext));

							// Allocate Receive Buffer
							context->rxbuf = (uint8_t *)malloc(rxbuflen);

							// Allocated Memory
							if (context->rxbuf != NULL) {
								// Clear Memory
								memset(context->rxbuf, 0, rxbuflen);

								// Fill in Context Data
								context->id = findFreeMatchingID();
								context->mode = mode;	
								context->maxpeers = maxnum;
								context->port = port;
								context->socket = socket;
								context->rxbuflen = rxbuflen;
								context->resendcounter = init_count;
								context->resend_int = rexmt_int; // used as ack timeout on lost packet (ie. not receiving anything after sending)?
								context->hello_int = hello_int; // client might set this to 0
								if (keepalive_int < 1) context->keepalive_int = PSP_ADHOCCTL_PING_TIMEOUT; else context->keepalive_int = keepalive_int; // client might set this to 0
								context->keepalivecounter = init_count; // used to multiply keepalive_int as timeout
								context->timeout = ((u64_le)keepalive_int * (u64_le)init_count);
								if (context->timeout < 5000000) context->timeout = 5000000; // For internet play we need higher timeout than what the game wanted
								context->handler = handler;

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
								return context->id;
							}

							// Close PDP Socket
							sceNetAdhocPdpDelete(socket, 0); // context->connected = (sceNetAdhocPdpDelete(socket, 0) < 0);
						}

						// Free Memory
						free(context);

						// Port in use
						if (socket < 1) return ERROR_NET_ADHOC_MATCHING_PORT_IN_USE; // ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED; // -1; // ERROR_NET_ADHOC_MATCHING_NOT_ESTABLISHED;
					}

					// Out of Memory
					return ERROR_NET_ADHOC_MATCHING_NO_SPACE;
				}

				// InvalidERROR_NET_Arguments
				return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
			}

			// Invalid Receive Buffer Size
			return ERROR_NET_ADHOC_MATCHING_RXBUF_TOO_SHORT;
		}

		// Invalid Member Limit
		return ERROR_NET_ADHOC_MATCHING_INVALID_MAXNUM;
	}
	// Uninitialized Library
	return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
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

		//Add your own MAC as a member (only if it's empty?)
		/*SceNetAdhocMatchingMemberInternal * peer = addMember(item, &item->mac);
		switch (item->mode) {
		case PSP_ADHOC_MATCHING_MODE_PARENT:
			peer->state = PSP_ADHOC_MATCHING_PEER_OFFER;
			break;
		case PSP_ADHOC_MATCHING_MODE_CHILD:
			peer->state = PSP_ADHOC_MATCHING_PEER_CHILD;
			break;
		case PSP_ADHOC_MATCHING_MODE_P2P:
			peer->state = PSP_ADHOC_MATCHING_PEER_P2P;
		}*/

		// Create & Start the Fake PSP Thread ("matching_ev%d" and "matching_io%d")
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

	sleep_ms(adhocMatchingEventDelayMS);
	//hleDelayResult(0, "give some time", adhocMatchingEventDelayMS * 1000); // Give a little time to make sure matching Threads are ready before the game use the next sceNet functions, should've checked for status instead of guessing the time?

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

	return NetAdhocMatching_Start(matchingId, evthPri, USER_PARTITION_ID, evthStack, inthPri, USER_PARTITION_ID, inthStack, optLen, optDataAddr);
}

// With params for Partition ID for the event & input handler stack
static int sceNetAdhocMatchingStart2(int matchingId, int evthPri, int evthPartitionId, int evthStack, int inthPri, int inthPartitionId, int inthStack, int optLen, u32 optDataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingStart2(%i, %i, %i, %i, %i, %i, %i, %i, %08x) at %08x", matchingId, evthPri, evthPartitionId, evthStack, inthPri, inthPartitionId, inthStack, optLen, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	return NetAdhocMatching_Start(matchingId, evthPri, evthPartitionId, evthStack, inthPri, inthPartitionId, inthStack, optLen, optDataAddr);
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
							if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointer(optDataPtr);
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

int sceNetAdhocMatchingCancelTargetWithOpt(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingCancelTargetWithOpt(%i, %s, %i, %08x) at %08x", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str(), optLen, optDataPtr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Initialized Library
	if (netAdhocMatchingInited)
	{
		SceNetEtherAddr * target = (SceNetEtherAddr *)macAddress;
		void * opt = NULL;
		if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointer(optDataPtr);

		// Valid Arguments
		if (target != NULL && ((optLen == 0) || (optLen > 0 && opt != NULL)))
		{
			// Find Matching Context
			SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);

			// Found Matching Context
			if (context != NULL)
			{
				// Running Context
				if (context->running)
				{
					// Find Peer
					SceNetAdhocMatchingMemberInternal * peer = findPeer(context, (SceNetEtherAddr *)target);

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

int sceNetAdhocMatchingCancelTarget(int matchingId, const char *macAddress) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingCancelTarget(%i, %s)", matchingId, mac2str((SceNetEtherAddr*)macAddress).c_str());
	if (!g_Config.bEnableWlan)
		return -1;
	return sceNetAdhocMatchingCancelTargetWithOpt(matchingId, macAddress, 0, 0);
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
			uint8_t * optdata = Memory::GetPointer(optDataAddr);
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
		return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
	
	// Multithreading Lock
	peerlock.lock();

	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);
	
	// Multithreading Unlock
	peerlock.unlock();

	// Found Context
	if (context != NULL)
	{
		// Valid Matching Modes
		if (context->mode != PSP_ADHOC_MATCHING_MODE_CHILD)
		{
			// Running Context
			if (context->running)
			{
				// Valid Optional Data Length
				if ((optLenAddr == 0) || (optLenAddr > 0 && optDataAddr != 0))
				{
					// Grab Existing Hello Data
					void * hello = context->hello;

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
						//memcpy(hello, opt, optLenAddr);
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

				// Invalid Optional Data Length
				return ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN;
			}

			// Context not running
			return ERROR_NET_ADHOC_MATCHING_NOT_RUNNING;
		}

		// Invalid Matching Mode (Child)
		return ERROR_NET_ADHOC_MATCHING_INVALID_MODE;
	}

	return 0;
}

static int sceNetAdhocMatchingGetMembers(int matchingId, u32 sizeAddr, u32 buf) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocMatchingGetMembers(%i, [%08x]=%i, %08x) at %08x", matchingId, sizeAddr, Memory::Read_U32(sizeAddr), buf, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	// Ys vs. Sora no Kiseki seems to be using this function even after AdhocMatching is Terminated, May be member list persist even after AdhocMatching terminated until the next init?
	if (!netAdhocMatchingInited) {
		//WARN_LOG(SCENET, "sceNetAdhocMatchingGetMembers - AdhocMatching is Not Initialized");
		return 0; // ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
	}

	// Minimum Argument
	if (!Memory::IsValidAddress(sizeAddr)) 
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");

	// Multithreading Lock
	peerlock.lock();
	// Find Matching Context
	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);
	// Multithreading Unlock
	peerlock.unlock();

	// Found Context
	if (context != NULL)
	{
		// Running Context
		if (context->running)
		{
			// Length Buffer available
			if (sizeAddr != 0)
			{
				int * buflen = (int *)Memory::GetPointer(sizeAddr);
				SceNetAdhocMatchingMemberInfoEmu * buf2 = NULL;
				if (Memory::IsValidAddress(buf)) {
					buf2 = (SceNetAdhocMatchingMemberInfoEmu *)Memory::GetPointer(buf);
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

					// Add Self-Peer
					if (requestedpeers > 0)
					{
						// Add Local MAC
						buf2[filledpeers++].mac_addr = context->mac;

						DEBUG_LOG(SCENET, "MemberSelf [%s]", mac2str(&context->mac).c_str());

						// Room for more than local peer
						if (requestedpeers > 1)
						{
							// P2P Mode
							if (context->mode == PSP_ADHOC_MATCHING_MODE_P2P)
							{
								// Find P2P Brother
								SceNetAdhocMatchingMemberInternal * p2p = findP2P(context, excludeTimedout);

								// P2P Brother found
								if (p2p != NULL)
								{
									// Faking lastping
									auto friendpeer = findFriend(&p2p->mac);
									if (p2p->lastping != 0 && friendpeer != NULL && friendpeer->last_recv != 0)
										p2p->lastping = CoreTiming::GetGlobalTimeUsScaled();

									// Add P2P Brother MAC
									buf2[filledpeers++].mac_addr = p2p->mac;

									DEBUG_LOG(SCENET, "MemberP2P [%s]", mac2str(&p2p->mac).c_str());
								}
							}

							// Parent or Child Mode
							else
							{
								// Iterate Peer List
								SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
								for (; peer != NULL && filledpeers < requestedpeers; peer = peer->next)
								{
									// Should we exclude timedout members?
									if (!excludeTimedout || peer->lastping != 0) {
										// Faking lastping
										auto friendpeer = findFriend(&peer->mac);
										if (peer->lastping != 0 && friendpeer != NULL && friendpeer->last_recv != 0)
											peer->lastping = CoreTiming::GetGlobalTimeUsScaled();

										// Parent Mode
										if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
											// Interested in Children
											if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) {
												// Add Child MAC
												buf2[filledpeers++].mac_addr = peer->mac;

												DEBUG_LOG(SCENET, "MemberChild [%s]", mac2str(&peer->mac).c_str());
											}
										}

										// Children Mode
										else {
											// Interested in Parent & Siblings
											if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD ||
												peer->state == PSP_ADHOC_MATCHING_PEER_PARENT) {
												// Add Peer MAC
												buf2[filledpeers++].mac_addr = peer->mac;

												if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)
													DEBUG_LOG(SCENET, "MemberParent [%s]", mac2str(&peer->mac).c_str());
												else
													DEBUG_LOG(SCENET, "MemberSibling [%s]", mac2str(&peer->mac).c_str());
											}
										}
									}
								}
							}

						}

						// Link Result List
						for (int i = 0; i < filledpeers - 1; i++)
						{
							// Link Next Element
							//buf2[i].next = &buf2[i + 1];
							buf2[i].next = buf + (sizeof(SceNetAdhocMatchingMemberInfoEmu)*i) + sizeof(SceNetAdhocMatchingMemberInfoEmu);
						}
						// Fix Last Element
						if (filledpeers > 0) buf2[filledpeers - 1].next = 0;
					}

					// Fix Buffer Size
					*buflen = sizeof(SceNetAdhocMatchingMemberInfoEmu) * filledpeers;
					DEBUG_LOG(SCENET, "MemberList [Requested: %i][Discovered: %i]", requestedpeers, filledpeers);
				}

				// Return Success
				return 0;
			}

			// Invalid Arguments
			return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ARG, "adhocmatching invalid arg");
		}

		// Context not running
		return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_RUNNING, "adhocmatching not running");
	}

	// Invalid Matching ID
	return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_ID, "adhocmatching invalid id");
}

int sceNetAdhocMatchingSendData(int matchingId, const char *mac, int dataLen, u32 dataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSendData(%i, %s, %i, %08x)", matchingId, mac2str((SceNetEtherAddr*)mac).c_str(), dataLen, dataAddr);
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
						void * data = NULL;
						if (Memory::IsValidAddress(dataAddr)) data = Memory::GetPointer(dataAddr);

						// Valid Data Length
						if (dataLen > 0 && data != NULL)
						{
							// Valid Peer Connection State
							if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT || peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_P2P)
							{
								// Send in Progress
								if (peer->sending) return ERROR_NET_ADHOC_MATCHING_DATA_BUSY;

								// Mark Peer as Sending
								peer->sending = 1;

								// Send Data to Peer
								sendBulkData(context, peer, dataLen, data);

								// Return Success
								return 0;
							}

							// Not connected / accepted
							return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_NOT_ESTABLISHED, "adhocmatching not established");
						}

						// Invalid Data Length
						return hleLogError(SCENET, ERROR_NET_ADHOC_MATCHING_INVALID_DATALEN, "adhocmatching invalid datalen");
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
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetPoolMaxAlloc()");
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Lazy way out - hardcoded return value
	return fakePoolSize/2; // (50 * 1024);
}

int sceNetAdhocMatchingGetPoolStat(u32 poolstatPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingGetPoolStat(%08x)", poolstatPtr);
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
		return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
}

void __NetTriggerCallbacks()
{
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	int delayus = 10000;
	
	auto params = adhocctlEvents.begin();
	if (params != adhocctlEvents.end())
	{
		u32 flags = params->first;
		u32 error = params->second;
		u32_le args[3] = { 0, 0, 0 };
		args[0] = flags;
		args[1] = error;

		//if (/*__KernelGetCurThread() == threadAdhocID &&*/ (!__IsInInterrupt() && __KernelIsDispatchEnabled() && !__KernelInCallback()) && IsAdhocctlInCallback() == 0)
		{
			// Since 0 is a valid index to types_ we use -1 to detects if it was loaded from an old save state
			if (actionAfterAdhocMipsCall < 0) {
				actionAfterAdhocMipsCall = __KernelRegisterActionType(AfterAdhocMipsCall::Create);
			}
			for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
				DEBUG_LOG(SCENET, "AdhocctlCallback: [ID=%i][EVENT=%i]", it->first, flags);
				args[2] = it->second.argument;
				AfterAdhocMipsCall* after = (AfterAdhocMipsCall*)__KernelCreateAction(actionAfterAdhocMipsCall);
				after->SetData(it->first, flags, args[2]);
				//SetAdhocctlInCallback(true);
				//__KernelDirectMipsCall(it->second.entryPoint, after, args, 3, true);
				hleEnqueueCall(it->second.entryPoint, 3, args, after);
			}
			adhocctlEvents.pop_front();
			if (flags == ADHOCCTL_EVENT_CONNECT)
				delayus = (adhocEventDelayMS + 2*adhocExtraPollDelayMS) * 1000; // May affects Dissidia 012 and GTA VCS
			else
				delayus = (adhocEventPollDelayMS + 2*adhocExtraPollDelayMS) * 1000; // Added an extra delay to prevent I/O Timing method from causing disconnection
		}
	}

	// Must be delayed long enough whenever there is a pending callback. Should it be 100-500ms for Adhocctl Events? or Not Less than the delays on sceNetAdhocctl HLE?
	hleDelayResult(0, "Prevent Adhocctl thread from blocking", delayus);
}

void __NetMatchingCallbacks() //(int matchingId)
{
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	int delayus = 10000;

	auto params = matchingEvents.begin();
	if (params != matchingEvents.end())
	{
		u32_le* args = (u32_le*)&(*params);
		//auto context = findMatchingContext(args[0]);

		//if (/*__KernelGetCurThread() == context->matching_thid &&*/ (!__IsInInterrupt() && __KernelIsDispatchEnabled() && !__KernelInCallback()) /*&& !IsMatchingInCallback(context)*/) 
		{
			if (actionAfterMatchingMipsCall < 0) {
				actionAfterMatchingMipsCall = __KernelRegisterActionType(AfterMatchingMipsCall::Create);
			}
			DEBUG_LOG(SCENET, "AdhocMatchingCallback: [ID=%i][EVENT=%i][%s]", args[0], args[1], mac2str((SceNetEtherAddr*)Memory::GetPointer(args[2])).c_str());
			AfterMatchingMipsCall* after = (AfterMatchingMipsCall*)__KernelCreateAction(actionAfterMatchingMipsCall);
			after->SetData(args[0], args[1], args[2]);
			//SetMatchingInCallback(context, true);
			//__KernelDirectMipsCall(args[5], after, args, 5, true);
			hleEnqueueCall(args[5], 5, args, after);
			matchingEvents.pop_front();
			delayus = (adhocMatchingEventDelayMS + 2*adhocExtraPollDelayMS) * 1000; // Added an extra delay to prevent I/O Timing method from causing disconnection
		}
	}

	// Must be delayed long enough whenever there is a pending callback. Should it be 10-100ms for Matching Events? or Not Less than the delays on sceNetAdhocMatching HLE?
	hleDelayResult(0, "Prevent AdhocMatching thread from blocking", delayus);
}

const HLEFunction sceNetAdhoc[] = {
	{0XE1D621D7, &WrapU_V<sceNetAdhocInit>,                            "sceNetAdhocInit",                        'x', ""         },
	{0XA62C6F57, &WrapI_V<sceNetAdhocTerm>,                            "sceNetAdhocTerm",                        'i', ""         },
	{0X0AD043ED, &WrapI_U<sceNetAdhocctlConnect>,                      "sceNetAdhocctlConnect",                  'i', "x"        },
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

static int sceNetAdhocctlExitGameMode() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlExitGameMode()");
	return 0;
}

static int sceNetAdhocctlGetGameModeInfo(u32 infoAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetGameModeInfo(%08x)", infoAddr);

	SceNetAdhocctlGameModeInfo* gmInfo = NULL;
	if (Memory::IsValidAddress(infoAddr)) gmInfo = (SceNetAdhocctlGameModeInfo*)Memory::GetPointer(infoAddr);
	// TODO: Writes number of participants and each participating MAC address into infoAddr/gmInfo

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
								peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();

							// Copy Peer Info
							buf[discovered].nickname = peer->nickname;
							buf[discovered].mac_addr = peer->mac_addr;
							buf[discovered].ip_addr = peer->ip_addr;
							buf[discovered].last_recv = peer->last_recv;
							discovered++;

							u32_le ipaddr = peer->ip_addr;
							DEBUG_LOG(SCENET, "Peer [%s][%s][%s][%llu]", mac2str(&peer->mac_addr).c_str(), inet_ntoa(*(in_addr*)&ipaddr), (const char*)&peer->nickname.data, peer->last_recv);
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
			return 0;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

static int sceNetAdhocctlGetAddrByName(const char *nickName, u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL; //int32_t
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);

	char nckName[ADHOCCTL_NICKNAME_LEN];
	memcpy(nckName, nickName, ADHOCCTL_NICKNAME_LEN); // Copied to null-terminated var to prevent unexpected behaviour on Logs
	nckName[ADHOCCTL_NICKNAME_LEN - 1] = 0;
	
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlGetAddrByName(%s, [%08x]=%d/%zu, %08x)", nckName, sizeAddr, buflen ? *buflen : -1, sizeof(SceNetAdhocctlPeerInfoEmu), bufAddr);
	
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
			if (buf == NULL) *buflen = getNicknameCount(nickName) * sizeof(SceNetAdhocctlPeerInfoEmu);

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

						getLocalIp(&addr);
						//buf->next = 0;
						buf[discovered].nickname = parameter.nickname;
						buf[discovered].nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
						getLocalMac(&buf[discovered].mac_addr);
						buf[discovered].ip_addr = addr.sin_addr.s_addr; // 0x11111111;
						//buf->padding = 0x1111; //0;
						buf[discovered++].last_recv = CoreTiming::GetGlobalTimeUsScaled(); 
					}

					// Peer Reference
					SceNetAdhocctlPeerInfo * peer = friends;

					// Iterate Peers
					for (; peer != NULL && discovered < requestcount; peer = peer->next)
					{
						// Match found
						if (strncmp((char *)&peer->nickname.data, nickName, ADHOCCTL_NICKNAME_LEN) == 0)
						{
							// Fake Receive Time
							if (peer->last_recv != 0) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled(); //sceKernelGetSystemTimeWide();

							// Copy Peer Info
							buf[discovered].nickname = peer->nickname;
							buf[discovered].nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
							buf[discovered].mac_addr = peer->mac_addr;
							buf[discovered].ip_addr = peer->ip_addr;
							buf[discovered++].last_recv = peer->last_recv;
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
			}

			// Multithreading Unlock
			peerlock.unlock();

			// Return Success
			return 0;
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
	{0X0AD043ED, &WrapI_U<sceNetAdhocctlConnect>,                      "sceNetAdhocctlConnect",                  'i', "x"        },
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

int sceNetAdhocDiscoverInitStart() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverInitStart()");
	return 0;
}

int sceNetAdhocDiscoverStop() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverStop()");
	return 0;
}

int sceNetAdhocDiscoverTerm() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverTerm()");
	return 0;
}

int sceNetAdhocDiscoverUpdate() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverUpdate()");
	return 0;
}

int sceNetAdhocDiscoverGetStatus() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverGetStatus()");
	return 0;
}

int sceNetAdhocDiscoverRequestSuspend()
{
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocDiscoverRequestSuspend()");
	return 0;
}

const HLEFunction sceNetAdhocDiscover[] = {
	{0X941B3877, &WrapI_V<sceNetAdhocDiscoverInitStart>,               "sceNetAdhocDiscoverInitStart",           'i', ""         },
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
	context->socketlock->lock();
	sceNetAdhocPdpSend(context->socket, (const char*)(SceNetEtherAddr *)broadcastMAC, context->port, &ping, sizeof(ping), 0, ADHOC_F_NONBLOCK);
	context->socketlock->unlock();
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

		// Copy Hello Data
		if (context->hellolen > 0) memcpy(hello + 5, context->hello, context->hellolen);

		// Send Broadcast
		context->socketlock->lock();
		sceNetAdhocPdpSend(context->socket, (const char*)(SceNetEtherAddr *)broadcastMAC, context->port, hello, 5 + context->hellolen, 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();

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
	// Find Peer
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);

	// Found Peer
	if (peer != NULL && (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_P2P))
	{
		// Required Sibling Buffer
		uint32_t siblingbuflen = 0;

		// Parent Mode
		if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) siblingbuflen = sizeof(SceNetEtherAddr) * (countConnectedPeers(context) - 2);

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
			sceNetAdhocPdpSend(context->socket, (const char*)mac, context->port, accept, 9 + optlen + siblingbuflen, 0, ADHOC_F_NONBLOCK);
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
			sceNetAdhocPdpSend(context->socket, (const char*)mac, context->port, join, 5 + optlen, 0, ADHOC_F_NONBLOCK);
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
		sceNetAdhocPdpSend(context->socket, (const char*)mac, context->port, cancel, 5 + optlen, 0, ADHOC_F_NONBLOCK);
		context->socketlock->unlock();

		// Free Memory
		free(cancel);
	}

	peerlock.lock();
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
	peerlock.unlock();
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
			sceNetAdhocPdpSend(context->socket, (const char*)mac, context->port, send, 5 + datalen, 0, ADHOC_F_NONBLOCK);
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
				int sent = sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac, context->port, packet, sizeof(packet), 0, ADHOC_F_NONBLOCK);
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
	// Find abandoned Child
	SceNetAdhocMatchingMemberInternal * deadkid = findPeer(context, mac);

	// Found abandoned Child
	if (deadkid != NULL)
	{
		// Packet Buffer
		uint8_t packet[7];

		// Set Opcode
		packet[0] = PSP_ADHOC_MATCHING_PACKET_DEATH;

		// Set abandoned Child MAC
		memcpy(packet + 1, mac, sizeof(SceNetEtherAddr));

		// Iterate Peers
		SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
		for (; peer != NULL; peer = peer->next)
		{
			// Skip dead Child
			if (peer == deadkid) continue;

			// Send only to children
			if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD)
			{
				// Send Packet
				context->socketlock->lock();
				sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac, context->port, packet, sizeof(packet), 0, ADHOC_F_NONBLOCK);
				context->socketlock->unlock();
			}
		}
	}
}

/**
* Tell Established Peers that we're shutting the Networking Layer down
* @param context Matching Context Pointer
*/
void sendByePacket(SceNetAdhocMatchingContext * context)
{
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
			sceNetAdhocPdpSend(context->socket, (const char*)&peer->mac, context->port, &opcode, sizeof(opcode), 0, ADHOC_F_NONBLOCK);
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
		peer->lastping = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0;
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
						peer->lastping = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0;

						peerlock.lock();
						// Link Peer into List
						peer->next = context->peerlist;
						context->peerlist = peer;
						peerlock.unlock();
					}
				}

				// Peer available now
				if (peer != NULL)
				{
					// Spawn Hello Event
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
					if (peer != NULL && context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) {
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
							peer->lastping = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0;

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
							if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD) postAcceptAddSiblings(context, siblingcount, siblings);

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

	// Get Parent
	SceNetAdhocMatchingMemberInternal * parent = findParent(context);

	// Get Outgoing Join Request
	SceNetAdhocMatchingMemberInternal * request = findOutgoingRequest(context);

	// Get P2P Partner
	SceNetAdhocMatchingMemberInternal * p2p = findP2P(context);

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

				// Child Mode
				if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
				{
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

			// Allocate Memory (If this fails... we are fucked.)
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
				sibling->lastping = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0;

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
			// Spawn Leave / Kick Event
			spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_BYE, sendermac, 0, NULL);

			// Delete Peer
			deletePeer(context, peer);
			// Instead of removing peer immediately, We should give a little time before removing the peer and let it timed out? just in case the game is in the middle of communicating with the peer on another thread so it won't recognize it as Unknown peer
			//peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
		}

		// Parent Bye
		else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)
		{
			// Iterate Peers
			SceNetAdhocMatchingMemberInternal * item = context->peerlist; 
			for (; item != NULL; item = item->next)
			{
				// Established Peer
				if (item->state == PSP_ADHOC_MATCHING_PEER_CHILD || item->state == PSP_ADHOC_MATCHING_PEER_PARENT)
				{
					// Spawn Leave / Kick Event
					spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_BYE, &item->mac, 0, NULL);
				}
			}

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
	setCurrentThreadName("MatchingEvent");
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
					context->eventlock->unlock();
					// Call Event Handler
					//context->handler(context->id, msg->opcode, &msg->mac, msg->optlen, opt);
					// Notify Event Handlers
					notifyMatchingHandler(context, msg, opt, bufAddr, bufLen, args); // If we're using shared Buffer & Args for All Events We should wait for the Mipscall to be fully executed before processing the next event. GTA VCS need this delay/sleep.

					// Give some time before executing the next mipscall to prevent event ACCEPT(6)->ESTABLISH(7) getting reversed After Action ESTABLISH(7)->ACCEPT(6)
					// Must Not be delayed too long to prevent desync/disconnect. Not longer than the delays on callback's HLE?
					//sleep_ms(10); //sceKernelDelayThread(10000);

					// Lock again
					context->eventlock->lock();

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
			while (Core_IsStepping() && coreState != CORE_POWERDOWN && contexts != NULL && context->eventRunning) sleep_ms(1);
		}

		// Process Last Messages
		if (contexts != NULL && context->event_stack != NULL)
		{
			// Claim Stack
			context->eventlock->lock();

			// Iterate Message List
			ThreadMessage * msg = context->event_stack; 
			for (; msg != NULL; msg = msg->next)
			{
				// Default Optional Data
				void * opt = NULL;

				// Grab Optional Data
				if (msg->optlen > 0) opt = ((u8 *)msg) + sizeof(ThreadMessage); //&msg[1]

				INFO_LOG(SCENET, "EventLoop[%d]: Matching Event [EVENT=%d]\n", matchingId, msg->opcode);

				context->eventlock->unlock();
				// Original Call Event Handler
				//context->handler(context->id, msg->opcode, &msg->mac, msg->optlen, opt);
				// Notify Event Handlers
				notifyMatchingHandler(context, msg, opt, bufAddr, bufLen, args);
				context->eventlock->lock();
			}

			// Clear Event Message Stack
			clearStack(context, PSP_ADHOC_MATCHING_EVENT_STACK);

			// Free Stack
			context->eventlock->unlock();
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
	setCurrentThreadName("MatchingInput");
	// Multithreading Lock
	peerlock.lock();
	// Cast Context
	SceNetAdhocMatchingContext * context = findMatchingContext(matchingId);
	// Multithreading Unlock
	peerlock.unlock();

	// Last Ping
	u64_le lastping = 0;

	// Last Hello
	u64_le lasthello = 0;

	u64_le now;

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
				now = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0;

				// Hello Message Sending Context with unoccupied Slots
				if ((context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && (countChildren(context) < (context->maxpeers - 1))) || (context->mode == PSP_ADHOC_MATCHING_MODE_P2P && findP2P(context) == NULL))
				{
					// Hello Message Broadcast necessary because of Hello Interval
					if (context->hello_int > 0)
						if ((now - lasthello) >= context->hello_int)
						{
							// Broadcast Hello Message
							broadcastHelloMessage(context);

							// Update Hello Timer
							lasthello = now;
						}
				}

				// Ping Required
				if (context->keepalive_int > 0)
					if ((now - lastping) >= context->keepalive_int)
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
					for (; msg != NULL; msg = msg->next)
					{
						// Default Optional Data
						void* opt = NULL;

						// Grab Optional Data
						if (msg->optlen > 0) opt = ((u8*)msg) + sizeof(ThreadMessage);

						context->inputlock->unlock(); // Unlock to prevent race condition when locking peerlock

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

						// Cancel Bulk Data Transfer (does nothing as of now as we fire and forget anyway) // Do we need to check DeathPacket and ByePacket here?
						//else if(msg->opcode == PSP_ADHOC_MATCHING_PACKET_BULK_ABORT) sendAbortBulkDataPacket(context, &msg->mac, msg->optlen, opt);

						context->inputlock->lock(); // Lock again
					}

					// Clear IO Message Stack
					clearStack(context, PSP_ADHOC_MATCHING_INPUT_STACK);

					// Free Stack
					context->inputlock->unlock();
				}

				// Receive PDP Datagram
				SceNetEtherAddr sendermac;
				uint16_t senderport;
				int rxbuflen = context->rxbuflen;
				context->socketlock->lock();
				int recvresult = sceNetAdhocPdpRecv(context->socket, &sendermac, &senderport, context->rxbuf, &rxbuflen, 0, ADHOC_F_NONBLOCK);
				context->socketlock->unlock();

				// Received Data from a Sender that interests us
				if (recvresult == 0 && rxbuflen > 0 && context->port == senderport)
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
						u64_le delta = now - peer->last_recv;
						DEBUG_LOG(SCENET, "Timestamp Delta: %llu (%llu - %llu) from %s", delta, now, peer->last_recv, mac2str(&sendermac).c_str());
						if (/*context->rxbuf[0] > 0 &&*/ peer->last_recv != 0) peer->last_recv = now - 1; // - context->keepalive_int; // May need to deduce by ping interval to prevent Dissidia 012 unable to see other players (ie. disappearing issue)
					}
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
			while (Core_IsStepping() && coreState != CORE_POWERDOWN && contexts != NULL && context->inputRunning) sleep_ms(1);
		}

		if (contexts != NULL) {
			// Send Bye Messages
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

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
#include "Core/Core.h"
#include "Core/MemMapHelpers.h"
#include "Common/ChunkFile.h"
#include "Core/MIPS/MIPSCodeUtils.h"

#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/proAdhocServer.h"

#include "base/mutex.h"

// shared in sceNetAdhoc.h since it need to be used from sceNet.cpp also
// TODO: Make accessor functions instead, and throw all this state in a struct.
bool netAdhocInited;
bool netAdhocctlInited;
bool networkInited;

static bool netAdhocMatchingInited;
int netAdhocMatchingStarted = 0;

SceUID threadAdhocID;

recursive_mutex adhocEvtMtx;
std::vector<std::pair<u32, u32>> adhocctlEvents;
std::vector<u64> matchingEvents;
u32 dummyThreadHackAddr = 0;
u32_le dummyThreadCode[3];

std::map<int, AdhocctlHandler> adhocctlHandlers;

int matchingEventThread(int matchingId); 
int matchingInputThread(int matchingId); 

int sceNetAdhocTerm();
int sceNetAdhocctlTerm();
int sceNetAdhocMatchingTerm();
int sceNetAdhocMatchingSetHelloOpt(int matchingId, int optLenAddr, u32 optDataAddr);

void __NetAdhocShutdown() {
	//Kill AdhocServer Thread
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
		sceNetAdhocTerm();
	}
	if (dummyThreadHackAddr) {
		kernelMemory.Free(dummyThreadHackAddr);
		dummyThreadHackAddr = 0;
	}
}

void __NetAdhocDoState(PointerWrap &p) {
	auto s = p.Section("sceNetAdhoc", 1, 2);
	if (!s)
		return;

	p.Do(netAdhocInited);
	p.Do(netAdhocctlInited);
	p.Do(netAdhocMatchingInited);
	p.Do(adhocctlHandlers);

	if (s >= 2) {
		p.Do(actionAfterMatchingMipsCall);
		__KernelRestoreActionType(actionAfterMatchingMipsCall, AfterMatchingMipsCall::Create);

		p.Do(dummyThreadHackAddr);
	} else if (p.mode == p.MODE_READ) {
		// Previously, this wasn't being saved.  It needs its own space.
		if (strcmp("dummythreadhack", kernelMemory.GetBlockTag(dummyThreadHackAddr)) != 0) {
			u32 blockSize = sizeof(dummyThreadCode);
			dummyThreadHackAddr = kernelMemory.Alloc(blockSize, false, "dummythreadhack");
		}
	}
	if (dummyThreadHackAddr) {
		Memory::Memcpy(dummyThreadHackAddr, dummyThreadCode, sizeof(dummyThreadCode));
	}
}

void __UpdateAdhocctlHandlers(u32 flag, u32 error) {
	lock_guard adhocGuard(adhocEvtMtx);
	adhocctlEvents.push_back({ flag, error });
}

// TODO: MipsCall needs to be called from it's own PSP Thread instead of from any random PSP Thread
void __UpdateMatchingHandler(u64 ArgsPtr) {
	lock_guard adhocGuard(adhocEvtMtx);
	matchingEvents.push_back(ArgsPtr);
}

static int getBlockingFlag(int id) {
#ifdef _MSC_VER
	return 0;
#else
	int sockflag = fcntl(id, F_GETFL, O_NONBLOCK);
	return sockflag & O_NONBLOCK;
#endif
}

void __NetAdhocInit() {
	friendFinderRunning = false;
	netAdhocInited = false;
	netAdhocctlInited = false;
	netAdhocMatchingInited = false;
	adhocctlHandlers.clear();
	__AdhocServerInit();
	dummyThreadCode[0] = MIPS_MAKE_SYSCALL("sceNetAdhoc", "__NetTriggerCallbacks");
	dummyThreadCode[1] = MIPS_MAKE_B(-2);
	dummyThreadCode[2] = MIPS_MAKE_NOP();
	u32 blockSize = sizeof(dummyThreadCode);
	dummyThreadHackAddr = kernelMemory.Alloc(blockSize, false, "dummythreadhack");
	Memory::Memcpy(dummyThreadHackAddr, dummyThreadCode, sizeof(dummyThreadCode)); // This area will be cleared again after loading an old savestate :(
	actionAfterMatchingMipsCall = __KernelRegisterActionType(AfterMatchingMipsCall::Create);
	// Create built-in AdhocServer Thread
	if (g_Config.bEnableWlan && g_Config.bEnableAdhocServer) {
		adhocServerRunning = true;
		adhocServerThread = std::thread(proAdhocServerThread, SERVER_PORT);
	}
}

u32 sceNetAdhocInit() {
	// Library uninitialized
	INFO_LOG(SCENET, "sceNetAdhocInit() at %08x", currentMIPS->pc);
	if (!netAdhocInited) {
		// Clear Translator Memory
		memset(&pdp, 0, sizeof(pdp));
		memset(&ptp, 0, sizeof(ptp));

		// Library initialized
		netAdhocInited = true;

		// Create fake PSP Thread for callback
		// TODO: Should use a separated threads for friendFinder, matchingEvent, and matchingInput and created on AdhocctlInit & AdhocMatchingStart instead of here
		#define PSP_THREAD_ATTR_KERNEL 0x00001000 // PSP_THREAD_ATTR_KERNEL is located in sceKernelThread.cpp instead of sceKernelThread.h :(
		//threadAdhocID = __KernelCreateThreadInternal("AdhocThread", __KernelGetCurThreadModuleId(), dummyThreadHackAddr, 0x30, 4096, PSP_THREAD_ATTR_KERNEL);
		threadAdhocID = __KernelCreateThread("AdhocThread", __KernelGetCurThreadModuleId(), dummyThreadHackAddr, 0x10, 0x1000, 0, PSP_THREAD_ATTR_KERNEL);
		if (threadAdhocID > 0) {
			__KernelStartThread(threadAdhocID, 0, 0);
		}

		// Return Success
		return 0;
	}
	// Already initialized
	return ERROR_NET_ADHOC_ALREADY_INITIALIZED;
}

static u32 sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlInit(%i, %i, %08x) at %08x", stackSize, prio, productAddr, currentMIPS->pc);
	
	if (netAdhocctlInited)
		return ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED;
	
	if(g_Config.bEnableWlan) {
		if (initNetwork((SceNetAdhocctlAdhocId *)Memory::GetPointer(productAddr)) == 0) {
			if (!friendFinderRunning) {
				friendFinderRunning = true;
				friendFinderThread = std::thread(friendFinder);
			}
			networkInited = true;
		} else {
			WARN_LOG(SCENET, "sceNetAdhocctlInit: Failed to init the network but faking success");
			networkInited = false;  // TODO: What needs to check this? Pretty much everything? Maybe we should just set netAdhocctlInited to false..
		}
	}

	netAdhocctlInited = true; //needed for cleanup during AdhocctlTerm even when it failed to connect to Adhoc Server (since it's being faked as success)
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
static int sceNetAdhocPdpCreate(const char *mac, u32 port, int bufferSize, u32 unknown) {
	INFO_LOG(SCENET, "sceNetAdhocPdpCreate(%s, %u, %u, %u) at %08x", mac, port, bufferSize, unknown, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	int retval = ERROR_NET_ADHOC_NOT_INITIALIZED;
	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)mac;
	if (netAdhocInited) {
		// Valid Arguments are supplied
		if (mac != NULL && bufferSize > 0) {
			// Valid MAC supplied
			if (isLocalMAC(saddr)) {
				//// Unused Port supplied
				//if (!_IsPDPPortInUse(port)) {} 
				//
				//// Port is in use by another PDP Socket
				//return ERROR_NET_ADHOC_PORT_IN_USE;

				// Create Internet UDP Socket
				int usocket = (int)INVALID_SOCKET;
				usocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
				// Valid Socket produced
				if (usocket != INVALID_SOCKET) {
					// Change socket buffer size when necessary
					if (getSockBufferSize(usocket, SO_SNDBUF) < bufferSize) setSockBufferSize(usocket, SO_SNDBUF, bufferSize);
					if (getSockBufferSize(usocket, SO_RCVBUF) < bufferSize) setSockBufferSize(usocket, SO_RCVBUF, bufferSize);

					// Enable Port Re-use, this will allow binding to an already used port, but only one of them can read the data (shared receive buffer?)
					int one = 1;
					setsockopt(usocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one)); // NO idea if we need this
					// Binding Information for local Port
					sockaddr_in addr;
					addr.sin_family = AF_INET;
					addr.sin_addr.s_addr = INADDR_ANY;

					//if (port < 7) addr.sin_port = htons(port + 1341); else // <= 443
					addr.sin_port = htons(port); // This not safe in any way...
					// The port might be under 1024 (ie. GTA:VCS use port 1, Ford Street Racing use port 0 (UNUSED_PORT), etc) and already used by other application/host OS, should we add 1024 to the port whenever it tried to use an already used port?

					// Bound Socket to local Port
					int iResult = bind(usocket, (sockaddr *)&addr, sizeof(addr));
					/*if (iResult == SOCKET_ERROR && errno == 10048) { //Forcing PdpCreate to be successfull using a different Port might not works and might affect other players sharing the same IP due to "deleteFriendByIP" (should delete by MAC instead of IP)
						addr.sin_port = 0; //UNUSED_PORT
						iResult = bind(usocket, (sockaddr *)&addr, sizeof(addr));
						WARN_LOG(SCENET, "Port %u is already used, replaced with UNUSED_PORT(%u)", port, ntohs(addr.sin_port));
					}*/
					if (iResult == 0) {
						// Allocate Memory for Internal Data
						SceNetAdhocPdpStat * internal = (SceNetAdhocPdpStat *)malloc(sizeof(SceNetAdhocPdpStat));

						// Allocated Memory
						if (internal != NULL) {
							// Clear Memory
							memset(internal, 0, sizeof(SceNetAdhocPdpStat));

							// Find Free Translator Index
							int i = 0; for (; i < 255; i++) if (pdp[i] == NULL) break;

							// Found Free Translator Index
							if (i < 255) {
								// Fill in Data
								internal->id = usocket;
								internal->laddr = *saddr;
								internal->lport = getLocalPort(usocket); //should use the port given to the socket (in case it's UNUSED_PORT port) isn't?
								internal->rcv_sb_cc = bufferSize;

								// Link Socket to Translator ID
								pdp[i] = internal;

								// Forward Port on Router
								//sceNetPortOpen("UDP", sport); // I need to figure out how to use this in windows/linux
								
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
	DEBUG_LOG(SCENET, "sceNetAdhocctlGetParameter(%08x)",paramAddr);
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
 * @param timeout Send Timeout
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_INVALID_ADDR, ADHOC_INVALID_PORT, ADHOC_INVALID_DATALEN, ADHOC_SOCKET_ALERTED, ADHOC_TIMEOUT, ADHOC_THREAD_ABORTED, ADHOC_WOULD_BLOCK, NET_NO_SPACE, NET_INTERNAL
 */
static int sceNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPdpSend(%i, %s, %i, %p, %i, %i, %i)", id, mac, port, data, len, timeout, flag);
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	SceNetEtherAddr * daddr = (SceNetEtherAddr *)mac;
	uint16 dport = (uint16)port;
	
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
							if (flag) timeout = 0;

							// Apply Send Timeout Settings to Socket
							setsockopt(socket->id, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));

							// Single Target
							if (!isBroadcastMAC(daddr)) {
								// Fill in Target Structure
								sockaddr_in target;
								target.sin_family = AF_INET;
								target.sin_port = htons(dport);

								// Get Peer IP
								if (resolveMAC((SceNetEtherAddr *)daddr, (uint32_t *)&target.sin_addr.s_addr)) {
									// Acquire Network Lock
									//_acquireNetworkLock();

									// Send Data
									changeBlockingMode(socket->id, flag);
									int sent = sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
									int error = errno;
									if (sent == SOCKET_ERROR) {
										DEBUG_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u] (size=%i)", error, id, getLocalPort(socket->id), ntohs(target.sin_port), len);
									}
									changeBlockingMode(socket->id, 0);

									// Free Network Lock
									//_freeNetworkLock();

									uint8_t * sip = (uint8_t *)&target.sin_addr.s_addr;
									// Sent Data
									if (sent == len) {
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Sent %u bytes to %u.%u.%u.%u:%u\n", id, getLocalPort(socket->id), sent, sip[0], sip[1], sip[2], sip[3], ntohs(target.sin_port));

										// Success
										return 0;
									}

									// Blocking Situation
									if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;

									// Timeout
									return ERROR_NET_ADHOC_TIMEOUT; //-1;
								}
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

								// Iterate Peers
								SceNetAdhocctlPeerInfo * peer = friends;
								for (; peer != NULL; peer = peer->next) {
									// Skip timed out friends
									if (peer->last_recv == 0) continue;

									// Fill in Target Structure
									sockaddr_in target;
									target.sin_family = AF_INET;
									target.sin_addr.s_addr = peer->ip_addr;
									target.sin_port = htons(dport);

									// Send Data
									changeBlockingMode(socket->id, flag);
									int sent = sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
									int error = errno;
									if (sent == SOCKET_ERROR) {
										DEBUG_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[%i:%u->%u](BC) [size=%i]", error, id, getLocalPort(socket->id), ntohs(target.sin_port), len);
									}
									changeBlockingMode(socket->id, 0);
									if (sent >= 0) {
										uint8_t * sip = (uint8_t *)&target.sin_addr.s_addr;
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u](BC): Sent %u bytes to %u.%u.%u.%u:%u\n", id, getLocalPort(socket->id), sent, sip[0], sip[1], sip[2], sip[3], ntohs(target.sin_port));
									}
								}

								// Free Peer Lock
								peerlock.unlock();

								// Free Network Lock
								//_freeNetworkLock();

								// Success, Broadcast never fails!
								return 0;
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
 * @param buf OUT: Received Data
 * @param len IN: Buffer Size OUT: Received Data Length
 * @param timeout Receive Timeout
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_NOT_ENOUGH_SPACE, ADHOC_THREAD_ABORTED, NET_INTERNAL
 */
static int sceNetAdhocPdpRecv(int id, void *addr, void * port, void *buf, void *dataLength, u32 timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, currentMIPS->pc);
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
				if (flag == 1) timeout = 0;
#else
				// Nonblocking Simulator
				int wouldblock = 0;

				// Minimum Timeout
				uint32_t mintimeout = 250000;

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

				// Apply Receive Timeout Settings to Socket
				setsockopt(socket->id, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

				// Sender Address
				sockaddr_in sin;

				// Set Address Length (so we get the sender ip)
				socklen_t sinlen = sizeof(sin);
				//sin.sin_len = (uint8_t)sinlen;

				// Acquire Network Lock
				//_acquireNetworkLock();

				// Receive Data
				changeBlockingMode(socket->id, flag);
				int received = recvfrom(socket->id, (char *)buf, *len,0,(sockaddr *)&sin, &sinlen);
				int error = errno;
				if (received == SOCKET_ERROR) {
					VERBOSE_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpRecv [size=%i]", error, *len);
				}
				changeBlockingMode(socket->id, 0); 

				// Received Data
				if (received >= 0) {
					uint8_t * sip = (uint8_t *)&sin.sin_addr.s_addr;
					DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %u bytes from %u.%u.%u.%u:%u\n", id, getLocalPort(socket->id), received, sip[0], sip[1], sip[2], sip[3], ntohs(sin.sin_port));

					// Peer MAC
					SceNetEtherAddr mac;

					// Find Peer MAC
					if (resolveIP(sin.sin_addr.s_addr, &mac)) {
						// Provide Sender Information
						*saddr = mac;
						*sport = ntohs(sin.sin_port);

						// Save Length
						*len = received;

						// Free Network Lock
						//_freeNetworkLock();

						// Return Success
						return 0;
					}
					WARN_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %i bytes from Unknown Peer %u.%u.%u.%u:%u [%02X:%02X:%02X:%02X:%02X:%02X]", id, getLocalPort(socket->id), received, sip[0], sip[1], sip[2], sip[3], ntohs(sin.sin_port), mac.data[0], mac.data[1], mac.data[2], mac.data[3], mac.data[4], mac.data[5]);

					// Free Network Lock
					//_freeNetworkLock();

					//Receiving data from unknown peer, ignore it ?
					//return ERROR_NET_ADHOC_WOULD_BLOCK; //ERROR_NET_ADHOC_NO_DATA_AVAILABLE
				}

				// Free Network Lock
				//_freeNetworkLock();

#ifdef PDP_DIRTY_MAGIC
				// Restore Nonblocking Flag for Return Value
				if (wouldblock) flag = 1;
#endif

				// Nothing received
				if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;
				return ERROR_NET_ADHOC_TIMEOUT;
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
			int i = 0; for (; i < count; i++)
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
			if (count > FD_SETSIZE) count = FD_SETSIZE; // return affectedsockets;

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

	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments
		if (id > 0 && id <= 255) {
			// Cast Socket
			SceNetAdhocPdpStat * sock = pdp[id - 1];

			// Valid Socket
			if (sock != NULL) {
				// Close Connection
				closesocket(sock->id);

				// Remove Port Forward from Router
				//sceNetPortClose("UDP", sock->lport);

				// Free Memory
				// free(sock);

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
		// Not connected
		if (threadStatus == ADHOCCTL_STATE_DISCONNECTED) {
			threadStatus = ADHOCCTL_STATE_SCANNING;

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

			// Wait for Status to be connected to prevent Ford Street Racing from Failed to find game session
			if (friendFinderRunning) {
				int cnt = 0;
				while ((threadStatus == ADHOCCTL_STATE_SCANNING) && (cnt < 5000)) {
					sleep_ms(1);
					cnt++;
				}
			}

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

	INFO_LOG(SCENET, "sceNetAdhocctlGetScanInfo([%08x]=%i, %08x)", sizeAddr, buflen ? *buflen : -1, bufAddr);
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
			if (buf == NULL) *buflen = countAvailableNetworks() * sizeof(SceNetAdhocctlScanInfoEmu);

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

						// Fake Channel Number 1 on Automatic Channel
						// if (buf[discovered].channel == 0) buf[discovered].channel = 1;

						//Always Fake Channel 1
						buf[discovered].channel = 1;

						// Increase Discovery Counter
						discovered++;
					}

					// Link List
					int i = 0; for (; i < discovered - 1; i++) {
						// Link Network
						buf[i].next = bufAddr + (sizeof(SceNetAdhocctlScanInfoEmu)*i) + sizeof(SceNetAdhocctlScanInfoEmu); // buf[i].next = &buf[i + 1];
					}

					// Fix Last Element
					if (discovered > 0) buf[discovered - 1].next = 0;
				}

				// Fix Size
				*buflen = discovered * sizeof(SceNetAdhocctlScanInfoEmu);
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
	struct AdhocctlHandler handler;
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

static u32 sceNetAdhocctlDisconnect() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(SCENET, "sceNetAdhocctlDisconnect() at %08x [group=%s]", currentMIPS->pc, parameter.group_name.data);
	/*
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	*/

	// Library initialized
	if (netAdhocctlInited) {
		// Connected State (Adhoc Mode)
		if (threadStatus != ADHOCCTL_STATE_DISCONNECTED) { // (threadStatus == ADHOCCTL_STATE_CONNECTED) 
			// Clear Network Name
			memset(&parameter.group_name, 0, sizeof(parameter.group_name));

			// Set Disconnected State
			threadStatus = ADHOCCTL_STATE_DISCONNECTED;

			// Set HUD Connection Status
			//setConnectionStatus(0);

			// Prepare Packet
			uint8_t opcode = OPCODE_DISCONNECT;

			// Acquire Network Lock
			//_acquireNetworkLock();

			// Send Disconnect Request Packet
			int iResult = send(metasocket, (const char *)&opcode, 1, 0);
			if (iResult == SOCKET_ERROR) {
				ERROR_LOG(SCENET, "Socket error (%i) when sending", errno);
			}

			// Free Network Lock
			//_freeNetworkLock();

			// Multithreading Lock
			//peerlock.lock();

			// Clear Peer List
			//freeFriendsRecursive(friends);

			// Delete Peer Reference
			//friends = NULL;

			// Clear Group List
			//freeGroupsRecursive(networks);

			// Delete Group Reference
			//networks = NULL;

			// Multithreading Unlock
			//peerlock.unlock();
		}
		
		// Notify Event Handlers (even if we weren't connected, not doing this will freeze games like God Eater, which expect this behaviour)
		{
			lock_guard adhocGuard(adhocEvtMtx);
			adhocctlEvents.push_back({ ADHOCCTL_EVENT_DISCONNECT, 0 });
		}
		// Return Success, some games might ignore returned value and always treat it as success, otherwise repeatedly calling this function
		return 0;
	}

	// Library uninitialized
	return 0; //ERROR_NET_ADHOC_NOT_INITIALIZED; // Wipeout Pulse will repeatedly calling this function if returned value is ERROR_NET_ADHOC_NOT_INITIALIZED
}

static u32 sceNetAdhocctlDelHandler(u32 handlerID) {
	if (adhocctlHandlers.find(handlerID) != adhocctlHandlers.end()) {
		adhocctlHandlers.erase(handlerID);
		WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlDelHandler(%d): deleted handler %d", handlerID, handlerID);
	} else {
		ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlDelHandler(%d): asked to delete invalid handler %d", handlerID, handlerID);
	}

	return 0;
}

int sceNetAdhocctlTerm() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(SCENET, "sceNetAdhocctlTerm()");

	if (netAdhocMatchingInited) sceNetAdhocMatchingTerm();

	if (netAdhocctlInited) {
		netAdhocctlInited = false;
		friendFinderRunning = false;
		if (friendFinderThread.joinable()) {
			friendFinderThread.join();
		}
		//May also need to clear Handlers
		adhocctlHandlers.clear();
		// Free stuff here
		closesocket(metasocket);
		metasocket = (int)INVALID_SOCKET;
/*#ifdef _MSC_VER
		WSACleanup(); // Might be better to call WSAStartup/WSACleanup from sceNetInit/sceNetTerm isn't? since it's the first/last network function being used, even better to put it in __NetInit/__NetShutdown as it's only called once
#endif*/
	}

	return 0;
}

static int sceNetAdhocctlGetNameByAddr(const char *mac, u32 nameAddr) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocctlGetNameByAddr(%s, %08x)", mac, nameAddr);
	
	// Library initialized
	if (netAdhocctlInited)
	{
		// Valid Arguments
		if (mac != NULL && nameAddr != 0)
		{
			SceNetAdhocctlNickname * nickname = NULL;
			if (Memory::IsValidAddress(nameAddr)) nickname = (SceNetAdhocctlNickname *)Memory::GetPointer(nameAddr);
			// Get Local MAC Address
			SceNetEtherAddr localmac; getLocalMac(&localmac);

			// Local MAC Matches
			if (memcmp(&localmac, mac, sizeof(SceNetEtherAddr)) == 0)
			{
				// Write Data
				*nickname = parameter.nickname;

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

					// Return Success
					return 0;
				}
			}

			// Multithreading Unlock
			peerlock.unlock(); 

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
			return sceNetAdhocctlCreate((const char*)&sinfo->group_name);
		}

		// Invalid Argument
		return ERROR_NET_ADHOCCTL_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

int sceNetAdhocctlGetPeerInfo(const char *mac, int size, u32 peerInfoAddr) {
	VERBOSE_LOG(SCENET, "sceNetAdhocctlGetPeerInfo(%s, %i, %08x) at %08x", mac, size, peerInfoAddr, currentMIPS->pc);
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
			strcpy((char*)nickname.data, g_Config.sNickName.c_str());
			//buf->next = 0;
			buf->nickname = nickname;
			buf->nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0; // last char need to be null-terminated char
			buf->mac_addr = *maddr;
			buf->ip_addr = addr.sin_addr.s_addr; // 0x11111111;
			//buf->padding = 0x1111; //0;
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
				//buf->padding = /*0;*/ 0x1111;
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
	INFO_LOG(SCENET, "sceNetAdhocctlCreate(%s) at %08x", groupName, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	const SceNetAdhocctlGroupName * groupNameStruct = (const SceNetAdhocctlGroupName *)groupName;
	// Library initialized
	if (netAdhocctlInited) {
		// Valid Argument
		if (validNetworkName(groupNameStruct)) {
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

				// Wait for Status to be connected to prevent Ford Street Racing from Failed to create game session
				if (friendFinderRunning) {
					int cnt = 0;
					while ((threadStatus != ADHOCCTL_STATE_CONNECTED) && (cnt < 5000)) {
						sleep_ms(1);
						cnt++;
					}
				}

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
		INFO_LOG(SCENET, "sceNetAdhocctlConnect(groupName=%s) at %08x", Memory::GetCharPointer(ptrToGroupName), currentMIPS->pc);
		return sceNetAdhocctlCreate(Memory::GetCharPointer(ptrToGroupName));
	} 
		
	return ERROR_NET_ADHOC_INVALID_ARG; // ERROR_NET_ADHOC_INVALID_ADDR;
}

static int sceNetAdhocctlCreateEnterGameMode(const char *groupName, int unknown, int playerNum, u32 macsAddr, int timeout, int unknown2) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlCreateEnterGameMode(%s, %i, %i, %08x, %i, %i) at %08x", groupName, unknown, playerNum, macsAddr, timeout, unknown2, currentMIPS->pc);
	return -1;
}

static int sceNetAdhocctlJoinEnterGameMode(const char *groupName, const char *macAddr, int timeout, int unknown2) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlJoinEnterGameMode(%s, %s, %i, %i) at %08x", groupName, macAddr, timeout, unknown2, currentMIPS->pc);
	return -1;
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
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlCreateEnterGameModeMin(%s, %i, %i, %i, %08x, %d, %i) at %08x", group_name, game_type, min_members, num_members, membersAddr, timeout, flag, currentMIPS->pc);
	// We don't really need the Minimum User Check
	return sceNetAdhocctlCreateEnterGameMode(group_name, game_type, num_members, membersAddr, timeout, flag);
}

int sceNetAdhocTerm() {
	INFO_LOG(SCENET, "sceNetAdhocTerm()");
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup all the sockets right?

	if (netAdhocctlInited) sceNetAdhocctlTerm();

	// Library is initialized
	if (netAdhocInited) {
		// Delete fake PSP Thread
		if (threadAdhocID != 0) {
			__KernelStopThread(threadAdhocID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocThread stopped");
			__KernelDeleteThread(threadAdhocID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocThread deleted");
		}

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
		return 0;
	} else {
		// Seems to return this when called a second time after being terminated without another initialisation
		return SCE_KERNEL_ERROR_LWMUTEX_NOT_FOUND;
	}
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
			int j = 0; for (; j < 255 && i < count; j++)
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
						buf[i - 1].next = structAddr + ((i - 1) * sizeof(SceNetAdhocPdpStat)) + sizeof(SceNetAdhocPdpStat); //buf[i - 1].next = &buf[i];

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
			int j = 0; for (; j < 255 && i < count; j++) {
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
						buf[i - 1].next = structAddr + ((i - 1) * sizeof(SceNetAdhocPtpStat)) + sizeof(SceNetAdhocPtpStat);
					
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
	INFO_LOG(SCENET, "sceNetAdhocPtpOpen(%s,%d,%s,%d,%d,%d,%d,%d)", srcmac, sport, dstmac,dport,bufsize, rexmt_int, rexmt_cnt, unknown);
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)srcmac;
	SceNetEtherAddr * daddr = (SceNetEtherAddr *)dstmac;
	// Library is initialized
	if (netAdhocInited) {
		// Valid Addresses
		if (saddr != NULL && isLocalMAC(saddr) && daddr != NULL && !isBroadcastMAC(daddr)) {
			// Random Port required
			if (sport == 0) {
				// Find unused Port
				// while (sport == 0 || _IsPTPPortInUse(sport)) {
				// 	// Generate Port Number
				// 	sport = (uint16_t)_getRandomNumber(65535);
				// }
			}
			
			// Valid Ports
			if (!isPTPPortInUse(sport) && dport != 0) {
				// Valid Arguments
				if (bufsize > 0 && rexmt_int > 0 && rexmt_cnt > 0) {
					// Create Infrastructure Socket
					int tcpsocket = (int)INVALID_SOCKET;
					tcpsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
					
					// Valid Socket produced
					if (tcpsocket > 0) {
						// Change socket buffer size when necessary
						if (getSockBufferSize(tcpsocket, SO_SNDBUF) < bufsize) setSockBufferSize(tcpsocket, SO_SNDBUF, bufsize);
						if (getSockBufferSize(tcpsocket, SO_RCVBUF) < bufsize) setSockBufferSize(tcpsocket, SO_RCVBUF, bufsize);

						// Enable Port Re-use
						setsockopt(tcpsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
						
						// Binding Information for local Port
						sockaddr_in addr;
						// addr.sin_len = sizeof(addr);
						addr.sin_family = AF_INET;
						addr.sin_addr.s_addr = INADDR_ANY;
						addr.sin_port = htons(sport);
						
						// Bound Socket to local Port
						if (bind(tcpsocket, (sockaddr *)&addr, sizeof(addr)) == 0) {
							// Update sport with the port assigned by bind
							socklen_t len = sizeof(addr);
							if (getsockname(tcpsocket, (sockaddr *)&addr, &len) == 0) {
								sport = ntohs(addr.sin_port);
							}
							
							// Allocate Memory
							SceNetAdhocPtpStat * internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));
							
							// Allocated Memory
							if (internal != NULL) {
								// Find Free Translator ID
								int i = 0; for (; i < 255; i++) if (ptp[i] == NULL) break;
								
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
									
									// Add Port Forward to Router
									// sceNetPortOpen("TCP", sport);
									
									// Return PTP Socket Pointer
									return i + 1;
								}
								
								// Free Memory
								free(internal);
							}
						}
						
						// Close Socket
						closesocket(tcpsocket);
					}
				}
				
				// Invalid Arguments
				return ERROR_NET_ADHOC_INVALID_ARG;
			}
			
			// Invalid Ports
			return ERROR_NET_ADHOC_INVALID_PORT;
		}
		
		// Invalid Addresses
		return ERROR_NET_ADHOC_INVALID_ADDR;
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

	DEBUG_LOG(SCENET, "sceNetAdhocPtpAccept(%d,%08x,[%08x]=%u,%d,%u) at %08x", id, peerMacAddrPtr, peerPortPtr, port ? *port : -1, timeout, flag, currentMIPS->pc);
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
						changeBlockingMode(socket->id,1);
					}
					
					// Accept Connection
					int newsocket = accept(socket->id, (sockaddr *)&peeraddr, &peeraddrlen);
					
					// Blocking Behaviour
					if (!flag && newsocket == -1) {
						// Get Start Time
						uint32_t starttime = (uint32_t)(real_time_now()*1000000.0);
						
						// Retry until Timeout hits
						while ((timeout == 0 ||((uint32_t)(real_time_now()*1000000.0) - starttime) < (uint32_t)timeout) && newsocket == -1) {
							// Accept Connection
							newsocket = accept(socket->id, (sockaddr *)&peeraddr, &peeraddrlen);
							
							// Wait a bit...
							sleep_ms(1);
						}
					}

					if (newsocket == SOCKET_ERROR) {
						int error = errno;
						DEBUG_LOG(SCENET, "sceNetAdhocPtpAccept[%i]: Socket Error (%i)", id, error);
					}
					
					// Restore Blocking Behaviour
					if (nbio == 0) {
						// Restore Socket Option
						changeBlockingMode(socket->id,0);
					}
					
					// Accepted New Connection
					if (newsocket > 0) {
						// Do we need to Change socket buffer size to match the listener buffer size?

						// Enable Port Re-use
						setsockopt(newsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
						
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
									int i = 0; for (; i < 255; i++) if (ptp[i] == NULL) break;
									
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
										internal->lport = ntohs(local.sin_port);
										
										// Copy Peer Address Data to Structure
										internal->paddr = mac;
										internal->pport = ntohs(peeraddr.sin_port);
										
										// Set Connected State
										internal->state = ADHOC_PTP_STATE_ESTABLISHED;
										
										// Return Peer Address Information
										*addr = internal->paddr;
										if (port != NULL) *port = internal->pport;
										
										// Link PTP Socket
										ptp[i] = internal;
										
										// Add Port Forward to Router
										// sceNetPortOpen("TCP", internal->lport);

										uint8_t * pip = (uint8_t *)&peeraddr.sin_addr.s_addr;
										INFO_LOG(SCENET, "sceNetAdhocPtpAccept[%i->%i:%u]: Established (%u.%u.%u.%u:%u)", id, i+1, internal->lport, pip[0], pip[1], pip[2], pip[3], internal->pport);
										
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
	INFO_LOG(SCENET, "sceNetAdhocPtpConnect(%i, %i, %08x) at %08x", id, timeout, flag, currentMIPS->pc);
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
			
			// Valid Client Socket
			if (socket->state == 0) {
				// Target Address
				sockaddr_in sin;
				memset(&sin, 0, sizeof(sin));
				
				// Setup Target Address
				// sin.sin_len = sizeof(sin);
				sin.sin_family = AF_INET;
				sin.sin_port = htons(socket->pport);
				
				// Grab Peer IP
				if (resolveMAC(&socket->paddr, (uint32_t *)&sin.sin_addr.s_addr)) {
					// Grab Nonblocking Flag
					uint32_t nbio = getBlockingFlag(socket->id);
					// Switch to Nonblocking Behaviour
					if (nbio == 0) {
						// Overwrite Socket Option
						changeBlockingMode(socket->id, 1);
					}
					
					// Connect Socket to Peer (Nonblocking)
					int connectresult = connect(socket->id, (sockaddr *)&sin, sizeof(sin));
					
					// Grab Error Code
					int errorcode = errno;

					if (connectresult == SOCKET_ERROR) {
						ERROR_LOG(SCENET, "sceNetAdhocPtpConnect[%i]: Socket Error (%i)", id, errorcode);
					}
					
					// Restore Blocking Behaviour
					if (nbio == 0) {
						// Restore Socket Option
						changeBlockingMode(socket->id,0);
					}
					
					// Instant Connection (Lucky!)
					if (connectresult == 0 || (connectresult == -1 && (errorcode == EISCONN /*|| errorcode == EALREADY)*/))) {
						// Set Connected State
						socket->state = ADHOC_PTP_STATE_ESTABLISHED;
						
						INFO_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Already Connected", id, socket->lport);
						// Success
						return 0;
					}
					
					// Connection in Progress
					else if (connectresult == -1 && connectInProgress(errorcode)) {
						// Nonblocking Mode
						if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK;
						
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

								uint8_t * pip = (uint8_t *)&peer.sin_addr.s_addr;
								INFO_LOG(SCENET, "sceNetAdhocPtpConnect[%i:%u]: Established (%u.%u.%u.%u:%u)", id, socket->lport, pip[0], pip[1], pip[2], pip[3], socket->pport);
								
								// Success
								return 0;
							}
							
							// Timeout occured
							return ERROR_NET_ADHOC_TIMEOUT;
						}
					}
				}
				
				// Peer not found
				return ERROR_NET_ADHOC_CONNECTION_REFUSED;
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


/**
 * Adhoc Emulator PTP Socket Closer
 * @param id Socket File Descriptor
 * @param flag Bitflags (Unused)
 * @return 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED
 */
static int sceNetAdhocPtpClose(int id, int unknown) {
	INFO_LOG(SCENET,"sceNetAdhocPtpClose(%d,%d) at %08x",id,unknown,currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	// Library is initialized
	if (netAdhocInited) {
		// Valid Arguments & Atleast one Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPtpStat * socket = ptp[id - 1];
			
			// Close Connection
			closesocket(socket->id);
			
			// Remove Port Forward from Router
			// sceNetPortClose("TCP", socket->lport);
			
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
	INFO_LOG(SCENET, "sceNetAdhocPtpListen(%s,%d,%d,%d,%d,%d,%d)",srcmac,sport,bufsize,rexmt_int,rexmt_cnt,backlog,unk);
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	// Library is initialized
	SceNetEtherAddr * saddr = (SceNetEtherAddr *)srcmac;
	if (netAdhocInited) {
		// Valid Address
		if (saddr != NULL && isLocalMAC(saddr))
		{
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

						// Enable Port Re-use
						setsockopt(tcpsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
						
						// Binding Information for local Port
						sockaddr_in addr;
						addr.sin_family = AF_INET;
						addr.sin_addr.s_addr = INADDR_ANY;
						addr.sin_port = htons(sport);
						
						int iResult = 0;
						// Bound Socket to local Port
						if ((iResult = bind(tcpsocket, (sockaddr *)&addr, sizeof(addr))) == 0) {
							// Switch into Listening Mode
							if ((iResult = listen(tcpsocket, backlog)) == 0) {
								// Allocate Memory
								SceNetAdhocPtpStat * internal = (SceNetAdhocPtpStat *)malloc(sizeof(SceNetAdhocPtpStat));
								
								// Allocated Memory
								if (internal != NULL) {
									// Find Free Translator ID
									int i = 0; for (; i < 255; i++) if (ptp[i] == NULL) break;
									
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
										// sceNetPortOpen("TCP", sport);
										
										// Return PTP Socket Pointer
										return i + 1;
									}
									
									// Free Memory
									free(internal);
								}
							}
						}
						
						if (iResult == SOCKET_ERROR) {
							int error = errno;
							ERROR_LOG(SCENET, "sceNetAdhocPtpListen[%i]: Socket Error (%i)", sport, error);
						}

						// Close Socket
						closesocket(tcpsocket);

						// Port not available (exclusively in use?)
						return ERROR_NET_ADHOC_PORT_NOT_AVAIL;
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
					if (flag) timeout = 0;
					
					// Apply Send Timeout Settings to Socket
					setsockopt(socket->id, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
					
					// Acquire Network Lock
					// _acquireNetworkLock();
					
					// Send Data
					changeBlockingMode(socket->id, flag);
					int sent = send(socket->id, data, *len, 0);
					int error = errno;
					changeBlockingMode(socket->id, 0);
					
					// Free Network Lock
					// _freeNetworkLock();
					
					// Success
					if (sent > 0) {
						// Save Length
						*len = sent;

						uint8_t * smac = (uint8_t *)&socket->paddr;
						INFO_LOG(SCENET, "sceNetAdhocPtpSend[%i:%u]: Sent %u bytes to %02X:%02X:%02X:%02X:%02X:%02X:%u", id, socket->lport, sent, smac[0], smac[1], smac[2], smac[3], smac[4], smac[5], socket->pport);
						
						// Return Success
						return 0;
					}
					
					// Non-Critical Error
					else if (sent == -1 && error == EAGAIN) {
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
	DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv(%d,%08x,%08x,%d,%d)", id, dataAddr, dataSizeAddr, timeout, flag);
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
				if (flag) timeout = 0;
				
				// Apply Send Timeout Settings to Socket
				setsockopt(socket->id, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
				
				// Acquire Network Lock
				// _acquireNetworkLock();
				
				// Receive Data
				changeBlockingMode(socket->id, flag);
				int received = recv(socket->id, (char *)buf, *len, 0);
				int error = errno;
				changeBlockingMode(socket->id, 0);
				
				// Free Network Lock
				// _freeNetworkLock();
				
				// Received Data
				if (received > 0) {
					// Save Length
					*len = received;

					uint8_t * smac = (uint8_t *)&socket->paddr;
					INFO_LOG(SCENET, "sceNetAdhocPtpRecv[%i:%u]: Received %u bytes from %02X:%02X:%02X:%02X:%02X:%02X:%u", id, socket->lport, received, smac[0], smac[1], smac[2], smac[3], smac[4], smac[5], socket->pport);
					
					// Return Success
					return 0;
				}
				
				// Non-Critical Error
				else if (received == -1 && error == EAGAIN) {
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
			// Dummy Result
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
	return -1;
}

static int sceNetAdhocGameModeCreateReplica(const char *mac, u32 data, int size) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeCreateReplica(%s, %08x, %i) at %08x", mac, data, size, currentMIPS->pc);
	return -1;
}

static int sceNetAdhocGameModeUpdateMaster() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateMaster()");
	return -1;
}

static int sceNetAdhocGameModeDeleteMaster() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteMaster()");
	return -1;
}

static int sceNetAdhocGameModeUpdateReplica(int id, u32 infoAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateReplica(%i, %08x)", id, infoAddr);
	return -1;
}

static int sceNetAdhocGameModeDeleteReplica(int id) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteReplica(%i)", id);
	return -1;
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

int sceNetAdhocMatchingStop(int matchingId) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingStop(%i) at %08x", matchingId, currentMIPS->pc);
	//if (!g_Config.bEnableWlan)
	//	return -1;

	SceNetAdhocMatchingContext * item = findMatchingContext(matchingId);

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
		//__KernelStopThread(item->matching_thid, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocMatching stopped");
		//item->matchingThread->Terminate();

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

int sceNetAdhocMatchingDelete(int matchingId) {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?

	// Previous Context Reference
	SceNetAdhocMatchingContext * prev = NULL;

	// Multithreading Lock
	peerlock.lock(); //contextlock.lock();

	// Context Pointer
	SceNetAdhocMatchingContext * item = contexts;

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
				sceNetAdhocMatchingStop(matchingId);
			}
			// Delete the Fake PSP Thread
			//__KernelDeleteThread(item->matching_thid, SCE_KERNEL_ERROR_THREAD_TERMINATED, "AdhocMatching deleted");
			//delete item->matchingThread;
			// Make sure nobody locking/using the socket
			item->socketlock->lock();
			// Delete the socket
			sceNetAdhocPdpDelete(item->socket, 0); // item->connected = (sceNetAdhocPdpDelete(item->socket, 0) < 0);
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

	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingDelete(%i) at %08x", matchingId, currentMIPS->pc);
	return 0;
}

int sceNetAdhocMatchingInit(u32 memsize) {
	WARN_LOG(SCENET, "sceNetAdhocMatchingInit(%d) at %08x", memsize, currentMIPS->pc);
	
	// Uninitialized Library
	if (netAdhocMatchingInited) return ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED;
		
	// Save Fake Pool Size
	fakePoolSize = memsize;

	// Initialize Library
	netAdhocMatchingInited = true;

	// Return Success
	return 0;
}

int sceNetAdhocMatchingTerm() {
	// Should we cleanup all created matching contexts first? just in case there are games that doesn't delete them before calling this
	if (netAdhocMatchingInited) {
		// Delete all Matching contexts
		SceNetAdhocMatchingContext * next = NULL;
		SceNetAdhocMatchingContext * context = contexts; 
		while (context != NULL) {
			next = context->next;
			if (context->running) sceNetAdhocMatchingStop(context->id);
			sceNetAdhocMatchingDelete(context->id);
			context = next;
		}
	}
	
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingTerm()");
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
			if (rxbuflen >= 1024) {
				// Valid Arguments
				if (mode >= 1 && mode <= 3) {
					// Iterate Matching Contexts
					SceNetAdhocMatchingContext * item = contexts; for (; item != NULL; item = item->next) {
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
								context->timeout = (keepalive_int * init_count);
								if (context->timeout < 5000000) context->timeout = 5000000; // For internet play we need higher timeout than what the game wanted
								context->handler = handler;

								// Fill in Selfpeer
								context->mac = localmac;

								// Create locks
								context->socketlock = new recursive_mutex;
								context->eventlock = new recursive_mutex; 
								context->inputlock = new recursive_mutex; 

								// Create fake thread
								//#define PSP_THREAD_ATTR_KERNEL 0x00001000 // PSP_THREAD_ATTR_KERNEL is located in sceKernelThread.cpp instead of sceKernelThread.h :(
								//context->matching_thid = __KernelCreateThreadInternal("AdhocMatching", 0, idleThreadHackAddr, 0x7f, 4096, PSP_THREAD_ATTR_KERNEL);
								//context->matchingThread = new HLEHelperThread("AdhocMatching", "AdhocMatchingMod", "AdhocMatchingFunc", 0x7f, 4096); //8192

								// Multithreading Lock
								peerlock.lock(); //contextlock.lock();
								
								// Add Callback Handler
								context->handler.entryPoint = callbackAddr;

								// Link Context
								//context->connected = true;
								context->next = contexts;
								contexts = context;

								// Multithreading UnLock
								peerlock.unlock(); //contextlock.unlock();

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

// TODO: Should we execute the callback used to create the Matching Id if it's a valid address?
static int sceNetAdhocMatchingStart(int matchingId, int evthPri, int evthStack, int inthPri, int inthStack, int optLen, u32 optDataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingStart(%i, %i, %i, %i, %i, %i, %08x) at %08x", matchingId, evthPri, evthStack, inthPri, inthStack, optLen, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	// Multithreading Lock
	peerlock.lock();

	SceNetAdhocMatchingContext * item = findMatchingContext(matchingId);
	
	if (item != NULL) {
		//sceNetAdhocMatchingSetHelloOpt(matchingId, optLen, optDataAddr); //SetHelloOpt only works when context is running
		if ((optLen > 0) && Memory::IsValidAddress(optDataAddr)) {
			// Allocate the memory and copy the content
			if (item->hello != NULL) free(item->hello);
			item->hello = (uint8_t *)malloc(optLen);
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

		// Start the Fake PSP Thread
		//__KernelStartThread(item->matching_thid, 0, 0);
		//item->matchingThread->Start(0, 0);

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

static int sceNetAdhocMatchingSelectTarget(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSelectTarget(%i, %s, %i, %08x) at %08x", matchingId, macAddress, optLen, optDataPtr, currentMIPS->pc);
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
						if ((optLen == 0 && optDataPtr == 0) || (optLen > 0 && optDataPtr != 0))
						{
							void * opt = NULL;
							if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointer(optDataPtr);
							// Host Mode
							if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT)
							{
								// Already Connected
								if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) return ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED;

								// Not enough space
								if (countChildren(context) == (context->maxpeers - 1)) return ERROR_NET_ADHOC_MATCHING_EXCEED_MAXNUM;

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
								if (findParent(context) != NULL) return ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED;

								// Outgoing Request in Progress
								if (findOutgoingRequest(context) != NULL) return ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS;

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
								if (findP2P(context) != NULL) return ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED;

								// Outgoing Request in Progress
								if (findOutgoingRequest(context) != NULL) return ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS;

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
							return ERROR_NET_ADHOC_MATCHING_TARGET_NOT_READY;
						}

						// Invalid Optional Data Length
						return ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN;
					}

					// Peer not found
					return ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET;
				}

				// Idle Context
				return ERROR_NET_ADHOC_MATCHING_NOT_RUNNING;
			}

			// Invalid Matching ID
			return ERROR_NET_ADHOC_MATCHING_INVALID_ID;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
}

int sceNetAdhocMatchingCancelTargetWithOpt(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingCancelTargetWithOpt(%i, %s, %i, %08x) at %08x", matchingId, macAddress, optLen, optDataPtr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Initialized Library
	if (netAdhocMatchingInited)
	{
		SceNetEtherAddr * target = (SceNetEtherAddr *)macAddress;
		void * opt = NULL;
		if (Memory::IsValidAddress(optDataPtr)) opt = Memory::GetPointer(optDataPtr);

		// Valid Arguments
		if (target != NULL && ((optLen == 0 && opt == NULL) || (optLen > 0 && opt != NULL)))
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

							// Return Success
							return 0;
						}
					}

					// Peer not found
					//return ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET;
					// Faking success to prevent the game (ie. Soul Calibur) to repeatedly calling this function when the other player is disconnected
					return 0;
				}

				// Context not running
				return ERROR_NET_ADHOC_MATCHING_NOT_RUNNING;
			}

			// Invalid Matching ID
			return ERROR_NET_ADHOC_MATCHING_INVALID_ID;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
}

int sceNetAdhocMatchingCancelTarget(int matchingId, const char *macAddress) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingCancelTarget(%i, %s)", matchingId, macAddress);
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
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSetHelloOpt(%i, %i, %08x) at %08x", matchingId, optLenAddr, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	if (!netAdhocMatchingInited) return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
	
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
				if ((optLenAddr == 0 && optDataAddr == 0) || (optLenAddr > 0 && optDataAddr != 0))
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

	if (!netAdhocMatchingInited) return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;

	// Minimum Argument
	if (!Memory::IsValidAddress(sizeAddr)) return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;

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

				// Number of Connected Peers
				uint32_t peercount = countConnectedPeers(context);

				// Calculate Connected Peer Bytesize
				int available = sizeof(SceNetAdhocMatchingMemberInfoEmu) * peercount;

				// Length Returner Mode
				if (buf == 0)
				{
					// Get Connected Peer Count
					*buflen = available;
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

						// Room for more than local peer
						if (requestedpeers > 1)
						{
							// P2P Mode
							if (context->mode == PSP_ADHOC_MATCHING_MODE_P2P)
							{
								// Find P2P Brother
								SceNetAdhocMatchingMemberInternal * p2p = findP2P(context);

								// P2P Brother found
								if (p2p != NULL)
								{
									// Add P2P Brother MAC
									buf2[filledpeers++].mac_addr = p2p->mac;
								}
							}

							// Parent or Child Mode
							else
							{
								// Iterate Peer List
								SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL && filledpeers < requestedpeers; peer = peer->next)
								{
									// Parent Mode
									if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT)
									{
										// Interested in Children (Michael Jackson Style)
										if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD)
										{
											// Add Child MAC
											buf2[filledpeers++].mac_addr = peer->mac;
										}
									}

									// Children Mode
									else
									{
										// Interested in Parent & Siblings
										if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD || peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)
										{
											// Add Peer MAC
											buf2[filledpeers++].mac_addr = peer->mac;
										}
									}
								}
							}

						}

						// Link Result List
						int i = 0;
						for (; i < filledpeers - 1; i++)
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
				}

				// Return Success
				return 0;
			}

			// Invalid Arguments
			return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
		}

		// Context not running
		return ERROR_NET_ADHOC_MATCHING_NOT_RUNNING;
	}

	// Invalid Matching ID
	return ERROR_NET_ADHOC_MATCHING_INVALID_ID;
}

int sceNetAdhocMatchingSendData(int matchingId, const char *mac, int dataLen, u32 dataAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSendData(%i, %s, %i, %08x)", matchingId, mac, dataLen, dataAddr);
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
							return ERROR_NET_ADHOC_MATCHING_NOT_ESTABLISHED;
						}

						// Invalid Data Length
						return ERROR_NET_ADHOC_MATCHING_INVALID_DATALEN;
					}

					// Peer not found
					return ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET;
				}

				// Context not running
				return ERROR_NET_ADHOC_MATCHING_NOT_RUNNING;
			}

			// Invalid Matching ID
			return ERROR_NET_ADHOC_MATCHING_INVALID_ID;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
}

int sceNetAdhocMatchingAbortSendData(int matchingId, const char *mac) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingAbortSendData(%i, %s)", matchingId, mac);
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
					return ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET;
				}

				// Context not running
				return ERROR_NET_ADHOC_MATCHING_NOT_RUNNING;
			}

			// Invalid Matching ID
			return ERROR_NET_ADHOC_MATCHING_INVALID_ID;
		}

		// Invalid Arguments
		return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
	}

	// Uninitialized Library
	return ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED;
}

static int sceNetAdhocMatchingGetPoolMaxAlloc() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetPoolMaxAlloc()");
	if (!g_Config.bEnableWlan)
		return -1;
	
	// Lazy way out - hardcoded return value
	return (50 * 1024);
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
			poolstat->maximum = fakePoolSize / 8 * 6;
			poolstat->free = fakePoolSize / 8 * 7;

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
	{
		lock_guard adhocGuard(adhocEvtMtx);

		for (auto &params : adhocctlEvents)
		{
			int flags = params.first;
			int error = params.second;
			u32_le args[3] = { 0, 0, 0 };
			args[0] = flags;
			args[1] = error;

			for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
				args[2] = it->second.argument;
				__KernelDirectMipsCall(it->second.entryPoint, NULL, args, 3, true);
			}
		}
		adhocctlEvents.clear();

		for (auto &param : matchingEvents)
		{
			u32_le *args = (u32_le *) param;
			AfterMatchingMipsCall *after = (AfterMatchingMipsCall *) __KernelCreateAction(actionAfterMatchingMipsCall);
			after->SetContextID(args[0], args[1]);
			__KernelDirectMipsCall(args[5], after, args, 5, true);
		}
		matchingEvents.clear();
	}
	//magically make this work
	hleDelayResult(0, "Prevent Adhoc thread from blocking", 1000);
}

const HLEFunction sceNetAdhoc[] = {
	{0XE1D621D7, &WrapU_V<sceNetAdhocInit>,                            "sceNetAdhocInit",                        'x', ""         },
	{0XA62C6F57, &WrapI_V<sceNetAdhocTerm>,                            "sceNetAdhocTerm",                        'i', ""         },
	{0X0AD043ED, &WrapI_U<sceNetAdhocctlConnect>,                      "sceNetAdhocctlConnect",                  'i', "x"        },
	{0X6F92741B, &WrapI_CUIU<sceNetAdhocPdpCreate>,                    "sceNetAdhocPdpCreate",                   'i', "sxix"     },
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
};

static int sceNetAdhocctlExitGameMode() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlExitGameMode()");
	return 0;
}

static int sceNetAdhocctlGetGameModeInfo(u32 infoAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetGameModeInfo(%08x)", infoAddr);
	return -1;
}

static int sceNetAdhocctlGetPeerList(u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL;
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);
	SceNetAdhocctlPeerInfoEmu *buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointer(bufAddr);

	DEBUG_LOG(SCENET, "sceNetAdhocctlGetPeerList([%08x]=%i, %08x) at %08x", sizeAddr, buflen ? *buflen : -1, bufAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	// Initialized Library
	if (netAdhocctlInited) {
		// Minimum Arguments
		if (buflen != NULL) {
			// Multithreading Lock
			peerlock.lock();

			// Length Calculation Mode
			if (buf == NULL) *buflen = getActivePeerCount() * sizeof(SceNetAdhocctlPeerInfoEmu);

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
						// Fake Receive Time
						if (peer->last_recv != 0) peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();

						// Copy Peer Info
						buf[discovered].nickname = peer->nickname;
						buf[discovered].mac_addr = peer->mac_addr;
						buf[discovered].ip_addr = peer->ip_addr;
						buf[discovered].last_recv = peer->last_recv;
						discovered++;

					}

					// Link List
					int i = 0; for (; i < discovered - 1; i++) {
						// Link Network
						buf[i].next = bufAddr+(sizeof(SceNetAdhocctlPeerInfoEmu)*i) + sizeof(SceNetAdhocctlPeerInfoEmu);
					}
					// Fix Last Element
					if (discovered > 0) buf[discovered - 1].next = 0;
				}

				// Fix Size
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

	// Uninitialized Library
	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

static int sceNetAdhocctlGetAddrByName(const char *nickName, u32 sizeAddr, u32 bufAddr) {
	s32_le *buflen = NULL; //int32_t
	if (Memory::IsValidAddress(sizeAddr)) buflen = (s32_le *)Memory::GetPointer(sizeAddr);
	
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlGetPeerList(%s, [%08x]=%i, %08x)", nickName, sizeAddr, buflen ? *buflen : -1, bufAddr);
	
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
					if (strcmp((char *)parameter.nickname.data, nickName) == 0)
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
						if (strcmp((char *)peer->nickname.data, nickName) == 0)
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
					int i = 0; for (; i < discovered - 1; i++)
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

int sceNetAdhocDiscoverRequestSuspend(void)
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
		hello = (uint8_t *)realloc(hello, 5 + context->hellolen);
		len = context->hellolen;
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
		uint8_t * accept = (uint8_t *)malloc(9 + optlen + siblingbuflen);

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
				SceNetAdhocMatchingMemberInternal * item = context->peerlist; for (; item != NULL; item = item->next)
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
		uint8_t * join = (uint8_t *)malloc(5 + optlen);

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
	uint8_t * cancel = (uint8_t *)malloc(5 + optlen);

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
		uint8_t * send = (uint8_t *)malloc(5 + datalen);

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
		SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL; peer = peer->next)
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
					INFO_LOG(SCENET, "InputLoop: Sending BIRTH to %02X:%02X:%02X:%02X:%02X:%02X", peer->mac.data[0], peer->mac.data[1], peer->mac.data[2], peer->mac.data[3], peer->mac.data[4], peer->mac.data[5]); 
				else
					WARN_LOG(SCENET, "InputLoop: Failed to Send BIRTH to %02X:%02X:%02X:%02X:%02X:%02X", peer->mac.data[0], peer->mac.data[1], peer->mac.data[2], peer->mac.data[3], peer->mac.data[4], peer->mac.data[5]);
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
		SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL; peer = peer->next)
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
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; for (; peer != NULL; peer = peer->next)
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

						// Link Peer into List
						peer->next = context->peerlist;
						context->peerlist = peer;
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
					if (peer != NULL && context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) return;

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

							// Link Peer into List
							peer->next = context->peerlist;
							context->peerlist = peer;

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
		INFO_LOG(SCENET, "Join Event(2) Rejected");
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
				if (optlen >= 0 && length >= (9 + optlen + sizeof(SceNetEtherAddr) * siblingcount))
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
						SceNetAdhocMatchingMemberInternal * item = context->peerlist; for (; item != NULL; item = item->next)
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
				peer->lastping = CoreTiming::GetGlobalTimeUsScaled(); //real_time_now()*1000000.0;

				peerlock.lock();

				// Link Peer
				sibling->next = context->peerlist;
				context->peerlist = sibling;

				peerlock.unlock();

				// Spawn Established Event
				spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, &sibling->mac, 0, NULL);
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
		}

		// Parent Bye
		else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_PARENT)
		{
			// Iterate Peers
			SceNetAdhocMatchingMemberInternal * item = context->peerlist; for (; item != NULL; item = item->next)
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
	u32 bufLen = 0;
	u32 bufAddr = 0;

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
		//static u32_le args[5] = { 0, 0, 0, 0, 0 }; // Need to be global/static so it can be accessed from a different thread
		u32_le * args = context->handlerArgs;

		while (context->eventRunning) // (context->eventThread.get_id() != NULL)
		{
			// Messages on Stack ready for processing
			if (context->event_stack != NULL)
			{
				// Claim Stack
				context->eventlock->lock();

				// Iterate Message List
				ThreadMessage * msg = context->event_stack; for (; msg != NULL; msg = msg->next)
				{
					// Default Optional Data
					void * opt = NULL;

					// Grab Optional Data
					if (msg->optlen > 0) opt = ((u8 *)msg) + sizeof(ThreadMessage); //&msg[1]

					// Log Matching Events
					INFO_LOG(SCENET, "EventLoop[%d]: Matching Event [%d=%s] OptSize=%d", matchingId, msg->opcode, getMatchingEventStr(msg->opcode), msg->optlen);

					context->eventlock->unlock(); // Unlock to prevent race-condition with other threads due to recursive lock
					// Call Event Handler
					//context->handler(context->id, msg->opcode, &msg->mac, msg->optlen, opt);
					// Notify Event Handlers
					notifyMatchingHandler(context, msg, opt, bufAddr, bufLen, args);
					context->eventlock->lock(); // Lock again
				}

				// Clear Event Message Stack
				clearStack(context, PSP_ADHOC_MATCHING_EVENT_STACK);

				// Free Stack
				context->eventlock->unlock();
			}

			// Share CPU Time
			sleep_ms(1); //10 //sceKernelDelayThread(10000);

			// Don't do anything if it's paused, otherwise the log will be flooded
			while (Core_IsStepping() && context->eventRunning) sleep_ms(1);
		}

		// Process Last Messages
		if (context->event_stack != NULL)
		{
			// Claim Stack
			context->eventlock->lock();

			// Iterate Message List
			ThreadMessage * msg = context->event_stack; for (; msg != NULL; msg = msg->next)
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

		// Delete Pointer Reference (and notify caller about finished cleanup)
		//context->eventThread = NULL;
	}

	// Log Shutdown
	INFO_LOG(SCENET, "EventLoop: End of EventLoop[%i] Thread", matchingId);

	// Free memory
	if (Memory::IsValidAddress(bufAddr)) userMemory.Free(bufAddr);

	// Return Zero to shut up Compiler (never reached anyway)
	return 0;
}

/**
* Matching IO Handler Thread
* @param args sizeof(SceNetAdhocMatchingContext *)
* @param argp SceNetAdhocMatchingContext *
* @return Exit Point is never reached...
*/
int matchingInputThread(int matchingId) 
{
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
		while (context->inputRunning)
		{
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
				ThreadMessage * msg = context->input_stack; 
				for (; msg != NULL; msg = msg->next)
				{
					// Default Optional Data
					void * opt = NULL;

					// Grab Optional Data
					if (msg->optlen > 0) opt = ((u8 *)msg) + sizeof(ThreadMessage);

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

					// Cancel Bulk Data Transfer (does nothing as of now as we fire and forget anyway) // Do we need to check DeathPacket and BytePacket here?
					// else if(msg->opcode == PSP_ADHOC_MATCHING_PACKET_BULK_ABORT) blabla;

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
				SceNetAdhocctlPeerInfo * peer = findFriend(&sendermac);
				if (peer != NULL) {
					now = CoreTiming::GetGlobalTimeUsScaled();
					u64_le delta = now - peer->last_recv;
					DEBUG_LOG(SCENET, "Timestamp Delta: %llu (%llu - %llu)", delta, now, peer->last_recv);
					if (/*context->rxbuf[0] > 0 &&*/ peer->last_recv != 0) peer->last_recv = now; // - context->keepalive_int; // May need to deduce by ping interval to prevent Dissidia 012 unable to see other players (ie. disappearing issue)
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

			// Share CPU Time
			sleep_ms(1); //10 //sceKernelDelayThread(10000);

			// Don't do anything if it's paused, otherwise the log will be flooded
			while (Core_IsStepping() && context->inputRunning) sleep_ms(1);
		}

		// Send Bye Messages
		sendByePacket(context);

		// Free Peer List Buffer
		clearPeerList(context); //deleteAllMembers(context);

		// Delete Pointer Reference (and notify caller about finished cleanup)
		//context->inputThread = NULL;
	}

	// Log Shutdown
	INFO_LOG(SCENET, "InputLoop: End of InputLoop[%i] Thread", matchingId);

	// Terminate Thread
	//sceKernelExitDeleteThread(0);

	// Return Zero to shut up Compiler (never reached anyway)
	return 0;
}

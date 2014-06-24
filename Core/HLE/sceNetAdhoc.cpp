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
#include "Common/ChunkFile.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/proAdhoc.h"
#include "Core/MemMap.h"

enum {
	ERROR_NET_ADHOC_INVALID_SOCKET_ID            = 0x80410701,
	ERROR_NET_ADHOC_INVALID_ADDR                 = 0x80410702,
	ERROR_NET_ADHOC_INVALID_ARG                  = 0x80410B04,
	ERROR_NET_ADHOC_NO_DATA_AVAILABLE            = 0x80410709,
	ERROR_NET_ADHOC_PORT_IN_USE                  = 0x8041070a,
	ERROR_NET_ADHOC_NOT_INITIALIZED              = 0x80410712,
	ERROR_NET_ADHOC_ALREADY_INITIALIZED          = 0x80410713,
	ERROR_NET_ADHOC_DISCONNECTED                 = 0x8041070c,
	ERROR_NET_ADHOC_TIMEOUT                      = 0x80410715,
	ERROR_NET_ADHOC_NO_ENTRY                     = 0x80410716,
	ERROR_NET_ADHOC_CONNECTION_REFUSED           = 0x80410718,
	ERROR_NET_ADHOC_INVALID_MATCHING_ID          = 0x80410807,
	ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED = 0x80410812,
	ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED     = 0x80410813,
	ERROR_NET_ADHOC_WOULD_BLOCK                  = 0x80410709,
	ERROR_NET_ADHOC_INVALID_DATALEN              = 0x80410705,
	ERROR_NET_ADHOC_INVALID_PORT                 = 0x80410703,
	ERROR_NET_ADHOC_NOT_LISTENED                 = 0x8040070E,
	ERROR_NET_ADHOC_NOT_OPENED                   = 0x8040070D,
	ERROR_NET_ADHOC_SOCKET_ID_NOT_AVAIL          = 0x8041070F,
	ERROR_NET_ADHOC_NOT_CONNECTED                = 0x8041070B,

	ERROR_NET_ADHOCCTL_INVALID_ARG               = 0x80410B04,
	ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF           = 0x80410b03,
	ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED       = 0x80410b07,
	ERROR_NET_ADHOCCTL_NOT_INITIALIZED           = 0x80410b08,
	ERROR_NET_ADHOCCTL_DISCONNECTED              = 0x80410b09,
	ERROR_NET_ADHOCCTL_BUSY                      = 0x80410b10,
	ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS         = 0x80410b12, 

	ERROR_NET_ADHOC_MATCHING_INVALID_MODE        = 0x80410801,
	ERROR_NET_ADHOC_MATCHING_INVALID_MAXNUM      = 0x80410803,
	ERROR_NET_ADHOC_MATCHING_RXBUF_TOO_SHORT     = 0x80410804,
	ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN      = 0x80410805,
	ERROR_NET_ADHOC_MATCHING_INVALID_ARG         = 0x80410806,
	ERROR_NET_ADHOC_MATCHING_INVALID_ID          = 0x80410807,
	ERROR_NET_ADHOC_MATCHING_ID_NOT_AVAIL        = 0x80410808,
	ERROR_NET_ADHOC_MATCHING_NO_SPACE            = 0x80410809,
	ERROR_NET_ADHOC_MATCHING_IS_RUNNING          = 0x8041080A,
	ERROR_NET_ADHOC_MATCHING_NOT_RUNNING         = 0x8041080B,
	ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET      = 0x8041080C,
	ERROR_NET_ADHOC_MATCHING_TARGET_NOT_READY    = 0x8041080D,
	ERROR_NET_ADHOC_MATCHING_EXCEED_MAXNUM       = 0x8041080E,
	ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS = 0x8041080F,
	ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED = 0x80410810,
	ERROR_NET_ADHOC_MATCHING_BUSY                = 0x80410811,
	ERROR_NET_ADHOC_MATCHING_PORT_IN_USE         = 0x80410814,
	ERROR_NET_ADHOC_MATCHING_STACKSIZE_TOO_SHORT = 0x80410815,
	ERROR_NET_ADHOC_MATCHING_INVALID_DATALEN     = 0x80410816,
	ERROR_NET_ADHOC_MATCHING_NOT_ESTABLISHED     = 0x80410817,
	ERROR_NET_ADHOC_MATCHING_DATA_BUSY           = 0x80410818,

	ERROR_NET_NO_SPACE                           = 0x80410001 
};

enum {
	PSP_ADHOC_POLL_READY_TO_SEND  = 1,
	PSP_ADHOC_POLL_DATA_AVAILABLE = 2,
	PSP_ADHOC_POLL_CAN_CONNECT    = 4,
	PSP_ADHOC_POLL_CAN_ACCEPT     = 8,
};

enum {
/**
* Matching events used in pspAdhocMatchingCallback
*/
	/** Hello event. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_HELLO = 1,
	/** Join request. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_JOIN = 2,
	/** Target left matching. */
	PSP_ADHOC_MATCHING_EVENT_LEFT = 3,
	/** Join request rejected. */
	PSP_ADHOC_MATCHING_EVENT_REJECT = 4,
	/** Join request cancelled. */
	PSP_ADHOC_MATCHING_EVENT_CANCEL = 5,
	/** Join request accepted. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_ACCEPT = 6,
	/** Matching is complete. */
	PSP_ADHOC_MATCHING_EVENT_COMPLETE = 7,
	/** Ping timeout event. */
	PSP_ADHOC_MATCHING_EVENT_TIMEOUT = 8,
	/** Error event. */
	PSP_ADHOC_MATCHING_EVENT_ERROR = 9,
	/** Peer disconnect event. */
	PSP_ADHOC_MATCHING_EVENT_DISCONNECT = 10,
	/** Data received event. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_DATA = 11,
	/** Data acknowledged event. */
	PSP_ADHOC_MATCHING_EVENT_DATA_CONFIRM = 12,
	/** Data timeout event. */
	PSP_ADHOC_MATCHING_EVENT_DATA_TIMEOUT = 13,

	/** Internal ping message. */
	PSP_ADHOC_MATCHING_EVENT_INTERNAL_PING = 100,

	/**
	* Matching modes used in sceNetAdhocMatchingCreate
	*/
	/** Host */
	PSP_ADHOC_MATCHING_MODE_HOST = 1,
	/** Client */
	PSP_ADHOC_MATCHING_MODE_CLIENT = 2,
	/** Peer to peer */
	PSP_ADHOC_MATCHING_MODE_PTP = 3,
};

const size_t MAX_ADHOCCTL_HANDLERS = 32;

// shared in sceNetAdhoc.h since it need to be used from sceNet.cpp also
/*static*/ bool netAdhocInited;
/*static*/ bool netAdhocctlInited;
static bool netAdhocMatchingInited;

struct AdhocctlHandler {
	u32 entryPoint;
	u32 argument;
};

static std::map<int, AdhocctlHandler> adhocctlHandlers;

void __NetAdhocInit() {
	friendFinderRunning = false;
	eventHandlerUpdate = -1;
	netAdhocInited = false;
	netAdhocctlInited = false;
	netAdhocMatchingInited = false;
	adhocctlHandlers.clear();
}


int sceNetAdhocTerm();
int sceNetAdhocctlTerm();
int sceNetAdhocMatchingTerm();
int sceNetAdhocMatchingSetHelloOpt(int matchingId, int optLenAddr, u32 optDataAddr);

void __NetAdhocShutdown() {
	// Checks to avoid confusing logspam
	if (netAdhocInited) {
		sceNetAdhocTerm();
	}
	if (netAdhocctlInited) {
		sceNetAdhocctlTerm();
	}
	if (netAdhocMatchingInited) {
		sceNetAdhocMatchingTerm();
	}
}

void __NetAdhocDoState(PointerWrap &p) {
	auto s = p.Section("sceNetAdhoc", 1);
	if (!s)
		return;

	p.Do(netAdhocInited);
	p.Do(netAdhocctlInited);
	p.Do(netAdhocMatchingInited);
	p.Do(adhocctlHandlers);
}

void __UpdateAdhocctlHandlers(int flag, int error) {
	u32 args[3] = { 0, 0, 0 };
	args[0] = flag;
	args[1] = error;

	for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
		args[2] = it->second.argument;
		__KernelDirectMipsCall(it->second.entryPoint, NULL, args, 3, true);
	}
}

int getBlockingFlag(int id) {
#ifdef _MSC_VER
	return 0;
#else
	int sockflag = fcntl(id, F_GETFL, O_NONBLOCK);
	return sockflag & O_NONBLOCK;
#endif
}

void __handlerUpdateCallback(u64 userdata, int cycleslate) {
	int buff[2];
	split64(userdata,buff);
	__UpdateAdhocctlHandlers(buff[0], buff[1]);
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

		// Return Success
		return 0;
	}
	// Already initialized
	return ERROR_NET_ADHOC_ALREADY_INITIALIZED;
}

u32 sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlInit(%i, %i, %08x) at %08x", stackSize, prio, productAddr, currentMIPS->pc);
	if (netAdhocctlInited) {
		return ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED;
	} else if (!g_Config.bEnableWlan) {
		// Pretend success but don't actually start the friendfinder thread and stuff.
		// Dunno if this is the way to go...
		netAdhocctlInited = true;
		return 0;
	} else if (initNetwork((SceNetAdhocctlAdhocId *)Memory::GetPointer(productAddr)) == 0) {
		netAdhocctlInited = true;
		eventHandlerUpdate = CoreTiming::RegisterEvent("HandlerUpdateEvent", __handlerUpdateCallback);
		friendFinderRunning = true;
		friendFinderThread = std::thread(friendFinder);
	} else {
		//WARN_LOG(SCENET, "sceNetAdhocctlInit: Failed to initialize");
		//return -1; // ERROR_NET_ADHOCCTL_NOT_INITIALIZED; //returning success while initNetwork failed to connect to ProServer may cause some games (ie. GTA:VCS) to behave strangely/stuck, but if not success some games (ie. Ford Street Racing) will also stuck
		WARN_LOG(SCENET, "sceNetAdhocctlInit: Faking success");
		//return 0; // Generic error, but just return success to make games conform.
	}
	netAdhocctlInited = true; //needed for cleanup during ctlTerm even when it failed to connect to Adhoc Server (since it's being faked as success)
	return 0;
}

int sceNetAdhocctlGetState(u32 ptrToStatus) {
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
int sceNetAdhocPdpCreate(const char *mac, u32 port, int bufferSize, u32 unknown) {
	INFO_LOG(SCENET, "sceNetAdhocPdpCreate(%08x, %u, %u, %u) at %08x", mac, port, bufferSize, unknown, currentMIPS->pc);
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
					// Enable Port Re-use
					//setsockopt(usocket, SOL_SOCKET, SO_REUSEADDR, &_one, sizeof(_one)); NO idea if we need this
					// Binding Information for local Port
					sockaddr_in addr;
					addr.sin_family = AF_INET;
					addr.sin_addr.s_addr = INADDR_ANY;

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
								internal->lport = ntohs(getLocalPort(usocket)); // port; //should use the port given to the socket (in case it's UNUSED_PORT port) isn't?
								internal->rcv_sb_cc = bufferSize;

								// Link Socket to Translator ID
								pdp[i] = internal;

								// Forward Port on Router
								//sceNetPortOpen("UDP", sport); // I need to figure out how to use this in windows/linux

								// Wait for Status to be connected to prevent Wipeout Pulse from showing "WLAN is turned off"
								if ((i == 0) && friendFinderRunning) {
									int cnt = 0;
									while ((threadStatus != ADHOCCTL_STATE_CONNECTED) && (cnt < 5000)) {
										sleep_ms(1);
										cnt++;
									}
								}
								
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
int sceNetAdhocctlGetParameter(u32 paramAddr) {
	DEBUG_LOG(SCENET, "sceNetAdhocctlGetParameter(%u)",paramAddr);
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
int sceNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPdpSend(%i, %08x, %i, %p, %i, %i, %i)", id, mac, port, data, len, timeout, flag);
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	SceNetEtherAddr * daddr = (SceNetEtherAddr *)mac;
	uint16 dport = (uint16)port;

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
								if (resolveMAC((SceNetEtherAddr *)daddr, (uint32_t *)&target.sin_addr.s_addr) == 0) {
									// Acquire Network Lock
									//_acquireNetworkLock();

									// Send Data
									changeBlockingMode(socket->id, flag);
									int sent = sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
									if (sent == SOCKET_ERROR) {
										ERROR_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend", errno);
									}
									changeBlockingMode(socket->id, 0);

									// Free Network Lock
									//_freeNetworkLock();

									uint8_t * sip = (uint8_t *)&target.sin_addr.s_addr;
									// Sent Data
									if (sent == len) {
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Sent %u bytes to %u.%u.%u.%u:%u", socket->id, ntohs(getLocalPort(socket->id)), sent, sip[0], sip[1], sip[2], sip[3], ntohs(target.sin_port));

										// Return sent size
										return sent;
										// Success
										//return 0;
									}
									// Partially Sent Data (when non-blocking socket's send buffer is smaller than len)
									else if (flag && (sent >= 0)) {
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u]: Partial Sent %u bytes to %u.%u.%u.%u:%u", socket->id, ntohs(getLocalPort(socket->id)), sent, sip[0], sip[1], sip[2], sip[3], ntohs(target.sin_port));
									
										// Return sent size
										return sent;
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
								int maxsent = 0;

								// Acquire Peer Lock
								peerlock.lock();

								// Iterate Peers
								SceNetAdhocctlPeerInfo * peer = friends;
								for (; peer != NULL; peer = peer->next) {
									// Fill in Target Structure
									sockaddr_in target;
									target.sin_family = AF_INET;
									target.sin_addr.s_addr = peer->ip_addr;
									target.sin_port = htons(dport);

									uint8_t * thing = (uint8_t *)&peer->ip_addr;
									// printf("Attempting PDP Send to %u.%u.%u.%u on Port %u\n", thing[0], thing[1], thing[2], thing[3], dport);
									// Send Data
									changeBlockingMode(socket->id, flag);
									int sent = sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
									if (sent > maxsent) maxsent = sent;
									if (sent == SOCKET_ERROR) {
										ERROR_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpSend[i%](Broadcast)", errno, socket->id);
									}
									changeBlockingMode(socket->id, 0);
									if (sent >= 0) {
										uint8_t * sip = (uint8_t *)&target.sin_addr.s_addr;
										DEBUG_LOG(SCENET, "sceNetAdhocPdpSend[%i:%u](BC): Sent %u bytes to %u.%u.%u.%u:%u", socket->id, ntohs(getLocalPort(socket->id)), sent, sip[0], sip[1], sip[2], sip[3], ntohs(target.sin_port));
									}
								}

								// Free Peer Lock
								peerlock.unlock();

								// Free Network Lock
								//_freeNetworkLock();

								// Broadcast never fails!
								//return 0;
								// Return sent size
								return maxsent;
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
int sceNetAdhocPdpRecv(int id, void *addr, void * port, void *buf, void *dataLength, u32 timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv(%i, %p, %p, %p, %p, %i, %i) at %08x", id, addr, port, buf, dataLength, timeout, flag, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	SceNetEtherAddr *saddr = (SceNetEtherAddr *)addr;
	uint32_t * sport = (uint32_t *)port;  // uint16_t * sport = (uint16_t *)port; //Looking at Quake3 sourcecode (net_adhoc.c) this should be 32bit isn't?
	int * len = (int *)dataLength;
	if (netAdhocInited) {
		// Valid Socket ID
		if (id > 0 && id <= 255 && pdp[id - 1] != NULL) {
			// Cast Socket
			SceNetAdhocPdpStat * socket = pdp[id - 1];

			// Valid Arguments
			if (saddr != NULL && port != NULL && buf != NULL && len != NULL && *len >= 0) { // since it's possible to recv 0 size, should we allow 0 size also?
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
				changeBlockingMode(socket->id,flag);
				int received = recvfrom(socket->id, (char *)buf, *len,0,(sockaddr *)&sin, &sinlen);
				/*if (received == SOCKET_ERROR) {
					ERROR_LOG(SCENET, "Socket Error (%i) on sceNetAdhocPdpRecv", errno);
				}*/
				changeBlockingMode(socket->id, 0); 

				// Received Data
				if (received >= 0) { // (received > 0)
					uint8_t * sip = (uint8_t *)&sin.sin_addr.s_addr;
					DEBUG_LOG(SCENET, "sceNetAdhocPdpRecv[%i:%u]: Received %u bytes from %u.%u.%u.%u:%u", socket->id, ntohs(getLocalPort(socket->id)), received, sip[0], sip[1], sip[2], sip[3], ntohs(sin.sin_port));

					// Peer MAC
					SceNetEtherAddr mac;

					// Find Peer MAC
					if (resolveIP(sin.sin_addr.s_addr, &mac) == 0) {
						// Provide Sender Information
						*saddr = mac;
						*sport = ntohs(sin.sin_port); //htons(sin.sin_port);

						// Save Length
						*len = received;

						// Free Network Lock
						//_freeNetworkLock();

						// Return Success
						//return 0;
					}

					//Return Received len
					return received;
				}

				// Free Network Lock
				//_freeNetworkLock();

#ifdef PDP_DIRTY_MAGIC
				// Restore Nonblocking Flag for Return Value
				if (wouldblock) flag = 1;
#endif

				// Nothing received
				if (flag) return ERROR_NET_ADHOC_WOULD_BLOCK; //ERROR_NET_ADHOC_NO_DATA_AVAILABLE;
				return ERROR_NET_ADHOC_TIMEOUT; //
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

// Assuming < 0 for failure, homebrew SDK doesn't have much to say about this one..
int sceNetAdhocSetSocketAlert(int id, int flag) {
 	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocSetSocketAlert(%d, %d)", id, flag);
	return -1;
}

int sceNetAdhocPollSocket(u32 socketStructAddr, int count, int timeout, int nonblock) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPollSocket(%08x, %i, %i, %i)", socketStructAddr, count, timeout, nonblock);
	return -1;
}

/**
 * Adhoc Emulator PDP Socket Delete
 * @param id Socket File Descriptor
 * @param flag Bitflags (Unused)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID
 */
int sceNetAdhocPdpDelete(int id, int unknown) {
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

int sceNetAdhocctlGetAdhocId(u32 productStructAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetAdhocId(%x)", productStructAddr);
	return 0;
}

int sceNetAdhocctlScan() {
	INFO_LOG(SCENET, "sceNetAdhocctlScan() at %08x", currentMIPS->pc);

	// Library initialized
	if (netAdhocctlInited) {
		// Not connected
		if (threadStatus == ADHOCCTL_STATE_DISCONNECTED) {
			threadStatus = ADHOCCTL_STATE_SCANNING;

			// Multithreading Lock
			peerlock.lock();

			// It seems AdHoc Server always sent the full group list, so we should reset current networks to prevent leaving host to be listed again
			freeGroupsRecursive(networks);
			networks = NULL;

			// Multithreading Unlock
			peerlock.unlock();

			// Prepare Scan Request Packet
			uint8_t opcode = OPCODE_SCAN;

			// Send Scan Request Packet, may failed with socket error 10054/10053 if someone else with the same IP already connected to AdHoc Server (the server might need to be modified to differentiate MAC instead of IP)
			int iResult = send(metasocket, (char *)&opcode, 1, 0);
			if (iResult == SOCKET_ERROR) {
				ERROR_LOG(SCENET, "Socket error (%i) when sending", errno);
				threadStatus = ADHOCCTL_STATE_DISCONNECTED;
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

int sceNetAdhocctlGetScanInfo(u32 size, u32 bufAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlGetScanInfo([%08x]=%i, %08x)", size, Memory::Read_U32(size), bufAddr);
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	int * buflen = (int *)Memory::GetPointer(size);
	SceNetAdhocctlScanInfoEmu * buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) {
		buf = (SceNetAdhocctlScanInfoEmu *)Memory::GetPointer(bufAddr);
	}
	// Library initialized
	if (netAdhocctlInited) {

		if (!Memory::IsValidAddress(size)) return ERROR_NET_ADHOCCTL_INVALID_ARG;

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
u32 sceNetAdhocctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = 0;
	struct AdhocctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	while (adhocctlHandlers.find(retval) != adhocctlHandlers.end())
		++retval;

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); it++) {
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

u32 sceNetAdhocctlDisconnect() {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	INFO_LOG(SCENET, "sceNetAdhocctlDisconnect() at %08x", currentMIPS->pc);
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
			peerlock.lock();

			// Clear Peer List
			freeFriendsRecursive(friends);

			// Delete Peer Reference
			friends = NULL;

			// Clear Group List
			freeGroupsRecursive(networks);

			// Delete Group Reference
			networks = NULL;

			// Multithreading Unlock
			peerlock.unlock();
		}
		
		// Notify Event Handlers (even if we weren't connected, not doing this will freeze games like God Eater, which expect this behaviour)
		__UpdateAdhocctlHandlers(ADHOCCTL_EVENT_DISCONNECT,0);

		// Return Success, some games might ignore returned value and always treat it as success, otherwise repeatedly calling this function
		return 0;
	}

	// Library uninitialized
	return 0; //ERROR_NET_ADHOC_NOT_INITIALIZED; // Wipeout Pulse will repeatedly calling this function if returned value is ERROR_NET_ADHOC_NOT_INITIALIZED
}

u32 sceNetAdhocctlDelHandler(u32 handlerID) {
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
	/*
	if (!g_Config.bEnableWlan) {
		netAdhocctlInited = false;
		return 0;
	}
	*/

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
#ifdef _MSC_VER
		WSACleanup(); // Might be better to call WSAStartup/WSACleanup from sceNetInit/sceNetTerm isn't? since it's the first/last network function being used
#endif
	}

	return 0;
}

int sceNetAdhocctlGetNameByAddr(const char *mac, u32 nameAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetNameByAddr(%s, %08x)", mac, nameAddr);
	return -1;
}

int sceNetAdhocctlJoin(u32 scanInfoAddr) {
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlJoin(%08x) at %08x", scanInfoAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}
	
	// Library initialized
	if (netAdhocctlInited) {

		if (!Memory::IsValidAddress(scanInfoAddr)) return ERROR_NET_ADHOCCTL_INVALID_ARG;

		// Disconnected State
		if (threadStatus == ADHOCCTL_STATE_DISCONNECTED) {

			SceNetAdhocctlScanInfo * sinfo = (SceNetAdhocctlScanInfo*)Memory::GetPointer(scanInfoAddr);

			// Prepare Connect Packet
			SceNetAdhocctlConnectPacketC2S packet;

			// Clear Packet Memory
			memset(&packet, 0, sizeof(packet));

			// Set Packet Opcode
			packet.base.opcode = OPCODE_CONNECT;

			// Set Target Group
			if (sinfo != NULL) 
			packet.group = sinfo->group_name;

			// Acquire Network Lock

			// Send Packet
			int iResult = send(metasocket, (const char *)&packet, sizeof(packet), 0);
			if (iResult == SOCKET_ERROR) {
				ERROR_LOG(SCENET, "Socket error (%i) when sending", errno);
				return ERROR_NET_ADHOCCTL_DISCONNECTED; // ERROR_NET_ADHOCCTL_BUSY;
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

	return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

int sceNetAdhocctlGetPeerInfo(const char *mac, int size, u32 peerInfoAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetPeerInfo(%s, %i, %08x)", mac, size, peerInfoAddr);
	return -1;
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
					return ERROR_NET_ADHOCCTL_DISCONNECTED; // ERROR_NET_ADHOCCTL_BUSY;
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

int sceNetAdhocctlConnect(u32 ptrToGroupName) {
	if (Memory::IsValidAddress(ptrToGroupName)) {
		INFO_LOG(SCENET, "sceNetAdhocctlConnect(groupName=%s) at %08x", Memory::GetCharPointer(ptrToGroupName), currentMIPS->pc);
		return sceNetAdhocctlCreate(Memory::GetCharPointer(ptrToGroupName));
	} else {
		return ERROR_NET_ADHOC_INVALID_ADDR; // shouldn't this be ERROR_NET_ADHOC_INVALID_ARG ?
	}
}

int sceNetAdhocctlCreateEnterGameMode(const char *groupName, int unknown, int playerNum, u32 macsAddr, int timeout, int unknown2) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlCreateEnterGameMode(%s, %i, %i, %08x, %i, %i) at %08x", groupName, unknown, playerNum, macsAddr, timeout, unknown2, currentMIPS->pc);
	return -1;
}

int sceNetAdhocctlJoinEnterGameMode(const char *groupName, const char *macAddr, int timeout, int unknown2) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlJoinEnterGameMode(%s, %s, %i, %i) at %08x", groupName, macAddr, timeout, unknown2, currentMIPS->pc);
	return -1;
}

int sceNetAdhocTerm() {
	INFO_LOG(SCENET, "sceNetAdhocTerm()");
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup all the sockets right?
	/*
	if (!g_Config.bEnableWlan) {
		netAdhocInited = false;
		return 0;
	}
	*/

	// Library is initialized
	if (netAdhocInited) {
		// Delete PDP Sockets
		deleteAllPDP();

		// Delete PTP Sockets
		deleteAllPTP();

		// Delete Gamemode Buffer
		//_deleteAllGMB();

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

int sceNetAdhocGetPdpStat(int structSize, u32 structAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGetPdpStat(%i, %08x)", structSize, structAddr);
	return 0;
}


/**
 * Adhoc Emulator PTP Socket List Getter
 * @param buflen IN: Length of Buffer in Bytes OUT: Required Length of Buffer in Bytes
 * @param buf PTP Socket List Buffer (can be NULL if you wish to receive Required Length)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED
 */
int sceNetAdhocGetPtpStat(u32 structSize, u32 structAddr) {
	// Spams a lot 
	VERBOSE_LOG(SCENET,"sceNetAdhocGetPtpStat(%u,%u)",structSize,structAddr);

	if (!g_Config.bEnableWlan) {
		return 0;
	}

	int * buflen = (int *)Memory::GetPointer(structSize);
	// Library is initialized
	if (netAdhocInited) {
		// Length Returner Mode
		if (buflen != NULL && !Memory::IsValidAddress(structAddr)) {
			// Return Required Size
			*buflen = sizeof(SceNetAdhocPtpStat) * getPTPSocketCount();
			
			// Success
			return 0;
		}
		
		// Status Returner Mode
		else if (buflen != NULL && Memory::IsValidAddress(structAddr)) {
			// Socket Count
			int socketcount = getPTPSocketCount();
			SceNetAdhocPtpStat * buf = (SceNetAdhocPtpStat *)Memory::GetPointer(structAddr);
			
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
						buf[i-1].next = structAddr + (i*sizeof(SceNetAdhocPtpStat)) + sizeof(SceNetAdhocPtpStat);
					
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
int sceNetAdhocPtpOpen(const char *srcmac, int sport, const char *dstmac, int dport, int bufsize, int rexmt_int, int rexmt_cnt, int unknown) {
	DEBUG_LOG(SCENET, "sceNetAdhocPtpOpen(%s,%d,%s,%d,%d,%d,%d,%d)", srcmac, sport, dstmac,dport,bufsize, rexmt_int, rexmt_cnt, unknown);
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
int sceNetAdhocPtpAccept(int id, u32 peerMacAddrPtr, u32 peerPortPtr, int timeout, int flag) {

	SceNetEtherAddr * addr = NULL;
	if (Memory::IsValidAddress(peerMacAddrPtr)) {
		addr = PSPPointer<SceNetEtherAddr>::Create(peerMacAddrPtr);
	}
	uint16_t * port = NULL; //
	if (Memory::IsValidAddress(peerPortPtr)) {
		port = (uint16_t *)Memory::GetPointer(peerPortPtr);
	}
	DEBUG_LOG(SCENET, "sceNetAdhocPtpAccept(%d,%s,%d,%u,%d)",id, addr->data,*port,timeout, flag);
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
			if (socket->state == PTP_STATE_LISTEN) {
				// Valid Arguments
				if (addr != NULL && port != NULL) {
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
						uint32_t starttime = (uint32_t)(real_time_now()*1000.0);
						
						// Retry until Timeout hits
						while ((timeout == 0 ||((uint32_t)(real_time_now()*1000.0) - starttime) < (uint32_t)timeout) && newsocket == -1) {
							// Accept Connection
							newsocket = accept(socket->id, (sockaddr *)&peeraddr, &peeraddrlen);
							
							// Wait a bit...
							sleep_ms(1);
						}
					}
					
					// Restore Blocking Behaviour
					if (nbio == 0) {
						// Restore Socket Option
						changeBlockingMode(socket->id,0);
					}
					
					// Accepted New Connection
					if (newsocket > 0) {
						// Enable Port Re-use
						setsockopt(newsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
						
						// Grab Local Address
						if (getsockname(newsocket, (sockaddr *)&local, &locallen) == 0) {
							// Peer MAC
							SceNetEtherAddr mac;
							
							// Find Peer MAC
							if (resolveIP(peeraddr.sin_addr.s_addr, &mac) == 0) {
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
										
										// Copy Local Address Data to Structure
										getLocalMac(&internal->laddr);
										internal->lport = ntohs(local.sin_port); // htons(local.sin_port);
										
										// Copy Peer Address Data to Structure
										internal->paddr = mac;
										internal->pport = ntohs(peeraddr.sin_port); // htons(peeraddr.sin_port);
										
										// Set Connected State
										internal->state = PTP_STATE_ESTABLISHED;
										
										// Return Peer Address Information
										*addr = internal->paddr;
										*port = internal->pport;
										
										// Link PTP Socket
										ptp[i] = internal;
										
										// Add Port Forward to Router
										// sceNetPortOpen("TCP", internal->lport);
										
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
int sceNetAdhocPtpConnect(int id, int timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPtpConnect(%i, %i, %08x)", id, timeout, flag);
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
				if (resolveMAC(&socket->paddr, (uint32_t *)&sin.sin_addr.s_addr) == 0) {
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
					
					// Restore Blocking Behaviour
					if (nbio == 0) {
						// Restore Socket Option
						changeBlockingMode(socket->id,0);
					}
					
					// Instant Connection (Lucky!)
					if (connectresult == 0 || (connectresult == -1 && errorcode == EISCONN)) {
						// Set Connected State
						socket->state = PTP_STATE_ESTABLISHED;
						
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
							uint32_t starttime = (uint32_t)(real_time_now()*1000.0);
							
							// Peer Information (for Connection-Polling)
							sockaddr_in peer;
							memset(&peer, 0, sizeof(peer));
							socklen_t peerlen = sizeof(peer);
							// Wait for Connection
							while ((timeout == 0 || ( (uint32_t)(real_time_now()*1000.0) - starttime) < (uint32_t)timeout) && getpeername(socket->id, (sockaddr *)&peer, &peerlen) != 0) {
								// Wait 1ms
								sleep_ms(1);
							}
							
							// Connected in Time
							if (sin.sin_addr.s_addr == peer.sin_addr.s_addr/* && sin.sin_port == peer.sin_port*/) {
								// Set Connected State
								socket->state = PTP_STATE_ESTABLISHED;
								
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
int sceNetAdhocPtpClose(int id, int unknown) {
	DEBUG_LOG(SCENET,"sceNetAdhocPtpClose(%d,%d)",id,unknown);
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
int sceNetAdhocPtpListen(const char *srcmac, int sport, int bufsize, int rexmt_int, int rexmt_cnt, int backlog, int unk) {
	DEBUG_LOG(SCENET, "sceNetAdhocPtpListen(%s,%d,%d,%d,%d,%d,%d)",srcmac,sport,bufsize,rexmt_int,rexmt_cnt,backlog,unk);
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
						// Enable Port Re-use
						setsockopt(tcpsocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
						
						// Binding Information for local Port
						sockaddr_in addr;
						addr.sin_family = AF_INET;
						addr.sin_addr.s_addr = INADDR_ANY;
						addr.sin_port = htons(sport);
						
						// Bound Socket to local Port
						if (bind(tcpsocket, (sockaddr *)&addr, sizeof(addr)) == 0) {
							// Switch into Listening Mode
							if (listen(tcpsocket, backlog) == 0) {
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
										internal->state = PTP_STATE_LISTEN;
										
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
						
						// Close Socket
						closesocket(tcpsocket);
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
int sceNetAdhocPtpSend(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPtpSend(%d,%08x,%08x,%d,%d)", id, dataAddr, dataSizeAddr, timeout, flag);
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
			if (socket->state == PTP_STATE_ESTABLISHED) {
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
					socket->state = PTP_STATE_CLOSED;
					
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
int sceNetAdhocPtpRecv(int id, u32 dataAddr, u32 dataSizeAddr, int timeout, int flag) {
	DEBUG_LOG(SCENET, "sceNetAdhocPtpRecv(%d,%08x,%08x,%d,%d)", id, dataAddr, dataSizeAddr, timeout, flag);
	if (!g_Config.bEnableWlan) {
		return 0;
	}
	void * buf = (void *)Memory::GetPointer(dataAddr);
	int * len = (int *)Memory::GetPointer(dataSizeAddr);
	// Library is initialized
	if (netAdhocInited) {
		// Valid Socket
		if (id > 0 && id <= 255 && ptp[id - 1] != NULL && ptp[id - 1]->state == PTP_STATE_ESTABLISHED) {
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
				socket->state = PTP_STATE_CLOSED;
				
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
int sceNetAdhocPtpFlush(int id, int timeout, int nonblock) {
	DEBUG_LOG(SCENET,"sceNetAdhocPtpFlush(%d,%d,%d)", id, timeout, nonblock);
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

int sceNetAdhocGameModeCreateMaster(u32 data, int size) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeCreateMaster(%08x, %i) at %08x", data, size, currentMIPS->pc);
	return -1;
}

int sceNetAdhocGameModeCreateReplica(const char *mac, u32 data, int size) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeCreateReplica(%s, %08x, %i) at %08x", mac, data, size, currentMIPS->pc);
	return -1;
}

int sceNetAdhocGameModeUpdateMaster() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateMaster()");
	return -1;
}

int sceNetAdhocGameModeDeleteMaster() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteMaster()");
	return -1;
}

int sceNetAdhocGameModeUpdateReplica(int id, u32 infoAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateReplica(%i, %08x)", id, infoAddr);
	return -1;
}

int sceNetAdhocGameModeDeleteReplica(int id) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteReplica(%i)", id);
	return -1;
}

int sceNetAdhocGetSocketAlert() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGetSocketAlert()");
	return 0;
}

int sceNetAdhocMatchingInit(u32 memsize) {
	INFO_LOG(SCENET, "sceNetAdhocMatchingInit(%d) at %08x", memsize, currentMIPS->pc);
	// Uninitialized Library
	if (!netAdhocMatchingInited) {
		// Save Fake Pool Size
		fakePoolSize = memsize;

		// Initialize Library
		netAdhocMatchingInited = true;

		// Return Success
		return 0;
	} else {
		return ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED;
	}
}

int sceNetAdhocMatchingTerm() {
	// Should we cleanup all created matching contexts first? just in case there are games that doesn't delete them before calling this
	/*
	if (netAdhocMatchingInited) {
		// Should we also delete all PDP? since creating a matching context will also create a PDP socket

	}
	*/
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingTerm()");
	netAdhocMatchingInited = false;

	return 0;
}



// Presumably returns a "matchingId".
int sceNetAdhocMatchingCreate(int mode, int maxnum, int port, int rxbuflen, int hello_int, int keepalive_int, int init_count, int rexmt_int, u32 callbackAddr) {
	INFO_LOG(SCENET, "sceNetAdhocMatchingCreate(mode=%i, maxnum=%i, port=%i, rxbuflen=%i, hello=%i, keepalive=%i, initcount=%i, rexmt=%i, callbackAddr=%08x) at %08x", mode, maxnum, port, rxbuflen, hello_int, keepalive_int, init_count, rexmt_int, callbackAddr, currentMIPS->pc);
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
								context->hello_int = hello_int;
								context->keepalive_int = 500000;
								//context->keepalive_int = keepalive_int;
								context->resendcounter = init_count;
								context->keepalivecounter = 100;
								//context->keepalivecounter = init_count;
								context->resend_int = rexmt_int;
								context->handler = handler;

								// Fill in Selfpeer
								context->mac = localmac;

								// Multithreading Lock
								peerlock.lock(); //contextlock.lock();

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
int sceNetAdhocMatchingStart(int matchingId, int evthPri, int evthStack, int inthPri, int inthStack, int optLen, u32 optDataAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingStart(%i, %i, %i, %i, %i, %i, %08x) at %08x", matchingId, evthPri, evthStack, inthPri, inthStack, optLen, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	SceNetAdhocMatchingContext * item = findMatchingContext(matchingId);
	if (item != NULL) {
		sceNetAdhocMatchingSetHelloOpt(matchingId, optLen, optDataAddr);
		if ((optLen > 0) && Memory::IsValidAddress(optDataAddr)) {
			// Allocate the memory and copy the content
			if (item->hello != NULL) free(item->hello);
			item->hello = malloc(optLen);
			if (item->hello != NULL)
				Memory::Memcpy(item->hello, optDataAddr, optLen);
		}
		//else return ERROR_NET_ADHOC_MATCHING_INVALID_ARG; // ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN; // Returning Not Success will cause GTA:VC unable to host/join

		//Add your own MAC as a member (ony if it's empty?)
		addMember(item, &item->mac);

		//TODO: Create a new thread
	    // Easier to implement if using an existing thread (friendFinder) instead of creating a new one ^_^
		
		//sceKernelCreateThread
		/*eventHandlerUpdate = CoreTiming::RegisterEvent("HandlerUpdateEvent", __handlerUpdateCallback);
		memberFinderRunning = true;
		memberFinderThread = std::thread(memberFinder);*/
	}

	return 0;
}

int sceNetAdhocMatchingStop(int matchingId) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingStop(%i)", matchingId);
	if (!g_Config.bEnableWlan)
		return -1;

	SceNetAdhocMatchingContext * item = findMatchingContext(matchingId);
	if (item != NULL) {
		/*memberFinderRunning = false;
		if (memberFinderThread.joinable()) {
			friendFinderThread.join();
		}*/

		// Remove your own MAC, or All memebers, don't remove at all?
		//delAllMembers();
	}

	return 0;
}

int sceNetAdhocMatchingDelete(int matchingId) {
	// WLAN might be disabled in the middle of successfull multiplayer, but we still need to cleanup right?
	/*
	if (!g_Config.bEnableWlan)
		return -1;
	*/

	// Previous Context Reference
	SceNetAdhocMatchingContext * prev = NULL;

	// Context Pointer
	SceNetAdhocMatchingContext * item = contexts;

	// Iterate contexts
	for (; item != NULL; item = item->next) {
		// Found matching ID
		if (item->id == matchingId) {
			// Multithreading Lock
			peerlock.lock(); //contextlock.lock();

			// Unlink Left (Beginning)
			if (prev == NULL) contexts = item->next;

			// Unlink Left (Other)
			else prev->next = item->next;

			// Multithreading Unlock
			peerlock.unlock(); //contextlock.unlock();

			// Delete the socket
			sceNetAdhocPdpDelete(item->socket, 0); // item->connected = (sceNetAdhocPdpDelete(item->socket, 0) < 0);
			// Free allocated memories
			free(item->hello);
			free(item->rxbuf);
			deleteAllMembers(item);
			// Free item context memory
			free(item);

			// Stop Search
			break;
		}

		// Set Previous Reference
		prev = item;
	}
	
	WARN_LOG(SCENET, "UNTESTED sceNetAdhocMatchingDelete(%i) at %08x", matchingId, currentMIPS->pc);
	return 0;
}

int sceNetAdhocMatchingSelectTarget(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingSelectTarget(%i, %s, %i, %08x)", matchingId, macAddress, optLen, optDataPtr);
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

int sceNetAdhocMatchingCancelTargetWithOpt(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingCancelTargetWithOpt(%i, %s, %i, %08x)", matchingId, macAddress, optLen, optDataPtr);
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

int sceNetAdhocMatchingCancelTarget(int matchingId, const char *macAddress) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingCancelTarget(%i, %s)", matchingId, macAddress);
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

int sceNetAdhocMatchingGetHelloOpt(int matchingId, u32 optLenAddr, u32 optDataAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetHelloOpt(%i, %08x, %08x)", matchingId, optLenAddr, optDataAddr);
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

int sceNetAdhocMatchingSetHelloOpt(int matchingId, int optLenAddr, u32 optDataAddr) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocMatchingSetHelloOpt(%i, %i, %08x) at %08x", matchingId, optLenAddr, optDataAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	//if (!Memory::IsValidAddress(optDataAddr)) return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;
	
	SceNetAdhocMatchingContext * item = findMatchingContext(matchingId);
	if (item != NULL) {
		// Set OptData
		int oldLen = item->hellolen;
		item->hellolen = optLenAddr; //optLenAddr doesn't seems to be an address to the actual len
		item->helloAddr = optDataAddr;

		if (optLenAddr <= 0 || !Memory::IsValidAddress(optDataAddr)) {
			if (item->hello != NULL) {
				free(item->hello);
				item->hello = NULL;
			}
		}
		else {
			if (optLenAddr > oldLen) {
				free(item->hello);
				item->hello = malloc(optLenAddr);
			}
			Memory::Memcpy(item->hello, optDataAddr, optLenAddr);
		}
	}
	//else return ERROR_NET_ADHOC_MATCHING_INVALID_ID;

	//sceNetAdhocctlScan();

	return 0;
}

int sceNetAdhocMatchingGetMembers(int matchingId, u32 sizeAddr, u32 buf) {
	DEBUG_LOG(SCENET, "UNTESTED sceNetAdhocMatchingGetMembers(%i, [%08x]=%i, %08x) at %08x", matchingId, sizeAddr, Memory::Read_U32(sizeAddr), buf, currentMIPS->pc);
	if (!g_Config.bEnableWlan)
		return -1;

	// Minimum Argument
	if (!Memory::IsValidAddress(sizeAddr)) return ERROR_NET_ADHOC_MATCHING_INVALID_ARG;

	SceNetAdhocMatchingContext * item = findMatchingContext(matchingId);
	if (item != NULL) {
		int * buflen = (int *)Memory::GetPointer(sizeAddr);
		SceNetAdhocMatchingMemberInfoEmu * buf2 = NULL;
		if (Memory::IsValidAddress(buf)) {
			buf2 = (SceNetAdhocMatchingMemberInfoEmu *)Memory::GetPointer(buf);
		}

		// Multithreading Lock
		peerlock.lock();

		// Iterate Members
		//SceNetAdhocMatchingMemberInternal * peer = item->peerlist;

		//Using friend list instead, since it's easier to implement ^_^
		SceNetAdhocctlPeerInfo * peer = friends;

		int count = 0;
		if (!Memory::IsValidAddress(buf)) { // Returning needed buf size instead
			count++;
			for (; peer != NULL; peer = peer->next) count++;
		}
		else {
			buf2[count].mac_addr = item->mac; //add your own MAC
			buf2[count].padding[0] = 0; // does padding need to be 0 ?
			buf2[count++].padding[1] = 0;
			for (; peer != NULL; peer = peer->next) {
				buf2[count].mac_addr = peer->mac_addr;
				buf2[count].padding[0] = 0;
				buf2[count].padding[1] = 0;
				count++;
			}
			// Link List
			int i = 0; 
			for (; i < count - 1; i++) {
				// Link Network
				buf2[i].next = buf + (sizeof(SceNetAdhocMatchingMemberInfoEmu)*i) + sizeof(SceNetAdhocMatchingMemberInfoEmu);
			}
			// Fix Last Element
			if (count > 0) buf2[count - 1].next = 0;
		}

		// Fix Size
		*buflen = count * sizeof(SceNetAdhocMatchingMemberInfoEmu); //includes your own MAC

		// Multithreading Unlock
		peerlock.unlock();
	}
	//else return ERROR_NET_ADHOC_MATCHING_INVALID_ID;

	return 0;
}

int sceNetAdhocMatchingSendData(int matchingId, const char *mac, int dataLen, u32 dataAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingSendData(%i, %s, %i, %08x)", matchingId, mac, dataLen, dataAddr);
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

int sceNetAdhocMatchingAbortSendData(int matchingId, const char *mac) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingAbortSendData(%i, %s)", matchingId, mac);
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

int sceNetAdhocMatchingGetPoolMaxAlloc() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetPoolMaxAlloc()");
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

int sceNetAdhocMatchingGetPoolStat() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetPoolStat()");
	if (!g_Config.bEnableWlan)
		return -1;
	return 0;
}

const HLEFunction sceNetAdhoc[] = {
	{0xE1D621D7, WrapU_V<sceNetAdhocInit>, "sceNetAdhocInit"}, 
	{0xA62C6F57, WrapI_V<sceNetAdhocTerm>, "sceNetAdhocTerm"}, 
	{0x0AD043ED, WrapI_U<sceNetAdhocctlConnect>, "sceNetAdhocctlConnect"},
	{0x6f92741b, WrapI_CUIU<sceNetAdhocPdpCreate>, "sceNetAdhocPdpCreate"},
	{0xabed3790, WrapI_ICUVIII<sceNetAdhocPdpSend>, "sceNetAdhocPdpSend"},
	{0xdfe53e03, WrapI_IVVVVUI<sceNetAdhocPdpRecv>, "sceNetAdhocPdpRecv"},
	{0x7f27bb5e, WrapI_II<sceNetAdhocPdpDelete>, "sceNetAdhocPdpDelete"},
	{0xc7c1fc57, WrapI_IU<sceNetAdhocGetPdpStat>, "sceNetAdhocGetPdpStat"},
	{0x157e6225, WrapI_II<sceNetAdhocPtpClose>, "sceNetAdhocPtpClose"},
	{0x4da4c788, WrapI_IUUII<sceNetAdhocPtpSend>, "sceNetAdhocPtpSend"},
	{0x877f6d66, WrapI_CICIIIII<sceNetAdhocPtpOpen>, "sceNetAdhocPtpOpen"},
	{0x8bea2b3e, WrapI_IUUII<sceNetAdhocPtpRecv>, "sceNetAdhocPtpRecv"},
	{0x9df81198, WrapI_IUUII<sceNetAdhocPtpAccept>, "sceNetAdhocPtpAccept"},
	{0xe08bdac1, WrapI_CIIIIII<sceNetAdhocPtpListen>, "sceNetAdhocPtpListen"},
	{0xfc6fc07b, WrapI_III<sceNetAdhocPtpConnect>, "sceNetAdhocPtpConnect"},
	{0x9ac2eeac, WrapI_III<sceNetAdhocPtpFlush>, "sceNetAdhocPtpFlush"},
	{0xb9685118, WrapI_UU<sceNetAdhocGetPtpStat>, "sceNetAdhocGetPtpStat"},
	{0x3278ab0c, WrapI_CUI<sceNetAdhocGameModeCreateReplica>, "sceNetAdhocGameModeCreateReplica"},
	{0x98c204c8, WrapI_V<sceNetAdhocGameModeUpdateMaster>, "sceNetAdhocGameModeUpdateMaster"}, 
	{0xfa324b4e, WrapI_IU<sceNetAdhocGameModeUpdateReplica>, "sceNetAdhocGameModeUpdateReplica"},
	{0xa0229362, WrapI_V<sceNetAdhocGameModeDeleteMaster>, "sceNetAdhocGameModeDeleteMaster"},
	{0x0b2228e9, WrapI_I<sceNetAdhocGameModeDeleteReplica>, "sceNetAdhocGameModeDeleteReplica"},
	{0x7F75C338, WrapI_UI<sceNetAdhocGameModeCreateMaster>, "sceNetAdhocGameModeCreateMaster"},
	{0x73bfd52d, WrapI_II<sceNetAdhocSetSocketAlert>, "sceNetAdhocSetSocketAlert"},
	{0x4d2ce199, WrapI_V<sceNetAdhocGetSocketAlert>, "sceNetAdhocGetSocketAlert"},
	{0x7a662d6b, WrapI_UIII<sceNetAdhocPollSocket>, "sceNetAdhocPollSocket"},
};							

const HLEFunction sceNetAdhocMatching[] = {
	{0x2a2a1e07, WrapI_U<sceNetAdhocMatchingInit>, "sceNetAdhocMatchingInit"},
	{0x7945ecda, WrapI_V<sceNetAdhocMatchingTerm>, "sceNetAdhocMatchingTerm"},
	{0xca5eda6f, WrapI_IIIIIIIIU<sceNetAdhocMatchingCreate>, "sceNetAdhocMatchingCreate"},
	{0x93ef3843, WrapI_IIIIIIU<sceNetAdhocMatchingStart>, "sceNetAdhocMatchingStart"},
	{0x32b156b3, WrapI_I<sceNetAdhocMatchingStop>, "sceNetAdhocMatchingStop"},
	{0xf16eaf4f, WrapI_I<sceNetAdhocMatchingDelete>, "sceNetAdhocMatchingDelete"},
	{0x5e3d4b79, WrapI_ICIU<sceNetAdhocMatchingSelectTarget>, "sceNetAdhocMatchingSelectTarget"},
	{0xea3c6108, WrapI_IC<sceNetAdhocMatchingCancelTarget>, "sceNetAdhocMatchingCancelTarget"},
	{0x8f58bedf, WrapI_ICIU<sceNetAdhocMatchingCancelTargetWithOpt>, "sceNetAdhocMatchingCancelTargetWithOpt"},
	{0xb5d96c2a, WrapI_IUU<sceNetAdhocMatchingGetHelloOpt>, "sceNetAdhocMatchingGetHelloOpt"},
	{0xb58e61b7, WrapI_IIU<sceNetAdhocMatchingSetHelloOpt>, "sceNetAdhocMatchingSetHelloOpt"},
	{0xc58bcd9e, WrapI_IUU<sceNetAdhocMatchingGetMembers>, "sceNetAdhocMatchingGetMembers"},
	{0xf79472d7, WrapI_ICIU<sceNetAdhocMatchingSendData>, "sceNetAdhocMatchingSendData"},
	{0xec19337d, WrapI_IC<sceNetAdhocMatchingAbortSendData>, "sceNetAdhocMatchingAbortSendData"},
	{0x40F8F435, WrapI_V<sceNetAdhocMatchingGetPoolMaxAlloc>, "sceNetAdhocMatchingGetPoolMaxAlloc"},
	{0x9c5cfb7d, WrapI_V<sceNetAdhocMatchingGetPoolStat>, "sceNetAdhocMatchingGetPoolStat"},
};

int sceNetAdhocctlExitGameMode() {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlExitGameMode()");
	return -1;
}

int sceNetAdhocctlGetGameModeInfo(u32 infoAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetGameModeInfo(%08x)", infoAddr);
	return -1;
}


int sceNetAdhocctlGetPeerList(u32 sizeAddr, u32 bufAddr) {
	INFO_LOG(SCENET, "sceNetAdhocctlGetPeerList(%08x, %08x) at %08x", sizeAddr, bufAddr, currentMIPS->pc);
	if (!g_Config.bEnableWlan) {
		return -1;
	}

	int * buflen = (int *)Memory::GetPointer(sizeAddr);
	SceNetAdhocctlPeerInfoEmu * buf = NULL;
	if (Memory::IsValidAddress(bufAddr)) {
		buf = (SceNetAdhocctlPeerInfoEmu *)Memory::GetPointer(bufAddr);
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
						peer->last_recv = (uint64_t)time(NULL); 

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
						buf[i].next = bufAddr+(sizeof(SceNetAdhocctlPeerInfoEmu)*i)+
							sizeof(SceNetAdhocctlPeerInfoEmu);
					}
					// Fix Last Element
					if (discovered > 0) buf[discovered - 1].next = 0;
				}

				// Fix Size
				*buflen = discovered * sizeof(SceNetAdhocctlPeerInfo);
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

int sceNetAdhocctlGetAddrByName(const char *nickName, u32 sizeAddr, u32 bufAddr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetPeerList(%s, %08x, %08x)", nickName, sizeAddr, bufAddr);
	return -1;
}

const HLEFunction sceNetAdhocctl[] = {
	{0xE26F226E, WrapU_IIU<sceNetAdhocctlInit>, "sceNetAdhocctlInit"},
	{0x9D689E13, WrapI_V<sceNetAdhocctlTerm>, "sceNetAdhocctlTerm"},
	{0x20B317A0, WrapU_UU<sceNetAdhocctlAddHandler>, "sceNetAdhocctlAddHandler"},
	{0x6402490B, WrapU_U<sceNetAdhocctlDelHandler>, "sceNetAdhocctlDelHandler"},
	{0x34401D65, WrapU_V<sceNetAdhocctlDisconnect>, "sceNetAdhocctlDisconnect"},
	{0x0ad043ed, WrapI_U<sceNetAdhocctlConnect>, "sceNetAdhocctlConnect"},
	{0x08fff7a0, WrapI_V<sceNetAdhocctlScan>, "sceNetAdhocctlScan"},
	{0x75ecd386, WrapI_U<sceNetAdhocctlGetState>, "sceNetAdhocctlGetState"},
	{0x8916c003, WrapI_CU<sceNetAdhocctlGetNameByAddr>, "sceNetAdhocctlGetNameByAddr"},
	{0xded9d28e, WrapI_U<sceNetAdhocctlGetParameter>, "sceNetAdhocctlGetParameter"},
	{0x81aee1be, WrapI_UU<sceNetAdhocctlGetScanInfo>, "sceNetAdhocctlGetScanInfo"},
	{0x5e7f79c9, WrapI_U<sceNetAdhocctlJoin>, "sceNetAdhocctlJoin"},
	{0x8db83fdc, WrapI_CIU<sceNetAdhocctlGetPeerInfo>, "sceNetAdhocctlGetPeerInfo"},
	{0xec0635c1, WrapI_C<sceNetAdhocctlCreate>, "sceNetAdhocctlCreate"},
	{0xa5c055ce, WrapI_CIIUII<sceNetAdhocctlCreateEnterGameMode>, "sceNetAdhocctlCreateEnterGameMode"},
	{0x1ff89745, WrapI_CCII<sceNetAdhocctlJoinEnterGameMode>, "sceNetAdhocctlJoinEnterGameMode"},
	{0xcf8e084d, WrapI_V<sceNetAdhocctlExitGameMode>, "sceNetAdhocctlExitGameMode"},
	{0xe162cb14, WrapI_UU<sceNetAdhocctlGetPeerList>, "sceNetAdhocctlGetPeerList"},
	{0x362cbe8f, WrapI_U<sceNetAdhocctlGetAdhocId>, "sceNetAdhocctlGetAdhocId"},
	{0x5a014ce0, WrapI_U<sceNetAdhocctlGetGameModeInfo>, "sceNetAdhocctlGetGameModeInfo"},
	{0x99560abe, WrapI_CUU<sceNetAdhocctlGetAddrByName>, "sceNetAdhocctlGetAddrByName"},
	{0xb0b80e80, 0, "sceNetAdhocctlCreateEnterGameModeMin"}, // ??
};

const HLEFunction sceNetAdhocDiscover[] = {
	{0x941B3877, 0, "sceNetAdhocDiscoverInitStart"},
	{0x52DE1B97, 0, "sceNetAdhocDiscoverUpdate"},
	{0x944DDBC6, 0, "sceNetAdhocDiscoverGetStatus"},
	{0xA2246614, 0, "sceNetAdhocDiscoverTerm"},
	{0xF7D13214, 0, "sceNetAdhocDiscoverStop"},
	{0xA423A21B, 0, "sceNetAdhocDiscoverRequestSuspend"},
};

void Register_sceNetAdhoc() {
	RegisterModule("sceNetAdhoc", ARRAY_SIZE(sceNetAdhoc), sceNetAdhoc);
	RegisterModule("sceNetAdhocMatching", ARRAY_SIZE(sceNetAdhocMatching), sceNetAdhocMatching);
	RegisterModule("sceNetAdhocDiscover", ARRAY_SIZE(sceNetAdhocDiscover), sceNetAdhocDiscover);
	RegisterModule("sceNetAdhocctl", ARRAY_SIZE(sceNetAdhocctl), sceNetAdhocctl);
}

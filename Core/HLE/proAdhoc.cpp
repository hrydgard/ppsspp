// TODO: Add license

#include <cstring>
#include "util/text/parsers.h"
#include "proAdhoc.h" 

uint32_t fakePoolSize                 = 0;
SceNetAdhocMatchingContext * contexts = NULL;
int one                               = 1;
bool friendFinderRunning              = false;
SceNetAdhocctlPeerInfo * friends      = NULL;
SceNetAdhocctlScanInfo * networks     = NULL;
int eventHandlerUpdate                = -1;
int threadStatus                      = ADHOCCTL_STATE_DISCONNECTED;

int metasocket;
SceNetAdhocctlParameter parameter;
std::thread friendFinderThread;
recursive_mutex peerlock;
SceNetAdhocPdpStat * pdp[255];
SceNetAdhocPtpStat * ptp[255];

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
  int i = 0; for(; i < 255; i++) if(pdp[i] != NULL && pdp[i]->lport == port) return 1;

  // Unused Port
  return 0;
}

int isPTPPortInUse(uint16_t port) {
	// Iterate Sockets
	int i = 0; for(; i < 255; i++) if(ptp[i] != NULL && ptp[i]->lport == port) return 1;
	
	// Unused Port
	return 0;
}

SceNetAdhocMatchingMemberInternal* findMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac) {
	if (context == NULL || mac == NULL) return NULL;
		
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist;
	while (peer != NULL) {
		if (IsMatch(peer->mac, *mac))
			return peer;
		peer = peer->next;
	}

	return NULL;
}

void addMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac) {
	if (context == NULL || mac == NULL) return;
	
	SceNetAdhocMatchingMemberInternal * peer = findMember(context, mac);
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
}

void deleteMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac) {
	if (context == NULL || mac == NULL) return;

	// Previous Peer Reference
	SceNetAdhocMatchingMemberInternal * prev = NULL;

	// Peer Pointer
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Found Peer
		if (IsMatch(context->mac, *mac)) {
			// Multithreading Lock
			//context->peerlock.lock();

			// Unlink Left (Beginning)
			if (prev == NULL) context->peerlist = peer->next;

			// Unlink Left (Other)
			else prev->next = peer->next;

			// Multithreading Unlock
			//context->peerlock.unlock();

			// Free Memory
			free(peer);

			// Stop Search
			break;
		}

		// Set Previous Reference
		prev = peer;
	}

}

void deleteAllMembers(SceNetAdhocMatchingContext * context) {
	if (context == NULL) return;
	
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist;
	while (peer != NULL) {
		context->peerlist = peer->next;
		free(peer);
		peer = context->peerlist;
	}
}

void addFriend(SceNetAdhocctlConnectPacketS2C * packet) {
  if (packet == NULL) return;

  // Allocate Structure
  SceNetAdhocctlPeerInfo * peer = (SceNetAdhocctlPeerInfo *)malloc(sizeof(SceNetAdhocctlPeerInfo));
  // Allocated Structure
  if(peer != NULL) {
    // Clear Memory
    memset(peer, 0, sizeof(SceNetAdhocctlPeerInfo));

    // Link to existing Peers
    peer->next = friends;

    // Save Nickname
    peer->nickname = packet->name;

    // Save MAC Address
    peer->mac_addr = packet->mac;

    // Save IP Address
    peer->ip_addr = packet->ip;

    // Multithreading Lock
    peerlock.lock();

    // Link into Peerlist
    friends = peer;

    // Multithreading Unlock
    peerlock.unlock();
  }
}

void changeBlockingMode(int fd, int nonblocking) {
  unsigned long on = 1;
  unsigned long off = 0;
#ifdef _MSC_VER
  if(nonblocking){
    // Change to Non-Blocking Mode
    ioctlsocket(fd,FIONBIO,&on);
  }else { 
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
  for(; group != NULL; group = group->next) count++;

  // Return Network Count
  return count;
}

SceNetAdhocctlScanInfo * findGroup(SceNetEtherAddr * MAC) {
	if (MAC == NULL) return NULL;

	// Group Reference
	SceNetAdhocctlScanInfo * group = networks;

	// Count Groups
	for (; group != NULL; group = group->next) {
		if (IsMatch(group->bssid.mac_addr, *MAC)) break;
	}

	// Return Network Count
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
  int i = 0; for(; i < 255; i++) {
    // Active Socket
    if(pdp[i] != NULL) {
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
  int i = 0; for(; i < 255; i++) {
    // Active Socket
    if(ptp[i] != NULL) {
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
  for(; peer != NULL; peer = peer->next) {
    // Found Peer
    if(peer->ip_addr == ip) {
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

      // Stop Search
      break;
    }

    // Set Previous Reference
    prev = peer;
  }
}

int findFreeMatchingID(void) {
  // Minimum Matching ID
  int min = 1;

  // Maximum Matching ID
  int max = 0;

  // Find highest Matching ID
  SceNetAdhocMatchingContext * item = contexts; for(; item != NULL; item = item->next) {
    // New Maximum
    if(max < item->id) max = item->id;
  }

  // Find unoccupied ID
  int i = min; for(; i < max; i++) {
    // Found unoccupied ID
    if(findMatchingContext(i) == NULL) return i;
  }

  // Append at virtual end
  return max + 1;
}

SceNetAdhocMatchingContext * findMatchingContext(int id) {
  // Iterate Matching Context List
  SceNetAdhocMatchingContext * item = contexts; for(; item != NULL; item = item->next) { // Found Matching ID
    if(item->id == id) return item;
  }

  // Context not found
  return NULL;
}

void freeFriendsRecursive(SceNetAdhocctlPeerInfo * node) {
  // End of List
  if(node == NULL) return;

  // Increase Recursion Depth
  freeFriendsRecursive(node->next);

  // Free Memory
  free(node);
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
  while(friendFinderRunning) {
    // Acquire Network Lock
    //_acquireNetworkLock();

    // Ping Server
    now = real_time_now()*1000.0;
    if(now - lastping >= 100) {
      // Update Ping Time
      lastping = now;

      // Prepare Packet
      uint8_t opcode = OPCODE_PING;

      // Send Ping to Server, may failed with socket error 10054/10053 if someone else with the same IP already connected to AdHoc Server (the server might need to be modified to differentiate MAC instead of IP)
      int iResult = send(metasocket, (const char *)&opcode, 1,0);
	  /*if (iResult == SOCKET_ERROR) {
		  ERROR_LOG(SCENET, "FriendFinder: Socket Error (%i) when sending OPCODE_PING", errno);
		  //friendFinderRunning = false;
	  }*/
    }

    // Send Chat Messages
    //while(popFromOutbox(chat.message))
    //{
    //  // Send Chat to Server
    //  sceNetInetSend(metasocket, (const char *)&chat, sizeof(chat), 0);
    //}

    // Wait for Incoming Data
    int received = recv(metasocket, (char *)(rx + rxpos), sizeof(rx) - rxpos,0);

    // Free Network Lock
    //_freeNetworkLock();

    // Received Data
    if(received > 0) {
      // Fix Position
      rxpos += received;

      // Log Incoming Traffic
      //printf("Received %d Bytes of Data from Server\n", received);
      INFO_LOG(SCENET, "Received %d Bytes of Data from Adhoc Server", received);
    }

    // Handle Packets
    if(rxpos > 0) {
      // BSSID Packet
      if(rx[0] == OPCODE_CONNECT_BSSID) {
		INFO_LOG(SCENET, "FriendFinder: Incoming OPCODE_CONNECT_BSSID");
        // Enough Data available
        if(rxpos >= (int)sizeof(SceNetAdhocctlConnectBSSIDPacketS2C)) {
          // Cast Packet
          SceNetAdhocctlConnectBSSIDPacketS2C * packet = (SceNetAdhocctlConnectBSSIDPacketS2C *)rx;
          // Update BSSID
          parameter.bssid.mac_addr = packet->mac;
          // Change State
          threadStatus = ADHOCCTL_STATE_CONNECTED;
          // Notify Event Handlers
          CoreTiming::ScheduleEvent_Threadsafe_Immediate(eventHandlerUpdate, join32(ADHOCCTL_EVENT_CONNECT, 0));

          // Move RX Buffer
          memmove(rx, rx + sizeof(SceNetAdhocctlConnectBSSIDPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlConnectBSSIDPacketS2C));

          // Fix RX Buffer Length
          rxpos -= sizeof(SceNetAdhocctlConnectBSSIDPacketS2C);
        }
      }

      // Chat Packet
      else if(rx[0] == OPCODE_CHAT) {
		INFO_LOG(SCENET, "FriendFinder: Incoming OPCODE_CHAT");
        // Enough Data available
        if(rxpos >= (int)sizeof(SceNetAdhocctlChatPacketS2C)) {
          // Cast Packet
          SceNetAdhocctlChatPacketS2C * packet = (SceNetAdhocctlChatPacketS2C *)rx;

          // Fix for Idiots that try to troll the "ME" Nametag
          if(strcasecmp((char *)packet->name.data, "ME") == 0) strcpy((char *)packet->name.data, "NOT ME");

          // Add Incoming Chat to HUD
          //printf("Receive chat message %s", packet->base.message);
		  DEBUG_LOG(SCENET, "Received chat message %s", packet->base.message);

          // Move RX Buffer
          memmove(rx, rx + sizeof(SceNetAdhocctlChatPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlChatPacketS2C));

          // Fix RX Buffer Length
          rxpos -= sizeof(SceNetAdhocctlChatPacketS2C);
        }
      }

      // Connect Packet
      else if(rx[0] == OPCODE_CONNECT) {
		DEBUG_LOG(SCENET, "FriendFinder: OPCODE_CONNECT");
        // Enough Data available
        if(rxpos >= (int)sizeof(SceNetAdhocctlConnectPacketS2C)) {
          // Log Incoming Peer
          INFO_LOG(SCENET,"Incoming Peer Data...");

          // Cast Packet
          SceNetAdhocctlConnectPacketS2C * packet = (SceNetAdhocctlConnectPacketS2C *)rx;

          // Add User
          addFriend(packet);

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
      else if(rx[0] == OPCODE_DISCONNECT) {
		DEBUG_LOG(SCENET, "FriendFinder: OPCODE_DISCONNECT");
        // Enough Data available
        if(rxpos >= (int)sizeof(SceNetAdhocctlDisconnectPacketS2C)) {
          // Log Incoming Peer Delete Request
          INFO_LOG(SCENET,"FriendFinder: Incoming Peer Data Delete Request...");

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
      else if(rx[0] == OPCODE_SCAN) {
		DEBUG_LOG(SCENET, "FriendFinder: OPCODE_SCAN");
        // Enough Data available
        if(rxpos >= (int)sizeof(SceNetAdhocctlScanPacketS2C)) {
          // Log Incoming Network Information
          INFO_LOG(SCENET,"Incoming Group Information...");
          // Cast Packet
          SceNetAdhocctlScanPacketS2C * packet = (SceNetAdhocctlScanPacketS2C *)rx;

		  // Multithreading Lock
		  peerlock.lock();

		  // It seems AdHoc Server always sent the full group list, so we should reset group list during Scan initialization

		  // Should only add non-existing group (or replace an existing group) to prevent Ford Street Racing from showing a strange game session list
		  /*SceNetAdhocctlScanInfo * group = findGroup(&packet->mac);

		  if (group != NULL) {
			  // Copy Group Name
			  group->group_name = packet->group;

			  // Set Group Host
			  group->bssid.mac_addr = packet->mac;
		  }
		  else*/ {
			  // Allocate Structure Data
			  SceNetAdhocctlScanInfo * group = (SceNetAdhocctlScanInfo *)malloc(sizeof(SceNetAdhocctlScanInfo));

			  // Allocated Structure Data
			  if (group != NULL)
			  {
				  // Clear Memory, should this be done only when allocating new group?
				  memset(group, 0, sizeof(SceNetAdhocctlScanInfo));

				  // Link to existing Groups
				  group->next = networks;

				  // Copy Group Name
				  group->group_name = packet->group;

				  // Set Group Host
				  group->bssid.mac_addr = packet->mac;

				  // Link into Group List
				  networks = group;
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
      else if(rx[0] == OPCODE_SCAN_COMPLETE) {
		DEBUG_LOG(SCENET, "FriendFinder: OPCODE_SCAN_COMPLETE");
        // Log Scan Completion
        INFO_LOG(SCENET,"FriendFinder: Incoming Scan complete response...");

        // Change State
        threadStatus = ADHOCCTL_STATE_DISCONNECTED;

        // Notify Event Handlers
        CoreTiming::ScheduleEvent_Threadsafe_Immediate(eventHandlerUpdate,join32(ADHOCCTL_EVENT_SCAN, 0));
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
    sleep_ms(100);
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
  for(; peer != NULL; peer = peer->next) {
    // Increase Counter
    count++;
  }

  // Return Result
  return count;
}

int getLocalIp(sockaddr_in * SocketAddress){
#ifdef _XBOX
	return -1;
#elif defined(_MSC_VER)
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
	SocketAddress->sin_addr.s_addr = inet_addr("192.168.12.1");
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
	if (!ParseMacAddress(g_Config.localMacAddress.c_str(), mac)) {
		ERROR_LOG(SCENET, "Error parsing mac address %s", g_Config.localMacAddress.c_str());
	}
	memcpy(addr, mac, ETHER_ADDR_LEN);
}

uint16_t getLocalPort(int sock) {
	struct sockaddr_in localAddr;
	localAddr.sin_port = 0;
	socklen_t addrLen = sizeof(localAddr);
	getsockname(sock, (struct sockaddr*)&localAddr, &addrLen);
	return localAddr.sin_port;
}

int getPTPSocketCount(void) {
  // Socket Counter
  int counter = 0;

  // Count Sockets
  int i = 0; for(; i < 255; i++) if(ptp[i] != NULL) counter++;

  // Return Socket Count
  return counter;
}

int initNetwork(SceNetAdhocctlAdhocId *adhoc_id){
#ifdef _XBOX
	return -1;
#else
  int iResult = 0;
#ifdef _MSC_VER
  WSADATA data;
  iResult = WSAStartup(MAKEWORD(2,2),&data); // Might be better to call WSAStartup/WSACleanup from sceNetInit/sceNetTerm isn't? since it's the first/last network function being used
  if(iResult != NOERROR){
    ERROR_LOG(SCENET, "WSA failed");
    return iResult;
  }
#endif
  metasocket = (int)INVALID_SOCKET;
  metasocket = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
  if(metasocket == INVALID_SOCKET){
    ERROR_LOG(SCENET,"Invalid socket");
    return -1;
  }
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(27312); // Maybe read this from config too

	// Resolve dns
	addrinfo * resultAddr;
    addrinfo * ptr;
    in_addr serverIp;
	serverIp.s_addr = INADDR_NONE;

	iResult = getaddrinfo(g_Config.proAdhocServer.c_str(),0,NULL,&resultAddr);
	if (iResult != 0) {
		ERROR_LOG(SCENET, "DNS error (%s)\n", g_Config.proAdhocServer.c_str());
		return iResult;
	}
	for (ptr = resultAddr; ptr != NULL; ptr = ptr->ai_next) {
		switch (ptr->ai_family) {
		case AF_INET:
			serverIp = ((sockaddr_in *)ptr->ai_addr)->sin_addr;
			break;
		}
	}
	server_addr.sin_addr = serverIp;
	iResult = connect(metasocket,(sockaddr *)&server_addr,sizeof(server_addr));
	if (iResult == SOCKET_ERROR) {
		uint8_t * sip = (uint8_t *)&server_addr.sin_addr.s_addr;
		ERROR_LOG(SCENET, "Socket error (%i) when connecting to %s/%u.%u.%u.%u:%u", errno, g_Config.proAdhocServer.c_str(), sip[0], sip[1], sip[2], sip[3], ntohs(server_addr.sin_port));
		return iResult;
	}
  memset(&parameter,0,sizeof(parameter));
  strcpy((char *)&parameter.nickname.data, g_Config.sNickName.c_str());
  parameter.channel = 1; // Fake Channel 1

  // Prepare Login Packet
  getLocalMac(&parameter.bssid.mac_addr);
  SceNetAdhocctlLoginPacketC2S packet;
  packet.base.opcode = OPCODE_LOGIN;
  SceNetEtherAddr addres;
  getLocalMac(&addres);
  packet.mac = addres;
  strcpy((char *)packet.name.data, g_Config.sNickName.c_str());
  memcpy(packet.game.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);
  int sent = send(metasocket, (char*)&packet, sizeof(packet), 0);
  changeBlockingMode(metasocket,1); // Change to non-blocking
  if(sent > 0){
    return 0;
  }else{
    return -1;
  }
#endif
}

int isBroadcastMAC(const SceNetEtherAddr * addr) {
  // Broadcast MAC
  if(memcmp(addr->data, "\xFF\xFF\xFF\xFF\xFF\xFF", ETHER_ADDR_LEN) == 0) return 1;
  // Normal MAC
  return 0;
}

int resolveIP(uint32_t ip, SceNetEtherAddr * mac) {
  sockaddr_in addr;
  getLocalIp(&addr);
  uint32 localIp = addr.sin_addr.s_addr;

  if(ip == localIp){
    getLocalMac(mac);
    return 0;
  }

  // Multithreading Lock
  peerlock.lock();

  // Peer Reference
  SceNetAdhocctlPeerInfo * peer = friends;

  // Iterate Peers
  for(; peer != NULL; peer = peer->next) {
    // Found Matching Peer
    if(peer->ip_addr == ip) {
      // Copy Data
      *mac = peer->mac_addr;

      // Multithreading Unlock
      peerlock.unlock();

      // Return Success
      return 0;
    }
  }

  // Multithreading Unlock
  peerlock.unlock();

  // Peer not found
  return -1;
}

int resolveMAC(SceNetEtherAddr * mac, uint32_t * ip) {
  // Get Local MAC Address
  SceNetEtherAddr localMac;
  getLocalMac(&localMac);
  // Local MAC Requested
  if(memcmp(&localMac, mac, sizeof(SceNetEtherAddr)) == 0) {
    // Get Local IP Address
    sockaddr_in sockAddr;
    getLocalIp(&sockAddr);
    *ip = sockAddr.sin_addr.s_addr;
    return 0; // return succes
  }

  // Multithreading Lock
  peerlock.lock();

  // Peer Reference
  SceNetAdhocctlPeerInfo * peer = friends;

  // Iterate Peers
  for(; peer != NULL; peer = peer->next) {
    // Found Matching Peer
    if(memcmp(&peer->mac_addr, mac, sizeof(SceNetEtherAddr)) == 0) {
      // Copy Data
      *ip = peer->ip_addr;

      // Multithreading Unlock
      peerlock.unlock();

      // Return Success
      return 0;
    }
  }

  // Multithreading Unlock
  peerlock.unlock();

  // Peer not found
  return -1;
}

int validNetworkName(const SceNetAdhocctlGroupName * group_name) {
  // Result
  int valid = 1;

  // Name given
  if(group_name != NULL) {
    // Iterate Name Characters
    int i = 0; for(; i < ADHOCCTL_GROUPNAME_LEN && valid; i++) {
      // End of Name
      if(group_name->data[i] == 0) break;

      // Not a digit
      if(group_name->data[i] < '0' || group_name->data[i] > '9') {
        // Not 'A' to 'Z'
        if(group_name->data[i] < 'A' || group_name->data[i] > 'Z') {
          // Not 'a' to 'z'
          if(group_name->data[i] < 'a' || group_name->data[i] > 'z') {
            // Invalid Name
            valid = 0;
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
  int num2 = (int)((num&secondMask)>>32);
  buff[0] = num1;
  buff[1] = num2;
}

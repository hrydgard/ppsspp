// Copyright (c) 2014- PPSSPP Project.

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


// proAdhocServer

// This is a direct port of Coldbird's code from http://code.google.com/p/aemu/
// All credit goes to him!

#pragma once

#include <cstdint>
#include <time.h>
#include "proAdhoc.h"

// Server Listening Port
//#define SERVER_PORT 27312

// Listener Connection Backlog (aka. Max Concurrent Logins)
#define SERVER_LISTEN_BACKLOG 128

// Server User Maximum
#define SERVER_USER_MAXIMUM 1024

// Server User Timeout (in seconds)
#define SERVER_USER_TIMEOUT 15

// Server SQLite3 Database
#define SERVER_DATABASE "database.db"

// Server Status Logfile
#define SERVER_STATUS_XMLOUT "www/status.xml"

// Server Shutdown Message
#define SERVER_SHUTDOWN_MESSAGE "ADHOC SERVER HUB IS SHUTTING DOWN!"

typedef struct db_crosslink{
	char id_from[PRODUCT_CODE_LENGTH + 1]; //SceNetAdhocctlProductCode id_from;
	char id_to[PRODUCT_CODE_LENGTH + 1]; //SceNetAdhocctlProductCode id_to;
} db_crosslink;

typedef struct db_productid {
	char id[PRODUCT_CODE_LENGTH + 1]; //SceNetAdhocctlProductCode id;
	char name[ADHOCCTL_NICKNAME_LEN]; //Title name
} db_productid;

/* PSPSTRUCTS */

// Ethernet Address (MAC)
/*#define ETHER_ADDR_LEN 6
typedef struct SceNetEtherAddr {
	uint8_t data[ETHER_ADDR_LEN];
} SceNetEtherAddr;

// Adhoc Virtual Network Name (1234ABCD)
#define ADHOCCTL_GROUPNAME_LEN 8
typedef struct SceNetAdhocctlGroupName {
	uint8_t data[ADHOCCTL_GROUPNAME_LEN];
} SceNetAdhocctlGroupName;

// Player Nickname
#define ADHOCCTL_NICKNAME_LEN 128
typedef struct SceNetAdhocctlNickname {
	uint8_t data[ADHOCCTL_NICKNAME_LEN];
} SceNetAdhocctlNickname;*/

/* PACKETS */

/*#define OPCODE_PING 0
#define OPCODE_LOGIN 1
#define OPCODE_CONNECT 2
#define OPCODE_DISCONNECT 3
#define OPCODE_SCAN 4
#define OPCODE_SCAN_COMPLETE 5
#define OPCODE_CONNECT_BSSID 6
#define OPCODE_CHAT 7

// PSP Product Code
#define PRODUCT_CODE_LENGTH 9
typedef struct
{
	// Game Product Code (ex. ULUS12345)
	char data[PRODUCT_CODE_LENGTH];
} PACK SceNetAdhocctlProductCode; // __attribute__((packed))

// Basic Packet
typedef struct
{
	uint8_t opcode;
} PACK SceNetAdhocctlPacketBase;

// C2S Login Packet
typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
	SceNetAdhocctlNickname name;
	SceNetAdhocctlProductCode game;
} PACK SceNetAdhocctlLoginPacketC2S;

// C2S Connect Packet
typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetAdhocctlGroupName group;
} PACK SceNetAdhocctlConnectPacketC2S;

// C2S Chat Packet
typedef struct
{
	SceNetAdhocctlPacketBase base;
	char message[64];
} PACK SceNetAdhocctlChatPacketC2S;

// S2C Connect Packet
typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetAdhocctlNickname name;
	SceNetEtherAddr mac;
	uint32_t ip;
} PACK SceNetAdhocctlConnectPacketS2C;

// S2C Disconnect Packet
typedef struct
{
	SceNetAdhocctlPacketBase base;
	uint32_t ip;
} PACK SceNetAdhocctlDisconnectPacketS2C;

// S2C Scan Packet
typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetAdhocctlGroupName group;
	SceNetEtherAddr mac;
} PACK SceNetAdhocctlScanPacketS2C;

// S2C Connect BSSID Packet
typedef struct
{
	SceNetAdhocctlPacketBase base;
	SceNetEtherAddr mac;
} PACK SceNetAdhocctlConnectBSSIDPacketS2C;

// S2C Chat Packet
typedef struct
{
	SceNetAdhocctlChatPacketC2S base;
	SceNetAdhocctlNickname name;
} PACK SceNetAdhocctlChatPacketS2C;*/

/* USER */

// User States
#define USER_STATE_WAITING 0
#define USER_STATE_LOGGED_IN 1
#define USER_STATE_TIMED_OUT 2

// PSP Resolver Information
typedef struct
{
	// PSP MAC Address
	SceNetEtherAddr mac;

	// PSP Hotspot IP Address
	uint32_t ip;

	// PSP Player Name
	SceNetAdhocctlNickname name;
} SceNetAdhocctlResolverInfo;

// Type Prototypes
typedef struct SceNetAdhocctlGameNode SceNetAdhocctlGameNode;
typedef struct SceNetAdhocctlGroupNode SceNetAdhocctlGroupNode;

// Double-Linked User List
typedef struct SceNetAdhocctlUserNode {
	// Next Element
	struct SceNetAdhocctlUserNode * next;

	// Previous Element
	struct SceNetAdhocctlUserNode * prev;

	// Next Element (Group)
	struct SceNetAdhocctlUserNode * group_next;

	// Previous Element
	struct SceNetAdhocctlUserNode * group_prev;

	// Resolver Information
	SceNetAdhocctlResolverInfo resolver;

	// Game Link
	SceNetAdhocctlGameNode * game;

	// Group Link
	SceNetAdhocctlGroupNode * group;

	// TCP Socket
	int stream;

	// Last Ping Update
	time_t last_recv;

	// RX Buffer
	uint8_t rx[1024];
	uint32_t rxpos;
} SceNetAdhocctlUserNode;

// Double-Linked Game List
struct SceNetAdhocctlGameNode {
	// Next Element
	struct SceNetAdhocctlGameNode * next;

	// Previous Element
	struct SceNetAdhocctlGameNode * prev;

	// PSP Game Product Code
	SceNetAdhocctlProductCode game;

	// Number of Players
	uint32_t playercount;

	// Number of Groups
	uint32_t groupcount;

	// Double-Linked Group List
	SceNetAdhocctlGroupNode * group;
};

// Double-Linked Group List
struct SceNetAdhocctlGroupNode {
	// Next Element
	struct SceNetAdhocctlGroupNode * next;

	// Previous Element
	struct SceNetAdhocctlGroupNode * prev;

	// Game Link
	SceNetAdhocctlGameNode * game;

	// PSP Adhoc Group Name
	SceNetAdhocctlGroupName group;

	// Number of Players
	uint32_t playercount;

	// Double-Linked Player List
	SceNetAdhocctlUserNode * player;
};

// User Count
extern uint32_t _db_user_count;

// User Database
extern SceNetAdhocctlUserNode * _db_user;

// Game Database
extern SceNetAdhocctlGameNode * _db_game;

void __AdhocServerInit();

/**
 * Login User into Database (Stream)
 * @param fd Socket
 * @param ip IP Address (Network Order)
 */
void login_user_stream(int fd, uint32_t ip);

/**
 * Login User into Database (Login Data)
 * @param user User Node
 * @param data Login Packet
 */
void login_user_data(SceNetAdhocctlUserNode * user, SceNetAdhocctlLoginPacketC2S * data);

/**
 * Logout User from Database
 * @param user User Node
 */
void logout_user(SceNetAdhocctlUserNode * user);

/**
 * Free Database Memory
 */
void free_database();

/**
 * Connect User to Game Group
 * @param user User Node
 * @param group Group Name
 */
void connect_user(SceNetAdhocctlUserNode * user, SceNetAdhocctlGroupName * group);

/**
 * Disconnect User from Game Group
 * @param user User Node
 */
void disconnect_user(SceNetAdhocctlUserNode * user);

/**
 * Send Game Group List
 * @param user User Node
 */
void send_scan_results(SceNetAdhocctlUserNode * user);

/**
 * Spread Chat Message in P2P Network
 * @param user Sender User Node
 * @param message Chat Message
 */
void spread_message(SceNetAdhocctlUserNode *user, const char *message);

/**
 * Get User State
 * @param user User Node
 */
int get_user_state(SceNetAdhocctlUserNode * user);

/**
 * Clear RX Buffer
 * @param user User Node
 * @param clear Number of Bytes to clear (-1 for all)
 */
void clear_user_rxbuf(SceNetAdhocctlUserNode * user, int clear);

/**
 * Patch Game Product Code
 * @param product To-be-patched Product Code
 * @param from If the Product Code matches this...
 * @param to ... then change it to this one.
 */
void game_product_relink(SceNetAdhocctlProductCode * product, char * from, char * to);

/**
 * Game Product Override (used for mixing multi-region games)
 * @param product IN: Source Product OUT: Override Product
 */
void game_product_override(SceNetAdhocctlProductCode * product);

/* STATUS */

/**
 * Update Status Logfile
 */
void update_status();

/**
* Server Entry Point
* @param argc Number of Arguments
* @param argv Arguments
* @return OS Error Code
*/
int proAdhocServerThread(int port); // (int argc, char * argv[])

//extern int _status;
extern std::atomic<bool> adhocServerRunning;
extern std::thread adhocServerThread;

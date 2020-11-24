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

#pragma once

#ifdef _MSC_VER
#pragma pack(push,1)
#endif

// Based on https://playstationdev.wiki/psvitadevwiki/index.php?title=Error_Codes
#define	SCE_NP_ERROR_ALREADY_INITIALIZED				0x80550001
#define	SCE_NP_ERROR_NOT_INITIALIZED					0x80550002
#define	SCE_NP_ERROR_INVALID_ARGUMENT					0x80550003

#define	SCE_NP_AUTH_ERROR_ALREADY_INITIALIZED			0x80550301
#define	SCE_NP_AUTH_ERROR_NOT_INITIALIZED				0x80550302
#define	SCE_NP_AUTH_ERROR_EINVAL						0x80550303
#define	SCE_NP_AUTH_ERROR_ENOMEM						0x80550304
#define	SCE_NP_AUTH_ERROR_ESRCH							0x80550305
#define	SCE_NP_AUTH_ERROR_EBUSY							0x80550306
#define	SCE_NP_AUTH_ERROR_ABORTED						0x80550307
#define	SCE_NP_AUTH_ERROR_INVALID_SERVICE_ID			0x80550308
#define	SCE_NP_AUTH_ERROR_INVALID_CREDENTIAL			0x80550309
#define	SCE_NP_AUTH_ERROR_INVALID_ENTITLEMENT_ID		0x8055030a
#define	SCE_NP_AUTH_ERROR_INVALID_DATA_LENGTH			0x8055030b
#define	SCE_NP_AUTH_ERROR_UNSUPPORTED_TICKET_VERSION	0x8055030c
#define	SCE_NP_AUTH_ERROR_STACKSIZE_TOO_SHORT			0x8055030d
#define	SCE_NP_AUTH_ERROR_TICKET_STATUS_CODE_INVALID	0x8055030e
#define	SCE_NP_AUTH_ERROR_TICKET_PARAM_NOT_FOUND		0x8055030f
#define	SCE_NP_AUTH_ERROR_INVALID_TICKET_VERSION		0x80550310
#define	SCE_NP_AUTH_ERROR_INVALID_ARGUMENT				0x80550311

#define	SCE_NP_AUTH_ERROR_SERVICE_END					0x80550400
#define	SCE_NP_AUTH_ERROR_SERVICE_DOWN					0x80550401
#define	SCE_NP_AUTH_ERROR_SERVICE_BUSY					0x80550402

// Based on https://github.com/RPCS3/rpcs3/blob/psp2/rpcs3/Emu/PSP2/Modules/sceNpCommon.h
enum SceNpServiceState : s32
{
	SCE_NP_SERVICE_STATE_UNKNOWN = 0,
	SCE_NP_SERVICE_STATE_SIGNED_OUT,
	SCE_NP_SERVICE_STATE_SIGNED_IN,
	SCE_NP_SERVICE_STATE_ONLINE
};

struct SceNpCommunicationId
{
	char data[9];
	char term;
	u8 num;
	char dummy;
};

struct SceNpCommunicationPassphrase
{
	u8 data[128];
};

struct SceNpCommunicationSignature
{
	u8 data[160];
};

struct SceNpCommunicationConfig
{
	PSPPointer<SceNpCommunicationId> commId;
	PSPPointer<SceNpCommunicationPassphrase> commPassphrase;
	PSPPointer<SceNpCommunicationSignature> commSignature;
};

struct SceNpCountryCode
{
	char data[2];
	char term;
	char padding[1];
};

// Username?
struct SceNpOnlineId
{
	char data[16];
	char term;
	char dummy[3];
};

struct SceNpId
{
	SceNpOnlineId handle;
	u8 opt[8];
	u8 reserved[8];
};

struct SceNpAvatarUrl
{
	char data[127];
	char term;
};

struct SceNpUserInformation
{
	SceNpId userId;
	SceNpAvatarUrl icon;
	u8 reserved[52];
};

struct SceNpMyLanguages
{
	s32_le language1;
	s32_le language2;
	s32_le language3;
	u8 padding[4];
};

struct SceNpAvatarImage
{
	u8 data[200 * 1024];
	u32_le size;
	u8 reserved[12];
};

enum SceNpAvatarSizeType : s32
{
	SCE_NP_AVATAR_SIZE_LARGE,
	SCE_NP_AVATAR_SIZE_MIDDLE,
	SCE_NP_AVATAR_SIZE_SMALL
};

struct SceNpAboutMe
{
	char data[64];
};

struct SceNpDate
{
	u16_le year;
	u8 month;
	u8 day;
};

union SceNpTicketParam
{
	s32_le _s32;
	s64_le _s64;
	u32_le _u32;
	u64_le _u64;
	SceNpDate date;
	u8 data[256];
};

struct SceNpTicketVersion
{
	u16_le major;
	u16_le minor;
};

struct NpAuthHandler {
	u32 entryPoint;
	u32 argument;
};

struct NpAuthArgs {
	u32_le data[3]; // id, result, ArgAddr
};

using SceNpAuthCallback = s32(s32 id, s32 result, PSPPointer<void> arg);

struct SceNpAuthRequestParameter
{
	u32_le size; // Size of this struct
	SceNpTicketVersion version; // Highest ticket version supported by this game/device? so PSN server can return supported ticket
	u32_le serviceIdAddr; //PSPPointer<char> serviceId; // null-terminated string
	u32_le cookieAddr; //PSPPointer<char> cookie; // null-terminated string?
	u32_le cookieSize;
	u32_le entitlementIdAddr; //PSPPointer<char> entitlementId; // null-terminated string
	u32_le consumedCount; // related to entitlement?
	u32 ticketCbAddr; //PSPPointer<SceNpAuthCallback> ticketCb
	u32_le cbArgAddr; //PSPPointer<void> cbArg
};

struct SceNpEntitlementId
{
	u8 data[32];
};

struct SceNpEntitlement
{
	SceNpEntitlementId id;
	u64_le createdDate;
	u64_le expireDate;
	u32_le type;
	s32_le remainingCount;
	u32_le consumedCount;
	u8 padding[4];
};

#define	TICKET_VER_2_0	0x21000000;
#define	TICKET_VER_2_1	0x21010000;
#define	TICKET_VER_3_0	0x31000000;
#define	TICKET_VER_4_0	0x41000000;

#define	NUMBER_PARAMETERS			12

#define	PARAM_TYPE_NULL				0
#define	PARAM_TYPE_INT				1
#define	PARAM_TYPE_LONG				2
#define	PARAM_TYPE_STRING			4 // PSP returns maximum 255 bytes
#define	PARAM_TYPE_DATE				7
#define	PARAM_TYPE_STRING_ASCII		8 // PSP returns maximum 255 bytes, can contains control code

#define	SECTION_TYPE_BODY			0x3000
#define	SECTION_TYPE_FOOTER			0x3002

// Tickets data are in big-endian based on captured packets
struct SceNpTicketParamData
{
	u16_be type;
	u16_be length;
	//u8 value[]; // optional data
};

struct SceNpTicketHeader
{
	u32_be version; // Version contents byte are: V1 0M 00 00, where V = major version, M = minor version according to https://www.psdevwiki.com/ps3/X-I-5-Ticket
	s32_be size; // total ticket size (excluding this 8-bytes header struct)
};

// Section contents byte are: 30 XX 00 YY, where XX = section type, YY = section size according to https://www.psdevwiki.com/ps3/X-I-5-Ticket
// A section can contain other sections or param data, thus sharing their enum/def?
struct SceNpTicketSection
{
	u16_be type; // section type? ie. 30 XX where known XX are 00, 02, 10, 11
	u16_be size; // total section size (excluding this 4-bytes section delimiter struct)
};

struct SceNpTicket
{
	SceNpTicketHeader header;
	SceNpTicketSection section; // Body or Parameter sections?
	//SceNpTicketParamData parameters[]; // a list of TicketParamData
	//u8 unknownBytes[]; // optional data?
};

#define	PARENTAL_CONTROL_DISABLED	0
#define	PARENTAL_CONTROL_ENABLED	1

#define	STATUS_ACCOUNT_SUSPENDED					0x80
#define	STATUS_ACCOUNT_CHAT_RESTRICTED				0x100
#define	STATUS_ACCOUNT_PARENTAL_CONTROL_ENABLED		0x200

struct SceNpAuthMemoryStat {
	int npMemSize;     // Memory allocated by the NP utility.
	int npMaxMemSize;  // Maximum memory used by the NP utility.
	int npFreeMemSize; // Free memory available to use by the NP utility.
};

#ifdef _MSC_VER
#pragma pack(pop)
#endif


extern std::recursive_mutex npAuthEvtMtx;
extern std::deque<NpAuthArgs> npAuthEvents;
extern std::map<int, NpAuthHandler> npAuthHandlers;

void Register_sceNp();
void Register_sceNpCommerce2();
void Register_sceNpService();
void Register_sceNpAuth();

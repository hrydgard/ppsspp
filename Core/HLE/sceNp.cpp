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

// This is pretty much a stub implementation. Doesn't actually do anything, just tries to return values
// to keep games happy anyway.

#include <mutex>
#include <deque>
#include <map>
#include <StringUtils.h>
#include "Core/MemMapHelpers.h"
#include <Core/CoreTiming.h>
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceNp.h"


bool npAuthInited = false;
int npSigninState = NP_SIGNIN_STATUS_NONE;
SceNpAuthMemoryStat npAuthMemStat = {};
SceNpAuthMemoryStat npMatching2MemStat = {};
PSPTimeval npSigninTimestamp{};

// TODO: These should probably be grouped in a struct, since they're used to generate an auth ticket
int npParentalControl = PARENTAL_CONTROL_ENABLED;
int npUserAge = 24; // faking user Age to 24 yo
int npChatRestriction = 0; // default/initial value on Patapon 3 is 1 (restricted boolean?)
SceNpMyLanguages npMyLangList = { 1033, 2057, 1036 };
char npCountryCode[3] = "fr"; // dummy data taken from https://www.psdevwiki.com/ps3/X-I-5-Ticket
char npRegionCode[3] = "c9"; // not sure what "c9" meant, since it was close to country code data, might be region-related data?
std::string npOnlineId = "DummyOnlineId"; // SceNpOnlineId struct?
std::string npServiceId = ""; // UNO game uses EP2006-NPEH00020_00
std::string npAvatarUrl = "http://DummyAvatarUrl"; // SceNpAvatarUrl struct?

bool npMatching2Inited = false;
SceNpCommunicationId npTitleId;

std::recursive_mutex npAuthEvtMtx;
std::deque<NpAuthArgs> npAuthEvents;
std::map<int, NpAuthHandler> npAuthHandlers;

// TODO: Moves NpMatching2-related stuff to sceNp2.cpp
std::recursive_mutex npMatching2EvtMtx;
std::deque<NpMatching2Args> npMatching2Events;
std::map<int, NpMatching2Handler> npMatching2Handlers;
//std::map<int, NpMatching2Context> npMatching2Contexts;


// Tickets data are in big-endian based on captured packets
int writeTicketParam(u8* buffer, const u16_be type, const char* data = nullptr, const u16_be size = 0) {
	if (buffer == nullptr) return 0;

	u16_be sz = (data == nullptr)? static_cast<u16_be>(0): size;
	memcpy(buffer, &type, 2);
	memcpy(buffer + 2, &sz, 2);
	if (sz > 0 && data != nullptr) 
		memcpy(buffer + 4, data, sz);

	return sz + 4;
}

int writeTicketStringParam(u8* buffer, const u16_be type, const char* data = nullptr, const u16_be size = 0) {
	if (buffer == nullptr) return 0;

	u16_be sz = (data == nullptr) ? static_cast<u16_be>(0) : size;
	memcpy(buffer, &type, 2);
	memcpy(buffer + 2, &sz, 2);
	if (sz > 0) {
		memset(buffer + 4, 0, sz);
		truncate_cpy((char*)buffer + 4, sz, data);
	}
	return sz + 4;
}

int writeTicketU32Param(u8* buffer, const u16_be type, const u32_be data) {
	if (buffer == nullptr) return 0;
	
	u16_be sz = 4;
	memcpy(buffer, &type, 2);
	memcpy(buffer + 2, &sz, 2);
	memcpy(buffer + 4, &data, 4);

	return sz + 4;
}

int writeTicketU64Param(u8* buffer, const u16_be type, const u64_be data) {
	if (buffer == nullptr) return 0;

	u16_be sz = 8;
	memcpy(buffer, &type, 2);
	memcpy(buffer + 2, &sz, 2);
	memcpy(buffer + 4, &data, sz);

	return sz + 4;
}

void notifyNpAuthHandlers(u32 id, u32 result, u32 argAddr) {
	std::lock_guard<std::recursive_mutex> npAuthGuard(npAuthEvtMtx);
	npAuthEvents.push_back({ id, result, argAddr });
}

// serverId: 0 on 0x0103/0x0104/0x0105/0x0107/0x0108/0x0109/0x010a/0x010b/0x010c/0x010d (ie. when already joined to a server?)
// unk1~unk5 usually 0, 
// unk1: 1st 32-bit of LeaveRoom/etc's Arg2 on 0x0103/0x0104/0x0105/0x0107/0x0108/0x0109/0x010a/0x010b/0x010c/0x010d/0x010e
// unk2: 2nd 32-bit of LeaveRoom/etc's Arg2 on 0x0103/0x0104/0x0105/0x0107/0x0108/0x0109/0x010a/0x010b/0x010c/0x010d/0x010e
// unk5: 1 on 0x0002/0x0003/0x0005/0x0006/0x0007/0x0101/0x0102/0x0106
// unk6 (new state?): 8-bit?(masked with 0xff) 0x01 on 0x0001, 0x03 on 0x0002, 0x04 on 0x0003, 0x05 on 0x0004, 0x06 on 0x0005, 0x07 on 0x0006, 0x08 on 0x0007,
//		0x09 on 0x0101, 0x0A on 0x0102, 0x0C on 0x0103, 0x0D on 0x0104, 0x0E on 0x0105, 0x0F on 0x0106, 0x10 on 0x0107, 0x11 on 0x0108,
//		0x12 on 0x0109, 0x13 on 0x010a, 0x14 on 0x010b, 0x15 on 0x010c, 0x16 on 0x010d, 0x17 on 0x010e, 0x18 on 0xa102
void notifyNpMatching2Handlers(NpMatching2Args &args, u32 ctxId, u32 serverId, u32 cbFuncAddr, u32 cbArgAddr, u32 unk3, u32 unk4, u32 unk5, u8 unk6) {
	std::lock_guard<std::recursive_mutex> npMatching2Guard(npMatching2EvtMtx);
	// TODO: separate/map each list per ctxId
	npMatching2Events.push_back(args);
}

static int sceNpInit()
{
	ERROR_LOG(SCENET, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpTerm()
{
	// No parameters
	ERROR_LOG(SCENET, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpGetContentRatingFlag(u32 parentalControlAddr, u32 userAgeAddr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x, %08x)", __FUNCTION__, parentalControlAddr, userAgeAddr);

	if (!Memory::IsValidAddress(parentalControlAddr) || !Memory::IsValidAddress(userAgeAddr))
		return hleLogError(SCENET, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	INFO_LOG(SCENET, "%s - Parental Control: %d", __FUNCTION__, npParentalControl);
	INFO_LOG(SCENET, "%s - User Age: %d", __FUNCTION__, npUserAge);

	Memory::Write_U32(npParentalControl, parentalControlAddr);
	Memory::Write_U32(npUserAge, userAgeAddr);

	return 0;
}

static int sceNpGetChatRestrictionFlag(u32 flagAddr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x)", __FUNCTION__, flagAddr);

	if (!Memory::IsValidAddress(flagAddr))
		return hleLogError(SCENET, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	INFO_LOG(SCENET, "%s - Chat Restriction: %d", __FUNCTION__, npChatRestriction);

	Memory::Write_U32(npChatRestriction, flagAddr);

	return 0;
}

static int sceNpGetOnlineId(u32 idPtr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x)", __FUNCTION__, idPtr);

	if (!Memory::IsValidAddress(idPtr))
		return hleLogError(SCENET, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	SceNpOnlineId dummyOnlineId{};
	truncate_cpy(dummyOnlineId.data, sizeof(dummyOnlineId.data), npOnlineId.c_str());

	INFO_LOG(SCENET, "%s - Online ID: %s", __FUNCTION__, dummyOnlineId.data);

	Memory::WriteStruct(idPtr, &dummyOnlineId);

	return 0;
}

int NpGetNpId(SceNpId* npid)
{
	truncate_cpy(npid->handle.data, sizeof(npid->handle.data), npOnlineId.c_str());
	return 0;
}

static int sceNpGetNpId(u32 idPtr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x)", __FUNCTION__, idPtr);

	if (!Memory::IsValidAddress(idPtr))
		return hleLogError(SCENET, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	SceNpId dummyNpId{};
	int retval = NpGetNpId(&dummyNpId);
	if (retval < 0)
		return hleLogError(SCENET, retval);

	INFO_LOG(SCENET, "%s - Online ID: %s", __FUNCTION__, dummyNpId.handle.data);
	std::string datahex;
	DataToHexString(dummyNpId.opt, sizeof(dummyNpId.opt), &datahex);
	INFO_LOG(SCENET, "%s - Options?: %s", __FUNCTION__, datahex.c_str());

	Memory::WriteStruct(idPtr, &dummyNpId);

	return 0;
}

static int sceNpGetAccountRegion(u32 countryCodePtr, u32 regionCodePtr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x, %08x)", __FUNCTION__, countryCodePtr, regionCodePtr);

	if (!Memory::IsValidAddress(countryCodePtr) || !Memory::IsValidAddress(regionCodePtr))
		return hleLogError(SCENET, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	SceNpCountryCode dummyCountryCode{};
	memcpy(dummyCountryCode.data, npCountryCode, sizeof(dummyCountryCode.data));
	SceNpCountryCode dummyRegionCode{};
	memcpy(dummyRegionCode.data, npRegionCode, sizeof(dummyRegionCode.data));

	INFO_LOG(SCENET, "%s - Country Code: %d", __FUNCTION__, dummyCountryCode.data);
	INFO_LOG(SCENET, "%s - Region? Code: %d", __FUNCTION__, dummyRegionCode.data);

	Memory::WriteStruct(countryCodePtr, &dummyCountryCode);
	Memory::WriteStruct(regionCodePtr, &dummyRegionCode);

	return 0;
}

static int sceNpGetMyLanguages(u32 langListPtr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x)", __FUNCTION__, langListPtr);

	if (!Memory::IsValidAddress(langListPtr))
		return hleLogError(SCENET, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	INFO_LOG(SCENET, "%s - Language1 Code: %d", __FUNCTION__, npMyLangList.language1);
	INFO_LOG(SCENET, "%s - Language2 Code: %d", __FUNCTION__, npMyLangList.language2);
	INFO_LOG(SCENET, "%s - Language3 Code: %d", __FUNCTION__, npMyLangList.language3);

	Memory::WriteStruct(langListPtr, &npMyLangList);

	return 0;
}

static int sceNpGetUserProfile(u32 profilePtr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x)", __FUNCTION__, profilePtr);

	if (!Memory::IsValidAddress(profilePtr))
		return hleLogError(SCENET, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	SceNpUserInformation dummyProfile{};
	truncate_cpy(dummyProfile.userId.handle.data, sizeof(dummyProfile.userId.handle.data), npOnlineId.c_str());
	truncate_cpy(dummyProfile.icon.data, sizeof(dummyProfile.icon.data), npAvatarUrl.c_str());

	INFO_LOG(SCENET, "%s - Online ID: %s", __FUNCTION__, dummyProfile.userId.handle.data);
	std::string datahex;
	DataToHexString(dummyProfile.userId.opt, sizeof(dummyProfile.userId.opt), &datahex);
	INFO_LOG(SCENET, "%s - Options?: %s", __FUNCTION__, datahex.c_str());
	INFO_LOG(SCENET, "%s - Avatar URL: %s", __FUNCTION__, dummyProfile.icon.data);

	Memory::WriteStruct(profilePtr, &dummyProfile);

	return 0;
}

const HLEFunction sceNp[] = {
	{0X857B47D3, &WrapI_V<sceNpInit>,					"sceNpInit",					'i', ""   },
	{0X37E1E274, &WrapI_V<sceNpTerm>,					"sceNpTerm",					'i', ""   },
	{0XBB069A87, &WrapI_UU<sceNpGetContentRatingFlag>,	"sceNpGetContentRatingFlag",	'i', "xx" },
	{0X1D60AE4B, &WrapI_U<sceNpGetChatRestrictionFlag>,	"sceNpGetChatRestrictionFlag",	'i', "x"  },
	{0x4B5C71C8, &WrapI_U<sceNpGetOnlineId>,			"sceNpGetOnlineId",				'i', "x"  },
	{0x633B5F71, &WrapI_U<sceNpGetNpId>,				"sceNpGetNpId",					'i', "x"  },
	{0x7E0864DF, &WrapI_U<sceNpGetUserProfile>,			"sceNpGetUserProfile",			'i', "x"  },
	{0xA0BE3C4B, &WrapI_UU<sceNpGetAccountRegion>,		"sceNpGetAccountRegion",		'i', "xx" },
	{0xCDCC21D3, &WrapI_U<sceNpGetMyLanguages>,			"sceNpGetMyLanguages",			'i', "x"  },
};

void Register_sceNp()
{
	RegisterModule("sceNp", ARRAY_SIZE(sceNp), sceNp);
}

static int sceNpAuthTerm()
{
	// No parameters
	ERROR_LOG(SCENET, "UNIMPL %s()", __FUNCTION__);
	npAuthInited = false;
	return 0;
}

static int sceNpAuthInit(u32 poolSize, u32 stackSize, u32 threadPrio) 
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %d, %d)", __FUNCTION__, poolSize, stackSize, threadPrio);
	npAuthMemStat.npMemSize = poolSize - 0x20;
	npAuthMemStat.npMaxMemSize = 0x4050; // Dummy maximum foot print
	npAuthMemStat.npFreeMemSize = npAuthMemStat.npMemSize;
	npAuthEvents.clear();

	npAuthInited = true;
	return 0;
}

int sceNpAuthGetMemoryStat(u32 memStatAddr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x)", __FUNCTION__, memStatAddr);

	if (!Memory::IsValidAddress(memStatAddr))
		return hleLogError(SCENET, SCE_NP_AUTH_ERROR_INVALID_ARGUMENT, "invalid arg");

	Memory::WriteStruct(memStatAddr, &npAuthMemStat);

	return 0;
}

/* 
"Authenticating matching server usage license" on Patapon 3. Could be waiting for a state change for eternity? probably need to trigger a callback handler?
TODO: Login to "https://auth.np.ac.playstation.net/nav/auth" based on https://www.psdevwiki.com/ps3/Online_Connections
param seems to be a struct where offset:
	+00: 32-bit is the size of the struct (ie. 36 bytes),
	+04: 32-bit is also a small number (ie. 3) a mode/event/flag/version may be?,
	+08: 32-bit is a pointer to a productId? (ie. "EP9000-UCES01421_00"),
	+0C: 4x 32-bit reserved? all zero
	+1C: 32-bit callback handler? optional handler? seems to be a valid pointer and pointing to a starting point of a function (have a label on the disassembly)
	+20: 32-bit a pointer to a random data (4 to 8-bytes data max? both 2x 32-bit seems to be a valid pointer). optional handler args?
return value >= 0 and <0 seems to be stored at a different location by the game (valid result vs error code?)
*/
int sceNpAuthCreateStartRequest(u32 paramAddr)
{
	WARN_LOG(SCENET, "UNTESTED %s(%08x) at %08x", __FUNCTION__, paramAddr, currentMIPS->pc);

	if (!Memory::IsValidAddress(paramAddr))
		return hleLogError(SCENET, SCE_NP_AUTH_ERROR_INVALID_ARGUMENT, "invalid arg");

	SceNpAuthRequestParameter params = {};
	int size = Memory::Read_U32(paramAddr);
	Memory::Memcpy(&params, paramAddr, size);
	npServiceId = Memory::GetCharPointer(params.serviceIdAddr);

	INFO_LOG(SCENET, "%s - Max Version: %u.%u", __FUNCTION__, params.version.major, params.version.minor);
	INFO_LOG(SCENET, "%s - Service ID: %s", __FUNCTION__, Memory::GetCharPointer(params.serviceIdAddr));
	INFO_LOG(SCENET, "%s - Entitlement ID: %s", __FUNCTION__, Memory::GetCharPointer(params.entitlementIdAddr));
	INFO_LOG(SCENET, "%s - Consumed Count: %d", __FUNCTION__, params.consumedCount);
	INFO_LOG(SCENET, "%s - Cookie (size = %d): %s", __FUNCTION__, params.cookieSize, Memory::GetCharPointer(params.cookieAddr));

	u32 retval = 0;
	if (params.size >= 32 && params.ticketCbAddr != 0) {
		bool foundHandler = false;

		struct NpAuthHandler handler;
		memset(&handler, 0, sizeof(handler));

		while (npAuthHandlers.find(retval) != npAuthHandlers.end())
			++retval;

		handler.entryPoint = params.ticketCbAddr;
		handler.argument = params.cbArgAddr;

		for (std::map<int, NpAuthHandler>::iterator it = npAuthHandlers.begin(); it != npAuthHandlers.end(); it++) {
			if (it->second.entryPoint == handler.entryPoint) {
				foundHandler = true;
				retval = it->first;
				break;
			}
		}

		if (!foundHandler && Memory::IsValidAddress(handler.entryPoint)) {
			npAuthHandlers[retval] = handler;
			WARN_LOG(SCENET, "%s - Added handler(%08x, %08x) : %d", __FUNCTION__, handler.entryPoint, handler.argument, retval);
		}
		else {
			ERROR_LOG(SCENET, "%s - Same handler(%08x, %08x) already exists", __FUNCTION__, handler.entryPoint, handler.argument);
		}
		// Patapon 3 will only Abort & Destroy AuthRequest if the ID is larger than 0. Is 0 a valid request id?
		retval++;

		// 1st Arg usually either an ID returned from Create/AddHandler function or an Event ID if the game is expecting a sequence of events.
		// 2nd Arg seems to be used if not a negative number and exits the handler if it's negative (error code?)
		// 3rd Arg seems to be a data (ie. 92 bytes of data?) pointer and tested for null within callback handler (optional callback args?)
		u32 ticketLength = 248; // default ticket length? should be updated using the ticket length returned from login
		notifyNpAuthHandlers(retval, ticketLength, (params.size >= 36) ? params.cbArgAddr : 0);
	}

	//hleDelayResult(0, "give time", 500000);
	return retval;
}

// Used within callback of sceNpAuthCreateStartRequest (arg1 = callback's args[0], arg2 = output structPtr?, arg3 = callback's args[1])
// Is this using request id for Arg1 or cbId?
// JPCSP is using length = 248 for dummy ticket
int sceNpAuthGetTicket(u32 requestId, u32 bufferAddr, u32 length)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %d) at %08x", __FUNCTION__, requestId, bufferAddr, length, currentMIPS->pc);

	if (!Memory::IsValidAddress(bufferAddr))
		return hleLogError(SCENET, SCE_NP_AUTH_ERROR_INVALID_ARGUMENT, "invalid arg");

	int result = length;
	Memory::Memset(bufferAddr, 0, length);
	SceNpTicket ticket = {};
	// Dummy Login ticket returned as Login response. Dummy ticket contents were taken from https://www.psdevwiki.com/ps3/X-I-5-Ticket
	ticket.header.version = TICKET_VER_2_1;
	ticket.header.size = 0xF0; // size excluding the header
	u8* buf = Memory::GetPointerWrite(bufferAddr + sizeof(ticket));
	int ofs = 0;
	ofs += writeTicketParam(buf, PARAM_TYPE_STRING_ASCII, "\x4c\x47\x56\x3b\x81\x39\x4a\x22\xd8\x6b\xc1\x57\x71\x6e\xfd\xb8\xab\x63\xcc\x51", 20); // 20 random letters, token key or SceNpSignature?
	ofs += writeTicketU32Param(buf + ofs, PARAM_TYPE_INT, 0x0100); // a flags?
	PSPTimeval tv; //npSigninTimestamp
	__RtcTimeOfDay(&tv);
	u64 now = 1000ULL*tv.tv_sec + tv.tv_usec/1000ULL; // in milliseconds, since 1900?	 
	ofs += writeTicketU64Param(buf + ofs, PARAM_TYPE_DATE, now);
	ofs += writeTicketU64Param(buf + ofs, PARAM_TYPE_DATE, now + 10 * 60 * 1000); // now + 10 minutes, expired time?
	ofs += writeTicketU64Param(buf + ofs, PARAM_TYPE_LONG, 0x592e71c546e86859); // seems to be consistent, 8-bytes password hash may be? or related to entitlement? or console id?
	ofs += writeTicketStringParam(buf + ofs, PARAM_TYPE_STRING, npOnlineId.c_str(), 32); // username
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_STRING_ASCII, npCountryCode, 4); // SceNpCountryCode ? ie. "fr" + 00 02
	ofs += writeTicketStringParam(buf + ofs, PARAM_TYPE_STRING, npRegionCode, 4); // 2-char code? related to country/lang code? ie. "c9" + 00 00
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_STRING_ASCII, npServiceId.c_str(), 24);
	int status = 0;
	if (npParentalControl == PARENTAL_CONTROL_ENABLED) {
		status |= STATUS_ACCOUNT_PARENTAL_CONTROL_ENABLED;
	}
	status |= (npUserAge & 0x7F) << 24;
	ofs += writeTicketU32Param(buf + ofs, PARAM_TYPE_INT, status);
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_NULL);
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_NULL);
	ticket.section.type = SECTION_TYPE_BODY;
	ticket.section.size = ofs;
	Memory::WriteStruct(bufferAddr, &ticket);
	SceNpTicketSection footer = { SECTION_TYPE_FOOTER, 32 }; // footer section? ie. 32-bytes on version 2.1 containing 4-chars ASCII + 20-chars ASCII
	Memory::WriteStruct(bufferAddr + sizeof(ticket) + ofs, &footer);
	ofs += sizeof(footer);
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_STRING_ASCII, "\x34\xcd\x3c\xa9", 4);
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_STRING_ASCII, "\x3a\x4b\x42\x66\x92\xda\x6b\x7c\xb7\x4c\xe8\xd9\x4f\x2b\x77\x15\x91\xb8\xa4\xa9", 20); // 20 random letters, token key or SceNpSignature?
	u8 unknownBytes[36] = {}; // includes Language list?
	Memory::WriteStruct(bufferAddr + sizeof(ticket) + ofs, unknownBytes);

	result = ticket.header.size + sizeof(ticket.header); // dummy ticket is 248 bytes

	return result;
}

// Used within callback of sceNpAuthCreateStartRequest (arg1 = structPtr?, arg2 = callback's args[1], arg3 = DLCcode? ie. "EP9000-UCES01421_00-DLL001", arg4 = Patapon 3 always set to 0?)
// Patapon 3 will loop (for each DLC?) through an array of 4+4 bytes, ID addr (pchar) + result (int). Each loop calls this function using the same ticket addr but use different ID addr (arg3) and store the return value in result field (default/initial = -1)
int sceNpAuthGetEntitlementById(u32 ticketBufferAddr, u32 ticketLength, u32 entitlementIdAddr, u32 arg4)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x, %d, %08x, %d)", __FUNCTION__, ticketBufferAddr, ticketLength, entitlementIdAddr, arg4);
	INFO_LOG(SCENET, "%s - Entitlement ID: %s", __FUNCTION__, Memory::GetCharPointer(entitlementIdAddr));

	// Do we return the entitlement through function result? or update the ticket content? or replace the arg3 data with SceNpEntitlement struct?
	return 1; // dummy value assuming it's a boolean/flag, since we don't know how to return the entitlement result yet
}

int sceNpAuthAbortRequest(int requestId)
{
	WARN_LOG(SCENET, "UNTESTED %s(%i)", __FUNCTION__, requestId);
	// TODO: Disconnect HTTPS connection & cancel the callback event
	std::lock_guard<std::recursive_mutex> npAuthGuard(npAuthEvtMtx);
	for (auto it = npAuthEvents.begin(); it != npAuthEvents.end(); ) {
		(it->data[0] == requestId) ? it = npAuthEvents.erase(it) : ++it;
	}

	return 0;
}

int sceNpAuthDestroyRequest(int requestId)
{
	WARN_LOG(SCENET, "UNTESTED %s(%i)", __FUNCTION__, requestId);
	// Remove callback handler
	int handlerID = requestId - 1;
	if (npAuthHandlers.find(handlerID) != npAuthHandlers.end()) {
		npAuthHandlers.erase(handlerID);
		WARN_LOG(SCENET, "%s: Deleted handler %d", __FUNCTION__, handlerID);
	}
	else {
		ERROR_LOG(SCENET, "%s: Invalid request ID %d", __FUNCTION__, requestId);
	}

	// Patapon 3 is checking for error code 0x80550402
	return 0;
}

int sceNpAuthGetTicketParam(u32 ticketBufPtr, int ticketLen, int paramNum, u32 bufferPtr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x, %0d, %d, %08x) at %08x", __FUNCTION__, ticketBufPtr, ticketLen, paramNum, bufferPtr, currentMIPS->pc);
	const u32 PARAM_BUFFER_MAX_SIZE = 256;
	Memory::Memset(bufferPtr, 0, PARAM_BUFFER_MAX_SIZE); // JPCSP: This clear is always done, even when an error is returned
	if (paramNum < 0 || paramNum >= NUMBER_PARAMETERS) {
		return SCE_NP_MANAGER_ERROR_INVALID_ARGUMENT;
	}

	SceNpTicket* ticket = (SceNpTicket*)Memory::GetPointer(ticketBufPtr);
	u32 inbuf = ticketBufPtr;
	inbuf += sizeof(ticket->header);
	inbuf += ticket->section.size + sizeof(ticket->section);
	u32 outbuf = bufferPtr;
	for (int i = 0; i < paramNum; i++) {
		SceNpTicketParamData* ticketParam = (SceNpTicketParamData*)Memory::GetPointer(inbuf);
		u32 sz = (u32)sizeof(SceNpTicketParamData) + ticketParam->length;
		Memory::Memcpy(outbuf, inbuf, sz);
		DEBUG_LOG(SCENET, "%s - Param #%d: Type = %04x, Length = %u", __FUNCTION__, i, ticketParam->type, static_cast<unsigned int>(ticketParam->length));
		outbuf += sz;
		inbuf += sz;
		if (outbuf - bufferPtr >= PARAM_BUFFER_MAX_SIZE || inbuf - ticketBufPtr >= (u32)ticketLen)
			break;
	}

	return 0;
}

const HLEFunction sceNpAuth[] = {
	{0X4EC1F667, &WrapI_V<sceNpAuthTerm>,						"sceNpAuthTerm",					'i', ""     },
	{0XA1DE86F8, &WrapI_UUU<sceNpAuthInit>,						"sceNpAuthInit",					'i', "xxx"  },
	{0XF4531ADC, &WrapI_U<sceNpAuthGetMemoryStat>,				"sceNpAuthGetMemoryStat",			'i', "x"    },
	{0XCD86A656, &WrapI_U<sceNpAuthCreateStartRequest>,			"sceNpAuthCreateStartRequest",		'i', "x"    },
	{0X3F1C1F70, &WrapI_UUU<sceNpAuthGetTicket>,				"sceNpAuthGetTicket",				'i', "xxx"  },
	{0X6900F084, &WrapI_UUUU<sceNpAuthGetEntitlementById>,		"sceNpAuthGetEntitlementById",		'i', "xxxx" },
	{0XD99455DD, &WrapI_I<sceNpAuthAbortRequest>,				"sceNpAuthAbortRequest",			'i', "i"    },
	{0X72BB0467, &WrapI_I<sceNpAuthDestroyRequest>,				"sceNpAuthDestroyRequest",			'i', "i"    },
	{0x5A3CB57A, &WrapI_UIIU<sceNpAuthGetTicketParam>,			"sceNpAuthGetTicketParam",			'i', "xiix" },
	{0x75FB0AE3, nullptr,										"sceNpAuthGetEntitlementIdList",	'i', ""     },
};

void Register_sceNpAuth()
{
	RegisterModule("sceNpAuth", ARRAY_SIZE(sceNpAuth), sceNpAuth);
}

static int sceNpServiceTerm()
{
	// No parameters
	ERROR_LOG(SCENET, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpServiceInit(u32 poolSize, u32 stackSize, u32 threadPrio) 
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x, %08x, %08x)", __FUNCTION__, poolSize, stackSize, threadPrio);
	return 0;
}

// FIXME: On Patapon 3 the Arg is pointing to a String, but based on RPCS3 the Arg is an Id integer ?
static int sceNpLookupCreateTransactionCtx(u32 lookupTitleCtxIdAddr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x)", __FUNCTION__, lookupTitleCtxIdAddr);
	INFO_LOG(SCENET, "%s - Title ID: %s", __FUNCTION__, Memory::GetCharPointer(lookupTitleCtxIdAddr));
	// Patapon 3 will only Destroy if returned Id > 0. Is 0 a valid id?
	return 1; // returning dummy transaction id
}

// transId: id returned from sceNpLookupCreateTransactionCtx
static int sceNpLookupDestroyTransactionCtx(s32 transId)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d)", __FUNCTION__, transId);
	return 0;
}

// transId: id returned from sceNpLookupCreateTransactionCtx
// Patapon 3 always set Arg5 to 0
// Addr args have something to do with GameUpdate?
// FIXME: maxSize and contentLength are u64 based on https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/sceNp.cpp ? But on Patapon 3 optionAddr will be deadbeef if maxSize is u64 ?
static int sceNpLookupTitleSmallStorage(s32 transId, u32 dataAddr, u32 maxSize, u32 contentLengthAddr, u32 optionAddr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %d, %08x[%d], %08x) at %08x", __FUNCTION__, transId, dataAddr, maxSize, contentLengthAddr, (Memory::IsValidAddress(contentLengthAddr)? Memory::Read_U32(contentLengthAddr): 0), optionAddr, currentMIPS->pc);
	return 0;
}

// On Resistance Retribution:
//		unknownAddr pointing to a struct of:
//			32-bit pointer (ie. 08efc6c4)? or a timestamp combined with the next 32-bit value?
//			32-bit pointer (ie. 08f9d101)? but unaligned (the lowest byte seems to be intentionally set to 1)? so probably not a pointer, may be a timestamp combined with previous 32-bit value?
//			32-bit pointer? Seems to be pointing to dummy ticket data generated by sceNpAuthGetTicket
//			32-bit value (248) dummy ticket length from NpAuth Ticket?
//			There could be more data in the struct? (at least 48-bytes?)
static int sceNpRosterCreateRequest(u32 unknownAddr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x) at %08x", __FUNCTION__, unknownAddr, currentMIPS->pc);
	return 1; // returning dummy roster id
}

// On Resistance Retribution: 
//		unknown1 set to 50 (max entries?), 
//		unknown2 set to 0, 
//		unknown3Addr pointing to unset buffer? (output entry data? usually located right after number of entries?), 
//		unknown4Addr pointing to 32-bit value set to 0 (output number of entries?), 
//		unknown5Addr pointing to zeroed buffer?,
//		unknown6 set to 0
static int sceNpRosterGetFriendListEntry(s32 rosterId, u32 unknown1, u32 unknown2, u32 unknown3Addr, u32 unknown4Addr, u32 unknown5Addr, u32 unknown6)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %08x, %08x, %08x, %08x, %08x) at %08x", __FUNCTION__, rosterId, unknown1, unknown2, unknown3Addr, unknown4Addr, unknown5Addr, unknown6, currentMIPS->pc);
	return 0;
}

static int sceNpRosterAbort(s32 rosterId)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d) at %08x", __FUNCTION__, rosterId, currentMIPS->pc);
	return 0;
}

static int sceNpRosterDeleteRequest(s32 rosterId)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d) at %08x", __FUNCTION__, rosterId, currentMIPS->pc);
	return 0;
}

const HLEFunction sceNpService[] = {
	{0X00ACFAC3, &WrapI_V<sceNpServiceTerm>,					"sceNpServiceTerm",						'i', ""       },
	{0X0F8F5821, &WrapI_UUU<sceNpServiceInit>,					"sceNpServiceInit",						'i', "xxx"    },
	{0X5494274B, &WrapI_U<sceNpLookupCreateTransactionCtx>,		"sceNpLookupCreateTransactionCtx",		'i', "x"      },
	{0XA670D3A3, &WrapI_I<sceNpLookupDestroyTransactionCtx>,	"sceNpLookupDestroyTransactionCtx",		'i', "i"      },
	{0XC76F55ED, &WrapI_IUUUU<sceNpLookupTitleSmallStorage>,	"sceNpLookupTitleSmallStorage",			'i', "ixxxx"  },
	{0XBE22EEA3, &WrapI_U<sceNpRosterCreateRequest>,			"sceNpRosterCreateRequest",				'i', "x"      },
	{0X4E851B10, &WrapI_IUUUUUU<sceNpRosterGetFriendListEntry>,	"sceNpRosterGetFriendListEntry",		'i', "ixxxxxx"},
	{0X5F5E32AF, &WrapI_I<sceNpRosterAbort>,					"sceNpRosterAbort",						'i', "i"      },
	{0X66C64821, &WrapI_I<sceNpRosterDeleteRequest>,			"sceNpRosterDeleteRequest",				'i', "i"      },
};

void Register_sceNpService()
{
	RegisterModule("sceNpService", ARRAY_SIZE(sceNpService), sceNpService);
}

// TODO: Moves NpCommerce2-related stuff to sceNpCommerce2.cpp
const HLEFunction sceNpCommerce2[] = {
	{0X005B5F20, nullptr,                            "sceNpCommerce2GetProductInfoStart",				'?', ""   },
	{0X0E9956E3, nullptr,                            "sceNpCommerce2Init",								'?', ""   },
	{0X1888A9FE, nullptr,                            "sceNpCommerce2DestroyReq",						'?', ""   },
	{0X1C952DCB, nullptr,                            "sceNpCommerce2GetGameProductInfo",				'?', ""   },
	{0X2B25F6E9, nullptr,                            "sceNpCommerce2CreateSessionStart",				'?', ""   },
	{0X3371D5F1, nullptr,                            "sceNpCommerce2GetProductInfoCreateReq",			'?', ""   },
	{0X4ECD4503, nullptr,                            "sceNpCommerce2CreateSessionCreateReq",			'?', ""   },
	{0X590A3229, nullptr,                            "sceNpCommerce2GetSessionInfo",					'?', ""   },
	{0X6F1FE37F, nullptr,                            "sceNpCommerce2CreateCtx",							'?', ""   },
	{0XA5A34EA4, nullptr,                            "sceNpCommerce2Term",								'?', ""   },
	{0XAA4A1E3D, nullptr,                            "sceNpCommerce2GetProductInfoGetResult",			'?', ""   },
	{0XBC61FFC8, nullptr,                            "sceNpCommerce2CreateSessionGetResult",			'?', ""   },
	{0XC7F32242, nullptr,                            "sceNpCommerce2AbortReq",							'?', ""   },
	{0XF2278B90, nullptr,                            "sceNpCommerce2GetGameSkuInfoFromGameProductInfo",	'?', ""   },
	{0XF297AB9C, nullptr,                            "sceNpCommerce2DestroyCtx",						'?', ""   },
	{0XFC30C19E, nullptr,                            "sceNpCommerce2InitGetProductInfoResult",			'?', ""   },
};

void Register_sceNpCommerce2()
{
	RegisterModule("sceNpCommerce2", ARRAY_SIZE(sceNpCommerce2), sceNpCommerce2);
}

static int sceNpMatching2Init(int poolSize, int threadPriority, int cpuAffinityMask, int threadStackSize)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %d, %d, %d) at %08x", __FUNCTION__, poolSize, threadPriority, cpuAffinityMask, threadStackSize, currentMIPS->pc);
	//if (npMatching2Inited)
	//	return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_ALREADY_INITIALIZED);

	npMatching2MemStat.npMemSize = poolSize - 0x20;
	npMatching2MemStat.npMaxMemSize = 0x4050; // Dummy maximum foot print
	npMatching2MemStat.npFreeMemSize = npMatching2MemStat.npMemSize;

	npMatching2Handlers.clear();
	npMatching2Events.clear();
	npMatching2Inited = true;
	return 0;
}

static int sceNpMatching2Term()
{
	ERROR_LOG(SCENET, "UNIMPL %s() at %08x", __FUNCTION__, currentMIPS->pc);
	npMatching2Inited = false;
	npMatching2Handlers.clear();
	npMatching2Events.clear();

	return 0;
}

static int sceNpMatching2CreateContext(u32 communicationIdPtr, u32 passPhrasePtr, u32 ctxIdPtr, int unknown)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x[%s], %08x[%08x], %08x[%d], %d) at %08x", __FUNCTION__, communicationIdPtr, Memory::GetCharPointer(communicationIdPtr), passPhrasePtr, Memory::Read_U32(passPhrasePtr), ctxIdPtr, Memory::Read_U16(ctxIdPtr), unknown, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(communicationIdPtr) || !Memory::IsValidAddress(passPhrasePtr) || !Memory::IsValidAddress(ctxIdPtr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX);

	// FIXME: It seems Context are mapped to TitleID? may return 0x80550C05 or 0x80550C06 when finding an existing context
	SceNpCommunicationId* titleid = (SceNpCommunicationId*)Memory::GetCharPointer(communicationIdPtr);
	memcpy(npTitleId.data, titleid->data, sizeof(npTitleId));

	SceNpCommunicationPassphrase* passph = (SceNpCommunicationPassphrase*)Memory::GetCharPointer(passPhrasePtr);

	SceNpId npid{};
	int retval = NpGetNpId(&npid);
	if (retval < 0)
		return hleLogError(SCENET, retval);

	INFO_LOG(SCENET, "%s - Title ID: %s", __FUNCTION__, titleid->data);
	INFO_LOG(SCENET, "%s - Online ID: %s", __FUNCTION__, npid.handle.data);
	std::string datahex;
	DataToHexString(npid.opt, sizeof(npid.opt), &datahex);
	INFO_LOG(SCENET, "%s - Options?: %s", __FUNCTION__, datahex.c_str());
	datahex.clear();
	DataToHexString(10, 0, passph->data, sizeof(passph->data), &datahex);
	INFO_LOG(SCENET, "%s - Passphrase: \n%s", __FUNCTION__, datahex.c_str());

	// TODO: Allocate & zeroed a memory of 68 bytes where npId (36 bytes) is copied to offset 8, offset 44 = 0x00026808, offset 48 = 0

	// Returning dummy Id, a 16-bit variable according to JPCSP
	// FIXME: It seems ctxId need to be in the range of 1 to 7 to be valid ?
	Memory::Write_U16(1, ctxIdPtr);
	return 0;
}

static int sceNpMatching2ContextStart(int ctxId)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d) at %08x", __FUNCTION__, ctxId, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	//if (!npMatching2Ctx)
	//	return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND); //SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID

	//if (npMatching2Ctx.started)
	//	return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_ALREADY_STARTED);

	// TODO: use sceNpGetUserProfile and check server availability using sceNpService_76867C01
	//npMatching2Ctx.started = true;
	Url url("http://static-resource.np.community.playstation.net/np/resource/psp-title/" + std::string(npTitleId.data) + "_00/matching/" + std::string(npTitleId.data) + "_00-matching.xml");
	http::Client client;
	http::RequestProgress progress;
	if (!client.Resolve(url.Host().c_str(), url.Port())) {
		return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "HTTP failed to resolve %s", url.Resource().c_str());
	}

	client.SetDataTimeout(20.0);
	if (client.Connect()) {
		char requestHeaders[4096];
		snprintf(requestHeaders, sizeof(requestHeaders),
			"User-Agent: PS3Community-agent/1.0.0 libhttp/1.0.0\r\n");

		DEBUG_LOG(SCENET, "GET URI: %s", url.ToString().c_str());
		http::RequestParams req(url.Resource(), "*/*");
		int err = client.SendRequest("GET", req, requestHeaders, &progress);
		if (err < 0) {
			client.Disconnect();
			return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "HTTP GET Error = %d", err);
		}

		net::Buffer readbuf;
		std::vector<std::string> responseHeaders;
		int code = client.ReadResponseHeaders(&readbuf, responseHeaders, &progress);
		if (code != 200) {
			client.Disconnect();
			return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "HTTP Error Code = %d", code);
		}

		net::Buffer output;
		int res = client.ReadResponseEntity(&readbuf, responseHeaders, &output, &progress);
		if (res != 0) {
			WARN_LOG(SCENET, "Unable to read HTTP response entity: %d", res);
		}
		client.Disconnect();

		std::string entity;
		size_t readBytes = output.size();
		output.Take(readBytes, &entity);

		// TODO: Use XML Parser to get the Tag and it's attributes instead of searching for keywords on the string
		std::string text;
		size_t ofs = entity.find("titleid=");
		if (ofs == std::string::npos) 
			return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "titleid not found");

		ofs += 9;
		size_t ofs2 = entity.find('"', ofs);
		text = entity.substr(ofs, ofs2-ofs);
		INFO_LOG(SCENET, "%s - Title ID: %s", __FUNCTION__, text.c_str());

		int i = 1;
		while (true) {
			ofs = entity.find("<agent-fqdn", ++ofs2);
			if (ofs == std::string::npos)
				if (i == 1)
					return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent-fqdn not found");
				else
					break;

			size_t frontPos = ++ofs;
			ofs = entity.find("id=", frontPos);
			if (ofs == std::string::npos)
				return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent id not found");

			ofs += 4;
			ofs2 = entity.find('"', ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(SCENET, "%s - Agent-FQDN#%d ID: %s", __FUNCTION__, i, text.c_str());

			ofs = entity.find("port=", frontPos);
			if (ofs == std::string::npos)
				return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent port not found");

			ofs += 6;
			ofs2 = entity.find('"', ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(SCENET, "%s - Agent-FQDN#%d Port: %s", __FUNCTION__, i, text.c_str());

			ofs = entity.find("status=", frontPos);
			if (ofs == std::string::npos)
				return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent status not found");

			ofs += 8;
			ofs2 = entity.find('"', ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(SCENET, "%s - Agent-FQDN#%d Status: %s", __FUNCTION__, i, text.c_str());

			ofs = entity.find(">", ++ofs2);
			if (ofs == std::string::npos)
				return hleLogError(SCENET, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent host not found");

			ofs2 = entity.find("</agent-fqdn", ++ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(SCENET, "%s - Agent-FQDN#%d Host: %s", __FUNCTION__, i, text.c_str());

			i++;
		}
	}
	hleEatMicro(1000000);
	// Returning 0x805508A6 (error code inherited from sceNpService_76867C01 which check server availability) if can't check server availability (ie. Fat Princess (US) through http://static-resource.np.community.playstation.net/np/resource/psp-title/NPWR00670_00/matching/NPWR00670_00-matching.xml using User-Agent: "PS3Community-agent/1.0.0 libhttp/1.0.0")
	return 0;
}

static int sceNpMatching2ContextStop(int ctxId)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d) at %08x", __FUNCTION__, ctxId, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	//if (!npMatching2Ctx)
	//	return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND); //SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID

	//if (!npMatching2Ctx.started)
	//	return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED);

	//TODO: Stop any in-progress HTTPClient communication used on sceNpMatching2ContextStart
	//npMatching2Ctx.started = false;

	return 0;
}

static int sceNpMatching2DestroyContext(int ctxId)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d) at %08x", __FUNCTION__, ctxId, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	//if (!npMatching2Ctx)
	//	return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND); //SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID

	// Remove callback handler
	int handlerID = ctxId - 1;
	if (npMatching2Handlers.find(handlerID) != npMatching2Handlers.end()) {
		npMatching2Handlers.erase(handlerID);
		WARN_LOG(SCENET, "%s: Deleted handler %d", __FUNCTION__, handlerID);
	}
	else {
		ERROR_LOG(SCENET, "%s: Invalid Context ID %d", __FUNCTION__, ctxId);
	}

	return 0;
}

static int sceNpMatching2GetMemoryStat(u32 memStatPtr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%08x) at %08x", __FUNCTION__, memStatPtr, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(memStatPtr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT);

	Memory::WriteStruct(memStatPtr, &npMatching2MemStat);

	return 0;
}

static int sceNpMatching2RegisterSignalingCallback(int ctxId, u32 callbackFunctionAddr, u32 callbackArgument)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %08x) at %08x", __FUNCTION__, ctxId, callbackFunctionAddr, callbackArgument, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (ctxId <= 0)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID);

	int id = ctxId - 1;
	if (callbackFunctionAddr != 0) {
		bool foundHandler = false;

		struct NpMatching2Handler handler;
		memset(&handler, 0, sizeof(handler));

		handler.entryPoint = callbackFunctionAddr;
		handler.argument = callbackArgument;

		for (std::map<int, NpMatching2Handler>::iterator it = npMatching2Handlers.begin(); it != npMatching2Handlers.end(); it++) {
			if (it->second.entryPoint == handler.entryPoint) {
				foundHandler = true;
				id = it->first;
				break;
			}
		}

		if (!foundHandler && Memory::IsValidAddress(handler.entryPoint)) {
			npMatching2Handlers[id] = handler;
			WARN_LOG(SCENET, "%s - Added handler(%08x, %08x) : %d", __FUNCTION__, handler.entryPoint, handler.argument, id);
		}
		else {
			ERROR_LOG(SCENET, "%s - Same handler(%08x, %08x) already exists", __FUNCTION__, handler.entryPoint, handler.argument);
		}

		//u32 dataLength = 4097; 
		//notifyNpMatching2Handlers(retval, dataLength, handler.argument);

		// callback struct have 57 * u32? where [0]=0, [40]=flags, [55]=callbackFunc, and [56]=callbackArgs?
		//hleEnqueueCall(callbackFunctionAddr, 7, (u32*)Memory::GetPointer(callbackArgument), nullptr); // 7 args? since the callback handler is trying to use t2 register
	}
	return 0;
}

static int sceNpMatching2GetServerIdListLocal(int ctxId, u32 serverIdsPtr, int maxServerIds)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %d) at %08x", __FUNCTION__, ctxId, serverIdsPtr, maxServerIds, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(serverIdsPtr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT);

	// Returning dummy Id, a 16-bit variable according to JPCSP
	for (int i = 0; i < maxServerIds; i++)
		Memory::Write_U16(1234+i, serverIdsPtr+(i*2));

	return maxServerIds; // dummy value
}

// Unknown1 = optParam, unknown2 = assignedReqId according to https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/sceNp2.cpp ?
static int sceNpMatching2GetServerInfo(int ctxId, u32 serverIdPtr, u32 unknown1Ptr, u32 unknown2Ptr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x[%d], %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, serverIdPtr, Memory::Read_U16(serverIdPtr), unknown1Ptr, unknown2Ptr, Memory::Read_U32(unknown2Ptr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(serverIdPtr) || !Memory::IsValidAddress(unknown2Ptr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

	// Server ID is a 16-bit variable according to JPCSP
	int serverId = Memory::Read_U16(serverIdPtr);

	if (serverId == 0)
		return hleLogError(SCENET, 0x80550CBF); // Should be SCE_NP_MATCHING2_ERROR_INVALID_SERVER_ID ?

	// Output to unknown1(infoBuffer)? and unknown2(infoLength or flags)?
	// Patapon 3 is using serverId at 09FFF2F4, unknown1 at 09FFF2E4, unknown2 at 09FFF2E0, which mean unknown1's can only fit upto 16-bytes
	// Patapon 3 seems to be copying data from unknown1 with a fixed size of 20-bytes?
	// input unknown1 struct: based on Fat Princess (US)
	// 	   0000 32-bit function address (callback?) 0x08A08B40
	// 	   0004 32-bit pointer to a struct? (callback args?) 0x09888158 (contains 32-bit (-1) + 32-bit (1) + 16-bit ctxId(0001) + 32bit 0x06913801? + 16-bit serverId(1234), so on), probably only 2x 32-bit struct?
	// 	   0008 32-bit set to 0
	// 	   000a 16-bit set to 0
	//
	u32 cbFunc = Memory::Read_U32(unknown1Ptr);
	u32 cbArg = Memory::Read_U32(unknown1Ptr + 0x04);

	// Notify callback handler
	if (Memory::IsValidAddress(cbFunc)) {
		// The cbFunc seems to be storing s0~s4(s0 pointing to 0x0996DD58 containing data similar to 0x09888158 above on the 1st 2x 32-bit data, s1 seems to be ctxId, s2~s4=0xdeadbeef) into stack and use a0~t1 (6 args?):
		//		Arg1(a0) & Arg3(a2) are being masked with 0xffff (16-bit id?)
		//		This callback tried to load data from address 0x08BD4860+8 (not part of arg? which being set using content of unknown2 not long after returning from sceNpMatching2GetServerInfo, so we may need to give some delay before calling this callback)
		//		and comparing it with Arg2(a1), repeated by increasing the address 0x08BD4860 by 288 bytes on each loop for 64 times or until it found a matching one.
		//		When a match is found the callback will process the address further, otherwise exit the callback.
		//		Matching address struct: (zeroed before calling sceNpMatching2GetServerInfo? and set after returning from sceNpMatching2GetServerInfo?)
		//			0000 32-bit func address (another callback?) 0x08A07EF4
		//			0008 32-bit value from unknown2 content, being set not long after returning from sceNpMatching2GetServerInfo
		//			000c 32-bit unknown 
		//			0010 8-bit status to indicate not updated from callback yet? initially 0, set to 1 not long after returning from sceNpMatching2GetServerInfo (along with unknown2 content)
		//
		// There args are supposed to be constructed in the stack and the data need to be available even after returning from this function, so these args + optional data probably copied to somewhere
		NpMatching2Args args = {};
		args.data[0] = PSP_NP_MATCHING2_EVENT_0001;
		args.data[1] = PSP_NP_MATCHING2_STATE_1001; // or size of data?
		args.data[2] = serverIdPtr; // serverId or was it pointing to optional data at last arg (ie. args[10] where serverId is stored)?
		args.data[3] = unknown1Ptr;
		//args.data[4] = a boolean(0/1) related to a u8 value from the struct at args[9] (value XOR 0x04 == 0)
		args.data[5] = unknown2Ptr;
		args.data[6] = 0;
		//args.data[8] = 0 or a pointer to a struct related to context?
		//args.data[9] = 0 or a pointer to a struct related to context and matched serverId?
		//args.data[10] = serverId;

		notifyNpMatching2Handlers(args, ctxId, serverId, 0, 0, 0, 0, 0, 1);

		Memory::Write_U32(args.data[1], unknown2Ptr); // server status or flags?
	}

	// After returning, Fat Princess will loop for 64 times (increasing the address by 288 bytes on each loop) or until found a zero status byte (0x08BD4860 + 0x10), looking for empty/available entry to set?
	return 0;
}

static int sceNpMatching2LeaveRoom(int ctxId, u32 reqParamPtr, u32 optParamPtr, u32 assignedReqIdPtr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

	u32 cbFunc = Memory::Read_U32(reqParamPtr);
	u32 cbArg = Memory::Read_U32(reqParamPtr + 0x04);

	// Notify callback handler
	if (Memory::IsValidAddress(cbFunc)) {
		// There args are supposed to be constructed in the stack and the data need to be available even after returning from this function, so these args + optional data probably copied to somewhere
		NpMatching2Args args = {};
		args.data[0] = PSP_NP_MATCHING2_EVENT_0103;
		args.data[1] = PSP_NP_MATCHING2_STATE_3202;
		//args.data[2] = pointer to arg[8], where the 1st 20 bytes copied from (reqParamPtr+0x08), the rest of the struct are zeroed
		args.data[3] = optParamPtr;
		args.data[4] = 0;
		args.data[5] = assignedReqIdPtr;
		args.data[6] = 0;
		//args.data[8] = an initially zeroed struct of 536 bytes where the 1st 20 bytes were taken from reqParam offset 0x08

		notifyNpMatching2Handlers(args, ctxId, 0, cbFunc, cbArg, 0, 0, 0, 0x0c);

		Memory::Write_U32(args.data[1], assignedReqIdPtr);
	}

	// After returning, Fat Princess will loop for 64 times (increasing the address by 288 bytes on each loop) or until found a zero status byte (0x08BD4860 + 0x10), looking for empty/available entry to set?
	return 0;
}

static int sceNpMatching2JoinRoom(int ctxId, u32 reqParamPtr, u32 optParamPtr, u32 unknown1, u32 unknown2, u32 assignedReqIdPtr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

	// Server ID is a 16-bit variable according to JPCSP
	int serverId = Memory::Read_U16(reqParamPtr + 0x06);

	if (serverId == 0)
		return hleLogError(SCENET, 0x80550CBF); // Should be SCE_NP_MATCHING2_ERROR_INVALID_SERVER_ID ?

	u32 cbFunc = Memory::Read_U32(reqParamPtr);
	u32 cbArg = Memory::Read_U32(reqParamPtr + 0x04);

	// Notify callback handler
	if (Memory::IsValidAddress(cbFunc)) {
		// There args are supposed to be constructed in the stack and the data need to be available even after returning from this function, so these args + optional data probably copied to somewhere
		NpMatching2Args args = {};
		args.data[0] = PSP_NP_MATCHING2_EVENT_0102;
		args.data[1] = PSP_NP_MATCHING2_STATE_1209;
		//args.data[2] = pointer to arg[8] (optional data?)
		args.data[3] = optParamPtr;
		args.data[4] = 0;
		args.data[5] = assignedReqIdPtr;
		args.data[6] = 0;
		// Followed by optional data?
		args.data[8] = reqParamPtr; // an initially zeroed struct of 1224 bytes, where the 1st 32bit is set to reqParamPtr
		args.data[9] = unknown1;
		args.data[10] = unknown2;

		notifyNpMatching2Handlers(args, ctxId, serverId, 0, 0, 0, 0, 1, 0x0a);

		Memory::Write_U32(args.data[1], assignedReqIdPtr);
	}

	// After returning, Fat Princess will loop for 64 times (increasing the address by 288 bytes on each loop) or until found a zero status byte (0x08BD4860 + 0x10), looking for empty/available entry to set?
	return 0;
}

static int sceNpMatching2SearchRoom(int ctxId, u32 reqParamPtr, u32 optParamPtr, u32 assignedReqIdPtr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

	u32 cbFunc = Memory::Read_U32(reqParamPtr);
	u32 cbArg = Memory::Read_U32(reqParamPtr + 0x04);

	// Notify callback handler
	if (Memory::IsValidAddress(cbFunc)) {
		// There args are supposed to be constructed in the stack and the data need to be available even after returning from this function, so these args + optional data probably copied to somewhere
		NpMatching2Args args = {};
		// TODO: Set the correct callback args

		Memory::Write_U32(args.data[1], assignedReqIdPtr); // server status or flags?
	}

	return 0;
}

static int sceNpMatching2SendRoomChatMessage(int ctxId, u32 reqParamPtr, u32 optParamPtr, u32 assignedReqIdPtr)
{
	ERROR_LOG(SCENET, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(SCENET, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

	u32 cbFunc = Memory::Read_U32(reqParamPtr);
	u32 cbArg = Memory::Read_U32(reqParamPtr + 0x04);

	// Notify callback handler
	if (Memory::IsValidAddress(cbFunc)) {
		// There args are supposed to be constructed in the stack and the data need to be available even after returning from this function, so these args + optional data probably copied to somewhere
		NpMatching2Args args = {};
		args.data[0] = PSP_NP_MATCHING2_EVENT_0107;
		args.data[1] = PSP_NP_MATCHING2_STATE_3208;
		//args.data[2] = pointer to arg[8]
		args.data[3] = optParamPtr;
		args.data[4] = 0;
		args.data[5] = assignedReqIdPtr;
		args.data[6] = 0;
		//args.data[8] = reqParamPtr;

		notifyNpMatching2Handlers(args, ctxId, 0, cbFunc, cbArg, 0, 0, 0, 0x10);

		Memory::Write_U32(args.data[1], assignedReqIdPtr); // server status or flags?
	}

	// After returning, Fat Princess will loop for 64 times (increasing the address by 288 bytes on each loop) or until found a zero status byte (0x08BD4860 + 0x10), looking for empty/available entry to set?
	return 0;
}

const HLEFunction sceNpMatching2[] = {
	{0x2E61F6E1, &WrapI_IIII<sceNpMatching2Init>,						"sceNpMatching2Init",							'i', "iiii"   },
	{0x8BF37D8C, &WrapI_V<sceNpMatching2Term>,							"sceNpMatching2Term",							'i', ""       },
	{0x5030CC53, &WrapI_UUUI<sceNpMatching2CreateContext>,				"sceNpMatching2CreateContext",					'i', "xxxi"   },
	{0x190FF903, &WrapI_I<sceNpMatching2ContextStart>,					"sceNpMatching2ContextStart",					'i', "i"      },
	{0x2B3892FC, &WrapI_I<sceNpMatching2ContextStop>,					"sceNpMatching2ContextStop",					'i', "i"      },
	{0x3DE70241, &WrapI_I<sceNpMatching2DestroyContext>,				"sceNpMatching2DestroyContext",					'i', "i"      },
	{0x22F38DAF, &WrapI_U<sceNpMatching2GetMemoryStat>,					"sceNpMatching2GetMemoryStat",					'i', "x"      },
	{0xA3C298D1, &WrapI_IUU<sceNpMatching2RegisterSignalingCallback>,	"sceNpMatching2RegisterSignalingCallback",		'i', "ixx"    },
	{0xF47342FC, &WrapI_IUI<sceNpMatching2GetServerIdListLocal>,		"sceNpMatching2GetServerIdListLocal",			'i', "ixi"    },
	{0x4EE3A8EC, &WrapI_IUUU<sceNpMatching2GetServerInfo>,				"sceNpMatching2GetServerInfo",					'i', "ixxx"   },
	{0xC870535A, &WrapI_IUUU<sceNpMatching2LeaveRoom>,					"sceNpMatching2LeaveRoom",						'i', "ixxx"   },
	{0xAAD0946A, &WrapI_IUUUUU<sceNpMatching2JoinRoom>,					"sceNpMatching2JoinRoom",						'i', "ixxxxx" },
	{0x81C13E6D, &WrapI_IUUU<sceNpMatching2SearchRoom>,					"sceNpMatching2SearchRoom",						'i', "ixxx"   },
	{0x55F7837F, &WrapI_IUUU<sceNpMatching2SendRoomChatMessage>,		"sceNpMatching2SendRoomChatMessage",			'i', "ixxx"   },
};

void Register_sceNpMatching2()
{
	RegisterModule("sceNpMatching2", ARRAY_SIZE(sceNpMatching2), sceNpMatching2);
}

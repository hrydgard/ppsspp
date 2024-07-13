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
#include <StringUtils.h>
#include "Core/MemMapHelpers.h"
#include "Core/CoreTiming.h"
#include "Core/Config.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceNp.h"


bool npAuthInited = false;
int npSigninState = NP_SIGNIN_STATUS_NONE;
SceNpAuthMemoryStat npAuthMemStat = {};
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

SceNpCommunicationId npTitleId;

std::recursive_mutex npAuthEvtMtx;
std::deque<NpAuthArgs> npAuthEvents;
std::map<int, NpAuthHandler> npAuthHandlers;


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
	npAuthEvents.push_back({ { id, result, argAddr } });
}

static int sceNpInit()
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	npOnlineId = g_Config.sNickName;

	return 0;
}

static int sceNpTerm()
{
	// No parameters
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpGetContentRatingFlag(u32 parentalControlAddr, u32 userAgeAddr)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x, %08x)", __FUNCTION__, parentalControlAddr, userAgeAddr);

	if (!Memory::IsValidAddress(parentalControlAddr) || !Memory::IsValidAddress(userAgeAddr))
		return hleLogError(Log::sceNet, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	INFO_LOG(Log::sceNet, "%s - Parental Control: %d", __FUNCTION__, npParentalControl);
	INFO_LOG(Log::sceNet, "%s - User Age: %d", __FUNCTION__, npUserAge);

	Memory::Write_U32(npParentalControl, parentalControlAddr);
	Memory::Write_U32(npUserAge, userAgeAddr);

	return 0;
}

static int sceNpGetChatRestrictionFlag(u32 flagAddr)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x)", __FUNCTION__, flagAddr);

	if (!Memory::IsValidAddress(flagAddr))
		return hleLogError(Log::sceNet, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	INFO_LOG(Log::sceNet, "%s - Chat Restriction: %d", __FUNCTION__, npChatRestriction);

	Memory::Write_U32(npChatRestriction, flagAddr);

	return 0;
}

static int sceNpGetOnlineId(u32 idPtr)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x)", __FUNCTION__, idPtr);

	auto id = PSPPointer<SceNpOnlineId>::Create(idPtr);
	if (!id.IsValid())
		return hleLogError(Log::sceNet, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	memset((SceNpOnlineId *)id, 0, sizeof(SceNpOnlineId));
	truncate_cpy(id->data, sizeof(id->data), npOnlineId.c_str());
	id.NotifyWrite("NpGetOnlineId");

	INFO_LOG(Log::sceNet, "%s - Online ID: %s", __FUNCTION__, id->data);

	return 0;
}

int NpGetNpId(SceNpId* npid)
{
	truncate_cpy(npid->handle.data, sizeof(npid->handle.data), npOnlineId.c_str());
	return 0;
}

static int sceNpGetNpId(u32 idPtr)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x)", __FUNCTION__, idPtr);

	auto id = PSPPointer<SceNpId>::Create(idPtr);
	if (!id.IsValid())
		return hleLogError(Log::sceNet, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	SceNpId dummyNpId{};
	memset((SceNpId *)id, 0, sizeof(SceNpId));
	int retval = NpGetNpId(id);
	if (retval < 0)
		return hleLogError(Log::sceNet, retval);

	INFO_LOG(Log::sceNet, "%s - Online ID: %s", __FUNCTION__, id->handle.data);
	std::string datahex;
	DataToHexString(id->opt, sizeof(id->opt), &datahex);
	INFO_LOG(Log::sceNet, "%s - Options?: %s", __FUNCTION__, datahex.c_str());
	id.NotifyWrite("NpGetNpId");

	return 0;
}

static int sceNpGetAccountRegion(u32 countryCodePtr, u32 regionCodePtr)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x, %08x)", __FUNCTION__, countryCodePtr, regionCodePtr);

	auto countryCode = PSPPointer<SceNpCountryCode>::Create(countryCodePtr);
	auto regionCode = PSPPointer<SceNpCountryCode>::Create(regionCodePtr);
	if (!countryCode.IsValid() || !regionCode.IsValid())
		return hleLogError(Log::sceNet, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	memset((SceNpCountryCode *)countryCode, 0, sizeof(SceNpCountryCode));
	memcpy(countryCode->data, npCountryCode, sizeof(countryCode->data));
	memset((SceNpCountryCode *)regionCode, 0, sizeof(SceNpCountryCode));
	memcpy(regionCode->data, npRegionCode, sizeof(regionCode->data));

	INFO_LOG(Log::sceNet, "%s - Country Code: %s", __FUNCTION__, countryCode->data);
	INFO_LOG(Log::sceNet, "%s - Region? Code: %s", __FUNCTION__, regionCode->data);

	countryCode.NotifyWrite("NpGetAccountRegion");
	regionCode.NotifyWrite("NpGetAccountRegion");

	return 0;
}

static int sceNpGetMyLanguages(u32 langListPtr)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x)", __FUNCTION__, langListPtr);

	auto langList = PSPPointer<SceNpMyLanguages>::Create(langListPtr);
	if (!langList.IsValid())
		return hleLogError(Log::sceNet, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	INFO_LOG(Log::sceNet, "%s - Language1 Code: %d", __FUNCTION__, npMyLangList.language1);
	INFO_LOG(Log::sceNet, "%s - Language2 Code: %d", __FUNCTION__, npMyLangList.language2);
	INFO_LOG(Log::sceNet, "%s - Language3 Code: %d", __FUNCTION__, npMyLangList.language3);

	*langList = npMyLangList;
	langList.NotifyWrite("NpGetMyLanguages");

	return 0;
}

static int sceNpGetUserProfile(u32 profilePtr)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x)", __FUNCTION__, profilePtr);

	auto profile = PSPPointer<SceNpUserInformation>::Create(profilePtr);
	if (!Memory::IsValidAddress(profilePtr))
		return hleLogError(Log::sceNet, SCE_NP_ERROR_INVALID_ARGUMENT, "invalid arg");

	memset((SceNpUserInformation *)profile, 0, sizeof(SceNpUserInformation));
	truncate_cpy(profile->userId.handle.data, sizeof(profile->userId.handle.data), npOnlineId.c_str());
	truncate_cpy(profile->icon.data, sizeof(profile->icon.data), npAvatarUrl.c_str());

	INFO_LOG(Log::sceNet, "%s - Online ID: %s", __FUNCTION__, profile->userId.handle.data);
	std::string datahex;
	DataToHexString(profile->userId.opt, sizeof(profile->userId.opt), &datahex);
	INFO_LOG(Log::sceNet, "%s - Options?: %s", __FUNCTION__, datahex.c_str());
	INFO_LOG(Log::sceNet, "%s - Avatar URL: %s", __FUNCTION__, profile->icon.data);

	profile.NotifyWrite("NpGetUserProfile");

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
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	npAuthInited = false;
	return 0;
}

static int sceNpAuthInit(u32 poolSize, u32 stackSize, u32 threadPrio) 
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %d, %d)", __FUNCTION__, poolSize, stackSize, threadPrio);
	npAuthMemStat.npMemSize = poolSize - 0x20;
	npAuthMemStat.npMaxMemSize = 0x4050; // Dummy maximum foot print
	npAuthMemStat.npFreeMemSize = npAuthMemStat.npMemSize;
	npAuthEvents.clear();

	npAuthInited = true;
	return 0;
}

int sceNpAuthGetMemoryStat(u32 memStatAddr)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x)", __FUNCTION__, memStatAddr);

	auto memStat = PSPPointer<SceNpAuthMemoryStat>::Create(memStatAddr);
	if (!memStat.IsValid())
		return hleLogError(Log::sceNet, SCE_NP_AUTH_ERROR_INVALID_ARGUMENT, "invalid arg");

	*memStat = npAuthMemStat;
	memStat.NotifyWrite("NpAuthGetMemoryStat");

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
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x) at %08x", __FUNCTION__, paramAddr, currentMIPS->pc);

	if (!Memory::IsValidAddress(paramAddr))
		return hleLogError(Log::sceNet, SCE_NP_AUTH_ERROR_INVALID_ARGUMENT, "invalid arg");

	SceNpAuthRequestParameter params = {};
	int size = Memory::Read_U32(paramAddr);
	Memory::Memcpy(&params, paramAddr, size);
	npServiceId = Memory::GetCharPointer(params.serviceIdAddr);

	INFO_LOG(Log::sceNet, "%s - Max Version: %u.%u", __FUNCTION__, params.version.major, params.version.minor);
	INFO_LOG(Log::sceNet, "%s - Service ID: %s", __FUNCTION__, Memory::GetCharPointer(params.serviceIdAddr));
	INFO_LOG(Log::sceNet, "%s - Entitlement ID: %s", __FUNCTION__, Memory::GetCharPointer(params.entitlementIdAddr));
	INFO_LOG(Log::sceNet, "%s - Consumed Count: %d", __FUNCTION__, params.consumedCount);
	INFO_LOG(Log::sceNet, "%s - Cookie (size = %d): %s", __FUNCTION__, params.cookieSize, Memory::GetCharPointer(params.cookieAddr));

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
			WARN_LOG(Log::sceNet, "%s - Added handler(%08x, %08x) : %d", __FUNCTION__, handler.entryPoint, handler.argument, retval);
		}
		else {
			ERROR_LOG(Log::sceNet, "%s - Same handler(%08x, %08x) already exists", __FUNCTION__, handler.entryPoint, handler.argument);
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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %d) at %08x", __FUNCTION__, requestId, bufferAddr, length, currentMIPS->pc);

	if (!Memory::IsValidAddress(bufferAddr))
		return hleLogError(Log::sceNet, SCE_NP_AUTH_ERROR_INVALID_ARGUMENT, "invalid arg");

	int result = length;
	Memory::Memset(bufferAddr, 0, length, "NpAuthGetTicket");
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
	Memory::Memcpy(bufferAddr, &ticket, sizeof(SceNpTicket), "NpAuthGetTicket");
	SceNpTicketSection footer = { SECTION_TYPE_FOOTER, 32 }; // footer section? ie. 32-bytes on version 2.1 containing 4-chars ASCII + 20-chars ASCII
	Memory::Memcpy(bufferAddr + sizeof(ticket) + ofs, &footer, sizeof(SceNpTicketSection), "NpAuthGetTicket");
	ofs += sizeof(footer);
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_STRING_ASCII, "\x34\xcd\x3c\xa9", 4);
	ofs += writeTicketParam(buf + ofs, PARAM_TYPE_STRING_ASCII, "\x3a\x4b\x42\x66\x92\xda\x6b\x7c\xb7\x4c\xe8\xd9\x4f\x2b\x77\x15\x91\xb8\xa4\xa9", 20); // 20 random letters, token key or SceNpSignature?
	// includes Language list?
	Memory::Memset(bufferAddr + sizeof(ticket) + ofs, 0, 36);

	result = ticket.header.size + sizeof(ticket.header); // dummy ticket is 248 bytes

	return result;
}

// Used within callback of sceNpAuthCreateStartRequest (arg1 = structPtr?, arg2 = callback's args[1], arg3 = DLCcode? ie. "EP9000-UCES01421_00-DLL001", arg4 = Patapon 3 always set to 0?)
// Patapon 3 will loop (for each DLC?) through an array of 4+4 bytes, ID addr (pchar) + result (int). Each loop calls this function using the same ticket addr but use different ID addr (arg3) and store the return value in result field (default/initial = -1)
int sceNpAuthGetEntitlementById(u32 ticketBufferAddr, u32 ticketLength, u32 entitlementIdAddr, u32 arg4)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %d, %08x, %d)", __FUNCTION__, ticketBufferAddr, ticketLength, entitlementIdAddr, arg4);
	INFO_LOG(Log::sceNet, "%s - Entitlement ID: %s", __FUNCTION__, Memory::GetCharPointer(entitlementIdAddr));

	// Do we return the entitlement through function result? or update the ticket content? or replace the arg3 data with SceNpEntitlement struct?
	return 1; // dummy value assuming it's a boolean/flag, since we don't know how to return the entitlement result yet
}

int sceNpAuthAbortRequest(int requestId)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i)", __FUNCTION__, requestId);
	// TODO: Disconnect HTTPS connection & cancel the callback event
	std::lock_guard<std::recursive_mutex> npAuthGuard(npAuthEvtMtx);
	for (auto it = npAuthEvents.begin(); it != npAuthEvents.end(); ) {
		(it->data[0] == requestId) ? it = npAuthEvents.erase(it) : ++it;
	}

	return 0;
}

int sceNpAuthDestroyRequest(int requestId)
{
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i)", __FUNCTION__, requestId);
	// Remove callback handler
	int handlerID = requestId - 1;
	if (npAuthHandlers.find(handlerID) != npAuthHandlers.end()) {
		npAuthHandlers.erase(handlerID);
		WARN_LOG(Log::sceNet, "%s: Deleted handler %d", __FUNCTION__, handlerID);
	}
	else {
		ERROR_LOG(Log::sceNet, "%s: Invalid request ID %d", __FUNCTION__, requestId);
	}

	// Patapon 3 is checking for error code 0x80550402
	return 0;
}

int sceNpAuthGetTicketParam(u32 ticketBufPtr, int ticketLen, int paramNum, u32 bufferPtr)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %0d, %d, %08x) at %08x", __FUNCTION__, ticketBufPtr, ticketLen, paramNum, bufferPtr, currentMIPS->pc);
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
		DEBUG_LOG(Log::sceNet, "%s - Param #%d: Type = %04x, Length = %u", __FUNCTION__, i, static_cast<unsigned int>(ticketParam->type), static_cast<unsigned int>(ticketParam->length));
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
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpServiceInit(u32 poolSize, u32 stackSize, u32 threadPrio) 
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %08x, %08x)", __FUNCTION__, poolSize, stackSize, threadPrio);
	return 0;
}

// FIXME: On Patapon 3 the Arg is pointing to a String, but based on RPCS3 the Arg is an Id integer ?
static int sceNpLookupCreateTransactionCtx(u32 lookupTitleCtxIdAddr)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x)", __FUNCTION__, lookupTitleCtxIdAddr);
	INFO_LOG(Log::sceNet, "%s - Title ID: %s", __FUNCTION__, Memory::GetCharPointer(lookupTitleCtxIdAddr));
	// Patapon 3 will only Destroy if returned Id > 0. Is 0 a valid id?
	return 1; // returning dummy transaction id
}

// transId: id returned from sceNpLookupCreateTransactionCtx
static int sceNpLookupDestroyTransactionCtx(s32 transId)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d)", __FUNCTION__, transId);
	return 0;
}

// transId: id returned from sceNpLookupCreateTransactionCtx
// Patapon 3 always set Arg5 to 0
// Addr args have something to do with GameUpdate?
// FIXME: maxSize and contentLength are u64 based on https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/sceNp.cpp ? But on Patapon 3 optionAddr will be deadbeef if maxSize is u64 ?
static int sceNpLookupTitleSmallStorage(s32 transId, u32 dataAddr, u32 maxSize, u32 contentLengthAddr, u32 optionAddr)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %d, %08x[%d], %08x) at %08x", __FUNCTION__, transId, dataAddr, maxSize, contentLengthAddr, (Memory::IsValidAddress(contentLengthAddr)? Memory::Read_U32(contentLengthAddr): 0), optionAddr, currentMIPS->pc);
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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x) at %08x", __FUNCTION__, unknownAddr, currentMIPS->pc);
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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %08x, %08x, %08x, %08x, %08x) at %08x", __FUNCTION__, rosterId, unknown1, unknown2, unknown3Addr, unknown4Addr, unknown5Addr, unknown6, currentMIPS->pc);
	return 0;
}

static int sceNpRosterAbort(s32 rosterId)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d) at %08x", __FUNCTION__, rosterId, currentMIPS->pc);
	return 0;
}

static int sceNpRosterDeleteRequest(s32 rosterId)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d) at %08x", __FUNCTION__, rosterId, currentMIPS->pc);
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


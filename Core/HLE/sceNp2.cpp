// Copyright (c) 2022- PPSSPP Project.

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
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceNp.h"
#include "Core/HLE/sceNp2.h"


bool npMatching2Inited = false;
SceNpAuthMemoryStat npMatching2MemStat = {};

std::recursive_mutex npMatching2EvtMtx;
std::deque<NpMatching2Args> npMatching2Events;
std::map<int, NpMatching2Handler> npMatching2Handlers;
//std::map<int, NpMatching2Context> npMatching2Contexts;


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

static int sceNpMatching2Init(int poolSize, int threadPriority, int cpuAffinityMask, int threadStackSize)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %d, %d, %d) at %08x", __FUNCTION__, poolSize, threadPriority, cpuAffinityMask, threadStackSize, currentMIPS->pc);
	//if (npMatching2Inited)
	//	return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_ALREADY_INITIALIZED);

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
	ERROR_LOG(Log::sceNet, "UNIMPL %s() at %08x", __FUNCTION__, currentMIPS->pc);
	npMatching2Inited = false;
	npMatching2Handlers.clear();
	npMatching2Events.clear();

	return 0;
}

static int sceNpMatching2CreateContext(u32 communicationIdPtr, u32 passPhrasePtr, u32 ctxIdPtr, int unknown)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x[%s], %08x[%08x], %08x[%hu], %i) at %08x", __FUNCTION__, communicationIdPtr, safe_string(Memory::GetCharPointer(communicationIdPtr)), passPhrasePtr, Memory::Read_U32(passPhrasePtr), ctxIdPtr, Memory::Read_U16(ctxIdPtr), unknown, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(communicationIdPtr) || !Memory::IsValidAddress(passPhrasePtr) || !Memory::IsValidAddress(ctxIdPtr))
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX);

	// FIXME: It seems Context are mapped to TitleID? may return 0x80550C05 or 0x80550C06 when finding an existing context
	SceNpCommunicationId* titleid = (SceNpCommunicationId*)Memory::GetCharPointer(communicationIdPtr);
	memcpy(npTitleId.data, titleid->data, sizeof(npTitleId));

	SceNpCommunicationPassphrase* passph = (SceNpCommunicationPassphrase*)Memory::GetCharPointer(passPhrasePtr);

	SceNpId npid{};
	int retval = NpGetNpId(&npid);
	if (retval < 0)
		return hleLogError(Log::sceNet, retval);

	INFO_LOG(Log::sceNet, "%s - Title ID: %s", __FUNCTION__, titleid->data);
	INFO_LOG(Log::sceNet, "%s - Online ID: %s", __FUNCTION__, npid.handle.data);
	std::string datahex;
	DataToHexString(npid.opt, sizeof(npid.opt), &datahex);
	INFO_LOG(Log::sceNet, "%s - Options?: %s", __FUNCTION__, datahex.c_str());
	datahex.clear();
	DataToHexString(10, 0, passph->data, sizeof(passph->data), &datahex);
	INFO_LOG(Log::sceNet, "%s - Passphrase: \n%s", __FUNCTION__, datahex.c_str());

	// TODO: Allocate & zeroed a memory of 68 bytes where npId (36 bytes) is copied to offset 8, offset 44 = 0x00026808, offset 48 = 0

	// Returning dummy Id, a 16-bit variable according to JPCSP
	// FIXME: It seems ctxId need to be in the range of 1 to 7 to be valid ?
	Memory::Write_U16(1, ctxIdPtr);
	return 0;
}

static int sceNpMatching2ContextStart(int ctxId)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d) at %08x", __FUNCTION__, ctxId, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	//if (!npMatching2Ctx)
	//	return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND); //SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID

	//if (npMatching2Ctx.started)
	//	return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_ALREADY_STARTED);

	// TODO: use sceNpGetUserProfile and check server availability using sceNpService_76867C01
	//npMatching2Ctx.started = true;
	Url url("http://static-resource.np.community.playstation.net/np/resource/psp-title/" + std::string(npTitleId.data) + "_00/matching/" + std::string(npTitleId.data) + "_00-matching.xml");
	http::Client client;
	bool cancelled = false;
	net::RequestProgress progress(&cancelled);
	if (!client.Resolve(url.Host().c_str(), url.Port())) {
		return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "HTTP failed to resolve %s", url.Resource().c_str());
	}

	client.SetDataTimeout(20.0);
	if (client.Connect()) {
		char requestHeaders[4096];
		snprintf(requestHeaders, sizeof(requestHeaders),
			"User-Agent: PS3Community-agent/1.0.0 libhttp/1.0.0\r\n");

		DEBUG_LOG(Log::sceNet, "GET URI: %s", url.ToString().c_str());
		http::RequestParams req(url.Resource(), "*/*");
		int err = client.SendRequest("GET", req, requestHeaders, &progress);
		if (err < 0) {
			client.Disconnect();
			return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "HTTP GET Error = %d", err);
		}

		net::Buffer readbuf;
		std::vector<std::string> responseHeaders;
		int code = client.ReadResponseHeaders(&readbuf, responseHeaders, &progress);
		if (code != 200) {
			client.Disconnect();
			return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "HTTP Error Code = %d", code);
		}

		net::Buffer output;
		int res = client.ReadResponseEntity(&readbuf, responseHeaders, &output, &progress);
		if (res != 0) {
			WARN_LOG(Log::sceNet, "Unable to read HTTP response entity: %d", res);
		}
		client.Disconnect();

		std::string entity;
		size_t readBytes = output.size();
		output.Take(readBytes, &entity);

		// TODO: Use XML Parser to get the Tag and it's attributes instead of searching for keywords on the string
		std::string text;
		size_t ofs = entity.find("titleid=");
		if (ofs == std::string::npos) 
			return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "titleid not found");

		ofs += 9;
		size_t ofs2 = entity.find('"', ofs);
		text = entity.substr(ofs, ofs2-ofs);
		INFO_LOG(Log::sceNet, "%s - Title ID: %s", __FUNCTION__, text.c_str());

		int i = 1;
		while (true) {
			ofs = entity.find("<agent-fqdn", ++ofs2);
			if (ofs == std::string::npos) {
				if (i == 1)
					return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent-fqdn not found");
				else
					break;
			}

			size_t frontPos = ++ofs;
			ofs = entity.find("id=", frontPos);
			if (ofs == std::string::npos)
				return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent id not found");

			ofs += 4;
			ofs2 = entity.find('"', ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(Log::sceNet, "%s - Agent-FQDN#%d ID: %s", __FUNCTION__, i, text.c_str());

			ofs = entity.find("port=", frontPos);
			if (ofs == std::string::npos)
				return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent port not found");

			ofs += 6;
			ofs2 = entity.find('"', ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(Log::sceNet, "%s - Agent-FQDN#%d Port: %s", __FUNCTION__, i, text.c_str());

			ofs = entity.find("status=", frontPos);
			if (ofs == std::string::npos)
				return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent status not found");

			ofs += 8;
			ofs2 = entity.find('"', ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(Log::sceNet, "%s - Agent-FQDN#%d Status: %s", __FUNCTION__, i, text.c_str());

			ofs = entity.find('>', ++ofs2);
			if (ofs == std::string::npos)
				return hleLogError(Log::sceNet, SCE_NP_COMMUNITY_SERVER_ERROR_NO_SUCH_TITLE, "agent host not found");

			ofs2 = entity.find("</agent-fqdn", ++ofs);
			text = entity.substr(ofs, ofs2 - ofs);
			INFO_LOG(Log::sceNet, "%s - Agent-FQDN#%d Host: %s", __FUNCTION__, i, text.c_str());

			i++;
		}
	}
	hleEatMicro(1000000);
	// Returning 0x805508A6 (error code inherited from sceNpService_76867C01 which check server availability) if can't check server availability (ie. Fat Princess (US) through http://static-resource.np.community.playstation.net/np/resource/psp-title/NPWR00670_00/matching/NPWR00670_00-matching.xml using User-Agent: "PS3Community-agent/1.0.0 libhttp/1.0.0")
	return 0;
}

static int sceNpMatching2ContextStop(int ctxId)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d) at %08x", __FUNCTION__, ctxId, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	//if (!npMatching2Ctx)
	//	return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND); //SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID

	//if (!npMatching2Ctx.started)
	//	return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED);

	//TODO: Stop any in-progress HTTPClient communication used on sceNpMatching2ContextStart
	//npMatching2Ctx.started = false;

	return 0;
}

static int sceNpMatching2DestroyContext(int ctxId)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d) at %08x", __FUNCTION__, ctxId, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	//if (!npMatching2Ctx)
	//	return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND); //SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID

	// Remove callback handler
	int handlerID = ctxId - 1;
	if (npMatching2Handlers.find(handlerID) != npMatching2Handlers.end()) {
		npMatching2Handlers.erase(handlerID);
		WARN_LOG(Log::sceNet, "%s: Deleted handler %d", __FUNCTION__, handlerID);
	}
	else {
		ERROR_LOG(Log::sceNet, "%s: Invalid Context ID %d", __FUNCTION__, ctxId);
	}

	return 0;
}

static int sceNpMatching2GetMemoryStat(u32 memStatPtr)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x) at %08x", __FUNCTION__, memStatPtr, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	auto memStat = PSPPointer<SceNpAuthMemoryStat>::Create(memStatPtr);
	if (!memStat.IsValid())
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT);

	*memStat = npMatching2MemStat;
	memStat.NotifyWrite("NpMatching2GetMemoryStat");

	return 0;
}

static int sceNpMatching2RegisterSignalingCallback(int ctxId, u32 callbackFunctionAddr, u32 callbackArgument)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %08x) at %08x", __FUNCTION__, ctxId, callbackFunctionAddr, callbackArgument, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (ctxId <= 0)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_INVALID_CONTEXT_ID);

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
			WARN_LOG(Log::sceNet, "%s - Added handler(%08x, %08x) : %d", __FUNCTION__, handler.entryPoint, handler.argument, id);
		}
		else {
			ERROR_LOG(Log::sceNet, "%s - Same handler(%08x, %08x) already exists", __FUNCTION__, handler.entryPoint, handler.argument);
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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %d) at %08x", __FUNCTION__, ctxId, serverIdsPtr, maxServerIds, currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(serverIdsPtr))
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT);

	// Returning dummy Id, a 16-bit variable according to JPCSP
	for (int i = 0; i < maxServerIds; i++)
		Memory::Write_U16(1234+i, serverIdsPtr+(i*2));

	return maxServerIds; // dummy value
}

// Unknown1 = optParam, unknown2 = assignedReqId according to https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/sceNp2.cpp ?
static int sceNpMatching2GetServerInfo(int ctxId, u32 serverIdPtr, u32 unknown1Ptr, u32 unknown2Ptr)
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x[%d], %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, serverIdPtr, Memory::Read_U16(serverIdPtr), unknown1Ptr, unknown2Ptr, Memory::Read_U32(unknown2Ptr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(serverIdPtr) || !Memory::IsValidAddress(unknown2Ptr))
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

	// Server ID is a 16-bit variable according to JPCSP
	int serverId = Memory::Read_U16(serverIdPtr);

	if (serverId == 0)
		return hleLogError(Log::sceNet, 0x80550CBF); // Should be SCE_NP_MATCHING2_ERROR_INVALID_SERVER_ID ?

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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

	// Server ID is a 16-bit variable according to JPCSP
	int serverId = Memory::Read_U16(reqParamPtr + 0x06);

	if (serverId == 0)
		return hleLogError(Log::sceNet, 0x80550CBF); // Should be SCE_NP_MATCHING2_ERROR_INVALID_SERVER_ID ?

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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

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
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%d, %08x, %08x, %08x[%08x]) at %08x", __FUNCTION__, ctxId, reqParamPtr, optParamPtr, assignedReqIdPtr, Memory::Read_U32(assignedReqIdPtr), currentMIPS->pc);
	if (!npMatching2Inited)
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);

	if (!Memory::IsValidAddress(reqParamPtr) || !Memory::IsValidAddress(assignedReqIdPtr))
		return hleLogError(Log::sceNet, SCE_NP_MATCHING2_ERROR_CONTEXT_MAX); // Should be SCE_NP_MATCHING2_ERROR_INVALID_ARGUMENT ?

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

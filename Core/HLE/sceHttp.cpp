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

#include "HLE.h"

#include "sceHttp.h"

// Could come in handy someday if we ever implement sceHttp* for real.
enum PSPHttpMethod {
	PSP_HTTP_METHOD_GET,
	PSP_HTTP_METHOD_POST,
	PSP_HTTP_METHOD_HEAD
};

int sceHttpSetResolveRetry(int connectionID, int retryCount) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetResolveRetry(%d, %d)", connectionID, retryCount);
	return 0;
}

int sceHttpInit() {
	ERROR_LOG(HLE, "UNIMPL sceHttpInit()");
	return 0;
}

int sceHttpEnd() {
	ERROR_LOG(HLE, "UNIMPL sceHttpInit()");
	return 0;
}

int sceHttpInitCache(int size) {
	ERROR_LOG(HLE, "UNIMPL sceHttpInitCache(%d)", size);
	return 0;
}

int sceHttpEndCache() {
	ERROR_LOG(HLE, "UNIMPL sceHttpEndCache()");
	return 0;
}

int sceHttpEnableCache(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpEnableCache(%d)", id);
	return 0;
}

int sceHttpDisableCache(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDisableCache(%d)", id);
	return 0;
}

u32 sceHttpGetProxy(u32 id, u32 activateFlagPtr, u32 modePtr, u32 proxyHostPtr, u32 len, u32 proxyPort) {
	ERROR_LOG(HLE, "UNIMPL sceHttpGetProxy(%d, %x, %x, %x, %d, %x)", id, activateFlagPtr, modePtr, proxyHostPtr, len, proxyPort);
	return 0;
}

int sceHttpGetStatusCode(int requestID, u32 statusCodePtr) {
	ERROR_LOG(HLE, "UNIMPL sceHttpGetStatusCode(%d, %x)", requestID, statusCodePtr);
	return 0;
}

int sceHttpReadData(int requestID, u32 dataPtr, u32 dataSize) {
	ERROR_LOG(HLE, "UNIMPL sceHttpReadData(%d, %x, %x)", requestID, dataPtr, dataSize);
	return 0;
}

int sceHttpSendRequest(int requestID, u32 dataPtr, u32 dataSize) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSendRequest(%d, %x, %x)", requestID, dataPtr, dataSize);
	return 0;
}

int sceHttpDeleteRequest(int requestID) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDeleteRequest(%d)", requestID);
	return 0;
}

int sceHttpDeleteHeader(int id, const char *name) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDeleteHeader(%d, %s)", id, name);
	return 0;
}

int sceHttpDeleteConnection(int connectionID) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDisableCache(%d)", connectionID);
	return 0;
}

int sceHttpSetConnectTimeOut(int id, u32 timeout) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetConnectTimeout(%d, %d)", id, timeout);
	return 0;
}

int sceHttpSetSendTimeOut(int id, u32 timeout) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetSendTimeout(%d, %d)", id, timeout);
	return 0;
}

u32 sceHttpSetProxy(u32 id, u32 activateFlagPtr, u32 mode, u32 newProxyHostPtr, u32 newProxyPort) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetProxy(%d, %x, %x, %x, %d)", id, activateFlagPtr, mode, newProxyHostPtr, newProxyPort);
	return 0;
}

int sceHttpEnableCookie(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpEnableCookie(%d)", id);
	return 0;
}

int sceHttpEnableKeepAlive(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpEnableKeepAlive(%d)", id);
	return 0;
}

int sceHttpDisableCookie(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDisableCookie(%d)", id);
	return 0;
}

int sceHttpDisableKeepAlive(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDisableKeepAlive(%d)", id);
	return 0;
}

int sceHttpsInit(int unknown1, int unknown2, int unknown3, int unknown4) {
	ERROR_LOG(HLE, "UNIMPL sceHttpsInit(%d, %d, %d, %d)", unknown1, unknown2, unknown3, unknown4);
	return 0;
}

int sceHttpsEnd() {
	ERROR_LOG(HLE, "UNIMPL sceHttpsEnd()");
	return 0;
}

// Parameter "method" should be one of PSPHttpMethod's listed entries
int sceHttpCreateRequest(int connectionID, int method, const char *path, u64 contentLength) {
	ERROR_LOG(HLE, "UNIMPL sceHttpCreateRequest(%d, %d, %s, %llx)", connectionID, method, path, contentLength);
	return 0;
}

int sceHttpCreateConnection(int templateID, const char *host, const char *unknown1, u32 port, int unknown2) {
	ERROR_LOG(HLE, "UNIMPL sceHttpCreateConnection(%d, %s, %s, %d, %d)", templateID, host, unknown1, port, unknown2);
	return 0;
}

int sceHttpGetNetworkErrno(int request, u32 errNumPtr) {
	ERROR_LOG(HLE, "UNIMPL sceHttpGetNetworkErrno(%d, %x)", request, errNumPtr);
	return 0;
}

int sceHttpAddExtraHeader(int id, const char *name, const char *value, int unknown) {
	ERROR_LOG(HLE, "UNIMPL sceHttpAddExtraHeader(%d, %s, %s, %d)", id, name, value, unknown);
	return 0;
}

int sceHttpAbortRequest(int requestID) {
	ERROR_LOG(HLE, "UNIMPL sceHttpAbortRequest(%d)", requestID);
	return 0;
}

int sceHttpDeleteTemplate(int templateID) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDeleteTemplate(%d)", templateID);
	return 0;
}

int sceHttpSetMallocFunction(u32 mallocFuncPtr, u32 freeFuncPtr, u32 reallocFuncPtr) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetMallocFunction(%x, %x, %x)", mallocFuncPtr, freeFuncPtr, reallocFuncPtr);
	return 0;
}

int sceHttpSetResolveTimeOut(int id, u32 timeout) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetResolveTimeOut(%d, %d)", id, timeout);
	return 0;
}

int sceHttpSetAuthInfoCB(int id, u32 callbackFuncPtr) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetAuthInfoCB(%d, %x)", id, callbackFuncPtr);
	return 0;
}

int sceHttpEnableRedirect(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpEnableRedirect(%d)", id);
	return 0;
}

int sceHttpEnableAuth(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpEnableAuth(%d)", id);
	return 0;
}

int sceHttpDisableRedirect(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDisableRedirect(%d)", id);
	return 0;
}

int sceHttpDisableAuth(int id) {
	ERROR_LOG(HLE, "UNIMPL sceHttpDisableAuth(%d)", id);
	return 0;
}

int sceHttpSaveSystemCookie() {
	ERROR_LOG(HLE, "UNIMPL sceHttpSaveSystemCookie()");
	return 0;
}

int sceHttpsLoadDefaultCert(int unknown1, int unknown2) {
	ERROR_LOG(HLE, "UNIMPL sceHttpLoadDefaultCert(%d, %d)", unknown1, unknown2);
	return 0;
}

int sceHttpLoadSystemCookie() {
	ERROR_LOG(HLE, "UNIMPL sceHttpLoadSystemCookie()");
	return 0;
}

int sceHttpCreateTemplate(const char *agent, int unknown1, int unknown2) {
	ERROR_LOG(HLE, "UNIMPL sceHttpCreateTemplate(%s, %d, %d)", agent, unknown1, unknown2);
	return 0;
}

// Parameter "method" should be one of PSPHttpMethod's listed entries
int sceHttpCreateRequestWithURL(int connectionID, int method, const char *url, u64 contentLength) {
	ERROR_LOG(HLE, "UNIMPL sceHttpCreateRequestWithURL(%d, %d, %s, %llx)", connectionID, method, url, contentLength);
	return 0;
}

int sceHttpCreateConnectionWithURL(int templateID, const char *url, int unknown1) {
	ERROR_LOG(HLE, "UNIMPL sceHttpCreateConnectionWithURL(%d, %s, %d)", templateID, url, unknown1);
	return 0;
}

int sceHttpSetRecvTimeOut(int id, u32 timeout) {
	ERROR_LOG(HLE, "UNIMPL sceHttpSetRecvTimeOut(%d, %x)", id, timeout);
	return 0;
}

int sceHttpGetAllHeader(int request, u32 headerPtrToPtr, u32 headerSize) {
	ERROR_LOG(HLE, "UNIMPL sceHttpGetAllHeader(%d, %x, %x)", request, headerPtrToPtr, headerSize);
	return 0;
}

int sceHttpGetContentLength(int requestID, u64 contentLengthPtr) {
	ERROR_LOG(HLE, "UNIMPL sceHttpGetContentLength(%d, %llx)", requestID, contentLengthPtr);
	return 0;
}

/*
*	0x62411801 sceSircsInit
0x19155a2f sceSircsEnd
0x71eef62d sceSircsSend
	*/
const HLEFunction sceHttp[] = {
	{0xab1abe07,WrapI_V<sceHttpInit>,"sceHttpInit"},
	{0xd1c8945e,WrapI_V<sceHttpEnd>,"sceHttpEnd"},
	{0xa6800c34,WrapI_I<sceHttpInitCache>,"sceHttpInitCache"},
	{0x78b54c09,WrapI_V<sceHttpEndCache>,"sceHttpEndCache"},
	{0x59e6d16f,WrapI_I<sceHttpEnableCache>,"sceHttpEnableCache"},
	{0xccbd167a,WrapI_I<sceHttpDisableCache>,"sceHttpDisableCache"},
	{0xd70d4847,WrapU_UUUUUU<sceHttpGetProxy>,"sceHttpGetProxy"},
	{0x4cc7d78f,WrapI_IU<sceHttpGetStatusCode>,"sceHttpGetStatusCode"},
	{0xedeeb999,WrapI_IUU<sceHttpReadData>,"sceHttpReadData"},
	{0xbb70706f,WrapI_IUU<sceHttpSendRequest>,"sceHttpSendRequest"},
	{0xa5512e01,WrapI_I<sceHttpDeleteRequest>,"sceHttpDeleteRequest"},
	{0x15540184,WrapI_IC<sceHttpDeleteHeader>,"sceHttpDeleteHeader"},
	{0x5152773b,WrapI_I<sceHttpDeleteConnection>,"sceHttpDeleteConnection"},
	{0x8acd1f73,WrapI_IU<sceHttpSetConnectTimeOut>,"sceHttpSetConnectTimeOut"},
	{0x9988172d,WrapI_IU<sceHttpSetSendTimeOut>,"sceHttpSetSendTimeOut"},
	{0xf0f46c62,WrapU_UUUUU<sceHttpSetProxy>,"sceHttpSetProxy"},
	{0x0dafa58f,WrapI_I<sceHttpEnableCookie>,"sceHttpEnableCookie"},
	{0x78a0d3ec,WrapI_I<sceHttpEnableKeepAlive>,"sceHttpEnableKeepAlive"},
	{0x0b12abfb,WrapI_I<sceHttpDisableCookie>,"sceHttpDisableCookie"},
	{0xc7ef2559,WrapI_I<sceHttpDisableKeepAlive>,"sceHttpDisableKeepAlive"},
	{0xe4d21302,WrapI_IIII<sceHttpsInit>,"sceHttpsInit"},
	{0xf9d8eb63,WrapI_V<sceHttpsEnd>,"sceHttpsEnd"},
	{0x47347b50,WrapI_IICU64<sceHttpCreateRequest>,"sceHttpCreateRequest"},
	{0x8eefd953,WrapI_ICCUI<sceHttpCreateConnection>,"sceHttpCreateConnection"},
	{0xd081ec8f,WrapI_IU<sceHttpGetNetworkErrno>,"sceHttpGetNetworkErrno"},
	{0x3eaba285,WrapI_ICCI<sceHttpAddExtraHeader>,"sceHttpAddExtraHeader"},
	{0xc10b6bd9,WrapI_I<sceHttpAbortRequest>,"sceHttpAbortRequest"},
	{0xfcf8c055,WrapI_I<sceHttpDeleteTemplate>,"sceHttpDeleteTemplate"},
	{0xf49934f6,WrapI_UUU<sceHttpSetMallocFunction>,"sceHttpSetMallocFunction"},
	{0x03D9526F,&WrapI_II<sceHttpSetResolveRetry>, "sceHttpSetResolveRetry"},
	{0x47940436,WrapI_IU<sceHttpSetResolveTimeOut>,"sceHttpSetResolveTimeOut"},
	{0x2a6c3296,WrapI_IU<sceHttpSetAuthInfoCB>,"sceHttpSetAuthInfoCB"},
	{0x0809c831,WrapI_I<sceHttpEnableRedirect>,"sceHttpEnableRedirect"},
	{0x9fc5f10d,WrapI_I<sceHttpEnableAuth>,"sceHttpEnableAuth"},
	{0x1a0ebb69,WrapI_I<sceHttpDisableRedirect>,"sceHttpDisableRedirect"},
	{0xae948fee,WrapI_I<sceHttpDisableAuth>,"sceHttpDisableAuth"},
	{0x76d1363b,WrapI_V<sceHttpSaveSystemCookie>,"sceHttpSaveSystemCookie"},
	{0x87797bdd,WrapI_II<sceHttpsLoadDefaultCert>,"sceHttpsLoadDefaultCert"},
	{0xf1657b22,WrapI_V<sceHttpLoadSystemCookie>,"sceHttpLoadSystemCookie"},
	{0x9B1F1F36,WrapI_CII<sceHttpCreateTemplate>,"sceHttpCreateTemplate"},
	{0xB509B09E,WrapI_IICU64<sceHttpCreateRequestWithURL>,"sceHttpCreateRequestWithURL"},
	{0xCDF8ECB9,WrapI_ICI<sceHttpCreateConnectionWithURL>,"sceHttpCreateConnectionWithURL"},
	{0x1F0FC3E3,WrapI_IU<sceHttpSetRecvTimeOut>,"sceHttpSetRecvTimeOut"},
	{0xDB266CCF,WrapI_IUU<sceHttpGetAllHeader>,"sceHttpGetAllHeader"},
	{0x0282A3BD,WrapI_IU64<sceHttpGetContentLength>,"sceHttpGetContentLength"},
	{0x68AB0F86,0,"sceHttpsInitWithPath"},
	{0xB3FAF831,0,"sceHttpsDisableOption"},
	{0x2255551E,0,"sceHttpGetNetworkPspError"},
	{0xAB1540D5,0,"sceHttpsGetSslError"},
	{0xA4496DE5,0,"sceHttpSetRedirectCallback"},
	{0x267618f4,0,"sceHttpSetAuthInfoCallback"},
	{0x569a1481,0,"sceHttpsSetSslCallback"},
	{0xbac31bf1,0,"sceHttpsEnableOption"},
};				

void Register_sceHttp()
{
	RegisterModule("sceHttp",ARRAY_SIZE(sceHttp),sceHttp);
}

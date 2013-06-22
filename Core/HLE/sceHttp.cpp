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

int sceHttpSetResolveRetry(int connectionID, int retryCount)
{
	ERROR_LOG(HLE, "UNIMPL sceHttpSetResolveRetry()");
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

/*
*	0x62411801 sceSircsInit
0x19155a2f sceSircsEnd
0x71eef62d sceSircsSend
	*/
const HLEFunction sceHttp[] = {
	{0xab1abe07,WrapI_V<sceHttpInit>,"sceHttpInit"},
	{0xd1c8945e,WrapI_V<sceHttpEnd>,"sceHttpEnd"},
	{0xa6800c34,0,"sceHttpInitCache"},
	{0x78b54c09,0,"sceHttpEndCache"},
	{0x59e6d16f,0,"sceHttpEnableCache"},
	{0xccbd167a,0,"sceHttpDisableCache"},
	{0xd70d4847,0,"sceHttpGetProxy"},
	{0x4cc7d78f,0,"sceHttpGetStatusCode"},
	{0xedeeb999,0,"sceHttpReadData"},
	{0xbb70706f,0,"sceHttpSendRequest"},
	{0xa5512e01,0,"sceHttpDeleteRequest"},
	{0x15540184,0,"sceHttpDeleteHeader"},
	{0x5152773b,0,"sceHttpDeleteConnection"},
	{0x8acd1f73,0,"sceHttpSetConnectTimeOut"},
	{0x9988172d,0,"sceHttpSetSendTimeOut"},
	{0xf0f46c62,0,"sceHttpSetProxy"},
	{0x0dafa58f,0,"sceHttpEnableCookie"},
	{0x78a0d3ec,0,"sceHttpEnableKeepAlive"},
	{0x0b12abfb,0,"sceHttpDisableCookie"},
	{0xc7ef2559,0,"sceHttpDisableKeepAlive"},
	{0xe4d21302,0,"sceHttpsInit"},
	{0xf9d8eb63,0,"sceHttpsEnd"},
	{0x47347b50,0,"sceHttpCreateRequest"},
	{0x8eefd953,0,"sceHttpCreateConnection"},
	{0xd081ec8f,0,"sceHttpGetNetworkErrno"},
	{0x3eaba285,0,"sceHttpAddExtraHeader"},
	{0xc10b6bd9,0,"sceHttpAbortRequest"},
	{0xfcf8c055,0,"sceHttpDeleteTemplate"},
	{0xf49934f6,0,"sceHttpSetMallocFunction"},
	{0x03D9526F,&WrapI_II<sceHttpSetResolveRetry>, "sceHttpSetResolveRetry"},
	{0x47940436,0,"sceHttpSetResolveTimeOut"},
	{0x2a6c3296,0,"sceHttpSetAuthInfoCB"},
	{0xd081ec8f,0,"sceHttpGetNetworkErrno"},
	{0x0809c831,0,"sceHttpEnableRedirect"},
	{0x9fc5f10d,0,"sceHttpEnableAuth"},
	{0x1a0ebb69,0,"sceHttpDisableRedirect"},
	{0xae948fee,0,"sceHttpDisableAuth"},
	{0xccbd167a,0,"sceHttpDisableCache"},
	{0xd081ec8f,0,"sceHttpGetNetworkErrno"},
	{0x76d1363b,0,"sceHttpSaveSystemCookie"},
	{0x87797bdd,0,"sceHttpsLoadDefaultCert"},
	{0xf1657b22,0,"sceHttpLoadSystemCookie"},
	{0x9B1F1F36,0,"sceHttpCreateTemplate"},
	{0xB509B09E,0,"sceHttpCreateRequestWithURL"},
	{0xCDF8ECB9,0,"sceHttpCreateConnectionWithURL"},
	{0x1F0FC3E3,0,"sceHttpSetRecvTimeOut"},
	{0xDB266CCF,0,"sceHttpGetAllHeader"},
	{0x0282A3BD,0,"sceHttpGetContentLength"},
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

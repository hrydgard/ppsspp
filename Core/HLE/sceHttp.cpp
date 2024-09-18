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

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"

#include "Core/HLE/sceHttp.h"
#include "Common/Net/HTTPClient.h"

// If http isn't loaded (seems unlikely), most functions should return SCE_KERNEL_ERROR_LIBRARY_NOTFOUND


// Could come in handy someday if we ever implement sceHttp* for real.
enum PSPHttpMethod {
	PSP_HTTP_METHOD_GET,
	PSP_HTTP_METHOD_POST,
	PSP_HTTP_METHOD_HEAD
};

// Just a holder for settings like user agent string
class HTTPTemplate {
	char useragent[512];
};

class HTTPConnection {

};

class HTTPRequest {

};


int sceHttpSetResolveRetry(int connectionID, int retryCount) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetResolveRetry(%d, %d)", connectionID, retryCount);
	return 0;
}

static int sceHttpInit(int unknown) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpInit(%i)", unknown);
	return 0;
}

static int sceHttpEnd() {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpEnd()");
	return 0;
}

static int sceHttpInitCache(int size) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpInitCache(%d)", size);
	return 0;
}

static int sceHttpEndCache() {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpEndCache()");
	return 0;
}

static int sceHttpEnableCache(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpEnableCache(%d)", id);
	return 0;
}

static int sceHttpDisableCache(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDisableCache(%d)", id);
	return 0;
}

static u32 sceHttpGetProxy(u32 id, u32 activateFlagPtr, u32 modePtr, u32 proxyHostPtr, u32 len, u32 proxyPort) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpGetProxy(%d, %x, %x, %x, %d, %x)", id, activateFlagPtr, modePtr, proxyHostPtr, len, proxyPort);
	return 0;
}

static int sceHttpGetStatusCode(int requestID, u32 statusCodePtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpGetStatusCode(%d, %x)", requestID, statusCodePtr);
	return 0;
}

static int sceHttpReadData(int requestID, u32 dataPtr, u32 dataSize) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpReadData(%d, %x, %x)", requestID, dataPtr, dataSize);
	return 0;
}

static int sceHttpSendRequest(int requestID, u32 dataPtr, u32 dataSize) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSendRequest(%d, %x, %x)", requestID, dataPtr, dataSize);
	return 0;
}

static int sceHttpDeleteRequest(int requestID) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDeleteRequest(%d)", requestID);
	return 0;
}

static int sceHttpDeleteHeader(int id, const char *name) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDeleteHeader(%d, %s)", id, name);
	return 0;
}

static int sceHttpDeleteConnection(int connectionID) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDisableCache(%d)", connectionID);
	return 0;
}

static int sceHttpSetConnectTimeOut(int id, u32 timeout) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetConnectTimeout(%d, %d)", id, timeout);
	return 0;
}

static int sceHttpSetSendTimeOut(int id, u32 timeout) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetSendTimeout(%d, %d)", id, timeout);
	return 0;
}

static u32 sceHttpSetProxy(u32 id, u32 activateFlagPtr, u32 mode, u32 newProxyHostPtr, u32 newProxyPort) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetProxy(%d, %x, %x, %x, %d)", id, activateFlagPtr, mode, newProxyHostPtr, newProxyPort);
	return 0;
}

static int sceHttpEnableCookie(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpEnableCookie(%d)", id);
	return 0;
}

static int sceHttpEnableKeepAlive(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpEnableKeepAlive(%d)", id);
	return 0;
}

static int sceHttpDisableCookie(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDisableCookie(%d)", id);
	return 0;
}

static int sceHttpDisableKeepAlive(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDisableKeepAlive(%d)", id);
	return 0;
}

static int sceHttpsInit(int unknown1, int unknown2, int unknown3, int unknown4) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpsInit(%d, %d, %d, %d)", unknown1, unknown2, unknown3, unknown4);
	return 0;
}

static int sceHttpsEnd() {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpsEnd()");
	return 0;
}

// Parameter "method" should be one of PSPHttpMethod's listed entries
static int sceHttpCreateRequest(int connectionID, int method, const char *path, u64 contentLength) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpCreateRequest(%d, %d, %s, %llx)", connectionID, method, path, contentLength);
	return 0;
}

static int sceHttpCreateConnection(int templateID, const char *hostString, const char *unknown1, u32 port, int unknown2) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpCreateConnection(%d, %s, %s, %d, %d)", templateID, hostString, unknown1, port, unknown2);
	return 0;
}

static int sceHttpGetNetworkErrno(int request, u32 errNumPtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpGetNetworkErrno(%d, %x)", request, errNumPtr);
	return 0;
}

static int sceHttpAddExtraHeader(int id, const char *name, const char *value, int unknown) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpAddExtraHeader(%d, %s, %s, %d)", id, name, value, unknown);
	return 0;
}

static int sceHttpAbortRequest(int requestID) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpAbortRequest(%d)", requestID);
	return 0;
}

static int sceHttpDeleteTemplate(int templateID) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDeleteTemplate(%d)", templateID);
	return 0;
}

static int sceHttpSetMallocFunction(u32 mallocFuncPtr, u32 freeFuncPtr, u32 reallocFuncPtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetMallocFunction(%x, %x, %x)", mallocFuncPtr, freeFuncPtr, reallocFuncPtr);
	return 0;
}

static int sceHttpSetResolveTimeOut(int id, u32 timeout) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetResolveTimeOut(%d, %d)", id, timeout);
	return 0;
}

static int sceHttpSetAuthInfoCB(int id, u32 callbackFuncPtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetAuthInfoCB(%d, %x)", id, callbackFuncPtr);
	return 0;
}

static int sceHttpEnableRedirect(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpEnableRedirect(%d)", id);
	return 0;
}

static int sceHttpEnableAuth(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpEnableAuth(%d)", id);
	return 0;
}

static int sceHttpDisableRedirect(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDisableRedirect(%d)", id);
	return 0;
}

static int sceHttpDisableAuth(int id) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpDisableAuth(%d)", id);
	return 0;
}

static int sceHttpSaveSystemCookie() {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSaveSystemCookie()");
	return 0;
}

static int sceHttpsLoadDefaultCert(int unknown1, int unknown2) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpLoadDefaultCert(%d, %d)", unknown1, unknown2);
	return 0;
}

static int sceHttpLoadSystemCookie() {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpLoadSystemCookie()");
	return 0;
}

static int sceHttpCreateTemplate(const char *agent, int unknown1, int unknown2) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpCreateTemplate(%s, %d, %d)", agent, unknown1, unknown2);
	return 0;
}

// Parameter "method" should be one of PSPHttpMethod's listed entries
static int sceHttpCreateRequestWithURL(int connectionID, int method, const char *url, u64 contentLength) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpCreateRequestWithURL(%d, %d, %s, %llx)", connectionID, method, url, contentLength);
	return 0;
}

static int sceHttpCreateConnectionWithURL(int templateID, const char *url, int unknown1) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpCreateConnectionWithURL(%d, %s, %d)", templateID, url, unknown1);
	return 0;
}

static int sceHttpSetRecvTimeOut(int id, u32 timeout) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpSetRecvTimeOut(%d, %x)", id, timeout);
	return 0;
}

static int sceHttpGetAllHeader(int request, u32 headerPtrToPtr, u32 headerSize) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpGetAllHeader(%d, %x, %x)", request, headerPtrToPtr, headerSize);
	return 0;
}

static int sceHttpGetContentLength(int requestID, u64 contentLengthPtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceHttpGetContentLength(%d, %llx)", requestID, contentLengthPtr);
	return 0;
}

const HLEFunction sceHttp[] = {
	{0XAB1ABE07, &WrapI_I<sceHttpInit>,                      "sceHttpInit",                    'i', "i"     },
	{0XD1C8945E, &WrapI_V<sceHttpEnd>,                       "sceHttpEnd",                     'i', ""      },
	{0XA6800C34, &WrapI_I<sceHttpInitCache>,                 "sceHttpInitCache",               'i', "i"     },
	{0X78B54C09, &WrapI_V<sceHttpEndCache>,                  "sceHttpEndCache",                'i', ""      },
	{0X59E6D16F, &WrapI_I<sceHttpEnableCache>,               "sceHttpEnableCache",             'i', "i"     },
	{0XCCBD167A, &WrapI_I<sceHttpDisableCache>,              "sceHttpDisableCache",            'i', "i"     },
	{0XD70D4847, &WrapU_UUUUUU<sceHttpGetProxy>,             "sceHttpGetProxy",                'x', "xxxxxx"},
	{0X4CC7D78F, &WrapI_IU<sceHttpGetStatusCode>,            "sceHttpGetStatusCode",           'i', "ix"    },
	{0XEDEEB999, &WrapI_IUU<sceHttpReadData>,                "sceHttpReadData",                'i', "ixx"   },
	{0XBB70706F, &WrapI_IUU<sceHttpSendRequest>,             "sceHttpSendRequest",             'i', "ixx"   },
	{0XA5512E01, &WrapI_I<sceHttpDeleteRequest>,             "sceHttpDeleteRequest",           'i', "i"     },
	{0X15540184, &WrapI_IC<sceHttpDeleteHeader>,             "sceHttpDeleteHeader",            'i', "is"    },
	{0X5152773B, &WrapI_I<sceHttpDeleteConnection>,          "sceHttpDeleteConnection",        'i', "i"     },
	{0X8ACD1F73, &WrapI_IU<sceHttpSetConnectTimeOut>,        "sceHttpSetConnectTimeOut",       'i', "ix"    },
	{0X9988172D, &WrapI_IU<sceHttpSetSendTimeOut>,           "sceHttpSetSendTimeOut",          'i', "ix"    },
	{0XF0F46C62, &WrapU_UUUUU<sceHttpSetProxy>,              "sceHttpSetProxy",                'x', "xxxxx" },
	{0X0DAFA58F, &WrapI_I<sceHttpEnableCookie>,              "sceHttpEnableCookie",            'i', "i"     },
	{0X78A0D3EC, &WrapI_I<sceHttpEnableKeepAlive>,           "sceHttpEnableKeepAlive",         'i', "i"     },
	{0X0B12ABFB, &WrapI_I<sceHttpDisableCookie>,             "sceHttpDisableCookie",           'i', "i"     },
	{0XC7EF2559, &WrapI_I<sceHttpDisableKeepAlive>,          "sceHttpDisableKeepAlive",        'i', "i"     },
	{0XE4D21302, &WrapI_IIII<sceHttpsInit>,                  "sceHttpsInit",                   'i', "iiii"  },
	{0XF9D8EB63, &WrapI_V<sceHttpsEnd>,                      "sceHttpsEnd",                    'i', ""      },
	{0X47347B50, &WrapI_IICU64<sceHttpCreateRequest>,        "sceHttpCreateRequest",           'i', "iisX"  },
	{0X8EEFD953, &WrapI_ICCUI<sceHttpCreateConnection>,      "sceHttpCreateConnection",        'i', "issxi" },
	{0XD081EC8F, &WrapI_IU<sceHttpGetNetworkErrno>,          "sceHttpGetNetworkErrno",         'i', "ix"    },
	{0X3EABA285, &WrapI_ICCI<sceHttpAddExtraHeader>,         "sceHttpAddExtraHeader",          'i', "issi"  },
	{0XC10B6BD9, &WrapI_I<sceHttpAbortRequest>,              "sceHttpAbortRequest",            'i', "i"     },
	{0XFCF8C055, &WrapI_I<sceHttpDeleteTemplate>,            "sceHttpDeleteTemplate",          'i', "i"     },
	{0XF49934F6, &WrapI_UUU<sceHttpSetMallocFunction>,       "sceHttpSetMallocFunction",       'i', "xxx"   },
	{0X03D9526F, &WrapI_II<sceHttpSetResolveRetry>,          "sceHttpSetResolveRetry",         'i', "ii"    },
	{0X47940436, &WrapI_IU<sceHttpSetResolveTimeOut>,        "sceHttpSetResolveTimeOut",       'i', "ix"    },
	{0X2A6C3296, &WrapI_IU<sceHttpSetAuthInfoCB>,            "sceHttpSetAuthInfoCB",           'i', "ix"    },
	{0X0809C831, &WrapI_I<sceHttpEnableRedirect>,            "sceHttpEnableRedirect",          'i', "i"     },
	{0X9FC5F10D, &WrapI_I<sceHttpEnableAuth>,                "sceHttpEnableAuth",              'i', "i"     },
	{0X1A0EBB69, &WrapI_I<sceHttpDisableRedirect>,           "sceHttpDisableRedirect",         'i', "i"     },
	{0XAE948FEE, &WrapI_I<sceHttpDisableAuth>,               "sceHttpDisableAuth",             'i', "i"     },
	{0X76D1363B, &WrapI_V<sceHttpSaveSystemCookie>,          "sceHttpSaveSystemCookie",        'i', ""      },
	{0X87797BDD, &WrapI_II<sceHttpsLoadDefaultCert>,         "sceHttpsLoadDefaultCert",        'i', "ii"    },
	{0XF1657B22, &WrapI_V<sceHttpLoadSystemCookie>,          "sceHttpLoadSystemCookie",        'i', ""      },
	{0X9B1F1F36, &WrapI_CII<sceHttpCreateTemplate>,          "sceHttpCreateTemplate",          'i', "sii"   },
	{0XB509B09E, &WrapI_IICU64<sceHttpCreateRequestWithURL>, "sceHttpCreateRequestWithURL",    'i', "iisX"  },
	{0XCDF8ECB9, &WrapI_ICI<sceHttpCreateConnectionWithURL>, "sceHttpCreateConnectionWithURL", 'i', "isi"   },
	{0X1F0FC3E3, &WrapI_IU<sceHttpSetRecvTimeOut>,           "sceHttpSetRecvTimeOut",          'i', "ix"    },
	{0XDB266CCF, &WrapI_IUU<sceHttpGetAllHeader>,            "sceHttpGetAllHeader",            'i', "ixx"   },
	{0X0282A3BD, &WrapI_IU64<sceHttpGetContentLength>,       "sceHttpGetContentLength",        'i', "iX"    },
	{0X7774BF4C, nullptr,                                    "sceHttpAddCookie",               '?', ""      },
	{0X68AB0F86, nullptr,                                    "sceHttpsInitWithPath",           '?', ""      },
	{0XB3FAF831, nullptr,                                    "sceHttpsDisableOption",          '?', ""      },
	{0X2255551E, nullptr,                                    "sceHttpGetNetworkPspError",      '?', ""      },
	{0XAB1540D5, nullptr,                                    "sceHttpsGetSslError",            '?', ""      },
	{0XA4496DE5, nullptr,                                    "sceHttpSetRedirectCallback",     '?', ""      },
	{0X267618F4, nullptr,                                    "sceHttpSetAuthInfoCallback",     '?', ""      },
	{0X569A1481, nullptr,                                    "sceHttpsSetSslCallback",         '?', ""      },
	{0XBAC31BF1, nullptr,                                    "sceHttpsEnableOption",           '?', ""      },
};				

void Register_sceHttp()
{
	RegisterModule("sceHttp",ARRAY_SIZE(sceHttp),sceHttp);
}

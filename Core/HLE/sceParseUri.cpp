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
#include "Core/HLE/sceParseUri.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Common/Net/URL.h"
#include "Common/StringUtils.h"


int workAreaAddString(u32 workAreaAddr, int workAreaSize, int offset, const char* s) {
	const std::string str = (s? s:"");

	int length = (int)str.length() + 1; // added space for null termination
	if (offset + length > workAreaSize) {
		length = workAreaSize - offset;
		if (length <= 0) {
			return offset;
		}
	}

	Memory::MemcpyUnchecked(workAreaAddr + offset, str.c_str(), length);
	return offset + length;
}

// FIXME: parsedUriArea, workArea can be 0/null? LittelBigPlanet seems to set workAreaSizeAddr to 0
static int sceUriParse(u32 parsedUriAreaAddr, const char* url, u32 workAreaAddr, u32 workAreaSizeAddr, int workAreaSize) {
	WARN_LOG(Log::sceNet, "UNTESTED sceUriParse(%x, %s, %x, %x, %d) at %08x", parsedUriAreaAddr, safe_string(url), workAreaAddr, workAreaSizeAddr, workAreaSize, currentMIPS->pc);
	if (url == nullptr)
		return hleLogError(Log::sceNet, -1, "invalid arg");

	auto workAreaSz = PSPPointer<u32>::Create(workAreaSizeAddr);

	// Size returner
	if (parsedUriAreaAddr == 0 || workAreaAddr == 0) {
		// Based on JPCSP: The required workArea size is maximum the size of the URL + 7 times the null-byte for string termination.
		int sz = (int)strlen(url) + 7;
		if (workAreaSz.IsValid()) {
			*workAreaSz = sz;
			workAreaSz.NotifyWrite("UriParse");
		}
		return hleLogDebug(Log::sceNet, 0, "workAreaSize: %d, %d", sz, workAreaSize);
	}

	auto parsedUri = PSPPointer<PSPParsedUri>::Create(parsedUriAreaAddr);
	
	// Parse the URL into URI components
	// Full URI = scheme ":" ["//" [username [":" password] "@"] host [":" port]] path ["?" query] ["#" fragment]
	Url uri(url);
	// FIXME: URI without "://" should be valid too, due to PSPParsedUri.noSlash property
	if (!uri.Valid()) {
		return hleLogError(Log::sceNet, -1, "invalid arg");
	}

	// Parse Host() into userInfo (in the format "<userName>:<password>") and host
	std::string host = uri.Host();
	std::string userInfoUserName = "";
	std::string userInfoPassword = "";
	// Extract Host
	size_t pos = host.find('@');
	if (pos <= host.size()) {
		userInfoUserName = host.substr(0, pos);
		host.erase(0, pos + 1LL); // removing the "@"
	}
	// Extract UserName and Password
	pos = userInfoUserName.find(':');
	if (pos <= userInfoUserName.size()) {
		userInfoPassword = userInfoUserName.substr(pos + 1LL); // removing the ":"
		userInfoUserName.erase(pos);
	}

	// Parse Resource() into path(/), query(?), and fragment(#)
	std::string path = uri.Resource();
	std::string query = "";
	std::string fragment = "";
	// Extract Path
	pos = path.find('?');
	if (pos <= path.size()) {
		query = path.substr(pos); // Not removing the leading "?". Query must have the leading "?" if it's not empty (according to JPCSP)
		if (query.size() == 1)
			query.clear();
		path.erase(pos);
	}
	// Extract Query and Fragment
	pos = query.find('#');
	if (pos <= query.size()) {
		fragment = query.substr(pos);  // FIXME: Should we remove the leading "#" ? probably not, just like query
		query.erase(pos);
	}
	
	// FIXME: Related to "scheme-specific-part" (ie. parts after the scheme + ":") ? 0 = start with "//"
	pos = std::string(url).find("://");
	if (parsedUri.IsValid())
		parsedUri->noSlash = (pos != std::string::npos) ? 0 : 1;

	// Store the URI components in sequence into workAreanand store the respective addresses into the parsedUri structure.
	int offset = 0;
	if (parsedUri.IsValid())
		parsedUri->schemeAddr = workAreaAddr + offset;
	offset = workAreaAddString(workAreaAddr, workAreaSize, offset, uri.Protocol().c_str()); // FiXME: scheme in lowercase, while protocol in uppercase?

	if (parsedUri.IsValid())
		parsedUri->userInfoUserNameAddr = workAreaAddr + offset;
	offset = workAreaAddString(workAreaAddr, workAreaSize, offset, userInfoUserName.c_str());

	if (parsedUri.IsValid())
		parsedUri->userInfoPasswordAddr = workAreaAddr + offset;
	offset = workAreaAddString(workAreaAddr, workAreaSize, offset, userInfoPassword.c_str());

	if (parsedUri.IsValid())
		parsedUri->hostAddr = workAreaAddr + offset;
	offset = workAreaAddString(workAreaAddr, workAreaSize, offset, host.c_str());

	if (parsedUri.IsValid())
		parsedUri->pathAddr = workAreaAddr + offset;
	offset = workAreaAddString(workAreaAddr, workAreaSize, offset, path.c_str());

	if (parsedUri.IsValid())
		parsedUri->queryAddr = workAreaAddr + offset;
	offset = workAreaAddString(workAreaAddr, workAreaSize, offset, query.c_str());

	if (parsedUri.IsValid())
		parsedUri->fragmentAddr = workAreaAddr + offset;
	offset = workAreaAddString(workAreaAddr, workAreaSize, offset, fragment.c_str());

	if (parsedUri.IsValid()) {
		parsedUri->port = uri.Port();
		memset(parsedUri->unknown, 0, sizeof(parsedUri->unknown));
		parsedUri.NotifyWrite("UriParse");
	}

	// Update the size
	if (workAreaSz.IsValid()) {
		*workAreaSz = offset;
		workAreaSz.NotifyWrite("UriParse");
	}

	return 0;
}

const HLEFunction sceParseUri[] =
{
	{0X49E950EC, nullptr,                            "sceUriEscape",   '?', ""},
	{0X062BB07E, nullptr,                            "sceUriUnescape", '?', ""},
	{0X568518C9, &WrapI_UCUUI<sceUriParse>,          "sceUriParse",    'i', "xsxxi" },
	{0X7EE318AF, nullptr,                            "sceUriBuild",    '?', ""},
};

void Register_sceParseUri()
{
	RegisterModule("sceParseUri", ARRAY_SIZE(sceParseUri), sceParseUri);
}

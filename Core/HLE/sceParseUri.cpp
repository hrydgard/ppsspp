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
		return hleLogDebug(Log::sceNet, -1, "workAreaSize: %d, %d", sz, workAreaSize);
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

static int sceUriBuild(u32 workAreaAddr, u32 workAreaSizeAddr, int workAreaSize, u32 parsedUriAddr, int flags) {
	WARN_LOG(Log::sceNet, "UNTESTED sceUriBuild(%x, %x, %d, %x, %d) at %08x", workAreaAddr, workAreaSizeAddr, workAreaSize, parsedUriAddr, flags, currentMIPS->pc);

	auto workAreaSz = PSPPointer<u32>::Create(workAreaSizeAddr);
	
	if (parsedUriAddr == 0) {
		return hleLogError(Log::sceNet, -1, "invalid parsedUriAddr");
	}

	auto parsedUri = PSPPointer<PSPParsedUri>::Create(parsedUriAddr);
	if (!parsedUri.IsValid()) {
		return hleLogError(Log::sceNet, -1, "invalid parsedUri structure");
	}

	auto getUriComponent = [](u32 addr, int flags, int flagMask) -> std::string {
		if (addr == 0 || (flags & flagMask) == 0) return "";
		const char* str = Memory::GetCharPointer(addr);
		return str ? std::string(str) : "";
	};

	std::string scheme = getUriComponent(parsedUri->schemeAddr, flags, 0x1);
	std::string userInfoUserName = getUriComponent(parsedUri->userInfoUserNameAddr, flags, 0x10);
	std::string userInfoPassword = getUriComponent(parsedUri->userInfoPasswordAddr, flags, 0x20);
	std::string host = getUriComponent(parsedUri->hostAddr, flags, 0x2);
	std::string path = getUriComponent(parsedUri->pathAddr, flags, 0x8);
	std::string query = getUriComponent(parsedUri->queryAddr, flags, 0x40);
	std::string fragment = getUriComponent(parsedUri->fragmentAddr, flags, 0x80);
	int port = (flags & 0x4) != 0 ? parsedUri->port : -1;

	// Build the complete URI
	std::string uri = "";
	
	if (!scheme.empty()) {
		std::string finalScheme = scheme;
		if (finalScheme == "https") { // https is unsupported
			finalScheme = "http";
		}
		uri += finalScheme + ":";
	}
	
	if (parsedUri->noSlash == 0) {
		uri += "//";
	}
	
	if (!userInfoUserName.empty()) {
		uri += userInfoUserName;
		if (!userInfoPassword.empty()) {
			uri += ":" + userInfoPassword;
		}
		uri += "@";
	}
	
	if (!host.empty()) {
		uri += host;
	}
	
	if (port > 0) {
		int defaultPort = -1;
		if (parsedUri->schemeAddr != 0) {
			const char* protocol = Memory::GetCharPointer(parsedUri->schemeAddr);
			if (protocol) {
				std::string protocolStr(protocol);
				if (protocolStr == "http") defaultPort = 80;
				//else if (protocolStr == "https") defaultPort = 443;
				else if (protocolStr == "ftp") defaultPort = 21;
				else if (protocolStr == "ssh") defaultPort = 22;
			}
		}
		if (port != defaultPort) {
			uri += ":" + std::to_string(port);
		}
	}
	
	if (!path.empty()) {
		uri += path;
	}
	
	if (!query.empty()) {
		uri += query;
	}
	
	if (!fragment.empty()) {
		uri += fragment;
	}

	int requiredSize = (int)uri.length() + 1;

	if (workAreaSz.IsValid()) {
		*workAreaSz = requiredSize;
		workAreaSz.NotifyWrite("UriBuild");
	}

	if (workAreaAddr == 0) {
		return hleLogDebug(Log::sceNet, -1, "size query, required size: %d", requiredSize);
	}

	if (requiredSize > workAreaSize) {
		return hleLogError(Log::sceNet, -1, "work area too small: need %d, have %d", requiredSize, workAreaSize);
	}

	Memory::MemcpyUnchecked(workAreaAddr, uri.c_str(), requiredSize);

	return hleLogDebug(Log::sceNet, 0, "built URI: %s", uri.c_str());
}

static int sceUriEscape(u32 escapedAddr, u32 escapedLengthAddr, int escapedBufferLength, const char* source) {
	WARN_LOG(Log::sceNet, "UNTESTED sceUriEscape(%x, %x, %d, %s) at %08x", escapedAddr, escapedLengthAddr, escapedBufferLength, safe_string(source), currentMIPS->pc);

	if (source == nullptr) {
		return hleLogError(Log::sceNet, -1, "invalid source");
	}

	auto escapedLength = PSPPointer<u32>::Create(escapedLengthAddr);

	// Helper function to check if a character needs to be escaped
	auto needsEscaping = [](unsigned char c) -> bool {
		// Characters that should NOT be escaped (unreserved characters in RFC 3986)
		// ALPHA / DIGIT / "-" / "." / "_" / "~"
		if ((c >= 'A' && c <= 'Z') || 
		    (c >= 'a' && c <= 'z') || 
		    (c >= '0' && c <= '9') ||
		    c == '-' || c == '.' || c == '_' || c == '~') {
			return false;
		}
		// Everything else needs to be escaped
		return true;
	};

	// Helper function to convert a hex digit to character
	auto toHexChar = [](int digit) -> char {
		if (digit < 10) return '0' + digit;
		return 'A' + (digit - 10);
	};

	std::string sourceStr(source);
	int requiredSize = 0;
	
	for (unsigned char c : sourceStr) {
		if (needsEscaping(c)) {
			requiredSize += 3; // %XX format
		} else {
			requiredSize += 1; // unchanged character
		}
	}
	requiredSize += 1; // null terminator

	if (escapedLength.IsValid()) {
		*escapedLength = requiredSize;
		escapedLength.NotifyWrite("UriEscape");
	}

	if (escapedAddr == 0) {
		return hleLogError(Log::sceNet, -1, "size query, required size: %d", requiredSize);
	}

	if (requiredSize > escapedBufferLength) {
		return hleLogError(Log::sceNet, -1, "buffer too small: need %d, have %d", requiredSize, escapedBufferLength);
	}

	std::string escaped = "";
	
	for (unsigned char c : sourceStr) {
		if (needsEscaping(c)) {
			// Escape as %XX
			escaped += '%';
			escaped += toHexChar((c >> 4) & 0xF);
			escaped += toHexChar(c & 0xF); 
		} else {
			// Keep unchanged
			escaped += c;
		}
	}

	Memory::MemcpyUnchecked(escapedAddr, escaped.c_str(), escaped.length() + 1);

	return hleLogDebug(Log::sceNet, 0, "escaped '%s' to '%s'", source, escaped.c_str());
}

const HLEFunction sceParseUri[] =
{
	{0X49E950EC, &WrapI_UUIC<sceUriEscape>,          "sceUriEscape",   'i', "xxis"},
	{0X062BB07E, nullptr,                            "sceUriUnescape", '?', ""},
	{0X568518C9, &WrapI_UCUUI<sceUriParse>,          "sceUriParse",    'i', "xsxxi" },
	{0X7EE318AF, &WrapI_UUIUI<sceUriBuild>,          "sceUriBuild",    'i', "xxxxi" },
};

void Register_sceParseUri()
{
	RegisterHLEModule("sceParseUri", ARRAY_SIZE(sceParseUri), sceParseUri);
}

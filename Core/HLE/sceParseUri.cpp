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
#include "Core/HLE/ErrorCodes.h"


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

// The shape sceUriParse actually splits a URI into, worked out on hardware by
// pspautotests/tests/net/parseuri:
//
//   [scheme ":"] ["//"] [[user [":" password] "@"] host [":" port]] path ["?" query] ["#" frag]
//
// Every one of the seven pieces is written into the caller's work area even when empty, so each
// gets its own address and the required size is the sum of their lengths plus a terminator
// each. Query and fragment keep their leading "?" and "#". The scheme is stored with its case
// intact but matched case-insensitively when filling in a default port.
//
// This does not use the Url class: that one is built for HTTP, insists on "://", turns an empty
// path into "/" and has nowhere to put a fragment.

// A host runs until something that cannot appear in one. Letters, digits, dots and dashes are
// in; underscore, percent and bracket are not, which is why an IPv6 literal in brackets ends up
// as the path instead of the host.
static bool IsUriHostChar(char c) {
	return isalnum((unsigned char)c) || c == '.' || c == '-';
}

// Ports take at most five digits and at most the value 65536, which then wraps to zero. Anything
// longer or larger is refused outright, and so is anything that isn't a digit.
static bool ParseUriPort(const std::string &str, u16 *port) {
	if (str.empty()) {
		// No port given at all - the caller keeps the scheme's default.
		return true;
	}
	if (str.size() > 5) {
		return false;
	}
	u32 value = 0;
	for (char c : str) {
		if (!isdigit((unsigned char)c)) {
			return false;
		}
		value = value * 10 + (c - '0');
	}
	if (value > 65536) {
		return false;
	}
	*port = (u16)value;
	return true;
}

static u16 UriDefaultPort(const std::string &scheme) {
	if (equalsNoCase(scheme, "http")) {
		return 80;
	}
	if (equalsNoCase(scheme, "https")) {
		return 443;
	}
	return 0;
}

struct UriParts {
	std::string scheme;
	std::string user;
	std::string password;
	std::string host;
	std::string path;
	std::string query;
	std::string fragment;
	int noSlash = 1;
	u16 port = 0;
};

static bool SplitUri(const std::string &uri, UriParts *parts) {
	size_t pos = 0;

	// A scheme only counts if its colon comes before any slash.
	const size_t colon = uri.find(':');
	const size_t slash = uri.find('/');
	if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
		parts->scheme = uri.substr(0, colon);
		pos = colon + 1;
	}
	parts->port = UriDefaultPort(parts->scheme);

	// "//" right after the scheme is the only thing that clears noSlash, and it is also the only
	// way to get an authority when the remainder starts with a slash.
	bool authority = pos < uri.size() && uri[pos] != '/';
	if (!parts->scheme.empty() && uri.compare(pos, 2, "//") == 0) {
		parts->noSlash = 0;
		pos += 2;
		authority = true;
	}

	if (authority) {
		// Userinfo, if there is an "@" before the authority ends.
		const size_t end = std::min(uri.find('/', pos), uri.size());
		const size_t at = uri.find('@', pos);
		if (at != std::string::npos && at < end) {
			std::string userInfo = uri.substr(pos, at - pos);
			const size_t sep = userInfo.find(':');
			if (sep != std::string::npos) {
				parts->password = userInfo.substr(sep + 1);
				userInfo.erase(sep);
			}
			parts->user = userInfo;
			pos = at + 1;
		}

		const size_t hostStart = pos;
		while (pos < uri.size() && IsUriHostChar(uri[pos])) {
			pos++;
		}
		parts->host = uri.substr(hostStart, pos - hostStart);

		// Only a colon straight after the host is a port; anything else the scan stopped on is
		// the start of the path.
		if (pos < uri.size() && uri[pos] == ':') {
			const size_t portEnd = std::min(uri.find('/', pos + 1), uri.size());
			if (!ParseUriPort(uri.substr(pos + 1, portEnd - pos - 1), &parts->port)) {
				return false;
			}
			pos = portEnd;
		}
	}

	// Whatever is left is path, then query from "?", then fragment from "#", each keeping its
	// leading punctuation.
	std::string rest = uri.substr(std::min(pos, uri.size()));
	const size_t hash = rest.find('#');
	if (hash != std::string::npos) {
		parts->fragment = rest.substr(hash);
		rest.erase(hash);
	}
	const size_t question = rest.find('?');
	if (question != std::string::npos) {
		parts->query = rest.substr(question);
		rest.erase(question);
	}
	parts->path = rest;
	return true;
}

static int sceUriParse(u32 parsedUriAreaAddr, const char *url, u32 workAreaAddr, u32 workAreaSizeAddr, int workAreaSize) {
	if (url == nullptr) {
		return hleLogError(Log::sceNet, SCE_PARSEURI_ERROR_INVALID_URI, "null uri");
	}

	auto workAreaSz = PSPPointer<u32_le>::Create(workAreaSizeAddr);
	// Leaving out either area means "just tell me how much room this needs", which needs
	// somewhere to put the answer.
	const bool sizeOnly = parsedUriAreaAddr == 0 || workAreaAddr == 0;
	if (sizeOnly && !workAreaSz.IsValid()) {
		return hleLogError(Log::sceNet, SCE_PARSEURI_ERROR_INVALID_PARAMETER, "nowhere to put the size");
	}

	UriParts parts;
	if (!SplitUri(url, &parts)) {
		return hleLogError(Log::sceNet, SCE_PARSEURI_ERROR_INVALID_URI, "bad uri: %s", url);
	}

	const std::string *pieces[] = {
		&parts.scheme, &parts.user, &parts.password, &parts.host,
		&parts.path, &parts.query, &parts.fragment,
	};
	int needed = 0;
	for (const std::string *piece : pieces) {
		needed += (int)piece->size() + 1;
	}

	if (!sizeOnly) {
		if (workAreaSize < needed) {
			return hleLogError(Log::sceNet, SCE_PARSEURI_ERROR_OUT_OF_MEMORY, "work area of %d needs %d", workAreaSize, needed);
		}
		// workAreaAddString writes into it unchecked.
		if (!Memory::IsValidRange(workAreaAddr, needed) || !Memory::IsValidRange(parsedUriAreaAddr, sizeof(PSPParsedUri))) {
			return hleLogError(Log::sceNet, -1, "invalid work area or parsed URI area");
		}

		auto parsedUri = PSPPointer<PSPParsedUri>::Create(parsedUriAreaAddr);
		s32 *fields[] = {
			&parsedUri->schemeAddr, &parsedUri->userInfoUserNameAddr, &parsedUri->userInfoPasswordAddr,
			&parsedUri->hostAddr, &parsedUri->pathAddr, &parsedUri->queryAddr, &parsedUri->fragmentAddr,
		};
		int offset = 0;
		for (size_t i = 0; i < ARRAY_SIZE(pieces); ++i) {
			*fields[i] = workAreaAddr + offset;
			offset = workAreaAddString(workAreaAddr, workAreaSize, offset, pieces[i]->c_str());
		}
		parsedUri->noSlash = parts.noSlash;
		parsedUri->port = parts.port;
		memset(parsedUri->unknown, 0, sizeof(parsedUri->unknown));
		parsedUri.NotifyWrite("UriParse");
		NotifyMemInfo(MemBlockFlags::WRITE, workAreaAddr, offset, "UriParse");
	}

	if (workAreaSz.IsValid()) {
		*workAreaSz = needed;
		workAreaSz.NotifyWrite("UriParse");
	}
	return hleLogDebug(Log::sceNet, 0);
}

static int sceUriBuild(MIPSState *mips, u32 workAreaAddr, u32 workAreaSizeAddr, int workAreaSize, u32 parsedUriAddr, int flags) {
	WARN_LOG(Log::sceNet, "UNTESTED sceUriBuild(%x, %x, %d, %x, %d) at %08x", workAreaAddr, workAreaSizeAddr, workAreaSize, parsedUriAddr, flags, mips->pc);

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
		return Memory::IsValidNullTerminatedString(addr) ? std::string(Memory::GetCharPointerUnchecked(addr)) : "";
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
		if (Memory::IsValidNullTerminatedString(parsedUri->schemeAddr)) {
			std::string protocolStr(Memory::GetCharPointerUnchecked(parsedUri->schemeAddr));
			if (protocolStr == "http") defaultPort = 80;
			//else if (protocolStr == "https") defaultPort = 443;
			else if (protocolStr == "ftp") defaultPort = 21;
			else if (protocolStr == "ssh") defaultPort = 22;
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
		return hleLogDebug(Log::sceNet, 0, "size query, required size: %d", requiredSize);
	}

	if (requiredSize > workAreaSize) {
		return hleLogError(Log::sceNet, -1, "work area too small: need %d, have %d", requiredSize, workAreaSize);
	}
	if (!Memory::IsValidRange(workAreaAddr, requiredSize)) {
		return hleLogError(Log::sceNet, -1, "invalid work area");
	}

	Memory::MemcpyUnchecked(workAreaAddr, uri.c_str(), requiredSize);

	return hleLogDebug(Log::sceNet, 0, "built URI: %s", uri.c_str());
}

// Everything but these gets percent-encoded, with uppercase hex. Note that "*" survives and "~"
// does not, which is the other way round from RFC 3986's unreserved set - measured on hardware
// by pspautotests/tests/net/parseuri/escape.
static bool IsUriUnreserved(unsigned char c) {
	return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '*';
}

static int HexDigit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	return (c | 0x20) - 'a' + 10;
}

// When the buffer is too small the library still fills what it can before giving up, and leaves
// the length output alone. Returns the value to hand back; the caller does the logging, since
// the hleLog macros only work directly inside an HLE function.
static int UriWriteResult(u32 outAddr, u32 lengthAddr, int bufferLength, const std::string &str, int success) {
	auto lengthPtr = PSPPointer<u32_le>::Create(lengthAddr);
	const int required = (int)str.size() + 1;

	if (outAddr == 0) {
		// No buffer at all is a size query.
		if (lengthPtr.IsValid()) {
			*lengthPtr = required;
			lengthPtr.NotifyWrite("UriEscape");
		}
		return success;
	}

	if (required > bufferLength) {
		const int partial = std::min((int)str.size(), bufferLength);
		if (partial > 0 && Memory::IsValidRange(outAddr, partial)) {
			Memory::MemcpyUnchecked(outAddr, str.data(), partial);
		}
		return SCE_PARSEURI_ERROR_OUT_OF_MEMORY;
	}

	if (!Memory::IsValidRange(outAddr, required)) {
		return -1;
	}
	Memory::MemcpyUnchecked(outAddr, str.c_str(), required);
	if (lengthPtr.IsValid()) {
		*lengthPtr = required;
		lengthPtr.NotifyWrite("UriEscape");
	}
	return success;
}

static int sceUriEscape(u32 escapedAddr, u32 escapedLengthAddr, int escapedBufferLength, const char *source) {
	// The real one reads straight through this pointer with no check, so a game passing null
	// gets an exception rather than an error. An error is more useful to us.
	if (source == nullptr) {
		return hleLogError(Log::sceNet, SCE_PARSEURI_ERROR_INVALID_URI, "null source");
	}

	static const char *hex = "0123456789ABCDEF";
	std::string escaped;
	for (const char *p = source; *p != '\0'; ++p) {
		const unsigned char c = (unsigned char)*p;
		if (IsUriUnreserved(c)) {
			escaped.push_back((char)c);
		} else {
			escaped.push_back('%');
			escaped.push_back(hex[c >> 4]);
			escaped.push_back(hex[c & 0xF]);
		}
	}

	const int result = UriWriteResult(escapedAddr, escapedLengthAddr, escapedBufferLength, escaped, 0);
	if (result < 0) {
		return hleLogError(Log::sceNet, result, "buffer of %d too small", escapedBufferLength);
	}
	return hleLogDebug(Log::sceNet, result);
}

static int sceUriUnescape(u32 unescapedAddr, u32 unescapedLengthAddr, int unescapedBufferLength, const char *source) {
	if (source == nullptr) {
		return hleLogError(Log::sceNet, SCE_PARSEURI_ERROR_INVALID_URI, "null source");
	}

	const std::string src(source);
	std::string unescaped;
	// Unlike escape, which always answers zero, this one comes back with a running count of one
	// per plain character and five per escape. Nothing is known to depend on it, but it is what
	// the hardware returns and it is positive, so callers testing for failure still work.
	int count = 0;

	for (size_t i = 0; i < src.size(); ) {
		if (src[i] != '%') {
			unescaped.push_back(src[i]);
			i++;
			count += 1;
			continue;
		}
		if (i + 2 >= src.size() || !isxdigit((unsigned char)src[i + 1]) || !isxdigit((unsigned char)src[i + 2])) {
			return hleLogError(Log::sceNet, SCE_PARSEURI_ERROR_INVALID_URI, "truncated escape in %s", source);
		}
		unescaped.push_back((char)((HexDigit(src[i + 1]) << 4) | HexDigit(src[i + 2])));
		i += 3;
		count += 5;
	}

	const int result = UriWriteResult(unescapedAddr, unescapedLengthAddr, unescapedBufferLength, unescaped, count);
	if (result < 0) {
		return hleLogError(Log::sceNet, result, "buffer of %d too small", unescapedBufferLength);
	}
	return hleLogDebug(Log::sceNet, result);
}

const HLEFunction sceParseUri[] =
{
	{0X49E950EC, &WrapI_UUIC<sceUriEscape>,          "sceUriEscape",   'i', "xxis"},
	{0X062BB07E, &WrapI_UUIC<sceUriUnescape>,        "sceUriUnescape", 'i', "xxis"},
	{0X568518C9, &WrapI_UCUUI<sceUriParse>,          "sceUriParse",    'i', "xsxxi" },
	{0X7EE318AF, &WrapI_UUIUI<sceUriBuild>,          "sceUriBuild",    'i', "xxxxi" },
	// libparse_uri.prx exports two more that nothing has been seen to call. 8aa55d17 goes
	// through sceUriParse and sceUriBuild, so it is probably a resolve-against-a-base helper;
	// 8885a782 stands alone. Listed so a game that does call one is named in the log.
	{0X8AA55D17, nullptr,                            "sceParseUri_8AA55D17", '?', "" },
	{0X8885A782, nullptr,                            "sceParseUri_8885A782", '?', "" },
};

void Register_sceParseUri()
{
	RegisterHLEModule("sceParseUri", ARRAY_SIZE(sceParseUri), sceParseUri);
}

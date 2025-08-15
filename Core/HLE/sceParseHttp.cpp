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

#include <sstream>
#include <regex>

#include "Core/HLE/HLE.h"
#include "Core/HLE/sceHttp.h"
#include "Core/HLE/sceParseHttp.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Common/StringUtils.h"

std::string getHeaderString(std::istringstream &headers) {
	std::string line = "";
	while (headers.rdbuf()->in_avail()) {
		char c = headers.get();
		if (c == '\n' || c == '\r') {
			break;
		}
		line.push_back(c);
	}

	return line;
}

// FIXME: Is it allowed for fieldName to be null/0 or empty string ? JPCSP seems to ignore it
static int sceParseHttpResponseHeader(u32 headerAddr, int headerLength, const char *fieldName, u32 valueAddr, u32 valueLengthAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceParseHttpResponseHeader(%x, %i, %s, %x, %x[%i]) at %08x", headerAddr, headerLength, fieldName, valueAddr, valueLengthAddr, Memory::Read_U32(valueLengthAddr), currentMIPS->pc);
	if (!Memory::IsValidRange(headerAddr, headerLength))
		return hleLogError(Log::sceNet, -1, "invalid arg");

	if (!Memory::IsValidRange(valueLengthAddr, 4))
		return hleLogError(Log::sceNet, -1, "invalid arg");

	// FIXME: Not sure whether valuerAddr can be null or not (in case the game just need to get the size to allocate the value buffer first)
	// Note: Based on the outputted value address from JPCSP, the address seems to be within the input headers address range, thus no need to allocate output value buffer
	if (!Memory::IsValidRange(valueAddr, Memory::Read_U32(valueLengthAddr)))
		return hleLogError(Log::sceNet, -1, "invalid arg");

	std::string field = "";
	if (fieldName != nullptr)
		field = std::string(fieldName);
	field = StripSpaces(field);
	std::string headers(Memory::GetCharPointer(headerAddr), headerLength);
	std::istringstream hdrs(headers);
	u32 addr = 0;
	u32 len = 0;
	bool found = false;
	while (hdrs.rdbuf()->in_avail()) {
		std::string headerString = getHeaderString(hdrs);
		const std::string delim = ":"; // JPCSP use ": " delimiter
		size_t delimpos = headerString.find(delim);
		std::string key = headerString.substr(0, delimpos); // the key part
		if (equalsNoCase(StripSpaces(key), field.c_str())) {
			found = true;
			int offset = hdrs.tellg();
			if (offset >= 0) {
				offset -= (int)headerString.length();
				offset--; // counting the excluded LF/CR
			}
			else {
				offset = headerLength - (int)headerString.length();
			}
			addr = headerAddr + offset + (int)(delimpos != std::string::npos? delimpos + delim.length() : headerString.length()); // value address within the headers
			headerString.erase(0, delimpos + delim.length()); // the value part
			size_t valpos = headerString.find_first_not_of(" "); // excludes the leading space (if any)
			if (valpos == std::string::npos) {
				len = 0;
			}
			else {
				len = (int)headerString.length() - (int)valpos;
				addr += (int)valpos;
			}

			Memory::WriteUnchecked_U32(addr, valueAddr);
			NotifyMemInfo(MemBlockFlags::WRITE, valueAddr, 4, "sceParseHttpResponseHeader");
			Memory::WriteUnchecked_U32(len, valueLengthAddr);
			NotifyMemInfo(MemBlockFlags::WRITE, valueLengthAddr, 4, "sceParseHttpResponseHeader");
			break;
		}
	}

	if (!found) {
		Memory::WriteUnchecked_U32(0, valueAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, valueAddr, 4, "sceParseHttpResponseHeader");
		Memory::WriteUnchecked_U32(0, valueLengthAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, valueLengthAddr, 4, "sceParseHttpResponseHeader");
		return hleLogError(Log::sceNet, SCE_HTTP_ERROR_PARSE_HTTP_NOT_FOUND, "parse http not found");
	}

	return hleLogDebug(Log::sceNet, len);
}

static int sceParseHttpStatusLine(u32 headerAddr, u32 headerLength, u32 httpVersionMajorAddr, u32 httpVersionMinorAddr, u32 httpStatusCodeAddr, u32 httpStatusCommentAddr, u32 httpStatusCommentLengthAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED sceParseHttpStatusLine(%x, %d, %x, %x, %x, %x, %x) at %08x", headerAddr, headerLength, httpVersionMajorAddr, httpVersionMinorAddr, httpStatusCodeAddr, httpStatusCommentAddr, httpStatusCommentLengthAddr, currentMIPS->pc);
	if (!Memory::IsValidRange(headerAddr, headerLength))
		return hleLogError(Log::sceNet, -1, "invalid arg");

	std::string headers(Memory::GetCharPointer(headerAddr), headerLength);
	// Get first line from header, line ending can be '\n' or '\r'
	std::istringstream hdr(headers);
	std::string line;
	std::getline(hdr, line, '\n');
	hdr.str(line);
	std::getline(hdr, line, '\r');
	NotifyMemInfo(MemBlockFlags::READ, headerAddr, (u32)line.size(), "ParseHttpStatusLine");
	DEBUG_LOG(Log::sceNet, "Headers: %s", headers.c_str());

	// Split the string by pattern, based on JPCSP
	std::regex rgx("HTTP/(\\d)\\.(\\d)\\s+(\\d+)(.*)");
	std::smatch matches;

	if (!std::regex_search(line, matches, rgx))
		return hleLogError(Log::sceNet, -1, "invalid arg"); // SCE_HTTP_ERROR_PARSE_HTTP_NOT_FOUND

	try {
		// Convert and Store the value
		if (Memory::IsValidRange(httpVersionMajorAddr, 4)) {
			Memory::WriteUnchecked_U32(stoi(matches[1].str()), httpVersionMajorAddr);
			NotifyMemInfo(MemBlockFlags::WRITE, httpVersionMajorAddr, 4, "ParseHttpStatusLine");
		}

		if (Memory::IsValidRange(httpVersionMinorAddr, 4)) {
			Memory::WriteUnchecked_U32(stoi(matches[2].str()), httpVersionMinorAddr);
			NotifyMemInfo(MemBlockFlags::WRITE, httpVersionMinorAddr, 4, "ParseHttpStatusLine");
		}

		if (Memory::IsValidRange(httpStatusCodeAddr, 4)) {
			Memory::WriteUnchecked_U32(stoi(matches[3].str()), httpStatusCodeAddr);
			NotifyMemInfo(MemBlockFlags::WRITE, httpStatusCodeAddr, 4, "ParseHttpStatusLine");
		}

		std::string sc = matches[4].str();
		if (Memory::IsValidRange(httpStatusCommentAddr, 4)) {
			Memory::WriteUnchecked_U32(headerAddr + (int)headers.find(sc), httpStatusCommentAddr);
			NotifyMemInfo(MemBlockFlags::WRITE, httpStatusCommentAddr, 4, "ParseHttpStatusLine");
		}

		if (Memory::IsValidRange(httpStatusCommentLengthAddr, 4)) {
			Memory::WriteUnchecked_U32((u32)sc.size(), httpStatusCommentLengthAddr);
			NotifyMemInfo(MemBlockFlags::WRITE, httpStatusCommentLengthAddr, 4, "ParseHttpStatusLine");
		}
	}
	catch (const std::runtime_error& ex) {
		return hleLogError(Log::sceNet, -1, "Runtime error: %s", ex.what());
	}
	catch (const std::exception& ex) {
		return hleLogError(Log::sceNet, -1, "Error occurred: %s", ex.what()); // SCE_HTTP_ERROR_PARSE_HTTP_INVALID_VALUE
	}
	catch (...) {
		return hleLogError(Log::sceNet, -1, "Unknown error");
	}

	return hleLogDebug(Log::sceNet, 0);
}

const HLEFunction sceParseHttp[] = {
	{0X8077A433, &WrapI_UUUUUUU<sceParseHttpStatusLine>,      "sceParseHttpStatusLine",     'i', "xxxxxxx"},
	{0XAD7BFDEF, &WrapI_UICUU<sceParseHttpResponseHeader>,    "sceParseHttpResponseHeader", 'i', "xisxx"  },
};

void Register_sceParseHttp() {
	RegisterHLEModule("sceParseHttp", ARRAY_SIZE(sceParseHttp), sceParseHttp);
}

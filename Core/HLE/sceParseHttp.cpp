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
#include "Core/HLE/sceParseHttp.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/Debugger/MemBlockInfo.h"


static int sceParseHttpStatusLine(u32 headerAddr, u32 headerLength, u32 httpVersionMajorAddr, u32 httpVersionMinorAddr, u32 httpStatusCodeAddr, u32 httpStatusCommentAddr, u32 httpStatusCommentLengthAddr) {
	WARN_LOG(SCENET, "UNTESTED sceParseHttpStatusLine(%x, %d, %x, %x, %x, %x, %x) at %08x", headerAddr, headerLength, httpVersionMajorAddr, httpVersionMinorAddr, httpStatusCodeAddr, httpStatusCommentAddr, httpStatusCommentLengthAddr, currentMIPS->pc);
	if (!Memory::IsValidRange(headerAddr, headerLength))
		return hleLogError(SCENET, -1, "invalid arg");

	std::string headers(Memory::GetCharPointer(headerAddr), headerLength);
	// Get first line from header, line ending can be '\n' or '\r'
	std::istringstream hdr(headers);
	std::string line;
	std::getline(hdr, line, '\n');
	hdr.str(line);
	std::getline(hdr, line, '\r');
	NotifyMemInfo(MemBlockFlags::READ, headerAddr, (u32)line.size(), "ParseHttpStatusLine");
	DEBUG_LOG(SCENET, "Headers: %s", headers.c_str());

	// Split the string by pattern, based on JPCSP
	std::regex rgx("HTTP/(\\d)\\.(\\d)\\s+(\\d+)(.*)");
	std::smatch matches;

	if (!std::regex_search(line, matches, rgx))
		return hleLogError(SCENET, -1, "invalid arg"); // SCE_HTTP_ERROR_PARSE_HTTP_NOT_FOUND

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
		hleLogError(SCENET, -1, "Runtime error: %s", ex.what());
	}
	catch (const std::exception& ex) {
		hleLogError(SCENET, -1, "Error occurred: %s", ex.what()); // SCE_HTTP_ERROR_PARSE_HTTP_INVALID_VALUE
	}
	catch (...) {
		hleLogError(SCENET, -1, "Unknown error");
	}

	return 0;
}

const HLEFunction sceParseHttp [] = 
{
	{0X8077A433, &WrapI_UUUUUUU<sceParseHttpStatusLine>,    "sceParseHttpStatusLine",     'i', "xxxxxxx"},
	{0XAD7BFDEF, nullptr,                                   "sceParseHttpResponseHeader", '?', ""       },
};

void Register_sceParseHttp()
{
	RegisterModule("sceParseHttp", ARRAY_SIZE(sceParseHttp), sceParseHttp);
}

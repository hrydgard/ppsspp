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

#include "zlib.h"

#include "Common/CommonTypes.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceDeflt.h"
#include "Core/MemMap.h"

// All the decompress functions are identical with only differing window bits.
static int CommonDecompress(int windowBits, u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
	if (!Memory::IsValidAddress(OutBuffer) || !Memory::IsValidAddress(InBuffer)) {
		return hleLogError(Log::HLE, 0, "bad address");
	}

	auto crc32Addr = PSPPointer<u32_le>::Create(Crc32Addr);
	if (Crc32Addr && !crc32Addr.IsValid()) {
		return hleLogError(Log::HLE, 0, "bad crc32 address");
	}

	z_stream stream{};
	u8 *outBufferPtr = Memory::GetPointerWrite(OutBuffer);
	stream.next_in = (Bytef*)Memory::GetPointer(InBuffer);
	// We don't know the available length, just let it use as much as it wants.
	stream.avail_in = (uInt)Memory::ValidSize(InBuffer, Memory::g_MemorySize);
	stream.next_out = outBufferPtr;
	stream.avail_out = (uInt)OutBufferLength;

	int err = inflateInit2(&stream, windowBits);
	if (err != Z_OK) {
		return hleLogError(Log::HLE, 0, "inflateInit2 failed %08x", err);
	}
	err = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);

	if (err != Z_STREAM_END) {
		return hleLogError(Log::HLE, 0, "inflate failed %08x", err);
	}
	if (crc32Addr.IsValid()) {
		uLong crc = crc32(0L, Z_NULL, 0);
		*crc32Addr = crc32(crc, outBufferPtr, stream.total_out);
	}

	if (MemBlockInfoDetailed(stream.total_in, stream.total_out)) {
		char tagData[128];
		size_t tagSize = FormatMemWriteTagAt(tagData, sizeof(tagData), "sceDeflt/", InBuffer, stream.total_in);
		NotifyMemInfo(MemBlockFlags::READ, InBuffer, stream.total_in, tagData, tagSize);
		NotifyMemInfo(MemBlockFlags::WRITE, OutBuffer, stream.total_out, tagData, tagSize);
	}

	return hleLogSuccessI(Log::HLE, stream.total_out);
}

static int sceDeflateDecompress(u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
	return CommonDecompress(-MAX_WBITS, OutBuffer, OutBufferLength, InBuffer, Crc32Addr);
}

static int sceGzipDecompress(u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
	return CommonDecompress(16 + MAX_WBITS, OutBuffer, OutBufferLength, InBuffer, Crc32Addr);
}

static int sceZlibDecompress(u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
	return CommonDecompress(MAX_WBITS, OutBuffer, OutBufferLength, InBuffer, Crc32Addr);
}

const HLEFunction sceDeflt[] = {
	{0X0BA3B9CC, nullptr,                            "sceGzipGetCompressedData", '?', ""    },
	{0X106A3552, nullptr,                            "sceGzipGetName",           '?', ""    },
	{0X1B5B82BC, nullptr,                            "sceGzipIsValid",           '?', ""    },
	{0X2EE39A64, nullptr,                            "sceZlibAdler32",           '?', ""    },
	{0X44054E03, &WrapI_UIUU<sceDeflateDecompress>,  "sceDeflateDecompress",     'i', "xixp"},
	{0X6A548477, nullptr,                            "sceZlibGetCompressedData", '?', ""    },
	{0X6DBCF897, &WrapI_UIUU<sceGzipDecompress>,     "sceGzipDecompress",        'i', "xixp"},
	{0X8AA82C92, nullptr,                            "sceGzipGetInfo",           '?', ""    },
	{0XA9E4FB28, &WrapI_UIUU<sceZlibDecompress>,     "sceZlibDecompress",        'i', "xixp"},
	{0XAFE01FD3, nullptr,                            "sceZlibGetInfo",           '?', ""    },
	{0XB767F9A0, nullptr,                            "sceGzipGetComment",        '?', ""    },
	{0XE46EB986, nullptr,                            "sceZlibIsValid",           '?', ""    },
};

void Register_sceDeflt() {
	RegisterModule("sceDeflt", ARRAY_SIZE(sceDeflt), sceDeflt);
}

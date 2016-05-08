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
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceDeflt.h"
#include "Core/MemMap.h"

// All the decompress functions are identical with only differing window bits. Possibly should be one function

static int sceDeflateDecompress(u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
	DEBUG_LOG(HLE, "sceGzipDecompress(%08x, %x, %08x, %08x)", OutBuffer, OutBufferLength, InBuffer, Crc32Addr);
	int err;
	uLong crc;
	z_stream stream;
	u8 *outBufferPtr;
	u32 *crc32AddrPtr = 0;

	if (!Memory::IsValidAddress(OutBuffer) || !Memory::IsValidAddress(InBuffer)) {
		ERROR_LOG(HLE, "sceZlibDecompress: Bad address %08x %08x", OutBuffer, InBuffer);
		return 0;
	}
	if (Crc32Addr) {
		if (!Memory::IsValidAddress(Crc32Addr)) {
			ERROR_LOG(HLE, "sceZlibDecompress: Bad address %08x", Crc32Addr);
			return 0;
		}
		crc32AddrPtr = (u32 *)Memory::GetPointer(Crc32Addr);
	}
	outBufferPtr = Memory::GetPointer(OutBuffer);
	stream.next_in = (Bytef*)Memory::GetPointer(InBuffer);
	stream.avail_in = (uInt)OutBufferLength;
	stream.next_out = outBufferPtr;
	stream.avail_out = (uInt)OutBufferLength;
	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;
	err = inflateInit2(&stream, -MAX_WBITS);
	if (err != Z_OK) {
		ERROR_LOG(HLE, "sceZlibDecompress: inflateInit2 failed %08x", err);
		return 0;
	}
	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		inflateEnd(&stream);
		ERROR_LOG(HLE, "sceZlibDecompress: inflate failed %08x", err);
		return 0;
	}
	inflateEnd(&stream);
	if (crc32AddrPtr) {
		crc = crc32(0L, Z_NULL, 0);
		*crc32AddrPtr = crc32(crc, outBufferPtr, stream.total_out);
	}
	return stream.total_out;
}


static int sceGzipDecompress(u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
	DEBUG_LOG(HLE, "sceGzipDecompress(%08x, %x, %08x, %08x)", OutBuffer, OutBufferLength, InBuffer, Crc32Addr);
	int err;
	uLong crc;
	z_stream stream;
	u8 *outBufferPtr;
	u32 *crc32AddrPtr = 0;

	if (!Memory::IsValidAddress(OutBuffer) || !Memory::IsValidAddress(InBuffer)) {
		ERROR_LOG(HLE, "sceZlibDecompress: Bad address %08x %08x", OutBuffer, InBuffer);
		return 0;
	}
	if (Crc32Addr) {
		if (!Memory::IsValidAddress(Crc32Addr)) {
			ERROR_LOG(HLE, "sceZlibDecompress: Bad address %08x", Crc32Addr);
			return 0;
		}
		crc32AddrPtr = (u32 *)Memory::GetPointer(Crc32Addr);
	}
	outBufferPtr = Memory::GetPointer(OutBuffer);
	stream.next_in = (Bytef*)Memory::GetPointer(InBuffer);
	stream.avail_in = (uInt)OutBufferLength;
	stream.next_out = outBufferPtr;
	stream.avail_out = (uInt)OutBufferLength;
	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;
	err = inflateInit2(&stream, 16+MAX_WBITS);
	if (err != Z_OK) {
		ERROR_LOG(HLE, "sceZlibDecompress: inflateInit2 failed %08x", err);
		return 0;
	}
	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		inflateEnd(&stream);
		ERROR_LOG(HLE, "sceZlibDecompress: inflate failed %08x", err);
		return 0;
	}
	inflateEnd(&stream);
	if (crc32AddrPtr) {
		crc = crc32(0L, Z_NULL, 0);
		*crc32AddrPtr = crc32(crc, outBufferPtr, stream.total_out);
	}
	return stream.total_out;
}

static int sceZlibDecompress(u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
	DEBUG_LOG(HLE, "sceZlibDecompress(%08x, %x, %08x, %08x)", OutBuffer, OutBufferLength, InBuffer, Crc32Addr);
	int err;
	uLong crc;
	z_stream stream;
	u8 *outBufferPtr;
	u32 *crc32AddrPtr = 0;

	if (!Memory::IsValidAddress(OutBuffer) || !Memory::IsValidAddress(InBuffer)) {
		ERROR_LOG(HLE, "sceZlibDecompress: Bad address %08x %08x", OutBuffer, InBuffer);
		return 0;
	}
	if (Crc32Addr) {
		if (!Memory::IsValidAddress(Crc32Addr)) {
			ERROR_LOG(HLE, "sceZlibDecompress: Bad address %08x", Crc32Addr);
			return 0;
		}
		crc32AddrPtr = (u32 *)Memory::GetPointer(Crc32Addr);
	}
	outBufferPtr = Memory::GetPointer(OutBuffer);
	stream.next_in = (Bytef*)Memory::GetPointer(InBuffer);
	stream.avail_in = (uInt)OutBufferLength;
	stream.next_out = outBufferPtr;
	stream.avail_out = (uInt)OutBufferLength;
	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;
	err = inflateInit2(&stream, MAX_WBITS);
	if (err != Z_OK) {
		ERROR_LOG(HLE, "sceZlibDecompress: inflateInit failed %08x", err);
		return 0;
	}
	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		inflateEnd(&stream);
		ERROR_LOG(HLE, "sceZlibDecompress: inflate failed %08x", err);
		return 0;
	}
	inflateEnd(&stream);
	if (crc32AddrPtr) {
		crc = crc32(0L, Z_NULL, 0);
		*crc32AddrPtr = crc32(crc, outBufferPtr, stream.total_out);
	}
	return stream.total_out;
}

const HLEFunction sceDeflt[] = {
	{0X0BA3B9CC, nullptr,                            "sceGzipGetCompressedData", '?', ""    },
	{0X106A3552, nullptr,                            "sceGzipGetName",           '?', ""    },
	{0X1B5B82BC, nullptr,                            "sceGzipIsValid",           '?', ""    },
	{0X2EE39A64, nullptr,                            "sceZlibAdler32",           '?', ""    },
	{0X44054E03, &WrapI_UIUU<sceDeflateDecompress>,  "sceDeflateDecompress",     'i', "xixx"},
	{0X6A548477, nullptr,                            "sceZlibGetCompressedData", '?', ""    },
	{0X6DBCF897, &WrapI_UIUU<sceGzipDecompress>,     "sceGzipDecompress",        'i', "xixx"},
	{0X8AA82C92, nullptr,                            "sceGzipGetInfo",           '?', ""    },
	{0XA9E4FB28, &WrapI_UIUU<sceZlibDecompress>,     "sceZlibDecompress",        'i', "xixx"},
	{0XAFE01FD3, nullptr,                            "sceZlibGetInfo",           '?', ""    },
	{0XB767F9A0, nullptr,                            "sceGzipGetComment",        '?', ""    },
	{0XE46EB986, nullptr,                            "sceZlibIsValid",           '?', ""    },
};

void Register_sceDeflt() {
	RegisterModule("sceDeflt", ARRAY_SIZE(sceDeflt), sceDeflt);
}

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

#include "Globals.h"
#include "HLE.h"
#include "zlib.h"

int sceZlibDecompress(u32 OutBuffer, int OutBufferLength, u32 InBuffer, u32 Crc32Addr) {
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
	err = inflateInit(&stream);
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
	{0x0BA3B9CC, 0,	"sceGzipGetCompressedData"},
	{0x106A3552, 0,	"sceGzipGetName"},
	{0x1B5B82BC, 0,	"sceGzipIsValid"},
	{0x2EE39A64, 0, "sceZlibAdler32"},
	{0x44054E03, 0, "sceDeflateDecompress"},
	{0x6A548477, 0,	"sceZlibGetCompressedData"},
	{0x6DBCF897, 0, "sceGzipDecompress"},
	{0x8AA82C92, 0,	"sceGzipGetInfo"},
	{0xA9E4FB28, WrapI_UIUU<sceZlibDecompress>,	"sceZlibDecompress"},
	{0xAFE01FD3, 0,	"sceZlibGetInfo"},
	{0xB767F9A0, 0,	"sceGzipGetComment"},
	{0xE46EB986, 0,	"sceZlibIsValid"},
};

void Register_sceDeflt() {
	RegisterModule("sceDeflt", ARRAY_SIZE(sceDeflt), sceDeflt);
}

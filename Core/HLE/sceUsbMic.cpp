// Copyright (c) 2019- PPSSPP Project.

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

#include <mutex>

#include "base/NativeApp.h"
#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceUsbMic.h"
#include "Core/MemMapHelpers.h"

static int sceUsbMicInputBlocking(u32 size, u32 samplerate, u32 bufAddr) {
	INFO_LOG(HLE, "UNIMPL sceUsbMicInputBlocking: size: %d, samplerate: %d", size, samplerate);
	for (unsigned int i = 0; i < size; i++) {
		if (Memory::IsValidAddress(bufAddr + i)) {
			Memory::Write_U8(i & 0xFF, bufAddr + i);
		}
	}
	hleEatMicro(1000000 / samplerate * (size / 2));
	return size;
}

const HLEFunction sceUsbMic[] =
{
	{0x06128E42, nullptr,                            "sceUsbMicPollInputEnd",         '?', "" },
	{0x2E6DCDCD, &WrapI_UUU<sceUsbMicInputBlocking>, "sceUsbMicInputBlocking",        'i', "xxx" },
	{0x45310F07, nullptr,                            "sceUsbMicInputInitEx",          '?', "" },
	{0x5F7F368D, nullptr,                            "sceUsbMicInput",                '?', "" },
	{0x63400E20, nullptr,                            "sceUsbMicGetInputLength",       '?', "" },
	{0xB8E536EB, nullptr,                            "sceUsbMicInputInit",            '?', "" },
	{0xF899001C, nullptr,                            "sceUsbMicWaitInputEnd",         '?', "" },
};

void Register_sceUsbMic()
{
	RegisterModule("sceUsbMic", ARRAY_SIZE(sceUsbMic), sceUsbMic);
}

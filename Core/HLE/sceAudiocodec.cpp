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

#include "sceAudiocodec.h"
#include "Core/Reporting.h"

int sceAudiocodecInit(u32 audioCodec, int codeType) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceAudiocodecInit(%08x, %x)", audioCodec, codeType);
	return 0;
}
int sceAudiocodecDecode(u32 audioCodec, int codeType) {
	ERROR_LOG_REPORT(HLE, "UNIMPL sceAudiocodecDecode(%08x, %x)", audioCodec, codeType);
	return 0;
}

const HLEFunction sceAudiocodec[] =
{
	{0x70A703F8, WrapI_UI<sceAudiocodecDecode>, "sceAudiocodecDecode"},
	{0x5B37EB1D, WrapI_UI<sceAudiocodecInit>, "sceAudiocodecInit"},
	{0x8ACA11D5, 0, "sceAudiocodecGetInfo"},
	{0x3A20A200, 0, "sceAudiocodecGetEDRAM"},
	{0x29681260, 0, "sceAudiocodecReleaseEDRAM"},
	{0x9D3F790C, 0, "sceAudiocodeCheckNeedMem"},
};

void Register_sceAudiocodec()
{
	RegisterModule("sceAudiocodec", ARRAY_SIZE(sceAudiocodec), sceAudiocodec);
}
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

#include "HLE.h"
#include "FunctionWrappers.h"
#include "sceVaudio.h"

u32 sceVaudioOutputBlocking() {
	ERROR_LOG(HLE, "UNIMPL sceVaudioOutputBlocking(...)");
	return 0;
}

u32 sceVaudioChReserve() {
	ERROR_LOG(HLE, "UNIMPL sceVaudioChReserve(...)");
	return 0;
}

u32 sceVaudioChRelease() {
	ERROR_LOG(HLE, "UNIMPL sceVaudioChRelease(...)");
	return 0;
}

const HLEFunction sceVaudio[] = {
	{0x03b6807d, WrapU_V<sceVaudioOutputBlocking>, "sceVaudioOutputBlockingFunction"},
	{0x67585dfd, WrapU_V<sceVaudioChReserve>, "sceVaudioChReserveFunction"},
	{0x8986295e, WrapU_V<sceVaudioChRelease>, "sceVaudioChReleaseFunction"},
};

void Register_sceVaudio() {
	RegisterModule("sceVaudio",ARRAY_SIZE(sceVaudio), sceVaudio );
}

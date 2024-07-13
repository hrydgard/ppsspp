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

#ifdef SHARED_ZLIB
#include <zlib.h>
#else
#include "../ext/zlib/zlib.h"
#endif

#include "sceAdler.h"
#include "Common/Log.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"

static u32 sceAdler32(u32 adler, u32 data, u32 datalen) {
	if (!Memory::IsValidAddress(data) || !Memory::IsValidAddress(data + datalen - 1)) {
		ERROR_LOG(Log::sceMisc, "sceAdler32(adler=%08x, data=%08x, datalen=%08x) - bad address(es)", adler, data, datalen);
		return -1;
	}
	INFO_LOG(Log::sceMisc, "sceAdler32(adler=%08x, data=%08x, datalen=%08x)", adler, data, datalen);

	u8 *buf = Memory::GetPointerWriteUnchecked(data);
	u32 ret = adler32(adler, buf, datalen);

	return ret;
}

const HLEFunction sceAdler[] = {
	{0X9702EF11, &WrapU_UUU<sceAdler32>, "sceAdler32", 'x', "xxx"},
};

void Register_sceAdler() {
	RegisterModule("sceAdler", ARRAY_SIZE(sceAdler), sceAdler);
}

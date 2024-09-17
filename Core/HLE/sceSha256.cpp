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

#include "Common/Crypto/sha256.h"
#include "Common/Log.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceSha256.h"

static int sceSha256Digest(u32 data, int dataLen, u32 digestPtr) {
	if (!Memory::IsValidAddress(data) || !Memory::IsValidAddress(digestPtr) || !Memory::IsValidAddress(data + dataLen)) {
		ERROR_LOG(Log::HLE, "sceSha256Digest(data=%08x, len=%d, digest=%08x) - bad address(es)", data, dataLen, digestPtr);
		return -1;
	}
	INFO_LOG(Log::HLE, "sceSha256Digest(data=%08x, len=%d, digest=%08x)", data, dataLen, digestPtr);

	// Already checked above...
	u8 *digest = Memory::GetPointerWriteUnchecked(digestPtr);
	sha256_context ctx;
	sha256_starts(&ctx);
	sha256_update(&ctx, Memory::GetPointerWriteUnchecked(data), dataLen);
	sha256_finish(&ctx, digest);

	return 0;
}

const HLEFunction sceSha256[] =
{
	{0X318A350C, &WrapI_UIU<sceSha256Digest>,        "sceSha256Digest", 'i', "xix"},
};

void Register_sceSha256()
{
	RegisterModule("sceSha256", ARRAY_SIZE(sceSha256), sceSha256);
}

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
#include "Core/Reporting.h"
#include "Common/Crypto/md5.h"

// Not really sure where these belong - is it worth giving them their own file?
u32 sceKernelUtilsMt19937Init(u32 ctx, u32 seed) {
	DEBUG_LOG(HLE, "sceKernelUtilsMt19937Init(%08x, %08x)", ctx, seed);
	if (!Memory::IsValidAddress(ctx))
		return -1;
	void *ptr = Memory::GetPointer(ctx);
	// This is made to match the memory layout of a PSP MT structure exactly.
	// Let's just construct it in place with placement new. Elite C++ hackery FTW.
	new (ptr) MersenneTwister(seed);
	return 0;
}

u32 sceKernelUtilsMt19937UInt(u32 ctx) {
	VERBOSE_LOG(HLE, "sceKernelUtilsMt19937UInt(%08x)", ctx);
	if (!Memory::IsValidAddress(ctx))
		return -1;
	MersenneTwister *mt = (MersenneTwister *)Memory::GetPointer(ctx);
	return mt->R32();
}

// TODO: This MD5 stuff needs tests!

static md5_context md5_ctx;

int sceMd5BlockInit(u32 ctxAddr) {
	DEBUG_LOG(HLE, "sceMd5BlockInit(%08x)", ctxAddr);
	if (!Memory::IsValidAddress(ctxAddr))
		return -1;

	// TODO: Until I know how large a context is, we just go all lazy and use a global context,
	// which will work just fine unless games do several MD5 concurrently.

	md5_starts(&md5_ctx);
	return 0;
}

int sceMd5BlockUpdate(u32 ctxAddr, u32 dataPtr, u32 len) {
	DEBUG_LOG(HLE, "sceMd5BlockUpdate(%08x, %08x, %d)", ctxAddr, dataPtr, len);
	if (!Memory::IsValidAddress(ctxAddr) || !Memory::IsValidAddress(dataPtr))
		return -1;
	
	md5_update(&md5_ctx, Memory::GetPointer(dataPtr), (int)len);
	return 0;
}

int sceMd5BlockResult(u32 ctxAddr, u32 digestAddr) {
	DEBUG_LOG(HLE, "sceMd5BlockResult(%08x, %08x)", ctxAddr, digestAddr);
	if (!Memory::IsValidAddress(ctxAddr) || !Memory::IsValidAddress(digestAddr))
		return -1;

	md5_finish(&md5_ctx, Memory::GetPointer(digestAddr));
	// TODO: Should output be hex ascii?
	return 0;
}

int sceMd5Digest(u32 dataPtr, u32 len, u32 digestAddr) {
	WARN_LOG(HLE, "sceMd5Digest(%08x, %d, %08x)", dataPtr, len, digestAddr);
	
	if (!Memory::IsValidAddress(dataPtr) || !Memory::IsValidAddress(digestAddr))
		return -1;

	md5(Memory::GetPointer(dataPtr), (int)len, Memory::GetPointer(digestAddr));
	// TODO: Should output be hex ascii?
	return 0;
}

const HLEFunction sceMd5[] = {
	{0x19884A15, WrapI_U<sceMd5BlockInit>, "sceMd5BlockInit"},
	{0xA30206C2, WrapI_UUU<sceMd5BlockUpdate>, "sceMd5BlockUpdate"},
	{0x4876AFFF, WrapI_UU<sceMd5BlockResult>, "sceMd5BlockResult"},
	{0x98E31A9E, WrapI_UUU<sceMd5Digest>, "sceMd5Digest"},
};

void Register_sceMd5() {
	RegisterModule("sceMd5", ARRAY_SIZE(sceMd5), sceMd5);
}
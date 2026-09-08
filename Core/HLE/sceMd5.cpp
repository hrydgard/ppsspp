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

#include "Common/Crypto/md5.h"
#include "Common/Crypto/sha1.h"
#include "Common/Data/Random/Rng.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceMd5.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"

#ifdef USE_CRT_DBG
#undef new
#endif

// Not really sure where these belong - is it worth giving them their own file?
u32 sceKernelUtilsMt19937Init(u32 ctx, u32 seed) {
	DEBUG_LOG(Log::HLE, "sceKernelUtilsMt19937Init(%08x, %08x)", ctx, seed);
	if (!Memory::IsValidAddress(ctx))
		return -1;
	void *ptr = Memory::GetPointerWriteOrException(ctx);
	// This is made to match the memory layout of a PSP MT structure exactly.
	// Let's just construct it in place with placement new. Elite C++ hackery FTW.
	new (ptr) MersenneTwister(seed);
	return 0;
}

u32 sceKernelUtilsMt19937UInt(u32 ctx) {
	VERBOSE_LOG(Log::HLE, "sceKernelUtilsMt19937UInt(%08x)", ctx);
	if (!Memory::IsValidAddress(ctx))
		return -1;
	MersenneTwister *mt = (MersenneTwister *)Memory::GetPointerUnchecked(ctx);
	return mt->R32();
}

// The MD5 context lives in the game's own memory, exactly as it does on hardware, so two digests
// can be in flight at once. Layout confirmed against a real PSP by pspautotests hash/md5ctx:
// 96 bytes, and the word at offset 16 is never written by the kernel.
//
// SHA-1 gets the same treatment further down. Its context is the same size with the same
// bookkeeping, but note it does not stream whole blocks through buf the way MD5 does.
struct PSPMd5Context {
	u32_le h[4];
	u32_le pad;           // the kernel leaves this one alone
	u16_le usRemains;     // bytes currently held in buf
	u16_le usComputed;    // stays zero on hardware
	u64_le ullTotalLen;   // total bytes fed in so far
	u8 buf[64];
};

static void Md5ContextRead(const PSPPointer<PSPMd5Context> &ctx, md5_context *out) {
	for (int i = 0; i < 4; i++) {
		out->state[i] = ctx->h[i];
	}
	u64 total = ctx->ullTotalLen;
	out->total[0] = (u32)total;
	out->total[1] = (u32)(total >> 32);
	memcpy(out->buffer, ctx->buf, sizeof(out->buffer));
}

static void Md5ContextWrite(PSPPointer<PSPMd5Context> &ctx, const md5_context *in) {
	for (int i = 0; i < 4; i++) {
		ctx->h[i] = (u32)in->state[i];
	}
	u64 total = (u64)(u32)in->total[0] | ((u64)(u32)in->total[1] << 32);
	ctx->ullTotalLen = total;
	ctx->usRemains = (u16)(total & 0x3F);
	ctx->usComputed = 0;
	ctx.NotifyWrite("Md5Context");
}

// Hardware streams every byte through buf on its way into the digest, so after a whole-block
// update buf holds that block - not just the leftover tail. Our md5 hashes full blocks straight
// out of the caller's buffer, so reproduce what the PSP would have left behind: the data laid
// into a 64 byte window at its absolute offset in the stream.
static void Md5ContextFillBuf(PSPPointer<PSPMd5Context> &ctx, u64 totalBefore, const u8 *data, u32 len) {
	if (len == 0) {
		return;
	}
	if (len >= 64) {
		data += len - 64;
		totalBefore += len - 64;
		len = 64;
	}
	for (u32 i = 0; i < len; i++) {
		ctx->buf[(u32)((totalBefore + i) % 64)] = data[i];
	}
	ctx.NotifyWrite("Md5Context");
}

// Init touches only the state and the counters - buf and the pad word are left as they were.
static int Md5BlockInit(u32 ctxAddr) {
	auto ctx = PSPPointer<PSPMd5Context>::Create(ctxAddr);
	if (!ctx.IsValid())
		return hleLogError(Log::HLE, -1, "bad context address");
	md5_context fresh;
	ppsspp_md5_starts(&fresh);
	for (int i = 0; i < 4; i++) {
		ctx->h[i] = (u32)fresh.state[i];
	}
	ctx->usRemains = 0;
	ctx->usComputed = 0;
	ctx->ullTotalLen = 0;
	ctx.NotifyWrite("Md5Context");
	return hleLogDebug(Log::HLE, 0);
}

static int Md5BlockUpdate(u32 ctxAddr, u32 dataPtr, u32 len) {
	auto ctx = PSPPointer<PSPMd5Context>::Create(ctxAddr);
	if (!ctx.IsValid() || !Memory::IsValidRange(dataPtr, len))
		return hleLogError(Log::HLE, -1, "bad address");
	md5_context work;
	Md5ContextRead(ctx, &work);
	u64 totalBefore = ctx->ullTotalLen;
	const u8 *data = Memory::GetPointerWriteUnchecked(dataPtr);
	ppsspp_md5_update(&work, (unsigned char *)data, (int)len);
	Md5ContextWrite(ctx, &work);
	Md5ContextFillBuf(ctx, totalBefore, data, len);
	return hleLogDebug(Log::HLE, 0);
}

static int Md5BlockResult(u32 ctxAddr, u32 digestAddr) {
	auto ctx = PSPPointer<PSPMd5Context>::Create(ctxAddr);
	if (!ctx.IsValid() || !Memory::IsValidRange(digestAddr, 16))
		return hleLogError(Log::HLE, -1, "bad address");
	md5_context work;
	Md5ContextRead(ctx, &work);
	ppsspp_md5_finish(&work, Memory::GetPointerWriteUnchecked(digestAddr));
	Md5ContextWrite(ctx, &work);
	return hleLogDebug(Log::HLE, 0);
}


static int sceMd5Digest(u32 dataAddr, u32 len, u32 digestAddr) {
	DEBUG_LOG(Log::HLE, "sceMd5Digest(%08x, %d, %08x)", dataAddr, len, digestAddr);

	if (!Memory::IsValidAddress(dataAddr) || !Memory::IsValidAddress(digestAddr))
		return -1;

	ppsspp_md5(Memory::GetPointerWriteUnchecked(dataAddr), (int)len, Memory::GetPointerWriteUnchecked(digestAddr));
	return 0;
}

static int sceMd5BlockInit(u32 ctxAddr) {
	return Md5BlockInit(ctxAddr);
}

static int sceMd5BlockUpdate(u32 ctxAddr, u32 dataPtr, u32 len) {
	return Md5BlockUpdate(ctxAddr, dataPtr, len);
}

static int sceMd5BlockResult(u32 ctxAddr, u32 digestAddr) {
	return Md5BlockResult(ctxAddr, digestAddr);
}

int sceKernelUtilsMd5Digest(u32 dataAddr, int len, u32 digestAddr) {
	DEBUG_LOG(Log::HLE, "sceKernelUtilsMd5Digest(%08x, %d, %08x)", dataAddr, len, digestAddr);

	if (!Memory::IsValidAddress(dataAddr) || !Memory::IsValidAddress(digestAddr))
		return -1;

	ppsspp_md5(Memory::GetPointerWriteUnchecked(dataAddr), (int)len, Memory::GetPointerWriteUnchecked(digestAddr));
	return 0;
}

int sceKernelUtilsMd5BlockInit(u32 ctxAddr) {
	return Md5BlockInit(ctxAddr);
}

int sceKernelUtilsMd5BlockUpdate(u32 ctxAddr, u32 dataPtr, int len) {
	return Md5BlockUpdate(ctxAddr, dataPtr, (u32)len);
}

int sceKernelUtilsMd5BlockResult(u32 ctxAddr, u32 digestAddr) {
	return Md5BlockResult(ctxAddr, digestAddr);
}


// SHA-1's context, confirmed against a real PSP by pspautotests hash/sha1ctx. Same 96 bytes and
// same bookkeeping as MD5, but no pad word - and unlike MD5, a whole-block update leaves buf
// alone rather than copying the block through it, which is what our sha1 does anyway.
struct PSPSha1Context {
	u32_le h[5];
	u16_le usRemains;
	u16_le usComputed;
	u64_le ullTotalLen;
	u8 buf[64];
};

static void Sha1ContextRead(const PSPPointer<PSPSha1Context> &ctx, sha1_context *out) {
	for (int i = 0; i < 5; i++) {
		out->state[i] = ctx->h[i];
	}
	u64 total = ctx->ullTotalLen;
	out->total[0] = (u32)total;
	out->total[1] = (u32)(total >> 32);
	memcpy(out->buffer, ctx->buf, sizeof(out->buffer));
}

static void Sha1ContextWrite(PSPPointer<PSPSha1Context> &ctx, const sha1_context *in) {
	for (int i = 0; i < 5; i++) {
		ctx->h[i] = (u32)in->state[i];
	}
	u64 total = (u64)(u32)in->total[0] | ((u64)(u32)in->total[1] << 32);
	ctx->ullTotalLen = total;
	ctx->usRemains = (u16)(total & 0x3F);
	ctx->usComputed = 0;
	memcpy(ctx->buf, in->buffer, sizeof(ctx->buf));
	ctx.NotifyWrite("Sha1Context");
}

static int Sha1BlockInit(u32 ctxAddr) {
	auto ctx = PSPPointer<PSPSha1Context>::Create(ctxAddr);
	if (!ctx.IsValid())
		return hleLogError(Log::HLE, -1, "bad context address");
	sha1_context fresh;
	sha1_starts(&fresh);
	for (int i = 0; i < 5; i++) {
		ctx->h[i] = (u32)fresh.state[i];
	}
	ctx->usRemains = 0;
	ctx->usComputed = 0;
	ctx->ullTotalLen = 0;
	ctx.NotifyWrite("Sha1Context");
	return hleLogDebug(Log::HLE, 0);
}


int sceKernelUtilsSha1Digest(u32 dataAddr, int len, u32 digestAddr) {
	DEBUG_LOG(Log::HLE, "sceKernelUtilsSha1Digest(%08x, %d, %08x)", dataAddr, len, digestAddr);

	if (!Memory::IsValidAddress(dataAddr) || !Memory::IsValidAddress(digestAddr))
		return -1;

	sha1(Memory::GetPointerWriteUnchecked(dataAddr), (int)len, Memory::GetPointerWriteUnchecked(digestAddr));
	return 0;
}

int sceKernelUtilsSha1BlockInit(u32 ctxAddr) {
	return Sha1BlockInit(ctxAddr);
}

int sceKernelUtilsSha1BlockUpdate(u32 ctxAddr, u32 dataAddr, int len) {
	auto ctx = PSPPointer<PSPSha1Context>::Create(ctxAddr);
	if (!ctx.IsValid() || !Memory::IsValidRange(dataAddr, len))
		return hleLogError(Log::HLE, -1, "bad address");
	sha1_context work;
	Sha1ContextRead(ctx, &work);
	sha1_update(&work, Memory::GetPointerWriteUnchecked(dataAddr), (int)len);
	Sha1ContextWrite(ctx, &work);
	return hleLogDebug(Log::HLE, 0);
}

int sceKernelUtilsSha1BlockResult(u32 ctxAddr, u32 digestAddr) {
	auto ctx = PSPPointer<PSPSha1Context>::Create(ctxAddr);
	if (!ctx.IsValid() || !Memory::IsValidRange(digestAddr, 20))
		return hleLogError(Log::HLE, -1, "bad address");
	sha1_context work;
	Sha1ContextRead(ctx, &work);
	sha1_finish(&work, Memory::GetPointerWriteUnchecked(digestAddr));
	Sha1ContextWrite(ctx, &work);
	return hleLogDebug(Log::HLE, 0);
}


const HLEFunction sceMd5[] = {
	{0X19884A15, &WrapI_U<sceMd5BlockInit>,          "sceMd5BlockInit",   'i', "x"  },
	{0XA30206C2, &WrapI_UUU<sceMd5BlockUpdate>,      "sceMd5BlockUpdate", 'i', "xxx"},
	{0X4876AFFF, &WrapI_UU<sceMd5BlockResult>,       "sceMd5BlockResult", 'i', "xx" },
	{0X98E31A9E, &WrapI_UUU<sceMd5Digest>,           "sceMd5Digest",      'i', "xxx"},
};

void Register_sceMd5() {
	RegisterHLEModule("sceMd5", ARRAY_SIZE(sceMd5), sceMd5);
}

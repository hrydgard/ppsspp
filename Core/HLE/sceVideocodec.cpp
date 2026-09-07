// Copyright (c) 2026- PPSSPP Project.

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

// sceVideocodec - the H.264 decoding interface the Media Engine exposes.
//
// This exists so that flash0:/kd/mpeg.prx can be run in place of our sceMpeg HLE: mpeg.prx needs
// only sceVideocodec, sceMpegbase and sceAudiocodec from us, and the other two we already have.
// The point is to have a reference to compare the HLE against, so it aims to behave like the
// hardware rather than to be the fastest way to get pixels on screen.
//
// Behaviour cross-checked against JPCSP, whose description of the buffer layout was established
// by looking at sceMpegBaseYCrCbCopy output on a real PSP.

#include <algorithm>
#include <map>
#include <vector>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceVideocodec.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/Util/BlockAllocator.h"
#include "Core/HW/AvcDecoder.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"

// The context the caller hands us is 96 bytes. The offsets below are what mpeg.prx actually
// reads and writes; anything not listed here it doesn't look at.
enum {
	CTX_MAGIC = 0,          // 0x05100601, same marker sceAudiocodec's context carries
	CTX_VERSION = 4,        // GetVersion writes 0x78 here
	CTX_STATUS = 8,
	CTX_MEM = 12,
	CTX_OUT_INFO = 16,      // pointer to the 108-byte result descriptor
	CTX_EDRAM = 20,
	CTX_EDRAM_SIZE = 24,
	CTX_AU_DATA = 36,       // the access unit to decode
	CTX_AU_SIZE = 40,
	CTX_YUV_STRUCT = 44,    // type 0: pointer to the eight output buffers
	CTX_EDRAM_RAW = 92,
};

// Fields of the descriptor at CTX_OUT_INFO that mpeg.prx reads back.
enum {
	OUT_DATA = 0,
	OUT_SIZE = 4,
	OUT_UNK8 = 8,
	OUT_UNK12 = 12,
	OUT_CONSUMED = 44,
	OUT_WIDTH = 48,
	OUT_HEIGHT = 52,
	OUT_FRAME_READY = 60,   // 2 when a frame came out, 1 when it didn't
	OUT_UNK64 = 64,
	OUT_UNK72 = 72,
	OUT_TIMESTAMP = 76,
	OUT_FPS = 80,
	OUT_BUFFER_Y = 84,
	OUT_BUFFER_CR = 88,
	OUT_BUFFER_CB = 92,
	OUT_WIDTH_Y = 96,
	OUT_WIDTH_CR = 100,
	OUT_WIDTH_CB = 104,
};

// A game can have more than one of these open at once - Silent Hill Origins runs two, one that
// owns the EDRAM and one that does the decoding - so everything here is per context.
struct VideocodecCtx {
	AvcDecoder *decoder = nullptr;
	int type = 0;
	int frameCount = 0;
	// What sceVideocodecGetEDRAM handed out, as an address in g_meRam.
	u32 edram = 0;
	// The frame buffers the ME reported back, also in g_meRam - see PublishFrameBuffers.
	u32 frameBuffers = 0;
	u32 frameBuffersSize = 0;
	int frameBufferWidth = 0;
	int frameBufferHeight = 0;
};

static std::map<u32, VideocodecCtx> g_videocodecCtxs;

// The Media Engine's own 2MB of embedded DRAM, modelled as memory of ours.
//
// The CPU cannot address it. mpeg.prx asks for a block with sceVideocodecGetEDRAM, keeps the value
// and hands it back, and never dereferences it; the frame buffers the ME reports back live in here
// too, which is why sceVideocodecSetMemory is given a frame size rather than a buffer - 480, 272
// and a count of 2 for a full-screen movie, with nowhere for the caller to say where to put them.
//
// So none of it may come out of the game's partitions: taking it from user memory would push a
// game's own allocations around, and from kernel memory would spend memory a real PSP never does.
// The addresses handed out are offsets into g_meRam, based well outside anything PSP RAM maps so
// that a stray dereference faults where it happens instead of quietly reading the game's memory.
// Being outside PSP RAM, the contents aren't in the memory a savestate captures either, so the
// block and its allocator go in __VideocodecDoState.
static const u32 ME_EDRAM_BASE = 0xC0000000;
static const u32 ME_EDRAM_SIZE = 2 * 1024 * 1024;
static std::vector<u8> g_meRam;
static BlockAllocator g_meAlloc(64);

// The 2MB is only committed once something asks for a piece of it, so a game that never plays a
// video pays nothing for this and its savestates don't carry it.
static void MEEnsureRam() {
	if (g_meRam.size() != ME_EDRAM_SIZE) {
		g_meRam.assign(ME_EDRAM_SIZE, 0);
		g_meAlloc.Init(ME_EDRAM_BASE, ME_EDRAM_SIZE, false);
	}
}

u8 *VideocodecMEPointer(u32 addr, u32 size) {
	if (addr < ME_EDRAM_BASE || size > ME_EDRAM_SIZE || g_meRam.size() != ME_EDRAM_SIZE) {
		return nullptr;
	}
	const u32 offset = addr - ME_EDRAM_BASE;
	if (offset > ME_EDRAM_SIZE - size) {
		return nullptr;
	}
	return g_meRam.data() + offset;
}

static void FreeContext(VideocodecCtx &ctx) {
	delete ctx.decoder;
	ctx.decoder = nullptr;
	if (ctx.edram) {
		g_meAlloc.Free(ctx.edram);
		ctx.edram = 0;
	}
	if (ctx.frameBuffers) {
		g_meAlloc.Free(ctx.frameBuffers);
		ctx.frameBuffers = 0;
	}
}

// freeMemory is false when loading a savestate: the state carries its own g_meAlloc, so the blocks
// these contexts were holding belong to a world that no longer exists and freeing them would be
// freeing someone else's memory.
static void ClearContexts(bool freeMemory) {
	for (auto &[addr, ctx] : g_videocodecCtxs) {
		if (freeMemory) {
			FreeContext(ctx);
		} else {
			delete ctx.decoder;
			ctx.decoder = nullptr;
		}
	}
	g_videocodecCtxs.clear();
}

void __VideocodecInit() {
	// Nothing to free: a boot starts with a fresh allocator.
	g_videocodecCtxs.clear();
	g_meRam.clear();
	g_meRam.shrink_to_fit();
	g_meAlloc.Shutdown();
}

void __VideocodecShutdown() {
	ClearContexts(true);
	g_meRam.clear();
	g_meRam.shrink_to_fit();
	g_meAlloc.Shutdown();
}

void __VideocodecDoState(PointerWrap &p) {
	auto s = p.Section("sceVideocodec", 0, 1);
	if (!s) {
		return;
	}

	// The decoders themselves aren't serializable - a savestate resumes with fresh ones, which
	// costs at most the frames up to the next keyframe. The frame buffer allocations do have to
	// come back, or we'd lose track of memory the restored allocator still has handed out.
	int count = (int)g_videocodecCtxs.size();
	Do(p, count);
	if (p.mode == p.MODE_READ) {
		ClearContexts(false);
		for (int i = 0; i < count; i++) {
			u32 addr = 0;
			VideocodecCtx ctx;
			Do(p, addr);
			Do(p, ctx.type);
			Do(p, ctx.frameCount);
			Do(p, ctx.edram);
			Do(p, ctx.frameBuffers);
			Do(p, ctx.frameBuffersSize);
			Do(p, ctx.frameBufferWidth);
			Do(p, ctx.frameBufferHeight);
			g_videocodecCtxs[addr] = ctx;
		}
	} else {
		for (auto &[addr, ctx] : g_videocodecCtxs) {
			u32 a = addr;
			Do(p, a);
			Do(p, ctx.type);
			Do(p, ctx.frameCount);
			Do(p, ctx.edram);
			Do(p, ctx.frameBuffers);
			Do(p, ctx.frameBuffersSize);
			Do(p, ctx.frameBufferWidth);
			Do(p, ctx.frameBufferHeight);
		}
	}

	// The Media Engine's memory and who holds what of it. Empty until a video plays, and then it
	// is the one copy - the contexts above only carry addresses into it.
	Do(p, g_meRam);
	g_meAlloc.DoState(p);
}

// The descriptor mpeg.prx passes in is empty: on hardware the ME owns the frame buffers, and
// reports where it put them. So allocate them here and fill the descriptor in the shape
// sceMpegBaseCscAvc expects - dimensions in macroblocks, then the eight buffer addresses.
static bool PublishFrameBuffers(VideocodecCtx &vctx, u32 structAddr, int width, int height, u32 buffers[8]) {
	const int lumaLeft = ((width + 16) >> 5) * (height >> 1) * 16;
	const int lumaRight = (width >> 5) * (height >> 1) * 16;
	const int sizes[8] = {
		lumaLeft, lumaRight, lumaLeft, lumaRight,
		lumaLeft >> 1, lumaLeft >> 1, lumaRight >> 1, lumaRight >> 1,
	};

	u32 total = 0;
	for (int i = 0; i < 8; i++) {
		total += (sizes[i] + 63) & ~63;
	}
	if (total == 0) {
		return false;
	}

	if (vctx.frameBuffers && (width != vctx.frameBufferWidth || height != vctx.frameBufferHeight)) {
		g_meAlloc.Free(vctx.frameBuffers);
		vctx.frameBuffers = 0;
	}
	if (!vctx.frameBuffers) {
		MEEnsureRam();
		u32 size = total;
		vctx.frameBuffers = g_meAlloc.Alloc(size, false, "VideocodecFrame");
		if (vctx.frameBuffers == (u32)-1) {
			vctx.frameBuffers = 0;
			ERROR_LOG(Log::ME, "sceVideocodec: no room in ME memory for %d bytes of frame buffers", total);
			return false;
		}
		vctx.frameBuffersSize = total;
		vctx.frameBufferWidth = width;
		vctx.frameBufferHeight = height;
		INFO_LOG(Log::ME, "sceVideocodec: %d bytes of frame buffers at %08x for %dx%d",
			total, vctx.frameBuffers, width, height);
	}

	u32 addr = vctx.frameBuffers;
	for (int i = 0; i < 8; i++) {
		buffers[i] = addr;
		addr += (sizes[i] + 63) & ~63;
	}

	if (!Memory::IsValidRange(structAddr, 48)) {
		return false;
	}
	Memory::WriteUnchecked_U32(height >> 4, structAddr + 0);   // macroblocks
	Memory::WriteUnchecked_U32(width >> 4, structAddr + 4);
	for (int i = 0; i < 8; i++) {
		Memory::WriteUnchecked_U32(buffers[i], structAddr + 16 + i * 4);
	}
	return true;
}

bool VideocodecGetFrameBuffers(u32 firstBuffer, u32 buffers[8]) {
	// sceMpegbase only has the first of the eight addresses, so find whose allocation it is.
	const VideocodecCtx *found = nullptr;
	for (const auto &[addr, ctx] : g_videocodecCtxs) {
		if (ctx.frameBuffers && ctx.frameBuffers == firstBuffer) {
			found = &ctx;
			break;
		}
	}
	if (!found) {
		return false;
	}
	const int width = found->frameBufferWidth, height = found->frameBufferHeight;
	const int lumaLeft = ((width + 16) >> 5) * (height >> 1) * 16;
	const int lumaRight = (width >> 5) * (height >> 1) * 16;
	const int sizes[8] = {
		lumaLeft, lumaRight, lumaLeft, lumaRight,
		lumaLeft >> 1, lumaLeft >> 1, lumaRight >> 1, lumaRight >> 1,
	};
	u32 addr = found->frameBuffers;
	for (int i = 0; i < 8; i++) {
		buffers[i] = addr;
		addr += (sizes[i] + 63) & ~63;
	}
	return true;
}

// Writes the decoded frame into the eight buffers the hardware uses. The image is in 32-pixel
// vertical bands split into two 16-pixel halves, and which buffer a row lands in depends on
// whether it is even or odd. This is the exact inverse of ReadTiledYCbCr in sceMpeg.cpp, which
// is what reads it back out - see the comment there for the full layout.
static void WriteTiledYCbCr(const u32 *buffers, const AvcDecoder &dec, int width, int height) {
	const int width2 = width >> 1;
	const int height2 = height >> 1;

	const u8 *srcY = dec.Plane(0);
	const u8 *srcCb = dec.Plane(1);
	const u8 *srcCr = dec.Plane(2);
	const int strideY = dec.Stride(0);
	const int strideCb = dec.Stride(1);
	const int strideCr = dec.Stride(2);
	if (!srcY || !srcCb || !srcCr) {
		return;
	}

	const int lumaSizeLeft = ((width + 16) >> 5) * (height >> 1) * 16;
	const int lumaSizeRight = (width >> 5) * (height >> 1) * 16;
	const int ySize[4] = { lumaSizeLeft, lumaSizeRight, lumaSizeLeft, lumaSizeRight };
	const int cSize[4] = { lumaSizeLeft >> 1, lumaSizeLeft >> 1, lumaSizeRight >> 1, lumaSizeRight >> 1 };

	for (int b = 0; b < 4; b++) {
		if (ySize[b] <= 0) {
			continue;
		}
		u8 *dst = VideocodecMEPointer(buffers[b], ySize[b]);
		if (!dst) {
			continue;
		}
		const int xOffset = (b & 1) ? 16 : 0;
		const int yStart = (b >> 1) ? 1 : 0;
		int j = 0;
		for (int bandX = xOffset; bandX < width; bandX += 32) {
			const int run = std::min(16, width - bandX);
			for (int row = yStart; row < height; row += 2, j += 16) {
				if (run <= 0 || j + run > ySize[b]) {
					continue;
				}
				memcpy(dst + j, srcY + (size_t)row * strideY + bandX, run);
			}
		}
	}

	for (int b = 0; b < 4; b++) {
		if (cSize[b] <= 0) {
			continue;
		}
		u8 *dst = VideocodecMEPointer(buffers[4 + b], cSize[b]);
		if (!dst) {
			continue;
		}
		const int xOffset = (b >> 1) ? 8 : 0;
		const int yStart = (b & 1) ? 1 : 0;
		int j = 0;
		for (int bandX = xOffset; bandX < width2; bandX += 16) {
			for (int row = yStart; row < height2; row += 2) {
				for (int k = 0; k < 8; k++, j += 2) {
					const int x = bandX + k;
					if (x >= width2 || j + 1 >= cSize[b]) {
						continue;
					}
					dst[j] = srcCb[(size_t)row * strideCb + x];
					dst[j + 1] = srcCr[(size_t)row * strideCr + x];
				}
			}
		}
	}
}

static int sceVideocodecOpen(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	Memory::WriteUnchecked_U32(0x05100601, ctxAddr + CTX_MAGIC);
	if (!AvcDecoder::IsAvailable()) {
		return hleLogError(Log::ME, -1, "built without ffmpeg, can't decode video");
	}
	g_videocodecCtxs[ctxAddr].type = type;
	return hleLogInfo(Log::ME, 0, "type %d", type);
}

static int sceVideocodecInit(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	Memory::WriteUnchecked_U32(Memory::ReadUnchecked_U32(ctxAddr + CTX_EDRAM) + 8, ctxAddr + CTX_MEM);
	VideocodecCtx &vctx = g_videocodecCtxs[ctxAddr];
	delete vctx.decoder;
	vctx.decoder = new AvcDecoder();
	vctx.frameCount = 0;
	vctx.type = type;
	return hleLogInfo(Log::ME, 0, "type %d", type);
}

// See g_meRam for why this doesn't come out of the game's memory. The firmware keeps the block in
// the caller's context and nowhere else, so we do too - see ppsspp-re, modules/sceVideocodec.
static int sceVideocodecGetEDRAM(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	// The firmware refuses rather than replacing one it already handed out, and a game that asks
	// twice would otherwise leave the first block with nothing pointing at it.
	if (Memory::ReadUnchecked_U32(ctxAddr + CTX_EDRAM_RAW) != 0) {
		return hleLogError(Log::ME, SCE_MPEG_ERROR_AVC_INVALID_VALUE, "context already has EDRAM");
	}
	// Rounding as the firmware does it - the OR really is an OR, so every size ends in 0x3F.
	u32 size = (Memory::ReadUnchecked_U32(ctxAddr + CTX_EDRAM_SIZE) + 63) | 0x3F;
	MEEnsureRam();
	const u32 addr = g_meAlloc.Alloc(size, false, "VideocodecEDRAM");
	if (addr == (u32)-1) {
		return hleLogError(Log::ME, SCE_MPEG_ERROR_AVC_INVALID_VALUE, "no room in ME memory for %u bytes", size);
	}
	g_videocodecCtxs[ctxAddr].edram = addr;
	// Both fields as hardware fills them: the raw value and the 64-byte-aligned one. The allocator
	// works in 64-byte grains, so the two only differ in what they mean, not in value.
	Memory::WriteUnchecked_U32(addr, ctxAddr + CTX_EDRAM);
	Memory::WriteUnchecked_U32(addr, ctxAddr + CTX_EDRAM_RAW);
	return hleLogInfo(Log::ME, 0, "%u bytes at %08x in ME memory", size, addr);
}

static int sceVideocodecReleaseEDRAM(u32 ctxAddr) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	// Whether this context has one is recorded in the context struct, as on hardware.
	const u32 token = Memory::ReadUnchecked_U32(ctxAddr + CTX_EDRAM_RAW);
	if (!token) {
		return hleLogError(Log::ME, SCE_MPEG_ERROR_AVC_INVALID_VALUE, "context has no EDRAM");
	}
	auto it = g_videocodecCtxs.find(ctxAddr);
	if (it != g_videocodecCtxs.end() && it->second.edram) {
		g_meAlloc.Free(it->second.edram);
		it->second.edram = 0;
	}
	Memory::WriteUnchecked_U32(0, ctxAddr + CTX_EDRAM);
	Memory::WriteUnchecked_U32(0, ctxAddr + CTX_EDRAM_RAW);
	return hleLogInfo(Log::ME, 0, "released %08x", token);
}

static int sceVideocodecDecode(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	if (type != 0 && type != 1) {
		return hleLogError(Log::ME, -1, "unknown type %d", type);
	}
	// Only Open and Init create contexts. Keying off whatever address Decode is handed would let
	// a game that never opens one accumulate decoders that nothing ever deletes.
	auto ctxIter = g_videocodecCtxs.find(ctxAddr);
	if (ctxIter == g_videocodecCtxs.end()) {
		return hleLogError(Log::ME, -1, "decode on a context that was never opened");
	}
	VideocodecCtx &vctx = ctxIter->second;
	if (!vctx.decoder) {
		vctx.decoder = new AvcDecoder();
	}

	const u32 auAddr = Memory::ReadUnchecked_U32(ctxAddr + CTX_AU_DATA);
	const int auSize = (int)Memory::ReadUnchecked_U32(ctxAddr + CTX_AU_SIZE);
	const u32 outAddr = Memory::ReadUnchecked_U32(ctxAddr + CTX_OUT_INFO);
	Memory::WriteUnchecked_U32(0, ctxAddr + CTX_STATUS);

	if (!Memory::IsValidRange(outAddr, 108)) {
		return hleLogError(Log::ME, -1, "bad output descriptor");
	}

	// The access unit address mpeg.prx passes is in Media Engine space, which we can't read -
	// on hardware sceMpegBasePESpacketCopy DMA'd the data there. That copy is ours, so use what
	// it gathered instead, and fall back to main memory for any caller that points at it
	// directly.
	bool gotFrame = false;
	const u8 *au = nullptr;
	int auBytes = 0;
	if (auSize > 0 && Memory::IsValidRange(auAddr, auSize)) {
		au = Memory::GetTypedPointerRange<u8>(auAddr, auSize);
		auBytes = auSize;
	} else {
		// Ask for the payload copied to this exact address - the same call carries audio too.
		const std::vector<u8> *pes = MpegBaseGetPESPacket(auAddr);
		if (pes && !pes->empty()) {
			au = pes->data();
			auBytes = (int)pes->size();
		}
	}
	if (au && auBytes > 0) {
		gotFrame = vctx.decoder->Decode(au, auBytes);
	}

	const int width = gotFrame ? vctx.decoder->Width() : 0;
	const int height = gotFrame ? vctx.decoder->Height() : 0;

	auto out32 = [outAddr](int offset, u32 value) {
		Memory::WriteUnchecked_U32(value, outAddr + offset);
	};

	// Only the type 1 path fills the descriptor in. For type 0 the YCbCr descriptor sits just
	// 0x40 bytes after this one - mpeg.prx allocates them adjacently - so writing the type 1
	// fields here scribbles over the buffer addresses the colour conversion is about to read.
	if (type == 0) {
		out32(8, width);
		out32(12, height);
		out32(28, 1);
		out32(32, gotFrame ? 1 : 0);   // images decoded - mpeg.prx won't convert without this
		out32(36, gotFrame ? 0 : 1);
	} else {
		out32(OUT_DATA, auAddr);
		out32(OUT_SIZE, auSize);
		out32(OUT_UNK12, 0x40);
		out32(OUT_CONSUMED, auSize);
		out32(OUT_WIDTH, width);
		out32(OUT_HEIGHT, height);
		out32(OUT_FRAME_READY, gotFrame ? 2 : 1);
		out32(OUT_UNK64, 1);
		out32(OUT_UNK72, (u32)-1);
		out32(OUT_TIMESTAMP, vctx.frameCount * 0x64);
		out32(OUT_FPS, 2997);
	}

	if (gotFrame) {
		vctx.frameCount++;
		if (type == 0) {
			const u32 yuvStructAddr = Memory::ReadUnchecked_U32(ctxAddr + CTX_YUV_STRUCT);
			if (Memory::IsValidRange(yuvStructAddr, 8 * 4)) {
				u32 buffers[8];
				if (PublishFrameBuffers(vctx, yuvStructAddr, width, height, buffers)) {
					WriteTiledYCbCr(buffers, *vctx.decoder, width, height);
				}
			} else {
				WARN_LOG(Log::ME, "sceVideocodecDecode: type 0 without a usable buffer list");
			}
		} else {
			// Type 1 hands back plain planar YUV, so just point at the decoder's own planes -
			// nothing in the descriptor is read until the caller copies from them.
			out32(OUT_WIDTH_Y, width);
			out32(OUT_WIDTH_CR, width / 2);
			out32(OUT_WIDTH_CB, width / 2);
		}
	}

	return hleLogDebug(Log::ME, 0, "type %d, %d bytes -> %s %dx%d",
		type, auBytes, gotFrame ? "frame" : "no frame yet", width, height);
}

static int sceVideocodecStop(u32 ctxAddr, int type) {
	auto it = g_videocodecCtxs.find(ctxAddr);
	if (it != g_videocodecCtxs.end() && it->second.decoder) {
		it->second.decoder->Flush();
	}
	return hleLogInfo(Log::ME, 0);
}

static int sceVideocodecDelete(u32 ctxAddr, int type) {
	auto it = g_videocodecCtxs.find(ctxAddr);
	if (it != g_videocodecCtxs.end()) {
		FreeContext(it->second);
		g_videocodecCtxs.erase(it);
	}
	return hleLogInfo(Log::ME, 0);
}

static int sceVideocodecGetVersion(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	// The value a real PSP returns, read with JpcspTrace.
	Memory::WriteUnchecked_U32(0x78, ctxAddr + CTX_VERSION);
	return hleLogInfo(Log::ME, 0);
}

static int sceVideocodecGetSEI(u32 ctxAddr, int type) {
	return hleLogWarning(Log::ME, 0, "UNIMPL");
}

static int sceVideocodecScanHeader(u32 ctxAddr, int type) {
	return hleLogWarning(Log::ME, 0, "UNIMPL");
}

static int sceVideocodecGetFrameCrop(u32 ctxAddr, int type) {
	return hleLogWarning(Log::ME, 0, "UNIMPL");
}

static int sceVideocodecSetMemory(u32 ctxAddr, int type) {
	return hleLogDebug(Log::ME, 0);
}

static int sceVideocodec_893B32B1(u32 ctxAddr, int type) {
	return hleLogWarning(Log::ME, 0, "UNIMPL");
}

static int sceVideocodec_D95C24D5(u32 ctxAddr, int type) {
	return hleLogWarning(Log::ME, 0, "UNIMPL");
}

const HLEFunction sceVideocodec[] = {
	{0XC01EC829, &WrapI_UI<sceVideocodecOpen>,          "sceVideocodecOpen",          'i', "xi"},
	{0X2D31F5B1, &WrapI_UI<sceVideocodecGetEDRAM>,      "sceVideocodecGetEDRAM",      'i', "xi"},
	{0X17099F0A, &WrapI_UI<sceVideocodecInit>,          "sceVideocodecInit",          'i', "xi"},
	{0XDBA273FA, &WrapI_UI<sceVideocodecDecode>,        "sceVideocodecDecode",        'i', "xi"},
	{0X4F160BF4, &WrapI_U<sceVideocodecReleaseEDRAM>,   "sceVideocodecReleaseEDRAM",  'i', "x" },
	{0X745A7B7A, &WrapI_UI<sceVideocodecSetMemory>,     "sceVideocodecSetMemory",     'i', "xi"},
	{0X2F385E7F, &WrapI_UI<sceVideocodecScanHeader>,    "sceVideocodecScanHeader",    'i', "xi"},
	{0X307E6E1C, &WrapI_UI<sceVideocodecDelete>,        "sceVideocodecDelete",        'i', "xi"},
	{0XA2F0564E, &WrapI_UI<sceVideocodecStop>,          "sceVideocodecStop",          'i', "xi"},
	{0X17CF7D2C, &WrapI_UI<sceVideocodecGetFrameCrop>,  "sceVideocodecGetFrameCrop",  'i', "xi"},
	{0X26927D19, &WrapI_UI<sceVideocodecGetVersion>,    "sceVideocodecGetVersion",    'i', "xi"},
	{0X627B7D42, &WrapI_UI<sceVideocodecGetSEI>,        "sceVideocodecGetSEI",        'i', "xi"},
	{0X893B32B1, &WrapI_UI<sceVideocodec_893B32B1>,     "sceVideocodec_893B32B1",     'i', "xi"},
	{0XD95C24D5, &WrapI_UI<sceVideocodec_D95C24D5>,     "sceVideocodec_D95C24D5",     'i', "xi"},
};

void Register_sceVideocodec() {
	RegisterHLEModule("sceVideocodec", ARRAY_SIZE(sceVideocodec), sceVideocodec);
}

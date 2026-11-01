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
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/System.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceVideocodec.h"
#include "Core/Util/BlockAllocator.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/HLE/scePower.h"
#include "Core/HW/AvcDecoder.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
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
	CTX_SEQUENCE_INFO = 48, // 0x64 bytes the ME fills from the stream's parameter sets
	CTX_PICTURE_INFO = 56,  // optional 0x540 bytes for per-picture records (mpeg_vsh.prx's modes 3-6)
	CTX_SEI = 80,           // where sceVideocodecGetSEI writes
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
	// Which of the MAX_PICTURES frame stores the next picture goes into. Not saved: after a load the
	// pictures already handed out are gone either way.
	int nextFrameStore = 0;
};

static std::map<u32, VideocodecCtx> g_videocodecCtxs;

// The Media Engine's own memory, modelled as an address space rather than a pool of blocks.
//
// mpeg.prx bounds-checks the ones it uses against 0x3FFFFF (4MB), and we use all of that. A real
// PSP's ME has less, but the frame buffers we hand out here are bigger than the decoder's own: four
// 720x480 pictures (UMD Video) take 2MB. DMA:s can arrive in multiple pieces so this has to be
// a "real" separate address space, so a subsequent larger read will work.
static const u32 ME_MEM_SIZE = 4 * 1024 * 1024;
// What we hand out ourselves lives above the first megabyte, out of the way of the addresses mpeg.prx
// picks for itself, which have all been well down in it.
static const u32 ME_ALLOC_BASE = 1024 * 1024;
static std::vector<u8> g_meRam;
static BlockAllocator g_meAlloc(64);

// Only committed once something asks for a piece of it, so a game that never plays a video pays
// nothing for this and its savestates don't carry it.
static void MEEnsureRam() {
	if (g_meRam.size() != ME_MEM_SIZE) {
		g_meRam.assign(ME_MEM_SIZE, 0);
		g_meAlloc.Init(ME_ALLOC_BASE, ME_MEM_SIZE - ME_ALLOC_BASE, false);
	}
}

// When the Media Engine finishes the last job it was given. Not serialized: after a load it's
// simply free.
static s64 g_meBusyUntilUs;

int MEScheduleJob(int us) {
	const s64 now = CoreTiming::GetGlobalTimeUs();
	const s64 start = std::max(now, g_meBusyUntilUs);
	g_meBusyUntilUs = start + us;
	return (int)(g_meBusyUntilUs - now);
}

bool MEIsValidRange(u32 addr, u32 size) {
	return g_meRam.size() == ME_MEM_SIZE && addr < ME_MEM_SIZE && size <= ME_MEM_SIZE - addr;
}

u8 *MEGetPointerRange(u32 addr, u32 size) {
	return MEIsValidRange(addr, size) ? g_meRam.data() + addr : nullptr;
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
	// The decoders have to be deleted; the ME blocks they hold don't need freeing individually,
	// since the allocator is emptied right below.
	ClearContexts(false);
	g_meBusyUntilUs = 0;
	g_meRam.clear();
	g_meRam.shrink_to_fit();
	g_meAlloc.Shutdown();
}

void __VideocodecShutdown() {
	ClearContexts(true);
}

void __VideocodecDoState(PointerWrap &p) {
	if (p.mode == p.MODE_READ) {
		// Not serialized, and the clock it was measured against just moved (maybe backwards).
		g_meBusyUntilUs = 0;
	}

	auto s = p.Section("sceVideocodec", 0, 1);
	if (!s) {
		if (p.mode == p.MODE_READ) {
			// A state from before this module. Don't keep the contexts and ME memory of the session
			// before the load.
			ClearContexts(false);
			g_meRam.clear();
			g_meRam.shrink_to_fit();
			g_meAlloc.Shutdown();
		}
		return;
	}

	// The decoders themselves aren't serializable - a savestate resumes with fresh ones, which
	// costs at most the frames up to the next keyframe. The frame buffer allocations do have to
	// come back, or we'd lose track of memory the restored allocator still has handed out.
	//
	// If we in the future directly integrate with a h.264 decoder, it might be actually possible
	// to serialize the internal states. But 100% accurate savestates during cutscene playback are
	// not really that important.
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
			Do(p, ctx.frameBuffers);
			Do(p, ctx.frameBuffersSize);
			Do(p, ctx.frameBufferWidth);
			Do(p, ctx.frameBufferHeight);
			Do(p, ctx.edram);
			g_videocodecCtxs[addr] = std::move(ctx);
		}
	} else {
		for (auto &[addr, ctx] : g_videocodecCtxs) {
			u32 a = addr;
			Do(p, a);
			Do(p, ctx.type);
			Do(p, ctx.frameCount);
			Do(p, ctx.frameBuffers);
			Do(p, ctx.frameBuffersSize);
			Do(p, ctx.frameBufferWidth);
			Do(p, ctx.frameBufferHeight);
			Do(p, ctx.edram);
		}
	}

	// The Media Engine's memory and who holds what of it. Empty until a video plays, and then it
	// is the one copy - the contexts above only carry addresses into it.
	Do(p, g_meRam);
	g_meAlloc.DoState(p);
	if (p.mode == p.MODE_READ && !g_meRam.empty() && g_meRam.size() < ME_MEM_SIZE) {
		// From when the ME had 2MB. Its allocations stay where they are, and its allocator keeps
		// the old upper bound.
		g_meRam.resize(ME_MEM_SIZE, 0);
	}
}

u32 VideocodecFrameBufferLayout(int width, int height, int sizes[8], u32 offsets[8]) {
	// buffer0/2 take the odd band out when the width isn't a multiple of 32.
	const int lumaLeft = ((width + 16) >> 5) * (height >> 1) * 16;
	const int lumaRight = (width >> 5) * (height >> 1) * 16;
	// Chroma is paired like luma (left/right of a band, then even/odd rows), which is what
	// sceMpegBaseYCrCbCopy's flags assume: bit 0 selects buffers 0,1,4,5 and bit 1 selects 2,3,6,7.
	// The sizes have to match that, or a copy writes the wrong count into a buffer someone else
	// sized.
	const int local[8] = {
		lumaLeft, lumaRight, lumaLeft, lumaRight,
		lumaLeft >> 1, lumaRight >> 1, lumaLeft >> 1, lumaRight >> 1,
	};
	u32 total = 0;
	for (int i = 0; i < 8; i++) {
		if (sizes) {
			sizes[i] = local[i];
		}
		if (offsets) {
			offsets[i] = total;
		}
		total += (local[i] + 63) & ~63;
	}
	return total;
}

// How many pictures one call can hand back. The decoder holds three back at most, so a call can
// complete them and its own; mpeg.prx reads one descriptor per picture, 0x2C bytes apart.
static const int MAX_PICTURES = 4;
static const u32 PICTURE_DESCRIPTOR_SIZE = 0x2C;

// The descriptor mpeg.prx passes in is empty: on hardware the ME owns the frame buffers and
// reports where it put them. So allocate them here and fill in the eight buffer addresses, for
// the slot'th picture of the call.
static bool PublishFrameBuffers(VideocodecCtx &vctx, u32 structAddr, int width, int height, int slot, u32 buffers[8]) {
	u32 offsets[8];
	const u32 total = VideocodecFrameBufferLayout(width, height, nullptr, offsets);
	if (total == 0 || slot < 0 || slot >= MAX_PICTURES) {
		return false;
	}

	// A savestate from before frame buffers had a slot per picture carries a block with room for one.
	if (vctx.frameBuffers && (width != vctx.frameBufferWidth || height != vctx.frameBufferHeight ||
		vctx.frameBuffersSize < total * MAX_PICTURES)) {
		g_meAlloc.Free(vctx.frameBuffers);
		vctx.frameBuffers = 0;
	}
	if (!vctx.frameBuffers) {
		MEEnsureRam();
		u32 size = total * MAX_PICTURES;
		vctx.frameBuffers = g_meAlloc.Alloc(size, false, "VideocodecFrame");
		if (vctx.frameBuffers == (u32)-1) {
			vctx.frameBuffers = 0;
			ERROR_LOG(Log::ME, "sceVideocodec: no room in ME memory for %d bytes of frame buffers", total * MAX_PICTURES);
			return false;
		}
		vctx.frameBuffersSize = total * MAX_PICTURES;
		vctx.frameBufferWidth = width;
		vctx.frameBufferHeight = height;
		INFO_LOG(Log::ME, "sceVideocodec: %d bytes of frame buffers at %08x for %dx%d",
			total * MAX_PICTURES, vctx.frameBuffers, width, height);
	}

	for (int i = 0; i < 8; i++) {
		buffers[i] = vctx.frameBuffers + vctx.nextFrameStore * total + offsets[i];
	}
	structAddr += slot * PICTURE_DESCRIPTOR_SIZE;

	// mpeg.prx reads eight buffer addresses off the front of this structure (`lw` at 0x00..0x1C,
	// verified in 1.3 at 08805698 and 1.8 at 08805898) and takes the frame dimensions from its own
	// context. So write only the addresses; anything else at the front lands in slots 0 and 1,
	// which sceMpegBaseYCrCbCopy then DMAs to.
	if (!Memory::IsValidRange(structAddr, 8 * 4)) {
		return false;
	}
	for (int i = 0; i < 8; i++) {
		Memory::WriteUnchecked_U32(buffers[i], structAddr + i * 4);
	}
	return true;
}

// The ME's SEI record (0xA4 bytes), which sceVideocodecGetSEI hands back for the last access unit
// decoded and the picture records below carry for each picture as it comes out (pspautotests
// video/mpeg/avcdetail). Read off that test's stream: +0x18 counts up by 2 per access unit from
// each buffering period and +0x1C stays 2, which is cpb_removal_delay and dpb_output_delay; +0x00
// is 7 for the stream's IDR picture and 2 for the rest; a buffering period sets +0x64 and puts
// 90000 (its initial_cpb_removal_delay?) at +0x08 and +0x10, which the parser doesn't give us.
// +0x20 is 0 for the test's frames, and the UMD Video player reads it as the pic_struct.
// +0x21, +0x24 and +0x28 are copied from the test.
static const u32 SEI_RECORD_SIZE = 0xA4;

static void WriteSeiRecord(u32 addr, const AvcTiming &timing) {
	Memory::Memset(addr, 0, SEI_RECORD_SIZE, "VideocodecSei");
	if (!timing.picTiming) {
		return;
	}
	Memory::WriteUnchecked_U32(timing.keyFrame ? 7 : 2, addr + 0x00);
	Memory::WriteUnchecked_U32(timing.cpbRemovalDelay, addr + 0x18);
	Memory::WriteUnchecked_U32(timing.dpbOutputDelay, addr + 0x1C);
	Memory::WriteUnchecked_U32(0x100 | (timing.picStruct & 0xFF), addr + 0x20);
	Memory::WriteUnchecked_U32(0x00030100, addr + 0x24);
	Memory::WriteUnchecked_U32(1, addr + 0x28);
	Memory::WriteUnchecked_U32(timing.bufferingPeriod ? 1 : 0, addr + 0x64);
}

// Per-picture records, which the VSH's UMD Video player reads and games' mpeg.prx doesn't. Given a
// buffer at CTX_PICTURE_INFO, the ME points a picture descriptor's +0x24 and +0x28 at two 0xA4-byte
// records for it, 0x148 bytes per picture; without one both stay 0 (pspautotests
// video/mpeg/avcdetail). The first is the picture's SEI record; the second stayed zero there, for a
// stream of frames. For a picture coded as two fields, the UMD Video player (video_main_plugin.prx,
// 08843338) takes pic_struct 1 or 2 from the first record to mean a field pair, and shows the top
// field first when the first record's output time (cpb_removal_delay + dpb_output_delay) is earlier
// than the second's. So a pair gets one record per field, the top field's first. That's inferred
// from the player, not tested on hardware. With the second record left empty, the player showed
// the top field first, and a bottom-field-first disc (Family Guy, The Dark Knight) ran each pair
// backwards in time.
static void PublishPictureInfo(u32 ctxAddr, u32 yuvStructAddr, int slot, const AvcPicture &pic) {
	const u32 descAddr = yuvStructAddr + slot * PICTURE_DESCRIPTOR_SIZE;
	const u32 records = Memory::ReadUnchecked_U32(ctxAddr + CTX_PICTURE_INFO);
	if (records == 0 || !Memory::IsValidRange(records + slot * 2 * SEI_RECORD_SIZE, 2 * SEI_RECORD_SIZE) ||
		!Memory::IsValidRange(descAddr, PICTURE_DESCRIPTOR_SIZE)) {
		return;
	}
	const u32 record = records + slot * 2 * SEI_RECORD_SIZE;
	if (pic.fieldPair) {
		const bool topFirst = pic.timing.picStruct == 1;
		WriteSeiRecord(record, topFirst ? pic.timing : pic.secondField);
		WriteSeiRecord(record + SEI_RECORD_SIZE, topFirst ? pic.secondField : pic.timing);
	} else {
		WriteSeiRecord(record, pic.timing);
		Memory::Memset(record + SEI_RECORD_SIZE, 0, SEI_RECORD_SIZE, "VideocodecPictureInfo");
	}
	Memory::WriteUnchecked_U32(record, descAddr + 0x24);
	Memory::WriteUnchecked_U32(record + SEI_RECORD_SIZE, descAddr + 0x28);
}

// The ME fills this from the stream's parameter sets. Only the VUI timing is filled in here: the
// PSP's streams count time at 240000 ticks a second, so num_units_in_tick is 4004 for 29.97 fps
// (pspautotests video/mpeg/avcdetail) and 5005 for 23.976, which the UMD Video player checks for.
static void PublishSequenceInfo(const VideocodecCtx &vctx, u32 ctxAddr) {
	const u32 info = Memory::ReadUnchecked_U32(ctxAddr + CTX_SEQUENCE_INFO);
	int num, den;
	if (info == 0 || !Memory::IsValidRange(info, 0x64) || !vctx.decoder->FrameRate(&num, &den)) {
		return;
	}
	const u32 timeScale = 240000;
	Memory::WriteUnchecked_U32((u32)((u64)timeScale * den / (2 * (u64)num)), info + 0x20);
	Memory::WriteUnchecked_U32(timeScale, info + 0x24);
}

void VideocodecGetCtxInfo(std::vector<VideocodecCtxInfo> *infos) {
	infos->clear();
	for (const auto &[addr, ctx] : g_videocodecCtxs) {
		VideocodecCtxInfo info;
		info.ctxAddr = addr;
		info.type = ctx.type;
		info.hasDecoder = ctx.decoder != nullptr;
		info.frameCount = ctx.frameCount;
		// Hardware keeps the token in the context struct, so that's where we read it back from too.
		info.edramToken = ctx.edram;
		info.edramSize = ctx.edram ? g_meAlloc.GetBlockSizeFromAddress(ctx.edram) : 0;
		info.frameBuffers = ctx.frameBuffers;
		info.frameBuffersSize = ctx.frameBuffersSize;
		info.width = ctx.frameBufferWidth;
		info.height = ctx.frameBufferHeight;
		infos->push_back(info);
	}
}

bool VideocodecGetFrameBuffers(u32 firstBuffer, u32 buffers[8], int *width, int *height) {
	// sceMpegbase only has the first of the eight addresses, so find whose allocation it is.
	const VideocodecCtx *found = nullptr;
	u32 base = 0;
	for (const auto &[addr, ctx] : g_videocodecCtxs) {
		if (!ctx.frameBuffers || firstBuffer < ctx.frameBuffers || firstBuffer >= ctx.frameBuffers + ctx.frameBuffersSize) {
			continue;
		}
		// One of the slots of the pictures a call handed back.
		const u32 total = VideocodecFrameBufferLayout(ctx.frameBufferWidth, ctx.frameBufferHeight, nullptr, nullptr);
		if (total != 0 && (firstBuffer - ctx.frameBuffers) % total == 0) {
			found = &ctx;
			base = firstBuffer;
			break;
		}
	}
	if (!found) {
		return false;
	}
	u32 offsets[8];
	VideocodecFrameBufferLayout(found->frameBufferWidth, found->frameBufferHeight, nullptr, offsets);
	if (width) {
		*width = found->frameBufferWidth;
	}
	if (height) {
		*height = found->frameBufferHeight;
	}
	for (int i = 0; i < 8; i++) {
		buffers[i] = base + offsets[i];
	}
	return true;
}

// Writes the decoded frame into the eight buffers the hardware uses. The image is in 32-pixel
// vertical bands split into two 16-pixel halves, and which buffer a row lands in depends on
// whether it is even or odd. This is the exact inverse of ReadTiledYCbCr in sceMpeg.cpp, which
// is what reads it back out - see the comment there for the full layout.
static void WriteTiledYCbCr(const u32 *buffers, const AvcPicture &pic) {
	const int width = pic.width;
	const int height = pic.height;
	const int width2 = width >> 1;
	const int height2 = height >> 1;

	const u8 *srcY = pic.planes[0];
	const u8 *srcCb = pic.planes[1];
	const u8 *srcCr = pic.planes[2];
	const int strideY = pic.strides[0];
	const int strideCb = pic.strides[1];
	const int strideCr = pic.strides[2];
	if (!srcY || !srcCb || !srcCr) {
		return;
	}

	int sizes[8];
	VideocodecFrameBufferLayout(width, height, sizes, nullptr);
	const int *ySize = sizes;
	const int *cSize = sizes + 4;

	for (int b = 0; b < 4; b++) {
		if (ySize[b] <= 0) {
			continue;
		}
		u8 *dst = MEGetPointerRange(buffers[b], ySize[b]);
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
		u8 *dst = MEGetPointerRange(buffers[4 + b], cSize[b]);
		if (!dst) {
			continue;
		}
		const int xOffset = (b & 1) ? 8 : 0;
		const int yStart = (b >> 1) ? 1 : 0;
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

// Every call here that reaches the ME blocks while it answers. Measured at 222MHz in pspautotests
// video/mp4/mp4timing: Open, GetEDRAM, GetVersion and ReleaseEDRAM called directly, the rest through
// mpeg.prx's thin wrappers. sceMpegCreate (Open, Init, GetVersion, SetMemory) took 26.9-28.0ms,
// nearly all of it Init and SetMemory, which are charged together to Init here. sceMpegDelete took
// 21.2ms, and 35.9ms in a run that had stopped the decoder twice first. An sceMpegAvcDecodeStop with
// nothing held back took 132us (with three pictures to hand over, 4.9ms, the rest being their output).
static const int openUs = 96;
static const int getEdramUs = 146;
static const int getVersionUs = 94;
static const int releaseEdramUs = 66;
static const int initUs = 26600;
static const int stopUs = 132;
static const int deleteUs = 21000;

static int MECall(int result, const char *reason, int us) {
	return hleDelayResult(result, reason, MEScheduleJob(PowerScaleFromDefaultClock(us)));
}

// Init and Delete take tens of milliseconds for the caller, but they don't hold the ME for that
// long: queueing them there stalled the SAS mix behind them, and with it Jak and Daxter's sound
// threads, whose last wake to video_sound_thread then came after the game had deleted it
// (NOT_DORMANT, then the orphan reads a freed context). On hardware SAS keeps mixing through them:
// pspautotests audio/timing/meshare saw ~1.3ms SAS calls throughout a 39ms sceMpegCreate and a 32ms
// sceMpegDelete, the longest 2.2ms.
static int CallerWait(int result, const char *reason, int us) {
	return hleDelayResult(result, reason, PowerScaleFromDefaultClock(us));
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
	if (type == 0) {
		// The EDRAM the decoder needs, which mpeg.prx reads back and passes to GetEDRAM.
		Memory::WriteUnchecked_U32(0x3c2c, ctxAddr + CTX_EDRAM_SIZE);
	}
	return MECall(hleLogInfo(Log::ME, 0, "type %d", type), "videocodec open", openUs);
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
	return CallerWait(hleLogInfo(Log::ME, 0, "type %d", type), "videocodec init", initUs);
}

// See g_meRam for why this doesn't come out of the game's memory.
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
	return MECall(hleLogInfo(Log::ME, 0, "%u bytes at %08x in ME memory", size, addr), "videocodec getedram", getEdramUs);
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
	return MECall(hleLogInfo(Log::ME, 0, "released %08x", token), "videocodec releaseedram", releaseEdramUs);
}

// Type 0: copies the pictures the decoder handed back into the frame buffers the ME reports,
// and says how many in the output descriptor. mpeg.prx converts the first one in
// sceMpegAvcDecode, and all of them in sceMpegAvcDecodeStop (08805130 in 6.61's mpeg.prx reads a
// descriptor per picture, 0x2C bytes apart). Returns the number published.
static int PublishPictures(VideocodecCtx &vctx, u32 ctxAddr, u32 outAddr) {
	const std::vector<AvcPicture> &pictures = vctx.decoder->Pictures();
	const u32 yuvStructAddr = Memory::ReadUnchecked_U32(ctxAddr + CTX_YUV_STRUCT);
	int published = 0;
	if (!pictures.empty() && !Memory::IsValidRange(yuvStructAddr, 8 * 4)) {
		WARN_LOG(Log::ME, "sceVideocodec: type 0 without a usable buffer list");
	} else {
		for (const AvcPicture &pic : pictures) {
			u32 buffers[8];
			if (published >= MAX_PICTURES || !PublishFrameBuffers(vctx, yuvStructAddr, pic.width, pic.height, published, buffers)) {
				break;
			}
			// Each picture gets the next frame store, as the ME rotates through its own. A caller can
			// still be reading the last one: the VSH's UMD Video player converts a picture in strips
			// spread over its next few frames.
			vctx.nextFrameStore = (vctx.nextFrameStore + 1) % MAX_PICTURES;
			WriteTiledYCbCr(buffers, pic);
			// 1 for a picture decoding can start from (the stream's IDR in pspautotests
			// video/mpeg/avcdetail), which mpeg.prx passes back from sceMpegAvcDecodeDetail. The UMD
			// Video player drops pictures after starting or seeking until it sees one.
			Memory::WriteUnchecked_U32(pic.timing.keyFrame ? 1 : 0, yuvStructAddr + published * PICTURE_DESCRIPTOR_SIZE + 0x20);
			PublishPictureInfo(ctxAddr, yuvStructAddr, published, pic);
			published++;
		}
	}
	if (published > 0) {
		PublishSequenceInfo(vctx, ctxAddr);
		Memory::WriteUnchecked_U32(pictures[0].width, outAddr + 8);
		Memory::WriteUnchecked_U32(pictures[0].height, outAddr + 12);
		Memory::WriteUnchecked_U32(1, outAddr + 28);
	}
	// The count of images - mpeg.prx won't convert without it.
	Memory::WriteUnchecked_U32(published, outAddr + 32);
	Memory::WriteUnchecked_U32(published ? 0 : 1, outAddr + 36);
	vctx.frameCount += published;
	return published;
}

// The decode takes real time on the ME: about 3.4ms for a 480x272 frame on a PSP, measured as
// sceMpegAvcDecode (5.8ms) less sceMpegAvcCsc alone (2.4ms), in pspautotests
// video/mpeg/playertiming. Movie players that present every decoded frame after a single
// vblank wait rely on decode, colour conversion and blit adding up to more than a vblank.
// It takes as long when the decoder holds the picture back: the first sceMpegAvcDecode calls of
// a stream, which return none, took 4.2-5.4ms in video/mp4/mp4timing. Until a picture has told
// us the size, assume full screen.
static int VideocodecDecodeResult(const VideocodecCtx &vctx, int type, int auBytes, int pictures) {
	int delayUs = 0;
	if (auBytes > 0) {
		int w = vctx.frameBufferWidth > 0 ? vctx.frameBufferWidth : vctx.decoder->Width();
		int h = vctx.frameBufferHeight > 0 ? vctx.frameBufferHeight : vctx.decoder->Height();
		if (w <= 0 || h <= 0) {
			w = 480;
			h = 272;
		}
		delayUs = MEScheduleJob(PowerScaleFromDefaultClock((int)(3400LL * w * h / (480 * 272))));
	}

	if (delayUs > 0) {
		return hleDelayResult(hleLogDebug(Log::ME, 0, "type %d, %d bytes -> %d picture(s)",
			type, auBytes, pictures), "videocodec decode", delayUs);
	}
	return hleLogDebug(Log::ME, 0, "type %d, %d bytes -> %d picture(s)", type, auBytes, pictures);
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

	// The access unit mpeg.prx points at is normally in Media Engine memory, which the DMA in
	// sceMpegBasePESpacketCopy has already filled in, so read it straight from there - auSize is
	// the length it says it wrote. A caller pointing at main memory instead is served from there.
	bool gotFrame = false;
	const u8 *au = nullptr;
	int auBytes = 0;
	if (auSize > 0) {
		au = MEGetPointerRange(auAddr, auSize);
		if (!au && Memory::IsValidRange(auAddr, auSize)) {
			au = Memory::GetTypedPointerRange<u8>(auAddr, auSize);
		}
		auBytes = au ? auSize : 0;
		if (!au) {
			WARN_LOG(Log::ME, "sceVideocodecDecode: %d bytes at %08x is neither game nor ME memory",
				auSize, auAddr);
		}
	}
	auto out32 = [outAddr](int offset, u32 value) {
		Memory::WriteUnchecked_U32(value, outAddr + offset);
	};

	if (type == 0) {
		// Pictures come out when the PSP's decoder would hand them back - see AvcDecoder.cpp.
		// Only the type 1 path fills the rest of the descriptor in: for type 0 the YCbCr
		// descriptor sits just 0x40 bytes after this one - mpeg.prx allocates them adjacently - so
		// writing the type 1 fields here scribbles over the buffer addresses the colour conversion
		// is about to read. Nor is a picture announced until the buffers holding it have been
		// published: saying "one image decoded" and then failing to allocate would have mpeg.prx
		// convert from whatever the descriptor pointed at last.
		int pictures = 0;
		if (au && auBytes > 0) {
			if (vctx.decoder->DecodePictures(au, auBytes) < 0) {
				// The PSP refuses streams with more than three reference frames, and mpeg.prx
				// passes this on from sceMpegAvcDecode (pspautotests video/mp4/avchold).
				return hleLogWarning(Log::ME, SCE_MPEG_ERROR_AVC_DECODE_FATAL, "stream the PSP's decoder can't play");
			}
			pictures = PublishPictures(vctx, ctxAddr, outAddr);
		} else {
			out32(32, 0);
			out32(36, 1);
		}
		return VideocodecDecodeResult(vctx, type, auBytes, pictures);
	}

	if (au && auBytes > 0) {
		gotFrame = vctx.decoder->Decode(au, auBytes);
	}

	const int width = gotFrame ? vctx.decoder->Width() : 0;
	const int height = gotFrame ? vctx.decoder->Height() : 0;

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

	if (gotFrame) {
		vctx.frameCount++;
		// Type 1 hands back plain planar YUV, so just point at the decoder's own planes - nothing
		// in the descriptor is read until the caller copies from them.
		out32(OUT_WIDTH_Y, width);
		out32(OUT_WIDTH_CR, width / 2);
		out32(OUT_WIDTH_CB, width / 2);
	}
	return VideocodecDecodeResult(vctx, type, auBytes, gotFrame ? 1 : 0);
}

// Returning immediately from Stop or Delete is not correct, because a game can be relying on a
// thread of its own getting to run once more before it tears things down. Jak and Daxter deletes
// its video_sound_thread straight after sceVideocodecDelete without waiting for it to exit, and
// with no time passing here the audio thread never gets to deliver the wake that would let it exit
// - so the delete fails with NOT_DORMANT and the thread lives on, reading a context the game has
// already freed. One audio mix block is 64 samples at 44100Hz, about 1.45ms, which Delete's
// measured time is well above.

// Stopping hands back the pictures the decoder was holding, once: on a PSP the first
// sceMpegAvcDecodeStop after a stream reports them, all at once, and the next ones none
// (pspautotests video/mpeg/decodeend, video/mp4/avchold).
static int sceVideocodecStop(u32 ctxAddr, int type) {
	auto it = g_videocodecCtxs.find(ctxAddr);
	if (it != g_videocodecCtxs.end() && it->second.decoder) {
		VideocodecCtx &vctx = it->second;
		const u32 outAddr = Memory::IsValidRange(ctxAddr, 96) ? Memory::ReadUnchecked_U32(ctxAddr + CTX_OUT_INFO) : 0;
		if (type == 0 && Memory::IsValidRange(outAddr, 108)) {
			vctx.decoder->StopPictures();
			PublishPictures(vctx, ctxAddr, outAddr);
		}
		vctx.decoder->Flush();
	}
	return MECall(hleLogInfo(Log::ME, 0), "videocodec stop", stopUs);
}

static int sceVideocodecDelete(u32 ctxAddr, int type) {
	auto it = g_videocodecCtxs.find(ctxAddr);
	if (it != g_videocodecCtxs.end()) {
		FreeContext(it->second);
		g_videocodecCtxs.erase(it);
	}
	return CallerWait(hleLogInfo(Log::ME, 0), "videocodec delete", deleteUs);
}

static int sceVideocodecGetVersion(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	// The value a real PSP returns, read with JpcspTrace.
	Memory::WriteUnchecked_U32(0x78, ctxAddr + CTX_VERSION);
	return MECall(hleLogInfo(Log::ME, 0), "videocodec getversion", getVersionUs);
}

// Writes the SEI record of the last access unit decoded to the buffer at CTX_SEI. mpeg_vsh.prx's
// sceMpeg_6F314410 asks for it, and the UMD Video player times pictures from its +0x1C.
static int sceVideocodecGetSEI(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 96)) {
		return hleLogError(Log::ME, -1, "bad context pointer");
	}
	const u32 sei = Memory::ReadUnchecked_U32(ctxAddr + CTX_SEI);
	if (!Memory::IsValidRange(sei, SEI_RECORD_SIZE)) {
		return hleLogError(Log::ME, -1, "bad SEI buffer %08x", sei);
	}
	auto it = g_videocodecCtxs.find(ctxAddr);
	AvcTiming timing;
	if (it != g_videocodecCtxs.end() && it->second.decoder) {
		timing = it->second.decoder->LastTiming();
	}
	WriteSeiRecord(sei, timing);
	return hleLogDebug(Log::ME, 0, "cpb_removal_delay %d, dpb_output_delay %d", timing.cpbRemovalDelay, timing.dpbOutputDelay);
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

// 0x893B32B1. mpeg.prx runs this from sceMpegCreate, only in mode 1 (the path where the game reads
// raw YCbCr out rather than letting sceMpegbase convert to RGB). On hardware it writes back the
// 0x28-byte output descriptor at ctx+0x10 and issues ME video op 0x6D68B223 to configure the codec.
// Nothing we run reaches it, so it stays a stub - implement it if a mode-1 game needs it.
static int sceVideocodecSetMode(u32 ctxAddr, int type) {
	return hleLogWarning(Log::ME, 0, "UNIMPL");
}

// 0xD95C24D5. Hands a decoded frame back to the caller as three planes, which is what
// sceMpegAvcCopyYCbCr is built on - the videocodec-level counterpart of sceMpegBaseYCrCbCopy.
// Games that want the raw YCbCr rather than letting sceMpegbase convert to RGB use this and nothing
// else (Monster Hunter Portable 3rd calls it once per decoded frame and never calls a Csc).
//
// mpeg.prx builds the descriptor on its own stack (AvcCopyDeeper in mpeg.prx 2.60) and avcodec.prx
// reads it back at 0x800015c4, which is where the layout below comes from:
//
//   0x00  width in pixels        0x04  height in pixels
//   0x0c  the eight frame buffers, but ordered 0,2,4,6 then 1,3,5,7 rather than 0..7
//   0x2c  destination Y, then Cb at +width*height and Cr a further width*height/4 on - so the
//         three planes are contiguous, and the caller gets ordinary planar YUV420.
static int sceVideocodecCopyYCbCr(u32 ctxAddr, int type) {
	if (!Memory::IsValidRange(ctxAddr, 0x38)) {
		return hleLogError(Log::ME, -1, "bad descriptor pointer");
	}
	const int width = (int)Memory::ReadUnchecked_U32(ctxAddr + 0x00);
	const int height = (int)Memory::ReadUnchecked_U32(ctxAddr + 0x04);
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return hleLogError(Log::ME, -1, "unreasonable frame size %dx%d", width, height);
	}

	// Back into the order the rest of our code uses: four luma, then four chroma.
	static const int fromDescriptor[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };
	u32 buffers[8]{};
	for (int i = 0; i < 8; i++) {
		buffers[fromDescriptor[i]] = Memory::ReadUnchecked_U32(ctxAddr + 0x0c + i * 4);
	}

	const u8 *luma, *cb, *cr;
	if (!ReadTiledYCbCr(buffers, width, height, &luma, &cb, &cr)) {
		return hleLogError(Log::ME, -1, "YCbCr buffers not readable");
	}

	const u32 dst[3] = {
		Memory::ReadUnchecked_U32(ctxAddr + 0x2c),
		Memory::ReadUnchecked_U32(ctxAddr + 0x30),
		Memory::ReadUnchecked_U32(ctxAddr + 0x34),
	};
	const u8 *planes[3] = { luma, cb, cr };
	const u32 planeSizes[3] = {
		(u32)(width * height),
		(u32)((width >> 1) * (height >> 1)),
		(u32)((width >> 1) * (height >> 1)),
	};
	for (int i = 0; i < 3; i++) {
		if (!Memory::IsValidRange(dst[i], planeSizes[i])) {
			return hleLogError(Log::ME, -1, "plane %d (%08x, %d bytes) not writable", i, dst[i], planeSizes[i]);
		}
		Memory::MemcpyUnchecked(dst[i], planes[i], planeSizes[i]);
	}
	return hleLogDebug(Log::ME, 0, "%dx%d -> %08x %08x %08x", width, height, dst[0], dst[1], dst[2]);
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
	{0X893B32B1, &WrapI_UI<sceVideocodecSetMode>,       "sceVideocodecSetMode",       'i', "xi"},
	{0XD95C24D5, &WrapI_UI<sceVideocodecCopyYCbCr>,     "sceVideocodecCopyYCbCr",     'i', "xi"},
};

void Register_sceVideocodec() {
	RegisterHLEModule("sceVideocodec", ARRAY_SIZE(sceVideocodec), sceVideocodec);
}

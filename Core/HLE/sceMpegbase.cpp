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

// sceMpegbase - the Media Engine's colour space conversion, and the DMA that feeds it.
//
// mpeg.prx drives these directly, so they have to be real for the firmware module to run in place
// of our sceMpeg HLE.

#include <map>
#include <vector>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/Swap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/HLE/sceVideocodec.h"
#include "Core/MemMapHelpers.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

// The PES payloads gathered by sceMpegBasePESpacketCopy, keyed by the destination each was
// copied to. It carries audio as well as video - the destination is what tells them apart - so
// sceVideocodec has to ask for the one matching the address it was handed.
static std::map<u32, std::vector<u8>> g_pesPackets;
// Set by sceMpegBaseCscInit / sceMpegBaseCscSetPixelMode, and used when the caller passes 0.
static int g_mpegBaseBufferWidth = 512;
static int g_mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;

void __MpegBaseInit() {
	// None of this survives a boot on hardware.
	g_pesPackets.clear();
	g_mpegBaseBufferWidth = 512;
	g_mpegBasePixelMode = GE_CMODE_32BIT_ABGR8888;
}

void __MpegBaseDoState(PointerWrap &p) {
	auto s = p.Section("sceMpegbase", 0, 1);
	if (!s) {
		return;
	}
	// The pixel mode decides both the colour packing and the bytes per pixel of the output, and a
	// game sets it once per movie rather than per frame - so without it here, a state resumed
	// mid-movie converted at the default until the next sceMpegBaseCscInit, which may never come.
	Do(p, g_mpegBaseBufferWidth);
	Do(p, g_mpegBasePixelMode);
	// A state can land between the copy and the decode that consumes it.
	Do(p, g_pesPackets);
}

// p pointing to a SceMpegLLI structure consists of video frame blocks.
static u32 sceMpegBasePESpacketCopy(u32 p)
{
	int nBlocks = 0;
	auto lli = PSPPointer<SceMpegLLI>::Create(p);
	while (lli.IsValid()) {
		nBlocks++;
		if (lli->Next == 0) {
			// Last block
			break;
		}
		++lli;
	}
	MpegSetPmpVideoSource(p, nBlocks);

	// On hardware this is the DMA that moves the PES payload into the Media Engine's own memory,
	// after which mpeg.prx hands sceVideocodecDecode an ME-side address we have no way to read.
	// Since the copy is ours, gather the blocks here instead and let sceVideocodec decode from
	// this - see MpegBaseTakePESPacket.
	lli = PSPPointer<SceMpegLLI>::Create(p);
	u32 dest = 0;
	std::vector<u8> gathered;
	for (int i = 0; i < nBlocks && lli.IsValid(); i++) {
		if (i == 0) {
			dest = lli->pDst;
		}
		// The list is game-supplied, so check the span validity before taking a pointer to it.
		const u8 *src = (lli->iSize > 0 && Memory::IsValidRange(lli->pSrc, lli->iSize))
			? Memory::GetTypedPointerRange<u8>(lli->pSrc, lli->iSize) : nullptr;
		if (src) {
			gathered.insert(gathered.end(), src, src + lli->iSize);
			// Audio payloads land in main memory, and mpeg.prx hands that same address to
			// sceAudiocodecDecode as its input, so for those the copy has to really happen.
			// Video goes to a Media Engine address that isn't mapped for us: the gather above
			// is what stands in for it there.
			if (Memory::IsValidRange(lli->pDst, lli->iSize)) {
				Memory::MemcpyUnchecked(lli->pDst, src, lli->iSize);
			}
		}
		++lli;
	}
	if (dest != 0) {
		g_pesPackets[dest] = std::move(gathered);
	}

	DEBUG_LOG(Log::Mpeg, "sceMpegBasePESpacketCopy(%08x), %d block(s) -> %08x, %d bytes",
		p, nBlocks, dest, dest ? (int)g_pesPackets[dest].size() : 0);
	return 0;
}

std::vector<u8> MpegBaseTakePESPacket(u32 dest) {
	auto it = g_pesPackets.find(dest);
	if (it == g_pesPackets.end()) {
		return std::vector<u8>();
	}
	std::vector<u8> packet = std::move(it->second);
	g_pesPackets.erase(it);
	return packet;
}


// --- sceMpegbase colour conversion ---------------------------------------------------------
//
// mpeg.prx hands the ME's decoded output to these to be converted to RGB. The descriptor it
// passes is 48 bytes, which matches the range mpegbase.prx bounds-checks before using it.
//
// mpeg.prx builds this on its own stack before each call (1.3 at 08805698, 1.8 at 08805898, the
// same shape) from the eight buffer addresses sceVideocodec published and the dimensions from its
// own context. mpegbase.prx reads the dimensions at 0x00/0x04 and the eight buffers at 0x10..0x2c.
//
// The buffers live either in Media Engine memory (a freshly decoded frame) or in the game's
// memory (once sceMpegBaseYCrCbCopy has moved one out).
struct SceMp4AvcCscStruct {
	s32_le height;        // 0x00  in macroblocks
	s32_le width;         // 0x04
	s32_le unk08;         // 0x08
	s32_le unk0c;         // 0x0c
	u32_le buffer[8];     // 0x10  four luma, then four chroma
};
static_assert(sizeof(SceMp4AvcCscStruct) == 0x30);


// A frame buffer is either in the Media Engine's memory or, after a copy, in the game's.
static const u8 *MpegBaseFramePointer(u32 addr, int size) {
	if (const u8 *me = VideocodecMEPointer(addr, size)) {
		return me;
	}
	return Memory::GetTypedPointerRange<u8>(addr, size);
}

// The ME doesn't write plain planar YCbCr. The layout below was established by analysing
// sceMpegBaseYCrCbCopy output on a real PSP (documented in JPCSP's sceVideocodec), and it uses
// all eight buffers in the descriptor:
//
//   The image is divided into vertical bands 32 pixels wide, each split into two 16-pixel halves.
//   Luma is one byte per pixel, and which buffer a row lands in depends on whether it is even or
//   odd:
//     buffer0: left half,  even rows        buffer1: right half, even rows
//     buffer2: left half,  odd rows         buffer3: right half, odd rows
//   Within a buffer, rows are stored 16 bytes at a time, band by band, top to bottom.
//
//   Chroma is one (Cb,Cr) byte pair per 2x2 pixel square, so in chroma coordinates the bands are
//   16 wide with 8-pixel halves, and the same even/odd split applies:
//     buffer4: left half,  even chroma rows  buffer5: right half, even chroma rows
//     buffer6: left half,  odd chroma rows   buffer7: right half, odd chroma rows
//
// Untangling it into plain planes costs one pass per frame, which keeps the conversion below
// readable and is not where the time goes.
bool ReadTiledYCbCr(const u32 *buffers, int width, int height,
	std::vector<u8> &luma, std::vector<u8> &cb, std::vector<u8> &cr) {
	const int width2 = width >> 1;
	const int height2 = height >> 1;

	int sizes[8];
	VideocodecFrameBufferLayout(width, height, sizes, nullptr);
	const int *ySize = sizes;
	const int *cSize = sizes + 4;

	const u8 *y[4] = {};
	const u8 *c[4] = {};
	for (int i = 0; i < 4; i++) {
		if (ySize[i] > 0) {
			y[i] = MpegBaseFramePointer(buffers[i], ySize[i]);
			if (!y[i]) {
				return false;
			}
		}
		if (cSize[i] > 0) {
			c[i] = MpegBaseFramePointer(buffers[4 + i], cSize[i]);
			if (!c[i]) {
				return false;
			}
		}
	}

	luma.assign((size_t)width * height, 0);
	cb.assign((size_t)width2 * height2, 128);
	cr.assign((size_t)width2 * height2, 128);

	// Luma: four buffers, keyed by (left/right half of the band, even/odd row).
	for (int b = 0; b < 4; b++) {
		if (!y[b]) {
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
				memcpy(&luma[(size_t)row * width + bandX], y[b] + j, run);
			}
		}
	}

	// Chroma: same shape in half-resolution coordinates, with interleaved Cb/Cr pairs.
	for (int b = 0; b < 4; b++) {
		if (!c[b]) {
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
					const size_t i = (size_t)row * width2 + x;
					cb[i] = c[b][j];
					cr[i] = c[b][j + 1];
				}
			}
		}
	}
	return true;
}

static u32 YCbCrToPixel(int y, int cbv, int crv, int pixelMode) {
	const int c = y - 16, d = cbv - 128, e = crv - 128;
	int r = (298 * c + 409 * e + 128) >> 8;
	int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
	int b = (298 * c + 516 * d + 128) >> 8;
	r = std::min(255, std::max(0, r));
	g = std::min(255, std::max(0, g));
	b = std::min(255, std::max(0, b));
	switch (pixelMode) {
	case GE_CMODE_16BIT_BGR5650:
		return ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
	case GE_CMODE_16BIT_ABGR5551:
		return (1 << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);
	case GE_CMODE_16BIT_ABGR4444:
		return (0xF << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4);
	default:
		return 0xFF000000 | (b << 16) | (g << 8) | r;
	}
}

// The conversion itself, with nothing around it. Pure, so that TestMpegCsc can measure it and
// check it - it is the hottest thing in video playback, and the point of having it out here is
// that it can be worked on without a game in the loop.
//
// luma is width by height; cb and cr are half that in both directions, as YUV420 is. dest is
// destStride pixels wide in the format pixelMode names, and the converted range always lands at
// its origin.
void MpegCscRange(u8 *dest, int destStride, int pixelMode,
	const u8 *luma, const u8 *cb, const u8 *cr, int width,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	const int bpp = pixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const int width2 = width >> 1;
	for (int y = 0; y < rangeHeight; y++) {
		const int sy = rangeY + y;
		for (int x = 0; x < rangeWidth; x++) {
			const int sx = rangeX + x;
			const int ci = (sy >> 1) * width2 + (sx >> 1);
			const u32 pixel = YCbCrToPixel(luma[sy * width + sx], cb[ci], cr[ci], pixelMode);
			if (bpp == 4) {
				memcpy(dest + (y * destStride + x) * 4, &pixel, 4);
			} else {
				const u16 p16 = (u16)pixel;
				memcpy(dest + (y * destStride + x) * 2, &p16, 2);
			}
		}
	}
}

// The shared body of sceMpegBaseCscAvc and sceMpegBaseCscAvcRange - the former is just the
// latter over the whole frame.
static int MpegBaseCscRange(u32 bufferRGB, u32 cscAddr, int bufferWidth,
	int rangeX, int rangeY, int rangeWidth, int rangeHeight) {
	auto csc = PSPPointer<SceMp4AvcCscStruct>::Create(cscAddr);
	if (!csc.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad csc struct pointer");
	}
	if (bufferWidth == 0) {
		bufferWidth = g_mpegBaseBufferWidth;
	}

	u32 buffers[8]{};
	for (int i = 0; i < 8; i++) {
		buffers[i] = csc->buffer[i];
	}
	const int width = csc->width << 4;
	const int height = csc->height << 4;
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return hleLogError(Log::Mpeg, -1, "unreasonable frame size %dx%d", width, height);
	}
	if (rangeWidth <= 0 || rangeHeight <= 0) {
		return hleLogDebug(Log::Mpeg, 0, "empty range");
	}
	rangeWidth = std::min(rangeWidth, width - rangeX);
	rangeHeight = std::min(rangeHeight, height - rangeY);
	// The output buffer is sized from the stride below, so a row can't be wider than one. Every
	// game seen so far passes a stride comfortably wider than the frame (512 for 480), but nothing
	// guarantees it, and writing a wider row than we measured would run off the end of the buffer.
	rangeWidth = std::min(rangeWidth, bufferWidth);
	if (rangeX < 0 || rangeY < 0 || rangeWidth <= 0 || rangeHeight <= 0) {
		return hleLogError(Log::Mpeg, -1, "range outside the frame");
	}

	std::vector<u8> luma, cb, cr;
	if (!ReadTiledYCbCr(buffers, width, height, luma, cb, cr)) {
		return hleLogError(Log::Mpeg, -1, "YCbCr buffers not readable");
	}

	const int bpp = g_mpegBasePixelMode == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const u32 destSize = (u32)(rangeHeight * bufferWidth * bpp);
	if (!Memory::IsValidRange(bufferRGB, destSize)) {
		return hleLogError(Log::Mpeg, -1, "output buffer not writable");
	}
	u8 *dest = Memory::GetTypedPointerWriteRange<u8>(bufferRGB, destSize);
	if (!dest) {
		return hleLogError(Log::Mpeg, -1, "output buffer not writable");
	}

	MpegCscRange(dest, bufferWidth, g_mpegBasePixelMode, luma.data(), cb.data(), cr.data(), width,
		rangeX, rangeY, rangeWidth, rangeHeight);
	NotifyMemInfo(MemBlockFlags::WRITE, bufferRGB, destSize, "MpegBaseCsc");
	// The CPU just wrote a video frame into what is usually a display buffer. The hardware backends
	// don't see that on their own, so without telling them the screen keeps showing the last frame
	// the GE drew (the same notification our sceMpegAvcCsc HLE does). The pixel mode numbering
	// matches GEBufferFormat, as it does there.
	gpu->PerformWriteFormattedFromMemory(bufferRGB, destSize, bufferWidth, (GEBufferFormat)g_mpegBasePixelMode);
	// This runs on the DMACPLUS and takes real time. A psmfplayer game blits the current video
	// frame every render frame while it waits for the next, so an instant return is a tight loop
	// that never yields and starves the audio thread that paces playback, and the A/V pipeline
	// deadlocks a few frames in (SOCOM: Tactical Strike hangs exactly here). Our sceMpeg HLE delays
	// sceMpegAvcCsc the same way.
	return hleDelayResult(hleLogDebug(Log::Mpeg, 0, "%dx%d at %d,%d -> %08x stride %d",
		rangeWidth, rangeHeight, rangeX, rangeY, bufferRGB, bufferWidth), "mpegbase csc", 4000);
}

static int sceMpegBaseCscInit(int bufferWidth) {
	g_mpegBaseBufferWidth = bufferWidth ? bufferWidth : 512;
	return hleLogInfo(Log::Mpeg, 0, "bufferWidth %d", bufferWidth);
}

// Sets the output pixel format for the conversions below. The official name isn't known - this one
// says what it does. Inside mpegbase.prx it stores the value and hands it to the DMACPLUS colour
// conversion hardware, and mpeg.prx gets it by indexing a table of {1, 2, 3, 0} with the pixel mode
// the game passed to sceMpegAvcDecodeMode. So the numbering is its own, not the GE's.
static int sceMpegBaseCscSetPixelMode(int pixelMode) {
	static const int toGeMode[4] = {
		GE_CMODE_32BIT_ABGR8888,
		GE_CMODE_16BIT_BGR5650,
		GE_CMODE_16BIT_ABGR5551,
		GE_CMODE_16BIT_ABGR4444,
	};
	if (pixelMode < 0 || pixelMode >= (int)ARRAY_SIZE(toGeMode)) {
		return hleLogError(Log::Mpeg, -1, "bad pixel mode %d", pixelMode);
	}
	g_mpegBasePixelMode = toGeMode[pixelMode];
	return hleLogInfo(Log::Mpeg, 0, "pixel mode %d -> GE mode %d", pixelMode, g_mpegBasePixelMode);
}

static int sceMpegBaseCscAvc(u32 bufferRGB, u32 unknown, int bufferWidth, u32 cscAddr) {
	auto csc = PSPPointer<SceMp4AvcCscStruct>::Create(cscAddr);
	if (!csc.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad csc struct pointer");
	}
	// The whole frame. MpegBaseCscRange clamps to the real frame size (from the allocation, not the
	// descriptor), so pass more than any frame can be.
	return MpegBaseCscRange(bufferRGB, cscAddr, bufferWidth, 0, 0, 1024, 1024);
}

static u32 sceMpegBaseCscAvcRange(u32 bufferRGB, u32 unknown, u32 rangeAddr, int bufferWidth, u32 cscAddr) {
	if (!Memory::IsValidRange(rangeAddr, 16)) {
		return hleLogError(Log::Mpeg, -1, "bad range pointer");
	}
	// Also in macroblocks.
	const int rangeX = Memory::ReadUnchecked_U32(rangeAddr + 0) << 4;
	const int rangeY = Memory::ReadUnchecked_U32(rangeAddr + 4) << 4;
	const int rangeWidth = Memory::ReadUnchecked_U32(rangeAddr + 8) << 4;
	const int rangeHeight = Memory::ReadUnchecked_U32(rangeAddr + 12) << 4;
	return MpegBaseCscRange(bufferRGB, cscAddr, bufferWidth, rangeX, rangeY, rangeWidth, rangeHeight);
}

// Moves a decoded frame between two sets of buffers. Both arguments are descriptors; what gets
// copied is the pixels they point at, not the descriptors.
//
// mpegbase.prx builds a DMA list over the eight buffers (080010f8 in mpegbase_260.prx): flag bit 0
// selects buffers 0,1,4,5 and bit 1 selects 2,3,6,7, with the per-buffer sizes sceVideocodec lays
// its frame out with. mpeg.prx always passes 3 (all eight). The source is the ME's frame; the
// destination is wherever the caller wants it (for psmfplayer, a slot in its output pool).
static int sceMpegBaseYCrCbCopy(u32 dstAddr, u32 srcAddr, int flags) {
	auto dst = PSPPointer<SceMp4AvcCscStruct>::Create(dstAddr);
	auto src = PSPPointer<SceMp4AvcCscStruct>::Create(srcAddr);
	if (!dst.IsValid() || !src.IsValid()) {
		return hleLogError(Log::Mpeg, -1, "bad descriptor pointer");
	}

	// The destination descriptor carries the dimensions in macroblocks (as the colour conversion's
	// does); the stack-built source descriptor states them in pixels, so read the destination.
	const int width = dst->width << 4;
	const int height = dst->height << 4;
	if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
		return hleLogError(Log::Mpeg, -1, "unreasonable frame size %dx%d", width, height);
	}
	int sizes[8];
	VideocodecFrameBufferLayout(width, height, sizes, nullptr);

	int copied = 0;
	for (int i = 0; i < 8; i++) {
		// Buffers 0,1,4,5 go with bit 0 and 2,3,6,7 with bit 1 (the even and odd row halves of luma
		// and chroma).
		const int bit = ((i & 3) < 2) ? 1 : 2;
		if (!(flags & bit) || sizes[i] <= 0) {
			continue;
		}
		const u8 *from = MpegBaseFramePointer(src->buffer[i], sizes[i]);
		if (!from) {
			return hleLogError(Log::Mpeg, -1, "source buffer %d not readable", i);
		}
		if (!Memory::IsValidRange(dst->buffer[i], sizes[i])) {
			return hleLogError(Log::Mpeg, -1, "destination buffer %d (%08x, %d bytes) not writable",
				i, (u32)dst->buffer[i], sizes[i]);
		}
		Memory::MemcpyUnchecked(dst->buffer[i], from, sizes[i]);
		copied += sizes[i];
	}
	return hleLogDebug(Log::Mpeg, 0, "flags %d, %dx%d, %d bytes", flags, width, height, copied);
}

const HLEFunction sceMpegbase[] =
{
	{0XBEA18F91, &WrapU_U<sceMpegBasePESpacketCopy>,           "sceMpegBasePESpacketCopy",           'x', "x"      },
	{0X492B5E4B, &WrapI_I<sceMpegBaseCscInit>,                  "sceMpegBaseCscInit",                 'i', "i"      },
	{0X0530BE4E, &WrapI_I<sceMpegBaseCscSetPixelMode>,          "sceMpegbase_0530BE4E",               'i', "i"      },
	{0X91929A21, &WrapI_UUIU<sceMpegBaseCscAvc>,               "sceMpegBaseCscAvc",                  'i', "xxix"   },
	{0X304882E1, &WrapU_UUUIU<sceMpegBaseCscAvcRange>,         "sceMpegBaseCscAvcRange",             'x', "xxxix"  },
	{0X7AC0321A, &WrapI_UUI<sceMpegBaseYCrCbCopy>,             "sceMpegBaseYCrCbCopy",               'i', "xxi"    }
};

void Register_sceMpegbase()
{
	RegisterHLEModule("sceMpegbase", ARRAY_SIZE(sceMpegbase), sceMpegbase);
};

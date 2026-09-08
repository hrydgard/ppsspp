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

#include "Common/Swap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceMpeg.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/HLE/sceVideocodec.h"
#include "Core/MemMapHelpers.h"
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

// p pointing to a SceMpegLLI structure consists of video frame blocks.
static u32 sceMpegBasePESpacketCopy(u32 p)
{
	int nBlocks = 0;
	auto lli = PSPPointer<SceMpegLLI>::Create(p);
	while (lli.IsValid()) {
		nBlocks++;
		// lli.Next ==0 for last block
		if (lli->Next == 0){
			break;
		}
		++lli;
	}
	MpegSetPmpVideoSource(p, nBlocks);

	// On hardware this is the DMA that moves the PES payload into the Media Engine's own memory,
	// after which mpeg.prx hands sceVideocodecDecode an ME-side address we have no way to read.
	// Since the copy is ours, gather the blocks here instead and let sceVideocodec decode from
	// this - see MpegBaseGetPESPacket.
	lli = PSPPointer<SceMpegLLI>::Create(p);
	u32 dest = 0;
	std::vector<u8> gathered;
	for (int i = 0; i < nBlocks && lli.IsValid(); i++) {
		if (i == 0) {
			dest = lli->pDst;
		}
		// The list is game-supplied, so check the span before taking a pointer to it: the range
		// accessors raise a memory exception rather than returning null, and a malformed block
		// shouldn't fault the game when the intent here is to skip it.
		const u8 *src = (lli->iSize > 0 && Memory::IsValidRange(lli->pSrc, lli->iSize))
			? Memory::GetTypedPointerRange<u8>(lli->pSrc, lli->iSize) : nullptr;
		if (src) {
			gathered.insert(gathered.end(), src, src + lli->iSize);
			// Audio payloads land in main memory, and mpeg.prx hands that same address to
			// sceAudiocodecDecode as its input - so for those the copy has to really happen.
			// Video goes to a Media Engine address that isn't mapped for us; the gather above
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

const std::vector<u8> *MpegBaseGetPESPacket(u32 dest) {
	auto it = g_pesPackets.find(dest);
	return it == g_pesPackets.end() ? nullptr : &it->second;
}


// --- sceMpegbase colour conversion ---------------------------------------------------------
//
// mpeg.prx hands the ME's decoded output to these to be converted to RGB. The descriptor it
// passes is 48 bytes - which is exactly the range mpegbase.prx bounds-checks before using it.
//
// The dimensions appear twice. Death Jr. has the plain macroblock counts in both pairs, but
// Thrillville's psmfplayer path has them shifted up by 8 in the first pair and plain in the
// second, so the second is the one to trust. The four luma buffers are at 0x20; the chroma ones
// aren't in here at all (see the recovery in MpegBaseCscRange).
struct SceMp4AvcCscStruct {
	s32_le scaledHeight;  // 0x00  height << 8 on some paths, macroblocks on others - unused
	s32_le scaledWidth;   // 0x04
	s32_le mode0;         // 0x08
	s32_le mode1;         // 0x0c
	s32_le height;        // 0x10  in macroblocks
	s32_le width;         // 0x14  in macroblocks
	s32_le unk18;         // 0x18
	s32_le unk1c;         // 0x1c
	u32_le buffer[4];     // 0x20  luma only
};
static_assert(sizeof(SceMp4AvcCscStruct) == 0x30);


// The ME doesn't write planar YCbCr. The layout below was established by analysing
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
//     buffer4: left half,  even chroma rows  buffer5: left half,  odd chroma rows
//     buffer6: right half, even chroma rows  buffer7: right half, odd chroma rows
//
// Untangling it into plain planes costs one pass per frame, which keeps the conversion below
// readable and is not where the time goes.
static bool ReadTiledYCbCr(const u32 *buffers, int width, int height,
	std::vector<u8> &luma, std::vector<u8> &cb, std::vector<u8> &cr) {
	const int width2 = width >> 1;
	const int height2 = height >> 1;

	// buffer0/2 take the odd band out when the width isn't a multiple of 32.
	const int lumaSizeLeft = ((width + 16) >> 5) * (height >> 1) * 16;
	const int lumaSizeRight = (width >> 5) * (height >> 1) * 16;
	const int chromaSizeLeft = lumaSizeLeft >> 1;
	const int chromaSizeRight = lumaSizeRight >> 1;

	const u8 *y[4] = {};
	const u8 *c[4] = {};
	const int ySize[4] = { lumaSizeLeft, lumaSizeRight, lumaSizeLeft, lumaSizeRight };
	const int cSize[4] = { chromaSizeLeft, chromaSizeLeft, chromaSizeRight, chromaSizeRight };
	// These are addresses in the Media Engine's memory, not in PSP RAM, so they resolve through
	// sceVideocodec rather than through Memory::. A descriptor that was never filled in holds
	// small integers instead, and those simply aren't in the ME's range.
	for (int i = 0; i < 4; i++) {
		if (ySize[i] > 0) {
			y[i] = VideocodecMEPointer(buffers[i], ySize[i]);
			if (!y[i]) {
				return false;
			}
		}
		if (cSize[i] > 0) {
			c[i] = VideocodecMEPointer(buffers[4 + i], cSize[i]);
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

	// The descriptor only carries the four luma buffers. Recover the full set from the allocation
	// sceVideocodec handed out - both ends of that are ours.
	u32 buffers[8]{};
	for (int i = 0; i < 4; i++) {
		buffers[i] = csc->buffer[i];
	}
	VideocodecGetFrameBuffers(csc->buffer[0], buffers);

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

	const int width2 = width >> 1;
	for (int y = 0; y < rangeHeight; y++) {
		const int sy = rangeY + y;
		for (int x = 0; x < rangeWidth; x++) {
			const int sx = rangeX + x;
			const int ci = (sy >> 1) * width2 + (sx >> 1);
			const u32 pixel = YCbCrToPixel(luma[sy * width + sx], cb[ci], cr[ci], g_mpegBasePixelMode);
			if (bpp == 4) {
				memcpy(dest + (y * bufferWidth + x) * 4, &pixel, 4);
			} else {
				const u16 p16 = (u16)pixel;
				memcpy(dest + (y * bufferWidth + x) * 2, &p16, 2);
			}
		}
	}
	NotifyMemInfo(MemBlockFlags::WRITE, bufferRGB, destSize, "MpegBaseCsc");
	return hleLogDebug(Log::Mpeg, 0, "%dx%d at %d,%d -> %08x stride %d",
		rangeWidth, rangeHeight, rangeX, rangeY, bufferRGB, bufferWidth);
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
	return MpegBaseCscRange(bufferRGB, cscAddr, bufferWidth, 0, 0, csc->width << 4, csc->height << 4);
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

// Copies one 48-byte YCrCb descriptor over another. The hardware copies the buffers themselves
// when told to; games seen so far only use it to move the descriptor around, so that is all we
// do until something needs more.
static int sceMpegBaseYCrCbCopy(u32 dstAddr, u32 srcAddr, int flags) {
	if (!Memory::IsValidRange(dstAddr, sizeof(SceMp4AvcCscStruct)) ||
		!Memory::IsValidRange(srcAddr, sizeof(SceMp4AvcCscStruct))) {
		return hleLogError(Log::Mpeg, -1, "bad descriptor pointer");
	}
	Memory::Memcpy(dstAddr, srcAddr, (u32)sizeof(SceMp4AvcCscStruct), "MpegBaseYCrCbCopy");
	return hleLogDebug(Log::Mpeg, 0, "flags %08x - descriptor copied, buffers left alone", flags);
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

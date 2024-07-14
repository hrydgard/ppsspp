// Copyright (c) 2017- PPSSPP Project.

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

#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <set>
#include <vector>
#include <mutex>
#include <zstd.h>

#include "Common/CommonTypes.h"
#include "Common/File/FileUtil.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"

#include "Core/Core.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/ThreadPools.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Debugger/Record.h"
#include "GPU/Debugger/RecordFormat.h"

namespace GPURecord {

static bool active = false;
static std::atomic<bool> nextFrame = false;
static int flipLastAction = -1;
static int flipFinishAt = -1;
static uint32_t lastEdramTrans = 0x400;
static std::function<void(const Path &)> writeCallback;

static std::vector<u8> pushbuf;
static std::vector<Command> commands;
static std::vector<u32> lastRegisters;
static std::vector<u32> lastTextures;
static std::set<u32> lastRenderTargets;
static std::vector<u8> lastVRAM;

enum class DirtyVRAMFlag : uint8_t {
	CLEAN = 0,
	UNKNOWN = 1,
	DIRTY = 2,
	DRAWN = 3,
};
static constexpr uint32_t DIRTY_VRAM_SHIFT = 8;
static constexpr uint32_t DIRTY_VRAM_ROUND = (1 << DIRTY_VRAM_SHIFT) - 1;
static constexpr uint32_t DIRTY_VRAM_SIZE = (2 * 1024 * 1024) >> DIRTY_VRAM_SHIFT;
static constexpr uint32_t DIRTY_VRAM_MASK = (2 * 1024 * 1024 - 1) >> DIRTY_VRAM_SHIFT;
static DirtyVRAMFlag dirtyVRAM[DIRTY_VRAM_SIZE];

static void FlushRegisters() {
	if (!lastRegisters.empty()) {
		Command last{CommandType::REGISTERS};
		last.ptr = (u32)pushbuf.size();
		last.sz = (u32)(lastRegisters.size() * sizeof(u32));
		pushbuf.resize(pushbuf.size() + last.sz);
		memcpy(pushbuf.data() + last.ptr, lastRegisters.data(), last.sz);
		lastRegisters.clear();

		commands.push_back(last);
	}
}

static Path GenRecordingFilename() {
	const Path dumpDir = GetSysDirectory(DIRECTORY_DUMP);

	File::CreateFullPath(dumpDir);

	const std::string prefix = g_paramSFO.GetDiscID();

	for (int n = 1; n < 10000; ++n) {
		std::string filename = StringFromFormat("%s_%04d.ppdmp", prefix.c_str(), n);

		const Path path = dumpDir / filename;

		if (!File::Exists(path)) {
			return path;
		}
	}

	return dumpDir / StringFromFormat("%s_%04d.ppdmp", prefix.c_str(), 9999);
}

static void DirtyAllVRAM(DirtyVRAMFlag flag) {
	if (flag == DirtyVRAMFlag::UNKNOWN) {
		for (uint32_t i = 0; i < DIRTY_VRAM_SIZE; ++i) {
			if (dirtyVRAM[i] == DirtyVRAMFlag::CLEAN)
				dirtyVRAM[i] = DirtyVRAMFlag::UNKNOWN;
		}
	} else {
		for (uint32_t i = 0; i < DIRTY_VRAM_SIZE; ++i)
			dirtyVRAM[i] = flag;
	}
}

static void DirtyVRAM(u32 start, u32 sz, DirtyVRAMFlag flag) {
	u32 count = (sz + DIRTY_VRAM_ROUND) >> DIRTY_VRAM_SHIFT;
	u32 first = (start >> DIRTY_VRAM_SHIFT) & DIRTY_VRAM_MASK;
	if (first + count > DIRTY_VRAM_SIZE) {
		DirtyAllVRAM(flag);
		return;
	}

	for (u32 i = 0; i < count; ++i)
		dirtyVRAM[first + i] = flag;
}

static void DirtyDrawnVRAM() {
	int w = std::min(gstate.getScissorX2(), gstate.getRegionX2()) + 1;
	int h = std::min(gstate.getScissorY2(), gstate.getRegionY2()) + 1;

	bool drawZ = !gstate.isModeClear() && gstate.isDepthWriteEnabled() && gstate.isDepthTestEnabled();
	bool clearZ = gstate.isModeClear() && gstate.isClearModeDepthMask();
	if (drawZ || clearZ) {
		int bytes = 2 * gstate.DepthBufStride() * h;
		if (w > gstate.DepthBufStride())
			bytes += 2 * (w - gstate.DepthBufStride());
		DirtyVRAM(gstate.getDepthBufAddress(), bytes, DirtyVRAMFlag::DRAWN);
	}

	int bpp = gstate.FrameBufFormat() == GE_FORMAT_8888 ? 4 : 2;
	int bytes = bpp * gstate.FrameBufStride() * h;
	if (w > gstate.FrameBufStride())
		bytes += bpp * (w - gstate.FrameBufStride());
	DirtyVRAM(gstate.getFrameBufAddress(), bytes, DirtyVRAMFlag::DRAWN);
}

static void BeginRecording() {
	active = true;
	nextFrame = false;
	lastTextures.clear();
	lastRenderTargets.clear();
	flipLastAction = gpuStats.numFlips;
	flipFinishAt = -1;

	u32 ptr = (u32)pushbuf.size();
	u32 sz = 512 * 4;
	pushbuf.resize(pushbuf.size() + sz);
	gstate.Save((u32_le *)(pushbuf.data() + ptr));
	commands.push_back({CommandType::INIT, sz, ptr});
	lastVRAM.resize(2 * 1024 * 1024);

	// Also save the initial CLUT.
	GPUDebugBuffer clut;
	if (gpuDebug->GetCurrentClut(clut)) {
		sz = clut.GetStride() * clut.PixelSize();
		_assert_msg_(sz == 1024, "CLUT should be 1024 bytes");
		ptr = (u32)pushbuf.size();
		pushbuf.resize(pushbuf.size() + sz);
		memcpy(pushbuf.data() + ptr, clut.GetData(), sz);
		commands.push_back({ CommandType::CLUT, sz, ptr });
	}

	DirtyAllVRAM(DirtyVRAMFlag::DIRTY);
}

static void WriteCompressed(FILE *fp, const void *p, size_t sz) {
	size_t compressed_size = ZSTD_compressBound(sz);
	u8 *compressed = new u8[compressed_size];
	compressed_size = ZSTD_compress(compressed, compressed_size, p, sz, 6);

	u32 write_size = (u32)compressed_size;
	fwrite(&write_size, sizeof(write_size), 1, fp);
	fwrite(compressed, compressed_size, 1, fp);

	delete [] compressed;
}

static Path WriteRecording() {
	FlushRegisters();

	const Path filename = GenRecordingFilename();

	NOTICE_LOG(Log::G3D, "Recording filename: %s", filename.c_str());

	FILE *fp = File::OpenCFile(filename, "wb");
	Header header{};
	strncpy(header.magic, HEADER_MAGIC, sizeof(header.magic));
	header.version = VERSION;
	strncpy(header.gameID, g_paramSFO.GetDiscID().c_str(), sizeof(header.gameID));
	fwrite(&header, sizeof(header), 1, fp);

	u32 sz = (u32)commands.size();
	fwrite(&sz, sizeof(sz), 1, fp);
	u32 bufsz = (u32)pushbuf.size();
	fwrite(&bufsz, sizeof(bufsz), 1, fp);

	WriteCompressed(fp, commands.data(), commands.size() * sizeof(Command));
	WriteCompressed(fp, pushbuf.data(), bufsz);

	fclose(fp);

	return filename;
}

static void GetVertDataSizes(int vcount, const void *indices, u32 &vbytes, u32 &ibytes) {
	VertexDecoder vdec;
	VertexDecoderOptions opts{};
	vdec.SetVertexType(gstate.vertType, opts);

	if (indices) {
		u16 lower = 0;
		u16 upper = 0;
		GetIndexBounds(indices, vcount, gstate.vertType, &lower, &upper);

		vbytes = (upper + 1) * vdec.VertexSize();
		u32 idx = gstate.vertType & GE_VTYPE_IDX_MASK;
		if (idx == GE_VTYPE_IDX_8BIT) {
			ibytes = vcount * sizeof(u8);
		} else if (idx == GE_VTYPE_IDX_16BIT) {
			ibytes = vcount * sizeof(u16);
		} else if (idx == GE_VTYPE_IDX_32BIT) {
			ibytes = vcount * sizeof(u32);
		}
	} else {
		vbytes = vcount * vdec.VertexSize();
	}
}

static const u8 *mymemmem(const u8 *haystack, size_t off, size_t hlen, const u8 *needle, size_t nlen, uintptr_t align) {
	if (!nlen) {
		return nullptr;
	}

	const u8 *last_possible = haystack + hlen - nlen;
	const u8 *first_possible = haystack + off;
	int first = *needle;

	const u8 *result = nullptr;
	std::mutex resultLock;

	int range = (int)(last_possible - first_possible);
	ParallelRangeLoop(&g_threadManager, [&](int l, int h) {
		const u8 *p = haystack + off + l;
		const u8 *pend = haystack + off + h;

		const uintptr_t align_mask = align - 1;
		auto poffset = [&]() {
			return ((uintptr_t)(p - haystack) & align_mask);
		};
		auto alignp = [&]() {
			uintptr_t offset = poffset();
			if (offset != 0)
				p += align - offset;
		};

		alignp();
		while (p <= pend) {
			p = (const u8 *)memchr(p, first, pend - p + 1);
			if (!p) {
				return;
			}
			if (poffset() == 0 && !memcmp(p, needle, nlen)) {
				std::lock_guard<std::mutex> guard(resultLock);
				// Take the lowest result so we get the same file for any # of threads.
				if (!result || p < result)
					result = p;
				return;
			}

			p++;
			alignp();
		}
	}, 0, range, 128 * 1024, TaskPriority::LOW);

	return result;
}

static Command EmitCommandWithRAM(CommandType t, const void *p, u32 sz, u32 align) {
	FlushRegisters();

	Command cmd{t, sz, 0};

	if (sz) {
		// If at all possible, try to find it already in the buffer.
		const u8 *prev = nullptr;
		const size_t NEAR_WINDOW = std::max((int)sz * 2, 1024 * 10);
		// Let's try nearby first... it will often be nearby.
		if (pushbuf.size() > NEAR_WINDOW) {
			prev = mymemmem(pushbuf.data(), pushbuf.size() - NEAR_WINDOW, pushbuf.size(), (const u8 *)p, sz, align);
		}
		if (!prev) {
			prev = mymemmem(pushbuf.data(), 0, pushbuf.size(), (const u8 *)p, sz, align);
		}

		if (prev) {
			cmd.ptr = (u32)(prev - pushbuf.data());
		} else {
			cmd.ptr = (u32)pushbuf.size();
			int pad = 0;
			if (cmd.ptr & (align - 1)) {
				pad = align - (cmd.ptr & (align - 1));
				cmd.ptr += pad;
			}
			pushbuf.resize(pushbuf.size() + sz + pad);
			if (pad) {
				memset(pushbuf.data() + cmd.ptr - pad, 0, pad);
			}
			memcpy(pushbuf.data() + cmd.ptr, p, sz);
		}
	}

	commands.push_back(cmd);

	return cmd;
}

static void UpdateLastVRAM(u32 addr, u32 bytes) {
	u32 base = addr & 0x001FFFFF;
	if (base + bytes > 0x00200000) {
		memcpy(&lastVRAM[base], Memory::GetPointerUnchecked(0x04000000 | base), 0x00200000 - base);
		bytes = base + bytes - 0x00200000;
		base = 0;
	}
	memcpy(&lastVRAM[base], Memory::GetPointerUnchecked(0x04000000 | base), bytes);
}

static void ClearLastVRAM(u32 addr, u8 c, u32 bytes) {
	u32 base = addr & 0x001FFFFF;
	if (base + bytes > 0x00200000) {
		memset(&lastVRAM[base], c, 0x00200000 - base);
		bytes = base + bytes - 0x00200000;
		base = 0;
	}
	memset(&lastVRAM[base], c, bytes);
}

static int CompareLastVRAM(u32 addr, u32 bytes) {
	u32 base = addr & 0x001FFFFF;
	if (base + bytes > 0x00200000) {
		int result = memcmp(&lastVRAM[base], Memory::GetPointerUnchecked(0x04000000 | base), 0x00200000 - base);
		if (result != 0)
			return result;

		bytes = base + bytes - 0x00200000;
		base = 0;
	}
	return memcmp(&lastVRAM[base], Memory::GetPointerUnchecked(0x04000000 | base), bytes);
}

static u32 GetTargetFlags(u32 addr, u32 sizeInRAM) {
	addr &= 0x041FFFFF;
	const bool isTarget = lastRenderTargets.find(addr) != lastRenderTargets.end();

	bool isUnknownVRAM = false;
	bool isDirtyVRAM = false;
	bool isDrawnVRAM = false;
	uint32_t start = (addr >> DIRTY_VRAM_SHIFT) & DIRTY_VRAM_MASK;
	uint32_t blocks = (sizeInRAM + DIRTY_VRAM_ROUND) >> DIRTY_VRAM_SHIFT;
	if (start + blocks >= DIRTY_VRAM_SIZE)
		return 0;
	bool startEven = (addr & DIRTY_VRAM_ROUND) == 0;
	bool endEven = ((addr + sizeInRAM) & DIRTY_VRAM_ROUND) == 0;
	for (uint32_t i = 0; i < blocks; ++i) {
		DirtyVRAMFlag flag = dirtyVRAM[start + i];
		isUnknownVRAM = (isUnknownVRAM || flag == DirtyVRAMFlag::UNKNOWN) && flag != DirtyVRAMFlag::DIRTY && flag != DirtyVRAMFlag::DRAWN;
		isDirtyVRAM = isDirtyVRAM || flag != DirtyVRAMFlag::CLEAN;
		isDrawnVRAM = isDrawnVRAM || flag == DirtyVRAMFlag::DRAWN;

		// Mark the VRAM clean now that it's been copied to VRAM.
		if (flag == DirtyVRAMFlag::UNKNOWN || flag == DirtyVRAMFlag::DIRTY) {
			if ((i > 0 || startEven) && (i < blocks || endEven))
				dirtyVRAM[start + i] = DirtyVRAMFlag::CLEAN;
		}
	}

	if (isUnknownVRAM && isDirtyVRAM) {
		// This means it's only UNKNOWN/CLEAN and not known to be actually dirty.
		// Let's check our shadow copy of what we last sent for this VRAM.
		int diff = CompareLastVRAM(addr, sizeInRAM);
		if (diff == 0)
			isDirtyVRAM = false;
	}

	// The isTarget flag is mostly used for replay of dumps on a PSP.
	u32 flags = isTarget ? 1 : 0;
	// The unchangedVRAM flag tells us we can skip recopying.
	if (!isDirtyVRAM)
		flags |= 2;
	// And the drawn flag tells us this data was potentially drawn to.
	if (isDrawnVRAM)
		flags |= 4;

	return flags;
}

static void EmitTextureData(int level, u32 texaddr) {
	GETextureFormat format = gstate.getTextureFormat();
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	int bufw = GetTextureBufw(level, texaddr, format);
	int extraw = w > bufw ? w - bufw : 0;
	u32 sizeInRAM = (textureBitsPerPixel[format] * (bufw * h + extraw)) / 8;

	CommandType type = CommandType((int)CommandType::TEXTURE0 + level);
	const u8 *p = Memory::GetPointerUnchecked(texaddr);
	u32 bytes = Memory::ValidSize(texaddr, sizeInRAM);
	std::vector<u8> framebufData;

	if (Memory::IsVRAMAddress(texaddr)) {
		struct FramebufData {
			u32 addr;
			int bufw;
			u32 flags;
			u32 pad;
		};

		u32 flags = GetTargetFlags(texaddr, bytes);
		FramebufData framebuf{ texaddr, bufw, flags };
		framebufData.resize(sizeof(framebuf) + bytes);
		memcpy(&framebufData[0], &framebuf, sizeof(framebuf));
		memcpy(&framebufData[sizeof(framebuf)], p, bytes);
		p = &framebufData[0];

		if ((flags & 2) == 0)
			UpdateLastVRAM(texaddr, bytes);

		// Okay, now we'll just emit this instead.
		type = CommandType((int)CommandType::FRAMEBUF0 + level);
		bytes += (u32)sizeof(framebuf);
	}

	if (bytes > 0) {
		FlushRegisters();

		// Dumps are huge - let's try to find this already emitted.
		for (u32 prevptr : lastTextures) {
			if (pushbuf.size() < prevptr + bytes) {
				continue;
			}

			if (memcmp(pushbuf.data() + prevptr, p, bytes) == 0) {
				commands.push_back({type, bytes, prevptr});
				// Okay, that was easy.  Bail out.
				return;
			}
		}

		// Not there, gotta emit anew.
		Command cmd = EmitCommandWithRAM(type, p, bytes, 16);
		lastTextures.push_back(cmd.ptr);
	}
}

static void FlushPrimState(int vcount) {
	// TODO: Eventually, how do we handle texturing from framebuf/zbuf?
	// TODO: Do we need to preload color/depth/stencil (in case from last frame)?

	lastRenderTargets.insert(PSP_GetVidMemBase() | gstate.getFrameBufRawAddress());
	lastRenderTargets.insert(PSP_GetVidMemBase() | gstate.getDepthBufRawAddress());

	// We re-flush textures always in case the game changed them... kinda expensive.
	bool textureEnabled = gstate.isTextureMapEnabled() || gstate.isAntiAliasEnabled();
	// Play it safe and allow texture coords to emit data too.
	bool textureCoords = (gstate.vertType & GE_VTYPE_TC_MASK) != 0;
	for (int level = 0; level < 8; ++level) {
		u32 texaddr = gstate.getTextureAddress(level);
		if (texaddr && (textureEnabled || textureCoords)) {
			EmitTextureData(level, texaddr);
		}
	}

	const void *verts = Memory::GetPointer(gstate_c.vertexAddr);
	const void *indices = nullptr;
	if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
		indices = Memory::GetPointer(gstate_c.indexAddr);
	}

	u32 ibytes = 0;
	u32 vbytes = 0;
	GetVertDataSizes(vcount, indices, vbytes, ibytes);

	if (indices && ibytes > 0) {
		EmitCommandWithRAM(CommandType::INDICES, indices, ibytes, 4);
	}
	if (verts && vbytes > 0) {
		EmitCommandWithRAM(CommandType::VERTICES, verts, vbytes, 4);
	}
}

static void EmitTransfer(u32 op) {
	FlushRegisters();

	// This may not make a lot of sense right now, unless it's to a framebuf...
	u32 dstBasePtr = gstate.getTransferDstAddress();
	if (!Memory::IsVRAMAddress(dstBasePtr)) {
		// Skip, not VRAM, so can't affect drawing (we flush textures each prim.)
		return;
	}

	u32 srcBasePtr = gstate.getTransferSrcAddress();
	u32 srcStride = gstate.getTransferSrcStride();
	int srcX = gstate.getTransferSrcX();
	int srcY = gstate.getTransferSrcY();
	u32 dstStride = gstate.getTransferDstStride();
	int dstX = gstate.getTransferDstX();
	int dstY = gstate.getTransferDstY();
	int width = gstate.getTransferWidth();
	int height = gstate.getTransferHeight();
	int bpp = gstate.getTransferBpp();

	u32 srcBytes = ((srcY + height - 1) * srcStride + (srcX + width)) * bpp;
	srcBytes = Memory::ValidSize(srcBasePtr, srcBytes);

	u32 dstBytes = ((dstY + height - 1) * dstStride + (dstX + width)) * bpp;
	dstBytes = Memory::ValidSize(dstBasePtr, dstBytes);

	if (srcBytes != 0) {
		EmitCommandWithRAM(CommandType::TRANSFERSRC, Memory::GetPointerUnchecked(srcBasePtr), srcBytes, 16);
		DirtyVRAM(dstBasePtr, dstBytes, DirtyVRAMFlag::DIRTY);
	}

	lastRegisters.push_back(op);
}

static void EmitClut(u32 op) {
	u32 addr = gstate.getClutAddress();

	// Hardware rendering may be using a framebuffer as CLUT.
	// To get at this, we first run the command (normally we're called right before it has run.)
	if (Memory::IsVRAMAddress(addr))
		gpuDebug->SetCmdValue(op);

	// Actually should only be 0x3F, but we allow enhanced CLUTs.  See #15727.
	u32 blocks = (op & 0x7F) == 0x40 ? 0x40 : (op & 0x3F);
	u32 bytes = blocks * 32;
	bytes = Memory::ValidSize(addr, bytes);

	if (bytes != 0) {
		// Send the original address so VRAM can be reasoned about.
		if (Memory::IsVRAMAddress(addr)) {
			struct ClutAddrData {
				u32 addr;
				u32 flags;
			};
			u32 flags = GetTargetFlags(addr, bytes);
			ClutAddrData data{ addr, flags };

			FlushRegisters();
			Command cmd{CommandType::CLUTADDR, sizeof(data), (u32)pushbuf.size()};
			pushbuf.resize(pushbuf.size() + sizeof(data));
			memcpy(pushbuf.data() + cmd.ptr, &data, sizeof(data));
			commands.push_back(cmd);

			if ((flags & 2) == 0)
				UpdateLastVRAM(addr, bytes);
		}
		EmitCommandWithRAM(CommandType::CLUT, Memory::GetPointerUnchecked(addr), bytes, 16);
	}

	lastRegisters.push_back(op);
}

static void EmitPrim(u32 op) {
	FlushPrimState(op & 0x0000FFFF);

	lastRegisters.push_back(op);
	DirtyDrawnVRAM();
}

static void EmitBezierSpline(u32 op) {
	int ucount = op & 0xFF;
	int vcount = (op >> 8) & 0xFF;
	FlushPrimState(ucount * vcount);

	lastRegisters.push_back(op);
	DirtyDrawnVRAM();
}

bool IsActive() {
	return active;
}

bool IsActivePending() {
	return nextFrame || active;
}

bool RecordNextFrame(const std::function<void(const Path &)> callback) {
	if (!nextFrame) {
		flipLastAction = gpuStats.numFlips;
		flipFinishAt = -1;
		writeCallback = callback;
		nextFrame = true;
		return true;
	}
	return false;
}

void ClearCallback() {
	// Not super thread safe..
	writeCallback = nullptr;
}

static void FinishRecording() {
	// We're done - this was just to write the result out.
	Path filename = WriteRecording();
	commands.clear();
	pushbuf.clear();
	lastVRAM.clear();

	NOTICE_LOG(Log::System, "Recording finished");
	active = false;
	flipLastAction = gpuStats.numFlips;
	flipFinishAt = -1;
	lastEdramTrans = 0x400;

	if (writeCallback) {
		writeCallback(filename);
	}
	writeCallback = nullptr;
}

static void CheckEdramTrans() {
	if (!gpuDebug)
		return;

	uint32_t value = gpuDebug->GetAddrTranslation();
	if (value == lastEdramTrans)
		return;
	lastEdramTrans = value;

	FlushRegisters();
	Command cmd{CommandType::EDRAMTRANS, sizeof(value), (u32)pushbuf.size()};
	pushbuf.resize(pushbuf.size() + sizeof(value));
	memcpy(pushbuf.data() + cmd.ptr, &value, sizeof(value));
	commands.push_back(cmd);
}

void NotifyCommand(u32 pc) {
	if (!active) {
		return;
	}

	CheckEdramTrans();
	const u32 op = Memory::Read_U32(pc);
	const GECommand cmd = GECommand(op >> 24);

	switch (cmd) {
	case GE_CMD_VADDR:
	case GE_CMD_IADDR:
	case GE_CMD_JUMP:
	case GE_CMD_CALL:
	case GE_CMD_RET:
	case GE_CMD_END:
	case GE_CMD_SIGNAL:
	case GE_CMD_FINISH:
	case GE_CMD_BASE:
	case GE_CMD_OFFSETADDR:
	case GE_CMD_ORIGIN:
		// These just prepare future commands, and are flushed with those commands.
		// TODO: Maybe add a command just to log that these were hit?
		break;

	case GE_CMD_BOUNDINGBOX:
	case GE_CMD_BJUMP:
		// Since we record each command, this is theoretically not relevant.
		// TODO: Output a CommandType to validate this.
		break;

	case GE_CMD_PRIM:
		EmitPrim(op);
		break;

	case GE_CMD_BEZIER:
	case GE_CMD_SPLINE:
		EmitBezierSpline(op);
		break;

	case GE_CMD_LOADCLUT:
		EmitClut(op);
		break;

	case GE_CMD_TRANSFERSTART:
		EmitTransfer(op);
		break;

	default:
		lastRegisters.push_back(op);
		break;
	}
}

void NotifyMemcpy(u32 dest, u32 src, u32 sz) {
	if (!active) {
		return;
	}

	CheckEdramTrans();
	if (Memory::IsVRAMAddress(dest)) {
		FlushRegisters();
		Command cmd{CommandType::MEMCPYDEST, sizeof(dest), (u32)pushbuf.size()};
		pushbuf.resize(pushbuf.size() + sizeof(dest));
		memcpy(pushbuf.data() + cmd.ptr, &dest, sizeof(dest));
		commands.push_back(cmd);

		sz = Memory::ValidSize(dest, sz);
		if (sz != 0) {
			EmitCommandWithRAM(CommandType::MEMCPYDATA, Memory::GetPointerUnchecked(dest), sz, 1);
			UpdateLastVRAM(dest, sz);
			DirtyVRAM(dest, sz, DirtyVRAMFlag::CLEAN);
		}
	}
}

void NotifyMemset(u32 dest, int v, u32 sz) {
	if (!active) {
		return;
	}

	CheckEdramTrans();
	struct MemsetCommand {
		u32 dest;
		int value;
		u32 sz;
	};

	if (Memory::IsVRAMAddress(dest)) {
		sz = Memory::ValidSize(dest, sz);
		MemsetCommand data{dest, v, sz};

		FlushRegisters();
		Command cmd{CommandType::MEMSET, sizeof(data), (u32)pushbuf.size()};
		pushbuf.resize(pushbuf.size() + sizeof(data));
		memcpy(pushbuf.data() + cmd.ptr, &data, sizeof(data));
		commands.push_back(cmd);
		ClearLastVRAM(dest, v, sz);
		DirtyVRAM(dest, sz, DirtyVRAMFlag::CLEAN);
	}
}

void NotifyUpload(u32 dest, u32 sz) {
	// This also checks the edram translation value and dirties VRAM.
	NotifyMemcpy(dest, dest, sz);
}

static bool HasDrawCommands() {
	if (commands.empty())
		return false;

	for (const Command &cmd : commands) {
		switch (cmd.type) {
		case CommandType::INIT:
		case CommandType::DISPLAY:
			continue;

		default:
			return true;
		}
	}

	// Only init and display commands, keep going.
	return false;
}

void NotifyDisplay(u32 framebuf, int stride, int fmt) {
	bool writePending = false;
	if (active && HasDrawCommands()) {
		writePending = true;
	}
	if (!active && nextFrame && (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0) {
		NOTICE_LOG(Log::System, "Recording starting on display...");
		BeginRecording();
	}
	if (!active) {
		return;
	}

	CheckEdramTrans();
	struct DisplayBufData {
		PSPPointer<u8> topaddr;
		int linesize, pixelFormat;
	};

	DisplayBufData disp{ { framebuf }, stride, fmt };

	FlushRegisters();
	u32 ptr = (u32)pushbuf.size();
	u32 sz = (u32)sizeof(disp);
	pushbuf.resize(pushbuf.size() + sz);
	memcpy(pushbuf.data() + ptr, &disp, sz);

	commands.push_back({ CommandType::DISPLAY, sz, ptr });

	if (writePending) {
		NOTICE_LOG(Log::System, "Recording complete on display");
		FinishRecording();
	}
}

void NotifyBeginFrame() {
	const bool noDisplayAction = flipLastAction + 4 < gpuStats.numFlips;
	// We do this only to catch things that don't call NotifyDisplay.
	if (active && HasDrawCommands() && (noDisplayAction || gpuStats.numFlips == flipFinishAt)) {
		NOTICE_LOG(Log::System, "Recording complete on frame");

		CheckEdramTrans();
		struct DisplayBufData {
			PSPPointer<u8> topaddr;
			u32 linesize, pixelFormat;
		};

		DisplayBufData disp;
		__DisplayGetFramebuf(&disp.topaddr, &disp.linesize, &disp.pixelFormat, 0);

		FlushRegisters();
		u32 ptr = (u32)pushbuf.size();
		u32 sz = (u32)sizeof(disp);
		pushbuf.resize(pushbuf.size() + sz);
		memcpy(pushbuf.data() + ptr, &disp, sz);

		commands.push_back({ CommandType::DISPLAY, sz, ptr });

		FinishRecording();
	}
	if (!active && nextFrame && (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0 && noDisplayAction) {
		NOTICE_LOG(Log::System, "Recording starting on frame...");
		BeginRecording();
		// If we began on a BeginFrame, end on a BeginFrame.
		flipFinishAt = gpuStats.numFlips + 1;
	}
}

void NotifyCPU() {
	if (!active) {
		return;
	}

	DirtyAllVRAM(DirtyVRAMFlag::UNKNOWN);
}

};

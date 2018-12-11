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
#include <cstring>
#include <functional>
#include <set>
#include <vector>
#include <snappy-c.h>
#include "base/stringutil.h"
#include "Common/Common.h"
#include "Common/FileUtil.h"
#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Debugger/Record.h"

namespace GPURecord {

static const char *HEADER = "PPSSPPGE";
// Version 1: Uncompressed
// Version 2: Uses snappy
// Version 3: Adds FRAMEBUF0-FRAMEBUF9
static const int VERSION = 3;
static const int MIN_VERSION = 2;

static bool active = false;
static bool nextFrame = false;
static int flipLastAction = -1;
static std::function<void(const std::string &)> writeCallback;

enum class CommandType : u8 {
	INIT = 0,
	REGISTERS = 1,
	VERTICES = 2,
	INDICES = 3,
	CLUT = 4,
	TRANSFERSRC = 5,
	MEMSET = 6,
	MEMCPYDEST = 7,
	MEMCPYDATA = 8,
	DISPLAY = 9,

	TEXTURE0 = 0x10,
	TEXTURE1 = 0x11,
	TEXTURE2 = 0x12,
	TEXTURE3 = 0x13,
	TEXTURE4 = 0x14,
	TEXTURE5 = 0x15,
	TEXTURE6 = 0x16,
	TEXTURE7 = 0x17,

	FRAMEBUF0 = 0x18,
	FRAMEBUF1 = 0x19,
	FRAMEBUF2 = 0x1A,
	FRAMEBUF3 = 0x1B,
	FRAMEBUF4 = 0x1C,
	FRAMEBUF5 = 0x1D,
	FRAMEBUF6 = 0x1E,
	FRAMEBUF7 = 0x1F,
};

#pragma pack(push, 1)

struct Command {
	CommandType type;
	u32 sz;
	u32 ptr;
};

#pragma pack(pop)

static std::vector<u8> pushbuf;
static std::vector<Command> commands;
static std::vector<u32> lastRegisters;
static std::vector<u32> lastTextures;
static std::set<u32> lastRenderTargets;

// TODO: Maybe move execute to another file?
class DumpExecute {
public:
	~DumpExecute();

	bool Run();

private:
	void SyncStall();
	bool SubmitCmds(void *p, u32 sz);
	void SubmitListEnd();

	void Init(u32 ptr, u32 sz);
	void Registers(u32 ptr, u32 sz);
	void Vertices(u32 ptr, u32 sz);
	void Indices(u32 ptr, u32 sz);
	void Clut(u32 ptr, u32 sz);
	void TransferSrc(u32 ptr, u32 sz);
	void Memset(u32 ptr, u32 sz);
	void MemcpyDest(u32 ptr, u32 sz);
	void Memcpy(u32 ptr, u32 sz);
	void Texture(int level, u32 ptr, u32 sz);
	void Framebuf(int level, u32 ptr, u32 sz);
	void Display(u32 ptr, u32 sz);

	u32 execMemcpyDest = 0;
	u32 execListBuf = 0;
	u32 execListPos = 0;
	u32 execListID = 0;
	const int LIST_BUF_SIZE = 256 * 1024;
	std::vector<u32> execListQueue;
	u16 lastBufw_[8]{};
};

// This class maps pushbuffer (dump data) sections to PSP memory.
// Dumps can be larger than available PSP memory, because they include generated data too.
//
// If possible, it maps to dynamically allocated "slabs" so nearby access is fast.
// Otherwise it uses "extra" allocations to manage sections that straddle two slabs.
// Slabs are managed with LRU, extra buffers are round-robin.
class BufMapping {
public:
	// Returns a pointer to contiguous memory for this access, or else 0 (failure).
	u32 Map(u32 bufpos, u32 sz, const std::function<void()> &flush);

	// Clear and reset allocations made.
	void Reset() {
		slabGeneration_ = 0;
		extraOffset_ = 0;
		for (int i = 0; i < SLAB_COUNT; ++i) {
			slabs_[i].Free();
		}
		for (int i = 0; i < EXTRA_COUNT; ++i) {
			extra_[i].Free();
		}
	}

protected:
	u32 MapSlab(u32 bufpos, const std::function<void()> &flush);
	u32 MapExtra(u32 bufpos, u32 sz, const std::function<void()> &flush);

	enum {
		// These numbers kept low because we only have 24 MB of user memory to map into.
		SLAB_SIZE = 1 * 1024 * 1024,
		// 10 is the number of texture units + verts + inds.
		// In the worst case, we could concurrently need 10 slabs/extras at the same time.
		SLAB_COUNT = 10,
		EXTRA_COUNT = 10,
	};

	// The current "generation".  Static simply as a convenience for access.
	// This increments on every allocation, for a simple LRU.
	static int slabGeneration_;

	// An aligned large mapping of the pushbuffer in PSP RAM.
	struct SlabInfo {
		u32 psp_pointer_;
		u32 buf_pointer_;
		int last_used_;

		bool Matches(u32 bufpos) {
			// We check psp_pointer_ because bufpos = 0 is valid, and the initial value.
			return buf_pointer_ == bufpos && psp_pointer_ != 0;
		}

		// Automatically marks used for LRU purposes.
		u32 Ptr(u32 bufpos) {
			last_used_ = slabGeneration_;
			return psp_pointer_ + (bufpos - buf_pointer_);
		}

		int Age() const {
			// If not allocated, it's as expired as it's gonna get.
			if (psp_pointer_ == 0)
				return std::numeric_limits<int>::max();
			return slabGeneration_ - last_used_;
		}

		bool Alloc();
		void Free();
		bool Setup(u32 bufpos);
	};

	// An adhoc mapping of the pushbuffer (either larger than a slab or straddling slabs.)
	// Remember: texture data, verts, etc. must be contiguous.
	struct ExtraInfo {
		u32 psp_pointer_;
		u32 buf_pointer_;
		u32 size_;

		bool Matches(u32 bufpos, u32 sz) {
			// We check psp_pointer_ because bufpos = 0 is valid, and the initial value.
			return buf_pointer_ == bufpos && psp_pointer_ != 0 && size_ >= sz;
		}

		u32 Ptr() {
			return psp_pointer_;
		}

		bool Alloc(u32 bufpos, u32 sz);
		void Free();
	};

	SlabInfo slabs_[SLAB_COUNT];
	u32 extraOffset_ = 0;
	ExtraInfo extra_[EXTRA_COUNT];
};

static BufMapping execMapping;

u32 BufMapping::Map(u32 bufpos, u32 sz, const std::function<void()> &flush) {
	int slab1 = bufpos / SLAB_SIZE;
	int slab2 = (bufpos + sz - 1) / SLAB_SIZE;

	if (slab1 == slab2) {
		// Doesn't straddle, so we can just map to a slab.
		return MapSlab(bufpos, flush);
	} else {
		// We need contiguous, so we'll just allocate separately.
		return MapExtra(bufpos, sz, flush);
	}
}

u32 BufMapping::MapSlab(u32 bufpos, const std::function<void()> &flush) {
	u32 slab_pos = (bufpos / SLAB_SIZE) * SLAB_SIZE;

	int best = 0;
	for (int i = 0; i < SLAB_COUNT; ++i) {
		if (slabs_[i].Matches(slab_pos)) {
			return slabs_[i].Ptr(bufpos);
		}

		if (slabs_[i].Age() > slabs_[best].Age()) {
			best = i;
		}
	}

	// Stall before mapping a new slab.
	flush();

	// Okay, we need to allocate.
	if (!slabs_[best].Setup(slab_pos)) {
		return 0;
	}
	return slabs_[best].Ptr(bufpos);
}

u32 BufMapping::MapExtra(u32 bufpos, u32 sz, const std::function<void()> &flush) {
	for (int i = 0; i < EXTRA_COUNT; ++i) {
		// Might be likely to reuse larger buffers straddling slabs.
		if (extra_[i].Matches(bufpos, sz)) {
			return extra_[i].Ptr();
		}
	}

	// Stall first, so we don't stomp existing RAM.
	flush();

	int i = extraOffset_;
	extraOffset_ = (extraOffset_ + 1) % EXTRA_COUNT;

	if (!extra_[i].Alloc(bufpos, sz)) {
		// Let's try to power on - hopefully none of these are still in use.
		for (int i = 0; i < EXTRA_COUNT; ++i) {
			extra_[i].Free();
		}
		if (!extra_[i].Alloc(bufpos, sz)) {
			return 0;
		}
	}
	return extra_[i].Ptr();
}

bool BufMapping::SlabInfo::Alloc() {
	u32 sz = SLAB_SIZE;
	psp_pointer_ = userMemory.Alloc(sz, false, "Slab");
	if (psp_pointer_ == -1) {
		psp_pointer_ = 0;
	}
	return psp_pointer_ != 0;
}

void BufMapping::SlabInfo::Free() {
	if (psp_pointer_) {
		userMemory.Free(psp_pointer_);
		psp_pointer_ = 0;
		buf_pointer_ = 0;
		last_used_ = 0;
	}
}

bool BufMapping::ExtraInfo::Alloc(u32 bufpos, u32 sz) {
	// Make sure we've freed any previous allocation first.
	Free();

	u32 allocSize = sz;
	psp_pointer_ = userMemory.Alloc(allocSize, false, "Straddle extra");
	if (psp_pointer_ == -1) {
		psp_pointer_ = 0;
	}
	if (psp_pointer_ == 0) {
		return false;
	}

	buf_pointer_ = bufpos;
	size_ = sz;
	Memory::MemcpyUnchecked(psp_pointer_, pushbuf.data() + bufpos, sz);
	return true;
}

void BufMapping::ExtraInfo::Free() {
	if (psp_pointer_) {
		userMemory.Free(psp_pointer_);
		psp_pointer_ = 0;
		buf_pointer_ = 0;
	}
}

bool BufMapping::SlabInfo::Setup(u32 bufpos) {
	// If it already has RAM, we're simply taking it over.  Slabs come only in one size.
	if (psp_pointer_ == 0) {
		if (!Alloc()) {
			return false;
		}
	}

	buf_pointer_ = bufpos;
	u32 sz = std::min((u32)SLAB_SIZE, (u32)pushbuf.size() - bufpos);
	Memory::MemcpyUnchecked(psp_pointer_, pushbuf.data() + bufpos, sz);

	slabGeneration_++;
	last_used_ = slabGeneration_;
	return true;
}

int BufMapping::slabGeneration_ = 0;

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

static std::string GenRecordingFilename() {
	const std::string dumpDir = GetSysDirectory(DIRECTORY_DUMP);
	const std::string prefix = dumpDir + g_paramSFO.GetDiscID();

	File::CreateFullPath(dumpDir);

	for (int n = 1; n < 10000; ++n) {
		std::string filename = StringFromFormat("%s_%04d.ppdmp", prefix.c_str(), n);
		if (!File::Exists(filename)) {
			return filename;
		}
	}

	return StringFromFormat("%s_%04d.ppdmp", prefix.c_str(), 9999);
}

static void BeginRecording() {
	active = true;
	nextFrame = false;
	lastTextures.clear();
	lastRenderTargets.clear();
	flipLastAction = gpuStats.numFlips;

	u32 ptr = (u32)pushbuf.size();
	u32 sz = 512 * 4;
	pushbuf.resize(pushbuf.size() + sz);
	gstate.Save((u32_le *)(pushbuf.data() + ptr));

	commands.push_back({CommandType::INIT, sz, ptr});
}

static void WriteCompressed(FILE *fp, const void *p, size_t sz) {
	size_t compressed_size = snappy_max_compressed_length(sz);
	u8 *compressed = new u8[compressed_size];
	snappy_compress((const char *)p, sz, (char *)compressed, &compressed_size);

	u32 write_size = (u32)compressed_size;
	fwrite(&write_size, sizeof(write_size), 1, fp);
	fwrite(compressed, compressed_size, 1, fp);

	delete [] compressed;
}

static std::string WriteRecording() {
	FlushRegisters();

	const std::string filename = GenRecordingFilename();

	NOTICE_LOG(G3D, "Recording filename: %s", filename.c_str());

	FILE *fp = File::OpenCFile(filename, "wb");
	fwrite(HEADER, 8, 1, fp);
	fwrite(&VERSION, sizeof(VERSION), 1, fp);

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

static const u8 *mymemmem(const u8 *haystack, size_t hlen, const u8 *needle, size_t nlen) {
	if (!nlen) {
		return nullptr;
	}

	const u8 *last_possible = haystack + hlen - nlen;
	int first = *needle;
	const u8 *p = haystack;
	while (p <= last_possible) {
		p = (const u8 *)memchr(p, first, last_possible - p + 1);
		if (!p) {
			return nullptr;
		}
		if (!memcmp(p, needle, nlen)) {
			return p;
		}

		p++;
	}

	return nullptr;
}

static Command EmitCommandWithRAM(CommandType t, const void *p, u32 sz) {
	FlushRegisters();

	Command cmd{t, sz, 0};

	if (sz) {
		// If at all possible, try to find it already in the buffer.
		const u8 *prev = nullptr;
		const size_t NEAR_WINDOW = std::max((int)sz * 2, 1024 * 10);
		// Let's try nearby first... it will often be nearby.
		if (pushbuf.size() > NEAR_WINDOW) {
			prev = mymemmem(pushbuf.data() + pushbuf.size() - NEAR_WINDOW, NEAR_WINDOW, (const u8 *)p, sz);
		}
		if (!prev) {
			prev = mymemmem(pushbuf.data(), pushbuf.size(), (const u8 *)p, sz);
		}

		if (prev) {
			cmd.ptr = (u32)(prev - pushbuf.data());
		} else {
			cmd.ptr = (u32)pushbuf.size();
			int pad = 0;
			if (cmd.ptr & 0xF) {
				pad = 0x10 - (cmd.ptr & 0xF);
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

static void EmitTextureData(int level, u32 texaddr) {
	GETextureFormat format = gstate.getTextureFormat();
	int w = gstate.getTextureWidth(level);
	int h = gstate.getTextureHeight(level);
	int bufw = GetTextureBufw(level, texaddr, format);
	int extraw = w > bufw ? w - bufw : 0;
	u32 sizeInRAM = (textureBitsPerPixel[format] * (bufw * h + extraw)) / 8;
	const bool isTarget = lastRenderTargets.find(texaddr) != lastRenderTargets.end();

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

		// The isTarget flag is mostly used for replay of dumps on a PSP.
		u32 flags = isTarget ? 1 : 0;
		FramebufData framebuf{ texaddr, bufw, flags };
		framebufData.resize(sizeof(framebuf) + bytes);
		memcpy(&framebufData[0], &framebuf, sizeof(framebuf));
		memcpy(&framebufData[sizeof(framebuf)], p, bytes);
		p = &framebufData[0];

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
		Command cmd = EmitCommandWithRAM(type, p, bytes);
		lastTextures.push_back(cmd.ptr);
	}
}

static void FlushPrimState(int vcount) {
	// TODO: Eventually, how do we handle texturing from framebuf/zbuf?
	// TODO: Do we need to preload color/depth/stencil (in case from last frame)?

	lastRenderTargets.insert(PSP_GetVidMemBase() | gstate.getFrameBufRawAddress());
	lastRenderTargets.insert(PSP_GetVidMemBase() | gstate.getDepthBufRawAddress());

	// We re-flush textures always in case the game changed them... kinda expensive.
	// TODO: Dirty textures on transfer/stall/etc. somehow?
	// TODO: Or maybe de-dup by validating if it has changed?
	for (int level = 0; level < 8; ++level) {
		u32 texaddr = gstate.getTextureAddress(level);
		if (texaddr) {
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
		EmitCommandWithRAM(CommandType::INDICES, indices, ibytes);
	}
	if (verts && vbytes > 0) {
		EmitCommandWithRAM(CommandType::VERTICES, verts, vbytes);
	}
}

static void EmitTransfer(u32 op) {
	FlushRegisters();

	// This may not make a lot of sense right now, unless it's to a framebuf...
	if (!Memory::IsVRAMAddress(gstate.getTransferDstAddress())) {
		// Skip, not VRAM, so can't affect drawing (we flush textures each prim.)
		return;
	}

	u32 srcBasePtr = gstate.getTransferSrcAddress();
	u32 srcStride = gstate.getTransferSrcStride();
	int srcX = gstate.getTransferSrcX();
	int srcY = gstate.getTransferSrcY();
	int width = gstate.getTransferWidth();
	int height = gstate.getTransferHeight();
	int bpp = gstate.getTransferBpp();

	u32 srcBytes = ((srcY + height - 1) * srcStride + (srcX + width)) * bpp;
	srcBytes = Memory::ValidSize(srcBasePtr, srcBytes);

	if (srcBytes != 0) {
		EmitCommandWithRAM(CommandType::TRANSFERSRC, Memory::GetPointerUnchecked(srcBasePtr), srcBytes);
	}

	lastRegisters.push_back(op);
}

static void EmitClut(u32 op) {
	u32 addr = gstate.getClutAddress();
	u32 bytes = (op & 0x3F) * 32;
	bytes = Memory::ValidSize(addr, bytes);

	if (bytes != 0) {
		EmitCommandWithRAM(CommandType::CLUT, Memory::GetPointerUnchecked(addr), bytes);
	}

	lastRegisters.push_back(op);
}

static void EmitPrim(u32 op) {
	FlushPrimState(op & 0x0000FFFF);

	lastRegisters.push_back(op);
}

static void EmitBezierSpline(u32 op) {
	int ucount = op & 0xFF;
	int vcount = (op >> 8) & 0xFF;
	FlushPrimState(ucount * vcount);

	lastRegisters.push_back(op);
}

bool IsActive() {
	return active;
}

bool IsActivePending() {
	return nextFrame || active;
}

bool Activate() {
	if (!nextFrame) {
		nextFrame = true;
		flipLastAction = gpuStats.numFlips;
		return true;
	}
	return false;
}

void SetCallback(const std::function<void(const std::string &)> callback) {
	writeCallback = callback;
}

static void FinishRecording() {
	// We're done - this was just to write the result out.
	std::string filename = WriteRecording();
	commands.clear();
	pushbuf.clear();

	NOTICE_LOG(SYSTEM, "Recording finished");
	active = false;
	flipLastAction = gpuStats.numFlips;

	if (writeCallback)
		writeCallback(filename);
	writeCallback = nullptr;
}

void NotifyCommand(u32 pc) {
	if (!active) {
		return;
	}

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
	if (Memory::IsVRAMAddress(dest)) {
		FlushRegisters();
		Command cmd{CommandType::MEMCPYDEST, sizeof(dest), (u32)pushbuf.size()};
		pushbuf.resize(pushbuf.size() + sizeof(dest));
		memcpy(pushbuf.data() + cmd.ptr, &dest, sizeof(dest));

		sz = Memory::ValidSize(dest, sz);
		if (sz != 0) {
			EmitCommandWithRAM(CommandType::MEMCPYDATA, Memory::GetPointer(dest), sz);
		}
	}
}

void NotifyMemset(u32 dest, int v, u32 sz) {
	if (!active) {
		return;
	}
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
	}
}

void NotifyUpload(u32 dest, u32 sz) {
	if (!active) {
		return;
	}
	NotifyMemcpy(dest, dest, sz);
}

void NotifyDisplay(u32 framebuf, int stride, int fmt) {
	bool writePending = false;
	if (active && !commands.empty()) {
		writePending = true;
	}
	if (nextFrame && (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0) {
		NOTICE_LOG(SYSTEM, "Recording starting on display...");
		BeginRecording();
	}
	if (!active) {
		return;
	}

	struct DisplayBufData {
		PSPPointer<u8> topaddr;
		int linesize, pixelFormat;
	};

	DisplayBufData disp{ framebuf, stride, fmt };

	FlushRegisters();
	u32 ptr = (u32)pushbuf.size();
	u32 sz = (u32)sizeof(disp);
	pushbuf.resize(pushbuf.size() + sz);
	memcpy(pushbuf.data() + ptr, &disp, sz);

	commands.push_back({ CommandType::DISPLAY, sz, ptr });

	if (writePending) {
		NOTICE_LOG(SYSTEM, "Recording complete on display");
		FinishRecording();
	}
}

void NotifyFrame() {
	const bool noDisplayAction = flipLastAction + 4 < gpuStats.numFlips;
	// We do this only to catch things that don't call NotifyDisplay.
	if (active && !commands.empty() && noDisplayAction) {
		NOTICE_LOG(SYSTEM, "Recording complete on frame");

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
	if (nextFrame && (gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0 && noDisplayAction) {
		NOTICE_LOG(SYSTEM, "Recording starting on frame...");
		BeginRecording();
	}
}

void DumpExecute::SyncStall() {
	gpu->UpdateStall(execListID, execListPos);
	s64 listTicks = gpu->GetListTicks(execListID);
	if (listTicks != -1) {
		s64 nowTicks = CoreTiming::GetTicks();
		if (listTicks > nowTicks) {
			currentMIPS->downcount -= listTicks - nowTicks;
		}
	}

	// Make sure downcount doesn't overflow.
	CoreTiming::ForceCheck();
}

bool DumpExecute::SubmitCmds(void *p, u32 sz) {
	if (execListBuf == 0) {
		u32 allocSize = LIST_BUF_SIZE;
		execListBuf = userMemory.Alloc(allocSize, "List buf");
		if (execListBuf == -1) {
			execListBuf = 0;
		}
		if (execListBuf == 0) {
			ERROR_LOG(SYSTEM, "Unable to allocate for display list");
			return false;
		}

		execListPos = execListBuf;
		Memory::Write_U32(GE_CMD_NOP << 24, execListPos);
		execListPos += 4;

		gpu->EnableInterrupts(false);
		auto optParam = PSPPointer<PspGeListArgs>::Create(0);
		execListID = gpu->EnqueueList(execListBuf, execListPos, -1, optParam, false);
		gpu->EnableInterrupts(true);
	}

	u32 pendingSize = (int)execListQueue.size() * sizeof(u32);
	// Validate space for jump.
	u32 allocSize = pendingSize + sz + 8;
	if (execListPos + allocSize >= execListBuf + LIST_BUF_SIZE) {
		Memory::Write_U32((GE_CMD_BASE << 24) | ((execListBuf >> 8) & 0x00FF0000), execListPos);
		Memory::Write_U32((GE_CMD_JUMP << 24) | (execListBuf & 0x00FFFFFF), execListPos + 4);

		execListPos = execListBuf;

		// Don't continue until we've stalled.
		SyncStall();
	}

	Memory::MemcpyUnchecked(execListPos, execListQueue.data(), pendingSize);
	execListPos += pendingSize;
	u32 writePos = execListPos;
	Memory::MemcpyUnchecked(execListPos, p, sz);
	execListPos += sz;

	// TODO: Unfortunate.  Maybe Texture commands should contain the bufw instead.
	// The goal here is to realistically combine prims in dumps.  Stalling for the bufw flushes.
	u32_le *ops = (u32_le *)Memory::GetPointer(writePos);
	for (u32 i = 0; i < sz / 4; ++i) {
		u32 cmd = ops[i] >> 24;
		if (cmd >= GE_CMD_TEXBUFWIDTH0 && cmd <= GE_CMD_TEXBUFWIDTH7) {
			int level = cmd - GE_CMD_TEXBUFWIDTH0;
			u16 bufw = ops[i] & 0xFFFF;

			// NOP the address part of the command to avoid a flush too.
			if (bufw == lastBufw_[level])
				ops[i] = GE_CMD_NOP << 24;
			else
				ops[i] = (gstate.texbufwidth[level] & 0xFFFF0000) | bufw;
			lastBufw_[level] = bufw;
		}

		// Since we're here anyway, also NOP out texture addresses.
		// This makes Step Tex not hit phantom textures.
		if (cmd >= GE_CMD_TEXADDR0 && cmd <= GE_CMD_TEXADDR7) {
			ops[i] = GE_CMD_NOP << 24;
		}
	}

	execListQueue.clear();

	return true;
}

void DumpExecute::SubmitListEnd() {
	if (execListPos == 0) {
		return;
	}

	// There's always space for the end, same size as a jump.
	Memory::Write_U32(GE_CMD_FINISH << 24, execListPos);
	Memory::Write_U32(GE_CMD_END << 24, execListPos + 4);
	execListPos += 8;

	SyncStall();
	gpu->ListSync(execListID, 0);
}

void DumpExecute::Init(u32 ptr, u32 sz) {
	gstate.Restore((u32_le *)(pushbuf.data() + ptr));
	gpu->ReapplyGfxState();
}

void DumpExecute::Registers(u32 ptr, u32 sz) {
	SubmitCmds(pushbuf.data() + ptr, sz);
}

void DumpExecute::Vertices(u32 ptr, u32 sz) {
	u32 psp = execMapping.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for vertices");
		return;
	}

	execListQueue.push_back((GE_CMD_BASE << 24) | ((psp >> 8) & 0x00FF0000));
	execListQueue.push_back((GE_CMD_VADDR << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::Indices(u32 ptr, u32 sz) {
	u32 psp = execMapping.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for indices");
		return;
	}

	execListQueue.push_back((GE_CMD_BASE << 24) | ((psp >> 8) & 0x00FF0000));
	execListQueue.push_back((GE_CMD_IADDR << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::Clut(u32 ptr, u32 sz) {
	u32 psp = execMapping.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for clut");
		return;
	}

	execListQueue.push_back((GE_CMD_CLUTADDRUPPER << 24) | ((psp >> 8) & 0x00FF0000));
	execListQueue.push_back((GE_CMD_CLUTADDR << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::TransferSrc(u32 ptr, u32 sz) {
	u32 psp = execMapping.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for transfer");
		return;
	}

	// Need to sync in order to access gstate.transfersrcw.
	SyncStall();

	execListQueue.push_back((gstate.transfersrcw & 0xFF00FFFF) | ((psp >> 8) & 0x00FF0000));
	execListQueue.push_back(((GE_CMD_TRANSFERSRC) << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::Memset(u32 ptr, u32 sz) {
	struct MemsetCommand {
		u32 dest;
		int value;
		u32 sz;
	};

	const MemsetCommand *data = (const MemsetCommand *)(pushbuf.data() + ptr);

	if (Memory::IsVRAMAddress(data->dest)) {
		SyncStall();
		gpu->PerformMemorySet(data->dest, (u8)data->value, data->sz);
	}
}

void DumpExecute::MemcpyDest(u32 ptr, u32 sz) {
	execMemcpyDest = *(const u32 *)(pushbuf.data() + ptr);
}

void DumpExecute::Memcpy(u32 ptr, u32 sz) {
	if (Memory::IsVRAMAddress(execMemcpyDest)) {
		SyncStall();
		Memory::MemcpyUnchecked(execMemcpyDest, pushbuf.data() + ptr, sz);
		gpu->PerformMemoryUpload(execMemcpyDest, sz);
	}
}

void DumpExecute::Texture(int level, u32 ptr, u32 sz) {
	u32 psp = execMapping.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for texture");
		return;
	}

	u32 bufwCmd = GE_CMD_TEXBUFWIDTH0 + level;
	u32 addrCmd = GE_CMD_TEXADDR0 + level;
	execListQueue.push_back((bufwCmd << 24) | ((psp >> 8) & 0x00FF0000) | lastBufw_[level]);
	execListQueue.push_back((addrCmd << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::Framebuf(int level, u32 ptr, u32 sz) {
	struct FramebufData {
		u32 addr;
		int bufw;
		u32 flags;
		u32 pad;
	};

	FramebufData *framebuf = (FramebufData *)(pushbuf.data() + ptr);

	u32 bufwCmd = GE_CMD_TEXBUFWIDTH0 + level;
	u32 addrCmd = GE_CMD_TEXADDR0 + level;
	execListQueue.push_back((bufwCmd << 24) | ((framebuf->addr >> 8) & 0x00FF0000) | framebuf->bufw);
	execListQueue.push_back((addrCmd << 24) | (framebuf->addr & 0x00FFFFFF));
	lastBufw_[level] = framebuf->bufw;

	// And now also copy the data into VRAM (in case it wasn't actually rendered.)
	u32 headerSize = (u32)sizeof(FramebufData);
	u32 pspSize = sz - headerSize;
	const bool isTarget = (framebuf->flags & 1) != 0;
	// Could potentially always skip if !isTarget, but playing it safe for offset texture behavior.
	if (Memory::IsValidRange(framebuf->addr, pspSize) && (!isTarget || !g_Config.bSoftwareRendering)) {
		// Intentionally don't trigger an upload here.
		Memory::MemcpyUnchecked(framebuf->addr, pushbuf.data() + ptr + headerSize, pspSize);
	}
}

void DumpExecute::Display(u32 ptr, u32 sz) {
	struct DisplayBufData {
		PSPPointer<u8> topaddr;
		int linesize, pixelFormat;
	};

	DisplayBufData *disp = (DisplayBufData *)(pushbuf.data() + ptr);

	// Sync up drawing.
	SyncStall();

	__DisplaySetFramebuf(disp->topaddr.ptr, disp->linesize, disp->pixelFormat, 1);
	__DisplaySetFramebuf(disp->topaddr.ptr, disp->linesize, disp->pixelFormat, 0);
}

DumpExecute::~DumpExecute() {
	execMemcpyDest = 0;
	if (execListBuf) {
		userMemory.Free(execListBuf);
		execListBuf = 0;
	}
	execListPos = 0;
	execMapping.Reset();

	commands.clear();
	pushbuf.clear();
}

bool DumpExecute::Run() {
	for (const Command &cmd : commands) {
		switch (cmd.type) {
		case CommandType::INIT:
			Init(cmd.ptr, cmd.sz);
			break;

		case CommandType::REGISTERS:
			Registers(cmd.ptr, cmd.sz);
			break;

		case CommandType::VERTICES:
			Vertices(cmd.ptr, cmd.sz);
			break;

		case CommandType::INDICES:
			Indices(cmd.ptr, cmd.sz);
			break;

		case CommandType::CLUT:
			Clut(cmd.ptr, cmd.sz);
			break;

		case CommandType::TRANSFERSRC:
			TransferSrc(cmd.ptr, cmd.sz);
			break;

		case CommandType::MEMSET:
			Memset(cmd.ptr, cmd.sz);
			break;

		case CommandType::MEMCPYDEST:
			MemcpyDest(cmd.ptr, cmd.sz);
			break;

		case CommandType::MEMCPYDATA:
			Memcpy(cmd.ptr, cmd.sz);
			break;

		case CommandType::TEXTURE0:
		case CommandType::TEXTURE1:
		case CommandType::TEXTURE2:
		case CommandType::TEXTURE3:
		case CommandType::TEXTURE4:
		case CommandType::TEXTURE5:
		case CommandType::TEXTURE6:
		case CommandType::TEXTURE7:
			Texture((int)cmd.type - (int)CommandType::TEXTURE0, cmd.ptr, cmd.sz);
			break;

		case CommandType::FRAMEBUF0:
		case CommandType::FRAMEBUF1:
		case CommandType::FRAMEBUF2:
		case CommandType::FRAMEBUF3:
		case CommandType::FRAMEBUF4:
		case CommandType::FRAMEBUF5:
		case CommandType::FRAMEBUF6:
		case CommandType::FRAMEBUF7:
			Framebuf((int)cmd.type - (int)CommandType::FRAMEBUF0, cmd.ptr, cmd.sz);
			break;

		case CommandType::DISPLAY:
			Display(cmd.ptr, cmd.sz);
			break;

		default:
			ERROR_LOG(SYSTEM, "Unsupported GE dump command: %d", (int)cmd.type);
			return false;
		}
	}

	SubmitListEnd();
	return true;
}

static bool ReadCompressed(u32 fp, void *dest, size_t sz) {
	u32 compressed_size = 0;
	if (pspFileSystem.ReadFile(fp, (u8 *)&compressed_size, sizeof(compressed_size)) != sizeof(compressed_size)) {
		return false;
	}

	u8 *compressed = new u8[compressed_size];
	if (pspFileSystem.ReadFile(fp, compressed, compressed_size) != compressed_size) {
		delete [] compressed;
		return false;
	}

	size_t real_size = sz;
	snappy_uncompress((const char *)compressed, compressed_size, (char *)dest, &real_size);
	delete [] compressed;

	return real_size == sz;
}

bool RunMountedReplay(const std::string &filename) {
	_assert_msg_(SYSTEM, !active && !nextFrame, "Cannot run replay while recording.");

	u32 fp = pspFileSystem.OpenFile(filename, FILEACCESS_READ);
	u8 header[8]{};
	int version = 0;
	pspFileSystem.ReadFile(fp, header, sizeof(header));
	pspFileSystem.ReadFile(fp, (u8 *)&version, sizeof(version));

	if (memcmp(header, HEADER, sizeof(header)) != 0 || version > VERSION || version < MIN_VERSION) {
		ERROR_LOG(SYSTEM, "Invalid GE dump or unsupported version");
		pspFileSystem.CloseFile(fp);
		return false;
	}

	u32 sz = 0;
	pspFileSystem.ReadFile(fp, (u8 *)&sz, sizeof(sz));
	u32 bufsz = 0;
	pspFileSystem.ReadFile(fp, (u8 *)&bufsz, sizeof(bufsz));

	commands.resize(sz);
	pushbuf.resize(bufsz);

	bool truncated = false;
	truncated = truncated || !ReadCompressed(fp, commands.data(), sizeof(Command) * sz);
	truncated = truncated || !ReadCompressed(fp, pushbuf.data(), bufsz);

	pspFileSystem.CloseFile(fp);

	if (truncated) {
		ERROR_LOG(SYSTEM, "Truncated GE dump");
		return false;
	}

	DumpExecute executor;
	return executor.Run();
}

};

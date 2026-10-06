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
#include <climits>
#include <cstring>
#include <functional>
#include <mutex>
#include <map>
#include <condition_variable>
#include <vector>
#include <thread>
#include <snappy-c.h>
#include <zstd.h>

#include "Common/Profiler/Profiler.h"
#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/System/Request.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/System.h"
#include "Core/Util/GameDB.h"
#include "Core/Util/PathUtil.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Debugger/Playback.h"
#include "GPU/Debugger/Record.h"
#include "GPU/Debugger/RecordFormat.h"

namespace GPURecord {

// Provide the illusion of synchronous execution, although the playback is actually running on a different thread.
enum class OpType {
	None,
	UpdateStallAddr,
	EnqueueList,
	ListSync,
	ReapplyGfxState,
	Done,
};

static const char *OpTypeToString(OpType type) {
	switch (type) {
	case OpType::None: return "None";
	case OpType::UpdateStallAddr: return "UpdateStallAddr";
	case OpType::EnqueueList: return "EnqueueList";
	case OpType::ListSync: return "ListSync";
	case OpType::ReapplyGfxState: return "ReapplyGfxState";
	case OpType::Done: return "Done";
	default: return "N/A";
	}
}

struct Operation {
	OpType type;
	u32 listID;  // also listPC in EnqueueList
	u32 param;  // stallAddr generally
};

static std::string lastExecFilename;
static uint32_t lastExecVersion;
static std::vector<Command> lastExecCommands;
static std::vector<u8> lastExecPushbuf;

// This thread is restarted every frame (dump execution) for simplicity. TODO: Make persistent?
// Alternatively, get rid of it, but the code is written in a way that makes it difficult (you'll see if you try).
static std::thread replayThread;

static std::mutex opStartLock;
static std::condition_variable g_condOpStartWait;

static std::mutex opFinishLock;
static std::condition_variable opFinishWait;

static Operation g_opToExec;
static u32 g_retVal;
static bool g_opDone = true;
static bool g_cancelled = false;
static int g_drawLimit = 0;

void SetReplayDrawLimit(int lastPrim) {
	g_drawLimit = lastPrim;
}

// Runs on operation thread
u32 ExecuteOnMain(Operation opToExec) {
	{
		std::unique_lock<std::mutex> startLock(opStartLock);
		g_opToExec = opToExec;
		g_retVal = 0;
		g_opDone = false;
		g_condOpStartWait.notify_one();
	}

	// now wait for completion. At that point, noone cares about g_opToExec anymore, and we can safely
	// overwrite it next time.
	{
		std::unique_lock<std::mutex> lock(opFinishLock);
		opFinishWait.wait(lock, []() { return g_opDone || g_cancelled; });
	}
	return g_retVal;
}

// This class maps pushbuffer (dump data) sections to PSP memory.
// Dumps can be larger than available PSP memory, because they include generated data too.
//
// If possible, it maps to dynamically allocated "slabs" so nearby access is fast.
// Otherwise it uses "extra" allocations to manage sections that straddle two slabs.
// Slabs are managed with LRU, extra buffers are round-robin.
class BufMapping {
public:
	BufMapping(const std::vector<u8> &pushbuf) : pushbuf_(pushbuf) {
	}

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
		u32 psp_pointer_ = 0;
		u32 buf_pointer_ = 0;
		int last_used_ = 0;

		bool Matches(u32 bufpos) const {
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
		bool Setup(u32 bufpos, const std::vector<u8> &pushbuf_);
	};

	// An adhoc mapping of the pushbuffer (either larger than a slab or straddling slabs.)
	// Remember: texture data, verts, etc. must be contiguous.
	struct ExtraInfo {
		u32 psp_pointer_ = 0;
		u32 buf_pointer_ = 0;
		u32 size_ = 0;

		bool Matches(u32 bufpos, u32 sz) const {
			// We check psp_pointer_ because bufpos = 0 is valid, and the initial value.
			return buf_pointer_ == bufpos && psp_pointer_ != 0 && size_ >= sz;
		}

		u32 Ptr() const {
			return psp_pointer_;
		}

		bool Alloc(u32 bufpos, u32 sz, const std::vector<u8> &pushbuf_);
		void Free();
	};

	SlabInfo slabs_[SLAB_COUNT]{};
	u32 lastSlab_ = 0;
	u32 extraOffset_ = 0;
	ExtraInfo extra_[EXTRA_COUNT]{};

	const std::vector<u8> &pushbuf_;
};

u32 BufMapping::Map(u32 bufpos, u32 sz, const std::function<void()> &flush) {
	int slab1 = bufpos / SLAB_SIZE;
	int slab2 = (bufpos + sz - 1) / SLAB_SIZE;

	if (slab1 == slab2) {
		// Shortcut in case it's simply the most recent slab.
		if (slabs_[lastSlab_].Matches(slab1 * SLAB_SIZE))
			return slabs_[lastSlab_].Ptr(bufpos);
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
	if (!slabs_[best].Setup(slab_pos, pushbuf_)) {
		return 0;
	}
	lastSlab_ = best;
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

	if (!extra_[i].Alloc(bufpos, sz, pushbuf_)) {
		// Let's try to power on - hopefully none of these are still in use.
		for (int i = 0; i < EXTRA_COUNT; ++i) {
			extra_[i].Free();
		}
		if (!extra_[i].Alloc(bufpos, sz, pushbuf_)) {
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

bool BufMapping::ExtraInfo::Alloc(u32 bufpos, u32 sz, const std::vector<u8> &pushbuf_) {
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
	Memory::MemcpyUnchecked(psp_pointer_, pushbuf_.data() + bufpos, sz);
	return true;
}

void BufMapping::ExtraInfo::Free() {
	if (psp_pointer_) {
		userMemory.Free(psp_pointer_);
		psp_pointer_ = 0;
		buf_pointer_ = 0;
	}
}

bool BufMapping::SlabInfo::Setup(u32 bufpos, const std::vector<u8> &pushbuf_) {
	// If it already has RAM, we're simply taking it over.  Slabs come only in one size.
	if (psp_pointer_ == 0) {
		if (!Alloc()) {
			return false;
		}
	}

	buf_pointer_ = bufpos;
	u32 sz = std::min((u32)SLAB_SIZE, (u32)pushbuf_.size() - bufpos);
	Memory::MemcpyUnchecked(psp_pointer_, pushbuf_.data() + bufpos, sz);

	slabGeneration_++;
	last_used_ = slabGeneration_;
	return true;
}

int BufMapping::slabGeneration_ = 0;

class DumpExecute {
public:
	DumpExecute(const std::vector<u8> &pushbuf, const std::vector<Command> &commands, uint32_t version)
		: pushbuf_(pushbuf), commands_(commands), mapping_(pushbuf), version_(version) {
	}
	~DumpExecute();

	ReplayResult Run();

private:
	void SyncStall();
	void SubmitListEnd();

	void Init(u32 ptr, u32 sz);
	void Registers(u32 ptr, u32 sz);
	void Vertices(u32 ptr, u32 sz);
	void Indices(u32 ptr, u32 sz);
	void ClutAddr(u32 ptr, u32 sz);
	void Clut(u32 ptr, u32 sz);
	void TransferSrc(u32 ptr, u32 sz);
	void Memset(u32 ptr, u32 sz);
	void MemcpyDest(u32 ptr, u32 sz);
	void Memcpy(u32 ptr, u32 sz);
	void Texture(int level, u32 ptr, u32 sz);
	void Framebuf(int level, u32 ptr, u32 sz);
	void Display(u32 ptr, u32 sz, bool allowFlip);
	void EdramTrans(u32 ptr, u32 sz);

	u32 execMemcpyDest = 0;
	u32 execClutAddr = 0;
	u32 execClutFlags = 0;
	u32 execListBuf = 0;
	u32 execListPos = 0;
	u32 execListID = 0;
	const int LIST_BUF_SIZE = 256 * 1024;
	std::vector<u32> execListQueue;
	u16 lastBufw_[8]{};
	u32 lastTex_[8]{};
	int prims_ = 0;
	u32 lastBase_ = 0;
	// The framebuffers drawn to so far by VRAM offset, with the areas the draws could reach, and the
	// current one's registers.
	struct DrawnRect {
		int x1, y1, x2, y2;
		bool Contains(int x, int y) const {
			return x >= x1 && x <= x2 && y >= y1 && y <= y2;
		}
	};
	struct DrawnTarget {
		u32 strideBytes;
		u32 bpp;
		std::vector<DrawnRect> rects;
	};
	std::map<u32, DrawnTarget> drawnTargets_;
	u32 fbPtr_ = 0;
	u32 fbWidth_ = 0;
	u32 fbFormat_ = 0;
	u32 region2_ = 0;
	u32 scissor2_ = 0;
	u32 vertType_ = 0;
	u32 zbPtr_ = 0;
	u32 zbWidth_ = 0;
	bool zTest_ = false;
	bool zWriteDisable_ = false;
	u32 clearMode_ = 0;
	// The vertices recorded for the next draw.
	u32 lastVertsPtr_ = 0;
	u32 lastVertsSize_ = 0;

	void MarkDrawn(u32 prim);
	void CopyAroundDrawn(u32 addr, const u8 *data, u32 size);

	const std::vector<u8> &pushbuf_;
	const std::vector<Command> &commands_;
	BufMapping mapping_;
	uint32_t version_ = 0;

	int resumeIndex_ = -1;
};

void DumpExecute::SyncStall() {
	if (execListBuf == 0) {
		VERBOSE_LOG(Log::GeDebugger, "SyncStall: No active display list");
		return;
	}

	ExecuteOnMain(Operation{ OpType::UpdateStallAddr, execListID, execListPos });

	s64 listTicks = gpu->GetListTicks(execListID);
	if (listTicks != -1) {
		s64 nowTicks = CoreTiming::GetTicks(currentMIPS);
		if (listTicks > nowTicks) {
			currentMIPS->downcount -= listTicks - nowTicks;
		}
	}

	// Make sure downcount doesn't overflow. (can this even happen?)
	// Also this doesn't do anything in this context, we don't reschedule... or at least
	// aren't supposed to.
	// CoreTiming::ForceCheck();
}

void DumpExecute::Registers(u32 ptr, u32 sz) {
	if (execListBuf == 0) {
		u32 allocSize = LIST_BUF_SIZE;
		execListBuf = userMemory.Alloc(allocSize, true, "List buf");
		if (execListBuf == -1) {
			execListBuf = 0;
		}
		if (execListBuf == 0) {
			ERROR_LOG(Log::GeDebugger, "Unable to allocate for display list");
			return;
		}

		execListPos = execListBuf;
		Memory::WriteUnchecked_U32(GE_CMD_NOP << 24, execListPos);
		execListPos += 4;

		// TODO: Why do we disable interrupts here?
		gpu->EnableInterrupts(false);
		execListID = ExecuteOnMain(Operation{ OpType::EnqueueList, execListBuf, execListPos });
		gpu->EnableInterrupts(true);
	}

	u32 pendingSize = (u32)execListQueue.size() * sizeof(u32);
	// Validate space for jump.
	u32 allocSize = pendingSize + sz + 8;
	if (execListPos + allocSize >= execListBuf + LIST_BUF_SIZE) {
		Memory::WriteUnchecked_U32((GE_CMD_BASE << 24) | ((execListBuf >> 8) & 0x00FF0000), execListPos);
		Memory::WriteUnchecked_U32((GE_CMD_JUMP << 24) | (execListBuf & 0x00FFFFFF), execListPos + 4);

		execListPos = execListBuf;
		// The queued vertex, index and texture addresses were made for the previous base, which the jump
		// just replaced (Tiger Woods ULUS10420 then read draw 21026's indices from 16 MB away).
		if (lastBase_ != 0xFFFFFFFF && lastBase_ != (execListBuf & 0xFF000000)) {
			Memory::WriteUnchecked_U32((GE_CMD_BASE << 24) | ((lastBase_ >> 8) & 0x00FF0000), execListPos);
			execListPos += 4;
		} else {
			lastBase_ = execListBuf & 0xFF000000;
		}

		// Don't continue until we've stalled.
		// TODO: Is this really needed? It seems fine without it.
		SyncStall();
	}

	Memory::MemcpyUnchecked(execListPos, execListQueue.data(), pendingSize);
	execListPos += pendingSize;
	u32 writePos = execListPos;
	void *srcData = (void *)(pushbuf_.data() + ptr);
	Memory::MemcpyUnchecked(execListPos, srcData, sz);
	execListPos += sz;

	// TODO: Unfortunate.  Maybe Texture commands should contain the bufw instead.
	// The goal here is to realistically combine prims in dumps.  Stalling for the bufw flushes.
	u32_le *ops = (u32_le *)Memory::GetPointerUnchecked(writePos);

	u32 lastTexHigh[8]{};
	for (int i = 0; i < 8; ++i)
		lastTexHigh[i] = ((lastTex_[i] & 0xFF000000) >> 8) | ((GE_CMD_TEXBUFWIDTH0 + i) << 24);

	for (u32 i = 0; i < sz / 4; ++i) {
		u32 cmd = ops[i] >> 24;
		if (cmd >= GE_CMD_TEXBUFWIDTH0 && cmd <= GE_CMD_TEXBUFWIDTH7) {
			int level = cmd - GE_CMD_TEXBUFWIDTH0;
			u16 bufw = ops[i] & 0xFFFF;

			// NOP the address part of the command to avoid a flush too.
			if (bufw == lastBufw_[level])
				ops[i] = GE_CMD_NOP << 24;
			else
				ops[i] = lastTexHigh[level] | bufw;
			lastBufw_[level] = bufw;
		}

		if (cmd == GE_CMD_FRAMEBUFPTR)
			fbPtr_ = ops[i] & 0x00FFFFFF;
		if (cmd == GE_CMD_FRAMEBUFWIDTH)
			fbWidth_ = ops[i] & 0x00FFFFFF;
		if (cmd == GE_CMD_FRAMEBUFPIXFORMAT)
			fbFormat_ = ops[i] & 3;
		if (cmd == GE_CMD_REGION2)
			region2_ = ops[i] & 0x000FFFFF;
		if (cmd == GE_CMD_SCISSOR2)
			scissor2_ = ops[i] & 0x000FFFFF;
		if (cmd == GE_CMD_VERTEXTYPE)
			vertType_ = ops[i] & 0x00FFFFFF;
		if (cmd == GE_CMD_ZBUFPTR)
			zbPtr_ = ops[i] & 0x00FFFFFF;
		if (cmd == GE_CMD_ZBUFWIDTH)
			zbWidth_ = ops[i] & 0x00FFFFFF;
		if (cmd == GE_CMD_ZTESTENABLE)
			zTest_ = (ops[i] & 1) != 0;
		if (cmd == GE_CMD_ZWRITEDISABLE)
			zWriteDisable_ = (ops[i] & 1) != 0;
		if (cmd == GE_CMD_CLEARMODE)
			clearMode_ = ops[i] & 0xFFFF;
		if (cmd == GE_CMD_PRIM || cmd == GE_CMD_BEZIER || cmd == GE_CMD_SPLINE) {
			MarkDrawn(cmd == GE_CMD_PRIM ? ops[i] & 0x00FFFFFF : 0);
			prims_++;
			if (g_drawLimit > 0 && prims_ > g_drawLimit)
				ops[i] = GE_CMD_NOP << 24;
		}
		// Past the limit, keep the render target too, for --screenshot-render-target.
		if (g_drawLimit > 0 && prims_ >= g_drawLimit && (cmd == GE_CMD_FRAMEBUFPTR || cmd == GE_CMD_FRAMEBUFWIDTH || cmd == GE_CMD_FRAMEBUFPIXFORMAT)) {
			ops[i] = GE_CMD_NOP << 24;
		}

		// Since we're here anyway, also NOP out texture addresses.
		// This makes Step Tex not hit phantom textures, but we rely on it for lastTex_[].
		if (cmd >= GE_CMD_TEXADDR0 && cmd <= GE_CMD_TEXADDR7) {
			ops[i] = GE_CMD_NOP << 24;
		}
		if (cmd == GE_CMD_SIGNAL || cmd == GE_CMD_BASE) {
			lastBase_ = 0xFFFFFFFF;
		}
	}

	execListQueue.clear();
}

void DumpExecute::SubmitListEnd() {
	if (execListPos == 0 || g_cancelled) {
		return;
	}

	// There's always space for the end, same size as a jump.
	Memory::WriteUnchecked_U32(GE_CMD_FINISH << 24, execListPos);
	Memory::WriteUnchecked_U32(GE_CMD_END << 24, execListPos + 4);
	execListPos += 8;

	for (int i = 0; i < 8; ++i)
		lastTex_[i] = 0;
	lastBase_ = 0xFFFFFFFF;

	SyncStall();
	ExecuteOnMain(Operation{ OpType::ListSync, execListID });
}

void DumpExecute::Init(u32 ptr, u32 sz) {
	gstate.Restore((u32_le *)(pushbuf_.data() + ptr));
	ExecuteOnMain(Operation{ OpType::ReapplyGfxState });
	fbPtr_ = gstate.fbptr & 0x00FFFFFF;
	fbWidth_ = gstate.fbwidth & 0x00FFFFFF;
	fbFormat_ = gstate.framebufpixformat & 3;
	region2_ = gstate.region2 & 0x000FFFFF;
	scissor2_ = gstate.scissor2 & 0x000FFFFF;
	vertType_ = gstate.vertType & 0x00FFFFFF;
	zbPtr_ = gstate.zbptr & 0x00FFFFFF;
	zbWidth_ = gstate.zbwidth & 0x00FFFFFF;
	zTest_ = (gstate.zTestEnable & 1) != 0;
	zWriteDisable_ = (gstate.zmsk & 1) != 0;
	clearMode_ = gstate.clearmode & 0xFFFF;

	// Hm, why are we doing this here? Should it be before?
	for (int i = 0; i < 8; ++i) {
		lastBufw_[i] = 0;
		lastTex_[i] = 0;
	}
	lastBase_ = 0xFFFFFFFF;
}

// The drawing-space bounds of a through mode draw's vertices, or false if they aren't simple to read
// (indexed, morphed, or not through mode).
static bool ThroughModeBounds(u32 vtype, const u8 *data, u32 size, u32 count, int &x1, int &y1, int &x2, int &y2) {
	if ((vtype & GE_VTYPE_THROUGH) == 0 || (vtype & GE_VTYPE_IDX_MASK) != 0 || (vtype & GE_VTYPE_MORPHCOUNT_MASK) != 0 || count == 0)
		return false;

	// Each component is aligned to its own size, and the vertex to the largest.
	static const u8 compSizes[4] = { 0, 1, 2, 4 };
	static const u8 colorSizes[8] = { 0, 0, 0, 0, 2, 2, 2, 4 };
	u32 offset = 0, align = 1;
	auto add = [&](u32 elemSize, u32 n) {
		if (elemSize == 0)
			return;
		offset = (offset + elemSize - 1) & ~(elemSize - 1);
		offset += elemSize * n;
		align = std::max(align, elemSize);
	};
	add(compSizes[(vtype & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT], ((vtype & GE_VTYPE_WEIGHTCOUNT_MASK) >> GE_VTYPE_WEIGHTCOUNT_SHIFT) + 1);
	add(compSizes[(vtype & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT], 2);
	add(colorSizes[(vtype & GE_VTYPE_COL_MASK) >> GE_VTYPE_COL_SHIFT], 1);
	add(compSizes[(vtype & GE_VTYPE_NRM_MASK) >> GE_VTYPE_NRM_SHIFT], 3);
	const u32 posSize = compSizes[(vtype & GE_VTYPE_POS_MASK) >> GE_VTYPE_POS_SHIFT];
	if (posSize == 0)
		return false;
	offset = (offset + posSize - 1) & ~(posSize - 1);
	const u32 posOffset = offset;
	add(posSize, 3);
	const u32 stride = (offset + align - 1) & ~(align - 1);
	if (stride * count > size)
		return false;

	x1 = y1 = INT_MAX;
	x2 = y2 = INT_MIN;
	for (u32 i = 0; i < count; ++i) {
		const u8 *p = data + i * stride + posOffset;
		int x, y;
		if (posSize == 4) {
			float fx, fy;
			memcpy(&fx, p, 4);
			memcpy(&fy, p + 4, 4);
			if (!(fx >= -4096.0f && fx <= 4096.0f && fy >= -4096.0f && fy <= 4096.0f))
				return false;
			x = (int)fx;
			y = (int)fy;
		} else if (posSize == 2) {
			x = (s16)(p[0] | (p[1] << 8));
			y = (s16)(p[2] | (p[3] << 8));
		} else {
			x = (s8)p[0];
			y = (s8)p[1];
		}
		// Truncation can be a pixel short at either end; the bounds only need to be generous.
		x1 = std::min(x1, x - 1);
		y1 = std::min(y1, y - 1);
		x2 = std::max(x2, x + 1);
		y2 = std::max(y2, y + 1);
	}
	return true;
}

void DumpExecute::MarkDrawn(u32 prim) {
	const u32 start = ((fbPtr_ & 0xFFFFF0) | ((fbWidth_ & 0xFF0000) << 8)) & 0x001FFFFF;
	const u32 bpp = fbFormat_ == GE_FORMAT_8888 ? 4 : 2;
	int x1 = 0, y1 = 0;
	int x2 = std::min(region2_ & 0x3FF, scissor2_ & 0x3FF);
	int y2 = std::min((region2_ >> 10) & 0x3FF, (scissor2_ >> 10) & 0x3FF);
	// A 2D draw (a sprite into a small buffer whose nominal area overlaps textures, Burnout) can say where
	// it is exactly.
	int vx1, vy1, vx2, vy2;
	if (prim != 0 && lastVertsSize_ != 0 && ThroughModeBounds(vertType_, pushbuf_.data() + lastVertsPtr_, lastVertsSize_, prim & 0xFFFF, vx1, vy1, vx2, vy2)) {
		x1 = std::max(x1, vx1);
		y1 = std::max(y1, vy1);
		x2 = std::min(x2, vx2);
		y2 = std::min(y2, vy2);
		if (x1 > x2 || y1 > y2)
			return;
	}

	auto add = [&](u32 base, u32 strideBytes, u32 bytesPerPixel, const DrawnRect &rect) {
		DrawnTarget &t = drawnTargets_[base];
		t.strideBytes = strideBytes;
		t.bpp = bytesPerPixel;
		for (const DrawnRect &r : t.rects) {
			if (r.Contains(rect.x1, rect.y1) && r.Contains(rect.x2, rect.y2))
				return;
		}
		t.rects.push_back(rect);
	};
	add(start, (fbWidth_ & 0x07FC) * bpp, bpp, DrawnRect{ x1, y1, x2, y2 });
	const bool writesDepth = (clearMode_ & 1) ? (clearMode_ & 0x400) != 0 : (zTest_ && !zWriteDisable_);
	const int zbw = zbWidth_ & 0x07FC;
	if (writesDepth && zbw != 0) {
		// The PSP stores depth swizzled, so what lands in a drawn area is spread over whole rows in bands
		// (SOCOM's frame texture copy ran into its depth buffer's columns 480-511).
		add(zbPtr_ & 0x001FFFF0, zbw * 2, 2, DrawnRect{ 0, y1 & ~7, zbw - 1, y2 | 7 });
	}
}

void DumpExecute::Vertices(u32 ptr, u32 sz) {
	lastVertsPtr_ = ptr;
	lastVertsSize_ = sz;
	u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(Log::GeDebugger, "Unable to allocate for vertices");
		return;
	}

	if (lastBase_ != (psp & 0xFF000000)) {
		execListQueue.push_back((GE_CMD_BASE << 24) | ((psp >> 8) & 0x00FF0000));
		lastBase_ = psp & 0xFF000000;
	}
	execListQueue.push_back((GE_CMD_VADDR << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::Indices(u32 ptr, u32 sz) {
	u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(Log::GeDebugger, "Unable to allocate for indices");
		return;
	}

	if (lastBase_ != (psp & 0xFF000000)) {
		execListQueue.push_back((GE_CMD_BASE << 24) | ((psp >> 8) & 0x00FF0000));
		lastBase_ = psp & 0xFF000000;
	}
	execListQueue.push_back((GE_CMD_IADDR << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::ClutAddr(u32 ptr, u32 sz) {
	struct ClutAddrData {
		u32 addr;
		u32 flags;
	};
	const ClutAddrData *data = (const ClutAddrData *)(pushbuf_.data() + ptr);
	execClutAddr = data->addr;
	execClutFlags = data->flags;
}

void DumpExecute::Clut(u32 ptr, u32 sz) {
	// This is always run when we have the actual address set.
	if (execClutAddr != 0) {
		const bool isTarget = (execClutFlags & 1) != 0;

		// Could potentially always skip if !isTarget, but playing it safe for offset texture behavior.
		if (Memory::IsValidRange(execClutAddr, sz) && (!isTarget || !g_Config.bSoftwareRendering)) {
			// Intentionally don't trigger an upload here.
			Memory::MemcpyUnchecked(execClutAddr, pushbuf_.data() + ptr, sz);
			NotifyMemInfo(MemBlockFlags::WRITE, execClutAddr, sz, "ReplayClut");
		}

		execClutAddr = 0;
	} else {
		u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
		if (psp == 0) {
			ERROR_LOG(Log::GeDebugger, "Unable to allocate for clut");
			return;
		}

		execListQueue.push_back((GE_CMD_CLUTADDRUPPER << 24) | ((psp >> 8) & 0x00FF0000));
		execListQueue.push_back((GE_CMD_CLUTADDR << 24) | (psp & 0x00FFFFFF));
	}
}

void DumpExecute::TransferSrc(u32 ptr, u32 sz) {
	u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(Log::GeDebugger, "Unable to allocate for transfer");
		return;
	}

	// Need to sync in order to access gstate.transfersrcw.
	SyncStall();

	execListQueue.push_back((gstate.transfersrcw & 0xFF00FFFF) | ((psp >> 8) & 0x00FF0000));
	execListQueue.push_back(((GE_CMD_TRANSFERSRC) << 24) | (psp & 0x00FFFFFF));
}

void DumpExecute::Memset(u32 ptr, u32 sz) {
	PROFILE_THIS_SCOPE("ReplayMemset");
	struct MemsetCommand {
		u32 dest;
		int value;
		u32 sz;
	};

	const MemsetCommand *data = (const MemsetCommand *)(pushbuf_.data() + ptr);

	if (Memory::IsVRAMAddress(data->dest)) {
		SyncStall();
		// Draws the software renderer still has queued come before the write.
		gpu->Flush();
		// TODO: should probably do this as an operation.
		// The software renderer leaves the memset to the caller, like sceKernelMemset does.
		if (!gpu->PerformMemorySet(data->dest, (u8)data->value, data->sz))
			Memory::Memset(data->dest, (u8)data->value, data->sz);
	}
}

void DumpExecute::MemcpyDest(u32 ptr, u32 sz) {
	execMemcpyDest = *(const u32 *)(pushbuf_.data() + ptr);
}

void DumpExecute::Memcpy(u32 ptr, u32 sz) {
	PROFILE_THIS_SCOPE("ReplayMemcpy");
	if (Memory::IsVRAMAddress(execMemcpyDest)) {
		SyncStall();
		gpu->Flush();
		Memory::MemcpyUnchecked(execMemcpyDest, pushbuf_.data() + ptr, sz);
		NotifyMemInfo(MemBlockFlags::WRITE, execMemcpyDest, sz, "ReplayMemcpy");
		gpu->PerformWriteColorFromMemory(execMemcpyDest, sz);
	}
}

void DumpExecute::Texture(int level, u32 ptr, u32 sz) {
	u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(Log::GeDebugger, "Unable to allocate for texture");
		return;
	}

	if (lastTex_[level] != psp) {
		u32 bufwCmd = GE_CMD_TEXBUFWIDTH0 + level;
		u32 addrCmd = GE_CMD_TEXADDR0 + level;
		execListQueue.push_back((bufwCmd << 24) | ((psp >> 8) & 0x00FF0000) | lastBufw_[level]);
		execListQueue.push_back((addrCmd << 24) | (psp & 0x00FFFFFF));
		lastTex_[level] = psp;
	}
}

void DumpExecute::Framebuf(int level, u32 ptr, u32 sz) {
	PROFILE_THIS_SCOPE("ReplayFramebuf");
	struct FramebufData {
		u32 addr;
		int bufw;
		u32 flags;
		u32 pad;
	};

	FramebufData *framebuf = (FramebufData *)(pushbuf_.data() + ptr);

	if (lastTex_[level] != framebuf->addr || lastBufw_[level] != framebuf->bufw) {
		u32 bufwCmd = GE_CMD_TEXBUFWIDTH0 + level;
		u32 addrCmd = GE_CMD_TEXADDR0 + level;
		execListQueue.push_back((bufwCmd << 24) | ((framebuf->addr >> 8) & 0x00FF0000) | framebuf->bufw);
		execListQueue.push_back((addrCmd << 24) | (framebuf->addr & 0x00FFFFFF));
		lastTex_[level] = framebuf->addr;
		lastBufw_[level] = framebuf->bufw;
	}

	// And now also copy the data into VRAM (in case it wasn't actually rendered.)
	u32 headerSize = (u32)sizeof(FramebufData);
	u32 pspSize = sz - headerSize;
	const bool isTarget = (framebuf->flags & 1) != 0;
	const bool unchangedVRAM = version_ >= 6 && (framebuf->flags & 2) != 0;
	// TODO: Could use drawnVRAM flag, but it can be wrong.
	// Could potentially always skip if !isTarget, but playing it safe for offset texture behavior.
	if (Memory::IsValidRange(framebuf->addr, pspSize) && !unchangedVRAM && (!isTarget || !g_Config.bSoftwareRendering)) {
		// After the draws before it, which the software renderer may still have queued.
		SyncStall();
		gpu->Flush();
		// Intentionally don't trigger an upload here.
		CopyAroundDrawn(framebuf->addr, pushbuf_.data() + ptr + headerSize, pspSize);
	}
}

// The software renderer has the real contents of what this replay drew, while the dump's copy can be stale
// (a hardware backend that didn't read it back, GTA LCS). So a copy leaves out the areas the replay's draws
// could reach: a texture inside one (Princess Maker 5 textures from the right half of its 1024 wide
// framebuffer), or one whose recorded size runs over a buffer drawn next to it (Rainbow Six copies its
// 512x512 frame texture before each strip it draws into the display buffer inside that range). A texture
// beside the drawn area still comes through (Burnout keeps one in columns 480-511).
void DumpExecute::CopyAroundDrawn(u32 addr, const u8 *data, u32 size) {
	// A texture recorded at its full size can run past the end of VRAM, into the depth swizzle mirror
	// (Princess Maker 5's 1024x1024 one at 0x04000400). That isn't the game's data.
	if (Memory::IsVRAMAddress(addr))
		size = std::min(size, 0x00200000 - (addr & 0x001FFFFF));
	std::vector<std::pair<int64_t, int64_t>> skip;
	const int64_t start = addr & 0x001FFFFF, end = start + size;
	if (g_Config.bSoftwareRendering) {
		for (const auto &it : drawnTargets_) {
			const DrawnTarget &t = it.second;
			if (t.strideBytes == 0)
				continue;
			for (const DrawnRect &r : t.rects) {
				for (int y = std::max(r.y1, 0); y <= r.y2; ++y) {
					const int64_t row = (int64_t)it.first + (int64_t)y * t.strideBytes;
					const int64_t a = row + (int64_t)std::max(r.x1, 0) * t.bpp;
					const int64_t b = row + (int64_t)(r.x2 + 1) * t.bpp;
					if (b > start && a < end)
						skip.emplace_back(std::max(a, start), std::min(b, end));
				}
			}
		}
		std::sort(skip.begin(), skip.end());
	}

	int64_t pos = start;
	auto copyTo = [&](int64_t until) {
		if (until > pos) {
			Memory::MemcpyUnchecked(addr + (u32)(pos - start), data + (pos - start), (u32)(until - pos));
			NotifyMemInfo(MemBlockFlags::WRITE, addr + (u32)(pos - start), (u32)(until - pos), "ReplayTex");
		}
	};
	for (const auto &s : skip) {
		copyTo(s.first);
		pos = std::max(pos, s.second);
	}
	copyTo(end);
}

void DumpExecute::Display(u32 ptr, u32 sz, bool allowFlip) {
	struct DisplayBufData {
		PSPPointer<u8> topaddr;
		int linesize, pixelFormat;
	};

	DisplayBufData *disp = (DisplayBufData *)(pushbuf_.data() + ptr);

	// Sync up drawing.
	SyncStall();

	__DisplaySetFramebuf(disp->topaddr.ptr, disp->linesize, disp->pixelFormat, 1);
	if (allowFlip) {
		__DisplaySetFramebuf(disp->topaddr.ptr, disp->linesize, disp->pixelFormat, 0);
	}
}

void DumpExecute::EdramTrans(u32 ptr, u32 sz) {
	uint32_t value;
	memcpy(&value, pushbuf_.data() + ptr, 4);

	// Sync up drawing.
	SyncStall();

	if (gpu)
		gpu->SetAddrTranslation(value);
}

DumpExecute::~DumpExecute() {
	execMemcpyDest = 0;
	if (execListBuf) {
		userMemory.Free(execListBuf);
		execListBuf = 0;
	}
	execListPos = 0;
	mapping_.Reset();
}

ReplayResult DumpExecute::Run() {
	// Start with the default value.
	if (gpu)
		gpu->SetAddrTranslation(0x400);

	if (resumeIndex_ >= 0) {
		SyncStall();
	}

	int start = resumeIndex_ >= 0 ? resumeIndex_ : 0;
	for (size_t i = start; i < commands_.size(); i++) {
		if (g_cancelled) {
			break;
		}

		const Command &cmd = commands_[i];
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

		case CommandType::CLUTADDR:
			ClutAddr(cmd.ptr, cmd.sz);
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

		case CommandType::EDRAMTRANS:
			EdramTrans(cmd.ptr, cmd.sz);
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
			Display(cmd.ptr, cmd.sz, i == commands_.size() - 1);
			break;

		default:
			ERROR_LOG(Log::GeDebugger, "Unsupported GE dump command: %d", (int)cmd.type);
			return ReplayResult::Error;
		}
	}

	SubmitListEnd();
	return ReplayResult::Done;
}

static bool ReadCompressed(u32 fp, void *dest, size_t sz, uint32_t version) {
	u32 compressed_size = 0;
	if (pspFileSystem.ReadFile(fp, (u8 *)&compressed_size, sizeof(compressed_size)) != sizeof(compressed_size)) {
		return false;
	}

	u8 *compressed = new u8[compressed_size];
	if (pspFileSystem.ReadFile(fp, compressed, compressed_size) != compressed_size) {
		delete[] compressed;
		return false;
	}

	size_t real_size = sz;
	if (version < 5)
		snappy_uncompress((const char *)compressed, compressed_size, (char *)dest, &real_size);
	else
		real_size = ZSTD_decompress(dest, real_size, compressed, compressed_size);
	delete[] compressed;

	return real_size == sz;
}

static u32 LoadReplay(const std::string &filename) {
	PROFILE_THIS_SCOPE("ReplayLoad");

	NOTICE_LOG(Log::GeDebugger, "LoadReplay %s", filename.c_str());

	g_cancelled = false;

	u32 fp = pspFileSystem.OpenFile(filename, FILEACCESS_READ);
	Header header;
	pspFileSystem.ReadFile(fp, (u8 *)&header, sizeof(header));
	u32 version = header.version;

	if (memcmp(header.magic, HEADER_MAGIC, sizeof(header.magic)) != 0 || header.version > VERSION || header.version < MIN_VERSION) {
		ERROR_LOG(Log::GeDebugger, "Invalid GE dump or unsupported version");
		pspFileSystem.CloseFile(fp);
		return 0;
	}
	if (header.version <= 3) {
		pspFileSystem.SeekFile(fp, 12, FILEMOVE_BEGIN);
		memset(header.gameID, 0, sizeof(header.gameID));
	}

	const std::string gameID(header.gameID, strnlen(header.gameID, sizeof(header.gameID)));
	// The ID picks the texture pack directory, among other things.
	if (IsSafePathComponent(gameID)) {
		g_paramSFO.SetValue("DISC_ID", gameID, (int)sizeof(header.gameID));
		std::vector<GameDBInfo> info;
		std::string gameTitle = "(unknown title)";
#if !defined(__LIBRETRO__)
		if (g_gameDB.GetGameInfos(gameID, &info)) {
			gameTitle = info[0].title;
			g_paramSFO.SetValue("TITLE", gameTitle, (int)gameTitle.size());
		}
#endif
		System_SetWindowTitle(g_paramSFO.GetValueString("DISC_ID") + " : " + gameTitle + " (GE frame dump)");
	} else {
		System_SetWindowTitle("(GE frame dump: old format, missing DISC_ID)");
	}

	u32 sz = 0;
	pspFileSystem.ReadFile(fp, (u8 *)&sz, sizeof(sz));
	u32 bufsz = 0;
	pspFileSystem.ReadFile(fp, (u8 *)&bufsz, sizeof(bufsz));

	lastExecCommands.resize(sz);
	lastExecPushbuf.resize(bufsz);

	bool truncated = false;
	truncated = truncated || !ReadCompressed(fp, lastExecCommands.data(), sizeof(Command) * sz, header.version);
	truncated = truncated || !ReadCompressed(fp, lastExecPushbuf.data(), bufsz, header.version);

	pspFileSystem.CloseFile(fp);

	if (truncated) {
		ERROR_LOG(Log::GeDebugger, "Truncated GE dump detected - can't replay");
		return 0;
	}

	lastExecFilename = filename;
	lastExecVersion = version;
	return version;
}

void Replay_Unload() {
	// We might be paused inside a replay - in this case, the thread is still running and we need to tell it to stop.
	if (replayThread.joinable()) {
		{
			// We just finish processing the commands until done.
			g_cancelled = true;

			std::unique_lock<std::mutex> lock(opFinishLock);
			opFinishWait.notify_one();
		}
		replayThread.join();
	}

	_dbg_assert_(!replayThread.joinable());

	lastExecFilename.clear();
	lastExecVersion = 0;
	lastExecCommands.clear();
	lastExecPushbuf.clear();

	g_opDone = true;
	g_retVal = 0;
}

void WriteRunDumpCode(u32 codeStart) {
	// NOTE: Not static, since parts are run-time computed (MIPS_MAKE_SYSCALL etc)
	const u32 runDumpCode[] = {
		// Save the filename.
		MIPS_MAKE_ORI(MIPS_REG_S0, MIPS_REG_A0, 0),
		MIPS_MAKE_ORI(MIPS_REG_S1, MIPS_REG_A1, 0),
		// Call the actual render. Jump here to start over.
		MIPS_MAKE_SYSCALL("FakeSysCalls", "__KernelGPUReplay"),
		MIPS_MAKE_NOP(),
		// Re-run immediately if requested by the return value from __KernelGPUReplay
		MIPS_MAKE_BNEZ(codeStart + 4 * 4, codeStart + 8, MIPS_REG_V0),
		MIPS_MAKE_NOP(),
		// When done (__KernelGPUReplay returned 0), make sure we don't get out of sync (is this needed?)
		MIPS_MAKE_LUI(MIPS_REG_A0, 0),
		MIPS_MAKE_SYSCALL("sceGe_user", "sceGeDrawSync"),
		MIPS_MAKE_NOP(),
		// Wait for the next vblank to render again, then (through the delay slot) jump right back up to __KernelGPUReplay.
		MIPS_MAKE_SYSCALL("sceDisplay", "sceDisplayWaitVblankStart"),
		MIPS_MAKE_NOP(),
		MIPS_MAKE_J(codeStart + 8),
		MIPS_MAKE_NOP(),
		// This never gets reached, just here to be "safe".
		MIPS_MAKE_BREAK(0),
	};
	for (size_t i = 0; i < ARRAY_SIZE(runDumpCode); ++i) {
		Memory::WriteUnchecked_U32(runDumpCode[i], codeStart + (u32)i * sizeof(u32_le));
	}
}

// This is called by the syscall. It spawns a "replayThread" which parses the file and sends the commands.
// A long term goal is inversion of control here, but it's tricky for a number of reasons that you'll find
// out if you try.
ReplayResult RunMountedReplay(const std::string &filename) {
	_assert_msg_(!gpu->GetRecorder()->IsActivePending(), "Cannot run replay while recording.");

	uint32_t version = lastExecVersion;
	if (lastExecFilename != filename) {
		// Does this ever happen? Can the filename change, without going through core shutdown/startup?
		if (replayThread.joinable()) {
			replayThread.join();
		}
		version = LoadReplay(filename);
		if (!version) {
			ERROR_LOG(Log::GeDebugger, "bad version %08x", version);
			return ReplayResult::Error;
		}
	}

	if (g_opToExec.type != OpType::None) {
		std::unique_lock<std::mutex> waitLock(opFinishLock);
		g_opDone = true;
		g_opToExec = Operation{ OpType::None };
		opFinishWait.notify_one();
	}

	if (!replayThread.joinable()) {
		_dbg_assert_(g_opToExec.type == OpType::None);
		g_opToExec = Operation{ OpType::None };
		replayThread = std::thread([version]() {
			SetCurrentThreadName("Replay");
			DumpExecute executor(lastExecPushbuf, lastExecCommands, version);
			GPURecord::ReplayResult retval = executor.Run();
			// Finish up
			ExecuteOnMain(Operation{ OpType::Done });
		});
	}

	// OK, now wait for and perform the desired action.
	{
		std::unique_lock<std::mutex> lock(opStartLock);
		g_condOpStartWait.wait(lock, []() { return g_opToExec.type != OpType::None; });
	}

	switch (g_opToExec.type) {
	case OpType::UpdateStallAddr:
	{
		bool runList;
		hleEatCycles(190);
		hleCoreTimingForceCheck();
		gpu->UpdateStall(g_opToExec.listID, g_opToExec.param, &runList);
		if (runList) {
			hleSplitSyscallOverGe();
		}
		// We're not done yet, request another go.
		return ReplayResult::Break;
	}
	case OpType::EnqueueList:
	{
		bool runList;
		u32 listPC = g_opToExec.listID;
		u32 execListPos = g_opToExec.param;
		auto optParam = PSPPointer<PspGeListArgs>::Create(0);
		g_retVal = gpu->EnqueueList(listPC, execListPos, -1, optParam, false, &runList);
		if (runList) {
			hleSplitSyscallOverGe();
		}
		// We're not done yet, request another go.
		hleEatCycles(490);
		hleCoreTimingForceCheck();
		return ReplayResult::Break;
	}
	case OpType::ReapplyGfxState:
	{
		// try again but no need to split the sys call
		gpu->ReapplyGfxState();
		return ReplayResult::Break;
	}
	case OpType::ListSync:
	{
		u32 execListID = g_opToExec.listID;
		u32 mode = g_opToExec.param;
		// try again but no need to split the sys call
		hleEatCycles(220);
		gpu->ListSync(execListID, mode);
		return ReplayResult::Break;
	}
	case OpType::Done:
	{
		_dbg_assert_(replayThread.joinable());
		{
			std::unique_lock<std::mutex> lock(opFinishLock);
			g_opDone = true;
			opFinishWait.notify_one();
		}
		replayThread.join();
		g_opToExec = { OpType::None };
		break;
	}
	case OpType::None:
		break;
	}
	return ReplayResult::Done;
}

}  // namespace GPURecord

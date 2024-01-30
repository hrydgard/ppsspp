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
#include <mutex>
#include <vector>
#include <snappy-c.h>
#include <zstd.h>
#include "Common/Profiler/Profiler.h"
#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/MemBlockInfo.h"
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
#include "GPU/Debugger/Playback.h"
#include "GPU/Debugger/Record.h"
#include "GPU/Debugger/RecordFormat.h"

namespace GPURecord {

static std::string lastExecFilename;
static uint32_t lastExecVersion;
static std::vector<Command> lastExecCommands;
static std::vector<u8> lastExecPushbuf;
static std::mutex executeLock;

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
		bool Setup(u32 bufpos, const std::vector<u8> &pushbuf_);
	};

	// An adhoc mapping of the pushbuffer (either larger than a slab or straddling slabs.)
	// Remember: texture data, verts, etc. must be contiguous.
	struct ExtraInfo {
		u32 psp_pointer_ = 0;
		u32 buf_pointer_ = 0;
		u32 size_ = 0;

		bool Matches(u32 bufpos, u32 sz) {
			// We check psp_pointer_ because bufpos = 0 is valid, and the initial value.
			return buf_pointer_ == bufpos && psp_pointer_ != 0 && size_ >= sz;
		}

		u32 Ptr() {
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

	bool Run();

private:
	void SyncStall();
	bool SubmitCmds(const void *p, u32 sz);
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
	u32 lastBase_ = 0;

	const std::vector<u8> &pushbuf_;
	const std::vector<Command> &commands_;
	BufMapping mapping_;
	uint32_t version_ = 0;
};

void DumpExecute::SyncStall() {
	if (execListBuf == 0) {
		return;
	}

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

bool DumpExecute::SubmitCmds(const void *p, u32 sz) {
	if (execListBuf == 0) {
		u32 allocSize = LIST_BUF_SIZE;
		execListBuf = userMemory.Alloc(allocSize, true, "List buf");
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

	u32 pendingSize = (u32)execListQueue.size() * sizeof(u32);
	// Validate space for jump.
	u32 allocSize = pendingSize + sz + 8;
	if (execListPos + allocSize >= execListBuf + LIST_BUF_SIZE) {
		Memory::Write_U32((GE_CMD_BASE << 24) | ((execListBuf >> 8) & 0x00FF0000), execListPos);
		Memory::Write_U32((GE_CMD_JUMP << 24) | (execListBuf & 0x00FFFFFF), execListPos + 4);

		execListPos = execListBuf;
		lastBase_ = execListBuf & 0xFF000000;

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

	for (int i = 0; i < 8; ++i)
		lastTex_[i] = 0;
	lastBase_ = 0xFFFFFFFF;

	SyncStall();
	gpu->ListSync(execListID, 0);
}

void DumpExecute::Init(u32 ptr, u32 sz) {
	gstate.Restore((u32_le *)(pushbuf_.data() + ptr));
	gpu->ReapplyGfxState();

	for (int i = 0; i < 8; ++i) {
		lastBufw_[i] = 0;
		lastTex_[i] = 0;
	}
	lastBase_ = 0xFFFFFFFF;
}

void DumpExecute::Registers(u32 ptr, u32 sz) {
	SubmitCmds(pushbuf_.data() + ptr, sz);
}

void DumpExecute::Vertices(u32 ptr, u32 sz) {
	u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for vertices");
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
		ERROR_LOG(SYSTEM, "Unable to allocate for indices");
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
			ERROR_LOG(SYSTEM, "Unable to allocate for clut");
			return;
		}

		execListQueue.push_back((GE_CMD_CLUTADDRUPPER << 24) | ((psp >> 8) & 0x00FF0000));
		execListQueue.push_back((GE_CMD_CLUTADDR << 24) | (psp & 0x00FFFFFF));
	}
}

void DumpExecute::TransferSrc(u32 ptr, u32 sz) {
	u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
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
	PROFILE_THIS_SCOPE("ReplayMemset");
	struct MemsetCommand {
		u32 dest;
		int value;
		u32 sz;
	};

	const MemsetCommand *data = (const MemsetCommand *)(pushbuf_.data() + ptr);

	if (Memory::IsVRAMAddress(data->dest)) {
		SyncStall();
		gpu->PerformMemorySet(data->dest, (u8)data->value, data->sz);
	}
}

void DumpExecute::MemcpyDest(u32 ptr, u32 sz) {
	execMemcpyDest = *(const u32 *)(pushbuf_.data() + ptr);
}

void DumpExecute::Memcpy(u32 ptr, u32 sz) {
	PROFILE_THIS_SCOPE("ReplayMemcpy");
	if (Memory::IsVRAMAddress(execMemcpyDest)) {
		SyncStall();
		Memory::MemcpyUnchecked(execMemcpyDest, pushbuf_.data() + ptr, sz);
		NotifyMemInfo(MemBlockFlags::WRITE, execMemcpyDest, sz, "ReplayMemcpy");
		gpu->PerformWriteColorFromMemory(execMemcpyDest, sz);
	}
}

void DumpExecute::Texture(int level, u32 ptr, u32 sz) {
	u32 psp = mapping_.Map(ptr, sz, std::bind(&DumpExecute::SyncStall, this));
	if (psp == 0) {
		ERROR_LOG(SYSTEM, "Unable to allocate for texture");
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
		// Intentionally don't trigger an upload here.
		Memory::MemcpyUnchecked(framebuf->addr, pushbuf_.data() + ptr + headerSize, pspSize);
		NotifyMemInfo(MemBlockFlags::WRITE, framebuf->addr, pspSize, "ReplayTex");
	}
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

bool DumpExecute::Run() {
	// Start with the default value.
	if (gpu)
		gpu->SetAddrTranslation(0x400);

	for (size_t i = 0; i < commands_.size(); i++) {
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
			ERROR_LOG(SYSTEM, "Unsupported GE dump command: %d", (int)cmd.type);
			return false;
		}
	}

	SubmitListEnd();
	return true;
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

static void ReplayStop() {
	// This can happen from a separate thread.
	std::lock_guard<std::mutex> guard(executeLock);
	lastExecFilename.clear();
	lastExecCommands.clear();
	lastExecPushbuf.clear();
	lastExecVersion = 0;
}

bool RunMountedReplay(const std::string &filename) {
	_assert_msg_(!GPURecord::IsActivePending(), "Cannot run replay while recording.");

	std::lock_guard<std::mutex> guard(executeLock);
	Core_ListenStopRequest(&ReplayStop);

	uint32_t version = lastExecVersion;
	if (lastExecFilename != filename) {
		PROFILE_THIS_SCOPE("ReplayLoad");
		u32 fp = pspFileSystem.OpenFile(filename, FILEACCESS_READ);
		Header header;
		pspFileSystem.ReadFile(fp, (u8 *)&header, sizeof(header));
		version = header.version;

		if (memcmp(header.magic, HEADER_MAGIC, sizeof(header.magic)) != 0 || header.version > VERSION || header.version < MIN_VERSION) {
			ERROR_LOG(SYSTEM, "Invalid GE dump or unsupported version");
			pspFileSystem.CloseFile(fp);
			return false;
		}
		if (header.version <= 3) {
			pspFileSystem.SeekFile(fp, 12, FILEMOVE_BEGIN);
			memset(header.gameID, 0, sizeof(header.gameID));
		}

		size_t gameIDLength = strnlen(header.gameID, sizeof(header.gameID));
		if (gameIDLength != 0) {
			g_paramSFO.SetValue("DISC_ID", std::string(header.gameID, gameIDLength), (int)sizeof(header.gameID));
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
			ERROR_LOG(SYSTEM, "Truncated GE dump");
			return false;
		}

		lastExecFilename = filename;
		lastExecVersion = version;
	}

	DumpExecute executor(lastExecPushbuf, lastExecCommands, version);
	return executor.Run();
}

};

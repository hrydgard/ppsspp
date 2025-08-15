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

#pragma once

#include <functional>
#include <atomic>
#include <vector>
#include <set>

#include "Common/CommonTypes.h"
#include "GPU/Debugger/RecordFormat.h"

class Path;

namespace GPURecord {

constexpr uint32_t DIRTY_VRAM_SHIFT = 8;
constexpr uint32_t DIRTY_VRAM_ROUND = (1 << DIRTY_VRAM_SHIFT) - 1;
constexpr uint32_t DIRTY_VRAM_SIZE = (2 * 1024 * 1024) >> DIRTY_VRAM_SHIFT;
constexpr uint32_t DIRTY_VRAM_MASK = (2 * 1024 * 1024 - 1) >> DIRTY_VRAM_SHIFT;
enum class DirtyVRAMFlag : uint8_t {
	CLEAN = 0,
	UNKNOWN = 1,
	DIRTY = 2,
	DRAWN = 3,
};

class Recorder {
public:
	bool IsActive() const {
		return active;
	}
	bool IsActivePending() const {
		return nextFrame || active;
	}
	bool RecordNextFrame(const std::function<void(const Path &)> callback);
	void ClearCallback() {
		// Not super thread safe..
		writeCallback = nullptr;
	}

	void NotifyCommand(u32 pc);
	void NotifyMemcpy(u32 dest, u32 src, u32 sz);
	void NotifyMemset(u32 dest, int v, u32 sz);
	void NotifyUpload(u32 dest, u32 sz);
	void NotifyDisplay(u32 addr, int stride, int fmt);
	void NotifyBeginFrame();
	void NotifyCPU();
private:
	void FlushRegisters();
	void DirtyAllVRAM(DirtyVRAMFlag flag);
	void DirtyVRAM(u32 start, u32 sz, DirtyVRAMFlag flag);
	void DirtyDrawnVRAM();

	bool BeginRecording();
	Path WriteRecording();

	bool HasDrawCommands() const;
	void CheckEdramTrans();
	void FinishRecording();

	Command EmitCommandWithRAM(CommandType t, const void *p, u32 sz, u32 align);

	void UpdateLastVRAM(u32 addr, u32 bytes);
	void ClearLastVRAM(u32 addr, u8 c, u32 bytes);
	int CompareLastVRAM(u32 addr, u32 bytes) const;

	u32 GetTargetFlags(u32 addr, u32 sizeInRAM);

	void FlushPrimState(int vcount);
	void EmitTextureData(int level, u32 texaddr);
	void EmitTransfer(u32 op);
	void EmitClut(u32 op);
	void EmitPrim(u32 op);
	void EmitBezierSpline(u32 op);

	bool active = false;
	std::atomic<bool> nextFrame = false;
	int flipLastAction = -1;
	int flipFinishAt = -1;
	uint32_t lastEdramTrans = 0x400;
	std::function<void(const Path &)> writeCallback;

	std::vector<u8> pushbuf;
	std::vector<Command> commands;
	std::vector<u32> lastRegisters;
	std::vector<u32> lastTextures;
	std::set<u32> lastRenderTargets;
	std::vector<u8> lastVRAM;

	DirtyVRAMFlag dirtyVRAM[DIRTY_VRAM_SIZE];
};

}  // namespace GPURecord

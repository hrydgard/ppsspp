// Copyright (c) 2014- PPSSPP Project.

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

#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"
#include "GPU/GLES/DisplayListCache.h"
#include "GPU/GLES/ShaderManager.h"

// TODO: Do based on op count instead?  Does this make sense?
const int MAX_COMPILED_PER_FRAME = 3;
const static int OLD_FLIPS = 90;

struct UniformDirtyTableEntry {
	u8 cmd;
	u32 flag;
};

static const UniformDirtyTableEntry dirtyTable[] = {
	{GE_CMD_FOGCOLOR, DIRTY_FOGCOLOR},
	{GE_CMD_FOG1, DIRTY_FOGCOEF},
	{GE_CMD_FOG2, DIRTY_FOGCOEF},
	{GE_CMD_TEXMAPMODE, DIRTY_UVSCALEOFFSET},
	{GE_CMD_AMBIENTCOLOR, DIRTY_AMBIENT},
	{GE_CMD_AMBIENTALPHA, DIRTY_AMBIENT},
	{GE_CMD_MATERIALDIFFUSE, DIRTY_MATDIFFUSE},
	{GE_CMD_MATERIALEMISSIVE, DIRTY_MATEMISSIVE},
	{GE_CMD_MATERIALAMBIENT, DIRTY_MATAMBIENTALPHA},
	{GE_CMD_MATERIALALPHA, DIRTY_MATAMBIENTALPHA},
	{GE_CMD_MATERIALSPECULAR, DIRTY_MATSPECULAR},
	{GE_CMD_MATERIALSPECULARCOEF, DIRTY_MATSPECULAR},
	{GE_CMD_COLORTEST, DIRTY_ALPHACOLORMASK},
	{GE_CMD_COLORTESTMASK, DIRTY_ALPHACOLORMASK},
	{GE_CMD_ALPHATEST, DIRTY_ALPHACOLORREF},
	{GE_CMD_COLORREF, DIRTY_ALPHACOLORREF},
	{GE_CMD_TEXENVCOLOR, DIRTY_TEXENV},
	{GE_CMD_STENCILTEST, DIRTY_STENCILREPLACEVALUE},
};

DisplayListCache::DisplayListCache(GLES_GPU *gpu) : gpu_(gpu) {
	for (int i = 0; i < 256; ++i) {
		cmds_[i] = &DisplayListCache::Jit_Generic;
	}

	cmds_[GE_CMD_NOP] = &DisplayListCache::Jit_Nop;
	cmds_[GE_CMD_VADDR] = &DisplayListCache::Jit_Vaddr;
	cmds_[GE_CMD_IADDR] = &DisplayListCache::Jit_Iaddr;
	cmds_[GE_CMD_PRIM] = &DisplayListCache::Jit_Prim;
	cmds_[GE_CMD_VERTEXTYPE] = &DisplayListCache::Jit_VertexType;

	memset(dirtyFlags_, 0, sizeof(dirtyFlags_));
	for (size_t i = 0; i < ARRAY_SIZE(dirtyTable); ++i) {
		const UniformDirtyTableEntry &entry = dirtyTable[i];
		dirtyFlags_[entry.cmd] = entry.flag;
		cmds_[entry.cmd] = &DisplayListCache::Jit_GenericDirty;
	}

	Initialize();
}

void DisplayListCache::DecimateLists() {
	for (auto it = jitted_.begin(), end = jitted_.end(); it != end; ) {
		JittedDisplayList &list = it->second;
		if (list.lastFrame + OLD_FLIPS < gpuStats.numFlips) {
			jitted_.erase(it++);
		} else {
			++it;
		}
	}
}

bool DisplayListCache::Execute(u32 &pc, int &downcount) {
	// TODO: Track flag if the section is unreliable.
	const u64 key = ((u64)pc << 32) | downcount;
	auto it = jitted_.find(key);
	if (it != jitted_.end()) {
		JittedDisplayList &list = it->second;
		if (list.unreliable) {
			return false;
		}

		// TODO: Could return ptr instead for fixup or etc.  Right now debugging.
		u32 failed_at = list.entry(&pc);
		if (failed_at != 0) {
			list.unreliable = true;
		}
		list.lastFrame = gpuStats.numFlips;
		return true;
	} else {
		if (compiledFlips_ != gpuStats.numFlips) {
			compiledThisFrame_ = 0;
			compiledFlips_ = gpuStats.numFlips;
		}
		if (compiledThisFrame_ >= MAX_COMPILED_PER_FRAME) {
			return false;
		}
		++compiledThisFrame_;

		auto entry = Compile(pc, downcount);
		JittedDisplayList list;
		list.entry = entry;
		list.lastFrame = gpuStats.numFlips;
		list.unreliable = !entry;
		jitted_[key] = list;
		return true;
	}
}
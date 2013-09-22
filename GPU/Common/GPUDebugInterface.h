// Copyright (c) 2012- PPSSPP Project.

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

#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "Core/MemMap.h"
#include <vector>
#include <string>

struct GPUDebugOp {
	u32 pc;
	u8 cmd;
	u32 op;
	std::string desc;
};

class GPUDebugInterface {
public:
	virtual bool GetCurrentDisplayList(DisplayList &list) = 0;
	virtual std::vector<DisplayList> ActiveDisplayLists() = 0;
	virtual void ResetListPC(int listID, u32 pc) = 0;
	virtual void ResetListStall(int listID, u32 stall) = 0;
	virtual void ResetListState(int listID, DisplayListState state) = 0;

	GPUDebugOp DissassembleOp(u32 pc) {
		return DissassembleOp(pc, Memory::Read_U32(pc));
	}
	virtual GPUDebugOp DissassembleOp(u32 pc, u32 op) = 0;
	virtual std::vector<GPUDebugOp> DissassembleOpRange(u32 startpc, u32 endpc) = 0;

	virtual u32 GetRelativeAddress(u32 data) = 0;
	virtual u32 GetVertexAddress() = 0;
	virtual u32 GetIndexAddress() = 0;
	virtual GPUgstate GetGState() = 0;

	// TODO:
	// cached framebuffers / textures / vertices?
	// get content of framebuffer / texture
	// vertex / texture decoding?
};
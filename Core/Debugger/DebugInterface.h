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

#include "Common/CommonTypes.h"
#include <cstddef>

struct MemMap;

class DebugInterface {
public:
	virtual u32 GetPC() const = 0;
	virtual u32 GetRA() const = 0;
	virtual u32 GetHi() const = 0;
	virtual u32 GetLo() const = 0;
	virtual u32 GetLLBit() const = 0;
	virtual u32 GetFPCond() const = 0;
	virtual u32 GetGPR32Value(int reg) const = 0;

	virtual void SetPC(u32 _pc) = 0;
	virtual void SetHi(u32 val) = 0;
	virtual void SetLo(u32 val) = 0;

	virtual u32 GetRegValue(int cat, int index) const = 0;
	virtual void PrintRegValue(int cat, int index, char *out, size_t outSize) const = 0;
	virtual void SetRegValue(int cat, int index, u32 value) {}
};

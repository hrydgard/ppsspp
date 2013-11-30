// Copyright (c) 2013- PPSSPP Project.

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

#include "base/basictypes.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/FunctionWrappers.h"

void NormalizeVector(u32 vecPtr) {
	// TODO
}

// Can either replace with C functions or functions emitted in Asm/ArmAsm.
// The latter is recommended for fast math functions.
static const ReplacementTableEntry entries[] = {
	{ 0x0, 64, "NormalizeVector", WrapV_U<NormalizeVector>, 20 }
};

int GetNumReplacementFuncs() {
	return ARRAY_SIZE(entries);
}

int GetReplacementFuncIndex(u64 hash, int size) {
	// TODO: Build a lookup and keep it around
	for (int i = 0; i < ARRAY_SIZE(entries); i++) {
		if (entries[i].hash == hash && entries[i].size == size) {
			return i;
		}
	}
	return -1;
}

const ReplacementTableEntry *GetReplacementFunc(int i) {
	return &entries[i];
}
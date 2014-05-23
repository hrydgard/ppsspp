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

#pragma once
#include "Core/HLE/sceKernel.h"

class PointerWrap;

class HLEHelperThread {
public:
	// For savestates.
	HLEHelperThread();
	HLEHelperThread(const char *threadName, u32 instructions[], u32 instrCount, u32 prio, int stacksize);
	HLEHelperThread(const char *threadName, const char *module, const char *func, u32 prio, int stacksize);
	~HLEHelperThread();
	void DoState(PointerWrap &p);

	void Start(u32 a0, u32 a1);
	void Terminate();

private:
	void AllocEntry(u32 size);
	void Create(const char *threadName, u32 prio, int stacksize);

	SceUID id_;
	u32 entry_;
};
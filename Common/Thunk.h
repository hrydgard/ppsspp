// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include <map>

#include "Common/CommonTypes.h"
#include "Common/x64Emitter.h"

// This simple class creates a wrapper around a C/C++ function that saves all fp state
// before entering it, and restores it upon exit. This is required to be able to selectively
// call functions from generated code, without inflicting the performance hit and increase
// of complexity that it means to protect the generated code from this problem.

// This process is called thunking.
// Only used for X86 right now.

// There will only ever be one level of thunking on the stack, plus,
// we don't want to pollute the stack, so we store away regs somewhere global.
// NOT THREAD SAFE. This may only be used from the CPU thread.
// Any other thread using this stuff will be FATAL.
typedef Gen::XEmitter ThunkEmitter;
typedef Gen::XCodeBlock ThunkCodeBlock;

class ThunkManager : public ThunkCodeBlock
{
	std::map<const void *, const u8 *> thunks;

	const u8 *save_regs;
	const u8 *load_regs;

public:
	ThunkManager() {
		Init();
	}
	~ThunkManager() {
		Shutdown();
	}
	const void *ProtectFunction(const void *function, int num_params);

	template <typename Tr>
	const void *ProtectFunction(Tr (*func)()) {
		return ProtectFunction((const void *)func, 0);
	}

	template <typename Tr, typename T1>
	const void *ProtectFunction(Tr (*func)(T1)) {
		return ProtectFunction((const void *)func, 1);
	}

	template <typename Tr, typename T1, typename T2>
	const void *ProtectFunction(Tr (*func)(T1, T2)) {
		return ProtectFunction((const void *)func, 2);
	}

	template <typename Tr, typename T1, typename T2, typename T3>
	const void *ProtectFunction(Tr (*func)(T1, T2, T3)) {
		return ProtectFunction((const void *)func, 3);
	}

	template <typename Tr, typename T1, typename T2, typename T3, typename T4>
	const void *ProtectFunction(Tr (*func)(T1, T2, T3, T4)) {
		return ProtectFunction((const void *)func, 4);
	}

	void Enter(ThunkEmitter *emit, bool withinCall = false);
	void Leave(ThunkEmitter *emit, bool withinCall = false);

private:
	void Init();
	void Shutdown();
	void Reset();

	int ThunkStackOffset();
	int ThunkBytesNeeded();
};

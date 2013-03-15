// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <map>

#include "Common.h"
#include "MemoryUtil.h"
#include "Thunk.h"

#define THUNK_ARENA_SIZE 1024*1024*1

namespace
{

static u8 GC_ALIGNED32(saved_fp_state[16 * 4 * 4]);
static u8 GC_ALIGNED32(saved_gpr_state[16 * 8]);
static u16 saved_mxcsr;

}  // namespace

using namespace ArmGen;

void ThunkManager::Init()
{
}

void ThunkManager::Reset()
{
	thunks.clear();
	ResetCodePtr();
}

void ThunkManager::Shutdown()
{
	Reset();
	FreeCodeSpace();
}

void *ThunkManager::ProtectFunction(void *function, int num_params)
{
	_dbg_assert_msg_(JIT, false, "Arm ThunkManager not implemented?  Will crash.");
	return NULL;
}

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

#include <map>
#include "base/basictypes.h"

#ifdef ARM
#include "Common/ArmEmitter.h"
#else
#include "Common/x64Emitter.h"
#endif

#include "Globals.h"

class GLES_GPU;
class TransformDrawEngine;

typedef u32 (*JittedDisplayListEntry)(u32 *pc);

struct JittedDisplayList {
	JittedDisplayListEntry entry;
	int lastFrame;
	bool unreliable;
};

#ifdef ARM
class DisplayListCache : public ArmGen::ARMXCodeBlock {
#else
class DisplayListCache : public Gen::XCodeBlock {
#endif
public:
	DisplayListCache(GLES_GPU *gpu);

	void DecimateLists();
	bool Execute(u32 &pc, int &downcount);

private:
	void Initialize();
	JittedDisplayListEntry Compile(u32 &pc, int &downcount);

	static void DoFlush(TransformDrawEngine *t);
	static void DoExecuteOp(GLES_GPU *g, u32 op, u32 diff);

	void JitLoadPC();
	void JitStorePC();
	inline void JitFlush(u32 diff = 0, bool onChange = false);

	void Jit_Generic(u32 op);
	void Jit_Vaddr(u32 op);
	void Jit_Prim(u32 op);

	typedef void (DisplayListCache::*JitCmd)(u32 op);
	JitCmd cmds_[256];

	int compiledThisFrame_;
	int compiledFlips_;

	GLES_GPU *gpu_;
	std::map<u64, JittedDisplayList> jitted_;
};

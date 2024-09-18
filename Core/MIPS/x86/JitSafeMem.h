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

#include <vector>

class ThunkManager;

namespace MIPSComp {

class Jit;

class JitSafeMem {
public:
	JitSafeMem(Jit *jit, MIPSGPReg raddr, s32 offset, u32 alignMask = 0xFFFFFFFF);

	// Emit code necessary for a memory write, returns true if MOV to dest is needed.
	bool PrepareWrite(Gen::OpArg &dest, int size);
	// Emit code proceeding a slow write call, returns true if slow write is needed.
	bool PrepareSlowWrite();
	// Emit a slow write from src.
	void DoSlowWrite(const void *safeFunc, const Gen::OpArg& src, int suboffset = 0);
	template <typename T>
	void DoSlowWrite(void (*safeFunc)(T val, u32 addr), const Gen::OpArg& src, int suboffset = 0) {
		DoSlowWrite((const void *)safeFunc, src, suboffset);
	}

	// Emit code necessary for a memory read, returns true if MOV from src is needed.
	bool PrepareRead(Gen::OpArg &src, int size);
	// Emit code for a slow read call, and returns true if result is in EAX.
	bool PrepareSlowRead(const void *safeFunc);
	template <typename T>
	bool PrepareSlowRead(T (*safeFunc)(u32 addr)) {
		return PrepareSlowRead((const void *)safeFunc);
	}
		
	// Cleans up final code for the memory access.
	void Finish();

	// WARNING: Only works for non-GPR.  Do not use for reads into GPR.
	Gen::OpArg NextFastAddress(int suboffset);
	// WARNING: Only works for non-GPR.  Do not use for reads into GPR.
	void NextSlowRead(const void *safeFunc, int suboffset);
	template <typename T>
	void NextSlowRead(T (*safeFunc)(u32 addr), int suboffset) {
		NextSlowRead((const void *)safeFunc, suboffset);
	}

private:
	enum MemoryOpType {
		MEM_READ,
		MEM_WRITE,
	};

	Gen::OpArg PrepareMemoryOpArg(MemoryOpType type);
	void PrepareSlowAccess();
	bool ImmValid();
	void IndirectCALL(const void *safeFunc);

	Jit *jit_;
	MIPSGPReg raddr_;
	s32 offset_;
	int size_;
	bool needsCheck_;
	bool needsSkip_;
	bool fast_;
	u32 alignMask_;
	u32 iaddr_;
	Gen::X64Reg xaddr_;
	Gen::FixupBranch tooLow_, tooHigh_, skip_;
	std::vector<Gen::FixupBranch> skipChecks_;
	const u8 *safe_;
};

// Kept separate to avoid mistakes in the above class not using jit_.
class JitSafeMemFuncs : public Gen::XCodeBlock
{
public:
	JitSafeMemFuncs() {
	}
	~JitSafeMemFuncs() {
		Shutdown();
	}

	void Init(ThunkManager *thunks);
	void Shutdown();

	const u8 *readU32;
	const u8 *readU16;
	const u8 *readU8;
	const u8 *writeU32;
	const u8 *writeU16;
	const u8 *writeU8;

private:
	void CreateReadFunc(int bits, const void *fallbackFunc);
	void CreateWriteFunc(int bits, const void *fallbackFunc);

	void CheckDirectEAX();
	void StartDirectAccess();

	std::vector<Gen::FixupBranch> skips_;
	ThunkManager *thunks_;
};

};

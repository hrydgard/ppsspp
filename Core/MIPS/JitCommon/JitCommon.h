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
#include <string>

#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

// TODO: Find a better place for these.
std::vector<std::string> DisassembleArm2(const u8 *data, int size);
std::vector<std::string> DisassembleArm64(const u8 *data, int size);
std::vector<std::string> DisassembleX86(const u8 *data, int size);

struct JitBlock;
class JitBlockCache;
class JitBlockCacheDebugInterface;
class PointerWrap;

#ifdef USING_QT_UI
#undef emit
#endif

class MIPSState;

namespace MIPSComp {
	void JitAt();

	class MIPSFrontendInterface {
	public:
		virtual ~MIPSFrontendInterface() {}

		virtual void EatPrefix() = 0;

		virtual void Comp_Generic(MIPSOpcode op) = 0;
		virtual void Comp_RunBlock(MIPSOpcode op) = 0;
		virtual void Comp_ReplacementFunc(MIPSOpcode op) = 0;
		virtual void Comp_ITypeMem(MIPSOpcode op) = 0;
		virtual void Comp_Cache(MIPSOpcode op) = 0;
		virtual void Comp_RelBranch(MIPSOpcode op) = 0;
		virtual void Comp_RelBranchRI(MIPSOpcode op) = 0;
		virtual void Comp_FPUBranch(MIPSOpcode op) = 0;
		virtual void Comp_FPULS(MIPSOpcode op) = 0;
		virtual void Comp_FPUComp(MIPSOpcode op) = 0;
		virtual void Comp_Jump(MIPSOpcode op) = 0;
		virtual void Comp_JumpReg(MIPSOpcode op) = 0;
		virtual void Comp_Syscall(MIPSOpcode op) = 0;
		virtual void Comp_Break(MIPSOpcode op) = 0;
		virtual void Comp_IType(MIPSOpcode op) = 0;
		virtual void Comp_RType2(MIPSOpcode op) = 0;
		virtual void Comp_RType3(MIPSOpcode op) = 0;
		virtual void Comp_ShiftType(MIPSOpcode op) = 0;
		virtual void Comp_Allegrex(MIPSOpcode op) = 0;
		virtual void Comp_Allegrex2(MIPSOpcode op) = 0;
		virtual void Comp_VBranch(MIPSOpcode op) = 0;
		virtual void Comp_MulDivType(MIPSOpcode op) = 0;
		virtual void Comp_Special3(MIPSOpcode op) = 0;
		virtual void Comp_FPU3op(MIPSOpcode op) = 0;
		virtual void Comp_FPU2op(MIPSOpcode op) = 0;
		virtual void Comp_mxc1(MIPSOpcode op) = 0;
		virtual void Comp_SV(MIPSOpcode op) = 0;
		virtual void Comp_SVQ(MIPSOpcode op) = 0;
		virtual void Comp_VPFX(MIPSOpcode op) = 0;
		virtual void Comp_VVectorInit(MIPSOpcode op) = 0;
		virtual void Comp_VMatrixInit(MIPSOpcode op) = 0;
		virtual void Comp_VDot(MIPSOpcode op) = 0;
		virtual void Comp_VecDo3(MIPSOpcode op) = 0;
		virtual void Comp_VV2Op(MIPSOpcode op) = 0;
		virtual void Comp_Mftv(MIPSOpcode op) = 0;
		virtual void Comp_Vmfvc(MIPSOpcode op) = 0;
		virtual void Comp_Vmtvc(MIPSOpcode op) = 0;
		virtual void Comp_Vmmov(MIPSOpcode op) = 0;
		virtual void Comp_VScl(MIPSOpcode op) = 0;
		virtual void Comp_Vmmul(MIPSOpcode op) = 0;
		virtual void Comp_Vmscl(MIPSOpcode op) = 0;
		virtual void Comp_Vtfm(MIPSOpcode op) = 0;
		virtual void Comp_VHdp(MIPSOpcode op) = 0;
		virtual void Comp_VCrs(MIPSOpcode op) = 0;
		virtual void Comp_VDet(MIPSOpcode op) = 0;
		virtual void Comp_Vi2x(MIPSOpcode op) = 0;
		virtual void Comp_Vx2i(MIPSOpcode op) = 0;
		virtual void Comp_Vf2i(MIPSOpcode op) = 0;
		virtual void Comp_Vi2f(MIPSOpcode op) = 0;
		virtual void Comp_Vh2f(MIPSOpcode op) = 0;
		virtual void Comp_Vcst(MIPSOpcode op) = 0;
		virtual void Comp_Vhoriz(MIPSOpcode op) = 0;
		virtual void Comp_VRot(MIPSOpcode op) = 0;
		virtual void Comp_VIdt(MIPSOpcode op) = 0;
		virtual void Comp_Vcmp(MIPSOpcode op) = 0;
		virtual void Comp_Vcmov(MIPSOpcode op) = 0;
		virtual void Comp_Viim(MIPSOpcode op) = 0;
		virtual void Comp_Vfim(MIPSOpcode op) = 0;
		virtual void Comp_VCrossQuat(MIPSOpcode op) = 0;
		virtual void Comp_Vsgn(MIPSOpcode op) = 0;
		virtual void Comp_Vocp(MIPSOpcode op) = 0;
		virtual void Comp_ColorConv(MIPSOpcode op) = 0;
		virtual void Comp_Vbfy(MIPSOpcode op) = 0;
		virtual void Comp_DoNothing(MIPSOpcode op) = 0;

		virtual int Replace_fabsf() = 0;
	};

	class JitInterface {
	public:
		virtual ~JitInterface() {}

		virtual bool CodeInRange(const u8 *ptr) const = 0;
		virtual bool DescribeCodePtr(const u8 *ptr, std::string &name) = 0;
		virtual const u8 *GetDispatcher() const = 0;
		virtual const u8 *GetCrashHandler() const = 0;
		virtual JitBlockCache *GetBlockCache() = 0;
		virtual JitBlockCacheDebugInterface *GetBlockCacheDebugInterface() = 0;
		virtual void InvalidateCacheAt(u32 em_address, int length = 4) = 0;
		virtual void DoState(PointerWrap &p) = 0;
		virtual void RunLoopUntil(u64 globalticks) = 0;
		virtual void Compile(u32 em_address) = 0;
		virtual void CompileFunction(u32 start_address, u32 length) { }
		virtual void ClearCache() = 0;
		virtual void UpdateFCR31() = 0;
		virtual MIPSOpcode GetOriginalOp(MIPSOpcode op) = 0;

		// No jit operations may be run between these calls.
		// Meant to be used to make memory safe for savestates, memcpy, etc.
		virtual std::vector<u32> SaveAndClearEmuHackOps() = 0;
		virtual void RestoreSavedEmuHackOps(std::vector<u32> saved) = 0;

		// Block linking. This may need to work differently for whole-function JITs and stuff
		// like that.
		virtual void LinkBlock(u8 *exitPoint, const u8 *entryPoint) = 0;
		virtual void UnlinkBlock(u8 *checkedEntry, u32 originalAddress) = 0;
	};

	typedef void (MIPSFrontendInterface::*MIPSCompileFunc)(MIPSOpcode opcode);
	typedef int (MIPSFrontendInterface::*MIPSReplaceFunc)();

	extern JitInterface *jit;

	void DoDummyJitState(PointerWrap &p);

	JitInterface *CreateNativeJit(MIPSState *mips);
}

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

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/x86/JitSafeMem.h"
#include "Core/System.h"

namespace MIPSComp
{

void JitMemCheck(u32 addr, int size, int isWrite)
{
	// Should we skip this breakpoint?
	if (CBreakPoints::CheckSkipFirst() == currentMIPS->pc)
		return;

	// Did we already hit one?
	if (coreState != CORE_RUNNING && coreState != CORE_NEXTFRAME)
		return;

	CBreakPoints::ExecMemCheckJitBefore(addr, isWrite == 1, size, currentMIPS->pc);
}

void JitMemCheckCleanup()
{
	CBreakPoints::ExecMemCheckJitCleanup();
}

JitSafeMem::JitSafeMem(Jit *jit, MIPSGPReg raddr, s32 offset, u32 alignMask)
	: jit_(jit), raddr_(raddr), offset_(offset), needsCheck_(false), needsSkip_(false), alignMask_(alignMask)
{
	// This makes it more instructions, so let's play it safe and say we need a far jump.
	far_ = !g_Config.bIgnoreBadMemAccess || !CBreakPoints::GetMemChecks().empty();
	if (jit_->gpr.IsImm(raddr_))
		iaddr_ = jit_->gpr.GetImm(raddr_) + offset_;
	else
		iaddr_ = (u32) -1;

	fast_ = g_Config.bFastMemory || raddr == MIPS_REG_SP;
}

void JitSafeMem::SetFar()
{
	_dbg_assert_msg_(JIT, !needsSkip_, "Sorry, you need to call SetFar() earlier.");
	far_ = true;
}

bool JitSafeMem::PrepareWrite(OpArg &dest, int size)
{
	size_ = size;
	// If it's an immediate, we can do the write if valid.
	if (iaddr_ != (u32) -1)
	{
		if (ImmValid())
		{
			MemCheckImm(MEM_WRITE);

#ifdef _M_IX86
			dest = M(Memory::base + (iaddr_ & Memory::MEMVIEW32_MASK & alignMask_));
#else
			dest = MDisp(RBX, iaddr_ & alignMask_);
#endif
			return true;
		}
		else
			return false;
	}
	// Otherwise, we always can do the write (conditionally.)
	else
		dest = PrepareMemoryOpArg(MEM_WRITE);
	return true;
}

bool JitSafeMem::PrepareRead(OpArg &src, int size)
{
	size_ = size;
	if (iaddr_ != (u32) -1)
	{
		if (ImmValid())
		{
			MemCheckImm(MEM_READ);

#ifdef _M_IX86
			src = M(Memory::base + (iaddr_ & Memory::MEMVIEW32_MASK & alignMask_));
#else
			src = MDisp(RBX, iaddr_ & alignMask_);
#endif
			return true;
		}
		else
			return false;
	}
	else
		src = PrepareMemoryOpArg(MEM_READ);
	return true;
}

OpArg JitSafeMem::NextFastAddress(int suboffset)
{
	if (jit_->gpr.IsImm(raddr_))
	{
		u32 addr = (jit_->gpr.GetImm(raddr_) + offset_ + suboffset) & alignMask_;

#ifdef _M_IX86
		return M(Memory::base + (addr & Memory::MEMVIEW32_MASK));
#else
		return MDisp(RBX, addr);
#endif
	}

	_dbg_assert_msg_(JIT, (suboffset & alignMask_) == suboffset, "suboffset must be aligned");

#ifdef _M_IX86
	return MDisp(xaddr_, (u32) Memory::base + offset_ + suboffset);
#else
	return MComplex(RBX, xaddr_, SCALE_1, offset_ + suboffset);
#endif
}

OpArg JitSafeMem::PrepareMemoryOpArg(ReadType type)
{
	// We may not even need to move into EAX as a temporary.
	bool needTemp = alignMask_ != 0xFFFFFFFF;
#ifdef _M_IX86
	// We always mask on 32 bit in fast memory mode.
	needTemp = needTemp || fast_;
#endif

	if (jit_->gpr.R(raddr_).IsSimpleReg() && !needTemp)
	{
		jit_->gpr.MapReg(raddr_, true, false);
		xaddr_ = jit_->gpr.RX(raddr_);
	}
	else
	{
		jit_->MOV(32, R(EAX), jit_->gpr.R(raddr_));
		xaddr_ = EAX;
	}

	MemCheckAsm(type);

	if (!fast_)
	{
		// Is it in physical ram?
		jit_->CMP(32, R(xaddr_), Imm32(PSP_GetKernelMemoryBase() - offset_));
		tooLow_ = jit_->J_CC(CC_B);
		jit_->CMP(32, R(xaddr_), Imm32(PSP_GetUserMemoryEnd() - offset_ - (size_ - 1)));
		tooHigh_ = jit_->J_CC(CC_AE);

		// We may need to jump back up here.
		safe_ = jit_->GetCodePtr();
	}
	else
	{
#ifdef _M_IX86
		jit_->AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
#endif
	}

	// TODO: This could be more optimal, but the common case is that we want xaddr_ not to include offset_.
	// Since we need to align them after add, we add and subtract.
	if (alignMask_ != 0xFFFFFFFF)
	{
		jit_->ADD(32, R(xaddr_), Imm32(offset_));
		jit_->AND(32, R(xaddr_), Imm32(alignMask_));
		jit_->SUB(32, R(xaddr_), Imm32(offset_));
	}

#ifdef _M_IX86
	return MDisp(xaddr_, (u32) Memory::base + offset_);
#else
	return MComplex(RBX, xaddr_, SCALE_1, offset_);
#endif
}

void JitSafeMem::PrepareSlowAccess()
{
	// Skip the fast path (which the caller wrote just now.)
	skip_ = jit_->J(far_);
	needsSkip_ = true;
	jit_->SetJumpTarget(tooLow_);
	jit_->SetJumpTarget(tooHigh_);

	// Might also be the scratchpad.
	jit_->CMP(32, R(xaddr_), Imm32(PSP_GetScratchpadMemoryBase() - offset_));
	FixupBranch tooLow = jit_->J_CC(CC_B);
	jit_->CMP(32, R(xaddr_), Imm32(PSP_GetScratchpadMemoryEnd() - offset_ - (size_ - 1)));
	jit_->J_CC(CC_B, safe_);
	jit_->SetJumpTarget(tooLow);
}

bool JitSafeMem::PrepareSlowWrite()
{
	// If it's immediate, we only need a slow write on invalid.
	if (iaddr_ != (u32) -1)
		return !fast_ && !ImmValid();

	if (!fast_)
	{
		PrepareSlowAccess();
		return true;
	}
	else
		return false;
}

void JitSafeMem::DoSlowWrite(const void *safeFunc, const OpArg src, int suboffset)
{
	if (iaddr_ != (u32) -1)
		jit_->MOV(32, R(EAX), Imm32((iaddr_ + suboffset) & alignMask_));
	else
	{
		jit_->LEA(32, EAX, MDisp(xaddr_, offset_ + suboffset));
		if (alignMask_ != 0xFFFFFFFF)
			jit_->AND(32, R(EAX), Imm32(alignMask_));
	}

#ifdef _M_IX86
	jit_->PUSH(EDX);
#endif
	if (!src.IsSimpleReg(EDX)) {
		jit_->MOV(32, R(EDX), src);
	}
	// This is a special jit-ABI'd function.
	jit_->CALL(safeFunc);
#ifdef _M_IX86
	jit_->POP(EDX);
#endif
	needsCheck_ = true;
}

bool JitSafeMem::PrepareSlowRead(const void *safeFunc)
{
	if (!fast_)
	{
		if (iaddr_ != (u32) -1)
		{
			// No slow read necessary.
			if (ImmValid())
				return false;
			jit_->MOV(32, R(EAX), Imm32(iaddr_ & alignMask_));
		}
		else
		{
			PrepareSlowAccess();
			jit_->LEA(32, EAX, MDisp(xaddr_, offset_));
			if (alignMask_ != 0xFFFFFFFF)
				jit_->AND(32, R(EAX), Imm32(alignMask_));
		}

		// This is a special jit-ABI'd function.
		jit_->CALL(safeFunc);
		needsCheck_ = true;
		return true;
	}
	else
		return false;
}

void JitSafeMem::NextSlowRead(const void *safeFunc, int suboffset)
{
	_dbg_assert_msg_(JIT, !fast_, "NextSlowRead() called in fast memory mode?");

	// For simplicity, do nothing for 0.  We already read in PrepareSlowRead().
	if (suboffset == 0)
		return;

	if (jit_->gpr.IsImm(raddr_))
	{
		_dbg_assert_msg_(JIT, !Memory::IsValidAddress(iaddr_ + suboffset), "NextSlowRead() for an invalid immediate address?");

		jit_->MOV(32, R(EAX), Imm32((iaddr_ + suboffset) & alignMask_));
	}
	// For GPR, if xaddr_ was the dest register, this will be wrong.  Don't use in GPR.
	else
	{
		jit_->LEA(32, EAX, MDisp(xaddr_, offset_ + suboffset));
		if (alignMask_ != 0xFFFFFFFF)
			jit_->AND(32, R(EAX), Imm32(alignMask_));
	}

	// This is a special jit-ABI'd function.
	jit_->CALL(safeFunc);
}

bool JitSafeMem::ImmValid()
{
	return iaddr_ != (u32) -1 && Memory::IsValidAddress(iaddr_) && Memory::IsValidAddress(iaddr_ + size_ - 1);
}

void JitSafeMem::Finish()
{
	// Memory::Read_U32/etc. may have tripped coreState.
	if (needsCheck_ && !g_Config.bIgnoreBadMemAccess)
		jit_->js.afterOp |= JitState::AFTER_CORE_STATE;
	if (needsSkip_)
		jit_->SetJumpTarget(skip_);
	for (auto it = skipChecks_.begin(), end = skipChecks_.end(); it != end; ++it)
		jit_->SetJumpTarget(*it);
}

void JitSafeMem::MemCheckImm(ReadType type)
{
	MemCheck *check = CBreakPoints::GetMemCheck(iaddr_, size_);
	if (check)
	{
		if (!(check->cond & MEMCHECK_READ) && type == MEM_READ)
			return;
		if (!(check->cond & MEMCHECK_WRITE) && type == MEM_WRITE)
			return;

		jit_->MOV(32, M(&jit_->mips_->pc), Imm32(jit_->js.compilerPC));
		jit_->CallProtectedFunction(&JitMemCheck, iaddr_, size_, type == MEM_WRITE ? 1 : 0);

		// CORE_RUNNING is <= CORE_NEXTFRAME.
		jit_->CMP(32, M(&coreState), Imm32(CORE_NEXTFRAME));
		skipChecks_.push_back(jit_->J_CC(CC_G, true));
		jit_->js.afterOp |= JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE | JitState::AFTER_MEMCHECK_CLEANUP;
	}
}

void JitSafeMem::MemCheckAsm(ReadType type)
{
	const auto memchecks = CBreakPoints::GetMemCheckRanges();
	bool possible = false;
	for (auto it = memchecks.begin(), end = memchecks.end(); it != end; ++it)
	{
		if (!(it->cond & MEMCHECK_READ) && type == MEM_READ)
			continue;
		if (!(it->cond & MEMCHECK_WRITE) && type == MEM_WRITE)
			continue;

		possible = true;

		FixupBranch skipNext, skipNextRange;
		if (it->end != 0)
		{
			jit_->CMP(32, R(xaddr_), Imm32(it->start - offset_ - size_));
			skipNext = jit_->J_CC(CC_BE);
			jit_->CMP(32, R(xaddr_), Imm32(it->end - offset_));
			skipNextRange = jit_->J_CC(CC_AE);
		}
		else
		{
			jit_->CMP(32, R(xaddr_), Imm32(it->start - offset_));
			skipNext = jit_->J_CC(CC_NE);
		}

		// Keep the stack 16-byte aligned, just PUSH/POP 4 times.
		for (int i = 0; i < 4; ++i)
			jit_->PUSH(xaddr_);
		jit_->MOV(32, M(&jit_->mips_->pc), Imm32(jit_->js.compilerPC));
		jit_->ADD(32, R(xaddr_), Imm32(offset_));
		jit_->CallProtectedFunction(&JitMemCheck, R(xaddr_), size_, type == MEM_WRITE ? 1 : 0);
		for (int i = 0; i < 4; ++i)
			jit_->POP(xaddr_);

		jit_->SetJumpTarget(skipNext);
		if (it->end != 0)
			jit_->SetJumpTarget(skipNextRange);
	}

	if (possible)
	{
		// CORE_RUNNING is <= CORE_NEXTFRAME.
		jit_->CMP(32, M(&coreState), Imm32(CORE_NEXTFRAME));
		skipChecks_.push_back(jit_->J_CC(CC_G, true));
		jit_->js.afterOp |= JitState::AFTER_CORE_STATE | JitState::AFTER_REWIND_PC_BAD_STATE | JitState::AFTER_MEMCHECK_CLEANUP;
	}
}

static const int FUNCS_ARENA_SIZE = 512 * 1024;

void JitSafeMemFuncs::Init(ThunkManager *thunks) {
	using namespace Gen;

	AllocCodeSpace(FUNCS_ARENA_SIZE);
	thunks_ = thunks;

	readU32 = GetCodePtr();
	CreateReadFunc(32, (const void *)&Memory::Read_U32);
	readU16 = GetCodePtr();
	CreateReadFunc(16, (const void *)&Memory::Read_U16);
	readU8 = GetCodePtr();
	CreateReadFunc(8, (const void *)&Memory::Read_U8);

	writeU32 = GetCodePtr();
	CreateWriteFunc(32, (const void *)&Memory::Write_U32);
	writeU16 = GetCodePtr();
	CreateWriteFunc(16, (const void *)&Memory::Write_U16);
	writeU8 = GetCodePtr();
	CreateWriteFunc(8, (const void *)&Memory::Write_U8);
}

void JitSafeMemFuncs::Shutdown() {
	ResetCodePtr();
	FreeCodeSpace();
}

// Mini ABI:
//   Read funcs take address in EAX, return in RAX.
//   Write funcs take address in EAX, data in RDX.
//   On x86-32, Write funcs also have an extra 4 bytes on the stack.

void JitSafeMemFuncs::CreateReadFunc(int bits, const void *fallbackFunc) {
	CheckDirectEAX();

	// Since we were CALLed, we need to align the stack before calling C++.
#ifdef _M_IX86
	SUB(32, R(ESP), Imm8(16 - 4));
	ABI_CallFunctionA(thunks_->ProtectFunction(fallbackFunc, 1), R(EAX));
	ADD(32, R(ESP), Imm8(16 - 4));
#else
	SUB(64, R(RSP), Imm8(0x28));
	ABI_CallFunctionA(thunks_->ProtectFunction(fallbackFunc, 1), R(EAX));
	ADD(64, R(RSP), Imm8(0x28));
#endif

	RET();

	StartDirectAccess();

#ifdef _M_IX86
	MOVZX(32, bits, EAX, MDisp(EAX, (u32)Memory::base));
#else
	MOVZX(32, bits, EAX, MRegSum(RBX, EAX));
#endif

	RET();
}

void JitSafeMemFuncs::CreateWriteFunc(int bits, const void *fallbackFunc) {
	CheckDirectEAX();

	// Since we were CALLed, we need to align the stack before calling C++.
#ifdef _M_IX86
	// 4 for return, 4 for saved reg on stack.
	SUB(32, R(ESP), Imm8(16 - 4 - 4));
	ABI_CallFunctionAA(thunks_->ProtectFunction(fallbackFunc, 2), R(EDX), R(EAX));
	ADD(32, R(ESP), Imm8(16 - 4 - 4));
#else
	SUB(64, R(RSP), Imm8(0x28));
	ABI_CallFunctionAA(thunks_->ProtectFunction(fallbackFunc, 2), R(EDX), R(EAX));
	ADD(64, R(RSP), Imm8(0x28));
#endif

	RET();

	StartDirectAccess();

#ifdef _M_IX86
	MOV(bits, MDisp(EAX, (u32)Memory::base), R(EDX));
#else
	MOV(bits, MRegSum(RBX, EAX), R(EDX));
#endif

	RET();
}

void JitSafeMemFuncs::CheckDirectEAX() {
	// Clear any cache/kernel bits.
	AND(32, R(EAX), Imm32(0x3FFFFFFF));
	
	CMP(32, R(EAX), Imm32(PSP_GetUserMemoryEnd()));
	FixupBranch tooHighRAM = J_CC(CC_AE);
	CMP(32, R(EAX), Imm32(PSP_GetKernelMemoryBase()));
	skips_.push_back(J_CC(CC_AE));
	
	CMP(32, R(EAX), Imm32(PSP_GetVidMemEnd()));
	FixupBranch tooHighVid = J_CC(CC_AE);
	CMP(32, R(EAX), Imm32(PSP_GetVidMemBase()));
	skips_.push_back(J_CC(CC_AE));
	
	CMP(32, R(EAX), Imm32(PSP_GetScratchpadMemoryEnd()));
	FixupBranch tooHighScratch = J_CC(CC_AE);
	CMP(32, R(EAX), Imm32(PSP_GetScratchpadMemoryBase()));
	skips_.push_back(J_CC(CC_AE));

	SetJumpTarget(tooHighRAM);
	SetJumpTarget(tooHighVid);
	SetJumpTarget(tooHighScratch);
}

void JitSafeMemFuncs::StartDirectAccess() {
	for (auto it = skips_.begin(), end = skips_.end(); it != end; ++it) {
		SetJumpTarget(*it);
	}
	skips_.clear();
}

};

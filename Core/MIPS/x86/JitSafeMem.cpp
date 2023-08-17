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

#include "ppsspp_config.h"

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include "Core/Config.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/x86/JitSafeMem.h"
#include "Core/System.h"

namespace MIPSComp
{
using namespace Gen;
using namespace X64JitConstants;

JitSafeMem::JitSafeMem(Jit *jit, MIPSGPReg raddr, s32 offset, u32 alignMask)
	: jit_(jit), raddr_(raddr), offset_(offset), needsCheck_(false), needsSkip_(false), alignMask_(alignMask)
{
	// Mask out the kernel RAM bit, because we'll end up with a negative offset to MEMBASEREG.
	if (jit_->gpr.IsImm(raddr_))
		iaddr_ = (jit_->gpr.GetImm(raddr_) + offset_) & 0x7FFFFFFF;
	else
		iaddr_ = (u32) -1;

	fast_ = g_Config.bFastMemory || raddr == MIPS_REG_SP;

	// If raddr_ is going to get loaded soon, load it now for more optimal code.
	// We assume that it was already locked.
	const int LOOKAHEAD_OPS = 3;
	if (!jit_->gpr.R(raddr_).IsImm() && MIPSAnalyst::IsRegisterUsed(raddr_, jit_->GetCompilerPC() + 4, LOOKAHEAD_OPS))
		jit_->gpr.MapReg(raddr_, true, false);
}

bool JitSafeMem::PrepareWrite(OpArg &dest, int size)
{
	size_ = size;
	// If it's an immediate, we can do the write if valid.
	if (iaddr_ != (u32) -1)
	{
		if (ImmValid())
		{
			u32 addr = (iaddr_ & alignMask_);
#ifdef MASKED_PSP_MEMORY
			addr &= Memory::MEMVIEW32_MASK;
#endif

#if PPSSPP_ARCH(32BIT)
			dest = M(Memory::base + addr);  // 32-bit only
#else
			dest = MDisp(MEMBASEREG, addr);
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
			u32 addr = (iaddr_ & alignMask_);
#ifdef MASKED_PSP_MEMORY
			addr &= Memory::MEMVIEW32_MASK;
#endif

#if PPSSPP_ARCH(32BIT)
			src = M(Memory::base + addr);  // 32-bit only
#else
			src = MDisp(MEMBASEREG, addr);
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
	if (iaddr_ != (u32) -1)
	{
		u32 addr = (iaddr_ + suboffset) & alignMask_;
#ifdef MASKED_PSP_MEMORY
		addr &= Memory::MEMVIEW32_MASK;
#endif

#if PPSSPP_ARCH(32BIT)
		return M(Memory::base + addr);  // 32-bit only
#else
		return MDisp(MEMBASEREG, addr);
#endif
	}

	_dbg_assert_msg_((suboffset & alignMask_) == suboffset, "suboffset must be aligned");

#if PPSSPP_ARCH(32BIT)
	return MDisp(xaddr_, (u32) Memory::base + offset_ + suboffset);
#else
	return MComplex(MEMBASEREG, xaddr_, SCALE_1, offset_ + suboffset);
#endif
}

OpArg JitSafeMem::PrepareMemoryOpArg(MemoryOpType type)
{
	// We may not even need to move into EAX as a temporary.
	bool needTemp = alignMask_ != 0xFFFFFFFF;

#ifdef MASKED_PSP_MEMORY
	bool needMask = true; // raddr_ != MIPS_REG_SP;    // Commented out this speedhack due to low impact
	// We always mask on 32 bit in fast memory mode.
	needTemp = needTemp || (fast_ && needMask);
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
#ifdef MASKED_PSP_MEMORY
		if (needMask) {
			jit_->AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
		}
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

#if PPSSPP_ARCH(32BIT)
	return MDisp(xaddr_, (u32) Memory::base + offset_);
#else
	return MComplex(MEMBASEREG, xaddr_, SCALE_1, offset_);
#endif
}

void JitSafeMem::PrepareSlowAccess()
{
	// Skip the fast path (which the caller wrote just now.)
	skip_ = jit_->J(true);
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

void JitSafeMem::DoSlowWrite(const void *safeFunc, const OpArg &src, int suboffset) {
	_dbg_assert_msg_(safeFunc != nullptr, "Safe func cannot be null");

	if (iaddr_ != (u32) -1)
		jit_->MOV(32, R(EAX), Imm32((iaddr_ + suboffset) & alignMask_));
	else
	{
		jit_->LEA(32, EAX, MDisp(xaddr_, offset_ + suboffset));
		if (alignMask_ != 0xFFFFFFFF)
			jit_->AND(32, R(EAX), Imm32(alignMask_));
	}

#if PPSSPP_ARCH(32BIT)
	jit_->PUSH(EDX);
#endif
	if (!src.IsSimpleReg(EDX)) {
		jit_->MOV(32, R(EDX), src);
	}
	if (!g_Config.bIgnoreBadMemAccess) {
		jit_->MOV(32, MIPSSTATE_VAR(pc), Imm32(jit_->GetCompilerPC()));
	}
	// This is a special jit-ABI'd function.
	if (jit_->CanCALLDirect(safeFunc)) {
		jit_->CALL(safeFunc);
	} else {
		// We can't safely flush a reg, but this shouldn't be normal.
		IndirectCALL(safeFunc);
	}
#if PPSSPP_ARCH(32BIT)
	jit_->POP(EDX);
#endif
	needsCheck_ = true;
}

bool JitSafeMem::PrepareSlowRead(const void *safeFunc) {
	_dbg_assert_msg_(safeFunc != nullptr, "Safe func cannot be null");
	if (!fast_) {
		if (iaddr_ != (u32) -1) {
			// No slow read necessary.
			if (ImmValid())
				return false;
			jit_->MOV(32, R(EAX), Imm32(iaddr_ & alignMask_));
		} else {
			PrepareSlowAccess();
			jit_->LEA(32, EAX, MDisp(xaddr_, offset_));
			if (alignMask_ != 0xFFFFFFFF)
				jit_->AND(32, R(EAX), Imm32(alignMask_));
		}

		if (!g_Config.bIgnoreBadMemAccess) {
			jit_->MOV(32, MIPSSTATE_VAR(pc), Imm32(jit_->GetCompilerPC()));
		}
		// This is a special jit-ABI'd function.
		if (jit_->CanCALLDirect(safeFunc)) {
			jit_->CALL(safeFunc);
		} else {
			// We can't safely flush a reg, but this shouldn't be normal.
			IndirectCALL(safeFunc);
		}
		needsCheck_ = true;
		return true;
	}
	else
		return false;
}

void JitSafeMem::NextSlowRead(const void *safeFunc, int suboffset) {
	_dbg_assert_msg_(safeFunc != nullptr, "Safe func cannot be null");
	_dbg_assert_msg_(!fast_, "NextSlowRead() called in fast memory mode?");

	// For simplicity, do nothing for 0.  We already read in PrepareSlowRead().
	if (suboffset == 0)
		return;

	if (jit_->gpr.IsImm(raddr_))
	{
		_dbg_assert_msg_(!Memory::IsValidAddress(iaddr_ + suboffset), "NextSlowRead() for an invalid immediate address?");

		jit_->MOV(32, R(EAX), Imm32((iaddr_ + suboffset) & alignMask_));
	}
	// For GPR, if xaddr_ was the dest register, this will be wrong.  Don't use in GPR.
	else
	{
		jit_->LEA(32, EAX, MDisp(xaddr_, offset_ + suboffset));
		if (alignMask_ != 0xFFFFFFFF)
			jit_->AND(32, R(EAX), Imm32(alignMask_));
	}

	if (!g_Config.bIgnoreBadMemAccess) {
		jit_->MOV(32, MIPSSTATE_VAR(pc), Imm32(jit_->GetCompilerPC()));
	}
	// This is a special jit-ABI'd function.
	if (jit_->CanCALLDirect(safeFunc)) {
		jit_->CALL(safeFunc);
	} else {
		// We can't safely flush a reg, but this shouldn't be normal.
		IndirectCALL(safeFunc);
	}
}

bool JitSafeMem::ImmValid()
{
	return iaddr_ != (u32) -1 && Memory::IsValidAddress(iaddr_) && Memory::IsValidAddress(iaddr_ + size_ - 1);
}

void JitSafeMem::IndirectCALL(const void *safeFunc) {
#if PPSSPP_ARCH(32BIT)
	jit_->PUSH(ECX);
	jit_->SUB(PTRBITS, R(ESP), Imm8(16 - 4));
	jit_->MOV(PTRBITS, R(ECX), ImmPtr(safeFunc));
	jit_->CALLptr(R(RCX));
	jit_->ADD(PTRBITS, R(ESP), Imm8(16 - 4));
	jit_->POP(ECX);
#else
	jit_->PUSH(RCX);
	jit_->SUB(PTRBITS, R(RSP), Imm8(8));
	jit_->MOV(PTRBITS, R(RCX), ImmPtr(safeFunc));
	jit_->CALLptr(R(RCX));
	jit_->ADD(64, R(RSP), Imm8(8));
	jit_->POP(RCX);
#endif
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

static const int FUNCS_ARENA_SIZE = 512 * 1024;

void JitSafeMemFuncs::Init(ThunkManager *thunks) {
	using namespace Gen;

	AllocCodeSpace(FUNCS_ARENA_SIZE);
	thunks_ = thunks;

	BeginWrite(1024);
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
	EndWrite();
}

void JitSafeMemFuncs::Shutdown() {
	ResetCodePtr(0);
	FreeCodeSpace();

	readU32 = nullptr;
	readU16 = nullptr;
	readU8 = nullptr;
	writeU32 = nullptr;
	writeU16 = nullptr;
	writeU8 = nullptr;
}

// Mini ABI:
//   Read funcs take address in EAX, return in RAX.
//   Write funcs take address in EAX, data in RDX.
//   On x86-32, Write funcs also have an extra 4 bytes on the stack.

void JitSafeMemFuncs::CreateReadFunc(int bits, const void *fallbackFunc) {
	CheckDirectEAX();

	// Since we were CALLed, we need to align the stack before calling C++.
#if PPSSPP_ARCH(32BIT)
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

#if PPSSPP_ARCH(32BIT)
	MOVZX(32, bits, EAX, MDisp(EAX, (u32)Memory::base));
#else
	MOVZX(32, bits, EAX, MRegSum(MEMBASEREG, EAX));
#endif

	RET();
}

void JitSafeMemFuncs::CreateWriteFunc(int bits, const void *fallbackFunc) {
	CheckDirectEAX();

	// Since we were CALLed, we need to align the stack before calling C++.
#if PPSSPP_ARCH(32BIT)
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

#if PPSSPP_ARCH(32BIT)
	MOV(bits, MDisp(EAX, (u32)Memory::base), R(EDX));
#else
	MOV(bits, MRegSum(MEMBASEREG, EAX), R(EDX));
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

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

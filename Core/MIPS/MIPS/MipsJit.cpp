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
#if PPSSPP_ARCH(MIPS)

#include "Common/Profiler/Profiler.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/HLE/ReplaceTables.h"

#include "MipsJit.h"
#include "CPUDetect.h"

void DisassembleMIPS(const u8 *data, int size) {
}

namespace MIPSComp
{

MipsJit::MipsJit(MIPSState *mipsState) : blocks(mipsState, this), mips_(mipsState)
{ 
	logBlocks = 0;
	dontLogBlocks = 0;
	blocks.Init();
	AllocCodeSpace(1024 * 1024 * 16);
	js.startDefaultPrefix = mips_->HasDefaultPrefix();
}

void MipsJit::DoState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	Do(p, js.startDefaultPrefix);
	if (s >= 2) {
		Do(p, js.hasSetRounding);
		if (p.mode == PointerWrap::MODE_READ) {
			js.lastSetRounding = 0;
		}
	} else {
		js.hasSetRounding = 1;
	}
}

void MipsJit::UpdateFCR31() {
}

void MipsJit::FlushAll()
{
	//gpr.FlushAll();
	//fpr.FlushAll();
	FlushPrefixV();
}

void MipsJit::FlushPrefixV()
{
}

MIPSOpcode MipsJit::GetOriginalOp(MIPSOpcode op) {
	JitBlockCache *bc = GetBlockCache();
	int block_num = bc->GetBlockNumberFromEmuHackOp(op, true);
	if (block_num >= 0) {
		return bc->GetOriginalFirstOp(block_num);
	} else {
		return op;
	}
}

void MipsJit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace(0);
	//GenerateFixedCode();
}

void MipsJit::InvalidateCacheAt(u32 em_address, int length) {
	if (blocks.RangeMayHaveEmuHacks(em_address, em_address + length)) {
		blocks.InvalidateICache(em_address, length);
	}
}

void MipsJit::EatInstruction(MIPSOpcode op) {
	MIPSInfo info = MIPSGetInfo(op);
	if (info & DELAYSLOT) {
		ERROR_LOG_REPORT_ONCE(ateDelaySlot, JIT, "Ate a branch op.");
	}
	if (js.inDelaySlot) {
		ERROR_LOG_REPORT_ONCE(ateInDelaySlot, JIT, "Ate an instruction inside a delay slot.");
	}

	js.numInstructions++;
	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
}

void MipsJit::CompileDelaySlot(int flags)
{
	//if (flags & DELAYSLOT_SAFE)
	//	Save flags here

	js.inDelaySlot = true;
	MIPSOpcode op = Memory::Read_Opcode_JIT(js.compilerPC + 4);
	MIPSCompileOp(op, this);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();
	//if (flags & DELAYSLOT_SAFE)
	//	Restore flags here
}


void MipsJit::Compile(u32 em_address) {
	PROFILE_THIS_SCOPE("jitc");
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull()) {
		ClearCache();
	}
	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(em_address, b);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink);

	bool cleanSlate = false;

	if (js.hasSetRounding && !js.lastSetRounding) {
		WARN_LOG(Log::JIT, "Detected rounding mode usage, rebuilding jit with checks");
		// Won't loop, since hasSetRounding is only ever set to 1.
		js.lastSetRounding = js.hasSetRounding;
		cleanSlate = true;
	}

	if (cleanSlate) {
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();
		Compile(em_address);
	}
}

void MipsJit::RunLoopUntil(u64 globalticks)
{
	PROFILE_THIS_SCOPE("jit");
	((void (*)())enterCode)();
}

const u8 *MipsJit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.lastContinuedPC = 0;
	js.initialBlockSize = 0;
	js.nextExit = 0;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;
	js.PrefixStart();
	b->normalEntry = GetCodePtr();
	js.numInstructions = 0;
	while (js.compiling)
	{
		MIPSOpcode inst = Memory::Read_Opcode_JIT(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst, this);

		js.compilerPC += 4;
		js.numInstructions++;

		// Safety check, in case we get a bunch of really large jit ops without a lot of branching.
		if (GetSpaceLeft() < 0x800 || js.numInstructions >= JitBlockCache::MAX_BLOCK_INSTRUCTIONS)
		{
			FlushAll();
			WriteExit(js.compilerPC, js.nextExit++);
			js.compiling = false;
		}
	}

	b->codeSize = GetCodePtr() - b->normalEntry;

	// Don't forget to zap the newly written instructions in the instruction cache!
	FlushIcache();

	if (js.lastContinuedPC == 0)
		b->originalSize = js.numInstructions;
	else
	{
		// We continued at least once.  Add the last proxy and set the originalSize correctly.
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (js.compilerPC - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
		b->originalSize = js.initialBlockSize;
	}

	return b->normalEntry;
}

void MipsJit::AddContinuedBlock(u32 dest)
{
	// The first block is the root block.  When we continue, we create proxy blocks after that.
	if (js.lastContinuedPC == 0)
		js.initialBlockSize = js.numInstructions;
	else
		blocks.ProxyBlock(js.blockStart, js.lastContinuedPC, (js.compilerPC - js.lastContinuedPC) / sizeof(u32), GetCodePtr());
	js.lastContinuedPC = dest;
}

bool MipsJit::DescribeCodePtr(const u8 *ptr, std::string &name)
{
	// TODO: Not used by anything yet.
	return false;
}

void MipsJit::Comp_RunBlock(MIPSOpcode op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(Log::JIT, "Comp_RunBlock should never be reached!");
}

void MipsJit::LinkBlock(u8 *exitPoint, const u8 *checkedEntry) {
	// TODO
}

void MipsJit::UnlinkBlock(u8 *checkedEntry, u32 originalAddress) {
	// TODO
}

bool MipsJit::ReplaceJalTo(u32 dest) {
	const ReplacementTableEntry *entry = nullptr;
	u32 funcSize = 0;
	if (!CanReplaceJalTo(dest, &entry, &funcSize)) {
		return false;
	}
	return false;
}

void MipsJit::Comp_ReplacementFunc(MIPSOpcode op)
{
}

void MipsJit::Comp_Generic(MIPSOpcode op)
{
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		//SaveDowncount();
		RestoreRoundingMode();
		// Move Imm32(js.compilerPC) in to M(&mips_->pc)
		QuickCallFunction(V1, (void *)func);
		ApplyRoundingMode();
		//RestoreDowncount();
	}

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0)
	{
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();
	}
}

void MipsJit::MovFromPC(MIPSReg r) {
}

void MipsJit::MovToPC(MIPSReg r) {
}

void MipsJit::SaveDowncount() {
}

void MipsJit::RestoreDowncount() {
}

void MipsJit::WriteDownCount(int offset) {
}

void MipsJit::WriteDownCountR(MIPSReg reg) {
}

void MipsJit::RestoreRoundingMode(bool force) {
}

void MipsJit::ApplyRoundingMode(bool force) {
}

void MipsJit::UpdateRoundingMode() {
}

void MipsJit::WriteExit(u32 destination, int exit_num)
{
	//WriteDownCount();
	JitBlock *b = js.curBlock;
	b->exitAddress[exit_num] = destination;
	b->exitPtrs[exit_num] = GetWritableCodePtr();

	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (block >= 0 && jo.enableBlocklink) {
		// It exists! Joy of joy!
		B(blocks.GetBlock(block)->checkedEntry);
		b->linkStatus[exit_num] = true;
	} else {
		//gpr.SetRegImm(V0, destination);
		//B((const void *)dispatcherPCInV0);
	}
}

void MipsJit::WriteExitDestInR(MIPSReg Reg)
{
	MovToPC(Reg);
	//WriteDownCount();
	// TODO: shouldn't need an indirect branch here...
	B((const void *)dispatcher);
}

void MipsJit::WriteSyscallExit()
{
	//WriteDownCount();
	B((const void *)dispatcherCheckCoreState);
}

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6) & 0x1F)
#define _POS ((op>>6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)

//memory regions:
//
// 08-0A
// 48-4A
// 04-05
// 44-45
// mov eax, addrreg
	// shr eax, 28
// mov eax, [table+eax]
// mov dreg, [eax+offreg]
	
}

#endif // PPSSPP_ARCH(MIPS)

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

#include "base/logging.h"
#include "Common/ChunkFile.h"
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

#include "FakeJit.h"
#include "CPUDetect.h"

void DisassembleFake(const u8 *data, int size) {
}

namespace MIPSComp
{

FakeJitOptions::FakeJitOptions() {
	enableBlocklink = true;
	downcountInRegister = true;
	useBackJump = false;
	useForwardJump = false;
	cachePointers = true;
	immBranches = false;
	continueBranches = false;
	continueJumps = false;
	continueMaxInstructions = 300;
}

Jit::Jit(MIPSState *mips) : blocks(mips, this), mips_(mips)
{ 
	logBlocks = 0;
	dontLogBlocks = 0;
	blocks.Init();
}

void Jit::DoState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	p.Do(js.startDefaultPrefix);
	if (s >= 2) {
		p.Do(js.hasSetRounding);
		js.lastSetRounding = 0;
	} else {
		js.hasSetRounding = 1;
	}
}

// This is here so the savestate matches between jit and non-jit.
void Jit::DoDummyState(PointerWrap &p)
{
	auto s = p.Section("Jit", 1, 2);
	if (!s)
		return;

	bool dummy = false;
	p.Do(dummy);
	if (s >= 2) {
		dummy = true;
		p.Do(dummy);
	}
}

void Jit::FlushAll()
{
	FlushPrefixV();
}

void Jit::FlushPrefixV()
{
}

void Jit::ClearCache()
{
	blocks.Clear();
	ClearCodeSpace();
}

void Jit::InvalidateCache()
{
	blocks.Clear();
}

void Jit::InvalidateCacheAt(u32 em_address, int length)
{
	blocks.InvalidateICache(em_address, length);
}

void Jit::EatInstruction(MIPSOpcode op) {
}

void Jit::CompileDelaySlot(int flags)
{
}


void Jit::Compile(u32 em_address) {
}

void Jit::RunLoopUntil(u64 globalticks)
{
	((void (*)())enterCode)();
}

const u8 *Jit::DoJit(u32 em_address, JitBlock *b)
{
	return b->normalEntry;
}

void Jit::AddContinuedBlock(u32 dest)
{
}

bool Jit::DescribeCodePtr(const u8 *ptr, std::string &name)
{
	// TODO: Not used by anything yet.
	return false;
}

void Jit::Comp_RunBlock(MIPSOpcode op)
{
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(JIT, "Comp_RunBlock should never be reached!");
}

bool Jit::ReplaceJalTo(u32 dest) {
	return true;
}

void Jit::Comp_ReplacementFunc(MIPSOpcode op)
{
}

void Jit::Comp_Generic(MIPSOpcode op)
{
	FlushAll();
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		SaveDowncount();
		RestoreDowncount();
	}

	const MIPSInfo info = MIPSGetInfo(op);
	if ((info & IS_VFPU) != 0 && (info & VFPU_NO_PREFIX) == 0)
	{
		// If it does eat them, it'll happen in MIPSCompileOp().
		if ((info & OUT_EAT_PREFIX) == 0)
			js.PrefixUnknown();
	}
}

void Jit::MovFromPC(FakeReg r) {
}

void Jit::MovToPC(FakeReg r) {
}

void Jit::SaveDowncount() {
}

void Jit::RestoreDowncount() {
}

void Jit::WriteDownCount(int offset) {
}

// Abuses R2
void Jit::WriteDownCountR(FakeReg reg) {
}

void Jit::RestoreRoundingMode(bool force) {
}

void Jit::ApplyRoundingMode(bool force) {
}

void Jit::UpdateRoundingMode() {
}

void Jit::WriteExit(u32 destination, int exit_num)
{
}

void Jit::WriteExitDestInR(FakeReg Reg) 
{
}

void Jit::WriteSyscallExit()
{
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

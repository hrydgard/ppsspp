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

#include "ppsspp_config.h"
#include "MemoryUtil.h"
#include "ABI.h"
#include "Thunk.h"

#define THUNK_ARENA_SIZE 1024*1024*1

namespace {

#if !PPSSPP_ARCH(AMD64)
alignas(32) static u8 saved_fp_state[16 * 4 * 4];
alignas(32) static u8 saved_gpr_state[16 * 8];
static u16 saved_mxcsr;
#endif

}  // namespace

using namespace Gen;

void ThunkManager::Init()
{
#if PPSSPP_ARCH(AMD64)
	// Account for the return address and "home space" on Windows (which needs to be at the bottom.)
	const int stackOffset = ThunkStackOffset();
	int stackPosition;
#endif

	AllocCodeSpace(THUNK_ARENA_SIZE);
	BeginWrite(512);
	save_regs = GetCodePtr();
#if PPSSPP_ARCH(AMD64)
	for (int i = 2; i < ABI_GetNumXMMRegs(); i++)
		MOVAPS(MDisp(RSP, stackOffset + (i - 2) * 16), (X64Reg)(XMM0 + i));
	stackPosition = (ABI_GetNumXMMRegs() - 2) * 2;
	STMXCSR(MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(RCX));
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(RDX));
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(R8) );
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(R9) );
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(R10));
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(R11));
#ifndef _WIN32
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(RSI));
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(RDI));
#endif
	MOV(64, MDisp(RSP, stackOffset + (stackPosition++ * 8)), R(RBX));
#else
	for (int i = 2; i < ABI_GetNumXMMRegs(); i++)
		MOVAPS(M(saved_fp_state + i * 16), (X64Reg)(XMM0 + i));
	STMXCSR(M(&saved_mxcsr));
	MOV(32, M(saved_gpr_state + 0 ), R(RCX));
	MOV(32, M(saved_gpr_state + 4 ), R(RDX));
#endif
	RET();

	load_regs = GetCodePtr();
#if PPSSPP_ARCH(AMD64)
	for (int i = 2; i < ABI_GetNumXMMRegs(); i++)
		MOVAPS((X64Reg)(XMM0 + i), MDisp(RSP, stackOffset + (i - 2) * 16));
	stackPosition = (ABI_GetNumXMMRegs() - 2) * 2;
	LDMXCSR(MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, R(RCX), MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, R(RDX), MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, R(R8) , MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, R(R9) , MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, R(R10), MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, R(R11), MDisp(RSP, stackOffset + (stackPosition++ * 8)));
#ifndef _WIN32
	MOV(64, R(RSI), MDisp(RSP, stackOffset + (stackPosition++ * 8)));
	MOV(64, R(RDI), MDisp(RSP, stackOffset + (stackPosition++ * 8)));
#endif
	MOV(64, R(RBX), MDisp(RSP, stackOffset + (stackPosition++ * 8)));
#else
	LDMXCSR(M(&saved_mxcsr));
	for (int i = 2; i < ABI_GetNumXMMRegs(); i++)
		MOVAPS((X64Reg)(XMM0 + i), M(saved_fp_state + i * 16));
	MOV(32, R(RCX), M(saved_gpr_state + 0 ));
	MOV(32, R(RDX), M(saved_gpr_state + 4 ));
#endif
	RET();
	EndWrite();
}

void ThunkManager::Reset()
{
	thunks.clear();
	ResetCodePtr(0);
}

void ThunkManager::Shutdown()
{
	Reset();
	FreeCodeSpace();
}

int ThunkManager::ThunkBytesNeeded()
{
	int space = (ABI_GetNumXMMRegs() - 2) * 16;
#if PPSSPP_ARCH(AMD64)
	// MXCSR
	space += 8;
	space += 7 * 8;
#ifndef _WIN32
	space += 2 * 8;
#endif
#else
	// MXCSR
	space += 4;
	space += 2 * 4;
#endif

	// Round up to the nearest 16 just in case.
	return (space + 15) & ~15;
}

int ThunkManager::ThunkStackOffset()
{
#if PPSSPP_ARCH(AMD64)
#ifdef _WIN32
	return 0x28;
#else
	return 0x8;
#endif
#else
	return 0;
#endif
}

const void *ThunkManager::ProtectFunction(const void *function, int num_params) {
	std::map<const void *, const u8 *>::iterator iter;
	iter = thunks.find(function);
	if (iter != thunks.end())
		return (const void *)iter->second;

	_assert_msg_(region != nullptr, "Can't protect functions before the emu is started.");

	BeginWrite(128);
	const u8 *call_point = GetCodePtr();
	Enter(this, true);

#if PPSSPP_ARCH(AMD64)
	ABI_CallFunction(function);
#else
	// Since parameters are in the previous stack frame, not in registers, this takes some
	// trickery : we simply re-push the parameters. might not be optimal, but that doesn't really
	// matter.
	ABI_AlignStack(num_params * 4);
	unsigned int alignedSize = ABI_GetAlignedFrameSize(num_params * 4);
	for (int i = 0; i < num_params; i++) {
		// ESP is changing, so we do not need i
		PUSH(32, MDisp(ESP, alignedSize - 4));
	}
	CALL(function);
	ABI_RestoreStack(num_params * 4);
#endif

	Leave(this, true);
	RET();
	EndWrite();

	thunks[function] = call_point;
	return (const void *)call_point;
}

void ThunkManager::Enter(ThunkEmitter *emit, bool withinCall)
{
#if PPSSPP_ARCH(AMD64)
	// Make sure to align stack.
	emit->SUB(64, R(ESP), Imm32(ThunkStackOffset() + ThunkBytesNeeded() + (withinCall ? 0 : 8)));
	emit->ABI_CallFunction(save_regs);
#else
	emit->CALL((const void *)save_regs);
#endif
}

void ThunkManager::Leave(ThunkEmitter *emit, bool withinCall)
{
#if PPSSPP_ARCH(AMD64)
	emit->ABI_CallFunction(load_regs);
	emit->ADD(64, R(ESP), Imm32(ThunkStackOffset() + ThunkBytesNeeded() + (withinCall ? 0 : 8)));
#else
	emit->CALL((void*)load_regs);
#endif
}

// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "ABI.h"
#include "x64Emitter.h"

#include "../../MemMap.h"

#include "Core/Core.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/CoreTiming.h"
#include "Common/MemoryUtil.h"

#include "Jit.h"
#include "../JitCommon/JitCommon.h"
#include "../../Core.h"
#include "Asm.h"

using namespace Gen;

//TODO - make an option
//#if _DEBUG
static bool enableDebug = false; 
//#else
//		bool enableDebug = false; 
//#endif

//static bool enableStatistics = false; //unused?

//GLOBAL STATIC ALLOCATIONS x86
//EAX - ubiquitous scratch register - EVERYBODY scratches this

//GLOBAL STATIC ALLOCATIONS x64
//EAX - ubiquitous scratch register - EVERYBODY scratches this
//RBX - Base pointer of memory
//R15 - Pointer to array of block pointers 

extern volatile CoreState coreState;

void Jit()
{
	MIPSComp::jit->Compile(currentMIPS->pc);
}

// IDEA, NOT IMPLEMENTED: no more block numbers - hack opcodes just contain offset within
// dynarec buffer, gets rid of lookup into block buffer
// At this offset - 4, there is an int specifying the block number if needed.

void ImHere()
{
	DEBUG_LOG(CPU, "JIT Here: %08x", currentMIPS->pc);
}

void AsmRoutineManager::Generate(MIPSState *mips, MIPSComp::Jit *jit)
{
	enterCode = AlignCode16();
	ABI_PushAllCalleeSavedRegsAndAdjustStack();
#ifdef _M_X64
	// Two statically allocated registers.
	MOV(64, R(RBX), Imm64((u64)Memory::base));
	MOV(64, R(R15), Imm64((u64)jit->GetBasePtr())); //It's below 2GB so 32 bits are good enough
#endif

	outerLoop = GetCodePtr();
		ABI_CallFunction(reinterpret_cast<void *>(&CoreTiming::Advance));
		FixupBranch skipToRealDispatch = J(); //skip the sync and compare first time

		dispatcherCheckCoreState = GetCodePtr();

		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		FixupBranch bailCoreState = J_CC(CC_S, true);

		CMP(32, M((void*)&coreState), Imm32(0));
		FixupBranch badCoreState = J_CC(CC_NZ, true);
		FixupBranch skipToRealDispatch2 = J(); //skip the sync and compare first time

		dispatcher = GetCodePtr();

			// The result of slice decrementation should be in flags if somebody jumped here
			// IMPORTANT - We jump on negative, not carry!!!
			FixupBranch bail = J_CC(CC_S, true);

			SetJumpTarget(skipToRealDispatch);
			SetJumpTarget(skipToRealDispatch2);

			dispatcherNoCheck = GetCodePtr();

			MOV(32, R(EAX), M(&mips->pc));
#ifdef _M_IX86
			AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
			_assert_msg_(CPU, Memory::base != 0, "Memory base bogus");
			MOV(32, R(EAX), MDisp(EAX, (u32)Memory::base));
#elif _M_X64
			MOV(32, R(EAX), MComplex(RBX, RAX, SCALE_1, 0));
#endif
			MOV(32, R(EDX), R(EAX));
			AND(32, R(EDX), Imm32(MIPS_EMUHACK_MASK));
			CMP(32, R(EDX), Imm32(MIPS_EMUHACK_OPCODE));
			FixupBranch notfound = J_CC(CC_NZ);
				// IDEA - we have 24 bits, why not just use offsets from base of code?
				if (enableDebug)
				{
					ADD(32, M(&mips->debugCount), Imm8(1));
				}
				//grab from list and jump to it
				AND(32, R(EAX), Imm32(MIPS_EMUHACK_VALUE_MASK));
#ifdef _M_IX86
				ADD(32, R(EAX), ImmPtr(jit->GetBasePtr()));
#elif _M_X64
				ADD(64, R(RAX), R(R15));
#endif
				JMPptr(R(EAX));
			SetJumpTarget(notfound);

			//Ok, no block, let's jit
			ABI_CallFunction((void *)&Jit);
			JMP(dispatcherNoCheck); // Let's just dispatch again, we'll enter the block since we know it's there.

		SetJumpTarget(bail);
		SetJumpTarget(bailCoreState);

		CMP(32, M((void*)&coreState), Imm32(0));
		J_CC(CC_Z, outerLoop, true);

	SetJumpTarget(badCoreState);
	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	RET();

	breakpointBailout = GetCodePtr();
	ABI_PopAllCalleeSavedRegsAndAdjustStack();
	RET();
}
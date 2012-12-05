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

#include "ABI.h"
#include <ArmEmitter.h>

#include "../../MemMap.h"

#include "../MIPS.h"
#include "../../CoreTiming.h"
#include "MemoryUtil.h"

#include "ABI.h"
#include "Jit.h"
#include "../JitCommon/JitCommon.h"
#include "../../Core.h"
#include "Asm.h"

using namespace ArmGen;

//static int temp32; // unused?

//TODO - make an option
//#if _DEBUG
static bool enableDebug = false; 
//#else
//		bool enableDebug = false; 
//#endif

//static bool enableStatistics = false; //unused?

//The standard ARM calling convention allocates the 16 ARM registers as:

// r15 is the program counter.
// r14 is the link register. (The BL instruction, used in a subroutine call, stores the return address in this register).
// r13 is the stack pointer. (The Push/Pop instructions in "Thumb" operating mode use this register only).
// r12 is the Intra-Procedure-call scratch register.
// r4 to r11: used to hold local variables.
// r0 to r3: used to hold argument values passed to a subroutine, and also hold results returned from a subroutine.

//	 R2, R3, R4, R5, R6, R7, R8, R10, R11	 // omitting R9?


// STATIC ALLOCATION ARM:
// R11 : Memory base pointer.

extern volatile CoreState coreState;

void Jit()
{
	MIPSComp::jit->Compile(currentMIPS->pc);
}

// PLAN: no more block numbers - crazy opcodes just contain offset within
// dynarec buffer
// At this offset - 4, there is an int specifying the block number.

void AsmRoutineManager::Generate(MIPSState *mips, MIPSComp::Jit *jit)
{
	enterCode = AlignCode16();
	//ARMABI_PushAllCalleeSavedRegsAndAdjustStack();
/*
#ifndef _M_IX86
	// Two statically allocated registers.
	MOV(64, R(RBX), Imm64((u64)Memory::base));
	MOV(64, R(R15), Imm64((u64)jit->GetBlockCache()->GetCodePointers())); //It's below 2GB so 32 bits are good enough
#endif
	*/

	PUSH(8, R5, R6, R7, R8, R9, R10, R11, _LR);
	SetCC(CC_AL);
	//ARMABI_MOVIMM32(R11, (u32)Memory::base);
	//ARMABI_MOVIMM32(R10, (u32)jit->GetBlockCache()->GetCodePointers());

	outerLoop = GetCodePtr();
		//ARMABI_CallFunction(reinterpret_cast<void *>(&CoreTiming::Advance));
		FixupBranch skipToRealDispatch = B(); //skip the sync and compare first time
	 
		dispatcher = GetCodePtr();
			// The result of slice decrementation should be in flags if somebody jumped here
			// IMPORTANT - We jump on negative, not carry!!!
			SetCC(CC_LT);
			FixupBranch bail = B();
			
			SetCC(CC_AL);
			SetJumpTarget(skipToRealDispatch);
			//INT3();

			dispatcherNoCheck = GetCodePtr();
			// LDR(R0, R11, R0, M(&mips->pc));
			dispatcherPcInR0 = GetCodePtr();

			_assert_msg_(CPU, Memory::base != 0, "Memory base bogus");
			//ARMABI_MOVIMM32(R1, Memory::MEMVIEW32_MASK + 1);
			AND(R0, R0, R1);
			LDR(R0, R11, R0);
			AND(R1, R0, Operand2(0xFC, 24));
			BIC(R0, R0, Operand2(0xFC, 24));
			CMP(R1, Operand2(MIPS_EMUHACK_OPCODE >> 24, 24));
			FixupBranch notfound = B_CC(CC_NEQ);
				// IDEA - we have 24 bits, why not just use offsets from base of code?
				if (enableDebug)
				{
					//ADD(32, M(&mips->debugCount), Imm8(1));
				}
				// grab from list and jump to it
				ADD(R0, R10, Operand2(2, ST_LSL, R0));
				LDR(R0, R0);
				B(R0);
			SetJumpTarget(notfound);

			ARMABI_CallFunction((void *)&Jit);
			B(dispatcherNoCheck); // no point in special casing this

		SetJumpTarget(bail);
		doTiming = GetCodePtr();

		testExternalExceptions = GetCodePtr();
		//TEST(32, M((void *)&mips->exceptions), Imm32(MIPSState::BREAKING_EXCEPTIONS));
		//FixupBranch noExtException = J_CC(CC_Z);
		//MOV(32, R(EAX), M(&mips->pc));
		// MOV(32, M(&NPC), R(EAX));
		// ABI_CallFunction(reinterpret_cast<void *>(&MIPSState::CheckExternalExceptions));
		//SetJumpTarget(noExtException);

		//CMP(M((void*)&coreState), Imm8(0));
		SetCC(CC_EQ);
		//B(outerLoop);
		SetCC(CC_AL);

	//Landing pad for drec space
	//ARMABI_PopAllCalleeSavedRegsAndAdjustStack();
	B(_LR); 

	PUSH(8, R5, R6, R7, R8, R9, R10, R11, _PC);  // Returns



	GenerateCommon();
}

void AsmRoutineManager::GenerateCommon()
{
	/*
	fifoDirectWrite8 = AlignCode4();
	GenFifoWrite(8);
	fifoDirectWrite16 = AlignCode4();
	GenFifoWrite(16);
	fifoDirectWrite32 = AlignCode4();
	GenFifoWrite(32);
	fifoDirectWriteFloat = AlignCode4();
	GenFifoFloatWrite();
	fifoDirectWriteXmm64 = AlignCode4(); 
	GenFifoXmm64Write();

	GenQuantizedLoads();
	GenQuantizedStores();
	GenQuantizedSingleStores();
	*/

	//CMPSD(R(XMM0), M(&zero), 
	// TODO

	// Fast write routines - special case the most common hardware write
	// TODO: use this.
	// Even in x86, the param values will be in the right registers.
	/*
	const u8 *fastMemWrite8 = AlignCode16();
	CMP(32, R(ABI_PARAM2), Imm32(0xCC008000));
	FixupBranch skip_fast_write = J_CC(CC_NE, false);
	MOV(32, EAX, M(&m_gatherPipeCount));
	MOV(8, MDisp(EAX, (u32)&m_gatherPipe), ABI_PARAM1);
	ADD(32, 1, M(&m_gatherPipeCount));
	RET();
	SetJumpTarget(skip_fast_write);
	CALL((void *)&Memory::Write_U8);*/
}

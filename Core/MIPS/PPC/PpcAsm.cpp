#include "Common/ChunkFile.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"

#include "PpcRegCache.h"
#include "ppcEmitter.h"
#include "PpcJit.h"

#include <ppcintrinsics.h>

using namespace PpcGen;

extern volatile CoreState coreState;

static void JitAt()
{
	MIPSComp::jit->Compile(currentMIPS->pc);
}

namespace MIPSComp
{
static int dontLogBlocks = 20;
static int logBlocks = 40;

const u8 *Jit::DoJit(u32 em_address, JitBlock *b)
{
	js.cancel = false;
	js.blockStart = js.compilerPC = mips_->pc;
	js.downcountAmount = 0;
	js.curBlock = b;
	js.compiling = true;
	js.inDelaySlot = false;
	js.PrefixStart();

	// We add a check before the block, used when entering from a linked block.
	b->checkedEntry = GetCodePtr();
	// Downcount flag check. The last block decremented downcounter, and the flag should still be available.
	
	MOVI2R(SREG, js.blockStart);

	// if (currentMIPS->downcount<0)
	CMPI(DCNTREG, 0);
	BLT((const void *)outerLoopPCInR0);

	b->normalEntry = GetCodePtr();
	// TODO: this needs work
	MIPSAnalyst::AnalysisResults analysis; // = MIPSAnalyst::Analyze(em_address);

	gpr.Start(analysis);
	fpr.Start(analysis);

	int numInstructions = 0;
	int cycles = 0;
	int partialFlushOffset = 0;
	if (logBlocks > 0) logBlocks--;
	if (dontLogBlocks > 0) dontLogBlocks--;

// #define LOGASM
#ifdef LOGASM
	char temp[256];
#endif
	while (js.compiling)
	{
		gpr.SetCompilerPC(js.compilerPC);  // Let it know for log messages
		fpr.SetCompilerPC(js.compilerPC);
		MIPSOpcode inst = Memory::Read_Instruction(js.compilerPC);
		js.downcountAmount += MIPSGetInstructionCycleEstimate(inst);

		MIPSCompileOp(inst);
	
		js.compilerPC += 4;
		numInstructions++;
	}
	//FlushLitPool();
#ifdef LOGASM
	if (logBlocks > 0 && dontLogBlocks == 0) {
		for (u32 cpc = em_address; cpc != js.compilerPC + 4; cpc += 4) {
			MIPSDisAsm(Memory::Read_Instruction(cpc), cpc, temp, true);
			INFO_LOG(JIT, "M: %08x   %s", cpc, temp);
		}
	}
#endif

	b->codeSize = GetCodePtr() - b->normalEntry;

#ifdef LOGASM
	if (logBlocks > 0 && dontLogBlocks == 0) {
		INFO_LOG(JIT, "=============== ARM ===============");
		DisassembleArm(b->normalEntry, GetCodePtr() - b->normalEntry);
	}
#endif	
	//DumpJit();

	AlignCode16();

	// Don't forget to zap the instruction cache!
	FlushIcache();

	b->originalSize = numInstructions;
	return b->normalEntry;
}

void Jit::DumpJit() {
#ifdef _XBOX
	u32 len = (u32)GetCodePtr() - (u32)GetBasePtr();
	FILE * fd;
	fd = fopen("game:\\jit.bin", "wb");
	fwrite(GetBasePtr(), len, 1, fd);
	fclose(fd);
#endif
}

void Jit::GenerateFixedCode() {
	enterCode = AlignCode16();
	
	INFO_LOG(JIT, "Base: %08x", (u32)Memory::base);
	INFO_LOG(JIT, "enterCode: 0x%08p", enterCode);	
	INFO_LOG(JIT, "GetBasePtr: 0x%08p", GetBasePtr());

	Prologue();
	
	// Map fixed register
	MOVI2R(BASEREG,	(u32)Memory::base);
	MOVI2R(CTXREG,	(u32)mips_);
	MOVI2R(CODEREG, (u32)GetBasePtr());

	// Update downcount reg value from memory
	RestoreDowncount(DCNTREG);

	// SREG = mips->pc
	MovFromPC(SREG);

	// Keep current location, TODO rename it, outerLoopPCInR0 to outerLoopPCInR3 ?? 
	outerLoopPCInR0 = GetCodePtr();
	
	// mips->pc = SREG
	MovToPC(SREG);

	// Keep current location
	outerLoop = GetCodePtr();

	// Jit loop
	// {
		// Save downcount reg value to memory
		SaveDowncount(DCNTREG);
		// Call CoreTiming::Advance() => update donwcount
		QuickCallFunction((void *)&CoreTiming::Advance);
		// Update downcount reg value from memory
		RestoreDowncount(DCNTREG);

		// branch to skipToRealDispatch
		FixupBranch skipToRealDispatch = B(); //skip the sync and compare first time

		// Keep current location dispatcherCheckCoreState:
		dispatcherCheckCoreState = GetCodePtr();

		// The result of slice decrementation should be in flags if somebody jumped here
		// IMPORTANT - We jump on negative, not carry!!!
		// branch to bailCoreState: (jump if(what ??) negative )
		FixupBranch bailCoreState = BLT(); // BLT ???

		// SREG = coreState
		MOVI2R(SREG, (u32)&coreState);
		// Compare coreState and CORE_RUNNING
		LWZ(SREG, SREG); // SREG = *SREG
		CMPI(SREG, 0); // compare 0(CORE_RUNNING) and CR0

		// branch to badCoreState: (jump if coreState != CORE_RUNNING)
		FixupBranch badCoreState = BNE(); 

		// branch to skipToRealDispatch2:
		FixupBranch skipToRealDispatch2 = B(); //skip the sync and compare first time

		// Keep current location, TODO rename it, outerLoopPCInR0 to outerLoopPCInSREG ?? 
		dispatcherPCInR0 = GetCodePtr();

		// mips->pc = SREG
		MovToPC(SREG);

		// At this point : flags = EQ. Fine for the next check, no need to jump over it.
		// label dispatcher:
		dispatcher = GetCodePtr(); 

		// {
			// The result of slice decrementation should be in flags if somebody jumped here
			// IMPORTANT - We jump on negative, not carry!!!
			// label bail:
			// arm B_CC(CC_MI);
			FixupBranch bail = BLT();

			// label skipToRealDispatch:
			SetJumpTarget(skipToRealDispatch);

			// label skipToRealDispatch2:
			SetJumpTarget(skipToRealDispatch2);

			// Keep current location
			dispatcherNoCheck = GetCodePtr();
			
			// read op
			// R3 = mips->pc & Memory::MEMVIEW32_MASK
			LWZ(R3, CTXREG, offsetof(MIPSState, pc));
			// & Memory::MEMVIEW32_MASK
			RLWINM(R3, R3, 0, 2, 31);

			// R3 = memory::base[r3];
			ADD(R3, BASEREG, R3);
			MOVI2R(R0, 0);
			LWBRX(R3, R3, R0);

			// R4 = R3 & MIPS_EMUHACK_VALUE_MASK
			RLWINM(R4, R3, 0, 6, 31);

			// R3 = R3 & MIPS_EMUHACK_MASK
			RLWINM(R3, R3, 0, 0, 6);
			
			// compare, op == MIPS_EMUHACK_OPCODE 
			MOVI2R(SREG, MIPS_EMUHACK_OPCODE);
			CMPL(R3, SREG);

			// Branch if func block not found 
			FixupBranch notfound = BNE();

			// {
				// R3 = R4 +  GetBasePtr()
				ADD(R3, R4, CODEREG);
				
				MTCTR(R3);
				BCTR();
			// }

			// label notfound:
			SetJumpTarget(notfound);

			//Ok, no block, let's jit
			// Save downcount reg value to memory
			SaveDowncount(DCNTREG);

			// Exec JitAt => Compile block !
			QuickCallFunction((void *)&JitAt);
						
			// Update downcount reg value from memory
			RestoreDowncount(DCNTREG);
			
			// branch to dispatcherNoCheck:
			B(dispatcherNoCheck); // no point in special casing this
		// }

		// label bail:
		SetJumpTarget(bail);

		// label bailCoreState:
		SetJumpTarget(bailCoreState);

		// Compare coreState and CORE_RUNNING
		MOVI2R(SREG, (u32)&coreState);
		LWZ(SREG, SREG); // SREG = *SREG => SREG = coreState
		CMPLI(SREG, 0); // compare 0(CORE_RUNNING) and corestate

		BEQ(outerLoop);
	// }

	// badCoreState label:
	SetJumpTarget(badCoreState);

	// Keep current location
	breakpointBailout = GetCodePtr();

	// mips->downcount = DCNTREG
	SaveDowncount(DCNTREG);

	Epilogue();

	// Go back to caller
	BLR();

	// Don't forget to zap the instruction cache!
	FlushIcache();
}

}
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

namespace MIPSComp
{

static u32 delaySlotFlagsValue;

void Jit::CompileDelaySlot(int flags)
{
	// preserve flag around the delay slot! Maybe this is not always necessary on ARM where 
	// we can (mostly) control whether we set the flag or not. Of course, if someone puts an slt in to the
	// delay slot, we're screwed.
	if (flags & DELAYSLOT_SAFE) {
		// Save flags register
		MOVI2R(SREG, (u32)&delaySlotFlagsValue);
		STW(FLAGREG, SREG);
	}

	js.inDelaySlot = true;
	u32 op = Memory::Read_Instruction(js.compilerPC + 4);
	MIPSCompileOp(op);
	js.inDelaySlot = false;

	if (flags & DELAYSLOT_FLUSH)
		FlushAll();

	if (flags & DELAYSLOT_SAFE) {
		// Restore flags register
		MOVI2R(SREG, (u32)&delaySlotFlagsValue);
		LWZ(FLAGREG, SREG);
	}
}

void Jit::Compile(u32 em_address)
{
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull())
	{
		ClearCache();
	}

	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	DoJit(em_address, b);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink);

	// Drat.  The VFPU hit an uneaten prefix at the end of a block.
	if (js.startDefaultPrefix && js.MayHavePrefix())
	{
		js.startDefaultPrefix = false;
		// Our assumptions are all wrong so it's clean-slate time.
		ClearCache();

		// Let's try that one more time.  We won't get back here because we toggled the value.
		Compile(em_address);
	}
}

void Jit::MovFromPC(PPCReg r) {
	LWZ(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::MovToPC(PPCReg r) {
	STW(r, CTXREG, offsetof(MIPSState, pc));
}

void Jit::SaveDowncount(PPCReg r) {
	STW(r, CTXREG, offsetof(MIPSState, downcount));
}

void Jit::RestoreDowncount(PPCReg r) {
	LWZ(r, CTXREG, offsetof(MIPSState, downcount));
}

static void ShowDownCount() {
	if (currentMIPS->downcount<0) {
		//ERROR_LOG(DYNA_REC, "MIPSState, downcount %08x", currentMIPS->downcount);
		Crash();
	}
}

void Jit::WriteDownCount(int offset)
{
	// don't know if the result is correct
	int theDowncount = js.downcountAmount + offset;
	if (jo.downcountInRegister) {		
		// DCNTREG = DCNTREG - theDowncount;
		MOVI2R(SREG, theDowncount);	
		SUBF(DCNTREG, SREG, DCNTREG, 1);
		STW(DCNTREG, CTXREG, offsetof(MIPSState, downcount));
	} else {
		// DCNTREG = MIPSState->downcount - theDowncount;
		MOVI2R(SREG, theDowncount);	
		LWZ(DCNTREG, CTXREG, offsetof(MIPSState, downcount));
		SUBF(DCNTREG, SREG, DCNTREG, 1);
		STW(DCNTREG, CTXREG, offsetof(MIPSState, downcount));
	}

	//QuickCallFunction(ShowDownCount);

	CMPI(DCNTREG, 0);
}

void Jit::Comp_Generic(u32 op) {
	FlushAll();

	// basic jit !!
	MIPSInterpretFunc func = MIPSGetInterpretFunc(op);
	if (func)
	{
		// Save mips PC and cycles
		SaveDowncount(DCNTREG);

		// call interpreted function
		MOVI2R(R3, op);
		QuickCallFunction((void *)func);

		// restore pc and cycles
		RestoreDowncount(DCNTREG);
	}
	// Might have eaten prefixes, hard to tell...
	if ((MIPSGetInfo(op) & IS_VFPU) != 0)
		js.PrefixStart();
}
	
void Jit::EatInstruction(u32 op) {
	u32 info = MIPSGetInfo(op);
	_dbg_assert_msg_(JIT, !(info & DELAYSLOT), "Never eat a branch op.");
	_dbg_assert_msg_(JIT, !js.inDelaySlot, "Never eat an instruction inside a delayslot.");

	js.compilerPC += 4;
	js.downcountAmount += MIPSGetInstructionCycleEstimate(op);
}

void Jit::Comp_RunBlock(u32 op) {
	// This shouldn't be necessary, the dispatcher should catch us before we get here.
	ERROR_LOG(DYNA_REC, "Comp_RunBlock should never be reached!");
}

void Jit::Comp_DoNothing(u32 op) {

}

void Jit::FlushAll()
{
	gpr.FlushAll();
	//fpr.FlushAll();
	//FlushPrefixV();
}

void Jit::ClearCache() {
	blocks.Clear();
	ClearCodeSpace();
	GenerateFixedCode();
}

void Jit::ClearCacheAt(u32 em_address) {
	ClearCache();
}

Jit::Jit(MIPSState *mips) : blocks(mips, this), gpr(mips, &jo),mips_(mips)
{ 
	blocks.Init();
	gpr.SetEmitter(this);
	AllocCodeSpace(1024 * 1024 * 16);  // 32MB is the absolute max because that's what an ARM branch instruction can reach, backwards and forwards.
	GenerateFixedCode();

	js.startDefaultPrefix = true;
}

void Jit::RunLoopUntil(u64 globalticks) {	
#ifdef _XBOX
	// force stack alinement
	_alloca(8*1024);
#endif
	
	// Run the compiled code
	((void (*)())enterCode)();
}


// IDEA - could have a WriteDualExit that takes two destinations and two condition flags,
// and just have conditional that set PC "twice". This only works when we fall back to dispatcher
// though, as we need to have the SUBS flag set in the end. So with block linking in the mix,
// I don't think this gives us that much benefit.
void Jit::WriteExit(u32 destination, int exit_num)
{
	WriteDownCount(); 
	//If nobody has taken care of this yet (this can be removed when all branches are done)
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
		MOVI2R(SREG, destination);
		B((const void *)dispatcherPCInR0);	
	}
}

void Jit::WriteExitDestInR(PPCReg Reg) 
{
	//Break();
	MovToPC(Reg);
	WriteDownCount();
	// TODO: shouldn't need an indirect branch here...
	B((const void *)dispatcher);
}

void Jit::WriteSyscallExit()
{
	WriteDownCount();
	B((const void *)dispatcherCheckCoreState);
}
}
#include <algorithm>
#include <cmath>

#include "ppsspp_config.h"

#include "Common/BitSet.h"
#include "Common/BitScan.h"
#include "Common/Common.h"
#include "Common/CommonFuncs.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/math_util.h"
#include "Common/Math/SIMDHeaders.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/System.h"
#include "Core/MIPS/MIPSTracer.h"

#ifdef mips
// Why do MIPS compilers define something so generic?  Try to keep defined, at least...
#undef mips
#define mips mips
#endif

alignas(16) static const float vec4InitValues[8][4] = {
	{ 0.0f, 0.0f, 0.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ -1.0f, -1.0f, -1.0f, -1.0f },
	{ 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 1.0f },
};

alignas(16) static const uint32_t signBits[4] = {
	0x80000000, 0x80000000, 0x80000000, 0x80000000,
};

alignas(16) static const uint32_t noSignMask[4] = {
	0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF,
};

alignas(16) static const uint32_t lowBytesMask[4] = {
	0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF,
};

u32 IRRunBreakpoint(u32 pc) {
	// Should we skip this breakpoint?
	uint32_t skipFirst = g_breakpoints.CheckSkipFirst();
	if (skipFirst == pc || skipFirst == currentMIPS->pc)
		return 0;

	// Did we already hit one?
	if (coreState != CORE_RUNNING_CPU && coreState != CORE_NEXTFRAME)
		return 1;

	g_breakpoints.ExecBreakPoint(pc);
	return coreState != CORE_RUNNING_CPU ? 1 : 0;
}

u32 IRRunMemCheck(u32 pc, u32 addr) {
	// Should we skip this breakpoint?
	uint32_t skipFirst = g_breakpoints.CheckSkipFirst();
	if (skipFirst == pc || skipFirst == currentMIPS->pc)
		return 0;

	// Did we already hit one?
	if (coreState != CORE_RUNNING_CPU && coreState != CORE_NEXTFRAME)
		return 1;

	g_breakpoints.ExecOpMemCheck(addr, pc);
	return coreState != CORE_RUNNING_CPU ? 1 : 0;
}

// With GCC and Clang, each op jumps straight to the next op's handler through a table ("threaded"
// dispatch), which predicts much better than the one shared indirect jump of a switch. Other
// compilers get the switch. Ops missing from the table fall back to the switch, so they still work.
#if defined(__GNUC__) || defined(__clang__)
#define IR_THREADED_DISPATCH 1
#define IR_CASE(op) case IROp::op: L_##op:
#else
#define IR_CASE(op) case IROp::op:
#endif

#ifdef _DEBUG
#define IR_CHECK_ZERO_REG() if (mips->r[0] != 0) Crash();
#else
#define IR_CHECK_ZERO_REG()
#endif

#if IR_THREADED_DISPATCH
#define IR_NEXT do { IR_CHECK_ZERO_REG(); inst++; goto *dispatch[(int)inst->op]; } while (false)
#else
#define IR_NEXT { IR_CHECK_ZERO_REG(); inst++; continue; }
#endif

u32 IRInterpret(MIPSState *mips, const IRInst *inst) {
#if IR_THREADED_DISPATCH
	static const void *dispatch[256];
	static bool dispatchReady = false;
	if (!dispatchReady) {
		for (const void *&target : dispatch)
			target = &&L_switch;
		dispatch[(int)IROp::SetConst] = &&L_SetConst;
		dispatch[(int)IROp::SetConstF] = &&L_SetConstF;
		dispatch[(int)IROp::Add] = &&L_Add;
		dispatch[(int)IROp::Sub] = &&L_Sub;
		dispatch[(int)IROp::And] = &&L_And;
		dispatch[(int)IROp::Or] = &&L_Or;
		dispatch[(int)IROp::Xor] = &&L_Xor;
		dispatch[(int)IROp::Mov] = &&L_Mov;
		dispatch[(int)IROp::AddConst] = &&L_AddConst;
		dispatch[(int)IROp::OptAddConst] = &&L_OptAddConst;
		dispatch[(int)IROp::SubConst] = &&L_SubConst;
		dispatch[(int)IROp::AndConst] = &&L_AndConst;
		dispatch[(int)IROp::OptAndConst] = &&L_OptAndConst;
		dispatch[(int)IROp::OrConst] = &&L_OrConst;
		dispatch[(int)IROp::OptOrConst] = &&L_OptOrConst;
		dispatch[(int)IROp::XorConst] = &&L_XorConst;
		dispatch[(int)IROp::Neg] = &&L_Neg;
		dispatch[(int)IROp::Not] = &&L_Not;
		dispatch[(int)IROp::Ext8to32] = &&L_Ext8to32;
		dispatch[(int)IROp::Ext16to32] = &&L_Ext16to32;
		dispatch[(int)IROp::ReverseBits] = &&L_ReverseBits;
		dispatch[(int)IROp::Load8] = &&L_Load8;
		dispatch[(int)IROp::Load8Ext] = &&L_Load8Ext;
		dispatch[(int)IROp::Load16] = &&L_Load16;
		dispatch[(int)IROp::Load16Ext] = &&L_Load16Ext;
		dispatch[(int)IROp::Load32] = &&L_Load32;
		dispatch[(int)IROp::Load32Left] = &&L_Load32Left;
		dispatch[(int)IROp::Load32Right] = &&L_Load32Right;
		dispatch[(int)IROp::Load32Linked] = &&L_Load32Linked;
		dispatch[(int)IROp::LoadFloat] = &&L_LoadFloat;
		dispatch[(int)IROp::Store8] = &&L_Store8;
		dispatch[(int)IROp::Store16] = &&L_Store16;
		dispatch[(int)IROp::Store32] = &&L_Store32;
		dispatch[(int)IROp::Store32Left] = &&L_Store32Left;
		dispatch[(int)IROp::Store32Right] = &&L_Store32Right;
		dispatch[(int)IROp::Store32Conditional] = &&L_Store32Conditional;
		dispatch[(int)IROp::StoreFloat] = &&L_StoreFloat;
		dispatch[(int)IROp::LoadVec4] = &&L_LoadVec4;
		dispatch[(int)IROp::StoreVec4] = &&L_StoreVec4;
		dispatch[(int)IROp::Vec4Init] = &&L_Vec4Init;
		dispatch[(int)IROp::Vec4Shuffle] = &&L_Vec4Shuffle;
		dispatch[(int)IROp::Vec4Blend] = &&L_Vec4Blend;
		dispatch[(int)IROp::Vec4Mov] = &&L_Vec4Mov;
		dispatch[(int)IROp::Vec4Add] = &&L_Vec4Add;
		dispatch[(int)IROp::Vec4Sub] = &&L_Vec4Sub;
		dispatch[(int)IROp::Vec4Mul] = &&L_Vec4Mul;
		dispatch[(int)IROp::Vec4Div] = &&L_Vec4Div;
		dispatch[(int)IROp::Vec4Scale] = &&L_Vec4Scale;
		dispatch[(int)IROp::Vec4Neg] = &&L_Vec4Neg;
		dispatch[(int)IROp::Vec4Abs] = &&L_Vec4Abs;
		dispatch[(int)IROp::Vec2Unpack16To31] = &&L_Vec2Unpack16To31;
		dispatch[(int)IROp::Vec2Unpack16To32] = &&L_Vec2Unpack16To32;
		dispatch[(int)IROp::Vec4Unpack8To32] = &&L_Vec4Unpack8To32;
		dispatch[(int)IROp::Vec2Pack32To16] = &&L_Vec2Pack32To16;
		dispatch[(int)IROp::Vec2Pack31To16] = &&L_Vec2Pack31To16;
		dispatch[(int)IROp::Vec4Pack32To8] = &&L_Vec4Pack32To8;
		dispatch[(int)IROp::Vec4Pack31To8] = &&L_Vec4Pack31To8;
		dispatch[(int)IROp::Vec4DuplicateUpperBitsAndShift1] = &&L_Vec4DuplicateUpperBitsAndShift1;
		dispatch[(int)IROp::FCmpVfpuBit] = &&L_FCmpVfpuBit;
		dispatch[(int)IROp::FCmpVfpuAggregate] = &&L_FCmpVfpuAggregate;
		dispatch[(int)IROp::FCmovVfpuCC] = &&L_FCmovVfpuCC;
		dispatch[(int)IROp::Vec4Dot] = &&L_Vec4Dot;
		dispatch[(int)IROp::FSin] = &&L_FSin;
		dispatch[(int)IROp::FCos] = &&L_FCos;
		dispatch[(int)IROp::FRSqrt] = &&L_FRSqrt;
		dispatch[(int)IROp::FRecip] = &&L_FRecip;
		dispatch[(int)IROp::FAsin] = &&L_FAsin;
		dispatch[(int)IROp::FVSqrt] = &&L_FVSqrt;
		dispatch[(int)IROp::FExp2] = &&L_FExp2;
		dispatch[(int)IROp::FLog2] = &&L_FLog2;
		dispatch[(int)IROp::FSinCos] = &&L_FSinCos;
		dispatch[(int)IROp::FHalfToFloat] = &&L_FHalfToFloat;
		dispatch[(int)IROp::ShlImm] = &&L_ShlImm;
		dispatch[(int)IROp::ShrImm] = &&L_ShrImm;
		dispatch[(int)IROp::SarImm] = &&L_SarImm;
		dispatch[(int)IROp::RorImm] = &&L_RorImm;
		dispatch[(int)IROp::Shl] = &&L_Shl;
		dispatch[(int)IROp::Shr] = &&L_Shr;
		dispatch[(int)IROp::Sar] = &&L_Sar;
		dispatch[(int)IROp::Ror] = &&L_Ror;
		dispatch[(int)IROp::Clz] = &&L_Clz;
		dispatch[(int)IROp::Slt] = &&L_Slt;
		dispatch[(int)IROp::SltU] = &&L_SltU;
		dispatch[(int)IROp::SltConst] = &&L_SltConst;
		dispatch[(int)IROp::SltUConst] = &&L_SltUConst;
		dispatch[(int)IROp::MovZ] = &&L_MovZ;
		dispatch[(int)IROp::MovNZ] = &&L_MovNZ;
		dispatch[(int)IROp::Max] = &&L_Max;
		dispatch[(int)IROp::Min] = &&L_Min;
		dispatch[(int)IROp::MtLo] = &&L_MtLo;
		dispatch[(int)IROp::MtHi] = &&L_MtHi;
		dispatch[(int)IROp::MfLo] = &&L_MfLo;
		dispatch[(int)IROp::MfHi] = &&L_MfHi;
		dispatch[(int)IROp::Mult] = &&L_Mult;
		dispatch[(int)IROp::MultU] = &&L_MultU;
		dispatch[(int)IROp::Madd] = &&L_Madd;
		dispatch[(int)IROp::MaddU] = &&L_MaddU;
		dispatch[(int)IROp::Msub] = &&L_Msub;
		dispatch[(int)IROp::MsubU] = &&L_MsubU;
		dispatch[(int)IROp::Div] = &&L_Div;
		dispatch[(int)IROp::DivU] = &&L_DivU;
		dispatch[(int)IROp::BSwap16] = &&L_BSwap16;
		dispatch[(int)IROp::BSwap32] = &&L_BSwap32;
		dispatch[(int)IROp::FAdd] = &&L_FAdd;
		dispatch[(int)IROp::FSub] = &&L_FSub;
		dispatch[(int)IROp::FMul] = &&L_FMul;
		dispatch[(int)IROp::FDiv] = &&L_FDiv;
		dispatch[(int)IROp::FMin] = &&L_FMin;
		dispatch[(int)IROp::FMax] = &&L_FMax;
		dispatch[(int)IROp::FMov] = &&L_FMov;
		dispatch[(int)IROp::FAbs] = &&L_FAbs;
		dispatch[(int)IROp::FSqrt] = &&L_FSqrt;
		dispatch[(int)IROp::FNeg] = &&L_FNeg;
		dispatch[(int)IROp::FSat0_1] = &&L_FSat0_1;
		dispatch[(int)IROp::FSatMinus1_1] = &&L_FSatMinus1_1;
		dispatch[(int)IROp::FSign] = &&L_FSign;
		dispatch[(int)IROp::FpCondFromReg] = &&L_FpCondFromReg;
		dispatch[(int)IROp::FpCondToReg] = &&L_FpCondToReg;
		dispatch[(int)IROp::FpCtrlFromReg] = &&L_FpCtrlFromReg;
		dispatch[(int)IROp::FpCtrlToReg] = &&L_FpCtrlToReg;
		dispatch[(int)IROp::VfpuCtrlToReg] = &&L_VfpuCtrlToReg;
		dispatch[(int)IROp::FRound] = &&L_FRound;
		dispatch[(int)IROp::FTrunc] = &&L_FTrunc;
		dispatch[(int)IROp::FCeil] = &&L_FCeil;
		dispatch[(int)IROp::FFloor] = &&L_FFloor;
		dispatch[(int)IROp::FCmp] = &&L_FCmp;
		dispatch[(int)IROp::FCvtSW] = &&L_FCvtSW;
		dispatch[(int)IROp::FCvtWS] = &&L_FCvtWS;
		dispatch[(int)IROp::FCvtScaledSW] = &&L_FCvtScaledSW;
		dispatch[(int)IROp::FCvtScaledWS] = &&L_FCvtScaledWS;
		dispatch[(int)IROp::FMovFromGPR] = &&L_FMovFromGPR;
		dispatch[(int)IROp::OptFCvtSWFromGPR] = &&L_OptFCvtSWFromGPR;
		dispatch[(int)IROp::FMovToGPR] = &&L_FMovToGPR;
		dispatch[(int)IROp::OptFMovToGPRShr8] = &&L_OptFMovToGPRShr8;
		dispatch[(int)IROp::ExitToConst] = &&L_ExitToConst;
		dispatch[(int)IROp::ExitToReg] = &&L_ExitToReg;
		dispatch[(int)IROp::OptExitToConstIfEqElse] = &&L_OptExitToConstIfEqElse;
		dispatch[(int)IROp::OptExitToConstIfNeqElse] = &&L_OptExitToConstIfNeqElse;
		dispatch[(int)IROp::OptExitToConstIfGtZElse] = &&L_OptExitToConstIfGtZElse;
		dispatch[(int)IROp::OptExitToConstIfGeZElse] = &&L_OptExitToConstIfGeZElse;
		dispatch[(int)IROp::OptExitToConstIfLtZElse] = &&L_OptExitToConstIfLtZElse;
		dispatch[(int)IROp::OptExitToConstIfLeZElse] = &&L_OptExitToConstIfLeZElse;
		dispatch[(int)IROp::ExitToConstIfEq] = &&L_ExitToConstIfEq;
		dispatch[(int)IROp::ExitToConstIfNeq] = &&L_ExitToConstIfNeq;
		dispatch[(int)IROp::ExitToConstIfGtZ] = &&L_ExitToConstIfGtZ;
		dispatch[(int)IROp::ExitToConstIfGeZ] = &&L_ExitToConstIfGeZ;
		dispatch[(int)IROp::ExitToConstIfLtZ] = &&L_ExitToConstIfLtZ;
		dispatch[(int)IROp::ExitToConstIfLeZ] = &&L_ExitToConstIfLeZ;
		dispatch[(int)IROp::Downcount] = &&L_Downcount;
		dispatch[(int)IROp::SetPC] = &&L_SetPC;
		dispatch[(int)IROp::SetPCConst] = &&L_SetPCConst;
		dispatch[(int)IROp::Syscall] = &&L_Syscall;
		dispatch[(int)IROp::SyscallUnresolved] = &&L_SyscallUnresolved;
		dispatch[(int)IROp::ExitToPC] = &&L_ExitToPC;
		dispatch[(int)IROp::Interpret] = &&L_Interpret;
		dispatch[(int)IROp::CallReplacement] = &&L_CallReplacement;
		dispatch[(int)IROp::SetCtrlVFPU] = &&L_SetCtrlVFPU;
		dispatch[(int)IROp::SetCtrlVFPUReg] = &&L_SetCtrlVFPUReg;
		dispatch[(int)IROp::SetCtrlVFPUFReg] = &&L_SetCtrlVFPUFReg;
		dispatch[(int)IROp::ApplyRoundingMode] = &&L_ApplyRoundingMode;
		dispatch[(int)IROp::RestoreRoundingMode] = &&L_RestoreRoundingMode;
		dispatch[(int)IROp::UpdateRoundingMode] = &&L_UpdateRoundingMode;
		dispatch[(int)IROp::Break] = &&L_Break;
		dispatch[(int)IROp::Breakpoint] = &&L_Breakpoint;
		dispatch[(int)IROp::MemoryCheck] = &&L_MemoryCheck;
		dispatch[(int)IROp::ValidateAddress8] = &&L_ValidateAddress8;
		dispatch[(int)IROp::ValidateAddress16] = &&L_ValidateAddress16;
		dispatch[(int)IROp::ValidateAddress32] = &&L_ValidateAddress32;
		dispatch[(int)IROp::ValidateAddress128] = &&L_ValidateAddress128;
		dispatch[(int)IROp::LogIRBlock] = &&L_LogIRBlock;
		dispatch[(int)IROp::Nop] = &&L_Nop;
		dispatch[(int)IROp::Bad] = &&L_Bad;
		dispatchReady = true;
	}
#endif

	while (true) {
#if IR_THREADED_DISPATCH
	L_switch:
#endif
		switch (inst->op) {
		IR_CASE(SetConst)
			mips->r[inst->dest] = inst->constant;
			IR_NEXT;
		IR_CASE(SetConstF)
			memcpy(&mips->f[inst->dest], &inst->constant, 4);
			IR_NEXT;
		IR_CASE(Add)
			mips->r[inst->dest] = mips->r[inst->src1] + mips->r[inst->src2];
			IR_NEXT;
		IR_CASE(Sub)
			mips->r[inst->dest] = mips->r[inst->src1] - mips->r[inst->src2];
			IR_NEXT;
		IR_CASE(And)
			mips->r[inst->dest] = mips->r[inst->src1] & mips->r[inst->src2];
			IR_NEXT;
		IR_CASE(Or)
			mips->r[inst->dest] = mips->r[inst->src1] | mips->r[inst->src2];
			IR_NEXT;
		IR_CASE(Xor)
			mips->r[inst->dest] = mips->r[inst->src1] ^ mips->r[inst->src2];
			IR_NEXT;
		IR_CASE(Mov)
			mips->r[inst->dest] = mips->r[inst->src1];
			IR_NEXT;
		IR_CASE(AddConst)
			mips->r[inst->dest] = mips->r[inst->src1] + inst->constant;
			IR_NEXT;
		IR_CASE(OptAddConst)  // For this one, it's worth having a "unary" variant of the above that only needs to read one register param.
			mips->r[inst->dest] += inst->constant;
			IR_NEXT;
		IR_CASE(SubConst)
			mips->r[inst->dest] = mips->r[inst->src1] - inst->constant;
			IR_NEXT;
		IR_CASE(AndConst)
			mips->r[inst->dest] = mips->r[inst->src1] & inst->constant;
			IR_NEXT;
		IR_CASE(OptAndConst)  // For this one, it's worth having a "unary" variant of the above that only needs to read one register param.
			mips->r[inst->dest] &= inst->constant;
			IR_NEXT;
		IR_CASE(OrConst)
			mips->r[inst->dest] = mips->r[inst->src1] | inst->constant;
			IR_NEXT;
		IR_CASE(OptOrConst)
			mips->r[inst->dest] |= inst->constant;
			IR_NEXT;
		IR_CASE(XorConst)
			mips->r[inst->dest] = mips->r[inst->src1] ^ inst->constant;
			IR_NEXT;
		IR_CASE(Neg)
			mips->r[inst->dest] = (u32)(-(s32)mips->r[inst->src1]);
			IR_NEXT;
		IR_CASE(Not)
			mips->r[inst->dest] = ~mips->r[inst->src1];
			IR_NEXT;
		IR_CASE(Ext8to32)
			mips->r[inst->dest] = SignExtend8ToU32(mips->r[inst->src1]);
			IR_NEXT;
		IR_CASE(Ext16to32)
			mips->r[inst->dest] = SignExtend16ToU32(mips->r[inst->src1]);
			IR_NEXT;
		IR_CASE(ReverseBits)
			mips->r[inst->dest] = ReverseBits32(mips->r[inst->src1]);
			IR_NEXT;

		IR_CASE(Load8)
			mips->r[inst->dest] = Memory::ReadUnchecked_U8(mips->r[inst->src1] + inst->constant);
			IR_NEXT;
		IR_CASE(Load8Ext)
			mips->r[inst->dest] = SignExtend8ToU32(Memory::ReadUnchecked_U8(mips->r[inst->src1] + inst->constant));
			IR_NEXT;
		IR_CASE(Load16)
			mips->r[inst->dest] = Memory::ReadUnchecked_U16(mips->r[inst->src1] + inst->constant);
			IR_NEXT;
		IR_CASE(Load16Ext)
			mips->r[inst->dest] = SignExtend16ToU32(Memory::ReadUnchecked_U16(mips->r[inst->src1] + inst->constant));
			IR_NEXT;
		IR_CASE(Load32)
			mips->r[inst->dest] = Memory::ReadUnchecked_U32(mips->r[inst->src1] + inst->constant);
			IR_NEXT;
		IR_CASE(Load32Left)
		{
			u32 addr = mips->r[inst->src1] + inst->constant;
			u32 shift = (addr & 3) * 8;
			u32 mem = Memory::ReadUnchecked_U32(addr & 0xfffffffc);
			u32 destMask = 0x00ffffff >> shift;
			mips->r[inst->dest] = (mips->r[inst->dest] & destMask) | (mem << (24 - shift));
			IR_NEXT;
		}
		IR_CASE(Load32Right)
		{
			u32 addr = mips->r[inst->src1] + inst->constant;
			u32 shift = (addr & 3) * 8;
			u32 mem = Memory::ReadUnchecked_U32(addr & 0xfffffffc);
			u32 destMask = 0xffffff00 << (24 - shift);
			mips->r[inst->dest] = (mips->r[inst->dest] & destMask) | (mem >> shift);
			IR_NEXT;
		}
		IR_CASE(Load32Linked)
			if (inst->dest != MIPS_REG_ZERO)
				mips->r[inst->dest] = Memory::ReadUnchecked_U32(mips->r[inst->src1] + inst->constant);
			mips->llBit = 1;
			IR_NEXT;
		IR_CASE(LoadFloat)
			mips->f[inst->dest] = Memory::ReadUnchecked_Float(mips->r[inst->src1] + inst->constant);
			IR_NEXT;

		IR_CASE(Store8)
			Memory::WriteUnchecked_U8(mips->r[inst->src3], mips->r[inst->src1] + inst->constant);
			IR_NEXT;
		IR_CASE(Store16)
			Memory::WriteUnchecked_U16(mips->r[inst->src3], mips->r[inst->src1] + inst->constant);
			IR_NEXT;
		IR_CASE(Store32)
			Memory::WriteUnchecked_U32(mips->r[inst->src3], mips->r[inst->src1] + inst->constant);
			IR_NEXT;
		IR_CASE(Store32Left)
		{
			u32 addr = mips->r[inst->src1] + inst->constant;
			u32 shift = (addr & 3) * 8;
			u32 mem = Memory::ReadUnchecked_U32(addr & 0xfffffffc);
			u32 memMask = 0xffffff00 << shift;
			u32 result = (mips->r[inst->src3] >> (24 - shift)) | (mem & memMask);
			Memory::WriteUnchecked_U32(result, addr & 0xfffffffc);
			IR_NEXT;
		}
		IR_CASE(Store32Right)
		{
			u32 addr = mips->r[inst->src1] + inst->constant;
			u32 shift = (addr & 3) * 8;
			u32 mem = Memory::ReadUnchecked_U32(addr & 0xfffffffc);
			u32 memMask = 0x00ffffff >> (24 - shift);
			u32 result = (mips->r[inst->src3] << shift) | (mem & memMask);
			Memory::WriteUnchecked_U32(result, addr & 0xfffffffc);
			IR_NEXT;
		}
		IR_CASE(Store32Conditional)
			if (mips->llBit) {
				Memory::WriteUnchecked_U32(mips->r[inst->src3], mips->r[inst->src1] + inst->constant);
				if (inst->dest != MIPS_REG_ZERO) {
					mips->r[inst->dest] = 1;
				}
			} else if (inst->dest != MIPS_REG_ZERO) {
				mips->r[inst->dest] = 0;
			}
			IR_NEXT;
		IR_CASE(StoreFloat)
			Memory::WriteUnchecked_Float(mips->f[inst->src3], mips->r[inst->src1] + inst->constant);
			IR_NEXT;

		IR_CASE(LoadVec4)
		{
			u32 base = mips->r[inst->src1] + inst->constant;
			// This compiles to a nice SSE load/store on x86, and hopefully similar on ARM.
			memcpy(&mips->f[inst->dest], Memory::GetPointerUnchecked(base), 4 * 4);
			IR_NEXT;
		}
		IR_CASE(StoreVec4)
		{
			u32 base = mips->r[inst->src1] + inst->constant;
			memcpy((float *)Memory::GetPointerUnchecked(base), &mips->f[inst->dest], 4 * 4);
			IR_NEXT;
		}

		IR_CASE(Vec4Init)
		{
			memcpy(&mips->f[inst->dest], vec4InitValues[inst->src1], 4 * sizeof(float));
			IR_NEXT;
		}

		IR_CASE(Vec4Shuffle)
		{
			// Can't use the SSE shuffle here because it takes an immediate. pshufb with a table would work though,
			// or a big switch - there are only 256 shuffles possible (4^4)
			float temp[4];
			for (u32 i = 0; i < 4; i++)
				temp[i] = mips->f[(u32)inst->src1 + (u32)((inst->src2 >> (i * 2)) & 3)];
			const u32 dest = inst->dest;
			for (u32 i = 0; i < 4; i++)
				mips->f[dest + i] = temp[i];
			IR_NEXT;
		}

		IR_CASE(Vec4Blend)
		{
			const u32 dest = inst->dest;
			const u32 src1 = inst->src1;
			const u32 src2 = inst->src2;
			const u32 constant = inst->constant;
			// 90% of calls to this is inst->constant == 7 or inst->constant == 8. Some are 1 and 4, others very rare.
			// Could use _mm_blendv_ps (SSE4+BMI), vbslq_f32 (ARM), __riscv_vmerge_vvm (RISC-V)
			float temp[4];
			for (u32 i = 0; i < 4; i++)
				temp[i] = ((constant >> i) & 1) ? mips->f[src2 + i] : mips->f[src1 + i];
			for (u32 i = 0; i < 4; i++)
				mips->f[dest + i] = temp[i];
			IR_NEXT;
		}

		IR_CASE(Vec4Mov)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_load_ps(&mips->f[inst->src1]));
#elif PPSSPP_ARCH(ARM_NEON)
			vst1q_f32(&mips->f[inst->dest], vld1q_f32(&mips->f[inst->src1]));
#else
			memcpy(&mips->f[inst->dest], &mips->f[inst->src1], 4 * sizeof(float));
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Add)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_add_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#elif PPSSPP_ARCH(ARM_NEON)
			vst1q_f32(&mips->f[inst->dest], vaddq_f32(vld1q_f32(&mips->f[inst->src1]), vld1q_f32(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] + mips->f[inst->src2 + i];
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Sub)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_sub_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#elif PPSSPP_ARCH(ARM_NEON)
			vst1q_f32(&mips->f[inst->dest], vsubq_f32(vld1q_f32(&mips->f[inst->src1]), vld1q_f32(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] - mips->f[inst->src2 + i];
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Mul)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_mul_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#elif PPSSPP_ARCH(ARM_NEON)
			vst1q_f32(&mips->f[inst->dest], vmulq_f32(vld1q_f32(&mips->f[inst->src1]), vld1q_f32(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] * mips->f[inst->src2 + i];
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Div)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_div_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#elif PPSSPP_ARCH(ARM64_NEON)
			vst1q_f32(&mips->f[inst->dest], vdivq_f32(vld1q_f32(&mips->f[inst->src1]), vld1q_f32(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] / mips->f[inst->src2 + i];
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Scale)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_mul_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_set1_ps(mips->f[inst->src2])));
#elif PPSSPP_ARCH(ARM_NEON)
			vst1q_f32(&mips->f[inst->dest], vmulq_lane_f32(vld1q_f32(&mips->f[inst->src1]), vdup_n_f32(mips->f[inst->src2]), 0));
#else
			const float factor = mips->f[inst->src2];
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] * factor;
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Neg)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_xor_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps((const float *)signBits)));
#elif PPSSPP_ARCH(ARM_NEON)
			vst1q_f32(&mips->f[inst->dest], vnegq_f32(vld1q_f32(&mips->f[inst->src1])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = -mips->f[inst->src1 + i];
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Abs)
		{
#if PPSSPP_ARCH(SSE2)
			_mm_store_ps(&mips->f[inst->dest], _mm_and_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps((const float *)noSignMask)));
#elif PPSSPP_ARCH(ARM_NEON)
			vst1q_f32(&mips->f[inst->dest], vabsq_f32(vld1q_f32(&mips->f[inst->src1])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = fabsf(mips->f[inst->src1 + i]);
#endif
			IR_NEXT;
		}

		IR_CASE(Vec2Unpack16To31)
		{
			const u32 dest = inst->dest;
			const u32 src1 = inst->src1;
			const u32 temp0 = (mips->fi[src1] << 16) >> 1;
			const u32 temp1 = (mips->fi[src1] & 0xFFFF0000) >> 1;
			mips->fi[dest] = temp0;
			mips->fi[dest + 1] = temp1;
			IR_NEXT;
		}

		IR_CASE(Vec2Unpack16To32)
		{
			const u32 dest = inst->dest;
			const u32 src1 = inst->src1;
			const u32 temp0 = (mips->fi[src1] << 16);
			const u32 temp1 = (mips->fi[src1] & 0xFFFF0000);
			mips->fi[dest] = temp0;
			mips->fi[dest + 1] = temp1;
			IR_NEXT;
		}

		IR_CASE(Vec4Unpack8To32)
		{
			// Used in Gran Turismo
#if PPSSPP_ARCH(SSE2)
			__m128i src = _mm_cvtsi32_si128(mips->fi[inst->src1]);
			src = _mm_unpacklo_epi8(src, _mm_setzero_si128());
			src = _mm_unpacklo_epi16(src, _mm_setzero_si128());
			_mm_store_si128((__m128i *)&mips->fi[inst->dest], _mm_slli_epi32(src, 24));
#elif PPSSPP_ARCH(ARM_NEON)
			const uint8x8_t value = (uint8x8_t)vdup_n_u32(mips->fi[inst->src1]);
			const uint16x8_t value16 = vmovl_u8(value);
			const uint32x4_t value32 = vshlq_n_u32(vshll_n_u16(vget_low_u16(value16), 8), 16);  // note: vshll has a range limited to 0..16
			vst1q_u32(&mips->fi[inst->dest], value32);
#else
			mips->fi[inst->dest] = (mips->fi[inst->src1] << 24);
			mips->fi[inst->dest + 1] = (mips->fi[inst->src1] << 16) & 0xFF000000;
			mips->fi[inst->dest + 2] = (mips->fi[inst->src1] << 8) & 0xFF000000;
			mips->fi[inst->dest + 3] = (mips->fi[inst->src1]) & 0xFF000000;
#endif
			IR_NEXT;
		}

		IR_CASE(Vec2Pack32To16)
		{
			u32 val = mips->fi[inst->src1] >> 16;
			mips->fi[inst->dest] = val | (mips->fi[(u32)inst->src1 + 1] & 0xFFFF0000);
			IR_NEXT;
		}

		IR_CASE(Vec2Pack31To16)
		{
			// Used in Tekken 6. Negative lanes clamp to zero.
			const u32 s0 = (s32)mips->fi[inst->src1] < 0 ? 0 : mips->fi[inst->src1];
			const u32 s1 = (s32)mips->fi[(u32)inst->src1 + 1] < 0 ? 0 : mips->fi[(u32)inst->src1 + 1];
			mips->fi[inst->dest] = ((s0 >> 15) & 0xFFFF) | ((s1 << 1) & 0xFFFF0000);
			IR_NEXT;
		}

		IR_CASE(Vec4Pack32To8)
		{
#if PPSSPP_ARCH(SSE2)
			__m128i src = _mm_loadu_si128((__m128i *)&mips->fi[inst->src1]);
			// Shift each 32-bit lane right by 24 bits
			src = _mm_srli_epi32(src, 24);
			// Pack 32-bit lanes to 16-bit, then 16-bit to 8-bit
			// This moves our target bytes to the bottom of the XMM register
			src = _mm_packs_epi32(src, src);
			src = _mm_packus_epi16(src, src);
			// Extract the lower 32 bits (which now contains our 4 bytes)
			mips->fi[inst->dest] = (u32)_mm_cvtsi128_si32(src);
#elif PPSSPP_ARCH(ARM_NEON)
			// 1. Load 4x32-bit lanes
			uint32x4_t src = vld1q_u32(&mips->fi[inst->src1]);
			// 2. Manual shift right by 24 (this is allowed on full 128-bit vectors. vshrn can't shift by more than 16.
			uint32x4_t shifted = vshrq_n_u32(src, 24);
			// 3. Narrow from 32-bit to 16-bit (vmovn works on the bottom 64 bits)
			uint16x4_t narrow_16 = vmovn_u32(shifted);
			// 4. Narrow from 16-bit to 8-bit
			// We combine the 64-bit result with itself to keep the 128-bit logic happy
			uint8x8_t narrow_8 = vmovn_u16(vcombine_u16(narrow_16, narrow_16));
			// 5. Extract the result as a single u32
			mips->fi[inst->dest] = vget_lane_u32(vreinterpret_u32_u8(narrow_8), 0);
#else
			// Removed previous SSE code due to the need for unsigned 16-bit pack, which I'm too lazy to work around the lack of in SSE2.
			// pshufb or SSE4 instructions can be used instead.
			u32 val = mips->fi[(u32)inst->src1] >> 24;
			val |= (mips->fi[(u32)inst->src1 + 1] >> 16) & 0xFF00;
			val |= (mips->fi[(u32)inst->src1 + 2] >> 8) & 0xFF0000;
			val |= (mips->fi[(u32)inst->src1 + 3]) & 0xFF000000;
			mips->fi[inst->dest] = val;
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4Pack31To8)
		{
			// Used in Tekken 6, Gran Turismo

			// Bits 30-23 of each lane, with negative lanes clamped to zero.
#if PPSSPP_ARCH(SSE2)
			__m128i src = _mm_loadu_si128((__m128i *) & mips->fi[inst->src1]);
			// An arithmetic shift leaves 0-255 for positive lanes and negative values for negative
			// ones, which the unsigned saturation in the second pack turns into 0.
			src = _mm_srai_epi32(src, 23);
			src = _mm_packs_epi32(src, src);
			src = _mm_packus_epi16(src, src);
			mips->fi[inst->dest] = (u32)_mm_cvtsi128_si32(src);
#elif PPSSPP_ARCH(ARM_NEON)
			int32x4_t value = vmaxq_s32(vld1q_s32((const int32_t *)&mips->fi[inst->src1]), vdupq_n_s32(0));
			uint32x4_t shifted = vshlq_n_u32(vreinterpretq_u32_s32(value), 1);
			uint16x4_t halved = vshrn_n_u32(shifted, 16);
			uint8x8_t halvedAgain = vshrn_n_u16(vcombine_u16(halved, vdup_n_u16(0)), 8);
			mips->fi[inst->dest] = vget_lane_u32(vreinterpret_u32_u8(halvedAgain), 0);
#else
			u32 val = 0;
			for (int i = 0; i < 4; i++) {
				const u32 lane = mips->fi[(u32)inst->src1 + i];
				if ((s32)lane > 0)
					val |= ((lane >> 23) & 0xFF) << (8 * i);
			}
			mips->fi[(u32)inst->dest] = val;
#endif
			IR_NEXT;
		}

		IR_CASE(Vec4DuplicateUpperBitsAndShift1)  // For vuc2i, the weird one.
		{
			const int src1 = inst->src1;
			const int dest = inst->dest;
			u32 temp[4];
			for (int i = 0; i < 4; i++) {
				u32 val = mips->fi[src1 + i];
				val = val | (val >> 8);
				val = val | (val >> 16);
				temp[i] = val >> 1;
			}
			for (int i = 0; i < 4; i++) {
				mips->fi[dest + i] = temp[i];
			}
			IR_NEXT;
		}

		IR_CASE(FCmpVfpuBit)
		{
			const int op = inst->dest & 0xF;
			const int bit = inst->dest >> 4;
			int result = 0;
			switch (op) {
			case VC_EQ: result = mips->f[inst->src1] == mips->f[inst->src2]; break;
			case VC_NE: result = mips->f[inst->src1] != mips->f[inst->src2]; break;
			case VC_LT: result = mips->f[inst->src1] < mips->f[inst->src2]; break;
			case VC_LE: result = mips->f[inst->src1] <= mips->f[inst->src2]; break;
			case VC_GT: result = mips->f[inst->src1] > mips->f[inst->src2]; break;
			case VC_GE: result = mips->f[inst->src1] >= mips->f[inst->src2]; break;
			case VC_EZ: result = mips->f[inst->src1] == 0.0f; break;
			case VC_NZ: result = mips->f[inst->src1] != 0.0f; break;
			case VC_EN: result = my_isnan(mips->f[inst->src1]); break;
			case VC_NN: result = !my_isnan(mips->f[inst->src1]); break;
			case VC_EI: result = my_isinf(mips->f[inst->src1]); break;
			case VC_NI: result = !my_isinf(mips->f[inst->src1]); break;
			case VC_ES: result = my_isnanorinf(mips->f[inst->src1]); break;
			case VC_NS: result = !my_isnanorinf(mips->f[inst->src1]); break;
			case VC_TR: result = 1; break;
			case VC_FL: result = 0; break;
			default:
				result = 0;
			}
			if (result != 0) {
				mips->vfpuCtrl[VFPU_CTRL_CC] |= (1 << bit);
			} else {
				mips->vfpuCtrl[VFPU_CTRL_CC] &= ~(1 << bit);
			}
			IR_NEXT;
		}

		IR_CASE(FCmpVfpuAggregate)
		{
			const u32 mask = inst->dest;
			const u32 cc = mips->vfpuCtrl[VFPU_CTRL_CC];
			int anyBit = (cc & mask) ? 0x10 : 0x00;
			int allBit = (cc & mask) == mask ? 0x20 : 0x00;
			mips->vfpuCtrl[VFPU_CTRL_CC] = (cc & ~0x30) | anyBit | allBit;
			IR_NEXT;
		}

		IR_CASE(FCmovVfpuCC)
			if (((mips->vfpuCtrl[VFPU_CTRL_CC] >> (inst->src2 & 0xf)) & 1) == ((u32)inst->src2 >> 7)) {
				mips->f[inst->dest] = mips->f[inst->src1];
			}
			IR_NEXT;

		IR_CASE(Vec4Dot)
		{
			// Not quickly implementable on all platforms, unfortunately.
			// Though, this is still pretty fast compared to one split into multiple IR instructions.
			// This might be good though: https://gist.github.com/rikusalminen/3040241
			const float *a = &mips->f[(u32)inst->src1];
			const float *b = &mips->f[(u32)inst->src2];
			mips->f[inst->dest] = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
			IR_NEXT;
		}

		IR_CASE(FSin)
			mips->f[inst->dest] = vfpu_sin(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FCos)
			mips->f[inst->dest] = vfpu_cos(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FRSqrt)
			mips->f[inst->dest] = vfpu_rsqrt(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FRecip)
			mips->f[inst->dest] = vfpu_rcp(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FAsin)
			mips->f[inst->dest] = vfpu_asin(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FVSqrt)
			mips->f[inst->dest] = vfpu_sqrt(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FExp2)
			mips->f[inst->dest] = vfpu_exp2(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FLog2)
			mips->f[inst->dest] = vfpu_log2(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FSinCos)
		{
			float s, c;
			vfpu_sincos(mips->f[inst->src1], s, c);
			mips->f[inst->dest] = s;
			mips->f[inst->dest + 1] = c;
			IR_NEXT;
		}
		IR_CASE(FHalfToFloat)
			mips->fi[inst->dest] = vfpu_h2f((u16)(inst->src2 ? mips->fi[inst->src1] >> 16 : mips->fi[inst->src1] & 0xFFFF));
			IR_NEXT;

		IR_CASE(ShlImm)
			mips->r[inst->dest] = mips->r[inst->src1] << (int)inst->src2;
			IR_NEXT;
		IR_CASE(ShrImm)
			mips->r[inst->dest] = mips->r[inst->src1] >> (int)inst->src2;
			IR_NEXT;
		IR_CASE(SarImm)
			mips->r[inst->dest] = (s32)mips->r[inst->src1] >> (int)inst->src2;
			IR_NEXT;
		IR_CASE(RorImm)
		{
			u32 x = mips->r[inst->src1];
			int sa = inst->src2;
			mips->r[inst->dest] = (x >> sa) | (x << (32 - sa));
		}
		IR_NEXT;

		IR_CASE(Shl)
			mips->r[inst->dest] = mips->r[inst->src1] << (mips->r[inst->src2] & 31);
			IR_NEXT;
		IR_CASE(Shr)
			mips->r[inst->dest] = mips->r[inst->src1] >> (mips->r[inst->src2] & 31);
			IR_NEXT;
		IR_CASE(Sar)
			mips->r[inst->dest] = (s32)mips->r[inst->src1] >> (mips->r[inst->src2] & 31);
			IR_NEXT;
		IR_CASE(Ror)
		{
			u32 x = mips->r[inst->src1];
			int sa = mips->r[inst->src2] & 31;
			mips->r[inst->dest] = (x >> sa) | (x << (32 - sa));
			IR_NEXT;
		}

		IR_CASE(Clz)
		{
			mips->r[inst->dest] = clz32(mips->r[inst->src1]);
			IR_NEXT;
		}

		IR_CASE(Slt)
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)mips->r[inst->src2];
			IR_NEXT;

		IR_CASE(SltU)
			mips->r[inst->dest] = mips->r[inst->src1] < mips->r[inst->src2];
			IR_NEXT;

		IR_CASE(SltConst)
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)inst->constant;
			IR_NEXT;

		IR_CASE(SltUConst)
			mips->r[inst->dest] = mips->r[inst->src1] < inst->constant;
			IR_NEXT;

		IR_CASE(MovZ)
			if (mips->r[inst->src1] == 0)
				mips->r[inst->dest] = mips->r[inst->src2];
			IR_NEXT;
		IR_CASE(MovNZ)
			if (mips->r[inst->src1] != 0)
				mips->r[inst->dest] = mips->r[inst->src2];
			IR_NEXT;

		IR_CASE(Max)
			mips->r[inst->dest] = (s32)mips->r[inst->src1] > (s32)mips->r[inst->src2] ? mips->r[inst->src1] : mips->r[inst->src2];
			IR_NEXT;
		IR_CASE(Min)
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)mips->r[inst->src2] ? mips->r[inst->src1] : mips->r[inst->src2];
			IR_NEXT;

		IR_CASE(MtLo)
			mips->lo = mips->r[inst->src1];
			IR_NEXT;
		IR_CASE(MtHi)
			mips->hi = mips->r[inst->src1];
			IR_NEXT;
		IR_CASE(MfLo)
			mips->r[inst->dest] = mips->lo;
			IR_NEXT;
		IR_CASE(MfHi)
			mips->r[inst->dest] = mips->hi;
			IR_NEXT;

		IR_CASE(Mult)
		{
			s64 result = (s64)(s32)mips->r[inst->src1] * (s64)(s32)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);  // note: lo is followed by hi, so this is ok (little-endian).
			IR_NEXT;
		}
		IR_CASE(MultU)
		{
			u64 result = (u64)mips->r[inst->src1] * (u64)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			IR_NEXT;
		}
		IR_CASE(Madd)
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result += (s64)(s32)mips->r[inst->src1] * (s64)(s32)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			IR_NEXT;
		}
		IR_CASE(MaddU)
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result += (u64)mips->r[inst->src1] * (u64)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			IR_NEXT;
		}
		IR_CASE(Msub)
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result -= (s64)(s32)mips->r[inst->src1] * (s64)(s32)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			IR_NEXT;
		}
		IR_CASE(MsubU)
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result -= (u64)mips->r[inst->src1] * (u64)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			IR_NEXT;
		}

		IR_CASE(Div)
		{
			s32 numerator = (s32)mips->r[inst->src1];
			s32 denominator = (s32)mips->r[inst->src2];
			if (numerator == (s32)0x80000000 && denominator == -1) {
				// The one overflow. Hardware leaves the remainder at zero (cpu/cpu_alu/cpu_div).
				mips->lo = 0x80000000;
				mips->hi = 0;
			} else if (denominator != 0) {
				mips->lo = (u32)(numerator / denominator);
				mips->hi = (u32)(numerator % denominator);
			} else {
				mips->lo = numerator < 0 ? 1 : -1;
				mips->hi = numerator;
			}
			IR_NEXT;
		}
		IR_CASE(DivU)
		{
			u32 numerator = mips->r[inst->src1];
			u32 denominator = mips->r[inst->src2];
			if (denominator != 0) {
				mips->lo = numerator / denominator;
				mips->hi = numerator % denominator;
			} else {
				mips->lo = numerator <= 0xFFFF ? 0xFFFF : -1;
				mips->hi = numerator;
			}
			IR_NEXT;
		}

		IR_CASE(BSwap16)
		{
			u32 x = mips->r[inst->src1];
			// Don't think we can beat this with intrinsics.
			mips->r[inst->dest] = ((x & 0xFF00FF00) >> 8) | ((x & 0x00FF00FF) << 8);
			IR_NEXT;
		}
		IR_CASE(BSwap32)
		{
			mips->r[inst->dest] = swap32(mips->r[inst->src1]);
			IR_NEXT;
		}

		IR_CASE(FAdd)
			mips->f[inst->dest] = mips->f[inst->src1] + mips->f[inst->src2];
			IR_NEXT;
		IR_CASE(FSub)
			mips->f[inst->dest] = mips->f[inst->src1] - mips->f[inst->src2];
			IR_NEXT;
		IR_CASE(FMul)
#if 1
		{
			float a = mips->f[inst->src1];
			float b = mips->f[inst->src2];
			if ((b == 0.0f && my_isinf(a)) || (a == 0.0f && my_isinf(b))) {
				mips->fi[inst->dest] = 0x7fc00000;
			} else {
				mips->f[inst->dest] = a * b;
			}
		}
			IR_NEXT;
#else
			// Not sure if faster since it needs to load the operands twice? But the code is simpler.
			{
				// Takes care of negative zero by masking away the top bit, which also makes the inf check shorter.
				u32 a = mips->fi[inst->src1] & 0x7FFFFFFF;
				u32 b = mips->fi[inst->src2] & 0x7FFFFFFF;
				if ((a == 0 && b == 0x7F800000) || (b == 0 && a == 0x7F800000)) {
					mips->fi[inst->dest] = 0x7fc00000;
				} else {
					mips->f[inst->dest] = mips->f[inst->src1] * mips->f[inst->src2];
				}
				IR_NEXT;
			}
#endif
		IR_CASE(FDiv)
			mips->f[inst->dest] = mips->f[inst->src1] / mips->f[inst->src2];
			IR_NEXT;
		IR_CASE(FMin)
			if (my_isnan(mips->f[inst->src1]) || my_isnan(mips->f[inst->src2])) {
				// See interpreter for this logic: this is for vmin, we're comparing mantissa+exp.
				if (mips->fs[inst->src1] < 0 && mips->fs[inst->src2] < 0) {
					mips->fs[inst->dest] = std::max(mips->fs[inst->src1], mips->fs[inst->src2]);
				} else {
					mips->fs[inst->dest] = std::min(mips->fs[inst->src1], mips->fs[inst->src2]);
				}
			} else {
				mips->f[inst->dest] = std::min(mips->f[inst->src1], mips->f[inst->src2]);
			}
			IR_NEXT;
		IR_CASE(FMax)
			if (my_isnan(mips->f[inst->src1]) || my_isnan(mips->f[inst->src2])) {
				// See interpreter for this logic: this is for vmax, we're comparing mantissa+exp.
				if (mips->fs[inst->src1] < 0 && mips->fs[inst->src2] < 0) {
					mips->fs[inst->dest] = std::min(mips->fs[inst->src1], mips->fs[inst->src2]);
				} else {
					mips->fs[inst->dest] = std::max(mips->fs[inst->src1], mips->fs[inst->src2]);
				}
			} else {
				mips->f[inst->dest] = std::max(mips->f[inst->src1], mips->f[inst->src2]);
			}
			IR_NEXT;

		IR_CASE(FMov)
			mips->f[inst->dest] = mips->f[inst->src1];
			IR_NEXT;
		IR_CASE(FAbs)
			mips->f[inst->dest] = fabsf(mips->f[inst->src1]);
			IR_NEXT;
		IR_CASE(FSqrt)
		{
			float src = mips->f[inst->src1];
			mips->f[inst->dest] = sqrtf(src);
			// A negative input gives a positive NaN, not the host's (cpu/fpu/roundmode).
			if (src < 0.0f) {
				mips->fi[inst->dest] = 0x7FC00000;
			}
			IR_NEXT;
		}
		IR_CASE(FNeg)
			mips->f[inst->dest] = -mips->f[inst->src1];
			IR_NEXT;
		IR_CASE(FSat0_1)
			// We have to do this carefully to handle NAN and -0.0f.
			mips->f[inst->dest] = vfpu_clamp(mips->f[inst->src1], 0.0f, 1.0f);
			IR_NEXT;
		IR_CASE(FSatMinus1_1)
			mips->f[inst->dest] = vfpu_clamp(mips->f[inst->src1], -1.0f, 1.0f);
			IR_NEXT;

		IR_CASE(FSign)
		{
			// Bitwise trickery. Denormals give zero, as on the hardware.
			u32 val;
			memcpy(&val, &mips->f[inst->src1], sizeof(u32));
			if ((val & 0x7F800000) == 0)
				mips->f[inst->dest] = 0.0f;
			else if ((val >> 31) == 0)
				mips->f[inst->dest] = 1.0f;
			else
				mips->f[inst->dest] = -1.0f;
			IR_NEXT;
		}

		IR_CASE(FpCondFromReg)
			// Note: the register is in src1, see the "_G" meta - the native backends read it there.
			mips->fpcond = mips->r[inst->src1];
			IR_NEXT;
		IR_CASE(FpCondToReg)
			mips->r[inst->dest] = mips->fpcond;
			IR_NEXT;
		IR_CASE(FpCtrlFromReg)
			mips->fcr31 = mips->r[inst->src1] & 0x0181FFFF;
			// Extract the new fpcond value.
			// TODO: Is it really helping us to keep it separate?
			mips->fpcond = (mips->fcr31 >> 23) & 1;
			IR_NEXT;
		IR_CASE(FpCtrlToReg)
			// Update the fpcond bit first.
			mips->fcr31 = (mips->fcr31 & ~(1 << 23)) | ((mips->fpcond & 1) << 23);
			mips->r[inst->dest] = mips->fcr31;
			IR_NEXT;
		IR_CASE(VfpuCtrlToReg)
			mips->r[inst->dest] = mips->vfpuCtrl[inst->src1];
			IR_NEXT;
		IR_CASE(FRound)
			mips->fs[inst->dest] = SaturatedFloatToInt(round_ieee_754(mips->f[inst->src1]));
			IR_NEXT;
		IR_CASE(FTrunc)
			mips->fs[inst->dest] = SaturatedFloatToInt(truncf(mips->f[inst->src1]));
			IR_NEXT;
		IR_CASE(FCeil)
			mips->fs[inst->dest] = SaturatedFloatToInt(ceilf(mips->f[inst->src1]));
			IR_NEXT;
		IR_CASE(FFloor)
			mips->fs[inst->dest] = SaturatedFloatToInt(floorf(mips->f[inst->src1]));
			IR_NEXT;
		IR_CASE(FCmp)
			switch (inst->dest) {
			case IRFpCompareMode::False:
				mips->fpcond = 0;
				break;
			case IRFpCompareMode::EitherUnordered:
			{
				float a = mips->f[inst->src1];
				float b = mips->f[inst->src2];
				mips->fpcond = !(a > b || a < b || a == b);
				break;
			}
			case IRFpCompareMode::EqualOrdered:
				mips->fpcond = mips->f[inst->src1] == mips->f[inst->src2];
				break;
			case IRFpCompareMode::EqualUnordered:
				mips->fpcond = mips->f[inst->src1] == mips->f[inst->src2] || my_isnan(mips->f[inst->src1]) || my_isnan(mips->f[inst->src2]);
				break;
			case IRFpCompareMode::LessEqualOrdered:
				mips->fpcond = mips->f[inst->src1] <= mips->f[inst->src2];
				break;
			case IRFpCompareMode::LessEqualUnordered:
				mips->fpcond = !(mips->f[inst->src1] > mips->f[inst->src2]);
				break;
			case IRFpCompareMode::LessOrdered:
				mips->fpcond = mips->f[inst->src1] < mips->f[inst->src2];
				break;
			case IRFpCompareMode::LessUnordered:
				mips->fpcond = !(mips->f[inst->src1] >= mips->f[inst->src2]);
				break;
			}
			IR_NEXT;

		IR_CASE(FCvtSW)
			mips->f[inst->dest] = (float)mips->fs[inst->src1];
			IR_NEXT;
		IR_CASE(FCvtWS)
		{
			float src = mips->f[inst->src1];
			// TODO: Inline assembly to use here would be better.
			switch (IRRoundMode(mips->fcr31 & 3)) {
			case IRRoundMode::RINT_0: mips->fs[inst->dest] = SaturatedFloatToInt(round_ieee_754(src)); break;
			case IRRoundMode::CAST_1: mips->fs[inst->dest] = SaturatedFloatToInt(truncf(src)); break;
			case IRRoundMode::CEIL_2: mips->fs[inst->dest] = SaturatedFloatToInt(ceilf(src)); break;
			case IRRoundMode::FLOOR_3: mips->fs[inst->dest] = SaturatedFloatToInt(floorf(src)); break;
			}
			IR_NEXT; //cvt.w.s
		}
		IR_CASE(FCvtScaledSW)
			mips->f[inst->dest] = (float)mips->fs[inst->src1] * (1.0f / (1UL << (inst->src2 & 0x1F)));
			IR_NEXT;
		IR_CASE(FCvtScaledWS)
		{
			float src = mips->f[inst->src1];
			if (my_isnan(src)) {
				// TODO: True for negatives too?
				mips->fs[inst->dest] = 2147483647L;
				IR_NEXT;
			}

			float mult = (float)(1UL << (inst->src2 & 0x1F));
			double sv = src * mult; // (float)0x7fffffff == (float)0x80000000
			// Cap/floor it to 0x7fffffff / 0x80000000
			if (sv > (double)0x7fffffff) {
				mips->fs[inst->dest] = 0x7fffffff;
			} else if (sv <= (double)(int)0x80000000) {
				mips->fs[inst->dest] = 0x80000000;
			} else {
				switch (IRRoundMode(inst->src2 >> 6)) {
				case IRRoundMode::RINT_0: mips->fs[inst->dest] = (int)round_ieee_754(sv); break;
				case IRRoundMode::CAST_1: mips->fs[inst->dest] = src >= 0 ? (int)floor(sv) : (int)ceil(sv); break;
				case IRRoundMode::CEIL_2: mips->fs[inst->dest] = (int)ceil(sv); break;
				case IRRoundMode::FLOOR_3: mips->fs[inst->dest] = (int)floor(sv); break;
				}
			}
			IR_NEXT;
		}

		IR_CASE(FMovFromGPR)
			memcpy(&mips->f[inst->dest], &mips->r[inst->src1], 4);
			IR_NEXT;
		IR_CASE(OptFCvtSWFromGPR)
			mips->f[inst->dest] = (float)(int)mips->r[inst->src1];
			IR_NEXT;
		IR_CASE(FMovToGPR)
			memcpy(&mips->r[inst->dest], &mips->f[inst->src1], 4);
			IR_NEXT;
		IR_CASE(OptFMovToGPRShr8)
		{
			u32 temp;
			memcpy(&temp, &mips->f[inst->src1], 4);
			mips->r[inst->dest] = temp >> 8;
			IR_NEXT;
		}

		IR_CASE(ExitToConst)
			return inst->constant;

		IR_CASE(ExitToReg)
			return mips->r[inst->src1];

		// The next instruction is the ExitToConst to take otherwise, never run itself.
		IR_CASE(OptExitToConstIfEqElse)
			return mips->r[inst->src1] == mips->r[inst->src2] ? inst->constant : inst[1].constant;
		IR_CASE(OptExitToConstIfNeqElse)
			return mips->r[inst->src1] != mips->r[inst->src2] ? inst->constant : inst[1].constant;
		IR_CASE(OptExitToConstIfGtZElse)
			return (s32)mips->r[inst->src1] > 0 ? inst->constant : inst[1].constant;
		IR_CASE(OptExitToConstIfGeZElse)
			return (s32)mips->r[inst->src1] >= 0 ? inst->constant : inst[1].constant;
		IR_CASE(OptExitToConstIfLtZElse)
			return (s32)mips->r[inst->src1] < 0 ? inst->constant : inst[1].constant;
		IR_CASE(OptExitToConstIfLeZElse)
			return (s32)mips->r[inst->src1] <= 0 ? inst->constant : inst[1].constant;
		IR_CASE(ExitToConstIfEq)
			if (mips->r[inst->src1] == mips->r[inst->src2])
				return inst->constant;
			IR_NEXT;
		IR_CASE(ExitToConstIfNeq)
			if (mips->r[inst->src1] != mips->r[inst->src2])
				return inst->constant;
			IR_NEXT;
		IR_CASE(ExitToConstIfGtZ)
			if ((s32)mips->r[inst->src1] > 0)
				return inst->constant;
			IR_NEXT;
		IR_CASE(ExitToConstIfGeZ)
			if ((s32)mips->r[inst->src1] >= 0)
				return inst->constant;
			IR_NEXT;
		IR_CASE(ExitToConstIfLtZ)
			if ((s32)mips->r[inst->src1] < 0)
				return inst->constant;
			IR_NEXT;
		IR_CASE(ExitToConstIfLeZ)
			if ((s32)mips->r[inst->src1] <= 0)
				return inst->constant;
			IR_NEXT;

		IR_CASE(Downcount)
			mips->downcount -= (int)inst->constant;
			IR_NEXT;

		IR_CASE(SetPC)
			mips->pc = mips->r[inst->src1];
			IR_NEXT;

		IR_CASE(SetPCConst)
			mips->pc = inst->constant;
			IR_NEXT;

		IR_CASE(Syscall)
			// IROp::SetPC was (hopefully) executed before.
		{
			// If we get here, the syscall is valid.
			MIPSOpcode op(inst->constant);
			CallSyscall(op);
			if (coreState != CORE_RUNNING_CPU) {
				CoreTiming::ForceCheck(mips);
			}
			IR_NEXT;
		}

		IR_CASE(SyscallUnresolved)
		{
			// If we get here, the syscall is invalid.
			u32 pc = inst->constant;
			CallSyscallUnresolvedAtPC(pc);
			if (coreState != CORE_RUNNING_CPU) {
				// hm, what's this for?
				CoreTiming::ForceCheck(mips);
			}
			IR_NEXT;
		}

		IR_CASE(ExitToPC)
			return mips->pc;

		IR_CASE(Interpret)  // SLOW fallback. Can be made faster. Ideally should be removed but may be useful for debugging.
		{
			MIPSOpcode op(inst->constant);
			MIPSInterpret(mips, op);
			IR_NEXT;
		}

		IR_CASE(CallReplacement)
		{
			int funcIndex = inst->constant;
			const ReplacementTableEntry *f = GetReplacementFunc(funcIndex);
			int cycles = f->replaceFunc();
			mips->r[inst->dest] = cycles < 0 ? -1 : 0;
			mips->downcount -= cycles < 0 ? -cycles : cycles;
			IR_NEXT;
		}

		IR_CASE(SetCtrlVFPU)
			mips->vfpuCtrl[inst->dest] = inst->constant;
			IR_NEXT;

		IR_CASE(SetCtrlVFPUReg)
			mips->vfpuCtrl[inst->dest] = mips->r[inst->src1];
			IR_NEXT;

		IR_CASE(SetCtrlVFPUFReg)
			memcpy(&mips->vfpuCtrl[inst->dest], &mips->f[inst->src1], 4);
			IR_NEXT;

		IR_CASE(ApplyRoundingMode)
			ApplyHostRoundingMode(mips);
			IR_NEXT;
		IR_CASE(RestoreRoundingMode)
			RestoreHostRoundingMode();
			IR_NEXT;
		IR_CASE(UpdateRoundingMode)
			// TODO: Implement
			IR_NEXT;

		IR_CASE(Break)
			Core_BreakException(mips->pc);
			return mips->pc + 4;

		IR_CASE(Breakpoint)
			if (IRRunBreakpoint(inst->constant)) {
				CoreTiming::ForceCheck(mips);
				return mips->pc;
			}
			IR_NEXT;

		IR_CASE(MemoryCheck)
			if (IRRunMemCheck(mips->pc + inst->dest, mips->r[inst->src1] + inst->constant)) {
				CoreTiming::ForceCheck(mips);
				return mips->pc;
			}
			IR_NEXT;

		IR_CASE(ValidateAddress8)
			if (RunValidateAddress<1>(mips->pc, mips->r[inst->src1] + inst->constant, inst->src2)) {
				CoreTiming::ForceCheck(mips);
				return mips->pc;
			}
			IR_NEXT;
		IR_CASE(ValidateAddress16)
			if (RunValidateAddress<2>(mips->pc, mips->r[inst->src1] + inst->constant, inst->src2)) {
				CoreTiming::ForceCheck(mips);
				return mips->pc;
			}
			IR_NEXT;
		IR_CASE(ValidateAddress32)
			if (RunValidateAddress<4>(mips->pc, mips->r[inst->src1] + inst->constant, inst->src2)) {
				CoreTiming::ForceCheck(mips);
				return mips->pc;
			}
			IR_NEXT;
		IR_CASE(ValidateAddress128)
			if (RunValidateAddress<16>(mips->pc, mips->r[inst->src1] + inst->constant, inst->src2)) {
				CoreTiming::ForceCheck(mips);
				return mips->pc;
			}
			IR_NEXT;
		IR_CASE(LogIRBlock)
			if (mipsTracer.tracing_enabled) {
				mipsTracer.executed_blocks.push_back(inst->constant);
			}
			IR_NEXT;

		IR_CASE(Nop)  // Unused, add a break if we start using it to avoid UNREACHABLE.
		IR_CASE(Bad)
		default:
			// Unimplemented IR op. Bad. We define it as unreachable so the compiler can optimize better (remove the range check).
			UNREACHABLE();
			IR_NEXT;
		}
	}

	// We should not reach here anymore.
	return 0;
}

#undef IR_NEXT
#undef IR_CASE
#undef IR_CHECK_ZERO_REG

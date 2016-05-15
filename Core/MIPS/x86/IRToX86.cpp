#include "Core/MIPS/x86/IRToX86.h"

namespace MIPSComp {

// Initial attempt at converting IR directly to x86.
// This is intended to be an easy way to benefit from the IR with the current infrastructure.
// Later tries may go across multiple blocks and a different representation.

struct GPRMapping {
	Gen::OpArg dest;
	Gen::OpArg src1;
	Gen::OpArg src2;
};

struct FPRMapping {
	Gen::OpArg dest;
	Gen::OpArg src1;
	Gen::OpArg src2;
};


class GreedyRegallocGPR {
public:
	GPRMapping Map(IRInst inst, const IRMeta &meta);

private:
	
};


GPRMapping GreedyRegallocGPR::Map(IRInst inst, const IRMeta &meta) {
	GPRMapping mapping;
	if (meta.types[0] == 'G') {

	}
	// etc..
	return mapping;
}

// Every 4 registers can also be mapped into an SSE register.
// When changing from single to vec4 mapping, we'll just flush, for now.
class GreedyRegallocFPR {
public:
	FPRMapping Map(IRInst inst, const IRMeta &meta);
};

FPRMapping GreedyRegallocFPR::Map(IRInst inst, const IRMeta &meta) {
	FPRMapping mapping;

	return mapping;
}


// This requires that ThreeOpToTwoOp has been run as the last pass.
void IRToX86::ConvertIRToNative(const IRInst *instructions, int count, const u32 *constants) {
	// Set up regcaches
	using namespace Gen;

	GreedyRegallocGPR gprAlloc;
	GreedyRegallocFPR fprAlloc;

	// Loop through all the instructions, emitting code as we go.
	// Note that we do need to implement them all - fallbacks are not permitted.
	for (int i = 0; i < count; i++) {
		const IRInst *inst = &instructions[i];
		const IRMeta &meta = *GetIRMeta(inst->op);
		GPRMapping gpr = gprAlloc.Map(*inst, meta);
		FPRMapping fpr = fprAlloc.Map(*inst, meta);

		bool symmetric = false;
		switch (inst->op) {
		case IROp::Nop:
			_assert_(false);
			break;

			// Output-only
		case IROp::SetConst:
			code_->MOV(32, gpr.dest, Imm32(constants[inst->src1]));
			break;
		case IROp::SetConstF:
			code_->MOV(32, gpr.dest, Imm32(constants[inst->src1]));
			break;

			// Add gets to be special cased because we have LEA.
		case IROp::Add:
			if (gpr.dest.IsSimpleReg() && gpr.src1.IsSimpleReg() && gpr.src2.IsSimpleReg()) {
				code_->LEA(32, gpr.dest.GetSimpleReg(), MRegSum(gpr.src1.GetSimpleReg(), gpr.src2.GetSimpleReg()));
				break;
			}
			// Else fall through.
			// 3-op arithmetic that directly corresponds to x86
			// (often 2-op in practice if src1 == dst). x86 only does 2-op though so some of these will need splitting.
		case IROp::Sub:
		case IROp::And:
		case IROp::Or:
		case IROp::Xor:
			if (inst->src1 != inst->dest) {
				code_->MOV(32, gpr.dest, gpr.src1);
			}
			// Emit directly
			switch (inst->op) {
			case IROp::Add: code_->ADD(32, gpr.dest, gpr.src2); break;
			case IROp::Sub: code_->SUB(32, gpr.dest, gpr.src2); break;
			case IROp::And: code_->AND(32, gpr.dest, gpr.src2); break;
			case IROp::Or: code_->OR(32, gpr.dest, gpr.src2); break;
			case IROp::Xor: code_->XOR(32, gpr.dest, gpr.src2); break;
			}
			break;

			// Variable shifts.
		case IROp::Shl:
		case IROp::Shr:
		case IROp::Sar:
		case IROp::Ror:

		case IROp::Slt:
		case IROp::SltU:
		case IROp::MovZ:
		case IROp::MovNZ:
		case IROp::Max:
		case IROp::Min:
			break;

			// 2-op arithmetic with constant
		case IROp::AddConst:
		case IROp::SubConst:
		case IROp::AndConst:
		case IROp::OrConst:
		case IROp::XorConst:
		case IROp::SltConst:
		case IROp::SltUConst:

			// 2-op arithmetic with immediate
		case IROp::ShlImm:
		case IROp::ShrImm:
		case IROp::SarImm:
		case IROp::RorImm:

			// 2-op arithmetic
		case IROp::Mov:
			code_->MOV(32, gpr.dest, gpr.src1);
			break;

		case IROp::Neg:
		case IROp::Not:
		case IROp::Ext8to32:
		case IROp::Ext16to32:
		case IROp::ReverseBits:
		case IROp::BSwap16:
		case IROp::BSwap32:
		case IROp::Clz:
			if (inst->dest != inst->src1) {
				code_->NEG(32, gpr.dest); break;
			}
			break;
				// Multiplier control
		case IROp::MtLo:
		case IROp::MtHi:
		case IROp::MfLo:
		case IROp::MfHi:
		case IROp::Mult:
		case IROp::MultU:
		case IROp::Madd:
		case IROp::MaddU:
		case IROp::Msub:
		case IROp::MsubU:
		case IROp::Div:
		case IROp::DivU:

			// Memory access
		case IROp::Load8:
		case IROp::Load8Ext:
		case IROp::Load16:
		case IROp::Load16Ext:
		case IROp::Load32:
		case IROp::LoadFloat:
		case IROp::Store8:
		case IROp::Store16:
		case IROp::Store32:
		case IROp::StoreFloat:
		case IROp::LoadVec4:
		case IROp::StoreVec4:

			// Output-only SIMD functions
		case IROp::Vec4Init:
		case IROp::Vec4Shuffle:

			// 2-op SIMD functions
		case IROp::Vec4Mov:
			code_->MOVAPS(fpr.dest.GetSimpleReg(), fpr.src1);
			break;
		case IROp::Vec4Neg:
		case IROp::Vec4Abs:
			break;
		case IROp::Vec4ClampToZero:
			code_->PXOR(XMM0, R(XMM0));
			code_->PMAXSW(XMM0, fpr.src1);
			code_->MOVAPD(fpr.dest, XMM0);
			break;
		case IROp::Vec4DuplicateUpperBitsAndShift1:
		case IROp::Vec2ClampToZero:

			// 3-op SIMD functions
		case IROp::Vec4Add:
		case IROp::Vec4Sub:
		case IROp::Vec4Mul:
		case IROp::Vec4Div:

		case IROp::Vec4Scale:
		case IROp::Vec4Dot:

			// Pack-unpack
		case IROp::Vec2Unpack16To31:
		case IROp::Vec2Unpack16To32:
		case IROp::Vec4Unpack8To32:
		case IROp::Vec2Pack32To16:
		case IROp::Vec2Pack31To16:
		case IROp::Vec4Pack32To8:
		case IROp::Vec4Pack31To8:

		case IROp::FCmpVfpuBit:
		case IROp::FCmpVfpuAggregate:
		case IROp::FCmovVfpuCC:

			// Trancendental functions (non-simd)
		case IROp::FSin:
		case IROp::FCos:
		case IROp::FRSqrt:
		case IROp::FRecip:
		case IROp::FAsin:

			// 3-Op FP
		case IROp::FAdd:
		case IROp::FSub:
		case IROp::FMul:
		case IROp::FDiv:
		case IROp::FMin:
		case IROp::FMax:

			// 2-Op FP
		case IROp::FMov:
		case IROp::FAbs:
		case IROp::FSqrt:
		case IROp::FNeg:
		case IROp::FSat0_1:
		case IROp::FSatMinus1_1:
		case IROp::FSign:
		case IROp::FCeil:
		case IROp::FFloor:
		case IROp::FCmp:
		case IROp::FCvtSW:
		case IROp::FCvtWS:
		case IROp::FRound:
		case IROp::FTrunc:

			// Cross moves
		case IROp::FMovFromGPR:
		case IROp::FMovToGPR:
		case IROp::FpCondToReg:
		case IROp::VfpuCtrlToReg:

			// VFPU flag/control
		case IROp::SetCtrlVFPU:
		case IROp::SetCtrlVFPUReg:
		case IROp::SetCtrlVFPUFReg:
		case IROp::ZeroFpCond:

			// Block Exits
		case IROp::ExitToConst:
		case IROp::ExitToReg:
		case IROp::ExitToConstIfEq:
		case IROp::ExitToConstIfNeq:
		case IROp::ExitToConstIfGtZ:
		case IROp::ExitToConstIfGeZ:
		case IROp::ExitToConstIfLtZ:
		case IROp::ExitToConstIfLeZ:
		case IROp::ExitToPC:

			// Utilities
		case IROp::Downcount:
		case IROp::SetPC:
		case IROp::SetPCConst:
		case IROp::Syscall:
		case IROp::Interpret:  // SLOW fallback. Can be made faster.
		case IROp::CallReplacement:
		case IROp::Break:
		default:
			break;
			}
		}
	}


}  // namespace
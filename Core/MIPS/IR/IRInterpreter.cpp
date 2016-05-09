#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/MIPS/MIPSTables.h"

#include "math/math_util.h"
#include "Common/CommonTypes.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRInterpreter.h"

u32 IRInterpret(MIPSState *mips, const IRInst *inst, const u32 *constPool, int count) {
	const IRInst *end = inst + count;
	while (inst != end) {
		switch (inst->op) {
		case IROp::SetConst:
			mips->r[inst->dest] = constPool[inst->src1];
			break;
		case IROp::SetConstF:
			memcpy(&mips->f[inst->dest], &constPool[inst->src1], 4);
			break;
		case IROp::SetConstV:
			memcpy(&mips->v[voffset[inst->dest]], &constPool[inst->src1], 4);
			break;
		case IROp::Add:
			mips->r[inst->dest] = mips->r[inst->src1] + mips->r[inst->src2];
			break;
		case IROp::Sub:
			mips->r[inst->dest] = mips->r[inst->src1] - mips->r[inst->src2];
			break;
		case IROp::And:
			mips->r[inst->dest] = mips->r[inst->src1] & mips->r[inst->src2];
			break;
		case IROp::Or:
			mips->r[inst->dest] = mips->r[inst->src1] | mips->r[inst->src2];
			break;
		case IROp::Xor:
			mips->r[inst->dest] = mips->r[inst->src1] ^ mips->r[inst->src2];
			break;
		case IROp::Mov:
			mips->r[inst->dest] = mips->r[inst->src1];
			break;
		case IROp::AddConst:
			mips->r[inst->dest] = mips->r[inst->src1] + constPool[inst->src2];
			break;
		case IROp::SubConst:
			mips->r[inst->dest] = mips->r[inst->src1] - constPool[inst->src2];
			break;
		case IROp::AndConst:
			mips->r[inst->dest] = mips->r[inst->src1] & constPool[inst->src2];
			break;
		case IROp::OrConst:
			mips->r[inst->dest] = mips->r[inst->src1] | constPool[inst->src2];
			break;
		case IROp::XorConst:
			mips->r[inst->dest] = mips->r[inst->src1] ^ constPool[inst->src2];
			break;
		case IROp::Neg:
			mips->r[inst->dest] = -(s32)mips->r[inst->src1];
			break;
		case IROp::Not:
			mips->r[inst->dest] = ~mips->r[inst->src1];
			break;
		case IROp::Ext8to32:
			mips->r[inst->dest] = (s32)(s8)mips->r[inst->src1];
			break;
		case IROp::Ext16to32:
			mips->r[inst->dest] = (s32)(s16)mips->r[inst->src1];
			break;

		case IROp::Load8:
			mips->r[inst->dest] = Memory::ReadUnchecked_U8(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load8Ext:
			mips->r[inst->dest] = (s32)(s8)Memory::ReadUnchecked_U8(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load16:
			mips->r[inst->dest] = Memory::ReadUnchecked_U16(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load16Ext:
			mips->r[inst->dest] = (s32)(s16)Memory::ReadUnchecked_U16(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Load32:
			mips->r[inst->dest] = Memory::ReadUnchecked_U32(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::LoadFloat:
			mips->f[inst->dest] = Memory::ReadUnchecked_Float(mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::LoadFloatV:
			mips->v[voffset[inst->dest]] = Memory::ReadUnchecked_Float(mips->r[inst->src1] + constPool[inst->src2]);
			break;

		case IROp::Store8:
			Memory::WriteUnchecked_U8(mips->r[inst->src3], mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Store16:
			Memory::WriteUnchecked_U16(mips->r[inst->src3], mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::Store32:
			Memory::WriteUnchecked_U32(mips->r[inst->src3], mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::StoreFloat:
			Memory::WriteUnchecked_Float(mips->f[inst->src3], mips->r[inst->src1] + constPool[inst->src2]);
			break;
		case IROp::StoreFloatV:
			Memory::WriteUnchecked_Float(mips->v[voffset[inst->src3]], mips->r[inst->src1] + constPool[inst->src2]);
			break;

		case IROp::ShlImm:
			mips->r[inst->dest] = mips->r[inst->src1] << (int)inst->src2;
			break;
		case IROp::ShrImm:
			mips->r[inst->dest] = mips->r[inst->src1] >> (int)inst->src2;
			break;
		case IROp::SarImm:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] >> (int)inst->src2;
			break;
		case IROp::RorImm:
		{
			u32 x = mips->r[inst->src1];
			int sa = inst->src2;
			mips->r[inst->dest] = (x >> sa) | (x << (32 - sa));
		}
		break;

		case IROp::Shl:
			mips->r[inst->dest] = mips->r[inst->src1] << (mips->r[inst->src2] & 31);
			break;
		case IROp::Shr:
			mips->r[inst->dest] = mips->r[inst->src1] >> (mips->r[inst->src2] & 31);
			break;
		case IROp::Sar:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] >> (mips->r[inst->src2] & 31);
			break;
		case IROp::Ror:
		{
			u32 x = mips->r[inst->src1];
			int sa = mips->r[inst->src2] & 31;
			mips->r[inst->dest] = (x >> sa) | (x << (32 - sa));
		}
		break;

		case IROp::Clz:
		{
			int x = 31;
			int count = 0;
			int value = mips->r[inst->src1];
			while (x >= 0 && !(value & (1 << x))) {
				count++;
				x--;
			}
			mips->r[inst->dest] = count;
			break;
		}

		case IROp::Slt:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)mips->r[inst->src2];
			break;

		case IROp::SltU:
			mips->r[inst->dest] = mips->r[inst->src1] < mips->r[inst->src2];
			break;

		case IROp::SltConst:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)constPool[inst->src2];
			break;

		case IROp::SltUConst:
			mips->r[inst->dest] = mips->r[inst->src1] < constPool[inst->src2];
			break;

		case IROp::MovZ:
			if (mips->r[inst->src1] == 0)
				mips->r[inst->dest] = mips->r[inst->src2];
			break;
		case IROp::MovNZ:
			if (mips->r[inst->src1] != 0)
				mips->r[inst->dest] = mips->r[inst->src2];
			break;

		case IROp::Max:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] > (s32)mips->r[inst->src2] ? mips->r[inst->src1] : mips->r[inst->src2];
			break;
		case IROp::Min:
			mips->r[inst->dest] = (s32)mips->r[inst->src1] < (s32)mips->r[inst->src2] ? mips->r[inst->src1] : mips->r[inst->src2];
			break;

		case IROp::MtLo:
			mips->lo = mips->r[inst->src1];
			break;
		case IROp::MtHi:
			mips->hi = mips->r[inst->src1];
			break;
		case IROp::MfLo:
			mips->r[inst->dest] = mips->lo;
			break;
		case IROp::MfHi:
			mips->r[inst->dest] = mips->hi;
			break;

		case IROp::Mult:
		{
			s64 result = (s64)(s32)mips->r[inst->src1] * (s64)(s32)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			break;
		}
		case IROp::MultU:
		{
			u64 result = (u64)mips->r[inst->src1] * (u64)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			break;
		}

		case IROp::BSwap16:
		{
			u32 x = mips->r[inst->src1];
			mips->r[inst->dest] = ((x & 0xFF00FF00) >> 8) | ((x & 0x00FF00FF) << 8);
			break;
		}
		case IROp::BSwap32:
		{
			u32 x = mips->r[inst->src1];
			mips->r[inst->dest] = ((x & 0xFF000000) >> 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8) | ((x & 0x000000FF) << 24);
			break;
		}

		case IROp::FAdd:
			mips->f[inst->dest] = mips->f[inst->src1] + mips->f[inst->src2];
			break;
		case IROp::FSub:
			mips->f[inst->dest] = mips->f[inst->src1] - mips->f[inst->src2];
			break;
		case IROp::FMul:
			mips->f[inst->dest] = mips->f[inst->src1] * mips->f[inst->src2];
			break;
		case IROp::FDiv:
			mips->f[inst->dest] = mips->f[inst->src1] / mips->f[inst->src2];
			break;

		case IROp::FMov:
			mips->f[inst->dest] = mips->f[inst->src1];
			break;
		case IROp::FAbs:
			mips->f[inst->dest] = fabsf(mips->f[inst->src1]);
			break;
		case IROp::FSqrt:
			mips->f[inst->dest] = sqrtf(mips->f[inst->src1]);
			break;
		case IROp::FNeg:
			mips->f[inst->dest] = -mips->f[inst->src1];
			break;
		case IROp::FpCondToReg:
			mips->r[inst->dest] = mips->fpcond;
			break;
		case IROp::VfpuCtrlToReg:
			mips->r[inst->dest] = mips->vfpuCtrl[inst->src1];
			break;
		case IROp::FRound:
			mips->fs[inst->dest] = (int)floorf(mips->f[inst->src1] + 0.5f);
			break;
		case IROp::FTrunc:
		{
			float src = mips->f[inst->src1];
			if (src >= 0.0f) {
				mips->fs[inst->dest] = (int)floorf(src);
				// Overflow, but it was positive.
				if (mips->fs[inst->dest] == -2147483648LL) {
					mips->fs[inst->dest] = 2147483647LL;
				}
			} else {
				// Overflow happens to be the right value anyway.
				mips->fs[inst->dest] = (int)ceilf(src);
			}
			break;
		}
		case IROp::FCeil:
			mips->fs[inst->dest] = (int)ceilf(mips->f[inst->src1]);
			break;
		case IROp::FFloor:
			mips->fs[inst->dest] = (int)floorf(mips->f[inst->src1]);
			break;

		case IROp::FCvtSW:
			mips->f[inst->dest] = (float)mips->fs[inst->src1];
			break;
		case IROp::FCvtWS:
		{
			float src = mips->f[inst->src1];
			if (my_isnanorinf(src))
			{
				mips->fs[inst->dest] = my_isinf(src) && src < 0.0f ? -2147483648LL : 2147483647LL;
				break;
			}
			switch (mips->fcr31 & 3)
			{
			case 0: mips->fs[inst->dest] = (int)round_ieee_754(src); break;  // RINT_0
			case 1: mips->fs[inst->dest] = (int)src; break;  // CAST_1
			case 2: mips->fs[inst->dest] = (int)ceilf(src); break;  // CEIL_2
			case 3: mips->fs[inst->dest] = (int)floorf(src); break;  // FLOOR_3
			}
			break; //cvt.w.s
		}

		case IROp::ZeroFpCond:
			mips->fpcond = 0;
			break;

		case IROp::FMovFromGPR:
			memcpy(&mips->f[inst->dest], &mips->r[inst->src1], 4);
			break;
		case IROp::FMovToGPR:
			memcpy(&mips->r[inst->dest], &mips->f[inst->src1], 4);
			break;

		case IROp::VMovFromGPR:
			memcpy(&mips->v[voffset[inst->dest]], &mips->r[inst->src1], 4);
			break;
		case IROp::VMovToGPR:
			memcpy(&mips->r[inst->dest], &mips->v[voffset[inst->src1]], 4);
			break;

		case IROp::ExitToConst:
			return constPool[inst->dest];

		case IROp::ExitToReg:
			return mips->r[inst->dest];

		case IROp::ExitToConstIfEq:
			if (mips->r[inst->src1] == mips->r[inst->src2])
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfNeq:
			if (mips->r[inst->src1] != mips->r[inst->src2])
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfGtZ:
			if ((s32)mips->r[inst->src1] > 0)
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfGeZ:
			if ((s32)mips->r[inst->src1] >= 0)
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfLtZ:
			if ((s32)mips->r[inst->src1] < 0)
				return constPool[inst->dest];
			break;
		case IROp::ExitToConstIfLeZ:
			if ((s32)mips->r[inst->src1] <= 0)
				return constPool[inst->dest];
			break;

		case IROp::Downcount:
			mips->downcount -= (inst->src1) | ((inst->src2) << 8);
			break;

		case IROp::SetPC:
			mips->pc = mips->r[inst->src1];
			break;

		case IROp::SetPCConst:
			mips->pc = constPool[inst->src1];
			break;

		case IROp::Syscall:
			// SetPC was executed before.
		{
			MIPSOpcode op(constPool[inst->src1]);
			CallSyscall(op);
			return mips->pc;
		}

		case IROp::Interpret:  // SLOW fallback. Can be made faster.
		{
			MIPSOpcode op(constPool[inst->src1]);
			MIPSInterpret(op);
			break;
		}

		case IROp::CallReplacement:
		{
			int funcIndex = constPool[inst->src1];
			const ReplacementTableEntry *f = GetReplacementFunc(funcIndex);
			int cycles = f->replaceFunc();
			mips->downcount -= cycles;
			break;
		}

		case IROp::Break:
			Crash();
			break;

		case IROp::SetCtrlVFPU:
			mips->vfpuCtrl[inst->dest] = constPool[inst->src1];
			break;

		default:
			Crash();
		}
#ifdef _DEBUG
		if (mips->r[0] != 0)
			Crash();
#endif
		inst++;
	}

	// If we got here, the block was badly constructed.
	Crash();
	return 0;
}

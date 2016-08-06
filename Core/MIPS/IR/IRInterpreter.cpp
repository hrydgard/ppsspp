#include <algorithm>
#include <cmath>

#include "math/math_util.h"
#include "Common/Common.h"

#ifdef _M_SSE
#include <xmmintrin.h>
#endif

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/IR/IRInst.h"
#include "Core/MIPS/IR/IRInterpreter.h"
#include "Core/System.h"

alignas(16) float vec4InitValues[8][4] = {
	{ 0.0f, 0.0f, 0.0f, 0.0f },
	{ 1.0f, 1.0f, 1.0f, 1.0f },
	{ -1.0f, -1.0f, -1.0f, -1.0f },
	{ 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 1.0f },
};

u32 RunBreakpoint(u32 pc) {
	// Should we skip this breakpoint?
	if (CBreakPoints::CheckSkipFirst() == pc)
		return 0;

	CBreakPoints::ExecBreakPoint(currentMIPS->pc);
	return coreState != CORE_RUNNING ? 1 : 0;
}

u32 RunMemCheck(u32 pc, u32 addr) {
	// Should we skip this breakpoint?
	if (CBreakPoints::CheckSkipFirst() == pc)
		return 0;

	CBreakPoints::ExecOpMemCheck(addr, pc);
	return coreState != CORE_RUNNING ? 1 : 0;
}

u32 IRInterpret(MIPSState *mips, const IRInst *inst, const u32 *constPool, int count) {
	const IRInst *end = inst + count;
	while (inst != end) {
		switch (inst->op) {
		case IROp::Nop:
			_assert_(false);
			break;
		case IROp::SetConst:
			mips->r[inst->dest] = constPool[inst->src1];
			break;
		case IROp::SetConstF:
			memcpy(&mips->f[inst->dest], &constPool[inst->src1], 4);
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
		case IROp::ReverseBits:
			mips->r[inst->dest] = ReverseBits32(mips->r[inst->src1]);
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

		case IROp::LoadVec4:
		{
			u32 base = mips->r[inst->src1] + constPool[inst->src2];
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_load_ps((const float *)Memory::GetPointerUnchecked(base)));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = Memory::ReadUnchecked_Float(base + 4 * i);
#endif
			break;
		}
		case IROp::StoreVec4:
		{
			u32 base = mips->r[inst->src1] + constPool[inst->src2];
#if defined(_M_SSE)
			_mm_store_ps((float *)Memory::GetPointerUnchecked(base), _mm_load_ps(&mips->f[inst->dest]));
#else
			for (int i = 0; i < 4; i++)
				Memory::WriteUnchecked_Float(mips->f[inst->dest + i], base + 4 * i);
#endif
			break;
		}

		case IROp::Vec4Init:
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_load_ps(vec4InitValues[inst->src1]));
#else
			memcpy(&mips->f[inst->dest], vec4InitValues[inst->src1], 4 * sizeof(float));
#endif
			break;

		case IROp::Vec4Shuffle:
		{
			// Can't use the SSE shuffle here because it takes an immediate.
			// Backends with SSE support could use that though.
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + ((inst->src2 >> (i * 2)) & 3)];
			break;
		}

		case IROp::Vec4Mov:
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_load_ps(&mips->f[inst->src1]));
#else
			memcpy(&mips->f[inst->dest], &mips->f[inst->src1], 4 * sizeof(float));
#endif
			break;

		case IROp::Vec4Add:
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_add_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] + mips->f[inst->src2 + i];
#endif
			break;

		case IROp::Vec4Sub:
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_sub_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] - mips->f[inst->src2 + i];
#endif
			break;

		case IROp::Vec4Mul:
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_mul_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] * mips->f[inst->src2 + i];
#endif
			break;

		case IROp::Vec4Div:
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_div_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_load_ps(&mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] / mips->f[inst->src2 + i];
#endif
			break;

		case IROp::Vec4Scale:
#if defined(_M_SSE)
			_mm_store_ps(&mips->f[inst->dest], _mm_mul_ps(_mm_load_ps(&mips->f[inst->src1]), _mm_set1_ps(mips->f[inst->src2])));
#else
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = mips->f[inst->src1 + i] * mips->f[inst->src2];
#endif
			break;

		case IROp::Vec4Neg:
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = -mips->f[inst->src1 + i];
			break;

		case IROp::Vec4Abs:
			for (int i = 0; i < 4; i++)
				mips->f[inst->dest + i] = fabsf(mips->f[inst->src1 + i]);
			break;

		case IROp::Vec2Unpack16To31:
			mips->fi[inst->dest] = (mips->fi[inst->src1] << 16) >> 1;
			mips->fi[inst->dest + 1] = (mips->fi[inst->src1] & 0xFFFF0000) >> 1;
			break;

		case IROp::Vec2Unpack16To32:
			mips->fi[inst->dest] = (mips->fi[inst->src1] << 16);
			mips->fi[inst->dest + 1] = (mips->fi[inst->src1] & 0xFFFF0000);
			break;

		case IROp::Vec4Unpack8To32:
			mips->fi[inst->dest] = (mips->fi[inst->src1] << 24);
			mips->fi[inst->dest + 1] = (mips->fi[inst->src1] << 16) & 0xFF000000;
			mips->fi[inst->dest + 2] = (mips->fi[inst->src1] << 8) & 0xFF000000;
			mips->fi[inst->dest + 3] = (mips->fi[inst->src1]) & 0xFF000000;
			break;

		case IROp::Vec2Pack32To16:
		{
			u32 val = mips->fi[inst->src1] >> 16;
			mips->fi[inst->dest] = (mips->fi[inst->src1 + 1] & 0xFFFF0000) | val;
			break;
		}

		case IROp::Vec2Pack31To16:
		{
			u32 val = (mips->fi[inst->src1] >> 15) & 0xFFFF;
			val |= (mips->fi[inst->src1 + 1] << 1) & 0xFFFF0000;
			mips->fi[inst->dest] = val;
			break;
		}

		case IROp::Vec4Pack32To8:
		{
			u32 val = mips->fi[inst->src1] >> 24;
			val |= (mips->fi[inst->src1 + 1] >> 16) & 0xFF00;
			val |= (mips->fi[inst->src1 + 2] >> 8) & 0xFF0000;
			val |= (mips->fi[inst->src1 + 3]) & 0xFF000000;
			mips->fi[inst->dest] = val;
			break;
		}

		case IROp::Vec4Pack31To8:
		{
			u32 val = (mips->fi[inst->src1] >> 23) & 0xFF;
			val |= (mips->fi[inst->src1 + 1] >> 15) & 0xFF00;
			val |= (mips->fi[inst->src1 + 2] >> 7) & 0xFF0000;
			val |= (mips->fi[inst->src1 + 3] << 1) & 0xFF000000;
			mips->fi[inst->dest] = val;
			break;
		}

		case IROp::Vec2ClampToZero:
		{
			for (int i = 0; i < 2; i++) {
				u32 val = mips->fi[inst->src1 + i];
				mips->fi[inst->dest + i] = (int)val >= 0 ? val : 0;
			}
			break;
		}

		case IROp::Vec4ClampToZero:
		{
			for (int i = 0; i < 4; i++) {
				u32 val = mips->fi[inst->src1 + i];
				mips->fi[inst->dest + i] = (int)val >= 0 ? val : 0;
			}
			break;
		}

		case IROp::Vec4DuplicateUpperBitsAndShift1:
			for (int i = 0; i < 4; i++) {
				u32 val = mips->fi[inst->src1 + i];
				val = val | (val >> 8);
				val = val | (val >> 16);
				val >>= 1;
				mips->fi[inst->dest + i] = val;
			}
			break;

		case IROp::FCmpVfpuBit:
		{
			int op = inst->dest & 0xF;
			int bit = inst->dest >> 4;
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
		}
			break;

		case IROp::FCmpVfpuAggregate:
		{
			u32 mask = inst->dest;
			u32 cc = mips->vfpuCtrl[VFPU_CTRL_CC];
			int a = (cc & mask) ? 0x10 : 0x00;
			int b = (cc & mask) == mask ? 0x20 : 0x00;
			mips->vfpuCtrl[VFPU_CTRL_CC] = (cc & ~0x30) | a | b;
		}
			break;

		case IROp::FCmovVfpuCC:
			if (((mips->vfpuCtrl[VFPU_CTRL_CC] >> (inst->src2 & 0xf)) & 1) == ((u32)inst->src2 >> 7)) {
				mips->f[inst->dest] = mips->f[inst->src1];
			}
			break;

		// Not quickly implementable on all platforms, unfortunately.
		case IROp::Vec4Dot:
		{
			float dot = mips->f[inst->src1] * mips->f[inst->src2];
			for (int i = 1; i < 4; i++)
				dot += mips->f[inst->src1 + i] * mips->f[inst->src2 + i];
			mips->f[inst->dest] = dot;
			break;
		}

		case IROp::FSin:
			mips->f[inst->dest] = vfpu_sin(mips->f[inst->src1]);
			break;
		case IROp::FCos:
			mips->f[inst->dest] = vfpu_cos(mips->f[inst->src1]);
			break;
		case IROp::FRSqrt:
			mips->f[inst->dest] = 1.0f / sqrtf(mips->f[inst->src1]);
			break;
		case IROp::FRecip:
			mips->f[inst->dest] = 1.0f / mips->f[inst->src1];
			break;
		case IROp::FAsin:
			mips->f[inst->dest] = vfpu_asin(mips->f[inst->src1]);
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
		case IROp::Madd:
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result += (s64)(s32)mips->r[inst->src1] * (s64)(s32)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			break;
		}
		case IROp::MaddU:
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result += (u64)mips->r[inst->src1] * (u64)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			break;
		}
		case IROp::Msub:
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result -= (s64)(s32)mips->r[inst->src1] * (s64)(s32)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			break;
		}
		case IROp::MsubU:
		{
			s64 result;
			memcpy(&result, &mips->lo, 8);
			result -= (u64)mips->r[inst->src1] * (u64)mips->r[inst->src2];
			memcpy(&mips->lo, &result, 8);
			break;
		}

		case IROp::Div:
		{
			s32 numerator = (s32)mips->r[inst->src1];
			s32 denominator = (s32)mips->r[inst->src2];
			if (numerator == (s32)0x80000000 && denominator == -1) {
				mips->lo = 0x80000000;
				mips->hi = -1;
			} else if (denominator != 0) {
				mips->lo = (u32)(numerator / denominator);
				mips->hi = (u32)(numerator % denominator);
			} else {
				mips->lo = numerator < 0 ? 1 : -1;
				mips->hi = numerator;
			}
			break;
		}
		case IROp::DivU:
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
		case IROp::FMin:
			mips->f[inst->dest] = std::min(mips->f[inst->src1], mips->f[inst->src2]);
			break;
		case IROp::FMax:
			mips->f[inst->dest] = std::max(mips->f[inst->src1], mips->f[inst->src2]);
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
		case IROp::FSat0_1:
			// We have to do this carefully to handle NAN and -0.0f.
			mips->f[inst->dest] = vfpu_clamp(mips->f[inst->src1], 0.0f, 1.0f);
			break;
		case IROp::FSatMinus1_1:
			mips->f[inst->dest] = vfpu_clamp(mips->f[inst->src1], -1.0f, 1.0f);
			break;

		// Bitwise trickery
		case IROp::FSign:
		{
			u32 val;
			memcpy(&val, &mips->f[inst->src1], sizeof(u32));
			if (val == 0 || val == 0x80000000)
				mips->f[inst->dest] = 0.0f;
			else if ((val >> 31) == 0)
				mips->f[inst->dest] = 1.0f;
			else
				mips->f[inst->dest] = -1.0f;
		}

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
		case IROp::FCmp:
			switch (inst->dest) {
			case IRFpCompareMode::False:
				mips->fpcond = 0;
				break;
			case IRFpCompareMode::EqualOrdered:
			case IRFpCompareMode::EqualUnordered:
				mips->fpcond = mips->f[inst->src1] == mips->f[inst->src2];
				break;
			case IRFpCompareMode::LessEqualOrdered:
			case IRFpCompareMode::LessEqualUnordered:
				mips->fpcond = mips->f[inst->src1] <= mips->f[inst->src2];
				break;
			case IRFpCompareMode::LessOrdered:
			case IRFpCompareMode::LessUnordered:
				mips->fpcond = mips->f[inst->src1] < mips->f[inst->src2];
				break;
			}
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

		case IROp::ExitToConst:
			return constPool[inst->dest];

		case IROp::ExitToReg:
			return mips->r[inst->src1];

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
			if (coreState != CORE_RUNNING)
				CoreTiming::ForceCheck();
			break;
		}

		case IROp::ExitToPC:
			return mips->pc;

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

		case IROp::SetCtrlVFPUReg:
			mips->vfpuCtrl[inst->dest] = mips->r[inst->src1];
			break;

		case IROp::SetCtrlVFPUFReg:
			memcpy(&mips->vfpuCtrl[inst->dest], &mips->f[inst->src1], 4);
			break;

		case IROp::Breakpoint:
			if (RunBreakpoint(mips->pc)) {
				CoreTiming::ForceCheck();
				return mips->pc;
			}
			break;

		case IROp::MemoryCheck:
			if (RunMemCheck(mips->pc, mips->r[inst->src1] + constPool[inst->src2])) {
				CoreTiming::ForceCheck();
				return mips->pc;
			}
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

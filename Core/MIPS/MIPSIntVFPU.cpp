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


// BUGS!
// It seems likely that there are VFPU bugs, as the intro videos to the Lego Harry Potter
// games showcase serious macroblock corruption and the IDCT is done with VFPU instructions.

// Here's a list of all the instructions used, and thus those which should be exhaustively tested:
// lv.q
// vs2i.p
// vi2f.q
// vmul.q
// vs2i.p
// vsub.q
// vadd.q
// vscl.q
// vpfxd 0-1 x 4  (before vscl.q)
// vscl.q
// vf2iz.q
// vi2uc.q
// sv.s

// TODO: Test and maybe fix: https://code.google.com/p/jpcsp/source/detail?r=3082#

#include "Core/Core.h"
#include "Core/Reporting.h"
#include "math/math_util.h"

#include <cmath>

#include "MIPS.h"
#include "MIPSInt.h"
#include "MIPSTables.h"
#include "MIPSVFPUUtils.h"

#include <limits>

#define R(i)   (currentMIPS->r[i])
#define V(i)   (currentMIPS->v[i])
#define VI(i)   (currentMIPS->vi[i])
#define FI(i)  (currentMIPS->fi[i])
#define FsI(i) (currentMIPS->fs[i])
#define PC     (currentMIPS->pc)

#define _RS   ((op >> 21) & 0x1F)
#define _RT   ((op >> 16) & 0x1F)
#define _RD   ((op>>11) & 0x1F)
#define _FS   ((op>>11) & 0x1F)
#define _FT   ((op >> 16) & 0x1F)
#define _FD   ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

#define HI currentMIPS->hi
#define LO currentMIPS->lo

#ifndef M_LOG2E
#define M_E        2.71828182845904523536f
#define M_LOG2E    1.44269504088896340736f
#define M_LOG10E   0.434294481903251827651f
#define M_LN2      0.693147180559945309417f
#define M_LN10     2.30258509299404568402f
#undef M_PI
#define M_PI       3.14159265358979323846f
#define M_PI_2     1.57079632679489661923f
#define M_PI_4     0.785398163397448309616f
#define M_1_PI     0.318309886183790671538f
#define M_2_PI     0.636619772367581343076f
#define M_2_SQRTPI 1.12837916709551257390f
#define M_SQRT2    1.41421356237309504880f
#define M_SQRT1_2  0.707106781186547524401f
#endif

// Preserves NaN in first param, takes sign of equal second param.
// Technically, std::max may do this but it's undefined.
inline float nanmax(float f, float cst)
{
	return f <= cst ? cst : f;
}

// Preserves NaN in first param, takes sign of equal second param.
inline float nanmin(float f, float cst)
{
	return f >= cst ? cst : f;
}

// Preserves NaN in first param, takes sign of equal value in others.
inline float nanclamp(float f, float lower, float upper)
{
	return nanmin(nanmax(f, lower), upper);
}


#ifndef BLACKBERRY
double rint(double x){
return floor(x+.5);
}
#endif

void ApplyPrefixST(float *v, u32 data, VectorSize size)
{
	// Possible optimization shortcut:
	if (data == 0xe4)
		return;

	int n = GetNumVectorElements(size);
	float origV[4];
	static const float constantArray[8] = {0.f, 1.f, 2.f, 0.5f, 3.f, 1.f/3.f, 0.25f, 1.f/6.f};

	for (int i = 0; i < n; i++)
	{
		origV[i] = v[i];
	}

	for (int i = 0; i < n; i++)
	{
		int regnum = (data >> (i*2)) & 3;
		int abs    = (data >> (8+i)) & 1;
		int negate = (data >> (16+i)) & 1;
		int constants = (data >> (12+i)) & 1;

		if (!constants)
		{
			// Prefix may say "z, z, z, z" but if this is a pair, we force to x.
			// TODO: But some ops seem to use const 0 instead?
			if (regnum >= n)
			{
				ERROR_LOG_REPORT(CPU, "Invalid VFPU swizzle: %08x / %d", data, size);
				regnum = 0;
			}

			v[i] = origV[regnum];
			if (abs)
				v[i] = fabs(v[i]);
		}
		else
		{
			v[i] = constantArray[regnum + (abs<<2)];
		}

		if (negate)
			v[i] = -v[i];
	}
}

inline void ApplySwizzleS(float *v, VectorSize size)
{
	ApplyPrefixST(v, currentMIPS->vfpuCtrl[VFPU_CTRL_SPREFIX], size);
}

inline void ApplySwizzleT(float *v, VectorSize size)
{
	ApplyPrefixST(v, currentMIPS->vfpuCtrl[VFPU_CTRL_TPREFIX], size);
}

void ApplyPrefixD(float *v, VectorSize size, bool onlyWriteMask = false)
{
	u32 data = currentMIPS->vfpuCtrl[VFPU_CTRL_DPREFIX];
	if (!data || onlyWriteMask)
		return;
	int n = GetNumVectorElements(size);
	for (int i = 0; i < n; i++)
	{
		int sat = (data >> (i * 2)) & 3;
		if (sat == 1)
		{
			if (v[i] > 1.0f) v[i] = 1.0f;
			// This includes -0.0f -> +0.0f.
			if (v[i] <= 0.0f) v[i] = 0.0f;
		}
		else if (sat == 3)
		{
			if (v[i] > 1.0f)  v[i] = 1.0f;
			if (v[i] < -1.0f) v[i] = -1.0f;
		}
	}
}

void EatPrefixes()
{
	currentMIPS->vfpuCtrl[VFPU_CTRL_SPREFIX] = 0xe4;  // passthru
	currentMIPS->vfpuCtrl[VFPU_CTRL_TPREFIX] = 0xe4;  // passthru
	currentMIPS->vfpuCtrl[VFPU_CTRL_DPREFIX] = 0;
}

namespace MIPSInt
{
	void Int_VPFX(u32 op)
	{
		int data = op & 0xFFFFF;
		int regnum = (op >> 24) & 3;
		currentMIPS->vfpuCtrl[VFPU_CTRL_SPREFIX + regnum] = data;
		PC += 4;
	}

	void Int_SVQ(u32 op)
	{
		int imm = (signed short)(op&0xFFFC);
		int rs = _RS;
		int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);

		u32 addr = R(rs) + imm;

		switch (op >> 26)
		{
		case 53: //lvl.q/lvr.q
			if (addr & 0x3)
			{
				_dbg_assert_msg_(CPU, 0, "Misaligned lvX.q");
			}
			if ((op&2) == 0)
			{
				// It's an LVL
				float d[4];
				ReadVector(d, V_Quad, vt);
				int offset = (addr >> 2) & 3;
				for (int i = 0; i < offset + 1; i++)
				{
					d[3 - i] = Memory::Read_Float(addr - i * 4);
				}
				WriteVector(d, V_Quad, vt);
			}
			else
			{
				// It's an LVR
				float d[4];
				ReadVector(d, V_Quad, vt);
				int offset = (addr >> 2) & 3;
				for (int i = 0; i < (3 - offset) + 1; i++)
				{
					d[i] = Memory::Read_Float(addr + 4 * i);
				}
				WriteVector(d, V_Quad, vt);
			}
			break;

		case 54: //lv.q
			if (addr & 0xF)
			{
				_dbg_assert_msg_(CPU, 0, "Misaligned lv.q");
			}
			WriteVector((const float*)Memory::GetPointer(addr), V_Quad, vt);
			break;

		case 61: // svl.q/svr.q
			if (addr & 0x3)
			{
				_dbg_assert_msg_(CPU, 0, "Misaligned svX.q");
			}
			if ((op&2) == 0)
			{
				// It's an SVL
				float d[4];
				ReadVector(d, V_Quad, vt);
				int offset = (addr >> 2) & 3;
				for (int i = 0; i < offset + 1; i++)
				{
					Memory::Write_Float(d[3 - i], addr - i * 4);
				}
			}
			else
			{
				// It's an SVR
				float d[4];
				ReadVector(d, V_Quad, vt);
				int offset = (addr >> 2) & 3;
				for (int i = 0; i < (3 - offset) + 1; i++)
				{
					Memory::Write_Float(d[i], addr + 4 * i);
				}
			}
			break;

		case 62: //sv.q
			if (addr & 0xF)
			{
				_dbg_assert_msg_(CPU, 0, "Misaligned sv.q");
			}
			ReadVector((float*)Memory::GetPointer(addr), V_Quad, vt);
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret VQ instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_VMatrixInit(u32 op)
	{
		static const float idt[16] = 
		{
			1,0,0,0,
			0,1,0,0,
			0,0,1,0,
			0,0,0,1,
		};
		static const float zero[16] = 
		{
			0,0,0,0,
			0,0,0,0,
			0,0,0,0,
			0,0,0,0,
		};
		static const float one[16] = 
		{
			1,1,1,1,
			1,1,1,1,
			1,1,1,1,
			1,1,1,1,
		};
		int vd = _VD;
		MatrixSize sz = GetMtxSize(op);
		const float *m;

		switch ((op >> 16) & 0xF)
		{
		case 3: m=idt; break; //identity   // vmidt
		case 6: m=zero; break;             // vzero
		case 7: m=one; break;              // vone
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			return;
		}

		WriteMatrix(m, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_VVectorInit(u32 op)
	{
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		static const float ones[4] = {1,1,1,1};
		static const float zeros[4] = {0,0,0,0};
		const float *v;
		switch ((op >> 16) & 0xF)
		{
		case 6: v=zeros; break;  //vzero
		case 7: v=ones; break;   //vone
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			return;
		}
		float o[4];
		for (int i = 0; i < n; i++)
			o[i] = v[i];
		ApplyPrefixD(o, sz);
		WriteVector(o, sz, vd);

		EatPrefixes();
		PC += 4;
	}

	void Int_Viim(u32 op)
	{
		int vt = _VT;
		s32 imm = (s16)(op&0xFFFF);
		u16 uimm16 = (op&0xFFFF);
		//V(vt) = (float)imm;
		float f[1];
		int type = (op >> 23) & 7;
		if (type == 6)
			f[0] = (float)imm;  // viim
		else if (type == 7)
			f[0] = Float16ToFloat32((u16)uimm16);   // vfim
		else
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
		
		ApplyPrefixD(f, V_Single);
		V(vt) = f[0];
		PC += 4;
		EatPrefixes();
	}

	void Int_Vidt(u32 op)
	{
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		float f[4];
		switch (sz)
		{
		case V_Pair:
			f[0] = (vd&1)==0 ? 1.0f : 0.0f;
			f[1] = (vd&1)==1 ? 1.0f : 0.0f;
			break;
		case V_Quad:
			f[0] = (vd&3)==0 ? 1.0f : 0.0f;
			f[1] = (vd&3)==1 ? 1.0f : 0.0f;
			f[2] = (vd&3)==2 ? 1.0f : 0.0f;
			f[3] = (vd&3)==3 ? 1.0f : 0.0f;
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		ApplyPrefixD(f, sz);
		WriteVector(f, sz, vd);
		PC += 4;
		EatPrefixes();
	}
	// The test really needs some work.
	void Int_Vmmul(u32 op)
	{
		float s[16];
		float t[16];
		float d[16];

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		ReadMatrix(s, sz, vs);
		ReadMatrix(t, sz, vt);

		for (int a = 0; a < n; a++)
		{
			for (int b = 0; b < n; b++)
			{
				float sum = 0.0f;
				for (int c = 0; c < n; c++)
				{
					sum += s[b*4 + c] * t[a*4 + c];
				}
				d[a*4 + b] = sum;
			}
		}

		WriteMatrix(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vmscl(u32 op)
	{
		float d[16];
		float s[16];
		float t[1];

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		ReadMatrix(s, sz, vs);
		ReadVector(t, V_Single, vt);

		for (int a = 0; a < n; a++)
		{
			for (int b = 0; b < n; b++)
			{
				d[a*4 + b] = s[a*4 + b] * t[0];
			}
		}

		WriteMatrix(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vmmov(u32 op)
	{
		float s[16];
		int vd = _VD;
		int vs = _VS;
		MatrixSize sz = GetMtxSize(op);
		ReadMatrix(s, sz, vs);
		// This is just for matrices. No prefixes.
		WriteMatrix(s, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vflush(u32 op)
	{
		// DEBUG_LOG(CPU,"vflush");
		PC += 4;
	}

	void Int_VV2Op(u32 op)
	{
		float s[4], d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		for (int i = 0; i < GetNumVectorElements(sz); i++)
		{
			switch ((op >> 16) & 0x1f)
			{
			case 0: d[i] = s[i]; break; //vmov
			case 1: d[i] = fabsf(s[i]); break; //vabs
			case 2: d[i] = -s[i]; break; //vneg
			// vsat0 changes -0.0 to +0.0.
			case 4: if (s[i] <= 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
			case 5: if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
			case 16: d[i] = 1.0f / s[i]; break; //vrcp
			case 17: d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
			case 18: d[i] = sinf((float)M_PI_2 * s[i]); break; //vsin
			case 19: d[i] = cosf((float)M_PI_2 * s[i]); break; //vcos
			case 20: d[i] = powf(2.0f, s[i]); break; //vexp2
			case 21: d[i] = logf(s[i])/log(2.0f); break; //vlog2
			case 22: d[i] = fabsf(sqrtf(s[i])); break; //vsqrt
			case 23: d[i] = asinf(s[i]) / M_PI_2; break; //vasin
			case 24: d[i] = -1.0f / s[i]; break; // vnrcp
			case 26: d[i] = -sinf((float)M_PI_2 * s[i]); break; // vnsin
			case 28: d[i] = 1.0f / powf(2.0, s[i]); break; // vrexp2
			default:
				_dbg_assert_msg_(CPU,0,"Trying to interpret VV2Op instruction that can't be interpreted");
				break;
			}
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vocp(u32 op)
	{
		float s[4], d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		for (int i = 0; i < GetNumVectorElements(sz); i++)
		{
			// Always positive NaN.
			d[i] = my_isnan(s[i]) ? fabsf(s[i]) : 1.0f - s[i];
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}
	
	void Int_Vsocp(u32 op)
	{
		float s[4], d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		int n = GetNumVectorElements(sz);
		float x = s[0];
		d[0] = nanclamp(1.0f - x, 0.0f, 1.0f);
		d[1] = nanclamp(x, 0.0f, 1.0f);
		VectorSize outSize = V_Pair;
		if (n > 1) {
			float y = s[1];
			d[2] = nanclamp(1.0f - y, 0.0f, 1.0f);
			d[3] = nanclamp(y, 0.0f, 1.0f);
			outSize = V_Quad;
		} 
		WriteVector(d, outSize, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vsgn(u32 op)
	{
		float s[4], d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		for (int i = 0; i < GetNumVectorElements(sz); i++)
		{
			// To handle NaNs correctly, we do this with integer hackery
			u32 val;
			memcpy(&val, &s[i], sizeof(u32));
			if (val == 0 || val == 0x80000000)
				d[i] = 0.0f;
			else if ((val >> 31) == 0)
				d[i] = 1.0f;
			else
				d[i] = -1.0f;
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vf2i(u32 op)
	{
		float s[4];
		int d[4];
		int vd = _VD;
		int vs = _VS;
		int imm = (op >> 16) & 0x1f;
		float mult = (float)(1UL << imm);
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz); //TODO: and the mask to kill everything but swizzle
		for (int i = 0; i < GetNumVectorElements(sz); i++)
		{
			if (my_isnan(s[i])) {
				d[i] = 0x7FFFFFFF;
				continue;
			}
			double sv = s[i] * mult; // (float)0x7fffffff == (float)0x80000000
			// Cap/floor it to 0x7fffffff / 0x80000000
			if (sv > 0x7fffffff) sv = 0x7fffffff;
			if (sv < (int)0x80000000) sv = (int)0x80000000;
			switch ((op >> 21) & 0x1f)
			{
			case 16: d[i] = (int)rint(sv); break; //n
			case 17: d[i] = s[i]>=0 ? (int)floor(sv) : (int)ceil(sv); break; //z
			case 18: d[i] = (int)ceil(sv); break; //u
			case 19: d[i] = (int)floor(sv); break; //d
			default: d[i] = 0x7FFFFFFF; break;
			}
		}
		ApplyPrefixD((float*)d, sz, true);
		WriteVector((float*)d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vi2f(u32 op)
	{
		int s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		int imm = (op >> 16) & 0x1f;
		float mult = 1.0f/(float)(1UL << imm);
		VectorSize sz = GetVecSize(op);
		ReadVector((float*)&s[0], sz, vs);
		ApplySwizzleS((float*)&s[0], sz); //TODO: and the mask to kill everything but swizzle
		for (int i = 0; i < GetNumVectorElements(sz); i++)
		{
			d[i] = (float)s[i] * mult;
		}
		ApplyPrefixD(d, sz); //TODO: and the mask to kill everything but mask
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	union FP32 {
		u32 u;
		float f;
	};
	
	struct FP16 {
		u16 u;
	};

	// magic code from ryg: http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
	// See also SSE2 version: https://gist.github.com/rygorous/2144712
	static FP32 half_to_float_fast5(FP16 h)
	{
		static const FP32 magic = { (127 + (127 - 15)) << 23 };
		static const FP32 was_infnan = { (127 + 16) << 23 };
		FP32 o;
		o.u = (h.u & 0x7fff) << 13;     // exponent/mantissa bits
		o.f *= magic.f;                 // exponent adjust
		if (o.f >= was_infnan.f)        // make sure Inf/NaN survive
			o.u |= 255 << 23;
		o.u |= (h.u & 0x8000) << 16;    // sign bit
		return o;
	}

	static float ExpandHalf(u16 half) {
		FP16 fp16;
		fp16.u = half;
		FP32 fp = half_to_float_fast5(fp16);
		return fp.f;
	}

	// More magic code: https://gist.github.com/rygorous/2156668
	static FP16 float_to_half_fast3(FP32 f)
	{
		static const FP32 f32infty = { 255 << 23 };
		static const FP32 f16infty = { 31 << 23 };
		static const FP32 magic = { 15 << 23 };
		static const u32 sign_mask = 0x80000000u;
		static const u32 round_mask = ~0xfffu;
		FP16 o = { 0 };

		u32 sign = f.u & sign_mask;
		f.u ^= sign;

		if (f.u >= f32infty.u) // Inf or NaN (all exponent bits set)
			 o.u = (f.u > f32infty.u) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
		else // (De)normalized number or zero
		{
			f.u &= round_mask;
			f.f *= magic.f;
			f.u -= round_mask;
			if (f.u > f16infty.u) f.u = f16infty.u; // Clamp to signed infinity if overflowed

			 o.u = f.u >> 13; // Take the bits!
		}

		o.u |= sign >> 16;
		return o;
	}

	static u16 ShrinkToHalf(float full) {
		FP32 fp32;
		fp32.f = full;
		FP16 fp = float_to_half_fast3(fp32);
		return fp.u;
	}

	void Int_Vh2f(u32 op)
	{
		u32 s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector((float*)&s[0], sz, vs);
		ApplySwizzleS((float*)&s[0], sz);
		
		VectorSize outsize = V_Pair;
		switch (sz) {
		case V_Single:
			outsize = V_Pair;
			d[0] = ExpandHalf(s[0] & 0xFFFF);
			d[1] = ExpandHalf(s[0] >> 16);
			break;
		case V_Pair:
			outsize = V_Quad;
			d[0] = ExpandHalf(s[0] & 0xFFFF);
			d[1] = ExpandHalf(s[0] >> 16);
			d[2] = ExpandHalf(s[1] & 0xFFFF);
			d[3] = ExpandHalf(s[1] >> 16);
			break;
		case V_Triple:
		case V_Quad:
			_dbg_assert_msg_(CPU, 0, "Trying to interpret Int_Vh2f instruction that can't be interpreted");
			break;
		}
		ApplyPrefixD(d, outsize);
		WriteVector(d, outsize, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vf2h(u32 op)
	{
		float s[4];
		u32 d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		
		VectorSize outsize = V_Single;
		switch (sz) {
		case V_Pair:
			outsize = V_Single;
			d[0] = ShrinkToHalf(s[0]) | ((u32)ShrinkToHalf(s[1]) << 16);
			break;
		case V_Quad:
			outsize = V_Pair;
			d[0] = ShrinkToHalf(s[0]) | ((u32)ShrinkToHalf(s[1]) << 16);
			d[1] = ShrinkToHalf(s[2]) | ((u32)ShrinkToHalf(s[3]) << 16);
			break;
		case V_Single:
		case V_Triple:
			_dbg_assert_msg_(CPU, 0, "Trying to interpret Int_Vf2h instruction that can't be interpreted");
			break;
		}
		ApplyPrefixD((float*)&d[0], outsize);
		WriteVector((float*)&d[0], outsize, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vx2i(u32 op)
	{
		u32 s[4];
		u32 d[4] = {0};
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		VectorSize oz = sz;
		ReadVector((float*)s, sz, vs);
		// ForbidVPFXS

		switch ((op >> 16) & 3) {
		case 0:  // vuc2i  
			// Quad is the only option.
			// This operation is weird. This particular way of working matches hw but does not 
			// seem quite sane.
			{
				u32 value = s[0];
				u32 value2 = value / 2;
				for (int i = 0; i < 4; i++) {
					d[i] = (u32)((value & 0xFF) * 0x01010101) >> 1;
					value >>= 8;
				}
				oz = V_Quad;
			}
			break;

		case 1:  // vc2i
			// Quad is the only option
			{
				u32 value = s[0];
				d[0] = (value & 0xFF) << 24;
				d[1] = (value & 0xFF00) << 16;
				d[2] = (value & 0xFF0000) << 8;
				d[3] = (value & 0xFF000000);
				oz = V_Quad;
			}
			break;

		case 2:  // vus2i
			oz = V_Pair;
			switch (sz)
			{
			case V_Pair:
				oz = V_Quad;
				// Intentional fallthrough.
			case V_Single:
				for (int i = 0; i < GetNumVectorElements(sz); i++) {
					u32 value = s[i];
					d[i * 2] = (value & 0xFFFF) << 15;
					d[i * 2 + 1] = (value & 0xFFFF0000) >> 1;
				}
				break;

			default:
				ERROR_LOG_REPORT(CPU, "vus2i with more than 2 elements.");
				break;
			}
			break;

		case 3:  // vs2i
			oz = V_Pair;
			switch (sz)
			{
			case V_Pair:
				oz = V_Quad;
				// Intentional fallthrough.
			case V_Single:
				for (int i = 0; i < GetNumVectorElements(sz); i++) {
					u32 value = s[i];
					d[i * 2] = (value & 0xFFFF) << 16;
					d[i * 2 + 1] = value & 0xFFFF0000;
				}
				break;

			default:
				ERROR_LOG_REPORT(CPU, "vs2i with more than 2 elements.");
				break;
			}
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		
		ApplyPrefixD((float*)d,oz, true);  // Only write mask
		WriteVector((float*)d,oz,vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vi2x(u32 op)
	{
		int s[4];
		u32 d[2] = {0};
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		VectorSize oz;
		ReadVector((float*)s, sz, vs);
		ApplySwizzleS((float*)s, sz); //TODO: and the mask to kill everything but swizzle
		switch ((op >> 16)&3)
		{
		case 0: //vi2uc
			{
				for (int i = 0; i < 4; i++)
				{
					int v = s[i];
					if (v < 0) v = 0;
					v >>= 23;
					d[0] |= ((u32)v & 0xFF) << (i * 8);
				}
				oz = V_Single;
			}
			break;

		case 1: //vi2c
			{
				for (int i = 0; i < 4; i++)
				{
					u32 v = s[i];
					d[0] |= (v >> 24) << (i * 8);
				}
				oz = V_Single;
			}
			break;

		case 2:  //vi2us
			{
				for (int i = 0; i < GetNumVectorElements(sz) / 2; i++) {
					int low = s[i * 2];
					int high = s[i * 2 + 1];
					if (low < 0) low = 0;
					if (high < 0) high = 0;
					low >>= 15;
					high >>= 15;
					d[i] = low | (high << 16);
				}
				if (sz == V_Quad) oz = V_Pair;
				if (sz == V_Pair) oz = V_Single;
			}
			break;
		case 3:  //vi2s
			{
				for (int i = 0; i < GetNumVectorElements(sz) / 2; i++) {
					u32 low = s[i * 2];
					u32 high = s[i * 2 + 1];
					low >>= 16;
					high >>= 16;
					d[i] = low | (high << 16);
				}
				if (sz == V_Quad) oz = V_Pair;
				if (sz == V_Pair) oz = V_Single;
			}
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			oz = V_Single;
			break;
		}
		ApplyPrefixD((float*)d,oz);
		WriteVector((float*)d,oz,vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_ColorConv(u32 op) 
	{
		int vd = _VD;
		int vs = _VS;
		u32 s[4];
		VectorSize sz = V_Quad;
		ReadVector((float *)s, sz, vs);
		u16 colors[4];
		for (int i = 0; i < 4; i++)
		{
			u32 in = s[i];
			u16 col = 0;
			switch ((op >> 16) & 3)
			{
			case 1:  // 4444
				{
					int a = ((in >> 24) & 0xFF) >> 4;
					int b = ((in >> 16) & 0xFF) >> 4;
					int g = ((in >> 8) & 0xFF) >> 4;
					int r = ((in) & 0xFF) >> 4;
					col = (a << 12) | (b << 8) | (g << 4 ) | (r);
					break;
				}
			case 2:  // 5551
				{
					int a = ((in >> 24) & 0xFF) >> 7;
					int b = ((in >> 16) & 0xFF) >> 3;
					int g = ((in >> 8) & 0xFF) >> 3;
					int r = ((in) & 0xFF) >> 3;
					col = (a << 15) | (b << 10) | (g << 5) | (r);
					break;
				}
			case 3:  // 565
				{
					int b = ((in >> 16) & 0xFF) >> 3;
					int g = ((in >> 8) & 0xFF) >> 2;
					int r = ((in) & 0xFF) >> 3;
					col = (b << 11) | (g << 5) | (r); 
					break;
				}
			}
			colors[i] = col;
		}
		u32 ov[2] = {(u32)colors[0] | (colors[1] << 16), (u32)colors[2] | (colors[3] << 16)};
		WriteVector((const float *)ov, V_Pair, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_VDot(u32 op)
	{
		float s[4], t[4];
		float d;
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		float sum = 0.0f;
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++)
		{
			sum += s[i]*t[i];
		}
		d = sum;
		ApplyPrefixD(&d,V_Single);
		WriteVector(&d, V_Single, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_VHdp(u32 op)
	{
		float s[4], t[4];
		float d;
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		float sum = 0.0f;
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++)
		{
			sum += (i == n - 1) ? t[i] : s[i]*t[i];
		}
		d = my_isnan(sum) ? fabsf(sum) : sum;
		ApplyPrefixD(&d,V_Single);
		WriteVector(&d, V_Single, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vbfy(u32 op)
	{
		float s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		int n = GetNumVectorElements(sz);
		if (op & 0x10000)
		{
			// vbfy2
			d[0] = s[0] + s[2];
			d[1] = s[1] + s[3];
			d[2] = s[0] - s[2];
			d[3] = s[1] - s[3];
		}
		else
		{
			for (int i = 0; i < n; i+=2)
			{
				d[i]   = s[i] + s[i+1];
				d[i+1] = s[i] - s[i+1];
			}
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}
	
	void Int_Vsrt1(u32 op)
	{
		float s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		float x = s[0];
		float y = s[1];
		float z = s[2];
		float w = s[3];
		d[0] = std::min(x, y);
		d[1] = std::max(x, y);
		d[2] = std::min(z, w);
		d[3] = std::max(z, w);
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vsrt2(u32 op)
	{
		float s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		float x = s[0];
		float y = s[1];
		float z = s[2];
		float w = s[3];
		d[0] = std::min(x, w);
		d[1] = std::min(y, z);
		d[2] = std::max(y, z);
		d[3] = std::max(x, w);
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vsrt3(u32 op)
	{
		float s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		float x = s[0];
		float y = s[1];
		float z = s[2];
		float w = s[3];
		d[0] = std::max(x, y);
		d[1] = std::min(x, y);
		d[2] = std::max(z, w);
		d[3] = std::min(z, w);
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vsrt4(u32 op)
	{
		float s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		float x = s[0];
		float y = s[1];
		float z = s[2];
		float w = s[3];
		d[0] = std::max(x, w);
		d[1] = std::max(y, z);
		d[2] = std::min(y, z);
		d[3] = std::min(x, w);
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}
	
	void Int_Vcrs(u32 op)
	{
		//half a cross product
		float s[4], t[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		if (sz != V_Triple)
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");

		ReadVector(s, sz, vs);
		ReadVector(t, sz, vt);
		// no swizzles allowed
		d[0] = s[1] * t[2];
		d[1] = s[2] * t[0];
		d[2] = s[0] * t[1];
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}
	
	void Int_Vdet(u32 op)
	{
		float s[4], t[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		if (sz != V_Pair)
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		d[0] = s[0] * t[1] - s[1] * t[0];
		ApplyPrefixD(d, sz);
		WriteVector(d, V_Single, vd);
		PC += 4;
		EatPrefixes();
	}
	
	void Int_Vfad(u32 op)
	{
		float s[4];
		float d;
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		float sum = 0.0f;
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++)
		{
			sum += s[i];
		}
		d = sum;
		ApplyPrefixD(&d,V_Single);
		V(vd) = d;
		PC += 4;
		EatPrefixes();
	}

	void Int_Vavg(u32 op)
	{
		float s[4];
		float d;
		int vd = _VD;
		int vs = _VS;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		float sum = 0.0f;
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++)
		{
			sum += s[i];
		}
		d = sum / n;
		ApplyPrefixD(&d, V_Single);
		V(vd) = d;
		PC += 4;
		EatPrefixes();
	}

	void Int_VScl(u32 op)
	{
		float s[4];
		float d[4];
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		float scale = V(vt);
		if (currentMIPS->vfpuCtrl[VFPU_CTRL_TPREFIX] != 0xE4)
		{
			// WARN_LOG(CPU, "Broken T prefix used with VScl: %08x / %08x", currentMIPS->vfpuCtrl[VFPU_CTRL_TPREFIX], op);
			ApplySwizzleT(&scale, V_Single);
		}
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++)
		{
			d[i] = s[i] * scale;
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vrnds(u32 op)
	{
		int vd = _VD;
		int seed = VI(vd);
		currentMIPS->rng.Init(seed);
		PC += 4;
		EatPrefixes();
	}

	void Int_VrndX(u32 op)
	{
		float d[4];
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++)
		{
			switch ((op >> 16) & 0x1f)
			{
			case 1: d[i] = (float)currentMIPS->rng.R32(); break;  // vrndi - TODO: copy bits instead?
			case 2: d[i] = 1.0f + ((float)currentMIPS->rng.R32() / 0xFFFFFFFF); break; // vrndf1   TODO: make more accurate
			case 3: d[i] = 2.0f + 2 * ((float)currentMIPS->rng.R32() / 0xFFFFFFFF); break; // vrndf2   TODO: make more accurate
			case 4: d[i] = 0.0f;  // Should not get here
			}
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	// Generates one line of a rotation matrix around one of the three axes
	void Int_Vrot(u32 op)
	{
		int vd = _VD;
		int vs = _VS;
		int imm = (op >> 16) & 0x1f;
		VectorSize sz = GetVecSize(op);
		float angle = V(vs) * M_PI_2;
		bool negSin = (imm & 0x10) ? true : false;
		float sine = sinf(angle);
		float cosine = cosf(angle);
		if (negSin)
			sine = -sine;
		float d[4] = {0};
		if (((imm >> 2) & 3) == (imm & 3))
		{
			for (int i = 0; i < 4; i++)
				d[i] = sine;
		}
		d[(imm >> 2) & 3] = sine;
		d[imm & 3] = cosine;
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vtfm(u32 op)
	{
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		int ins = (op >> 23) & 7;

		VectorSize sz = GetVecSize(op);
		MatrixSize msz = GetMtxSize(op);
		int n = GetNumVectorElements(sz);

		bool homogenous = false;
		if (n == ins)
		{
			n++;
			sz = (VectorSize)((int)(sz) + 1);
			msz = (MatrixSize)((int)(msz) + 1);
			homogenous = true;
		}

		float s[16];
		ReadMatrix(s, msz, vs);
		float t[4];
		ReadVector(t, sz, vt);
		float d[4];

		if (homogenous)
		{
			for (int i = 0; i < n; i++)
			{
				d[i] = 0.0f;
				for (int k = 0; k < n; k++)
				{
					d[i] += (k == n-1) ? s[i*4+k] : (s[i*4+k] * t[k]);
				}
			}
		}
		else if (n == ins + 1)
		{
			for (int i = 0; i < n; i++)
			{
				d[i] = 0.0f;
				for (int k = 0; k < n; k++)
				{
					d[i] += s[i*4+k] * t[k];
				}
			}
		}
		else
		{
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted (BADVTFM)");
		}
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}
 
	void Int_SV(u32 op)
	{
		s32 imm = (signed short)(op&0xFFFC);
		int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
		int rs = (op >> 21) & 0x1f;
		u32 addr = R(rs) + imm;

		switch (op >> 26)
		{
		case 50: //lv.s
			VI(vt) = Memory::Read_U32(addr);
			break;
		case 58: //sv.s
			Memory::Write_U32(VI(vt), addr);
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}


	void Int_Mftv(u32 op)
	{
		int imm = op & 0xFF;
		int rt = _RT;
		switch ((op >> 21) & 0x1f)
		{
		case 3: //mfv / mfvc
			// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
			if (rt != 0) {
				if (imm < 128) {
					R(rt) = VI(imm);
				} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc
					R(rt) = currentMIPS->vfpuCtrl[imm - 128];
				} else {
					//ERROR - maybe need to make this value too an "interlock" value?
					_dbg_assert_msg_(CPU,0,"mfv - invalid register");
				}
			}
			break;

		case 7: //mtv
			if (imm < 128) {
				VI(imm) = R(rt);
			} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc
				currentMIPS->vfpuCtrl[imm - 128] = R(rt);
			} else {
				//ERROR
				_dbg_assert_msg_(CPU,0,"mtv - invalid register");
			}
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		PC += 4;
	}

	void Int_Vmfvc(u32 op) {
		int vs = _VS;
		int imm = op & 0xFF;
		if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
			VI(vs) = currentMIPS->vfpuCtrl[imm - 128];
		}
		PC += 4;
	}

	void Int_Vmtvc(u32 op) {
		int vs = _VS;
		int imm = op & 0xFF;
		if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
			currentMIPS->vfpuCtrl[imm - 128] = VI(vs);
		}
		PC += 4;
	}

#undef max

	void Int_Vcst(u32 op)
	{
		static const float constants[32] =
		{
			0,
			std::numeric_limits<float>::max(),  // all these are verified on real PSP
			sqrtf(2.0f),
			sqrtf(0.5f),
			2.0f/sqrtf((float)M_PI),
			2.0f/(float)M_PI,
			1.0f/(float)M_PI,
			(float)M_PI/4,
			(float)M_PI/2,
			(float)M_PI,
			(float)M_E,
			(float)M_LOG2E,
			(float)M_LOG10E,
			(float)M_LN2,
			(float)M_LN10,
			2*(float)M_PI,
			(float)M_PI/6,
			log10f(2.0f),
			logf(10.0f)/logf(2.0f),
			sqrtf(3.0f)/2.0f,
		};

		int conNum = (op >> 16) & 0x1f;
		int vd = _VD;

		VectorSize sz = GetVecSize(op);
		float c = constants[conNum];
		float temp[4] = {c,c,c,c};
		ApplyPrefixD(temp, sz);
		WriteVector(temp, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	enum VCondition
	{
		VC_FL,
		VC_EQ,
		VC_LT,
		VC_LE,
		VC_TR,
		VC_NE,
		VC_GE,
		VC_GT,
		VC_EZ,
		VC_EN,
		VC_EI,
		VC_ES,
		VC_NZ,
		VC_NN,
		VC_NI,
		VC_NS
	};

	void Int_Vcmp(u32 op)
	{
		int vs = _VS;
		int vt = _VT;
		int cond = op & 0xf;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		float s[4];
		float t[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		int cc = 0;
		int or_val = 0;
		int and_val = 1;
		int affected_bits = (1 << 4) | (1 << 5);  // 4 and 5
		for (int i = 0; i < n; i++)
		{
			int c;
			// These set c to 0 or 1, nothing else.
			switch (cond)
			{
			case VC_FL: c = 0; break;
			case VC_EQ: c = s[i] == t[i]; break;
			case VC_LT: c = s[i] < t[i]; break;
			case VC_LE: c = s[i] <= t[i]; break;

			case VC_TR: c = 1; break;
			case VC_NE: c = s[i] != t[i]; break;
			case VC_GE: c = s[i] >= t[i]; break;
			case VC_GT: c = s[i] > t[i]; break;

			case VC_EZ: c = s[i] == 0.0f || s[i] == -0.0f; break;
			case VC_EN: c = my_isnan(s[i]); break;
			case VC_EI: c = my_isinf(s[i]); break;
			case VC_ES: c = my_isnan(s[i]) || my_isinf(s[i]); break;   // Tekken Dark Resurrection

			case VC_NZ: c = s[i] != 0; break;
			case VC_NN: c = !my_isnan(s[i]); break;
			case VC_NI: c = !my_isinf(s[i]); break;
			case VC_NS: c = !my_isnan(s[i]) && !my_isinf(s[i]); break;

			default:
				_dbg_assert_msg_(CPU,0,"Unsupported vcmp condition code %d", cond);
				PC += 4;
				EatPrefixes();
				return;
			}
			cc |= (c<<i);
			or_val |= c;
			and_val &= c;
			affected_bits |= 1 << i;
		}
		// Use masking to only change the affected bits
		currentMIPS->vfpuCtrl[VFPU_CTRL_CC] =
			(currentMIPS->vfpuCtrl[VFPU_CTRL_CC] & ~affected_bits) |
			((cc | (or_val << 4) | (and_val << 5)) & affected_bits);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vminmax(u32 op) {
		int vt = _VT;
		int vs = _VS;
		int vd = _VD;
		int cond = op&15;
		VectorSize sz = GetVecSize(op);
		int numElements = GetNumVectorElements(sz);
		float s[4];
		float t[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		// positive NAN always loses, unlike SSE
		// negative NAN seems different? TODO
		switch ((op >> 23) & 3) {
		case 2: // vmin
			for (int i = 0; i < numElements; i++)
				d[i] = my_isnan(t[i]) ? s[i] : (my_isnan(s[i]) ? t[i] : std::min(s[i], t[i]));
			break;
		case 3: // vmax
			for (int i = 0; i < numElements; i++)
				d[i] = my_isnan(t[i]) ? t[i] : (my_isnan(s[i]) ? s[i] : std::max(s[i], t[i]));
			break;
		default:
			_dbg_assert_msg_(CPU,0,"unknown min/max op %d", cond);
			return;
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}
	
	void Int_Vscmp(u32 op) {
		int vt = _VT;
		int vs = _VS;
		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		float s[4];
		float t[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n ; i++) {
			float a = s[i] - t[i];
			d[i] = (float) ((0.0 < a) - (a < 0.0));
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vsge(u32 op) {
		int vt = _VT;
		int vs = _VS;
		int vd = _VD;
		int cond = op&15;
		VectorSize sz = GetVecSize(op);
		int numElements = GetNumVectorElements(sz);
		float s[4];
		float t[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		for (int i = 0; i < GetNumVectorElements(sz); i++) {
			if ( my_isnan(s[i]) || my_isnan(t[i]) )
				d[i] = 0.0f;
			else
				d[i] = s[i] >= t[i] ? 1.0f : 0.0f;
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vslt(u32 op) {
		int vt = _VT;
		int vs = _VS;
		int vd = _VD;
		int cond = op&15;
		VectorSize sz = GetVecSize(op);
		int numElements = GetNumVectorElements(sz);
		float s[4];
		float t[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		for (int i = 0; i < GetNumVectorElements(sz); i++) {
			if ( my_isnan(s[i]) || my_isnan(t[i]) )
				d[i] = 0.0f;
			else
				d[i] = s[i] < t[i] ? 1.0f : 0.0f;
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}


	void Int_Vcmov(u32 op)
	{
		int vs = _VS;
		int vd = _VD;
		int tf = (op >> 19) & 1;
		int imm3 = (op >> 16) & 7;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		float s[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(d, sz, vd); //Yes!

		int CC = currentMIPS->vfpuCtrl[VFPU_CTRL_CC];

		if (imm3 < 6)
		{
			if (((CC >> imm3) & 1) == !tf)
			{
				for (int i = 0; i < n; i++)
					d[i] = s[i];
			}
		}
		else if (imm3 == 6)
		{
			for (int i = 0; i < n; i++)
			{
				if (((CC >> i) & 1) == !tf)
					d[i] = s[i];
			}
		}
		else
		{
			_dbg_assert_msg_(CPU,0,"Bad Imm3 in cmov");
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_VecDo3(u32 op)
	{
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		float s[4];
		float t[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		ReadVector(t, sz, vt);
		ApplySwizzleT(t, sz);
		for (int i = 0; i < GetNumVectorElements(sz); i++)
		{
			switch(op >> 26)
			{
			case 24: //VFPU0
				switch ((op >> 23)&7)
				{
				case 0: d[i] = s[i] + t[i]; break; //vadd
				case 1: d[i] = s[i] - t[i]; break; //vsub
				case 7: d[i] = s[i] / t[i]; break; //vdiv
				default: goto bad;
				}
				break;
			case 25: //VFPU1
				switch ((op >> 23)&7)
				{
				case 0: d[i] = s[i] * t[i]; break; //vmul
				default: goto bad;
				}
				break;
			default:
bad:
				_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
				break;
			}
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	
	void Int_CrossQuat(u32 op)
	{
		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);
		float s[4];
		float t[4];
		float d[4];
		ReadVector(s, sz, vs);
		ReadVector(t, sz, vt);
		switch (sz)
		{
		case V_Triple:  // vcrsp.t
			d[0] = s[1]*t[2] - s[2]*t[1];
			d[1] = s[2]*t[0] - s[0]*t[2];
			d[2] = s[0]*t[1] - s[1]*t[0];
			break;

		case V_Quad:   // vqmul.q
			d[0] = s[0]*t[3] + s[1]*t[2] - s[2]*t[1] + s[3]*t[0];
			d[1] = -s[0]*t[2] + s[1]*t[3] + s[2]*t[0] + s[3]*t[1];
			d[2] = s[0]*t[1] - s[1]*t[0] + s[2]*t[3] + s[3]*t[2];
			d[3] = -s[0]*t[0] - s[1]*t[1] - s[2]*t[2] + s[3]*t[3];
			break;

		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();
	}

	void Int_Vlgb(u32 op)
	{
		// S & D valid
		Reporting::ReportMessage("vlgb not implemented");
		_dbg_assert_msg_(CPU,0,"vlgb not implemented");
		PC += 4;
		EatPrefixes();
	}

	// There has to be a concise way of expressing this in terms of
	// bit manipulation on the raw floats.
	void Int_Vwbn(u32 op) {
		Reporting::ReportMessage("vwbn not implemented");
		_dbg_assert_msg_(CPU,0,"vwbn not implemented");
		PC += 4;
		EatPrefixes();

		/*
		int vd = _VD;
		int vs = _VS;

		double modulo = pow(2.0, 127 - (int)((op >> 16) & 0xFF));

		// Only S is allowed? gas says so
		VectorSize sz = GetVecSize(op);

		float s[4];
		float d[4];
		ReadVector(s, sz, vs);
		ApplySwizzleS(s, sz);
		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) {
			double bn = (double)s[i];
			if (bn > 0.0)
				bn = fmod((double)bn, modulo);
			d[i] = s[i] < 0.0f ? bn - modulo : bn + modulo;
		}
		ApplyPrefixD(d, sz);
		WriteVector(d, sz, vd);
		PC += 4;
		EatPrefixes();*/
	}

	void Int_Vsbn(u32 op)
	{
		Reporting::ReportMessage("vsbn not implemented");
		_dbg_assert_msg_(CPU,0,"vsbn not implemented");
		PC += 4;
		EatPrefixes();
	}

	void Int_Vsbz(u32 op)
	{
		Reporting::ReportMessage("vsbz not implemented");
		_dbg_assert_msg_(CPU,0,"vsbz not implemented");
		PC += 4;
		EatPrefixes();
	}
}

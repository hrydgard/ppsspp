
#include "math/math_util.h"

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

const bool disablePrefixes = false;

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { fpr.ReleaseSpillLocks(); Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

using namespace PpcGen;


//#define USE_VMX128

namespace MIPSComp
{
	// Vector regs can overlap in all sorts of swizzled ways.
	// This does allow a single overlap in sregs[i].
	static bool IsOverlapSafeAllowS(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL)
	{
		for (int i = 0; i < sn; ++i)
		{
			if (sregs[i] == dreg && i != di)
				return false;
		}
		for (int i = 0; i < tn; ++i)
		{
			if (tregs[i] == dreg)
				return false;
		}

		// Hurray, no overlap, we can write directly.
		return true;
	}

	static bool IsOverlapSafe(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL)
	{
		return IsOverlapSafeAllowS(dreg, di, sn, sregs, tn, tregs) && sregs[di] != dreg;
	}

	void Jit::ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz) {
		if (prefix == 0xE4) return;

		int n = GetNumVectorElements(sz);
		u8 origV[4];
		static const float constantArray[8] = {0.f, 1.f, 2.f, 0.5f, 3.f, 1.f/3.f, 0.25f, 1.f/6.f};

		for (int i = 0; i < n; i++)
			origV[i] = vregs[i];

		for (int i = 0; i < n; i++)
		{
			int regnum = (prefix >> (i*2)) & 3;
			int abs    = (prefix >> (8+i)) & 1;
			int negate = (prefix >> (16+i)) & 1;
			int constants = (prefix >> (12+i)) & 1;

			// Unchanged, hurray.
			if (!constants && regnum == i && !abs && !negate)
				continue;

			// This puts the value into a temp reg, so we won't write the modified value back.
			vregs[i] = fpr.GetTempV();
			if (!constants) {
				fpr.MapDirtyInV(vregs[i], origV[regnum]);
				fpr.SpillLockV(vregs[i]);

				// Prefix may say "z, z, z, z" but if this is a pair, we force to x.
				// TODO: But some ops seem to use const 0 instead?
				if (regnum >= n) {
					WARN_LOG(CPU, "JIT: Invalid VFPU swizzle: %08x : %d / %d at PC = %08x (%s)", prefix, regnum, n, js.compilerPC, currentMIPS->DisasmAt(js.compilerPC));
					regnum = 0;
				}
				
				if (abs) {
					FABS(fpr.V(vregs[i]), fpr.V(origV[regnum]));
					if (negate)
						FNEG(fpr.V(vregs[i]), fpr.V(vregs[i]));
				} else {
					if (negate)
						FNEG(fpr.V(vregs[i]), fpr.V(origV[regnum]));
					else
						FMR(fpr.V(vregs[i]), fpr.V(origV[regnum]));
				}
			} else {
				fpr.MapRegV(vregs[i], MAP_DIRTY | MAP_NOINIT);
				fpr.SpillLockV(vregs[i]);
				MOVI2F(fpr.V(vregs[i]), constantArray[regnum + (abs<<2)], negate);
			}
		}
	}

	void Jit::GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixDFlag & PpcJitState::PREFIX_KNOWN);

		GetVectorRegs(regs, sz, vectorReg);
		if (js.prefixD == 0)
			return;

		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) {
			// Hopefully this is rare, we'll just write it into a reg we drop.
			if (js.VfpuWriteMask(i))
				regs[i] = fpr.GetTempV();
		}
	}

	void Jit::ApplyPrefixD(const u8 *vregs, VectorSize sz) {
		_assert_(js.prefixDFlag & PpcJitState::PREFIX_KNOWN);
		if (!js.prefixD) return;

		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) 	{
			if (js.VfpuWriteMask(i))
				continue;

			// TODO: These clampers are wrong - put this into google
			// and look at the plot:   abs(x) - abs(x-0.5) + 0.5
			// It's too steep.

			// Also, they mishandle NaN and Inf.
			int sat = (js.prefixD >> (i * 2)) & 3;
			if (sat == 1) {
				fpr.MapRegV(vregs[i], MAP_DIRTY);
				
				MOVI2F(FPR6, 0.0f);
				MOVI2F(FPR7, 1.0f);

				FMAX(fpr.V(vregs[i]), fpr.V(vregs[i]), FPR6);
				FMIN(fpr.V(vregs[i]), fpr.V(vregs[i]), FPR7);
			} else if (sat == 3) {
				fpr.MapRegV(vregs[i], MAP_DIRTY);

				MOVI2F(FPR6, -1.0f);
				MOVI2F(FPR7, 1.0f);

				FMAX(fpr.V(vregs[i]), fpr.V(vregs[i]), FPR6);
				FMIN(fpr.V(vregs[i]), fpr.V(vregs[i]), FPR7);
			}
		}
	}

	void Jit::Comp_SV(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		s32 imm = (signed short)(op&0xFFFC);
		int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
		int rs = _RS;

		bool doCheck = false;
		switch (op >> 26)
		{
		case 50: //lv.s  // VI(vt) = Memory::Read_U32(addr);
			{
				// CC might be set by slow path below, so load regs first.
				fpr.MapRegV(vt, MAP_DIRTY | MAP_NOINIT);
				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(SREG, addr);
				} else {
					gpr.MapReg(rs);					
					SetRegToEffectiveAddress(SREG, rs, imm);
				}

				LoadFloatSwap(fpr.V(vt), BASEREG, SREG);
			}
			break;

		case 58: //sv.s   // Memory::Write_U32(VI(vt), addr);
			{
				// CC might be set by slow path below, so load regs first.
				fpr.MapRegV(vt);
				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(SREG, addr);
				} else {
					gpr.MapReg(rs);
					SetRegToEffectiveAddress(SREG, rs, imm);
				}
				SaveFloatSwap(fpr.V(vt), BASEREG, SREG);
			}
			break;


		default:
			DISABLE;
		}
	}

	void Jit::Comp_SVQ(MIPSOpcode op) {
		// Comp_Generic(op);
		CONDITIONAL_DISABLE;

		int imm = (signed short)(op&0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);
		int rs = _RS;

		bool doCheck = false;
		switch (op >> 26)
		{
		case 54: //lv.q
			{
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsAndSpillLockV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);

				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(SREG, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					SetRegToEffectiveAddress(SREG, rs, imm);
					ADD(SREG, SREG, BASEREG);
				}

				for (int i = 0; i < 4; i++) {				
					MOVI2R(R9, i * 4);
					LoadFloatSwap(fpr.V(vregs[i]), SREG, R9);
				}
			}
			break;

		case 62: //sv.q
			{
				// CC might be set by slow path below, so load regs first.
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsAndSpillLockV(vregs, V_Quad, 0);

				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(SREG, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					SetRegToEffectiveAddress(SREG, rs, imm);
					ADD(SREG, SREG, BASEREG);
				}

				for (int i = 0; i < 4; i++) {			
					MOVI2R(R9, i * 4);
					SaveFloatSwap(fpr.V(vregs[i]), SREG, R9);
				}
			}
			break;

		default:
			DISABLE;
			break;
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_VPFX(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		int data = op & 0xFFFFF;
		int regnum = (op >> 24) & 3;
		switch (regnum) {
		case 0:  // S
			js.prefixS = data;
			js.prefixSFlag = PpcJitState::PREFIX_KNOWN_DIRTY;
			break;
		case 1:  // T
			js.prefixT = data;
			js.prefixTFlag = PpcJitState::PREFIX_KNOWN_DIRTY;
			break;
		case 2:  // D
			js.prefixD = data;
			js.prefixDFlag = PpcJitState::PREFIX_KNOWN_DIRTY;
			break;
		default:
			ERROR_LOG(CPU, "VPFX - bad regnum %i : data=%08x", regnum, data);
			break;
		}
	}

	void Jit::Comp_VVectorInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// WARNING: No prefix support!
		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		switch ((op >> 16) & 0xF)
		{
		case 6: // v=zeros; break;  //vzero
			MOVI2F(FPR5, 0.0f);
			break;
		case 7: // v=ones; break;   //vone
			MOVI2F(FPR5, 1.0f);
			break;
		default:
			DISABLE;
			break;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT | MAP_DIRTY);

		for (int i = 0; i < n; ++i)
			FMR(fpr.V(dregs[i]), FPR5);

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_VMatrixInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 dregs[16];
		GetMatrixRegs(dregs, sz, _VD);

		switch ((op >> 16) & 0xF) {
		case 3: // vmidt
			MOVI2F(FPR6, 0.0f);
			MOVI2F(FPR7, 1.0f);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					FMR(fpr.V(dregs[a * 4 + b]), a == b ? FPR7 : FPR6);
				}
			}
			break;
		case 6: // vmzero
			MOVI2F(FPR6, 0.0f);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					FMR(fpr.V(dregs[a * 4 + b]), FPR6);
				}
			}
			break;
		case 7: // vmone
			MOVI2F(FPR7, 1.0f);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					FMR(fpr.V(dregs[a * 4 + b]), FPR7);
				}
			}
			break;
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_VDot(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixT(tregs, sz, vt);
		GetVectorRegsPrefixD(dregs, V_Single, vd);

		// TODO: applyprefixST here somehow (shuffle, etc...)
		fpr.MapRegsAndSpillLockV(sregs, sz, 0);
		fpr.MapRegsAndSpillLockV(tregs, sz, 0);
		FMULS(FPR6, fpr.V(sregs[0]), fpr.V(tregs[0]));

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++) {
			// sum += s[i]*t[i];
			FMADDS(FPR6, fpr.V(sregs[i]), fpr.V(tregs[i]), FPR6);
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();

		fpr.MapRegV(dregs[0], MAP_NOINIT | MAP_DIRTY);

		// TODO: applyprefixD here somehow (write mask etc..)
		FMR(fpr.V(dregs[0]), FPR6);
		ApplyPrefixD(dregs, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_VecDo3(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		
		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		MIPSReg tempregs[4];
		for (int i = 0; i < n; i++) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs, n, tregs)) {
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInInV(tempregs[i], sregs[i], tregs[i]);
			switch (op >> 26) {
			case 24: //VFPU0
				switch ((op >> 23)&7) {
				case 0: // d[i] = s[i] + t[i]; break; //vadd
					FADDS(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 1: // d[i] = s[i] - t[i]; break; //vsub
					FSUBS(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 7: // d[i] = s[i] / t[i]; break; //vdiv
					FDIVS(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				default:
					DISABLE;
				}
				break;
			case 25: //VFPU1
				switch ((op >> 23) & 7) {
				case 0: // d[i] = s[i] * t[i]; break; //vmul
					FMULS(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				default:
					DISABLE;
				}
				break;
			case 27: //VFPU3				
				// DISABLE
		
				switch ((op >> 23) & 7)	{
				case 2:  // vmin
					FMIN(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 3:  // vmax
					FMAX(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 6:  // vsge
					// DISABLE;  // pending testing
					MOVI2F(FPR6, 1.0f);
					MOVI2F(FPR7, 0.0f);
					FSUBS(FPR8, fpr.V(sregs[i]), fpr.V(tregs[i]));
					FSEL(fpr.V(tempregs[i]), FPR8, FPR6, FPR7);
					break;
				case 7:  // vslt
					// DISABLE;  // pending testing
					MOVI2F(FPR6, 1.0f);
					MOVI2F(FPR7, 0.0f);
					FSUBS(FPR8, fpr.V(sregs[i]), fpr.V(tregs[i]));
					FSEL(fpr.V(tempregs[i]), FPR8, FPR7, FPR6);
					break;
				}
				break;

			default:
				DISABLE;
			}
		}

		for (int i = 0; i < n; i++) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				FMR(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}
		ApplyPrefixD(dregs, sz);
		
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_VV2Op(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		// Pre-processing: Eliminate silly no-op VMOVs, common in Wipeout Pure
		if (((op >> 16) & 0x1f) == 0 && _VS == _VD && js.HasNoPrefix()) {
			return;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		MIPSReg tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		// Warning: sregs[i] and tempxregs[i] may be the same reg.
		// Helps for vmov, hurts for vrcp, etc.
		for (int i = 0; i < n; ++i) {
			switch ((op >> 16) & 0x1f) {
			case 0: // d[i] = s[i]; break; //vmov
				// Probably for swizzle.
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				FMR(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 1: // d[i] = fabsf(s[i]); break; //vabs
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				FABS(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 2: // d[i] = -s[i]; break; //vneg
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				FNEG(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;

			/*  These are probably just as broken as the prefix.
			case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				MOVI2F(S0, 0.5f, R0);
				VABS(S1, fpr.V(sregs[i]));                          // S1 = fabs(x)
				VSUB(fpr.V(tempregs[i]), fpr.V(sregs[i]), S0);     // S2 = fabs(x-0.5f) {VABD}
				VABS(fpr.V(tempregs[i]), fpr.V(tempregs[i]));
				VSUB(fpr.V(tempregs[i]), S1, fpr.V(tempregs[i])); // v[i] = S1 - S2 + 0.5f
				VADD(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S0);
				break;
			case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				MOVI2F(S0, 1.0f, R0);
				VABS(S1, fpr.V(sregs[i]));                          // S1 = fabs(x)
				VSUB(fpr.V(tempregs[i]), fpr.V(sregs[i]), S0);     // S2 = fabs(x-1.0f) {VABD}
				VABS(fpr.V(tempregs[i]), fpr.V(tempregs[i]));
				VSUB(fpr.V(tempregs[i]), S1, fpr.V(tempregs[i])); // v[i] = S1 - S2
				break;
				*/

			case 16: // d[i] = 1.0f / s[i]; break; //vrcp
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				MOVI2F(FPR6, 1.0f);
				FDIVS(fpr.V(tempregs[i]), FPR6, fpr.V(sregs[i]));
				break;
			case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				MOVI2F(FPR6, 1.0f);
				FSQRTS(FPR7, fpr.V(sregs[i]));
				FDIVS(fpr.V(tempregs[i]), FPR6, FPR7);
				break;
			case 18: // d[i] = sinf((float)M_PI_2 * s[i]); break; //vsin
				DISABLE;
				break;
			case 19: // d[i] = cosf((float)M_PI_2 * s[i]); break; //vcos
				DISABLE;
				break;
			case 20: // d[i] = powf(2.0f, s[i]); break; //vexp2
				DISABLE;
				break;
			case 21: // d[i] = logf(s[i])/log(2.0f); break; //vlog2
				DISABLE;
				break;
			case 22: // d[i] = sqrtf(s[i]); break; //vsqrt
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				FSQRTS(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				FABS(fpr.V(tempregs[i]), fpr.V(tempregs[i]));
				break;
			case 23: // d[i] = asinf(s[i] * (float)M_2_PI); break; //vasin
				DISABLE;
				break;
			case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
				fpr.MapDirtyInV(tempregs[i], sregs[i]);
				MOVI2F(FPR6, -1.0f);
				FDIVS(fpr.V(tempregs[i]), FPR6, fpr.V(sregs[i]));
				break;
			case 26: // d[i] = -sinf((float)M_PI_2 * s[i]); break; // vnsin
				DISABLE;
				break;
			case 28: // d[i] = 1.0f / expf(s[i] * (float)M_LOG2E); break; // vrexp2
				DISABLE;
				break;
			default:
				DISABLE;
				break;
			}
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				FMR(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_Mftv(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		int imm = op & 0xFF;
		MIPSGPReg rt = _RT;
		switch ((op >> 21) & 0x1f)
		{
		case 3: //mfv / mfvc
			// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
			if (rt != 0) {
				if (imm < 128) {  //R(rt) = VI(imm);
					fpr.FlushV(imm);
					gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
					LWZ(gpr.R(rt), CTXREG, fpr.GetMipsRegOffsetV(imm));
				} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc
					DISABLE;
					// In case we have a saved prefix.
					//FlushPrefixV();
					//gpr.BindToRegister(rt, false, true);
					//MOV(32, gpr.R(rt), M(&currentMIPS->vfpuCtrl[imm - 128]));
				} else {
					//ERROR - maybe need to make this value too an "interlock" value?
					ERROR_LOG(CPU, "mfv - invalid register %i", imm);
				}
			}
			break;

		case 7: // mtv
			if (imm < 128) {
				gpr.FlushR(rt);
				fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);
				LFS(fpr.V(imm), CTXREG, gpr.GetMipsRegOffset(rt));
			} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc //currentMIPS->vfpuCtrl[imm - 128] = R(rt);
				gpr.MapReg(rt);
				STW(gpr.R(rt), CTXREG, offsetof(MIPSState, vfpuCtrl) + 4 * (imm - 128));
				//gpr.BindToRegister(rt, true, false);
				//MOV(32, M(&currentMIPS->vfpuCtrl[imm - 128]), gpr.R(rt));

				// TODO: Optimization if rt is Imm?
				// Set these BEFORE disable!
				if (imm - 128 == VFPU_CTRL_SPREFIX) {
					js.prefixSFlag = PpcJitState::PREFIX_UNKNOWN;
				} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
					js.prefixTFlag = PpcJitState::PREFIX_UNKNOWN;
				} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
					js.prefixDFlag = PpcJitState::PREFIX_UNKNOWN;
				}
			} else {
				//ERROR
				_dbg_assert_msg_(CPU,0,"mtv - invalid register");
			}
			break;

		default:
			DISABLE;
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_Vmtvc(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		int vs = _VS;
		int imm = op & 0xFF;
		if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
			fpr.MapRegV(vs);
			ADDI(SREG, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + (imm - 128) * 4);
			SFS(fpr.V(vs), SREG, 0);
			fpr.ReleaseSpillLocksAndDiscardTemps();

			if (imm - 128 == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = PpcJitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = PpcJitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = PpcJitState::PREFIX_UNKNOWN;
			}
		}
	}

	void Jit::Comp_Vmmov(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// TODO: This probably ignores prefixes?
		//if (js.MayHavePrefix()) {
		//	DISABLE;
		//}

		if (_VS == _VD) {
			// A lot of these in Wipeout... Just drop the instruction entirely.
			return;	
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 sregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, _VS);
		GetMatrixRegs(dregs, sz, _VD);

		// Rough overlap check.
		bool overlap = false;
		if (GetMtx(_VS) == GetMtx(_VD)) {
			// Potential overlap (guaranteed for 3x3 or more).
			overlap = true;
		}

		if (overlap) {
			// Not so common, fallback.
			DISABLE;
		} else {
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapDirtyInV(dregs[a * 4 + b], sregs[a * 4 + b]);
					FMR(fpr.V(dregs[a * 4 + b]), fpr.V(sregs[a * 4 + b]));
				}
			}
			fpr.ReleaseSpillLocksAndDiscardTemps();
		}
	}

	void Jit::Comp_VScl(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4], treg;
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegs(&treg, V_Single, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		// Move to S0 early, so we don't have to worry about overlap with scale.
		fpr.LoadToRegV(FPR6, treg);

		// For prefixes to work, we just have to ensure that none of the output registers spill
		// and that there's no overlap.
		MIPSReg tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
				// Need to use temp regs
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		// The meat of the function!
		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			FMULS(fpr.V(tempregs[i]), fpr.V(sregs[i]), FPR6);
		}

		for (int i = 0; i < n; i++) {
			// All must be mapped for prefixes to work.
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				FMR(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_Vmmul(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// TODO: This probably ignores prefixes?
		if (js.MayHavePrefix() || disablePrefixes) {
			DISABLE;
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 sregs[16], tregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, _VS);
		GetMatrixRegs(tregs, sz, _VT);
		GetMatrixRegs(dregs, sz, _VD);

		// Rough overlap check.
		bool overlap = false;
		if (GetMtx(_VS) == GetMtx(_VD) || GetMtx(_VT) == GetMtx(_VD)) {
			// Potential overlap (guaranteed for 3x3 or more).
			overlap = true;
		}

		if (overlap) {
			DISABLE;
		} else {
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapInInV(sregs[b * 4], tregs[a * 4]);
					FMULS(FPR6, fpr.V(sregs[b * 4]), fpr.V(tregs[a * 4]));
					for (int c = 1; c < n; c++) {
						fpr.MapInInV(sregs[b * 4 + c], tregs[a * 4 + c]);
						FMADDS(FPR6, fpr.V(sregs[b * 4 + c]), fpr.V(tregs[a * 4 + c]), FPR6);
					}
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					FMR(fpr.V(dregs[a * 4 + b]), FPR6);
				}
			}
			fpr.ReleaseSpillLocksAndDiscardTemps();
		}
	}

	void Jit::Comp_Vmscl(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Vtfm(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		// TODO: This probably ignores prefixes?  Or maybe uses D?
		if (js.MayHavePrefix() || disablePrefixes) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		MatrixSize msz = GetMtxSize(op);
		int n = GetNumVectorElements(sz);
		int ins = (op >> 23) & 7;

		bool homogenous = false;
		if (n == ins) {
			n++;
			sz = (VectorSize)((int)(sz) + 1);
			msz = (MatrixSize)((int)(msz) + 1);
			homogenous = true;
		}
		// Otherwise, n should already be ins + 1.
		else if (n != ins + 1) {
			DISABLE;
		}

		u8 sregs[16], dregs[4], tregs[4];
		GetMatrixRegs(sregs, msz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		// TODO: test overlap, optimize.
		int tempregs[4];
		for (int i = 0; i < n; i++) {
			fpr.MapInInV(sregs[i * 4], tregs[0]);
			FMULS(FPR6, fpr.V(sregs[i * 4]), fpr.V(tregs[0]));
			for (int k = 1; k < n; k++) {
				if (!homogenous || k != n - 1) {
					fpr.MapInInV(sregs[i * 4 + k], tregs[k]);
					FMADDS(FPR6, fpr.V(sregs[i * 4 + k]), fpr.V(tregs[k]), FPR6);
				} else {
					fpr.MapRegV(sregs[i * 4 + k]);
					FADDS(FPR6, FPR6, fpr.V(sregs[i * 4 + k]));
				}
			}

			int temp = fpr.GetTempV();
			fpr.MapRegV(temp, MAP_NOINIT | MAP_DIRTY);
			fpr.SpillLockV(temp);
			FMR(fpr.V(temp), FPR6);
			tempregs[i] = temp;
		}
		for (int i = 0; i < n; i++) {
			u8 temp = tempregs[i];
			fpr.MapRegV(dregs[i], MAP_NOINIT | MAP_DIRTY);
			FMR(fpr.V(dregs[i]), fpr.V(temp));
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_VHdp(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_VCrs(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_VDet(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Vi2x(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Vx2i(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Vf2i(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Vi2f(MIPSOpcode op) {
		//DISABLE;
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix() || disablePrefixes)
			DISABLE;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		int imm = (op >> 16) & 0x1f;
		const float mult = 1.0f / (float)(1UL << imm);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		MIPSReg tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		if (mult != 1.0f)
			MOVI2F(FPR5, mult, false);
		
		u64 tmp = 0;
		MOVI2R(SREG, (u32)&tmp);
		
		//Break();

		for (int i = 0; i < n; i++) {
			// Crappy code !!
			fpr.MapDirtyInV(tempregs[i], sregs[i]);

			// float => mem
			SFS(fpr.V(sregs[i]), SREG, 0);

			// int <= mem
			LWZ(R6, SREG, 0);
			//RLDICL(R6, R6, 0, 23);
			EXTSW(R6, R6);

			// int => mem
			STD(R6, SREG, 0);

			// float <= mem
			LFD(fpr.V(tempregs[i]), SREG, 0);
			
			FCFID(fpr.V(tempregs[i]), fpr.V(tempregs[i]));			
			FRSP(fpr.V(tempregs[i]), fpr.V(tempregs[i]));

			if (mult != 1.0f) 
				FMULS(fpr.V(tempregs[i]), fpr.V(tempregs[i]), FPR5);
		}
		
		//Break();

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				FMR(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_Vh2f(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Vcst(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		int conNum = (op >> 16) & 0x1f;
		int vd = _VD;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT | MAP_DIRTY);

		MOVI2R(SREG, (u32)(void *)&cst_constants[conNum]);
		LFS(FPR6, SREG, 0);
		for (int i = 0; i < n; ++i)
			FMR(fpr.V(dregs[i]), FPR6);

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_Vhoriz(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_VRot(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_VIdt(MIPSOpcode op) {
		CONDITIONAL_DISABLE

		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		MOVI2F(FPR6, 0.0f);
		MOVI2F(FPR7, 1.0f);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT | MAP_DIRTY);
		switch (sz)
		{
		case V_Pair:
			FMR(fpr.V(dregs[0]), (vd&1)==0 ? FPR7 : FPR6);
			FMR(fpr.V(dregs[1]), (vd&1)==1 ? FPR7 : FPR6);
			break;								   
		case V_Quad:							   
			FMR(fpr.V(dregs[0]), (vd&3)==0 ? FPR7 : FPR6);
			FMR(fpr.V(dregs[1]), (vd&3)==1 ? FPR7 : FPR6);
			FMR(fpr.V(dregs[2]), (vd&3)==2 ? FPR7 : FPR6);
			FMR(fpr.V(dregs[3]), (vd&3)==3 ? FPR7 : FPR6);
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		
		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_Vcmp(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Vcmov(MIPSOpcode op) {
		DISABLE;
	}

	void Jit::Comp_Viim(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		u8 dreg;
		GetVectorRegs(&dreg, V_Single, _VT);

		s32 imm = (s32)(s16)(u16)(op & 0xFFFF);
		fpr.MapRegV(dreg, MAP_DIRTY | MAP_NOINIT);
		MOVI2F(fpr.V(dreg), (float)imm);

		ApplyPrefixD(&dreg, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_Vfim(MIPSOpcode op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		u8 dreg;
		GetVectorRegs(&dreg, V_Single, _VT);

		FP16 half;
		half.u = op & 0xFFFF;
		FP32 fval = half_to_float_fast5(half);
		fpr.MapRegV(dreg, MAP_DIRTY | MAP_NOINIT);
		MOVI2F(fpr.V(dreg), fval.f);

		ApplyPrefixD(&dreg, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Jit::Comp_VCrossQuat(MIPSOpcode op) {
		DISABLE;
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix() || disablePrefixes) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegs(sregs, sz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		// Map everything into registers.
		fpr.MapRegsAndSpillLockV(sregs, sz, 0);
		fpr.MapRegsAndSpillLockV(tregs, sz, 0);

		if (sz == V_Triple) {
			int temp3 = fpr.GetTempV();
			fpr.MapRegV(temp3, MAP_DIRTY | MAP_NOINIT);
			// Cross product vcrsp.t

			// Compute X
			FMULS(FPR6, fpr.V(sregs[1]), fpr.V(tregs[2]));
			FMSUBS(FPR6, fpr.V(sregs[2]), fpr.V(tregs[1]), FPR6);

			// Compute Y
			FMULS(FPR7, fpr.V(sregs[2]), fpr.V(tregs[0]));
			FMSUBS(FPR7, fpr.V(sregs[0]), fpr.V(tregs[2]), FPR7);

			// Compute Z
			FMULS(fpr.V(temp3), fpr.V(sregs[0]), fpr.V(tregs[1]));
			FMSUBS(fpr.V(temp3), fpr.V(sregs[1]), fpr.V(tregs[0]), fpr.V(temp3));

			fpr.MapRegsAndSpillLockV(dregs, V_Triple, MAP_DIRTY | MAP_NOINIT);
			FMR(fpr.V(dregs[0]), FPR6);
			FMR(fpr.V(dregs[1]), FPR7);
			FMR(fpr.V(dregs[2]), fpr.V(temp3));
		} else if (sz == V_Quad) {
			// Quaternion product  vqmul.q  untested
			DISABLE;
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}
	void Jit::Comp_Vsgn(MIPSOpcode op) {
		DISABLE;
	}
	void Jit::Comp_Vocp(MIPSOpcode op) {
		DISABLE;
	}
}


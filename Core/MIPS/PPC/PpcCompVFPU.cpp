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

#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

using namespace PpcGen;


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

	void Jit::Comp_SV(u32 op) {
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

	void Jit::Comp_SVQ(u32 op) {
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

	void Jit::Comp_VPFX(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VVectorInit(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VMatrixInit(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VDot(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VecDo3(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VV2Op(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Mftv(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vmtvc(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vmmov(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VScl(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vmmul(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vmscl(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vtfm(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VHdp(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VCrs(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VDet(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vi2x(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vx2i(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vf2i(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vi2f(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vcst(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vhoriz(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VRot(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VIdt(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vcmp(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vcmov(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Viim(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_Vfim(u32 op) {
		Comp_Generic(op);
	}

	void Jit::Comp_VCrossQuat(u32 op) {
		Comp_Generic(op);
	}
	void Jit::Comp_Vsge(u32 op) {
		Comp_Generic(op);
	}
	void Jit::Comp_Vslt(u32 op) {
		Comp_Generic(op);
	}
}
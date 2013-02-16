#include "../../MemMap.h"
#include "../MIPSAnalyst.h"

#include "ArmJit.h"
#include "ArmRegCache.h"


#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

// #define CONDITIONAL_DISABLE Comp_Generic(op); return;
#define CONDITIONAL_DISABLE ;
#define DISABLE Comp_Generic(op); return;

namespace MIPSComp
{
	void Jit::Comp_VPFX(u32 op)
	{
		CONDITIONAL_DISABLE;

		int data = op & 0xFFFFF;
		int regnum = (op >> 24) & 3;
		switch (regnum) {
		case 0:  // S
			js.prefixS = data;
			js.prefixSFlag = ArmJitState::PREFIX_KNOWN_DIRTY;
			break;
		case 1:  // T
			js.prefixT = data;
			js.prefixTFlag = ArmJitState::PREFIX_KNOWN_DIRTY;
			break;
		case 2:  // D
			js.prefixD = data;
			js.prefixDFlag = ArmJitState::PREFIX_KNOWN_DIRTY;
			break;
		}
	}

	void Jit::Comp_SV(u32 op) {
		CONDITIONAL_DISABLE;

		s32 imm = (signed short)(op&0xFFFC);
		int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
		int rs = _RS;

		switch (op >> 26)
		{
		case 50: //lv.s  // VI(vt) = Memory::Read_U32(addr);
			{
				gpr.MapReg(rs);
				SetR0ToEffectiveAddress(rs, imm);
				ADD(R0, R0, R11);
				fpr.MapRegV(vt, MAP_DIRTY | MAP_NOINIT);
				fpr.ReleaseSpillLocks();
				VLDR(fpr.V(vt), R0, 0);
			}
			break;

		case 58: //sv.s   // Memory::Write_U32(VI(vt), addr);
			{
				gpr.MapReg(rs);
				SetR0ToEffectiveAddress(rs, imm);
				ADD(R0, R0, R11);
				fpr.MapRegV(vt);
				fpr.ReleaseSpillLocks();
				VSTR(fpr.V(vt), R0, 0);
			}
			break;


		default:
			DISABLE;
		}
	}

	void Jit::Comp_SVQ(u32 op)
	{
		CONDITIONAL_DISABLE;

		int imm = (signed short)(op&0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);
		int rs = _RS;

		switch (op >> 26)
		{
		case 54: //lv.q
			{
				gpr.MapReg(rs);
				SetR0ToEffectiveAddress(rs, imm);
				ADD(R0, R0, R11);

				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);
				fpr.ReleaseSpillLocks();
				for (int i = 0; i < 4; i++)
					VLDR(fpr.V(vregs[i]), R0, i * 4);
			}
			break;

		case 62: //sv.q
			{
				gpr.MapReg(rs);
				SetR0ToEffectiveAddress(rs, imm);
				ADD(R0, R0, R11);

				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsV(vregs, V_Quad, 0);
				fpr.ReleaseSpillLocks();
				for (int i = 0; i < 4; i++)
					VSTR(fpr.V(vregs[i]), R0, i * 4);
			}
			break;

		default:
			DISABLE;
			break;
		}
	}

	void Jit::Comp_VDot(u32 op)
	{
		// DISABLE;
		CONDITIONAL_DISABLE;
		// WARNING: No prefix support!
		if (js.MayHavePrefix()) {
			Comp_Generic(op);
			js.EatPrefix();
			return;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4];
		GetVectorRegs(sregs, sz, vs);
		GetVectorRegs(tregs, sz, vt);

		// TODO: applyprefixST here somehow (shuffle, etc...)
		fpr.MapRegsV(sregs, sz, 0);
		fpr.MapRegsV(tregs, sz, 0);
		VMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[0]));

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++)
		{
			// sum += s[i]*t[i];
			VMUL(S1, fpr.V(sregs[i]), fpr.V(tregs[i]));
			VADD(S0, S0, S1);
		}
		fpr.ReleaseSpillLocks();

		fpr.MapRegV(vd, MAP_NOINIT | MAP_DIRTY);

		// TODO: applyprefixD here somehow (write mask etc..)
		VMOV(fpr.V(vd), S0);

		fpr.ReleaseSpillLocks();

		js.EatPrefix();
	}

	void Jit::Comp_VecDo3(u32 op)
	{
		DISABLE;  // Still buggy

		// WARNING: No prefix support!
		if (js.MayHavePrefix())
		{
			Comp_Generic(op);
			js.EatPrefix();
			return;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegs(sregs, sz, vs);
		GetVectorRegs(tregs, sz, vt);
		GetVectorRegs(dregs, sz, vd);

		void (ARMXEmitter::*triop)(ARMReg, ARMReg, ARMReg) = NULL;
		switch (op >> 26)
		{
		case 24: //VFPU0
			switch ((op >> 23)&7)
			{
			case 0: // d[i] = s[i] + t[i]; break; //vadd
				triop = &ARMXEmitter::VADD;
				break;
			case 1: // d[i] = s[i] - t[i]; break; //vsub
				triop = &ARMXEmitter::VSUB;
				break;
			case 7: // d[i] = s[i] / t[i]; break; //vdiv
				triop = &ARMXEmitter::VDIV;
				break;
			}
			break;
		case 25: //VFPU1
			switch ((op >> 23)&7)
			{
			case 0: // d[i] = s[i] * t[i]; break; //vmul
				triop = &ARMXEmitter::VMUL;
				break;
			}
			break;
		}

		if (triop == NULL)
		{
			Comp_Generic(op);
			js.EatPrefix();
			return;
		}

		int n = GetNumVectorElements(sz);
		fpr.MapRegsV(sregs, sz, 0);
		fpr.MapRegsV(tregs, sz, 0);
		fpr.MapReg(TEMP1);
		fpr.MapReg(TEMP2);
		fpr.MapReg(TEMP3);

		for (int i = 0; i < n; ++i) {
			fpr.MapReg(TEMP0 + i);
			(this->*triop)(fpr.R(TEMP0 + i), fpr.V(sregs[i]), fpr.V(tregs[i]));
			fpr.ReleaseSpillLock(sregs[i]);
			fpr.ReleaseSpillLock(tregs[i]);
		}
		fpr.MapRegsV(dregs, sz, MAP_DIRTY | MAP_NOINIT);
		// TODO: Can avoid this when no overlap
		for (int i = 0; i < n; i++) {
			VMOV(fpr.V(dregs[i]), fpr.R(TEMP0 + i));
		}
		fpr.ReleaseSpillLocks();

		js.EatPrefix();
	}

	void Jit::Comp_Mftv(u32 op)
	{
		// DISABLE;
		CONDITIONAL_DISABLE;

		int imm = op & 0xFF;
		int rt = _RT;
		switch ((op >> 21) & 0x1f)
		{
		case 3: //mfv / mfvc
			// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
			if (rt != 0) {
				if (imm < 128) {  //R(rt) = VI(imm);
					fpr.FlushV(imm);
					gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
					LDR(gpr.R(rt), CTXREG, fpr.GetMipsRegOffsetV(imm));
				} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc
					DISABLE;
					// In case we have a saved prefix.
					//FlushPrefixV();
					//gpr.BindToRegister(rt, false, true);
					//MOV(32, gpr.R(rt), M(&currentMIPS->vfpuCtrl[imm - 128]));
				} else {
					//ERROR - maybe need to make this value too an "interlock" value?
					_dbg_assert_msg_(CPU,0,"mfv - invalid register");
				}
			}
			break;

		case 7: //mtv
			if (imm < 128) {
				gpr.FlushR(rt);
				fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);
				VLDR(fpr.V(imm), CTXREG, gpr.GetMipsRegOffset(rt));
			} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc //currentMIPS->vfpuCtrl[imm - 128] = R(rt);
				DISABLE; 
				//gpr.BindToRegister(rt, true, false);
				//MOV(32, M(&currentMIPS->vfpuCtrl[imm - 128]), gpr.R(rt));

				// TODO: Optimization if rt is Imm?
				//if (imm - 128 == VFPU_CTRL_SPREFIX) {
				//js.prefixSFlag = JitState::PREFIX_UNKNOWN;
				//} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				//	js.prefixTFlag = JitState::PREFIX_UNKNOWN;
				//} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				//	js.prefixDFlag = JitState::PREFIX_UNKNOWN;
				//}
			} else {
				//ERROR
				_dbg_assert_msg_(CPU,0,"mtv - invalid register");
			}
			break;

		default:
			DISABLE;
		}
	}

	void Jit::Comp_Vmtvc(u32 op) {
		DISABLE;

		int vs = _VS;
		int imm = op & 0xFF;
		if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
			fpr.MapRegV(vs, 0);
			ADD(R0, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + (imm - 128) * 4);
			VSTR(fpr.V(vs), R0, 0);
			fpr.ReleaseSpillLocks();

			if (imm - 128 == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = ArmJitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = ArmJitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = ArmJitState::PREFIX_UNKNOWN;
			}
		}
	}

}

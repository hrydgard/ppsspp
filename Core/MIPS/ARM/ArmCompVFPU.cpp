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

	void Jit::Comp_SVQ(u32 op)
	{
		DISABLE;

		int imm = (signed short)(op&0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);
		int rs = _RS;

		switch (op >> 26)
		{
		case 54: //lv.q
			{
				gpr.MapReg(rs);
				SetR0ToEffectiveAddress(rs, imm);
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);
				fpr.ReleaseSpillLocks();
				// Just copy 4 words the easiest way while not wasting registers.
				for (int i = 0; i < 4; i++)
					VLDR(fpr.V(vregs[i]), R0, i * 4);
			}
			break;

		case 62: //sv.q
			{
				gpr.MapReg(rs);
				SetR0ToEffectiveAddress(rs, imm);

				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				// Even if we don't use real SIMD there's still 8 or 16 scalar float registers.
				fpr.MapRegsV(vregs, V_Quad, 0);
				fpr.ReleaseSpillLocks();
				// Just copy 4 words the easiest way while not wasting registers.
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
		DISABLE;
	}

	void Jit::Comp_VecDo3(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_Mftv(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_SV(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vmtvc(u32 op) {
		DISABLE;
	}

}

#include "Common/ChunkFile.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/MIPSTables.h"

#include "PpcRegCache.h"
#include "ppcEmitter.h"
#include "PpcJit.h"


#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS	((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

//#define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

using namespace PpcGen;

namespace MIPSComp
{

void Jit::SetRegToEffectiveAddress(PpcGen::PPCReg r, int rs, s16 offset) {
	if (offset) {
		ADDI(SREG, gpr.R(rs), offset);
		RLWINM(SREG, SREG, 0, 2, 31); // &= 0x3FFFFFFF
	} else {
		RLWINM(SREG, gpr.R(rs), 0, 2, 31); // &= 0x3FFFFFFF
	}
		
}
void Jit::Comp_ITypeMem(MIPSOpcode op) {
	CONDITIONAL_DISABLE;	

		int offset = (signed short)(op&0xFFFF);
		bool load = false;
		int rt = _RT;
		int rs = _RS;
		int o = op>>26;
		if (((op >> 29) & 1) == 0 && rt == 0) {
			// Don't load anything into $zr
			return;
		}

		if (!g_Config.bFastMemory) {
			DISABLE;
		}

		u32 iaddr = gpr.IsImm(rs) ? offset + gpr.GetImm(rs) : 0xFFFFFFFF;
		bool doCheck = false;

		switch (o)
		{
		case 32: //lb
		case 33: //lh
		case 35: //lw
		case 36: //lbu
		case 37: //lhu
			load = true;
		case 40: //sb
		case 41: //sh
		case 43: //sw

			if (gpr.IsImm(rs) && Memory::IsValidAddress(iaddr)) {
				// We can compute the full address at compile time. Kickass.
				u32 addr = iaddr & 0x3FFFFFFF;
				// Must be OK even if rs == rt since we have the value from imm already.
				gpr.MapReg(rt, load ? MAP_NOINIT | MAP_DIRTY : 0);
				MOVI2R(SREG, addr);
			} else {
				_dbg_assert_msg_(JIT, !gpr.IsImm(rs), "Invalid immediate address?  CPU bug?");
				load ? gpr.MapDirtyIn(rt, rs) : gpr.MapInIn(rt, rs);
				
				SetRegToEffectiveAddress(SREG, rs, offset);
			}
			switch (o)
			{
			// Load
			case 32:  //lb
				LBZX(gpr.R(rt), BASEREG, SREG); 
				EXTSB(gpr.R(rt), gpr.R(rt));
				break;
			case 33:  //lh
				LHBRX(gpr.R(rt), BASEREG, SREG); 
				EXTSH(gpr.R(rt), gpr.R(rt));
				break;
			case 35: //lw
				LWBRX(gpr.R(rt), BASEREG, SREG); 
				break;
			case 36: //lbu
				LBZX (gpr.R(rt), BASEREG, SREG); 
				break;
			case 37: //lhu
				LHBRX (gpr.R(rt), BASEREG, SREG); 
				break;
			// Store
			case 40:  //sb
				STBX (gpr.R(rt), BASEREG, SREG); 
				break;
			case 41: //sh
				STHBRX(gpr.R(rt), BASEREG, SREG); 		
				break;
			case 43: //sw
				STWBRX(gpr.R(rt), BASEREG, SREG); 
				break;
			}
			break;
		case 34: //lwl
		case 38: //lwr
			load = true;
		case 42: //swl
		case 46: //swr
			if (!js.inDelaySlot) {
				// Optimisation: Combine to single unaligned load/store
				bool isLeft = (o == 34 || o == 42);
				MIPSOpcode nextOp = Memory::Read_Instruction(js.compilerPC + 4);
				// Find a matching shift in opposite direction with opposite offset.
				if (nextOp == (isLeft ? (op.encoding + (4<<26) - 3)
				                      : (op.encoding - (4<<26) + 3)))
				{
					EatInstruction(nextOp);
					nextOp = MIPSOpcode(((load ? 35 : 43) << 26) | ((isLeft ? nextOp : op) & 0x03FFFFFF)); //lw, sw
					Comp_ITypeMem(nextOp);
					return;
				}
			}

			DISABLE; // Disabled until crashes are resolved.
			break;
		default:
			Comp_Generic(op);
			return ;
		}
	}
}

void Jit::Comp_Cache(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	// TODO: Could use this as a hint, and technically required to handle icache, etc.
	// But right now Int_Cache does nothing, so let's not even call it.
}

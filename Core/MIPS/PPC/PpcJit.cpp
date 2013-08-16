#include "PpcJit.h"

namespace MIPSComp
{
	Jit * jit=NULL;

	void Jit::Comp_Generic(u32 op) {

	}
	
	void Jit::EatInstruction(u32 op){}
	void Jit::Comp_RunBlock(u32 op){}
	void Jit::Comp_ITypeMem(u32 op){}

	void Jit::Comp_RelBranch(u32 op){}
	void Jit::Comp_RelBranchRI(u32 op){}
	void Jit::Comp_FPUBranch(u32 op){}
	void Jit::Comp_FPULS(u32 op){}
	void Jit::Comp_FPUComp(u32 op){}
	void Jit::Comp_Jump(u32 op){}
	void Jit::Comp_JumpReg(u32 op){}
	void Jit::Comp_Syscall(u32 op){}
	void Jit::Comp_Break(u32 op){}

	void Jit::Comp_IType(u32 op){}
	void Jit::Comp_RType2(u32 op){}
	void Jit::Comp_RType3(u32 op){}
	void Jit::Comp_ShiftType(u32 op){}
	void Jit::Comp_Allegrex(u32 op){}
	void Jit::Comp_Allegrex2(u32 op){}
	void Jit::Comp_VBranch(u32 op){}
	void Jit::Comp_MulDivType(u32 op){}
	void Jit::Comp_Special3(u32 op){}

	void Jit::Comp_FPU3op(u32 op){}
	void Jit::Comp_FPU2op(u32 op){}
	void Jit::Comp_mxc1(u32 op){}

	void Jit::Comp_DoNothing(u32 op){}

	void Jit::Comp_SV(u32 op){}
	void Jit::Comp_SVQ(u32 op){}
	void Jit::Comp_VPFX(u32 op){}
	void Jit::Comp_VVectorInit(u32 op){}
	void Jit::Comp_VMatrixInit(u32 op){}
	void Jit::Comp_VDot(u32 op){}
	void Jit::Comp_VecDo3(u32 op){}
	void Jit::Comp_VV2Op(u32 op){}
	void Jit::Comp_Mftv(u32 op){}
	void Jit::Comp_Vmtvc(u32 op){}
	void Jit::Comp_Vmmov(u32 op){}
	void Jit::Comp_VScl(u32 op){}
	void Jit::Comp_Vmmul(u32 op){}
	void Jit::Comp_Vmscl(u32 op){}
	void Jit::Comp_Vtfm(u32 op){}
	void Jit::Comp_VHdp(u32 op){}
	void Jit::Comp_VCrs(u32 op){}
	void Jit::Comp_VDet(u32 op){}
	void Jit::Comp_Vi2x(u32 op){}
	void Jit::Comp_Vx2i(u32 op){}
	void Jit::Comp_Vf2i(u32 op){}
	void Jit::Comp_Vi2f(u32 op){}
	void Jit::Comp_Vcst(u32 op){}
	void Jit::Comp_Vhoriz(u32 op){}
	void Jit::Comp_VRot(u32 op){}
	void Jit::Comp_VIdt(u32 op){}

	void Jit::ClearCache(){}
	void Jit::ClearCacheAt(u32 em_address){}
}
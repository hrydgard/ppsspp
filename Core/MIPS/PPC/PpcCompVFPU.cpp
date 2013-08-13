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

#include <ppcintrinsics.h>

using namespace PpcGen;


namespace MIPSComp
{
void Jit::Comp_SV(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_SVQ(u32 op) {
	Comp_Generic(op);
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
}
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

extern volatile CoreState coreState;

namespace MIPSComp
{

void Jit::Comp_IType(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_RType2(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_RType3(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_ShiftType(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_Allegrex(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_Allegrex2(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_MulDivType(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_Special3(u32 op) {
	Comp_Generic(op);
}

}
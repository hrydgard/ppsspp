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

void Jit::Comp_FPULS(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_FPUComp(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_FPU3op(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_FPU2op(u32 op) {
	Comp_Generic(op);
}

void Jit::Comp_mxc1(u32 op) {
	Comp_Generic(op);
}

}
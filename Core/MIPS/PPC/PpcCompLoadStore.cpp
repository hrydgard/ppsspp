#include "Common/ChunkFile.h"
#include "../../Core.h"
#include "../../CoreTiming.h"
#include "../MIPS.h"
#include "../MIPSCodeUtils.h"
#include "../MIPSInt.h"
#include "../MIPSTables.h"

#include "PpcRegCache.h"
#include "ppcEmitter.h"
#include "PpcJit.h"

#include <ppcintrinsics.h>

using namespace PpcGen;

namespace MIPSComp
{

void Jit::Comp_ITypeMem(u32 op) {
	Comp_Generic(op);
}

}
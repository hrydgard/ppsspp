#pragma once

#include "Common/CommonTypes.h"

class MIPSState;
struct IRInst;

namespace MIPSComp {
	class IRFrontend;
}

// TODO: Find a way to get rid of the frontend parameter (need a callback for rounding)
u32 IRInterpret(MIPSState *mips, const IRInst *inst, const u32 *constPool, int count, MIPSComp::IRFrontend *frontend);

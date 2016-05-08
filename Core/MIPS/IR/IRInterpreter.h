#pragma once

#include "Common/CommonTypes.h"

class MIPSState;
struct IRInst;

u32 IRInterpret(MIPSState *mips, const IRInst *inst, const u32 *constPool, int count);

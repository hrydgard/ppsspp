#pragma once

#include "Core/MIPS/IR/IRInst.h"

// Dumb example of a simplification pass that can't add or remove instructions.
void SimplifyInPlace(IRInst *inst, int count, const u32 *constPool);

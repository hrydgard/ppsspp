#pragma once

#include "Core/MIPS/IR/IRInst.h"

void SimplifyInPlace(IRInst *inst, int count, const u32 *constPool);

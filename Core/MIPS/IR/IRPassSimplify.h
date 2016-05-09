#pragma once

#include "Core/MIPS/IR/IRInst.h"

typedef bool (*IRPassFunc)(const IRWriter &in, IRWriter &out);
bool IRApplyPasses(const IRPassFunc *passes, size_t c, const IRWriter &in, IRWriter &out);

bool PropagateConstants(const IRWriter &in, IRWriter &out);
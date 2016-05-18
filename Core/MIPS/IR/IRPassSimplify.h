#pragma once

#include "Core/MIPS/IR/IRInst.h"

typedef bool (*IRPassFunc)(const IRWriter &in, IRWriter &out);
bool IRApplyPasses(const IRPassFunc *passes, size_t c, const IRWriter &in, IRWriter &out);

// Block optimizer passes of varying usefulness.
bool PropagateConstants(const IRWriter &in, IRWriter &out);
bool PurgeTemps(const IRWriter &in, IRWriter &out);
bool ReduceLoads(const IRWriter &in, IRWriter &out);
bool ThreeOpToTwoOp(const IRWriter &in, IRWriter &out);
bool OptimizeFPMoves(const IRWriter &in, IRWriter &out);
bool ReorderLoadStore(const IRWriter &in, IRWriter &out);
bool MergeLoadStore(const IRWriter &in, IRWriter &out);

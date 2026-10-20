#pragma once

#include "Core/MIPS/IR/IRInst.h"

typedef bool (*IRPassFunc)(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool IRApplyPasses(const IRPassFunc *passes, size_t c, const IRWriter &in, IRWriter &out, const IROptions &opts);

// Block optimizer passes of varying usefulness.
bool RemoveLoadStoreLeftRight(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool PropagateConstants(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool PurgeTemps(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool ReduceLoads(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool ThreeOpToTwoOp(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool OptimizeFPMoves(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool ReorderLoadStore(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool MergeLoadStore(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool ApplyMemoryValidation(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool ReduceVec4Flush(const IRWriter &in, IRWriter &out, const IROptions &opts);

bool OptimizeLoadsAfterStores(const IRWriter &in, IRWriter &out, const IROptions &opts);
// Kernel code's loads and stores at a constant address become plain ones where that's memory, so only
// hardware registers (and addresses only known when it runs) stay LoadKernel*/StoreKernel*.
bool ResolveKernelMemory(const IRWriter &in, IRWriter &out, const IROptions &opts);
bool OptimizeForInterpreter(const IRWriter &in, IRWriter &out, const IROptions &opts);

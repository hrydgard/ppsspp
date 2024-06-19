#pragma once

#include "Common/CommonTypes.h"
#include "Core/Core.h"
#include "Core/MemMap.h"

class MIPSState;
struct IRInst;

u32 IRRunBreakpoint(u32 pc);
u32 IRRunMemCheck(u32 pc, u32 addr);
u32 IRInterpret(MIPSState *ms, const IRInst *inst);

void IRApplyRounding();
void IRRestoreRounding();

template <uint32_t alignment>
u32 RunValidateAddress(u32 pc, u32 addr, u32 isWrite) {
	const auto toss = [&](MemoryExceptionType t) {
		Core_MemoryException(addr, alignment, pc, t);
		return coreState != CORE_RUNNING ? 1 : 0;
	};

	if (!Memory::IsValidRange(addr, alignment)) {
		MemoryExceptionType t = isWrite == 1 ? MemoryExceptionType::WRITE_WORD : MemoryExceptionType::READ_WORD;
		if constexpr (alignment > 4)
			t = isWrite ? MemoryExceptionType::WRITE_BLOCK : MemoryExceptionType::READ_BLOCK;
		return toss(t);
	}
	if constexpr (alignment > 1)
		if ((addr & (alignment - 1)) != 0)
			return toss(MemoryExceptionType::ALIGNMENT);
	return 0;
}

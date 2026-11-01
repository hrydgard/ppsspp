#pragma once

#include "Common/CommonTypes.h"
#include "Core/Core.h"
#include "Core/MemMap.h"

class MIPSState;
struct IRInst;

u32 IRRunBreakpoint(u32 pc);
u32 IRRunMemCheck(u32 pc, u32 addr);
// A LoadKernel*/StoreKernel* (op) to an address the native JITs don't access directly: a hardware
// register, or not memory at all.
u32 IRRunKernelLoad(MIPSState *mips, u32 addr, u32 op);
void IRRunKernelStore(MIPSState *mips, u32 addr, u32 value, u32 op);

template <uint32_t alignment>
u32 RunValidateAddress(u32 pc, u32 addr, u32 isWrite) {
	static_assert(alignment <= 16);
	const auto toss = [&](MemoryExceptionType t) {
		Core_MemoryException(addr, alignment, pc, t);
		return coreState != CORE_RUNNING_CPU ? 1 : 0;
	};

	if (!Memory::IsValidNAlignedAddress<alignment>(addr)) {
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

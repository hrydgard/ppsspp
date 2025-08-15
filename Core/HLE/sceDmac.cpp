// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/HLE/sceDmac.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/Debugger/Breakpoints.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

u64 dmacMemcpyDeadline;

void __DmacInit() {
	dmacMemcpyDeadline = 0;
}

void __DmacDoState(PointerWrap &p) {
	auto s = p.Section("sceDmac", 0, 1);
	if (s == 0) {
		dmacMemcpyDeadline = 0;
		return;
	}

	Do(p, dmacMemcpyDeadline);
}

static int __DmacMemcpy(u32 dst, u32 src, u32 size) {
	bool skip = false;
	if (Memory::IsVRAMAddress(src) || Memory::IsVRAMAddress(dst)) {
		skip = gpu->PerformMemoryCopy(dst, src, size);
	}
	if (!skip && size != 0) {
		currentMIPS->InvalidateICache(src, size);
		if (Memory::IsValidRange(dst, size) && Memory::IsValidRange(src, size)) {
			memcpy(Memory::GetPointerWriteUnchecked(dst), Memory::GetPointerUnchecked(src), size);
		}
		if (MemBlockInfoDetailed(size)) {
			NotifyMemInfoCopy(dst, src, size, "DmacMemcpy/");
		}
		currentMIPS->InvalidateICache(dst, size);
	}

	// This number seems strangely reproducible.
	if (size >= 272) {
		// Approx. 225 MiB/s or 235929600 B/s, so let's go with 236 B/us.
		int delayUs = size / 236;
		dmacMemcpyDeadline = CoreTiming::GetTicks() + usToCycles(delayUs);
		return delayUs;
	} else {
		return 0;
	}
}

static u32 sceDmacMemcpy(u32 dst, u32 src, u32 size) {
	if (size == 0) {
		// Some games seem to do this frequently.
		return hleLogDebug(Log::HLE, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid size");
	}
	if (!Memory::IsValidAddress(dst) || !Memory::IsValidAddress(src)) {
		return hleLogError(Log::HLE, SCE_KERNEL_ERROR_INVALID_POINTER, "invalid address (dst or src)");
	}
	if (dst + size >= 0x80000000 || src + size >= 0x80000000 || size >= 0x80000000) {
		return hleLogError(Log::HLE, SCE_KERNEL_ERROR_PRIV_REQUIRED, "illegal size");
	}

	if (dmacMemcpyDeadline > CoreTiming::GetTicks()) {
		WARN_LOG(Log::HLE, "sceDmacMemcpy(dest=%08x, src=%08x, size=%d): overlapping read", dst, src, size);
		// TODO: Should block, seems like copy doesn't start until previous finishes.
		// Might matter for overlapping copies.
	}

	int delay = __DmacMemcpy(dst, src, size);
	int result = hleLogDebug(Log::HLE, 0);
	return delay ? hleDelayResult(result, "dmac-memcpy", delay) : delay;
}

static u32 sceDmacTryMemcpy(u32 dst, u32 src, u32 size) {
	if (size == 0) {
		return hleLogError(Log::HLE, SCE_KERNEL_ERROR_INVALID_SIZE, "invalid size");
	}
	if (!Memory::IsValidAddress(dst) || !Memory::IsValidAddress(src)) {
		return hleLogError(Log::HLE, SCE_KERNEL_ERROR_INVALID_POINTER, "invalid address");
	}
	if (dst + size >= 0x80000000 || src + size >= 0x80000000 || size >= 0x80000000) {
		return hleLogError(Log::HLE, SCE_KERNEL_ERROR_PRIV_REQUIRED, "illegal size");
	}

	if (dmacMemcpyDeadline > CoreTiming::GetTicks()) {
		return hleLogDebug(Log::HLE, SCE_KERNEL_ERROR_BUSY, "busy");
	}

	int delay = __DmacMemcpy(dst, src, size);
	int result = hleLogDebug(Log::HLE, 0);
	return delay ? hleDelayResult(result, "dmac-memcpy", delay) : delay;
}

const HLEFunction sceDmac[] = {
	{0X617F3FE6, &WrapU_UUU<sceDmacMemcpy>,          "sceDmacMemcpy",    'x', "xxx"},
	{0XD97F94D8, &WrapU_UUU<sceDmacTryMemcpy>,       "sceDmacTryMemcpy", 'x', "xxx"},
};

void Register_sceDmac() {
	RegisterHLEModule("sceDmac", ARRAY_SIZE(sceDmac), sceDmac);
}

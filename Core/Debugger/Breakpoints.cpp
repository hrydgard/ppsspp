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

#include <atomic>

#include "Common/System/System.h"
#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/Debugger/WebSocket.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/LineInfo.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"

BreakpointManager g_breakpoints;

void MemCheck::Log(u32 addr, bool write, int size, u32 pc, const char *reason) const {
	if (action & BREAK_ACTION_LOG) {
		const char *type = write ? "Write" : "Read";
		if (logFormat.empty()) {
			NOTICE_LOG(Log::MemMap, "CHK %s%i(%s) at %08x (%s), PC=%08x (%s)", type, size * 8, reason, addr, g_symbolMap->GetDescription(addr).c_str(), pc, g_symbolMap->GetDescription(pc).c_str());
		} else {
			std::string formatted;
			g_breakpoints.EvaluateLogFormat(currentDebugMIPS, logFormat, formatted);
			NOTICE_LOG(Log::MemMap, "CHK %s%i(%s) at %08x: %s", type, size * 8, reason, addr, formatted.c_str());
		}
	}
}

BreakAction MemCheck::Apply(u32 addr, bool write, int size, u32 pc) {
	int condMask = write ? MEMCHECK_WRITE : MEMCHECK_READ;
	if (cond & condMask) {
		if (hasCondition) {
			if (!condition.Evaluate())
				return BREAK_ACTION_NONE;
		}

		++numHits;
		return action;
	}

	return BREAK_ACTION_NONE;
}

BreakAction MemCheck::Action(u32 addr, bool write, int size, u32 pc, const char *reason) {
	// Conditions have always already been checked if we get here.
	Log(addr, write, size, pc, reason);

	BreakpointHit hit;
	if (WebSocketDebuggerHasClients() || (action & BREAK_ACTION_PAUSE)) {
		hit.kind = BreakpointKind::Memory;
		hit.pc = pc;
		hit.address = addr;
		hit.size = size;
		hit.write = write;
		hit.rangeStart = start;
		hit.rangeEnd = end;
		// This is a copy of the stored memcheck, taken after Apply() bumped the count, so it's
		// already the post-hit value.
		hit.numHits = numHits;
		hit.logged = (action & BREAK_ACTION_LOG) != 0;
		hit.paused = (action & BREAK_ACTION_PAUSE) != 0;
		if (hasCondition)
			hit.condition = condition.expressionString;
		if (reason)
			hit.source = reason;
		WebSocketNotifyBreakpointHit(hit);
	}

	if (action & BREAK_ACTION_PAUSE) {
		// relatedAddress stays the range start for compatibility - the address actually touched
		// is in the hit, which is the whole point of it.
		Core_Break(BreakReason::MemoryBreakpoint, start, &hit);
	}
	return action;
}

size_t BreakpointManager::FindBreakpoint(u32 addr) {
	for (size_t i = 0; i < breakPoints_.size(); ++i) {
		if (breakPoints_[i].addr == addr)
			return i;
	}

	return INVALID_BREAKPOINT;
}

void BreakpointManager::UpdateAnyBreakPoints() {
	anyBreakPoints_ = !breakPoints_.empty() || tempBreakPoint_.valid;
}

size_t BreakpointManager::FindMemCheck(u32 start, u32 end) {
	for (size_t i = 0; i < memChecks_.size(); ++i) {
		if (memChecks_[i].start == start && memChecks_[i].end == end)
			return i;
	}

	return INVALID_MEMCHECK;
}

size_t BreakpointManager::FindRegBreakpoint(int reg) {
	for (size_t i = 0; i < regBreakpoints_.size(); ++i) {
		if (regBreakpoints_[i].reg == reg)
			return i;
	}

	return INVALID_REG_BREAKPOINT;
}

bool BreakpointManager::IsAddressBreakPoint(u32 addr) {
	if (!anyBreakPoints_)
		return false;
	size_t bp = FindBreakpoint(addr);
	if (bp == INVALID_BREAKPOINT) {
		return false;
	}
	return breakPoints_[bp].action != BREAK_ACTION_NONE;
}

bool BreakpointManager::IsAddressBreakPoint(u32 addr, bool* enabled) {
	if (!anyBreakPoints_)
		return false;
	size_t bp = FindBreakpoint(addr);
	if (bp == INVALID_BREAKPOINT) {
		return false;
	}
	if (enabled != nullptr) {
		*enabled = breakPoints_[bp].IsEnabled();
	}
	return true;
}

bool BreakpointManager::NeedsBreakCheckAt(u32 addr) {
	if (!anyBreakPoints_)
		return false;
	if (tempBreakPoint_.valid && tempBreakPoint_.addr == addr)
		return true;
	size_t bp = FindBreakpoint(addr);
	return bp != INVALID_BREAKPOINT && breakPoints_[bp].action != BREAK_ACTION_NONE;
}

bool BreakpointManager::RangeContainsBreakPoint(u32 addr, u32 size)
{
	if (!anyBreakPoints_)
		return false;
	const u32 end = addr + size;
	if (tempBreakPoint_.valid && tempBreakPoint_.addr >= addr && tempBreakPoint_.addr < end)
		return true;
	for (const auto &bp : breakPoints_)
	{
		if (bp.addr >= addr && bp.addr < end)
			return true;
	}

	return false;
}

int BreakpointManager::AddBreakPoint(u32 addr) {
	if (addr & 3) {
		WARN_LOG(Log::Debugger, "Breakpoint added at %08x will not be effective - unaligned address.", addr);
	}

	size_t bp = FindBreakpoint(addr);
	if (bp == INVALID_BREAKPOINT) {
		BreakPoint pt;
		pt.action |= BREAK_ACTION_PAUSE;
		pt.addr = addr;

		breakPoints_.push_back(pt);
		UpdateAnyBreakPoints();
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
		System_Notify(SystemNotification::DISASSEMBLY);
		return (int)breakPoints_.size() - 1;
	} else if (!breakPoints_[bp].IsEnabled()) {
		breakPoints_[bp].action |= BREAK_ACTION_PAUSE;
		breakPoints_[bp].hasCond = false;
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
		System_Notify(SystemNotification::DISASSEMBLY);
		return (int)bp;
	} else {
		// nothing to do, just return the already-existing breakpoint index
		return (int)bp;
	}
}

void BreakpointManager::RemoveBreakPoint(u32 addr) {
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT) {
		breakPoints_.erase(breakPoints_.begin() + bp);

		UpdateAnyBreakPoints();
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
		System_Notify(SystemNotification::DISASSEMBLY);
	}
}

void BreakpointManager::SetTempBreakPoint(u32 addr) {
	// Only one can be in flight - see TempBreakPoint. If there's an old one, it belonged to a step
	// that never completed, so drop it (and its stale compiled-in check) rather than accumulating.
	if (tempBreakPoint_.valid && tempBreakPoint_.addr != addr)
		currentMIPS->InvalidateICacheRangeDeferred(tempBreakPoint_.addr - 4, 8);

	tempBreakPoint_ = TempBreakPoint{};
	tempBreakPoint_.valid = true;
	tempBreakPoint_.addr = addr;

	UpdateAnyBreakPoints();
	currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
}

void BreakpointManager::SetTempBreakPointCond(const BreakPointCond &cond) {
	if (!tempBreakPoint_.valid)
		return;
	tempBreakPoint_.hasCond = true;
	tempBreakPoint_.cond = cond;
}

void BreakpointManager::ClearTempBreakPoint() {
	if (!tempBreakPoint_.valid)
		return;

	const u32 addr = tempBreakPoint_.addr;
	tempBreakPoint_ = TempBreakPoint{};

	UpdateAnyBreakPoints();
	currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
}

void BreakpointManager::ChangeBreakPoint(u32 addr, bool status) {
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT) {
		if (status) {
			breakPoints_[bp].action |= BREAK_ACTION_PAUSE;
		} else {
			breakPoints_[bp].action = BreakAction(breakPoints_[bp].action & ~BREAK_ACTION_PAUSE);
		}
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
		System_Notify(SystemNotification::DISASSEMBLY);
	}
}

void BreakpointManager::ChangeBreakPoint(u32 addr, BreakAction action) {
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT) {
		breakPoints_[bp].action = action;
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
		System_Notify(SystemNotification::DISASSEMBLY);
	}
}

// Relocates a breakpoint the user already set, rather than making them delete and re-add it.
// Returns false and changes nothing if there's no breakpoint at oldAddr, or if newAddr already has
// one of its own.
//
// Refusing the duplicate matters: ExecBreakPoint() goes through FindBreakpoint(), which returns
// only one entry per address, so a second breakpoint at the same address would be invisible.
bool BreakpointManager::ChangeBreakPointAddress(u32 oldAddr, u32 newAddr) {
	if (oldAddr == newAddr)
		return true;

	size_t bp = FindBreakpoint(oldAddr);
	if (bp == INVALID_BREAKPOINT)
		return false;
	if (FindBreakpoint(newAddr) != INVALID_BREAKPOINT)
		return false;

	if (newAddr & 3) {
		WARN_LOG(Log::Debugger, "Breakpoint moved to %08x will not be effective - unaligned address.", newAddr);
	}

	breakPoints_[bp].addr = newAddr;
	// The count belonged to the old address, so carrying it over would just be misleading.
	breakPoints_[bp].numHits = 0;

	// Both ends need invalidating, not just the new one: under a JIT the old address still has a
	// compiled-in check that now matches no breakpoint, and the new address has none at all.
	currentMIPS->InvalidateICacheRangeDeferred(oldAddr - 4, 8);
	currentMIPS->InvalidateICacheRangeDeferred(newAddr - 4, 8);
	System_Notify(SystemNotification::DISASSEMBLY);
	return true;
}

// This is not actually called, currently.
void BreakpointManager::ClearAllBreakPoints() {
	if (!anyBreakPoints_)
		return;
	if (!breakPoints_.empty()) {
		for (const auto &bp : breakPoints_) {
			currentMIPS->InvalidateICacheRangeDeferred(bp.addr - 4, 8);
		}
		breakPoints_.clear();
	}
	// Note: leaves the temporary breakpoint alone - it belongs to an in-flight step, not the user.
	UpdateAnyBreakPoints();
}

void BreakpointManager::ChangeBreakPointAddCond(u32 addr, const BreakPointCond &cond)
{
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].hasCond = true;
		breakPoints_[bp].cond = cond;
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
	}
}

void BreakpointManager::ChangeBreakPointRemoveCond(u32 addr) {
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT) {
		breakPoints_[bp].hasCond = false;
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
	}
}

BreakPointCond *BreakpointManager::GetBreakPointCondition(u32 addr) {
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT && breakPoints_[bp].hasCond)
		return &breakPoints_[bp].cond;
	return nullptr;
}

void BreakpointManager::ChangeBreakPointLogFormat(u32 addr, const std::string &fmt) {
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT) {
		breakPoints_[bp].logFormat = fmt;
		currentMIPS->InvalidateICacheRangeDeferred(addr - 4, 8);
	}
}

// Note that the user's breakpoint and the internal temporary one are handled independently, and the
// actions combine - a log-only breakpoint at the address a step-over is heading for must still log,
// and must still let the step complete. Whichever of them pauses, Core_Break() drops the temporary
// breakpoint, so a step that gets interrupted by something else doesn't leave one armed behind it.
BreakAction BreakpointManager::ExecBreakPoint(u32 addr) {
	if (!anyBreakPoints_)
		return BREAK_ACTION_NONE;

	BreakAction result = BREAK_ACTION_NONE;
	BreakpointHit hit;

	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT) {
		BreakPoint &info = breakPoints_[bp];
		const BreakAction action = info.action;

		bool condPassed = true;
		if (info.hasCond)
			condPassed = info.cond.Evaluate() != 0;

		if (condPassed) {
			++info.numHits;

			if (action != BREAK_ACTION_NONE && (WebSocketDebuggerHasClients() || (action & BREAK_ACTION_PAUSE))) {
				hit.kind = BreakpointKind::Exec;
				hit.pc = addr;
				hit.address = addr;
				hit.rangeStart = addr;
				hit.rangeEnd = addr;
				hit.numHits = info.numHits;
				hit.logged = (action & BREAK_ACTION_LOG) != 0;
				hit.paused = (action & BREAK_ACTION_PAUSE) != 0;
				if (info.hasCond)
					hit.condition = info.cond.expressionString;
				WebSocketNotifyBreakpointHit(hit);
			}

			if (action & BREAK_ACTION_LOG) {
				// Empty unless the game shipped an unstripped ELF - see Core/Debugger/LineInfo.h.
				// Worth the lookup even on this path: a log-only breakpoint's whole output is these
				// lines, and "mesh.zig:163" beats an address for reading a few thousand of them.
				const std::string source = g_lineInfo.LookupString(addr);
				const std::string at = source.empty() ? std::string() : " " + source;
				if (info.logFormat.empty()) {
					NOTICE_LOG(Log::JIT, "BKP PC=%08x%s (%s)", addr, at.c_str(), g_symbolMap->GetDescription(addr).c_str());
				} else {
					std::string formatted;
					BreakpointManager::EvaluateLogFormat(currentDebugMIPS, info.logFormat, formatted);
					NOTICE_LOG(Log::JIT, "BKP PC=%08x%s: %s", addr, at.c_str(), formatted.c_str());
				}
			}

			result |= action;
		}
	}

	if (tempBreakPoint_.valid && tempBreakPoint_.addr == addr) {
		// The condition, when set, narrows down which hit counts - to one thread for a step
		// ("threadid == ..."), or to a later frame for run-to-cursor ("flipcount > ...").  A hit that
		// fails it leaves the breakpoint armed, so the next one gets a chance.
		if (!tempBreakPoint_.hasCond || tempBreakPoint_.cond.Evaluate() != 0) {
			DEBUG_LOG(Log::Debugger, "Reached temporary breakpoint at %08x", addr);
			result |= BREAK_ACTION_PAUSE;
		}
	}

	if (result & BREAK_ACTION_PAUSE) {
		// hit stays kind None when only the temporary breakpoint fired - there's no user
		// breakpoint to describe in that case, just a step completing.
		Core_Break(BreakReason::CpuBreakpoint, addr, hit.kind != BreakpointKind::None ? &hit : nullptr);
		System_Notify(SystemNotification::DISASSEMBLY);
	}

	return result;
}

int BreakpointManager::AddMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction action) {
	size_t mc = FindMemCheck(start, end);
	if (mc == INVALID_MEMCHECK) {
		MemCheck check;
		check.start = start;
		check.end = end;
		check.cond = cond;
		check.action = action;

		memChecks_.push_back(check);
		bool hadAny = anyMemChecks_.exchange(true);
		if (!hadAny) {
			MemBlockOverrideDetailed();
		}
		updateMemChecks_ = true;
		currentMIPS->ClearJitCacheDeferred();  // memchecks apply to all memory accesses
		return (int)memChecks_.size() - 1;
	} else {
		// Update with additional cond and action bits. Not sure if we should OR or override?
		memChecks_[mc].cond = (MemCheckCondition)(memChecks_[mc].cond | cond);
		memChecks_[mc].action = memChecks_[mc].action | action;
		bool hadAny = anyMemChecks_.exchange(true);
		if (!hadAny) {
			MemBlockOverrideDetailed();
		}
		updateMemChecks_ = true;
		currentMIPS->ClearJitCacheDeferred();  // memchecks apply to all memory accesses
		return (int)mc;
	}
}

void BreakpointManager::RemoveMemCheck(u32 start, u32 end)
{
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK)
	{
		memChecks_.erase(memChecks_.begin() + mc);
		bool hadAny = anyMemChecks_.exchange(!memChecks_.empty());
		if (hadAny)
			MemBlockReleaseDetailed();
		updateMemChecks_ = true;
		currentMIPS->ClearJitCacheDeferred();  // memchecks apply to all memory accesses
	}
}

void BreakpointManager::ChangeMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction action)
{
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK)
	{
		memChecks_[mc].cond = cond;
		memChecks_[mc].action = action;
		updateMemChecks_ = true;
		currentMIPS->ClearJitCacheDeferred();  // memchecks apply to all memory accesses
	}
}

void BreakpointManager::ClearAllMemChecks()
{
	if (!memChecks_.empty())
	{
		memChecks_.clear();
		bool hadAny = anyMemChecks_.exchange(false);
		if (hadAny)
			MemBlockReleaseDetailed();
		updateMemChecks_ = true;
		currentMIPS->ClearJitCacheDeferred();  // memchecks apply to all memory accesses
	}
}

void BreakpointManager::ChangeMemCheckAddCond(u32 start, u32 end, const BreakPointCond &cond) {
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		memChecks_[mc].hasCondition = true;
		memChecks_[mc].condition = cond;
		// No need to update jit for a condition add/remove, they're not baked in.
	}
}

void BreakpointManager::ChangeMemCheckRemoveCond(u32 start, u32 end) {
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		memChecks_[mc].hasCondition = false;
		// No need to update jit for a condition add/remove, they're not baked in.
	}
}

BreakPointCond *BreakpointManager::GetMemCheckCondition(u32 start, u32 end) {
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK && memChecks_[mc].hasCondition)
		return &memChecks_[mc].condition;
	return nullptr;
}

void BreakpointManager::ChangeMemCheckLogFormat(u32 start, u32 end, const std::string &fmt) {
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		memChecks_[mc].logFormat = fmt;
		currentMIPS->ClearJitCacheDeferred();  // memchecks apply to all memory accesses
	}
}

bool BreakpointManager::GetMemCheck(u32 start, u32 end, MemCheck *check) {
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		*check = memChecks_[mc];
		return true;
	}
	return false;
}

static inline u32 NotCached(u32 val) {
	// Remove the cached part of the address as well as any mirror. Also ignores the kernel
	// bit (0x80000000) - not just the uncached bit (0x40000000) - so a memcheck registered
	// via one alias (e.g. user-space cached) still matches an access made through another
	// (e.g. kernel-space uncached). VRAM has no kernel-flagged mirror (see IsValidAddress),
	// so that case only needs the uncached bit masked.
	if ((val & 0x3F800000) == 0x04000000)
		return val & ~0x40600000;
	return val & ~0xC0000000;
}

bool BreakpointManager::GetMemCheckInRange(u32 address, int size, MemCheck *check) {
	auto result = FindMemCheckInRange(address, size);
	if (result)
		*check = *result;
	return result != nullptr;
}

MemCheck *BreakpointManager::FindMemCheckInRange(u32 address, int size) {
	std::vector<MemCheck>::iterator iter;
	for (MemCheck &check : memChecks_) {
		if (check.end != 0) {
			if (NotCached(address + size) > NotCached(check.start) && NotCached(address) < NotCached(check.end))
				return &check;
		} else {
			if (NotCached(check.start) == NotCached(address))
				return &check;
		}
	}

	// none found
	return 0;
}

BreakAction BreakpointManager::ExecMemCheck(u32 address, bool write, int size, u32 pc, const char *reason)
{
	if (!anyMemChecks_)
		return BREAK_ACTION_NONE;
	MemCheck *check = FindMemCheckInRange(address, size);
	if (check) {
		BreakAction applyAction = check->Apply(address, write, size, pc);
		if (applyAction == BREAK_ACTION_NONE)
			return applyAction;

		MemCheck copy = *check;
		return copy.Action(address, write, size, pc, reason);
	}
	return BREAK_ACTION_NONE;
}

BreakAction BreakpointManager::ExecOpMemCheck(u32 address, u32 pc) {
	// Note: currently, we don't check "on changed" for HLE (ExecMemCheck.)
	// We'd need to more carefully specify memory changes in HLE for that.
	int size = MIPSAnalyst::OpMemoryAccessSize(pc);
	if (size == 0 && MIPSAnalyst::OpHasDelaySlot(pc)) {
		// This means that the delay slot is what tripped us.
		pc += 4;
		size = MIPSAnalyst::OpMemoryAccessSize(pc);
	}

	bool write = MIPSAnalyst::IsOpMemoryWrite(pc);
	MemCheck *check = FindMemCheckInRange(address, size);
	if (check) {
		int mask = MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE;
		bool apply = false;
		if (write && (check->cond & mask) == mask) {
			if (MIPSAnalyst::OpWouldChangeMemory(currentMIPS, pc, address, size)) {
				apply = true;
			}
		} else {
			apply = true;
		}
		if (apply) {
			BreakAction applyAction = check->Apply(address, write, size, pc);
			if (applyAction == BREAK_ACTION_NONE)
				return applyAction;

			MemCheck copy = *check;
			return copy.Action(address, write, size, pc, "CPU");
		}
	}
	return BREAK_ACTION_NONE;
}

void BreakpointManager::RecomputeRegBreakpointMask() {
	u32 mask = 0;
	for (const auto &bp : regBreakpoints_) {
		if (bp.result != BREAK_ACTION_NONE)
			mask |= 1u << bp.reg;
	}
	regBreakpointMask_ = mask;
}

int BreakpointManager::AddRegBreakpoint(int reg) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp == INVALID_REG_BREAKPOINT) {
		RegBreakpoint pt;
		pt.reg = reg;
		pt.result |= BREAK_ACTION_PAUSE;

		regBreakpoints_.push_back(pt);
		RecomputeRegBreakpointMask();
		return (int)regBreakpoints_.size() - 1;
	} else if (!regBreakpoints_[bp].IsEnabled()) {
		regBreakpoints_[bp].result |= BREAK_ACTION_PAUSE;
		regBreakpoints_[bp].hasCond = false;
		RecomputeRegBreakpointMask();
		return (int)bp;
	} else {
		return (int)bp;
	}
}

void BreakpointManager::RemoveRegBreakpoint(int reg) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT) {
		regBreakpoints_.erase(regBreakpoints_.begin() + bp);
		RecomputeRegBreakpointMask();
	}
}

void BreakpointManager::ChangeRegBreakpoint(int reg, bool status) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT) {
		if (status)
			regBreakpoints_[bp].result |= BREAK_ACTION_PAUSE;
		else
			regBreakpoints_[bp].result = BreakAction(regBreakpoints_[bp].result & ~BREAK_ACTION_PAUSE);
		RecomputeRegBreakpointMask();
	}
}

void BreakpointManager::ChangeRegBreakpoint(int reg, BreakAction result) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT) {
		regBreakpoints_[bp].result = result;
		RecomputeRegBreakpointMask();
	}
}

void BreakpointManager::ClearAllRegBreakpoints() {
	if (!regBreakpoints_.empty()) {
		regBreakpoints_.clear();
		regBreakpointMask_ = 0;
	}
}

void BreakpointManager::ChangeRegBreakpointAddCond(int reg, const BreakPointCond &cond) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT) {
		regBreakpoints_[bp].hasCond = true;
		regBreakpoints_[bp].cond = cond;
	}
}

void BreakpointManager::ChangeRegBreakpointRemoveCond(int reg) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT) {
		regBreakpoints_[bp].hasCond = false;
	}
}

BreakPointCond *BreakpointManager::GetRegBreakpointCondition(int reg) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT && regBreakpoints_[bp].hasCond)
		return &regBreakpoints_[bp].cond;
	return nullptr;
}

void BreakpointManager::ChangeRegBreakpointLogFormat(int reg, const std::string &fmt) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT) {
		regBreakpoints_[bp].logFormat = fmt;
	}
}

bool BreakpointManager::IsRegBreakpoint(int reg) {
	return (regBreakpointMask_ & (1u << reg)) != 0;
}

bool BreakpointManager::GetRegBreakpoint(int reg, RegBreakpoint *check) {
	size_t bp = FindRegBreakpoint(reg);
	if (bp != INVALID_REG_BREAKPOINT) {
		*check = regBreakpoints_[bp];
		return true;
	}
	return false;
}

std::vector<RegBreakpoint> BreakpointManager::GetRegBreakpoints() {
	return regBreakpoints_;
}

BreakAction BreakpointManager::ExecRegBreakpoint(int reg, u32 pc) {
	// Callers are expected to have already checked GetRegBreakpointMask() themselves (that's
	// the whole point of exposing it - a single shift+and in the hot interpreter loop, skipping
	// a function call entirely in the overwhelmingly common no-breakpoint case), but check again
	// here too since this is also reachable directly.
	if ((regBreakpointMask_ & (1u << reg)) == 0)
		return BREAK_ACTION_NONE;
	size_t bp = FindRegBreakpoint(reg);
	if (bp == INVALID_REG_BREAKPOINT)
		return BREAK_ACTION_NONE;

	RegBreakpoint &info = regBreakpoints_[bp];
	if (info.result == BREAK_ACTION_NONE)
		return BREAK_ACTION_NONE;

	if (info.hasCond && !info.cond.Evaluate())
		return BREAK_ACTION_NONE;

	++info.numHits;

	BreakpointHit hit;
	if (WebSocketDebuggerHasClients() || (info.result & BREAK_ACTION_PAUSE)) {
		hit.kind = BreakpointKind::Register;
		hit.pc = pc;
		hit.address = pc;
		hit.reg = reg;
		hit.numHits = info.numHits;
		hit.logged = (info.result & BREAK_ACTION_LOG) != 0;
		hit.paused = (info.result & BREAK_ACTION_PAUSE) != 0;
		if (info.hasCond)
			hit.condition = info.cond.expressionString;
		WebSocketNotifyBreakpointHit(hit);
	}

	if (info.result & BREAK_ACTION_LOG) {
		if (info.logFormat.empty()) {
			NOTICE_LOG(Log::JIT, "BKP reg write r%d, PC=%08x (%s)", reg, pc, g_symbolMap->GetDescription(pc).c_str());
		} else {
			std::string formatted;
			BreakpointManager::EvaluateLogFormat(currentDebugMIPS, info.logFormat, formatted);
			NOTICE_LOG(Log::JIT, "BKP reg write r%d, PC=%08x: %s", reg, pc, formatted.c_str());
		}
	}
	if ((info.result & BREAK_ACTION_PAUSE) && g_breakpoints.CheckSkipFirst() != pc) {
		Core_Break(BreakReason::RegBreakpoint, pc, &hit);
	}

	return info.result;
}

void BreakpointManager::ClearSkipFirst() {
	breakSkipFirstAt_ = 0;
	breakSkipFirstTicks_ = 0;
}

void BreakpointManager::SetSkipFirst(u32 pc) {
	breakSkipFirstAt_ = pc;
	breakSkipFirstTicks_ = CoreTiming::GetTicks(currentMIPS);
}

u32 BreakpointManager::CheckSkipFirst() const {
	u32 pc = breakSkipFirstAt_;
	if (breakSkipFirstTicks_ == CoreTiming::GetTicks(currentMIPS))
		return pc;
	return 0;
}

static MemCheck NotCached(MemCheck mc) {
	// Toggle the uncached bit (0x40000000) of the address.
	mc.start ^= 0x40000000;
	if (mc.end != 0)
		mc.end ^= 0x40000000;
	return mc;
}

static MemCheck NotKernel(MemCheck mc) {
	// Toggle the kernel bit (0x80000000) of the address - independent of, and combinable
	// with, the uncached bit above. Not applied to VRAM ranges: VRAM has no kernel-flagged
	// mirror (see IsValidAddress's "disallow kernel-flagged VRAM" comment).
	mc.start ^= 0x80000000;
	if (mc.end != 0)
		mc.end ^= 0x80000000;
	return mc;
}

static MemCheck VRAMMirror(uint8_t mirror, MemCheck mc) {
	mc.start &= ~0x00600000;
	mc.start += 0x00200000 * mirror;
	if (mc.end != 0) {
		mc.end &= ~0x00600000;
		mc.end += 0x00200000 * mirror;
		if (mc.end < mc.start)
			mc.end += 0x00200000;
	}
	return mc;
}

void BreakpointManager::UpdateCachedMemCheckRanges() {
	memCheckRangesRead_.clear();
	memCheckRangesWrite_.clear();

	auto add = [&](bool read, bool write, const MemCheck &mc) {
		if (read)
			memCheckRangesRead_.push_back(mc);
		if (write)
			memCheckRangesWrite_.push_back(mc);
	};

	for (const auto &check : memChecks_) {
		bool read = (check.cond & MEMCHECK_READ) != 0;
		bool write = (check.cond & MEMCHECK_WRITE) != 0;

		if (Memory::IsVRAMAddress(check.start) && (check.end == 0 || Memory::IsVRAMAddress(check.end))) {
			for (uint8_t mirror = 0; mirror < 4; ++mirror) {
				MemCheck copy = VRAMMirror(mirror, check);
				add(read, write, copy);
				add(read, write, NotCached(copy));
			}
		} else {
			// All four combinations of the independent uncached (0x40000000) and kernel
			// (0x80000000) address bits - see NotCached(u32)/NotKernel() above.
			add(read, write, check);
			add(read, write, NotCached(check));
			add(read, write, NotKernel(check));
			add(read, write, NotKernel(NotCached(check)));
		}
	}
}

std::vector<MemCheck> BreakpointManager::GetMemCheckRanges(bool write) {
	if (write)
		return memCheckRangesWrite_;
	return memCheckRangesRead_;
}

std::vector<MemCheck> BreakpointManager::GetMemChecks() {
	return memChecks_;
}

std::vector<BreakPoint> BreakpointManager::GetBreakpoints() {
	return breakPoints_;
}

void BreakpointManager::Frame() {
	if (anyMemChecks_ && updateMemChecks_) {
		UpdateCachedMemCheckRanges();
		updateMemChecks_ = false;
	}
}

bool BreakpointManager::ValidateLogFormat(MIPSDebugInterface *cpu, const std::string &fmt) {
	std::string ignore;
	return EvaluateLogFormat(cpu, fmt, ignore);
}

bool BreakpointManager::EvaluateLogFormat(MIPSDebugInterface *cpu, const std::string &fmt, std::string &result) {
	PostfixExpression exp;
	result.clear();

	size_t pos = 0;
	while (pos < fmt.size()) {
		size_t next = fmt.find_first_of('{', pos);
		if (next == fmt.npos) {
			// End of the string.
			result += fmt.substr(pos);
			break;
		}
		if (next != pos) {
			result += fmt.substr(pos, next - pos);
			pos = next;
		}

		size_t end = fmt.find_first_of('}', next + 1);
		if (end == fmt.npos) {
			// Invalid: every expression needs a { and a }.
			return false;
		}

		std::string expression = fmt.substr(next + 1, end - next - 1);
		if (expression.empty()) {
			result += "{}";
		} else {
			int type = 'x';
			if (expression.length() > 2 && expression[expression.length() - 2] == ':') {
				switch (expression[expression.length() - 1]) {
				case 'd':
				case 'f':
				case 'p':
				case 's':
				case 'x':
					type = expression[expression.length() - 1];
					expression.resize(expression.length() - 2);
					break;

				default:
					// Assume a ternary.
					break;
				}
			}

			if (!initExpression(cpu, expression.c_str(), exp)) {
				return false;
			}

			union {
				int i;
				u32 u;
				float f;
			} expResult;
			char resultString[256];
			if (!parseExpression(cpu, exp, expResult.u)) {
				return false;
			}

			switch (type) {
			case 'd':
				snprintf(resultString, sizeof(resultString), "%d", expResult.i);
				break;
			case 'f':
				snprintf(resultString, sizeof(resultString), "%f", expResult.f);
				break;
			case 'p':
				if (Memory::IsValidAddress(expResult.u)) {
					snprintf(resultString, sizeof(resultString), "%08x[%08x]", expResult.u, Memory::ReadUnchecked_U32(expResult.u));
				} else {
					snprintf(resultString, sizeof(resultString), "%08x[invalid]", expResult.u);
				}
				break;
			case 's':
				snprintf(resultString, sizeof(resultString) - 1, "%s", Memory::IsValidAddress(expResult.u) ? Memory::GetCharPointer(expResult.u) : "(invalid)");
				break;
			case 'x':
				snprintf(resultString, sizeof(resultString), "%08x", expResult.u);
				break;
			}
			result += resultString;
		}

		// Skip the }.
		pos = end + 1;
	}

	return true;
}

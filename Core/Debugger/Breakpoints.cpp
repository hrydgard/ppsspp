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

#include <cstdio>
#include <atomic>
#include <mutex>

#include "Common/System/System.h"
#include "Common/Log.h"
#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/CoreTiming.h"

std::atomic<bool> anyBreakPoints_(false);
std::atomic<bool> anyMemChecks_(false);

static std::mutex breakPointsMutex_;
std::vector<BreakPoint> CBreakPoints::breakPoints_;
u32 CBreakPoints::breakSkipFirstAt_ = 0;
u64 CBreakPoints::breakSkipFirstTicks_ = 0;
static std::mutex memCheckMutex_;
std::vector<MemCheck> CBreakPoints::memChecks_;
std::vector<MemCheck> CBreakPoints::memCheckRangesRead_;
std::vector<MemCheck> CBreakPoints::memCheckRangesWrite_;

void MemCheck::Log(u32 addr, bool write, int size, u32 pc, const char *reason) {
	if (result & BREAK_ACTION_LOG) {
		const char *type = write ? "Write" : "Read";
		if (logFormat.empty()) {
			NOTICE_LOG(Log::MemMap, "CHK %s%i(%s) at %08x (%s), PC=%08x (%s)", type, size * 8, reason, addr, g_symbolMap->GetDescription(addr).c_str(), pc, g_symbolMap->GetDescription(pc).c_str());
		} else {
			std::string formatted;
			CBreakPoints::EvaluateLogFormat(currentDebugMIPS, logFormat, formatted);
			NOTICE_LOG(Log::MemMap, "CHK %s%i(%s) at %08x: %s", type, size * 8, reason, addr, formatted.c_str());
		}
	}
}

BreakAction MemCheck::Apply(u32 addr, bool write, int size, u32 pc) {
	int mask = write ? MEMCHECK_WRITE : MEMCHECK_READ;
	if (cond & mask) {
		if (hasCondition) {
			if (!condition.Evaluate())
				return BREAK_ACTION_IGNORE;
		}

		++numHits;
		return result;
	}

	return BREAK_ACTION_IGNORE;
}

BreakAction MemCheck::Action(u32 addr, bool write, int size, u32 pc, const char *reason) {
	// Conditions have always already been checked if we get here.
	Log(addr, write, size, pc, reason);
	if ((result & BREAK_ACTION_PAUSE) && coreState != CORE_POWERUP) {
		Core_EnableStepping(true, "memory.breakpoint", start);
	}

	return result;
}

// Note: must lock while calling this.
size_t CBreakPoints::FindBreakpoint(u32 addr, bool matchTemp, bool temp)
{
	size_t found = INVALID_BREAKPOINT;
	for (size_t i = 0; i < breakPoints_.size(); ++i)
	{
		const auto &bp = breakPoints_[i];
		if (bp.addr == addr && (!matchTemp || bp.temporary == temp))
		{
			if (bp.IsEnabled())
				return i;
			// Hold out until the first enabled one.
			if (found == INVALID_BREAKPOINT)
				found = i;
		}
	}

	return found;
}

size_t CBreakPoints::FindMemCheck(u32 start, u32 end)
{
	for (size_t i = 0; i < memChecks_.size(); ++i)
	{
		if (memChecks_[i].start == start && memChecks_[i].end == end)
			return i;
	}

	return INVALID_MEMCHECK;
}

bool CBreakPoints::IsAddressBreakPoint(u32 addr)
{
	if (!anyBreakPoints_)
		return false;
	std::lock_guard<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	return bp != INVALID_BREAKPOINT && breakPoints_[bp].result != BREAK_ACTION_IGNORE;
}

bool CBreakPoints::IsAddressBreakPoint(u32 addr, bool* enabled)
{
	if (!anyBreakPoints_)
		return false;
	std::lock_guard<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	if (bp == INVALID_BREAKPOINT) return false;
	if (enabled != nullptr)
		*enabled = breakPoints_[bp].IsEnabled();
	return true;
}

bool CBreakPoints::IsTempBreakPoint(u32 addr)
{
	std::lock_guard<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr, true, true);
	return bp != INVALID_BREAKPOINT;
}

bool CBreakPoints::RangeContainsBreakPoint(u32 addr, u32 size)
{
	if (!anyBreakPoints_)
		return false;
	std::lock_guard<std::mutex> guard(breakPointsMutex_);
	const u32 end = addr + size;
	for (const auto &bp : breakPoints_)
	{
		if (bp.addr >= addr && bp.addr < end)
			return true;
	}

	return false;
}

void CBreakPoints::AddBreakPoint(u32 addr, bool temp)
{
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr, true, temp);
	if (bp == INVALID_BREAKPOINT)
	{
		BreakPoint pt;
		pt.result |= BREAK_ACTION_PAUSE;
		pt.temporary = temp;
		pt.addr = addr;

		breakPoints_.push_back(pt);
		anyBreakPoints_ = true;
		guard.unlock();
		Update(addr);
	}
	else if (!breakPoints_[bp].IsEnabled())
	{
		breakPoints_[bp].result |= BREAK_ACTION_PAUSE;
		breakPoints_[bp].hasCond = false;
		guard.unlock();
		Update(addr);
	}
}

void CBreakPoints::RemoveBreakPoint(u32 addr)
{
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_.erase(breakPoints_.begin() + bp);

		// Check again, there might've been an overlapping temp breakpoint.
		bp = FindBreakpoint(addr);
		if (bp != INVALID_BREAKPOINT)
			breakPoints_.erase(breakPoints_.begin() + bp);

		anyBreakPoints_ = !breakPoints_.empty();
		guard.unlock();
		Update(addr);
	}
}

void CBreakPoints::ChangeBreakPoint(u32 addr, bool status)
{
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		if (status)
			breakPoints_[bp].result |= BREAK_ACTION_PAUSE;
		else
			breakPoints_[bp].result = BreakAction(breakPoints_[bp].result & ~BREAK_ACTION_PAUSE);

		guard.unlock();
		Update(addr);
	}
}

void CBreakPoints::ChangeBreakPoint(u32 addr, BreakAction result)
{
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].result = result;
		guard.unlock();
		Update(addr);
	}
}

void CBreakPoints::ClearAllBreakPoints()
{
	if (!anyBreakPoints_)
		return;
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	if (!breakPoints_.empty())
	{
		breakPoints_.clear();
		guard.unlock();
		Update();
	}
}

void CBreakPoints::ClearTemporaryBreakPoints()
{
	if (!anyBreakPoints_)
		return;
	std::unique_lock<std::mutex> guard(breakPointsMutex_);

	bool update = false;
	for (int i = (int)breakPoints_.size()-1; i >= 0; --i)
	{
		if (breakPoints_[i].temporary)
		{
			breakPoints_.erase(breakPoints_.begin() + i);
			update = true;
		}
	}

	guard.unlock();
	if (update)
		Update();
}

void CBreakPoints::ChangeBreakPointAddCond(u32 addr, const BreakPointCond &cond)
{
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].hasCond = true;
		breakPoints_[bp].cond = cond;
		guard.unlock();
		Update(addr);
	}
}

void CBreakPoints::ChangeBreakPointRemoveCond(u32 addr)
{
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT)
	{
		breakPoints_[bp].hasCond = false;
		guard.unlock();
		Update(addr);
	}
}

BreakPointCond *CBreakPoints::GetBreakPointCondition(u32 addr)
{
	std::lock_guard<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr);
	if (bp != INVALID_BREAKPOINT && breakPoints_[bp].hasCond)
		return &breakPoints_[bp].cond;
	return NULL;
}

void CBreakPoints::ChangeBreakPointLogFormat(u32 addr, const std::string &fmt) {
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr, true, false);
	if (bp != INVALID_BREAKPOINT) {
		breakPoints_[bp].logFormat = fmt;
		guard.unlock();
		Update(addr);
	}
}

BreakAction CBreakPoints::ExecBreakPoint(u32 addr) {
	if (!anyBreakPoints_)
		return BREAK_ACTION_IGNORE;
	std::unique_lock<std::mutex> guard(breakPointsMutex_);
	size_t bp = FindBreakpoint(addr, false);
	if (bp != INVALID_BREAKPOINT) {
		const BreakPoint &info = breakPoints_[bp];
		guard.unlock();

		if (info.hasCond) {
			// Evaluate the breakpoint and abort if necessary.
			auto cond = CBreakPoints::GetBreakPointCondition(currentMIPS->pc);
			if (cond && !cond->Evaluate())
				return BREAK_ACTION_IGNORE;
		}

		if (info.result & BREAK_ACTION_LOG) {
			if (info.logFormat.empty()) {
				NOTICE_LOG(Log::JIT, "BKP PC=%08x (%s)", addr, g_symbolMap->GetDescription(addr).c_str());
			} else {
				std::string formatted;
				CBreakPoints::EvaluateLogFormat(currentDebugMIPS, info.logFormat, formatted);
				NOTICE_LOG(Log::JIT, "BKP PC=%08x: %s", addr, formatted.c_str());
			}
		}
		if ((info.result & BREAK_ACTION_PAUSE) && coreState != CORE_POWERUP) {
			Core_EnableStepping(true, "cpu.breakpoint", info.addr);
		}

		return info.result;
	}

	return BREAK_ACTION_IGNORE;
}

void CBreakPoints::AddMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result)
{
	std::unique_lock<std::mutex> guard(memCheckMutex_);

	size_t mc = FindMemCheck(start, end);
	if (mc == INVALID_MEMCHECK)
	{
		MemCheck check;
		check.start = start;
		check.end = end;
		check.cond = cond;
		check.result = result;

		memChecks_.push_back(check);
		bool hadAny = anyMemChecks_.exchange(true);
		if (!hadAny)
			MemBlockOverrideDetailed();
		guard.unlock();
		Update();
	}
	else
	{
		memChecks_[mc].cond = (MemCheckCondition)(memChecks_[mc].cond | cond);
		memChecks_[mc].result = (BreakAction)(memChecks_[mc].result | result);
		bool hadAny = anyMemChecks_.exchange(true);
		if (!hadAny)
			MemBlockOverrideDetailed();
		guard.unlock();
		Update();
	}
}

void CBreakPoints::RemoveMemCheck(u32 start, u32 end)
{
	std::unique_lock<std::mutex> guard(memCheckMutex_);

	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK)
	{
		memChecks_.erase(memChecks_.begin() + mc);
		bool hadAny = anyMemChecks_.exchange(!memChecks_.empty());
		if (hadAny)
			MemBlockReleaseDetailed();
		guard.unlock();
		Update();
	}
}

void CBreakPoints::ChangeMemCheck(u32 start, u32 end, MemCheckCondition cond, BreakAction result)
{
	std::unique_lock<std::mutex> guard(memCheckMutex_);
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK)
	{
		memChecks_[mc].cond = cond;
		memChecks_[mc].result = result;
		guard.unlock();
		Update();
	}
}

void CBreakPoints::ClearAllMemChecks()
{
	std::unique_lock<std::mutex> guard(memCheckMutex_);

	if (!memChecks_.empty())
	{
		memChecks_.clear();
		bool hadAny = anyMemChecks_.exchange(false);
		if (hadAny)
			MemBlockReleaseDetailed();
		guard.unlock();
		Update();
	}
}


void CBreakPoints::ChangeMemCheckAddCond(u32 start, u32 end, const BreakPointCond &cond) {
	std::unique_lock<std::mutex> guard(memCheckMutex_);
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		memChecks_[mc].hasCondition = true;
		memChecks_[mc].condition = cond;
		guard.unlock();
		// No need to update jit for a condition add/remove, they're not baked in.
		Update(-1);
	}
}

void CBreakPoints::ChangeMemCheckRemoveCond(u32 start, u32 end) {
	std::unique_lock<std::mutex> guard(memCheckMutex_);
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		memChecks_[mc].hasCondition = false;
		guard.unlock();
		// No need to update jit for a condition add/remove, they're not baked in.
		Update(-1);
	}
}

BreakPointCond *CBreakPoints::GetMemCheckCondition(u32 start, u32 end) {
	std::unique_lock<std::mutex> guard(memCheckMutex_);
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK && memChecks_[mc].hasCondition)
		return &memChecks_[mc].condition;
	return nullptr;
}

void CBreakPoints::ChangeMemCheckLogFormat(u32 start, u32 end, const std::string &fmt) {
	std::unique_lock<std::mutex> guard(memCheckMutex_);
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		memChecks_[mc].logFormat = fmt;
		guard.unlock();
		Update();
	}
}

bool CBreakPoints::GetMemCheck(u32 start, u32 end, MemCheck *check) {
	std::lock_guard<std::mutex> guard(memCheckMutex_);
	size_t mc = FindMemCheck(start, end);
	if (mc != INVALID_MEMCHECK) {
		*check = memChecks_[mc];
		return true;
	}
	return false;
}

static inline u32 NotCached(u32 val) {
	// Remove the cached part of the address as well as any mirror.
	if ((val & 0x3F800000) == 0x04000000)
		return val & ~0x40600000;
	return val & ~0x40000000;
}

bool CBreakPoints::GetMemCheckInRange(u32 address, int size, MemCheck *check) {
	std::lock_guard<std::mutex> guard(memCheckMutex_);
	auto result = GetMemCheckLocked(address, size);
	if (result)
		*check = *result;
	return result != nullptr;
}

MemCheck *CBreakPoints::GetMemCheckLocked(u32 address, int size) {
	std::vector<MemCheck>::iterator iter;
	for (iter = memChecks_.begin(); iter != memChecks_.end(); ++iter)
	{
		MemCheck &check = *iter;
		if (check.end != 0)
		{
			if (NotCached(address + size) > NotCached(check.start) && NotCached(address) < NotCached(check.end))
				return &check;
		}
		else
		{
			if (NotCached(check.start) == NotCached(address))
				return &check;
		}
	}

	//none found
	return 0;
}

BreakAction CBreakPoints::ExecMemCheck(u32 address, bool write, int size, u32 pc, const char *reason)
{
	if (!anyMemChecks_)
		return BREAK_ACTION_IGNORE;
	std::unique_lock<std::mutex> guard(memCheckMutex_);
	auto check = GetMemCheckLocked(address, size);
	if (check) {
		BreakAction applyAction = check->Apply(address, write, size, pc);
		if (applyAction == BREAK_ACTION_IGNORE)
			return applyAction;

		auto copy = *check;
		guard.unlock();
		return copy.Action(address, write, size, pc, reason);
	}
	return BREAK_ACTION_IGNORE;
}

BreakAction CBreakPoints::ExecOpMemCheck(u32 address, u32 pc)
{
	// Note: currently, we don't check "on changed" for HLE (ExecMemCheck.)
	// We'd need to more carefully specify memory changes in HLE for that.
	int size = MIPSAnalyst::OpMemoryAccessSize(pc);
	if (size == 0 && MIPSAnalyst::OpHasDelaySlot(pc)) {
		// This means that the delay slot is what tripped us.
		pc += 4;
		size = MIPSAnalyst::OpMemoryAccessSize(pc);
	}

	bool write = MIPSAnalyst::IsOpMemoryWrite(pc);
	std::unique_lock<std::mutex> guard(memCheckMutex_);
	auto check = GetMemCheckLocked(address, size);
	if (check) {
		int mask = MEMCHECK_WRITE | MEMCHECK_WRITE_ONCHANGE;
		bool apply = false;
		if (write && (check->cond & mask) == mask) {
			if (MIPSAnalyst::OpWouldChangeMemory(pc, address, size)) {
				apply = true;
			}
		} else {
			apply = true;
		}
		if (apply) {
			BreakAction applyAction = check->Apply(address, write, size, pc);
			if (applyAction == BREAK_ACTION_IGNORE)
				return applyAction;

			// Make a copy so we can safely unlock.
			auto copy = *check;
			guard.unlock();
			return copy.Action(address, write, size, pc, "CPU");
		}
	}
	return BREAK_ACTION_IGNORE;
}

void CBreakPoints::SetSkipFirst(u32 pc)
{
	breakSkipFirstAt_ = pc;
	breakSkipFirstTicks_ = CoreTiming::GetTicks();
}
u32 CBreakPoints::CheckSkipFirst()
{
	u32 pc = breakSkipFirstAt_;
	if (breakSkipFirstTicks_ == CoreTiming::GetTicks())
		return pc;
	return 0;
}

static MemCheck NotCached(MemCheck mc) {
	// Toggle the cached part of the address.
	mc.start ^= 0x40000000;
	if (mc.end != 0)
		mc.end ^= 0x40000000;
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

void CBreakPoints::UpdateCachedMemCheckRanges() {
	std::lock_guard<std::mutex> guard(memCheckMutex_);
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
			add(read, write, check);
			add(read, write, NotCached(check));
		}
	}
}

std::vector<MemCheck> CBreakPoints::GetMemCheckRanges(bool write) {
	std::lock_guard<std::mutex> guard(memCheckMutex_);
	if (write)
		return memCheckRangesWrite_;
	return memCheckRangesRead_;
}

std::vector<MemCheck> CBreakPoints::GetMemChecks()
{
	std::lock_guard<std::mutex> guard(memCheckMutex_);
	return memChecks_;
}

std::vector<BreakPoint> CBreakPoints::GetBreakpoints()
{
	std::lock_guard<std::mutex> guard(breakPointsMutex_);
	return breakPoints_;
}

bool CBreakPoints::HasBreakPoints() {
	return anyBreakPoints_;
}

bool CBreakPoints::HasMemChecks() {
	return anyMemChecks_;
}

void CBreakPoints::Update(u32 addr) {
	if (MIPSComp::jit && addr != -1) {
		bool resume = false;
		if (Core_IsStepping() == false) {
			Core_EnableStepping(true, "cpu.breakpoint.update", addr);
			Core_WaitInactive(200);
			resume = true;
		}

		// In case this is a delay slot, clear the previous instruction too.
		if (addr != 0)
			mipsr4k.InvalidateICache(addr - 4, 8);
		else
			mipsr4k.ClearJitCache();

		if (resume)
			Core_EnableStepping(false);
	}

	if (anyMemChecks_ && addr != -1)
		UpdateCachedMemCheckRanges();

	// Redraw in order to show the breakpoint.
	System_Notify(SystemNotification::DISASSEMBLY);
}

bool CBreakPoints::ValidateLogFormat(DebugInterface *cpu, const std::string &fmt) {
	std::string ignore;
	return EvaluateLogFormat(cpu, fmt, ignore);
}

bool CBreakPoints::EvaluateLogFormat(DebugInterface *cpu, const std::string &fmt, std::string &result) {
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

			if (!cpu->initExpression(expression.c_str(), exp)) {
				return false;
			}

			union {
				int i;
				u32 u;
				float f;
			} expResult;
			char resultString[256];
			if (!cpu->parseExpression(exp, expResult.u)) {
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
				snprintf(resultString, sizeof(resultString), "%08x[%08x]", expResult.u, Memory::IsValidAddress(expResult.u) ? Memory::Read_U32(expResult.u) : 0);
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

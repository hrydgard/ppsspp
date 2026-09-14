// Copyright (c) 2012- PPSSPP Project / Dolphin Project.

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
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

#include "Common/Profiler/Profiler.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeList.h"
#include "Core/CoreTiming.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/MIPS/MIPS.h"

static const int initialHz = 222000000;
int CPU_HZ = 222000000;

// is this really necessary?
#define INITIAL_SLICE_LENGTH 20000
#define MAX_SLICE_LENGTH 100000000

namespace CoreTiming {

static std::vector<EventType> event_types;
// Only used during restore.
static std::set<int> usedEventTypes;
static std::set<int> restoredEventTypes;
static int nextEventTypeRestoreId = -1;

static Event *first;
static Event *eventPool = 0;

// Downcount has been moved to currentMIPS, to save a couple of clocks in every ARM JIT block
// as we can already reach that structure through a register.
int slicelength;

alignas(16) static s64 globalTimer;
static s64 idledCycles;
static s64 lastGlobalTimeTicks;
static s64 lastGlobalTimeUs;
// See SetBreakDeadlineUs. 0 = none. Deliberately not saved in savestates - it belongs to a
// debugger session, not to the emulated machine.
static s64 breakDeadlineUs;
static s64 breakDeadlineTicks;
static void RecomputeBreakDeadline();

bool SetClockFrequencyHz(int cpuHz) {
	if (cpuHz <= 0) {
		// Paranoid check, protecting against division by zero and similar nonsense.
		return true;  // encourage logging
	}

	if (cpuHz == CPU_HZ) {
		// Already at the correct frequency. Bail, false means we'll log at a lower level.
		return false;
	}

	// When the mhz changes, we keep track of what "time" it was before hand.
	// This way, time always moves forward, even if mhz is changed.
	lastGlobalTimeUs = GetGlobalTimeUs();
	lastGlobalTimeTicks = GetTicks(currentMIPS);

	CPU_HZ = cpuHz;

	// The remaining time to a debugger deadline is now a different number of ticks.
	RecomputeBreakDeadline();

	// TODO: Rescale times of scheduled events?
	__AudioCPUMHzChange();
	return true;
}

int GetClockFrequencyHz() {
	return CPU_HZ;
}

u64 GetGlobalTimeUsScaled() {
	return GetGlobalTimeUs();
}

u64 GetGlobalTimeUs() {
	s64 ticksSinceLast = GetTicks(currentMIPS) - lastGlobalTimeTicks;
	int freq = GetClockFrequencyHz();
	s64 usSinceLast = ticksSinceLast * 1000000 / freq;
	if (ticksSinceLast > UINT_MAX) {
		// Adjust the calculated value to avoid overflow errors.
		lastGlobalTimeUs += usSinceLast;
		lastGlobalTimeTicks = GetTicks(currentMIPS);
		usSinceLast = 0;
	}
	return lastGlobalTimeUs + usSinceLast;
}

// Turns the microsecond deadline into the tick count Advance() compares against. Has to be redone
// whenever the clock frequency changes, since that changes how many ticks the remaining time is.
static void RecomputeBreakDeadline() {
	if (!breakDeadlineUs) {
		breakDeadlineTicks = 0;
		return;
	}
	const s64 remainingUs = breakDeadlineUs - (s64)GetGlobalTimeUs();
	breakDeadlineTicks = (s64)GetTicks(currentMIPS) + (remainingUs > 0 ? usToCycles(remainingUs) : 0);
}

void SetBreakDeadlineUs(u64 us) {
	breakDeadlineUs = (s64)us;
	RecomputeBreakDeadline();
}

u64 GetBreakDeadlineUs() {
	return (u64)breakDeadlineUs;
}

u64 PeekGlobalTimeUs() {
	// Same sum as above without the rebasing, so this stays callable from a thread that isn't the
	// CPU thread. The rebasing exists purely to keep the multiply below from overflowing, and it
	// happens often enough on the CPU thread that ticksSinceLast stays small here.
	const s64 ticksSinceLast = GetTicks(currentMIPS) - lastGlobalTimeTicks;
	return lastGlobalTimeUs + ticksSinceLast * 1000000 / GetClockFrequencyHz();
}

const Event *GetFirstEvent() {
	return first;
}

const std::vector<EventType> &GetEventTypes() {
	return event_types;
}

Event* GetNewEvent()
{
	if(!eventPool)
		return new Event;

	Event* ev = eventPool;
	eventPool = ev->next;
	return ev;
}

void FreeEvent(Event* ev)
{
	ev->next = eventPool;
	eventPool = ev;
}

int RegisterEvent(const char *name, TimedCallback callback) {
	for (const auto &ty : event_types) {
		if (!strcmp(ty.name, name)) {
			_assert_msg_(false, "Event type %s already registered", name);
			// Try to make sure it doesn't work so we notice for sure.
			return -1;
		}
	}

	int id = (int)event_types.size();
	event_types.push_back(EventType{ callback, name });
	usedEventTypes.insert(id);
	return id;
}

void AntiCrashCallback(u64 userdata, int cyclesLate) {
	ERROR_LOG(Log::SaveState, "Savestate broken: an unregistered event was called.");
	Core_Break(BreakReason::SavestateCrash, 0);
}

void RestoreRegisterEvent(int &event_type, const char *name, TimedCallback callback) {
	// Some old states have a duplicate restore, do our best to fix...
	if (restoredEventTypes.count(event_type) != 0)
		event_type = -1;
	if (event_type == -1)
		event_type = nextEventTypeRestoreId++;
	if (event_type >= (int)event_types.size()) {
		// Give it any unused event id starting from the end.
		// Older save states with messed up ids have gaps near the end.
		for (int i = (int)event_types.size() - 1; i >= 0; --i) {
			if (usedEventTypes.count(i) == 0) {
				event_type = i;
				break;
			}
		}
	}
	_assert_msg_(event_type >= 0 && event_type < (int)event_types.size(), "Invalid event type %d", event_type);
	event_types[event_type] = EventType{ callback, name };
	usedEventTypes.insert(event_type);
	restoredEventTypes.insert(event_type);
}

void UnregisterAllEvents() {
	_dbg_assert_msg_(first == nullptr, "Unregistering events with events pending - this isn't good.");
	event_types.clear();
	usedEventTypes.clear();
	restoredEventTypes.clear();
}

void Init(MIPSState *mips) {
	mips->downcount = INITIAL_SLICE_LENGTH;
	slicelength = INITIAL_SLICE_LENGTH;
	globalTimer = 0;
	idledCycles = 0;
	lastGlobalTimeTicks = 0;
	lastGlobalTimeUs = 0;
	breakDeadlineUs = 0;
	breakDeadlineTicks = 0;
	CPU_HZ = initialHz;
}

void Shutdown()
{
	ClearPendingEvents();
	UnregisterAllEvents();

	while (eventPool) {
		Event *ev = eventPool;
		eventPool = ev->next;
		delete ev;
	}
}
 
u64 GetTicks(MIPSState *mips) {
	if (mips) {
		return (u64)globalTimer + slicelength - mips->downcount;
	} else {
		// Reporting can actually end up here during weird task switching sequences on Android
		return false;
	}
}

u64 GetIdleTicks() {
	return (u64)idledCycles;
}

void ClearPendingEvents()
{
	while (first)
	{
		Event *e = first->next;
		FreeEvent(first);
		first = e;
	}
}

void AddEventToQueue(Event* ne)
{
	Event* prev = NULL;
	Event** pNext = &first;
	for(;;)
	{
		Event*& next = *pNext;
		if(!next || ne->time < next->time)
		{
			ne->next = next;
			next = ne;
			break;
		}
		prev = next;
		pNext = &prev->next;
	}
}

// This must be run ONLY from within the cpu thread
// cyclesIntoFuture may be VERY inaccurate if called from anything else
// than Advance
void ScheduleEvent(s64 cyclesIntoFuture, int event_type, u64 userdata)
{
	Event *ne = GetNewEvent();
	ne->userdata = userdata;
	ne->type = event_type;
	ne->time = GetTicks(currentMIPS) + cyclesIntoFuture;
	AddEventToQueue(ne);
}

// Returns cycles left in timer.
s64 UnscheduleEvent(int event_type, u64 userdata)
{
	s64 result = 0;
	if (!first)
		return result;
	while(first)
	{
		if (first->type == event_type && first->userdata == userdata)
		{
			result = first->time - GetTicks(currentMIPS);

			Event *next = first->next;
			FreeEvent(first);
			first = next;
		}
		else
		{
			break;
		}
	}
	if (!first)
		return result;
	Event *prev = first;
	Event *ptr = prev->next;
	while (ptr)
	{
		if (ptr->type == event_type && ptr->userdata == userdata)
		{
			result = ptr->time - GetTicks(currentMIPS);

			prev->next = ptr->next;
			FreeEvent(ptr);
			ptr = prev->next;
		}
		else
		{
			prev = ptr;
			ptr = ptr->next;
		}
	}

	return result;
}

bool IsScheduled(int event_type) {
	if (!first)
		return false;
	Event *e = first;
	while (e) {
		if (e->type == event_type)
			return true;
		e = e->next;
	}
	return false;
}

void RemoveEvent(int event_type)
{
	if (!first)
		return;
	while(first)
	{
		if (first->type == event_type)
		{
			Event *next = first->next;
			FreeEvent(first);
			first = next;
		}
		else
		{
			break;
		}
	}
	if (!first)
		return;
	Event *prev = first;
	Event *ptr = prev->next;
	while (ptr)
	{
		if (ptr->type == event_type)
		{
			prev->next = ptr->next;
			FreeEvent(ptr);
			ptr = prev->next;
		}
		else
		{
			prev = ptr;
			ptr = ptr->next;
		}
	}
}

void ProcessEvents() {
	while (first) {
		if (first->time <= (s64)GetTicks(currentMIPS)) {
			// INFO_LOG(Log::CPU, "%s (%lld, %lld) ", first->name ? first->name : "?", (u64)GetTicks(currentMIPS), (u64)first->time);
			Event *evt = first;
			first = first->next;
			if (evt->type >= 0 && evt->type < (int)event_types.size()) {
				event_types[evt->type].callback(evt->userdata, (int)(GetTicks(currentMIPS) - evt->time));
			} else {
				_dbg_assert_msg_(false, "Bad event type %d", evt->type);
			}
			FreeEvent(evt);
		} else {
			// Caught up to the current time.
			break;
		}
	}
}

void ForceCheck(MIPSState *mips) {
	int cyclesExecuted = slicelength - mips->downcount;
	globalTimer += cyclesExecuted;
	// This will cause us to check for new events immediately.
	mips->downcount = -1;
	// But let's not eat a bunch more time in Advance() because of this.
	slicelength = -1;

#ifdef _DEBUG
	_dbg_assert_msg_( cyclesExecuted >= 0, "Shouldn't have a negative cyclesExecuted");
#endif
}

void Advance(MIPSState *mips) {
	PROFILE_THIS_SCOPE("advance");
	int cyclesExecuted = slicelength - mips->downcount;
	globalTimer += cyclesExecuted;
	mips->downcount = slicelength;

	// Debugger deadline - see SetBreakDeadlineTicks. Checked before the events so the break lands
	// on the requested tick rather than after whatever the events do.
	if (breakDeadlineTicks && globalTimer >= breakDeadlineTicks) {
		breakDeadlineTicks = 0;
		breakDeadlineUs = 0;
		Core_Break(BreakReason::RunUntilTime, mips->pc);
	}

	ProcessEvents();

	if (!first) {
		// This should never happen in PPSSPP.
		if (slicelength < 10000) {
			slicelength += 10000;
			mips->downcount += 10000;
		}
	} else {
		// Note that events can eat cycles as well.
		int target = (int)(first->time - globalTimer);
		if (target > MAX_SLICE_LENGTH)
			target = MAX_SLICE_LENGTH;

		const int diff = target - slicelength;
		slicelength += diff;
		mips->downcount += diff;
	}

	// Shorten the slice so we come back exactly on the deadline instead of up to a whole slice
	// past it - the point of cpu.runUntilTime is that it stops at a reproducible place.
	if (breakDeadlineTicks) {
		const s64 remaining = breakDeadlineTicks - globalTimer;
		if (remaining > 0 && remaining < slicelength) {
			const int diff = (int)remaining - slicelength;
			slicelength += diff;
			mips->downcount += diff;
		}
	}
}

void LogPendingEvents() {
	Event *ptr = first;
	while (ptr) {
		//INFO_LOG(Log::CPU, "PENDING: Now: %lld Pending: %lld Type: %d", globalTimer, ptr->time, ptr->type);
		ptr = ptr->next;
	}
}

void Idle(MIPSState *mips, int maxIdle) {
	int cyclesDown = mips->downcount;
	if (maxIdle != 0 && cyclesDown > maxIdle)
		cyclesDown = maxIdle;

	if (first && cyclesDown > 0) {
		int cyclesExecuted = slicelength - mips->downcount;
		int cyclesNextEvent = (int) (first->time - globalTimer);

		if (cyclesNextEvent < cyclesExecuted + cyclesDown)
			cyclesDown = cyclesNextEvent - cyclesExecuted;
	}

	// Now, now... no time machines, please.
	if (cyclesDown < 0)
		cyclesDown = 0;

	// VERBOSE_LOG(Log::CPU, "Idle for %i cycles! (%f ms)", cyclesDown, cyclesDown / (float)(CPU_HZ * 0.001f));

	idledCycles += cyclesDown;
	mips->downcount -= cyclesDown;
	if (mips->downcount == 0)
		mips->downcount = -1;
}

std::string GetScheduledEventsSummary() {
	Event *ptr = first;
	std::string text = "Scheduled events\n";
	text.reserve(1000);
	while (ptr) {
		unsigned int t = ptr->type;
		if (t >= event_types.size()) {
			_dbg_assert_msg_(false, "Invalid event type %d", t);
			ptr = ptr->next;
			continue;
		}
		const char *name = event_types[t].name;
		if (!name)
			name = "[unknown]";
		char temp[512];
		snprintf(temp, sizeof(temp), "%s : %i %08x%08x\n", name, (int)ptr->time, (u32)(ptr->userdata >> 32), (u32)(ptr->userdata));
		text += temp;
		ptr = ptr->next;
	}
	return text;
}

void Event_DoState(PointerWrap &p, BaseEvent *ev) {
	// There may be padding, so do each one individually.
	Do(p, ev->time);
	Do(p, ev->userdata);
	Do(p, ev->type);
	usedEventTypes.insert(ev->type);
}

void Event_DoStateOld(PointerWrap &p, BaseEvent *ev) {
	Do(p, *ev);
	usedEventTypes.insert(ev->type);
}

void DoState(PointerWrap &p) {
	auto s = p.Section("CoreTiming", 1, 3);
	if (!s)
		return;

	int n = (int)event_types.size();
	int current = n;
	Do(p, n);
	if (n > current) {
		WARN_LOG(Log::SaveState, "Savestate failure: more events than current (can't ever remove an event)");
		p.SetError(p.ERROR_FAILURE);
		return;
	}

	// These (should) be filled in later by the modules.
	for (int i = 0; i < current; ++i) {
		event_types[i].callback = AntiCrashCallback;
		event_types[i].name = "INVALID EVENT";
	}
	nextEventTypeRestoreId = n - 1;
	usedEventTypes.clear();
	restoredEventTypes.clear();

	if (s >= 3) {
		DoLinkedList<BaseEvent, GetNewEvent, FreeEvent, Event_DoState>(p, first, (Event **)nullptr);
		// This is here because we previously stored a second queue of "threadsafe" events. Gone now. Remove in the next section version upgrade.
		DoIgnoreUnusedLinkedList(p);
	} else {
		DoLinkedList<BaseEvent, GetNewEvent, FreeEvent, Event_DoStateOld>(p, first, (Event **)nullptr);
		DoIgnoreUnusedLinkedList(p);
	}

	Do(p, CPU_HZ);
	Do(p, slicelength);
	Do(p, globalTimer);
	Do(p, idledCycles);

	if (s >= 2) {
		Do(p, lastGlobalTimeTicks);
		Do(p, lastGlobalTimeUs);
	} else {
		lastGlobalTimeTicks = 0;
		lastGlobalTimeUs = 0;
	}

	__AudioCPUMHzChange();
}

}	// namespace

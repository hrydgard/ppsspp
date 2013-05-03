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


#include <vector>
#include <cstdio>

#include "MsgHandler.h"
#include "StdMutex.h"
#include "Atomics.h"
#include "CoreTiming.h"
#include "Core.h"
#include "HLE/sceKernelThread.h"
#include "../Common/ChunkFile.h"

int CPU_HZ = 222000000;

// is this really necessary?
#define INITIAL_SLICE_LENGTH 20000
#define MAX_SLICE_LENGTH 100000000

namespace CoreTiming
{

struct EventType
{
	EventType() {}

	EventType(TimedCallback cb, const char *n)
		: callback(cb), name(n) {}

	TimedCallback callback;
	const char *name;
};

std::vector<EventType> event_types;

struct BaseEvent
{
	s64 time;
	u64 userdata;
	int type;
//	Event *next;
};

typedef LinkedListItem<BaseEvent> Event;

Event *first;
Event *tsFirst;
Event *tsLast;

// event pools
Event *eventPool = 0;
Event *eventTsPool = 0;
int allocatedTsEvents = 0;
// Optimization to skip MoveEvents when possible.
volatile u32 hasTsEvents = false;

// Downcount has been moved to currentMIPS, to save a couple of clocks in every ARM JIT block
// as we can already reach that structure through a register.
int slicelength;

s64 globalTimer;
s64 idledCycles;

static std::recursive_mutex externalEventSection;

// Warning: not included in save state.
void (*advanceCallback)(int cyclesExecuted) = NULL;

void SetClockFrequencyMHz(int cpuMhz)
{
	CPU_HZ = cpuMhz * 1000000;
	// TODO: Rescale times of scheduled events?
}

int GetClockFrequencyMHz()
{
	return CPU_HZ / 1000000;
}


Event* GetNewEvent()
{
	if(!eventPool)
		return new Event;

	Event* ev = eventPool;
	eventPool = ev->next;
	return ev;
}

Event* GetNewTsEvent()
{
	allocatedTsEvents++;

	if(!eventTsPool)
		return new Event;

	Event* ev = eventTsPool;
	eventTsPool = ev->next;
	return ev;
}

void FreeEvent(Event* ev)
{
	ev->next = eventPool;
	eventPool = ev;
}

void FreeTsEvent(Event* ev)
{
	ev->next = eventTsPool;
	eventTsPool = ev;
	allocatedTsEvents--;
}

int RegisterEvent(const char *name, TimedCallback callback)
{
	event_types.push_back(EventType(callback, name));
	return (int)event_types.size() - 1;
}

void AntiCrashCallback(u64 userdata, int cyclesLate)
{
	ERROR_LOG(CPU, "Savestate broken: an unregistered event was called.");
	Core_Halt("invalid timing events");
}

void RestoreRegisterEvent(int event_type, const char *name, TimedCallback callback)
{
	if (event_type >= (int) event_types.size())
		event_types.resize(event_type + 1, EventType(AntiCrashCallback, "INVALID EVENT"));

	event_types[event_type] = EventType(callback, name);
}

void UnregisterAllEvents()
{
	if (first)
		PanicAlert("Cannot unregister events with events pending");
	event_types.clear();
}

void Init()
{
	currentMIPS->downcount = INITIAL_SLICE_LENGTH;
	slicelength = INITIAL_SLICE_LENGTH;
	globalTimer = 0;
	idledCycles = 0;
	hasTsEvents = 0;
}

void Shutdown()
{
	MoveEvents();
	ClearPendingEvents();
	UnregisterAllEvents();

	while(eventPool)
	{
		Event *ev = eventPool;
		eventPool = ev->next;
		delete ev;
	}

	std::lock_guard<std::recursive_mutex> lk(externalEventSection);
	while(eventTsPool)
	{
		Event *ev = eventTsPool;
		eventTsPool = ev->next;
		delete ev;
	}
}

u64 GetTicks()
{
	return (u64)globalTimer + slicelength - currentMIPS->downcount;
}

u64 GetIdleTicks()
{
	return (u64)idledCycles;
}


// This is to be called when outside threads, such as the graphics thread, wants to
// schedule things to be executed on the main thread.
void ScheduleEvent_Threadsafe(s64 cyclesIntoFuture, int event_type, u64 userdata)
{
	std::lock_guard<std::recursive_mutex> lk(externalEventSection);
	Event *ne = GetNewTsEvent();
	ne->time = globalTimer + cyclesIntoFuture;
	ne->type = event_type;
	ne->next = 0;
	ne->userdata = userdata;
	if(!tsFirst)
		tsFirst = ne;
	if(tsLast)
		tsLast->next = ne;
	tsLast = ne;

	Common::AtomicStoreRelease(hasTsEvents, 1);
}

// Same as ScheduleEvent_Threadsafe(0, ...) EXCEPT if we are already on the CPU thread
// in which case the event will get handled immediately, before returning.
void ScheduleEvent_Threadsafe_Immediate(int event_type, u64 userdata)
{
	if(false) //Core::IsCPUThread())
	{
		std::lock_guard<std::recursive_mutex> lk(externalEventSection);
		event_types[event_type].callback(userdata, 0);
	}
	else
		ScheduleEvent_Threadsafe(0, event_type, userdata);
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
	ne->time = GetTicks() + cyclesIntoFuture;
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
			result = first->time - globalTimer;

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
			result = ptr->time - globalTimer;

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

s64 UnscheduleThreadsafeEvent(int event_type, u64 userdata)
{
	s64 result = 0;
	std::lock_guard<std::recursive_mutex> lk(externalEventSection);
	if (!tsFirst)
		return result;
	while(tsFirst)
	{
		if (tsFirst->type == event_type && tsFirst->userdata == userdata)
		{
			result = tsFirst->time - globalTimer;

			Event *next = tsFirst->next;
			FreeTsEvent(tsFirst);
			tsFirst = next;
		}
		else
		{
			break;
		}
	}
	if (!tsFirst)
		return result;

	Event *prev = tsFirst;
	Event *ptr = prev->next;
	while (ptr)
	{
		if (ptr->type == event_type && ptr->userdata == userdata)
		{
			result = ptr->time - globalTimer;

			prev->next = ptr->next;
			FreeTsEvent(ptr);
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

// Warning: not included in save state.
void RegisterAdvanceCallback(void (*callback)(int cyclesExecuted))
{
	advanceCallback = callback;
}

bool IsScheduled(int event_type) 
{
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

void RemoveThreadsafeEvent(int event_type)
{
	std::lock_guard<std::recursive_mutex> lk(externalEventSection);
	if (!tsFirst)
	{
		return;
	}
	while(tsFirst)
	{
		if (tsFirst->type == event_type)
		{
			Event *next = tsFirst->next;
			FreeTsEvent(tsFirst);
			tsFirst = next;
		}
		else
		{
			break;
		}
	}
	if (!tsFirst)
	{
		return;
	}
	Event *prev = tsFirst;
	Event *ptr = prev->next;
	while (ptr)
	{
		if (ptr->type == event_type)
		{	
			prev->next = ptr->next;
			FreeTsEvent(ptr);
			ptr = prev->next;
		}
		else
		{
			prev = ptr;
			ptr = ptr->next;
		}
	}
}

void RemoveAllEvents(int event_type)
{	
	RemoveThreadsafeEvent(event_type);
	RemoveEvent(event_type);
}

//This raise only the events required while the fifo is processing data
void ProcessFifoWaitEvents()
{
	while (first)
	{
		if (first->time <= globalTimer)
		{
//			LOG(CPU, "[Scheduler] %s		 (%lld, %lld) ", 
//				first->name ? first->name : "?", (u64)globalTimer, (u64)first->time);
			Event* evt = first;
			first = first->next;
			event_types[evt->type].callback(evt->userdata, (int)(globalTimer - evt->time));
			FreeEvent(evt);
		}
		else
		{
			break;
		}
	}
}

void MoveEvents()
{
	Common::AtomicStoreRelease(hasTsEvents, 0);

	std::lock_guard<std::recursive_mutex> lk(externalEventSection);
		// Move events from async queue into main queue
	while (tsFirst)
	{
		Event *next = tsFirst->next;
		AddEventToQueue(tsFirst);
		tsFirst = next;
	}
	tsLast = NULL;

	// Move free events to threadsafe pool
	while(allocatedTsEvents > 0 && eventPool)
	{
		Event *ev = eventPool;
		eventPool = ev->next;
		ev->next = eventTsPool;
		eventTsPool = ev;
		allocatedTsEvents--;
	}
}

void AdvanceQuick()
{
	int cyclesExecuted = slicelength - currentMIPS->downcount;
	globalTimer += cyclesExecuted;
	currentMIPS->downcount = slicelength;

	ProcessFifoWaitEvents();

	if (!first)
	{
		// WARN_LOG(CPU, "WARNING - no events in queue. Setting currentMIPS->downcount to 10000");
		currentMIPS->downcount += 10000;
	}
	else
	{
		slicelength = (int)(first->time - globalTimer);
		if (slicelength > MAX_SLICE_LENGTH)
			slicelength = MAX_SLICE_LENGTH;
		currentMIPS->downcount = slicelength;
	}
	if (advanceCallback)
		advanceCallback(cyclesExecuted);
}

void Advance()
{
	if (Common::AtomicLoadAcquire(hasTsEvents))
		MoveEvents();

	AdvanceQuick();
}

void LogPendingEvents()
{
	Event *ptr = first;
	while (ptr)
	{
		//INFO_LOG(CPU, "PENDING: Now: %lld Pending: %lld Type: %d", globalTimer, ptr->time, ptr->type);
		ptr = ptr->next;
	}
}

void Idle(int maxIdle)
{
	int cyclesDown = currentMIPS->downcount;
	if (maxIdle != 0 && cyclesDown > maxIdle)
		cyclesDown = maxIdle;

	if (first && cyclesDown > 0)
	{
		int cyclesExecuted = slicelength - currentMIPS->downcount;
		int cyclesNextEvent = (int) (first->time - globalTimer);

		if (cyclesNextEvent < cyclesExecuted + cyclesDown)
		{
			cyclesDown = cyclesNextEvent - cyclesExecuted;
			// Now, now... no time machines, please.
			if (cyclesDown < 0)
				cyclesDown = 0;
		}
	}

	VERBOSE_LOG(CPU, "Idle for %i cycles! (%f ms)", cyclesDown, cyclesDown / (float)(CPU_HZ * 0.001f));

	idledCycles += cyclesDown;
	currentMIPS->downcount -= cyclesDown;
	if (currentMIPS->downcount == 0)
		currentMIPS->downcount = -1;
}

std::string GetScheduledEventsSummary()
{
	Event *ptr = first;
	std::string text = "Scheduled events\n";
	text.reserve(1000);
	while (ptr)
	{
		unsigned int t = ptr->type;
		if (t >= event_types.size())
			PanicAlert("Invalid event type"); // %i", t);
		const char *name = event_types[ptr->type].name;
		if (!name)
			name = "[unknown]";
		char temp[512];
		sprintf(temp, "%s : %i %08x%08x\n", name, (int)ptr->time, (u32)(ptr->userdata >> 32), (u32)(ptr->userdata));
		text += temp;
		ptr = ptr->next;
	}
	return text;
}

void Event_DoState(PointerWrap &p, BaseEvent *ev)
{
	p.Do(*ev);
}

void DoState(PointerWrap &p)
{
	std::lock_guard<std::recursive_mutex> lk(externalEventSection);

	int n = (int) event_types.size();
	p.Do(n);
	// These (should) be filled in later by the modules.
	event_types.resize(n, EventType(AntiCrashCallback, "INVALID EVENT"));

	p.DoLinkedList<BaseEvent, GetNewEvent, FreeEvent, Event_DoState>(first, (Event **) NULL);
	p.DoLinkedList<BaseEvent, GetNewTsEvent, FreeTsEvent, Event_DoState>(tsFirst, &tsLast);

	p.Do(CPU_HZ);
	p.Do(slicelength);
	p.Do(globalTimer);
	p.Do(idledCycles);
	p.DoMarker("CoreTiming");
}

}	// namespace

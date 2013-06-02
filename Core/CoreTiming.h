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

#ifndef _CORETIMING_H
#define _CORETIMING_H

// This is a system to schedule events into the emulated machine's future. Time is measured
// in main CPU clock cycles.

// To schedule an event, you first have to register its type. This is where you pass in the
// callback. You then schedule events using the type id you get back.

// See HW/SystemTimers.cpp for the main part of Dolphin's usage of this scheduler.

// The int cyclesLate that the callbacks get is how many cycles late it was.
// So to schedule a new event on a regular basis:
// inside callback:
//   ScheduleEvent(periodInCycles - cyclesLate, callback, "whatever")

#include "../Globals.h"

class PointerWrap;

//const int CPU_HZ = 222000000;
extern int CPU_HZ;

inline s64 msToCycles(int ms) {
	return CPU_HZ / 1000 * ms;
}

inline s64 msToCycles(float ms) {
	return (s64)(CPU_HZ * ms * (0.001f));
}

inline s64 msToCycles(double ms) {
	return (s64)(CPU_HZ * ms * (0.001));
}

inline s64 usToCycles(float us) {
	return (s64)(CPU_HZ * us * (0.000001f));
}

inline s64 usToCycles(int us) {
	return (CPU_HZ / 1000000 * (s64)us);
}

inline s64 usToCycles(s64 us) {
	return (CPU_HZ / 1000000 * us);
}

inline s64 usToCycles(u64 us) {
	return (s64)(CPU_HZ / 1000000 * us);
}

inline s64 cyclesToUs(s64 cycles) {
	return cycles / (CPU_HZ / 1000000);
}

namespace CoreTiming
{
	void Init();
	void Shutdown();

	typedef void (*TimedCallback)(u64 userdata, int cyclesLate);

	u64 GetTicks();
	u64 GetIdleTicks();

	// Returns the event_type identifier.
	int RegisterEvent(const char *name, TimedCallback callback);
	// For save states.
	void RestoreRegisterEvent(int event_type, const char *name, TimedCallback callback);
	void UnregisterAllEvents();

	// userdata MAY NOT CONTAIN POINTERS. userdata might get written and reloaded from disk,
	// when we implement state saves.
	void ScheduleEvent(s64 cyclesIntoFuture, int event_type, u64 userdata=0);
	void ScheduleEvent_Threadsafe(s64 cyclesIntoFuture, int event_type, u64 userdata=0);
	void ScheduleEvent_Threadsafe_Immediate(int event_type, u64 userdata=0);
	s64 UnscheduleEvent(int event_type, u64 userdata);
	s64 UnscheduleThreadsafeEvent(int event_type, u64 userdata);

	void RemoveEvent(int event_type);
	void RemoveThreadsafeEvent(int event_type);
	void RemoveAllEvents(int event_type);
	bool IsScheduled(int event_type);
	void Advance();
	void AdvanceQuick();
	void MoveEvents();
	void ProcessFifoWaitEvents();

	// Pretend that the main CPU has executed enough cycles to reach the next event.
	void Idle(int maxIdle = 0);

	// Clear all pending events. This should ONLY be done on exit or state load.
	void ClearPendingEvents();

	void LogPendingEvents();

	// Warning: not included in save states.
	void RegisterAdvanceCallback(void (*callback)(int cyclesExecuted));

	std::string GetScheduledEventsSummary();

	void DoState(PointerWrap &p);

	void SetClockFrequencyMHz(int cpuMhz);
	int GetClockFrequencyMHz();
	extern int slicelength;

}; // end of namespace

#endif

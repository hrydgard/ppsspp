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

#include "../Common/StdMutex.h"
#include <vector>

#include "SaveState.h"
#include "Core.h"
#include "CoreTiming.h"
#include "HLE/sceKernel.h"
#include "HW/MemoryStick.h"
#include "MemMap.h"

namespace SaveState
{
	struct SaveStart
	{
		void DoState(PointerWrap &p);
	};

	enum OperationType
	{
		SAVESTATE_SAVE,
		SAVESTATE_LOAD,
		SAVESTATE_VERIFY,
	};

	struct Operation
	{
		Operation(OperationType t, std::string &f, Callback cb)
			: type(t), filename(f), callback(cb)
		{
		}

		OperationType type;
		std::string filename;
		Callback callback;
	};

	static int timer;
	static std::vector<Operation> pending;
	static std::recursive_mutex mutex;

	// This is where the magic happens.
	void SaveStart::DoState(PointerWrap &p)
	{
		// Gotta do CoreTiming first since we'll restore into it.
		// TODO CoreTiming

		// This save state even saves its own state.
		p.Do(timer);
		CoreTiming::RestoreEvent(timer, "SaveState", Process);
		p.DoMarker("SaveState");

		Memory::DoState(p);
		MemoryStick_DoState(p);
		__KernelDoState(p);
	}

	void Process(u64 userdata, int cyclesLate);

	void Enqueue(SaveState::Operation op)
	{
		std::lock_guard<std::recursive_mutex> guard(mutex);
		pending.push_back(op);

		// Don't actually run it until next CoreTiming::Advance().
		// It's possible there might be a duplicate but it won't hurt us.
		if (Core_IsStepping())
		{
			// Warning: this may run on a different thread.
			Process(0, 0);
		}
		else
			CoreTiming::ScheduleEvent_Threadsafe(0, timer);
	}

	void Load(std::string &filename, Callback callback)
	{
		Enqueue(Operation(SAVESTATE_LOAD, filename, callback));
	}

	void Save(std::string &filename, Callback callback)
	{
		Enqueue(Operation(SAVESTATE_SAVE, filename, callback));
	}

	void Verify(Callback callback)
	{
		Enqueue(Operation(SAVESTATE_VERIFY, std::string(""), callback));
	}

	std::vector<Operation> Flush()
	{
		std::lock_guard<std::recursive_mutex> guard(mutex);
		std::vector<Operation> copy = pending;
		pending.clear();

		return copy;
	}

	void Process(u64 userdata, int cyclesLate)
	{
		std::vector<Operation> operations = Flush();
		SaveStart state;

		for (size_t i = 0, n = operations.size(); i < n; ++i)
		{
			Operation &op = operations[i];
			bool result;

			switch (op.type)
			{
			case SAVESTATE_LOAD:
				INFO_LOG(COMMON, "Loading state from %s", op.filename.c_str());
				result = CChunkFileReader::Load(op.filename, REVISION, state);
				break;

			case SAVESTATE_SAVE:
				INFO_LOG(COMMON, "Saving state to %s", op.filename.c_str());
				result = CChunkFileReader::Save(op.filename, REVISION, state);
				break;

			case SAVESTATE_VERIFY:
				INFO_LOG(COMMON, "Verifying save state system");
				result = CChunkFileReader::Verify(state);
				break;

			default:
				ERROR_LOG(COMMON, "Savestate failure: unknown operation type %d", op.type);
				result = false;
				break;
			}

			if (op.callback != NULL)
				op.callback(result);
		}
	}

	void Init()
	{
		timer = CoreTiming::RegisterEvent("SaveState", Process);
	}
}

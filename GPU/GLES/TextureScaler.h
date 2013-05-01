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

#pragma once

#include "Common/MemoryUtil.h"
#include "../Globals.h"
#include "../native/ext/glew/GL/glew.h"

#include <functional>
#include <vector>

#include "native/thread/thread.h"
#include "base/mutex.h"

// This is the simplest possible worker implementation I can think of
// but entirely sufficient for the given purpose.
// Only handles a single item of work at a time.
class WorkerThread {
public:
	WorkerThread();
	~WorkerThread();

	// submit a new work item
	void Process(const std::function<void()>& work);
	// wait for a submitted work item to be completed
	void WaitForCompletion();

private:
	std::thread *thread; // the worker thread
	condition_variable signal; // used to signal new work
	condition_variable done; // used to signal work completion
	recursive_mutex mutex, doneMutex; // associated with each respective condition variable
	volatile bool active, started;
	std::function<void()> work_; // the work to be done by this thread

	void WorkFunc();

	WorkerThread(const WorkerThread& other) { } // prevent copies
};

class TextureScaler {
public:
	TextureScaler();

	void Scale(u32* &data, GLenum &dstfmt, int &width, int &height);

private:
	const int numThreads;
	std::vector<std::shared_ptr<WorkerThread>> workers;

	bool workersStarted;
	void StartWorkers();

	void ParallelLoop(std::function<void(int,int)> loop, int lower, int upper);

	SimpleBuf<u32> bufInput;
	SimpleBuf<u32> bufOutput;
};

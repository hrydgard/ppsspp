// Copyright (c) 2014- PPSSPP Project.

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

#include <list>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "input/input_state.h"
#include "thread/threadutil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Windows/InputDevice.h"
#include "Windows/WindowsHost.h"

static volatile bool inputThreadStatus = false;
static volatile bool inputThreadEnabled = false;
static std::thread *inputThread = NULL;
static std::mutex inputMutex;
static std::condition_variable inputEndCond;
static bool focused = true;

inline static void ExecuteInputPoll() {
	if (host && (focused || !g_Config.bGamepadOnlyFocused)) {
		host->PollControllers();
	}
}

static void RunInputThread() {
	setCurrentThreadName("Input");

	// NOTE: The keyboard and mouse buttons are handled via raw input, not here.
	// This is mainly for controllers which need to be polled, instead of generating events.

	while (inputThreadEnabled) {
		ExecuteInputPoll();

		// Try to update 250 times per second.
		Sleep(4);
	}

	std::lock_guard<std::mutex> guard(inputMutex);
	inputThreadStatus = false;
	inputEndCond.notify_one();
}

void InputDevice::BeginPolling() {
	std::lock_guard<std::mutex> guard(inputMutex);
	inputThreadEnabled = true;
	inputThread = new std::thread(&RunInputThread);
	inputThread->detach();
}

void InputDevice::StopPolling() {
	inputThreadEnabled = false;

	std::unique_lock<std::mutex> guard(inputMutex);
	if (inputThreadStatus) {
		inputEndCond.wait(guard);
	}
	delete inputThread;
	inputThread = NULL;
}

void InputDevice::GainFocus() {
	focused = true;
}

void InputDevice::LoseFocus() {
	focused = false;
}

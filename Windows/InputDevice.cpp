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
#include <memory>
#include "base/mutex.h"
#include "input/input_state.h"
#include "thread/thread.h"
#include "thread/threadutil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Windows/InputDevice.h"
#include "Windows/XinputDevice.h"
#include "Windows/DinputDevice.h"
#include "Windows/KeyboardDevice.h"
#include "Windows/WindowsHost.h"

static volatile bool inputThreadStatus = false;
static volatile bool inputThreadEnabled = false;
static std::thread *inputThread = NULL;
static recursive_mutex inputMutex;
static condition_variable inputEndCond;
static bool focused = true;

extern InputState input_state;

inline static void ExecuteInputPoll() {
	// Hm, we may hold the input_state lock for quite a while (time it takes to poll all devices)...
	// If that becomes an issue, maybe should poll to a copy of inputstate and only hold the lock while
	// copying that one to the real one?
	lock_guard guard(input_state.lock);
	input_state.pad_buttons = 0;
	input_state.pad_lstick_x = 0;
	input_state.pad_lstick_y = 0;
	input_state.pad_rstick_x = 0;
	input_state.pad_rstick_y = 0;
	if (host && (focused || !g_Config.bGamepadOnlyFocused)) {
		host->PollControllers(input_state);
	}
	UpdateInputState(&input_state);
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

	lock_guard guard(inputMutex);
	inputThreadStatus = false;
	inputEndCond.notify_one();
}

void InputDevice::BeginPolling() {
	lock_guard guard(inputMutex);
	inputThreadEnabled = true;
	inputThread = new std::thread(&RunInputThread);
	inputThread->detach();
}

void InputDevice::StopPolling() {
	inputThreadEnabled = false;

	lock_guard guard(inputMutex);
	if (inputThreadStatus) {
		inputEndCond.wait(inputMutex);
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

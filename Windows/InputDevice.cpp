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

#include "stdafx.h"
#include <thread>
#include <atomic>

#include "Common/Input/InputState.h"
#include "Common/System/System.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Config.h"
#include "Windows/InputDevice.h"

static std::atomic_flag threadRunningFlag;
static std::thread inputThread;
static std::atomic_bool focused = ATOMIC_VAR_INIT(true);

inline static void ExecuteInputPoll() {
	if (focused.load(std::memory_order_relaxed) || !g_Config.bGamepadOnlyFocused) {
		System_Notify(SystemNotification::POLL_CONTROLLERS);
	}
}

static void RunInputThread() {
	SetCurrentThreadName("Input");

	// NOTE: The keyboard and mouse buttons are handled via raw input, not here.
	// This is mainly for controllers which need to be polled, instead of generating events.

	while (threadRunningFlag.test_and_set(std::memory_order_relaxed)) {
		ExecuteInputPoll();

		// Try to update 250 times per second.
		Sleep(4);
	}
}

void InputDevice::BeginPolling() {
	threadRunningFlag.test_and_set(std::memory_order_relaxed);
	inputThread = std::thread(&RunInputThread);
}

void InputDevice::StopPolling() {
	threadRunningFlag.clear(std::memory_order_relaxed);

	inputThread.join();
}

void InputDevice::GainFocus() {
	focused.store(true, std::memory_order_relaxed);
}

void InputDevice::LoseFocus() {
	focused.store(false, std::memory_order_relaxed);
}

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

InputManager g_InputManager;

void InputManager::InputThread() {
	SetCurrentThreadName("Input");

	for (auto &device : devices_) {
		device->Init();
	}

	// NOTE: The keyboard and mouse buttons are handled via raw input, not here.
	// This is mainly for controllers which need to be polled, instead of generating events.
	bool noSleep = false;
	while (runThread_.load(std::memory_order_relaxed)) {
		if (focused_.load(std::memory_order_relaxed) || !g_Config.bGamepadOnlyFocused) {
			System_Notify(SystemNotification::POLL_CONTROLLERS);
			for (const auto &device : devices_) {
				int state = device->UpdateState();
				if (state == InputDevice::UPDATESTATE_SKIP_PAD)
					break;
				if (state == InputDevice::UPDATESTATE_NO_SLEEP) {
					// Sleep was handled automatically.
					noSleep = true;
				}
			}
		}

		// Try to update 250 times per second.
		if (!noSleep)
			Sleep(4);
	}

	for (auto &device : devices_) {
		device->Shutdown();
	}
}

void InputManager::BeginPolling() {
	runThread_.store(true, std::memory_order_relaxed);
	inputThread_ = std::thread([this]() {
		InputThread();
	});
}

void InputManager::StopPolling() {
	runThread_.store(false, std::memory_order_relaxed);
	inputThread_.join();
}

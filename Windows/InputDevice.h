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

#pragma once

#include <memory>
#include <atomic>
#include <vector>
#include <thread>

#include "Common/CommonTypes.h"

class InputDevice {
public:
	virtual ~InputDevice() = default;

	virtual void Init() {}
	virtual void Shutdown() {}
	virtual bool HasAccelerometer() const { return false; }

	enum { UPDATESTATE_SKIP_PAD = 0x1234, UPDATESTATE_NO_SLEEP = 0x2345};
	virtual int UpdateState() = 0;
};

class InputManager {
public:
	void BeginPolling();
	void StopPolling();

	void Shutdown() {
		devices_.clear();
	}

	void GainFocus() {
		focused_.store(true, std::memory_order_relaxed);
	}
	void LoseFocus() {
		focused_.store(false, std::memory_order_relaxed);
	}

	void AddDevice(InputDevice *device) {
		devices_.emplace_back(std::unique_ptr<InputDevice>(device));
	}

	bool AnyAccelerometer() const;

private:
	void InputThread();

	std::vector<std::unique_ptr<InputDevice>> devices_;

	std::atomic<bool> runThread_;
	std::thread inputThread_;
	std::atomic<bool> focused_;
};

extern InputManager g_InputManager;

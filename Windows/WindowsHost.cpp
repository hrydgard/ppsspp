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

#include "ppsspp_config.h"

#include <algorithm>

// native stuff
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/StringUtils.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/CoreParameter.h"
#include "Core/HLE/Plugins.h"
#include "Core/System.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Instance.h"

#include "UI/OnScreenDisplay.h"

#include "Windows/EmuThread.h"
#include "Windows/WindowsHost.h"
#include "Windows/MainWindow.h"

#ifndef _M_ARM
#include "Windows/DinputDevice.h"
#endif
#include "Windows/XinputDevice.h"

#include "Windows/main.h"

void WindowsInputManager::PollControllers() {
	static const int CHECK_FREQUENCY = 71;  // Just an arbitrary prime to try to not collide with other periodic checks.
	if (checkCounter_++ > CHECK_FREQUENCY) {
#ifndef _M_ARM
		size_t newCount = DinputDevice::getNumPads();
		if (newCount > numDinputDevices_) {
			INFO_LOG(Log::System, "New controller device detected");
			for (size_t i = numDinputDevices_; i < newCount; i++) {
				input.push_back(std::make_unique<DinputDevice>(static_cast<int>(i)));
			}
			numDinputDevices_ = newCount;
		}
#endif
		checkCounter_ = 0;
	}

	for (const auto &device : input) {
		if (device->UpdateState() == InputDevice::UPDATESTATE_SKIP_PAD)
			break;
	}

	// Disabled by default, needs a workaround to map to psp keys.
	if (g_Config.bMouseControl) {
		NativeMouseDelta(mouseDeltaX_, mouseDeltaY_);
	}

	mouseDeltaX_ *= g_Config.fMouseSmoothing;
	mouseDeltaY_ *= g_Config.fMouseSmoothing;

	HLEPlugins::PluginDataAxis[JOYSTICK_AXIS_MOUSE_REL_X] = mouseDeltaX_;
	HLEPlugins::PluginDataAxis[JOYSTICK_AXIS_MOUSE_REL_Y] = mouseDeltaY_;
}

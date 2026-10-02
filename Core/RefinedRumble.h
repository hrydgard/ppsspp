#pragma once

#include "Common/Input/InputState.h"
#include "Core/Config.h"
#include "Core/Core.h"

inline bool IsRefinedRumbleEnabled() {
	return g_Config.bControllerHapticFeedback && Core_IsActive();
}

inline bool IsRefinedRumbleInputAllowed(const InputMapping &mapping) {
	if (!IsRefinedRumbleEnabled())
		return false;

	if (mapping.IsAxis()) {
		const int axis = mapping.Axis(nullptr);
		if (axis != JOYSTICK_AXIS_LTRIGGER && axis != JOYSTICK_AXIS_RTRIGGER)
			return false;
	} else {
		switch (mapping.keyCode) {
		case NKCODE_DPAD_UP:
		case NKCODE_DPAD_DOWN:
		case NKCODE_DPAD_LEFT:
		case NKCODE_DPAD_RIGHT:
		case NKCODE_DPAD_CENTER:
		case NKCODE_BUTTON_THUMBL:
		case NKCODE_BUTTON_THUMBR:
			return false;
		default:
			break;
		}
	}

	for (const std::string &excludedInput : g_Config.sControllerVibrationPauseButton) {
		if (!excludedInput.empty() && InputMapping::FromConfigString(excludedInput) == mapping)
			return false;
	}
	return true;
}

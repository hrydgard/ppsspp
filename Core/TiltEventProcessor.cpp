#define _USE_MATH_DEFINES

#include <algorithm>
#include <cmath>
#include <mutex>

#include "Common/Math/math_util.h"
#include "Common/Math/lin/vec3.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Log.h"
#include "Common/System/Display.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/TiltEventProcessor.h"

namespace TiltEventProcessor {

static u32 tiltButtonsDown = 0;
float rawTiltAnalogX;
float rawTiltAnalogY;

float g_currentYAngle = 0.0f;

float GetCurrentYAngle() {
	return g_currentYAngle;
}

// These functions generate tilt events given the current Tilt amount,
// and the deadzone radius.
void GenerateAnalogStickEvent(float analogX, float analogY);
void GenerateDPadEvent(int digitalX, int digitalY);
void GenerateActionButtonEvent(int digitalX, int digitalY);
void GenerateTriggerButtonEvent(int digitalX, int digitalY);

}

// deadzone is normalized - 0 to 1
// sensitivity controls how fast the deadzone reaches max value
inline float ApplyDeadzoneAxis(float x, float deadzone) {
	if (deadzone >= 0.99f) {
		// Meaningless, and not reachable with normal controls.
		return x;
	}
	const float factor = 1.0f / (1.0f - deadzone);
	if (x > deadzone) {
		return (x - deadzone) * factor + deadzone;
	} else if (x < -deadzone) {
		return (x + deadzone) * factor - deadzone;
	} else {
		return 0.0f;
	}
}


inline void ApplyDeadzoneXY(float x, float y, float *adjustedX, float *adjustedY, float deadzone, bool circular) {
	if (circular) {
		if (x == 0.0f && y == 0.0f) {
			*adjustedX = 0.0f;
			*adjustedY = 0.0f;
			return;
		}

		float magnitude = sqrtf(x * x + y * y);
		if (magnitude <= deadzone + 0.00001f) {
			*adjustedX = 0.0f;
			*adjustedY = 0.0f;
			return;
		}

		float factor = 1.0f / (1.0f - deadzone);
		float newMagnitude = (magnitude - deadzone) * factor;

		*adjustedX = (x / magnitude) * newMagnitude;
		*adjustedY = (y / magnitude) * newMagnitude;
	} else {
		*adjustedX = ApplyDeadzoneAxis(x, deadzone);
		*adjustedY = ApplyDeadzoneAxis(y, deadzone);
	}
}

namespace TiltEventProcessor {

// Also clamps to -1.0..1.0.
// This applies a (circular if desired) inverse deadzone.
inline void ApplyInverseDeadzone(float x, float y, float *outX, float *outY, float inverseDeadzone, bool circular) {
	if (inverseDeadzone == 0.0f) {
		*outX = Clamp(x, -1.0f, 1.0f);
		*outY = Clamp(y, -1.0f, 1.0f);
		return;
	}

	if (circular) {
		// If the regular deadzone centered it, let's leave it as-is.
		if (x == 0.0f && y == 0.0f) {
			*outX = x;
			*outY = y;
			return;
		}
		float magnitude = sqrtf(x * x + y * y);
		if (magnitude > 0.00001f) {
			magnitude = (magnitude + inverseDeadzone) / magnitude;
		}
		*outX = Clamp(x * magnitude, -1.0f, 1.0f);
		*outY = Clamp(y * magnitude, -1.0f, 1.0f);
	} else {
		// If the regular deadzone centered it, let's leave it as-is.
		*outX = x == 0.0f ? 0.0f : Clamp(x + copysignf(inverseDeadzone, x), -1.0f, 1.0f);
		*outY = y == 0.0f ? 0.0f : Clamp(y + copysignf(inverseDeadzone, y), -1.0f, 1.0f);
	}
}

void ProcessTilt(bool landscape, float calibrationAngle, float x, float y, float z, bool invertX, bool invertY, float xSensitivity, float ySensitivity) {
	if (g_Config.iTiltInputType == TILT_NULL) {
		// Turned off - nothing to do.
		return;
	}

	if (landscape) {
		std::swap(x, y);
	} else {
		x *= -1.0f;
	}

	Lin::Vec3 down = Lin::Vec3(x, y, z).normalized();

	float angleAroundX = atan2(down.z, down.y);
	g_currentYAngle = angleAroundX;  // TODO: Should smooth this out over time a bit.
	float yAngle = angleAroundX - calibrationAngle;
	float xAngle = asinf(down.x);

	_dbg_assert_(!my_isnanorinf(angleAroundX));
	_dbg_assert_(!my_isnanorinf(yAngle));
	_dbg_assert_(!my_isnanorinf(xAngle));

	float tiltX = xAngle;
	float tiltY = yAngle;

	// invert x and y axes if requested. Can probably remove this.
	if (invertX) {
		tiltX = -tiltX;
	}
	if (invertY) {
		tiltY = -tiltY;
	}

	// It's not obvious what the factor for converting from tilt angle to value should be,
	// but there's nothing that says that 1 would make sense. The important thing is that
	// the sensitivity sliders get a range of values that makes sense.
	const float tiltFactor = 3.0f;

	tiltX *= xSensitivity * tiltFactor;
	tiltY *= ySensitivity * tiltFactor;

	if (g_Config.iTiltInputType == TILT_ANALOG) {
		// Only analog mappings use the deadzone.
		float adjustedTiltX;
		float adjustedTiltY;
		ApplyDeadzoneXY(tiltX, tiltY, &adjustedTiltX, &adjustedTiltY, g_Config.fTiltAnalogDeadzoneRadius, g_Config.bTiltCircularDeadzone);

		_dbg_assert_(!my_isnanorinf(adjustedTiltX));
		_dbg_assert_(!my_isnanorinf(adjustedTiltY));

		// Unlike regular deadzone, where per-axis is okay, inverse deadzone (to compensate for game deadzones) really needs to be
		// applied on the two axes together.
		// TODO: Share this code with the joystick code. For now though, we keep it separate.
		ApplyInverseDeadzone(adjustedTiltX, adjustedTiltY, &adjustedTiltX, &adjustedTiltY, g_Config.fTiltInverseDeadzone, g_Config.bTiltCircularDeadzone);

		_dbg_assert_(!my_isnanorinf(adjustedTiltX));
		_dbg_assert_(!my_isnanorinf(adjustedTiltY));

		rawTiltAnalogX = adjustedTiltX;
		rawTiltAnalogY = adjustedTiltY;
		GenerateAnalogStickEvent(adjustedTiltX, adjustedTiltY);
		return;
	}

	// Remaining are digital now so do the digital check here.
	// We use a fixed 0.3 threshold instead of a deadzone since you can simply use sensitivity to set it -
	// these parameters were never independent. It should feel similar to analog that way.
	int digitalX = 0;
	int digitalY = 0;
	const float threshold = 0.5f;
	if (tiltX < -threshold) {
		digitalX = -1;
	} else if (tiltX > threshold) {
		digitalX = 1;
	}
	if (tiltY < -threshold) {
		digitalY = -1;
	} else if (tiltY > threshold) {
		digitalY = 1;
	}

	switch (g_Config.iTiltInputType) {
	case TILT_DPAD:
		GenerateDPadEvent(digitalX, digitalY);
		break;

	case TILT_ACTION_BUTTON:
		GenerateActionButtonEvent(digitalX, digitalY);
		break;

	case TILT_TRIGGER_BUTTONS:
		GenerateTriggerButtonEvent(digitalX, digitalY);
		break;

	default:
		break;
	}
}

inline float clamp(float f) {
	if (f > 1.0f) return 1.0f;
	if (f < -1.0f) return -1.0f;
	return f;
}

// TODO: Instead of __Ctrl, route data into the ControlMapper.

void GenerateAnalogStickEvent(float tiltX, float tiltY) {
	__CtrlSetAnalogXY(CTRL_STICK_LEFT, clamp(tiltX), clamp(tiltY));
}

void GenerateDPadEvent(int digitalX, int digitalY) {
	static const int dir[4] = { CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP };

	if (digitalX == 0) {
		__CtrlUpdateButtons(0, tiltButtonsDown & (CTRL_RIGHT | CTRL_LEFT));
		tiltButtonsDown &= ~(CTRL_LEFT | CTRL_RIGHT);
	}

	if (digitalY == 0) {
		__CtrlUpdateButtons(0, tiltButtonsDown & (CTRL_UP | CTRL_DOWN));
		tiltButtonsDown &= ~(CTRL_UP | CTRL_DOWN);
	}

	if (digitalX == 0 && digitalY == 0) {
		return;
	}

	int ctrlMask = 0;
	if (digitalX == -1) ctrlMask |= CTRL_LEFT;
	if (digitalX == 1) ctrlMask |= CTRL_RIGHT;
	if (digitalY == -1) ctrlMask |= CTRL_DOWN;
	if (digitalY == 1) ctrlMask |= CTRL_UP;

	ctrlMask &= ~__CtrlPeekButtons();
	__CtrlUpdateButtons(ctrlMask, 0);
	tiltButtonsDown |= ctrlMask;
}

void GenerateActionButtonEvent(int digitalX, int digitalY) {
	static const int buttons[4] = { CTRL_CIRCLE, CTRL_CROSS, CTRL_SQUARE, CTRL_TRIANGLE };

	if (digitalX == 0) {
		__CtrlUpdateButtons(0, tiltButtonsDown & (CTRL_SQUARE | CTRL_CIRCLE));
		tiltButtonsDown &= ~(CTRL_SQUARE | CTRL_CIRCLE);
	}

	if (digitalY == 0) {
		__CtrlUpdateButtons(0, tiltButtonsDown & (CTRL_TRIANGLE | CTRL_CROSS));
		tiltButtonsDown &= ~(CTRL_TRIANGLE | CTRL_CROSS);
	}

	if (digitalX == 0 && digitalY == 0) {
		return;
	}

	int ctrlMask = 0;
	if (digitalX == -1) ctrlMask |= CTRL_SQUARE;
	if (digitalX == 1) ctrlMask |= CTRL_CIRCLE;
	if (digitalY == -1) ctrlMask |= CTRL_CROSS;
	if (digitalY == 1) ctrlMask |= CTRL_TRIANGLE;

	ctrlMask &= ~__CtrlPeekButtons();
	__CtrlUpdateButtons(ctrlMask, 0);
	tiltButtonsDown |= ctrlMask;
}

void GenerateTriggerButtonEvent(int digitalX, int digitalY) {
	u32 upButtons = 0;
	u32 downButtons = 0;
	// Y axis up for both
	if (digitalY == 1) {
		downButtons = CTRL_LTRIGGER | CTRL_RTRIGGER;
	} else if (digitalX == 0) {
		upButtons = CTRL_LTRIGGER | CTRL_RTRIGGER;
	} else if (digitalX == -1) {
		downButtons = CTRL_LTRIGGER;
		upButtons = CTRL_RTRIGGER;
	} else if (digitalX == 1) {
		downButtons = CTRL_RTRIGGER;
		upButtons = CTRL_LTRIGGER;
	}

	downButtons &= ~__CtrlPeekButtons();
	__CtrlUpdateButtons(downButtons, tiltButtonsDown & upButtons);
	tiltButtonsDown = (tiltButtonsDown & ~upButtons) | downButtons;
}

void ResetTiltEvents() {
	// Reset the buttons we have marked pressed.
	__CtrlUpdateButtons(0, tiltButtonsDown);
	tiltButtonsDown = 0;
	__CtrlSetAnalogXY(CTRL_STICK_LEFT, 0.0f, 0.0f);
}

}  // namespace TiltEventProcessor

namespace MouseEventProcessor {

// Technically, we may be OK without a mutex here.
// But, the cost isn't high.
std::mutex g_mouseMutex;

float g_mouseDeltaXAccum = 0;
float g_mouseDeltaYAccum = 0;

float g_mouseDeltaX;
float g_mouseDeltaY;

void DecayMouse(double now) {
	g_mouseDeltaX = g_mouseDeltaXAccum;
	g_mouseDeltaY = g_mouseDeltaYAccum;

	const float decay = g_Config.fMouseSmoothing;

	static double lastTime = 0.0f;
	if (lastTime == 0.0) {
		lastTime = now;
		return;
	}
	double dt = now - lastTime;
	lastTime = now;

	// Decay the mouse deltas. We do an approximation of the old polling.
	// Should be able to use a smooth exponential here, when I get around to doing
	// the math.
	static double accumDt = 0.0;
	accumDt += dt;
	const double oldPollInterval = 1.0 / 250.0;  // See Windows "PollControllers".
	while (accumDt > oldPollInterval) {
		accumDt -= oldPollInterval;
		g_mouseDeltaXAccum *= decay;
		g_mouseDeltaYAccum *= decay;
	}
}

void ProcessDelta(double now, float dx, float dy) {
	std::unique_lock<std::mutex> lock(g_mouseMutex);

	// Accumulate mouse deltas, for some kind of smoothing.
	g_mouseDeltaXAccum += dx;
	g_mouseDeltaYAccum += dy;

	DecayMouse(now);
}

void MouseDeltaToAxes(double now, float *mx, float *my) {
	std::unique_lock<std::mutex> lock(g_mouseMutex);

	float scaleFactor_x = g_display.dpi_scale_x * 0.1 * g_Config.fMouseSensitivity;
	float scaleFactor_y = g_display.dpi_scale_y * 0.1 * g_Config.fMouseSensitivity;

	DecayMouse(now);

	// TODO: Make configurable.
	float mouseDeadZone = 0.1f;

	float outX = clamp_value(g_mouseDeltaX * scaleFactor_x, -1.0f, 1.0f);
	float outY = clamp_value(g_mouseDeltaY * scaleFactor_y, -1.0f, 1.0f);

	ApplyDeadzoneXY(outX, outY, mx, my, mouseDeadZone, true);
}

}  // namespace

#define _USE_MATH_DEFINES

#include <algorithm>
#include <cmath>

#include "Common/Math/math_util.h"
#include "Common/Math/lin/vec3.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Log.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/TiltEventProcessor.h"

namespace TiltEventProcessor {

static u32 tiltButtonsDown = 0;
float rawTiltAnalogX;
float rawTiltAnalogY;

// These functions generate tilt events given the current Tilt amount,
// and the deadzone radius.
void GenerateAnalogStickEvent(const Tilt &tilt);
void GenerateDPadEvent(const Tilt &tilt);
void GenerateActionButtonEvent(const Tilt &tilt);
void GenerateTriggerButtonEvent(const Tilt &tilt);

// deadzone is normalized - 0 to 1
// sensitivity controls how fast the deadzone reaches max value
inline float TiltInputCurve(float x, float deadzone, float sensitivity) {
	const float factor = sensitivity * 1.0f / (1.0f - deadzone);

	if (x > deadzone) {
		return (x - deadzone) * factor + deadzone;
	} else if (x < -deadzone) {
		return (x + deadzone) * factor - deadzone;
	} else {
		return 0.0f;
	}
}

// dampen the tilt according to the given deadzone amount.
inline Tilt DampenTilt(const Tilt &tilt, float deadzone, float xSensitivity, float ySensitivity) {
	//multiply sensitivity by 2 so that "overshoot" is possible. I personally prefer a
	//sensitivity >1 for kingdom hearts and < 1 for Gods Eater. so yes, overshoot is nice
	//to have. 
	return Tilt(
		TiltInputCurve(tilt.x_, deadzone, 2.0f * xSensitivity),
		TiltInputCurve(tilt.y_, deadzone, 2.0f * ySensitivity)
	);
}

Tilt GenTilt(bool landscape, float calibrationAngle, float x, float y, float z, bool invertX, bool invertY, float xSensitivity, float ySensitivity) {
	if (landscape) {
		std::swap(x, y);
	} else {
		x *= -1.0f;
	}

	float deadzone = g_Config.iTiltInputType > 1 ? g_Config.fTiltDigitalDeadzoneRadius : g_Config.fTiltAnalogDeadzoneRadius;

	Lin::Vec3 down = Lin::Vec3(x, y, z).normalized();

	float angleAroundX = atan2(down.z, down.y);
	float yAngle = angleAroundX - calibrationAngle;
	float xAngle = asinf(down.x);

	Tilt transformedTilt(xAngle, yAngle);

	// invert x and y axes if requested. Can probably remove this.
	if (invertX) {
		transformedTilt.x_ *= -1.0f;
	}

	if (invertY) {
		transformedTilt.y_ *= -1.0f;
	}

	// finally, dampen the tilt according to our curve.
	return DampenTilt(transformedTilt, deadzone, xSensitivity, ySensitivity);
}

void TranslateTiltToInput(const Tilt &tilt) {
	rawTiltAnalogX = tilt.x_;
	rawTiltAnalogY = tilt.y_;

	switch (g_Config.iTiltInputType) {
	case TILT_NULL:
		break;

	case TILT_ANALOG:
		GenerateAnalogStickEvent(tilt);
		break;

	case TILT_DPAD:
		GenerateDPadEvent(tilt);
		break;

	case TILT_ACTION_BUTTON:
		GenerateActionButtonEvent(tilt);
		break;

	case TILT_TRIGGER_BUTTONS:
		GenerateTriggerButtonEvent(tilt);
		break;
	}
}

inline float clamp(float f) {
	if (f > 1.0f) return 1.0f;
	if (f < -1.0f) return -1.0f;
	return f;
}

void GenerateAnalogStickEvent(const Tilt &tilt) {
	__CtrlSetAnalogXY(CTRL_STICK_LEFT, clamp(tilt.x_), clamp(tilt.y_));
}

void GenerateDPadEvent(const Tilt &tilt) {
	static const int dir[4] = { CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP };

	if (tilt.x_ == 0) {
		__CtrlButtonUp(tiltButtonsDown & (CTRL_RIGHT | CTRL_LEFT));
		tiltButtonsDown &= ~(CTRL_LEFT | CTRL_RIGHT);
	}

	if (tilt.y_ == 0) {
		__CtrlButtonUp(tiltButtonsDown & (CTRL_UP | CTRL_DOWN));
		tiltButtonsDown &= ~(CTRL_UP | CTRL_DOWN);
	}

	if (tilt.x_ == 0 && tilt.y_ == 0) {
		return;
	}

	int ctrlMask = 0;
	int direction = (int)(floorf((atan2f(-tilt.y_, tilt.x_) / (2.0f * (float)M_PI) * 8.0f) + 0.5f)) & 7;
	switch (direction) {
	case 0: ctrlMask |= CTRL_RIGHT; break;
	case 1: ctrlMask |= CTRL_RIGHT | CTRL_DOWN; break;
	case 2: ctrlMask |= CTRL_DOWN; break;
	case 3: ctrlMask |= CTRL_DOWN | CTRL_LEFT; break;
	case 4: ctrlMask |= CTRL_LEFT; break;
	case 5: ctrlMask |= CTRL_UP | CTRL_LEFT; break;
	case 6: ctrlMask |= CTRL_UP; break;
	case 7: ctrlMask |= CTRL_UP | CTRL_RIGHT; break;
	}

	ctrlMask &= ~__CtrlPeekButtons();
	__CtrlButtonDown(ctrlMask);
	tiltButtonsDown |= ctrlMask;
}

void GenerateActionButtonEvent(const Tilt &tilt) {
	static const int buttons[4] = { CTRL_CIRCLE, CTRL_CROSS, CTRL_SQUARE, CTRL_TRIANGLE };

	if (tilt.x_ == 0) {
		__CtrlButtonUp(tiltButtonsDown & (CTRL_SQUARE | CTRL_CIRCLE));
		tiltButtonsDown &= ~(CTRL_SQUARE | CTRL_CIRCLE);
	}

	if (tilt.y_ == 0) {
		__CtrlButtonUp(tiltButtonsDown & (CTRL_TRIANGLE | CTRL_CROSS));
		tiltButtonsDown &= ~(CTRL_TRIANGLE | CTRL_CROSS);
	}

	if (tilt.x_ == 0 && tilt.y_ == 0) {
		return;
	}

	int direction = (int)(floorf((atan2f(-tilt.y_, tilt.x_) / (2.0f * (float)M_PI) * 4.0f) + 0.5f)) & 3;
	int downButtons = buttons[direction] & ~__CtrlPeekButtons();
	__CtrlButtonDown(downButtons);
	tiltButtonsDown |= downButtons;
}

void GenerateTriggerButtonEvent(const Tilt &tilt) {
	u32 upButtons = 0;
	u32 downButtons = 0;
	// Y axis for both
	if (tilt.y_ < 0.0f) {
		downButtons = CTRL_LTRIGGER | CTRL_RTRIGGER;
	} else if (tilt.x_ == 0.0f) {
		upButtons = CTRL_LTRIGGER | CTRL_RTRIGGER;
	} else if (tilt.x_ < 0.0f) {
		downButtons = CTRL_LTRIGGER;
		upButtons = CTRL_RTRIGGER;
	} else if (tilt.x_ > 0.0f) {
		downButtons = CTRL_RTRIGGER;
		upButtons = CTRL_LTRIGGER;
	}

	downButtons &= ~__CtrlPeekButtons();
	__CtrlButtonUp(tiltButtonsDown & upButtons);
	__CtrlButtonDown(downButtons);
	tiltButtonsDown = (tiltButtonsDown & ~upButtons) | downButtons;
}

void ResetTiltEvents() {
	// Reset the buttons we have marked pressed.
	__CtrlButtonUp(tiltButtonsDown);
	tiltButtonsDown = 0;
	__CtrlSetAnalogXY(CTRL_STICK_LEFT, 0.0f, 0.0f);
}

}  // namespace TiltEventProcessor

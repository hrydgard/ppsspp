#include "Controls.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Input/InputState.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Core/KeyMap.h"

static void controllerButtonPressed(BOOL pressed, InputKeyCode keyCode) {
	KeyInput key;
	key.deviceId = DEVICE_ID_PAD_0;
	key.flags = pressed ? KEY_DOWN : KEY_UP;
	key.keyCode = keyCode;
	NativeKey(key);
}

static void analogTriggerPressed(InputAxis axis, float value) {
	AxisInput axisInput;
	axisInput.deviceId = DEVICE_ID_PAD_0;
	axisInput.axisId = axis;
	axisInput.value = value;
	NativeAxis(&axisInput, 1);
}

bool SetupController(GCController *controller) {
	GCExtendedGamepad *extendedProfile = controller.extendedGamepad;
	if (extendedProfile == nil) {
		return false;
	}

	extendedProfile.buttonA.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_2); // Cross
	};

	extendedProfile.buttonB.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_3); // Circle
	};

	extendedProfile.buttonX.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_4); // Square
	};

	extendedProfile.buttonY.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_1); // Triangle
	};

	extendedProfile.leftShoulder.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_L1); // LTrigger
	};

	extendedProfile.rightShoulder.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_R1); // RTrigger
	};

	extendedProfile.dpad.up.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_UP);
	};

	extendedProfile.dpad.down.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_DOWN);
	};

	extendedProfile.dpad.left.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_LEFT);
	};

	extendedProfile.dpad.right.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_RIGHT);
	};

	extendedProfile.leftTrigger.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		INFO_LOG(SYSTEM, "ltrigger: %f %d", value, (int)pressed);
		analogTriggerPressed(JOYSTICK_AXIS_LTRIGGER, value);
	};

	extendedProfile.rightTrigger.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		INFO_LOG(SYSTEM, "rtrigger: %f %d", value, (int)pressed);
		analogTriggerPressed(JOYSTICK_AXIS_RTRIGGER, value);
	};

#if defined(__IPHONE_12_1) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_12_1
	if ([extendedProfile respondsToSelector:@selector(leftThumbstickButton)] && extendedProfile.leftThumbstickButton != nil) {
		extendedProfile.leftThumbstickButton.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_THUMBL);
		};
	}
	if ([extendedProfile respondsToSelector:@selector(rightThumbstickButton)] && extendedProfile.rightThumbstickButton != nil) {
		extendedProfile.rightThumbstickButton.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_THUMBR);
		};
	}
#endif
#if defined(__IPHONE_13_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_13_0
	if ([extendedProfile respondsToSelector:@selector(buttonOptions)] && extendedProfile.buttonOptions != nil) {
		extendedProfile.buttonOptions.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_SELECT);
		};
	}
	if ([extendedProfile respondsToSelector:@selector(buttonMenu)] && extendedProfile.buttonMenu != nil) {
		extendedProfile.buttonMenu.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_START);
		};
	}
#endif
#if defined(__IPHONE_14_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_14_0
	if ([extendedProfile respondsToSelector:@selector(buttonHome)] && extendedProfile.buttonHome != nil) {
		extendedProfile.buttonHome.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_15);
		};
	}
#endif

	extendedProfile.leftThumbstick.xAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
		AxisInput axisInput;
		axisInput.deviceId = DEVICE_ID_PAD_0;
		axisInput.axisId = JOYSTICK_AXIS_X;
		axisInput.value = value;
		NativeAxis(&axisInput, 1);
	};

	extendedProfile.leftThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
		AxisInput axisInput;
		axisInput.deviceId = DEVICE_ID_PAD_0;
		axisInput.axisId = JOYSTICK_AXIS_Y;
		axisInput.value = -value;
		NativeAxis(&axisInput, 1);
	};

	// Map right thumbstick as another analog stick, particularly useful for controllers
	// like the DualShock 3/4 when connected to an iOS device
	extendedProfile.rightThumbstick.xAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
		AxisInput axisInput;
		axisInput.deviceId = DEVICE_ID_PAD_0;
		axisInput.axisId = JOYSTICK_AXIS_Z;
		axisInput.value = value;
		NativeAxis(&axisInput, 1);
	};

	extendedProfile.rightThumbstick.yAxis.valueChangedHandler = ^(GCControllerAxisInput *axis, float value) {
		AxisInput axisInput;
		axisInput.deviceId = DEVICE_ID_PAD_0;
		axisInput.axisId = JOYSTICK_AXIS_RZ;
		axisInput.value = -value;
		NativeAxis(&axisInput, 1);
	};

	return true;
}

void TouchTracker::SendTouchEvent(float x, float y, int code, int pointerId) {
	float scale = [UIScreen mainScreen].scale;
	if ([[UIScreen mainScreen] respondsToSelector:@selector(nativeScale)]) {
		scale = [UIScreen mainScreen].nativeScale;
	}

	float dp_xscale = (float)g_display.dp_xres / (float)g_display.pixel_xres;
	float dp_yscale = (float)g_display.dp_yres / (float)g_display.pixel_yres;

	float scaledX = (int)(x * dp_xscale) * scale;
	float scaledY = (int)(y * dp_yscale) * scale;

	TouchInput input;
	input.x = scaledX;
	input.y = scaledY;
	switch (code) {
		case 1: input.flags = TOUCH_DOWN; break;
		case 2: input.flags = TOUCH_UP; break;
		default: input.flags = TOUCH_MOVE; break;
	}
	input.id = pointerId;
	NativeTouch(input);
}

int TouchTracker::ToTouchID(UITouch *uiTouch, bool allowAllocate) {
	// Find the id for the touch.
	for (int localId = 0; localId < (int)ARRAY_SIZE(touches_); ++localId) {
		if (touches_[localId] == uiTouch) {
			return localId;
		}
	}

	// Allocate a new one, perhaps?
	if (allowAllocate) {
		for (int localId = 0; localId < (int)ARRAY_SIZE(touches_); ++localId) {
			if (touches_[localId] == 0) {
				touches_[localId] = uiTouch;
				return localId;
			}
		}

		// None were free. Ignore?
		return 0;
	}

	return -1;
}

void TouchTracker::Began(NSSet *touches, UIView *view) {
	for (UITouch* touch in touches) {
		CGPoint point = [touch locationInView:view];
		int touchId = ToTouchID(touch, true);
		SendTouchEvent(point.x, point.y, 1, touchId);
	}
}

void TouchTracker::Moved(NSSet *touches, UIView *view) {
	for (UITouch* touch in touches) {
		CGPoint point = [touch locationInView:view];
		int touchId = ToTouchID(touch, true);
		SendTouchEvent(point.x, point.y, 0, touchId);
	}
}

void TouchTracker::Ended(NSSet *touches, UIView *view) {
	for (UITouch* touch in touches) {
		CGPoint point = [touch locationInView:view];
		int touchId = ToTouchID(touch, false);
		if (touchId >= 0) {
			SendTouchEvent(point.x, point.y, 2, touchId);
			touches_[touchId] = nullptr;
		}
	}
}

void TouchTracker::Cancelled(NSSet *touches, UIView *view) {
	for (UITouch* touch in touches) {
		CGPoint point = [touch locationInView:view];
		int touchId = ToTouchID(touch, false);
		if (touchId >= 0) {
			SendTouchEvent(point.x, point.y, 2, touchId);
			touches_[touchId] = nullptr;
		}
	}
}

void ICadeTracker::InitKeyMap() {
	iCadeToKeyMap[iCadeJoystickUp]		= NKCODE_DPAD_UP;
	iCadeToKeyMap[iCadeJoystickRight]	= NKCODE_DPAD_RIGHT;
	iCadeToKeyMap[iCadeJoystickDown]	= NKCODE_DPAD_DOWN;
	iCadeToKeyMap[iCadeJoystickLeft]	= NKCODE_DPAD_LEFT;
	iCadeToKeyMap[iCadeButtonA]			= NKCODE_BUTTON_9; // Select
	iCadeToKeyMap[iCadeButtonB]			= NKCODE_BUTTON_7; // LTrigger
	iCadeToKeyMap[iCadeButtonC]			= NKCODE_BUTTON_10; // Start
	iCadeToKeyMap[iCadeButtonD]			= NKCODE_BUTTON_8; // RTrigger
	iCadeToKeyMap[iCadeButtonE]			= NKCODE_BUTTON_4; // Square
	iCadeToKeyMap[iCadeButtonF]			= NKCODE_BUTTON_2; // Cross
	iCadeToKeyMap[iCadeButtonG]			= NKCODE_BUTTON_1; // Triangle
	iCadeToKeyMap[iCadeButtonH]			= NKCODE_BUTTON_3; // Circle
}

void ICadeTracker::ButtonDown(iCadeState button) {
	if (simulateAnalog &&
		((button == iCadeJoystickUp) ||
		 (button == iCadeJoystickDown) ||
		 (button == iCadeJoystickLeft) ||
		 (button == iCadeJoystickRight))) {
		AxisInput axis;
		switch (button) {
			case iCadeJoystickUp :
				axis.axisId = JOYSTICK_AXIS_Y;
				axis.value = -1.0f;
				break;

			case iCadeJoystickDown :
				axis.axisId = JOYSTICK_AXIS_Y;
				axis.value = 1.0f;
				break;

			case iCadeJoystickLeft :
				axis.axisId = JOYSTICK_AXIS_X;
				axis.value = -1.0f;
				break;

			case iCadeJoystickRight :
				axis.axisId = JOYSTICK_AXIS_X;
				axis.value = 1.0f;
				break;

			default:
				break;
		}
		axis.deviceId = DEVICE_ID_PAD_0;
		NativeAxis(&axis, 1);
	} else {
		KeyInput key;
		key.flags = KEY_DOWN;
		key.keyCode = iCadeToKeyMap[button];
		key.deviceId = DEVICE_ID_PAD_0;
		NativeKey(key);
	}
}

void ICadeTracker::ButtonUp(iCadeState button) {
	if (!iCadeConnectNotified) {
		iCadeConnectNotified = true;
		KeyMap::NotifyPadConnected(DEVICE_ID_PAD_0, "iCade");
	}

	if (button == iCadeButtonA) {
		// Pressing Select twice within 1 second toggles the DPad between
		//     normal operation and simulating the Analog stick.
		if ((lastSelectPress + 1.0f) > time_now_d())
			simulateAnalog = !simulateAnalog;
		lastSelectPress = time_now_d();
	}

	if (button == iCadeButtonC) {
		// Pressing Start twice within 1 second will take to the Emu menu
		if ((lastStartPress + 1.0f) > time_now_d()) {
			KeyInput key;
			key.flags = KEY_DOWN;
			key.keyCode = NKCODE_ESCAPE;
			key.deviceId = DEVICE_ID_KEYBOARD;
			NativeKey(key);
			return;
		}
		lastStartPress = time_now_d();
	}

	if (simulateAnalog &&
		((button == iCadeJoystickUp) ||
		 (button == iCadeJoystickDown) ||
		 (button == iCadeJoystickLeft) ||
		 (button == iCadeJoystickRight))) {
		AxisInput axis;
		switch (button) {
			case iCadeJoystickUp :
				axis.axisId = JOYSTICK_AXIS_Y;
				axis.value = 0.0f;
				break;

			case iCadeJoystickDown :
				axis.axisId = JOYSTICK_AXIS_Y;
				axis.value = 0.0f;
				break;

			case iCadeJoystickLeft :
				axis.axisId = JOYSTICK_AXIS_X;
				axis.value = 0.0f;
				break;

			case iCadeJoystickRight :
				axis.axisId = JOYSTICK_AXIS_X;
				axis.value = 0.0f;
				break;

			default:
				break;
		}
		axis.deviceId = DEVICE_ID_PAD_0;
		NativeAxis(&axis, 1);
	} else {
		KeyInput key;
		key.flags = KEY_UP;
		key.keyCode = iCadeToKeyMap[button];
		key.deviceId = DEVICE_ID_PAD_0;
		NativeKey(key);
	}
}

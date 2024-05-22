#include "Controls.h"

#include "Common/Log.h"
#include "Common/Input/InputState.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"

static void controllerButtonPressed(BOOL pressed, InputKeyCode keyCode) {
	KeyInput key;
	key.deviceId = DEVICE_ID_PAD_0;
	key.flags = pressed ? KEY_DOWN : KEY_UP;
	key.keyCode = keyCode;
	NativeKey(key);
}

bool SetupController(GCController *controller) {
	GCGamepad *baseProfile = controller.gamepad;
	if (baseProfile == nil) {
		return false;
	}

	controller.controllerPausedHandler = ^(GCController *controller) {
		KeyInput key;
		key.flags = KEY_DOWN;
		key.keyCode = NKCODE_ESCAPE;
		key.deviceId = DEVICE_ID_KEYBOARD;
		NativeKey(key);
	};

	baseProfile.buttonA.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_2); // Cross
	};

	baseProfile.buttonB.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_3); // Circle
	};

	baseProfile.buttonX.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_4); // Square
	};

	baseProfile.buttonY.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_1); // Triangle
	};

	baseProfile.leftShoulder.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_7); // LTrigger
	};

	baseProfile.rightShoulder.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_8); // RTrigger
	};

	baseProfile.dpad.up.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_UP);
	};

	baseProfile.dpad.down.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_DOWN);
	};

	baseProfile.dpad.left.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_LEFT);
	};

	baseProfile.dpad.right.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_DPAD_RIGHT);
	};

	GCExtendedGamepad *extendedProfile = controller.extendedGamepad;
	if (extendedProfile == nil)
		return; // controller doesn't support extendedGamepad profile

	extendedProfile.leftTrigger.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_9); // Select
	};

	extendedProfile.rightTrigger.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		controllerButtonPressed(pressed, NKCODE_BUTTON_10); // Start
	};

#if defined(__IPHONE_12_1) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_12_1
	if ([extendedProfile respondsToSelector:@selector(leftThumbstickButton)] && extendedProfile.leftThumbstickButton != nil) {
		extendedProfile.leftThumbstickButton.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_11);
		};
	}
	if ([extendedProfile respondsToSelector:@selector(rightThumbstickButton)] && extendedProfile.rightThumbstickButton != nil) {
		extendedProfile.rightThumbstickButton.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_12);
		};
	}
#endif
#if defined(__IPHONE_13_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_13_0
	if ([extendedProfile respondsToSelector:@selector(buttonOptions)] && extendedProfile.buttonOptions != nil) {
		extendedProfile.buttonOptions.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_13);
		};
	}
	if ([extendedProfile respondsToSelector:@selector(buttonMenu)] && extendedProfile.buttonMenu != nil) {
		extendedProfile.buttonMenu.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
			controllerButtonPressed(pressed, NKCODE_BUTTON_14);
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

void SendTouchEvent(float x, float y, int code, int pointerId) {
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


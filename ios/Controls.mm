#include "Controls.h"

#include "Common/Data/Encoding/Utf8.h"
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

bool InitController(GCController *controller) {
	GCExtendedGamepad *extendedProfile = controller.extendedGamepad;
	if (extendedProfile == nil) {
		return false;
	}

	if (@available(iOS 14.0, tvOS 14.0, *)) {
		for (GCControllerElement* element in controller.physicalInputProfile.allElements) {
			element.preferredSystemGestureState = GCSystemGestureStateDisabled;
		}
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
		INFO_LOG(Log::System, "ltrigger: %f %d", value, (int)pressed);
		analogTriggerPressed(JOYSTICK_AXIS_LTRIGGER, value);
	};

	extendedProfile.rightTrigger.valueChangedHandler = ^(GCControllerButtonInput *button, float value, BOOL pressed) {
		INFO_LOG(Log::System, "rtrigger: %f %d", value, (int)pressed);
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
			controllerButtonPressed(pressed, NKCODE_HOME);
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

void ShutdownController(GCController *controller) {
	if (@available(iOS 14.0, tvOS 14.0, *)) {
		for (GCControllerElement* element in controller.physicalInputProfile.allElements) {
			element.preferredSystemGestureState = GCSystemGestureStateEnabled;
		}
	}
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

void ProcessAccelerometerData(CMAccelerometerData *accData) {
	CMAcceleration acc = accData.acceleration;
	// INFO_LOG(SYSTEM, "%f %f %f", acc.x, acc.y, acc.z);

	// Might need to change these for portrait or inverse landscape
	NativeAccelerometer(-acc.x, -acc.y, -acc.z);
}

void SendKeyboardChars(std::string_view str) {
	UTF8 chars(str);
	while (!chars.end()) {
		uint32_t codePoint = chars.next();
		KeyInput input{};
		input.deviceId = DEVICE_ID_KEYBOARD;
		input.flags = KEY_CHAR;
		input.unicodeChar = codePoint;
		NativeKey(input);
	}
}

void KeyboardPressesBegan(NSSet<UIPress *> *presses, UIPressesEvent *event) {
	for (UIPress *press in presses) {
		if (!press.key) {
			// I guess we could support remotes and stuff.
			continue;
		}
		if (@available(iOS 13.0, *)) {
			InputKeyCode code = HIDUsageToInputKeyCode(press.key.keyCode);
			if (code != NKCODE_UNKNOWN) {
				KeyInput input{};
				input.deviceId = DEVICE_ID_KEYBOARD;
				input.keyCode = code;
				input.flags = KEY_DOWN;
				NativeKey(input);
				INFO_LOG(Log::System, "pressesBegan %d", code);
			}
		}
		if (press.key.characters) {
			std::string chars([press.key.characters UTF8String]);
			SendKeyboardChars(chars);
		}
	}
}

void KeyboardPressesEnded(NSSet<UIPress *> *presses, UIPressesEvent *event) {
	for (UIPress *press in presses) {
		if (!press.key) {
			// I guess we could support remotes and stuff.
			continue;
		}
		if (@available(iOS 13.0, *)) {
			InputKeyCode code = HIDUsageToInputKeyCode(press.key.keyCode);
			if (code != NKCODE_UNKNOWN) {
				KeyInput input{};
				input.deviceId = DEVICE_ID_KEYBOARD;
				input.keyCode = code;
				input.flags = KEY_UP;
				NativeKey(input);
				INFO_LOG(Log::System, "pressesEnded %d", code);
			}
		}
	}
}

InputKeyCode HIDUsageToInputKeyCode(UIKeyboardHIDUsage usage) {
	switch (usage) {
	case UIKeyboardHIDUsageKeyboardA: return NKCODE_A;
	case UIKeyboardHIDUsageKeyboardB: return NKCODE_B;
	case UIKeyboardHIDUsageKeyboardC: return NKCODE_C;
	case UIKeyboardHIDUsageKeyboardD: return NKCODE_D;
	case UIKeyboardHIDUsageKeyboardE: return NKCODE_E;
	case UIKeyboardHIDUsageKeyboardF: return NKCODE_F;
	case UIKeyboardHIDUsageKeyboardG: return NKCODE_G;
	case UIKeyboardHIDUsageKeyboardH: return NKCODE_H;
	case UIKeyboardHIDUsageKeyboardI: return NKCODE_I;
	case UIKeyboardHIDUsageKeyboardJ: return NKCODE_J;
	case UIKeyboardHIDUsageKeyboardK: return NKCODE_K;
	case UIKeyboardHIDUsageKeyboardL: return NKCODE_L;
	case UIKeyboardHIDUsageKeyboardM: return NKCODE_M;
	case UIKeyboardHIDUsageKeyboardN: return NKCODE_N;
	case UIKeyboardHIDUsageKeyboardO: return NKCODE_O;
	case UIKeyboardHIDUsageKeyboardP: return NKCODE_P;
	case UIKeyboardHIDUsageKeyboardQ: return NKCODE_Q;
	case UIKeyboardHIDUsageKeyboardR: return NKCODE_R;
	case UIKeyboardHIDUsageKeyboardS: return NKCODE_S;
	case UIKeyboardHIDUsageKeyboardT: return NKCODE_T;
	case UIKeyboardHIDUsageKeyboardU: return NKCODE_U;
	case UIKeyboardHIDUsageKeyboardV: return NKCODE_V;
	case UIKeyboardHIDUsageKeyboardW: return NKCODE_W;
	case UIKeyboardHIDUsageKeyboardX: return NKCODE_X;
	case UIKeyboardHIDUsageKeyboardY: return NKCODE_Y;
	case UIKeyboardHIDUsageKeyboardZ: return NKCODE_Z;
	case UIKeyboardHIDUsageKeyboard1: return NKCODE_1;
	case UIKeyboardHIDUsageKeyboard2: return NKCODE_2;
	case UIKeyboardHIDUsageKeyboard3: return NKCODE_3;
	case UIKeyboardHIDUsageKeyboard4: return NKCODE_4;
	case UIKeyboardHIDUsageKeyboard5: return NKCODE_5;
	case UIKeyboardHIDUsageKeyboard6: return NKCODE_6;
	case UIKeyboardHIDUsageKeyboard7: return NKCODE_7;
	case UIKeyboardHIDUsageKeyboard8: return NKCODE_8;
	case UIKeyboardHIDUsageKeyboard9: return NKCODE_9;
	case UIKeyboardHIDUsageKeyboard0: return NKCODE_0;
	case UIKeyboardHIDUsageKeyboardReturnOrEnter: return NKCODE_NUMPAD_ENTER;
	case UIKeyboardHIDUsageKeyboardEscape: return NKCODE_ESCAPE;
	case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: return NKCODE_DEL;  // really.
	case UIKeyboardHIDUsageKeyboardTab: return NKCODE_TAB;
	case UIKeyboardHIDUsageKeyboardSpacebar: return NKCODE_SPACE;
	case UIKeyboardHIDUsageKeyboardHyphen: return NKCODE_MINUS;
	case UIKeyboardHIDUsageKeyboardEqualSign: return NKCODE_EQUALS;
	case UIKeyboardHIDUsageKeyboardOpenBracket: return NKCODE_LEFT_BRACKET;
	case UIKeyboardHIDUsageKeyboardCloseBracket: return NKCODE_RIGHT_BRACKET;
	case UIKeyboardHIDUsageKeyboardBackslash: return NKCODE_BACKSLASH;
	case UIKeyboardHIDUsageKeyboardNonUSPound: return NKCODE_POUND;
	case UIKeyboardHIDUsageKeyboardSemicolon: return NKCODE_SEMICOLON;
	case UIKeyboardHIDUsageKeyboardQuote: return NKCODE_APOSTROPHE;
	case UIKeyboardHIDUsageKeyboardGraveAccentAndTilde: return NKCODE_GRAVE;
	case UIKeyboardHIDUsageKeyboardComma: return NKCODE_COMMA;
	case UIKeyboardHIDUsageKeyboardPeriod: return NKCODE_PERIOD;
	case UIKeyboardHIDUsageKeyboardSlash: return NKCODE_SLASH;
	case UIKeyboardHIDUsageKeyboardCapsLock: return NKCODE_CAPS_LOCK;
	case UIKeyboardHIDUsageKeyboardF1: return NKCODE_F1;
	case UIKeyboardHIDUsageKeyboardF2: return NKCODE_F2;
	case UIKeyboardHIDUsageKeyboardF3: return NKCODE_F3;
	case UIKeyboardHIDUsageKeyboardF4: return NKCODE_F4;
	case UIKeyboardHIDUsageKeyboardF5: return NKCODE_F5;
	case UIKeyboardHIDUsageKeyboardF6: return NKCODE_F6;
	case UIKeyboardHIDUsageKeyboardF7: return NKCODE_F7;
	case UIKeyboardHIDUsageKeyboardF8: return NKCODE_F8;
	case UIKeyboardHIDUsageKeyboardF9: return NKCODE_F9;
	case UIKeyboardHIDUsageKeyboardF10: return NKCODE_F10;
	case UIKeyboardHIDUsageKeyboardF11: return NKCODE_F11;
	case UIKeyboardHIDUsageKeyboardF12: return NKCODE_F12;
	case UIKeyboardHIDUsageKeyboardScrollLock: return NKCODE_SCROLL_LOCK;
	case UIKeyboardHIDUsageKeyboardInsert: return NKCODE_INSERT;
	case UIKeyboardHIDUsageKeyboardHome: return NKCODE_HOME;
	case UIKeyboardHIDUsageKeyboardPageUp: return NKCODE_PAGE_UP;
	case UIKeyboardHIDUsageKeyboardDeleteForward: return NKCODE_DEL;
	case UIKeyboardHIDUsageKeyboardEnd: return NKCODE_MOVE_END;
	case UIKeyboardHIDUsageKeyboardPageDown: return NKCODE_PAGE_DOWN;
	case UIKeyboardHIDUsageKeyboardRightArrow: return NKCODE_DPAD_RIGHT;
	case UIKeyboardHIDUsageKeyboardLeftArrow: return NKCODE_DPAD_LEFT;
	case UIKeyboardHIDUsageKeyboardDownArrow: return NKCODE_DPAD_DOWN;
	case UIKeyboardHIDUsageKeyboardUpArrow: return NKCODE_DPAD_UP;
	case UIKeyboardHIDUsageKeypadNumLock: return NKCODE_NUM_LOCK;
	case UIKeyboardHIDUsageKeypadSlash: return NKCODE_NUMPAD_DIVIDE;
	case UIKeyboardHIDUsageKeypadAsterisk: return NKCODE_NUMPAD_MULTIPLY;
	case UIKeyboardHIDUsageKeypadHyphen: return NKCODE_NUMPAD_SUBTRACT;
	case UIKeyboardHIDUsageKeypadPlus: return NKCODE_NUMPAD_ADD;
	case UIKeyboardHIDUsageKeypadEnter: return NKCODE_NUMPAD_ENTER;
	case UIKeyboardHIDUsageKeypad1: return NKCODE_NUMPAD_1;
	case UIKeyboardHIDUsageKeypad2: return NKCODE_NUMPAD_2;
	case UIKeyboardHIDUsageKeypad3: return NKCODE_NUMPAD_3;
	case UIKeyboardHIDUsageKeypad4: return NKCODE_NUMPAD_4;
	case UIKeyboardHIDUsageKeypad5: return NKCODE_NUMPAD_5;
	case UIKeyboardHIDUsageKeypad6: return NKCODE_NUMPAD_6;
	case UIKeyboardHIDUsageKeypad7: return NKCODE_NUMPAD_7;
	case UIKeyboardHIDUsageKeypad8: return NKCODE_NUMPAD_8;
	case UIKeyboardHIDUsageKeypad9: return NKCODE_NUMPAD_9;
	case UIKeyboardHIDUsageKeypad0: return NKCODE_NUMPAD_0;
	case UIKeyboardHIDUsageKeypadPeriod: return NKCODE_NUMPAD_COMMA;
	case UIKeyboardHIDUsageKeyboardNonUSBackslash: return NKCODE_BACKSLASH;
	case UIKeyboardHIDUsageKeyboardApplication: return NKCODE_APP_SWITCH;
	case UIKeyboardHIDUsageKeyboardPower: return NKCODE_POWER;
	case UIKeyboardHIDUsageKeypadEqualSign: return NKCODE_NUMPAD_EQUALS;
	case UIKeyboardHIDUsageKeyboardMenu: return NKCODE_MENU;
	case UIKeyboardHIDUsageKeyboardMute: return NKCODE_MUTE;
	case UIKeyboardHIDUsageKeyboardVolumeUp: return NKCODE_VOLUME_UP;
	case UIKeyboardHIDUsageKeypadComma: return NKCODE_NUMPAD_COMMA;
	default: return NKCODE_UNKNOWN;
	}
}

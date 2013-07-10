// Copyright (c) 2013- PPSSPP Project.

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

#include "file/ini_file.h"
#include "input/input_state.h"
#include "../Core/Config.h"
#include "KeyMap.h"

namespace KeyMap {

KeyDef AxisDef(int deviceId, int axisId, int direction);

// TODO: Make use const_map.h from native
struct DefaultKeyMap {
	static KeyMapping defaultKeyboardMap()
	{
		KeyMapping m;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_A)] = CTRL_SQUARE;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_S)] = CTRL_TRIANGLE;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_X)] = CTRL_CIRCLE;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_Z)] = CTRL_CROSS;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_Q)] = CTRL_LTRIGGER;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_W)] = CTRL_RTRIGGER;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_SPACE)] = CTRL_START;
#ifdef _WIN32
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_V)] = CTRL_SELECT;
#else
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_ENTER)] = CTRL_SELECT;
#endif
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_DPAD_UP)] = CTRL_UP;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_DPAD_DOWN)] = CTRL_DOWN;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_DPAD_LEFT)] = CTRL_LEFT;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_DPAD_RIGHT)] = CTRL_RIGHT;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_I)] = VIRTKEY_AXIS_Y_MAX;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_K)] = VIRTKEY_AXIS_Y_MIN;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_J)] = VIRTKEY_AXIS_X_MIN;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_L)] = VIRTKEY_AXIS_X_MAX;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_SHIFT_LEFT)] = VIRTKEY_RAPID_FIRE;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_TAB)] = VIRTKEY_UNTHROTTLE;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_GRAVE)] = VIRTKEY_SPEED_TOGGLE;
		m[KeyDef(DEVICE_ID_KEYBOARD, KEYCODE_ESCAPE)] = VIRTKEY_PAUSE;
		return m;
	}

	static KeyMapping default360Map()
	{
		KeyMapping m;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_A)] = CTRL_CROSS;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_B)] = CTRL_CIRCLE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_X)] = CTRL_SQUARE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_Y)] = CTRL_TRIANGLE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_UP)] = CTRL_UP;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_RIGHT)] = CTRL_RIGHT;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_DOWN)] = CTRL_DOWN;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_LEFT)] = CTRL_LEFT;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_START)] = CTRL_START;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BACK)] = CTRL_SELECT;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_L1)] = CTRL_LTRIGGER;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_R1)] = CTRL_RTRIGGER;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_R2)] = VIRTKEY_UNTHROTTLE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_THUMBR)] = VIRTKEY_PAUSE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_L2)] = VIRTKEY_SPEED_TOGGLE;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_X, -1)] = VIRTKEY_AXIS_X_MIN;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_X, +1)] = VIRTKEY_AXIS_X_MAX;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_Y, -1)] = VIRTKEY_AXIS_Y_MIN;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_Y, +1)] = VIRTKEY_AXIS_Y_MAX;
		return m;
	}

	static KeyMapping defaultPadMap()
	{
		KeyMapping m;
#ifdef ANDROID
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_A)] = CTRL_CROSS;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_B)] = CTRL_CIRCLE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_X)] = CTRL_SQUARE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_Y)] = CTRL_TRIANGLE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_UP)] = CTRL_UP;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_RIGHT)] = CTRL_RIGHT;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_DOWN)] = CTRL_DOWN;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_DPAD_LEFT)] = CTRL_LEFT;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_START)] = CTRL_START;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BACK)] = CTRL_SELECT;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_L1)] = CTRL_LTRIGGER;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_R1)] = CTRL_RTRIGGER;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_R2)] = VIRTKEY_UNTHROTTLE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_THUMBR)] = VIRTKEY_PAUSE;
		m[KeyDef(DEVICE_ID_X360_0, KEYCODE_BUTTON_L2)] = VIRTKEY_SPEED_TOGGLE;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_X, -1)] = VIRTKEY_AXIS_X_MIN;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_X, +1)] = VIRTKEY_AXIS_X_MAX;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_Y, -1)] = VIRTKEY_AXIS_Y_MIN;
		m[AxisDef(DEVICE_ID_X360_0, JOYSTICK_AXIS_Y, +1)] = VIRTKEY_AXIS_Y_MAX;
#else
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_2)] = CTRL_CROSS;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_3)] = CTRL_CIRCLE;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_4)] = CTRL_SQUARE;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_1)] = CTRL_TRIANGLE;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_DPAD_UP)] = CTRL_UP;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_DPAD_RIGHT)] = CTRL_RIGHT;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_DPAD_DOWN)] = CTRL_DOWN;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_DPAD_LEFT)] = CTRL_LEFT;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_10)] = CTRL_START;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_9)] = CTRL_SELECT;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_7)] = CTRL_LTRIGGER;
		m[KeyDef(DEVICE_ID_PAD_0, KEYCODE_BUTTON_8)] = CTRL_RTRIGGER;
		m[AxisDef(DEVICE_ID_PAD_0, JOYSTICK_AXIS_X, -1)] = VIRTKEY_AXIS_X_MIN;
		m[AxisDef(DEVICE_ID_PAD_0, JOYSTICK_AXIS_X, +1)] = VIRTKEY_AXIS_X_MAX;
		m[AxisDef(DEVICE_ID_PAD_0, JOYSTICK_AXIS_Y, +1)] = VIRTKEY_AXIS_Y_MIN;
		m[AxisDef(DEVICE_ID_PAD_0, JOYSTICK_AXIS_Y, -1)] = VIRTKEY_AXIS_Y_MAX;
#endif
		return m;
	}

	static KeyMapping defaultXperiaPlay()
	{
		KeyMapping m;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BUTTON_CROSS)] = CTRL_CROSS;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BUTTON_CIRCLE)] = CTRL_CIRCLE;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BUTTON_X)] = CTRL_SQUARE;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BUTTON_Y)] = CTRL_TRIANGLE;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_DPAD_UP)] = CTRL_UP;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_DPAD_RIGHT)] = CTRL_RIGHT;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_DPAD_DOWN)] = CTRL_DOWN;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_DPAD_LEFT)] = CTRL_LEFT;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BUTTON_START)] = CTRL_START;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BACK)] = CTRL_SELECT;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BUTTON_L1)] = CTRL_LTRIGGER;
		m[KeyDef(DEVICE_ID_DEFAULT, KEYCODE_BUTTON_R1)] = CTRL_RTRIGGER;
		m[AxisDef(DEVICE_ID_DEFAULT, JOYSTICK_AXIS_X, -1)] = VIRTKEY_AXIS_X_MIN;
		m[AxisDef(DEVICE_ID_DEFAULT, JOYSTICK_AXIS_X, +1)] = VIRTKEY_AXIS_X_MAX;
		m[AxisDef(DEVICE_ID_DEFAULT, JOYSTICK_AXIS_Y, -1)] = VIRTKEY_AXIS_Y_MIN;
		m[AxisDef(DEVICE_ID_DEFAULT, JOYSTICK_AXIS_Y, +1)] = VIRTKEY_AXIS_Y_MAX;
		return m;
	}

	static std::vector<ControllerMap> init()
	{
		std::vector<ControllerMap> m;
		
#if defined(USING_GLES2)
		// Mobile! Only a pad map required, some can use a keyboard map though.
		ControllerMap pad;
		pad.keys = defaultPadMap();
		pad.name = "Pad";
		m.push_back(pad);

		ControllerMap kbd;
		kbd.keys = defaultKeyboardMap();
		kbd.name = "Keyboard";
		m.push_back(kbd);

#ifdef ANDROID
		ControllerMap xperia;
		xperia.keys = defaultXperiaPlay();
		xperia.name = "Xperia Play";
		xperia.active = false;
		m.push_back(xperia);
#endif

#else
		ControllerMap kbd;
		kbd.keys = defaultKeyboardMap();
		kbd.name = "Keyboard";
		m.push_back(kbd);

#ifdef _WIN32
		ControllerMap x360;
		x360.keys = default360Map();
		x360.name = "360";
		m.push_back(x360);
#endif 
		// Keyboard and pad maps.
		ControllerMap pad;
		pad.keys = defaultPadMap();
		pad.name = "Pad";
		pad.active = false;
		m.push_back(pad);
#endif
		return m;
	}

	static std::vector<ControllerMap> KeyMap;
};

std::vector<ControllerMap> DefaultKeyMap::KeyMap = DefaultKeyMap::init();


// Key & Button names
struct KeyMap_IntStrPair {
	int key;
	std::string name;
};
const KeyMap_IntStrPair key_names[] = {
	{KEYCODE_A, "A"},
	{KEYCODE_B, "B"},
	{KEYCODE_C, "C"},
	{KEYCODE_D, "D"},
	{KEYCODE_E, "E"},
	{KEYCODE_F, "F"},
	{KEYCODE_G, "G"},
	{KEYCODE_H, "H"},
	{KEYCODE_I, "I"},
	{KEYCODE_J, "J"},
	{KEYCODE_K, "K"},
	{KEYCODE_L, "L"},
	{KEYCODE_M, "M"},
	{KEYCODE_N, "N"},
	{KEYCODE_O, "O"},
	{KEYCODE_P, "P"},
	{KEYCODE_Q, "Q"},
	{KEYCODE_R, "R"},
	{KEYCODE_S, "S"},
	{KEYCODE_T, "T"},
	{KEYCODE_U, "U"},
	{KEYCODE_V, "V"},
	{KEYCODE_W, "W"},
	{KEYCODE_X, "X"},
	{KEYCODE_Y, "Y"},
	{KEYCODE_Z, "Z"},

	{KEYCODE_0, "0"},
	{KEYCODE_1, "1"},
	{KEYCODE_2, "2"},
	{KEYCODE_3, "3"},
	{KEYCODE_4, "4"},
	{KEYCODE_5, "5"},
	{KEYCODE_6, "6"},
	{KEYCODE_7, "7"},
	{KEYCODE_8, "8"},
	{KEYCODE_9, "9"},

	{KEYCODE_F1, "F1"},
	{KEYCODE_F2, "F2"},
	{KEYCODE_F3, "F3"},
	{KEYCODE_F4, "F4"},
	{KEYCODE_F5, "F5"},
	{KEYCODE_F6, "F6"},
	{KEYCODE_F7, "F7"},
	{KEYCODE_F8, "F8"},
	{KEYCODE_F9, "F9"},
	{KEYCODE_F10, "F10"},
	{KEYCODE_F11, "F11"},
	{KEYCODE_F12, "F12"},
	
	{KEYCODE_GRAVE, "`"},
	{KEYCODE_SLASH, "/"},
	{KEYCODE_BACKSLASH, "\\"},
	{KEYCODE_SEMICOLON, ";"},
	{KEYCODE_COMMA, ","},
	{KEYCODE_PERIOD, "."},
	{KEYCODE_LEFT_BRACKET, "["},
	{KEYCODE_RIGHT_BRACKET, "]"},
	{KEYCODE_APOSTROPHE, "'"},
	{KEYCODE_MINUS, "-"},
	{KEYCODE_PLUS, "+"},
	{KEYCODE_SYSRQ, "Print"},
	{KEYCODE_SCROLL_LOCK, "ScrLock"},
	{KEYCODE_BREAK, "Pause"},

	{KEYCODE_BACK, "Back"},
	{KEYCODE_TAB, "Tab"},
	{KEYCODE_ENTER, "Enter"},
	{KEYCODE_SHIFT_LEFT, "LShift"},
	{KEYCODE_SHIFT_RIGHT, "RShift"},
	{KEYCODE_CTRL_LEFT, "LCtrl"},
	{KEYCODE_CTRL_RIGHT, "RCtrl"},
	{KEYCODE_ALT_LEFT, "LAlt"},
	{KEYCODE_ALT_RIGHT, "RAlt"},
	{KEYCODE_SPACE, "Space"},
	{KEYCODE_WINDOW, "Windows"},
	{KEYCODE_DEL, "Del"},
	{KEYCODE_MOVE_HOME, "Home"},
	{KEYCODE_MOVE_END, "End"},
	{KEYCODE_ESCAPE, "Esc"},
	{KEYCODE_CAPS_LOCK, "CapsLock"},

	{KEYCODE_VOLUME_UP, "Vol +"},
	{KEYCODE_VOLUME_DOWN, "Vol -"},
	{KEYCODE_HOME, "Home"},
	{KEYCODE_INSERT, "Ins"},
	{KEYCODE_PAGE_UP, "PgUp"},
	{KEYCODE_PAGE_DOWN, "PgDn"},
	{KEYCODE_CLEAR, "Clear"}, // 5 when numlock off
	{KEYCODE_CALL, "Call"},
	{KEYCODE_ENDCALL, "End Call"},

	{KEYCODE_DPAD_LEFT, "Left"},
	{KEYCODE_DPAD_UP, "Up"},
	{KEYCODE_DPAD_RIGHT, "Right"},
	{KEYCODE_DPAD_DOWN, "Down"},

	{KEYCODE_BUTTON_L1, "L1"},
	{KEYCODE_BUTTON_L2, "L2"},
	{KEYCODE_BUTTON_R1, "R1"},
	{KEYCODE_BUTTON_R2, "R2"},

	{KEYCODE_BUTTON_A, "[A]"},
	{KEYCODE_BUTTON_B, "[B]"},
	{KEYCODE_BUTTON_C, "[C]"},
	{KEYCODE_BUTTON_X, "[X]"},
	{KEYCODE_BUTTON_Y, "[Y]"},
	{KEYCODE_BUTTON_Z, "[Z]"},
	{KEYCODE_BUTTON_1, "b1"},
	{KEYCODE_BUTTON_2, "b2"},
	{KEYCODE_BUTTON_3, "b3"},
	{KEYCODE_BUTTON_4, "b4"},
	{KEYCODE_BUTTON_5, "b5"},
	{KEYCODE_BUTTON_6, "b6"},
	{KEYCODE_BUTTON_7, "b7"},
	{KEYCODE_BUTTON_8, "b8"},
	{KEYCODE_BUTTON_9, "b9"},
	{KEYCODE_BUTTON_10, "b10"},
	{KEYCODE_BUTTON_11, "b11"},
	{KEYCODE_BUTTON_12, "b12"},
	{KEYCODE_BUTTON_13, "b13"},
	{KEYCODE_BUTTON_14, "b14"},
	{KEYCODE_BUTTON_15, "b15"},
	{KEYCODE_BUTTON_16, "b16"},
	{KEYCODE_BUTTON_START, "Start"},
	{KEYCODE_BUTTON_SELECT, "Select"},
	{KEYCODE_BUTTON_CIRCLE, "Circle"},
	{KEYCODE_BUTTON_CIRCLE_PS3, "Circle3"},
	{KEYCODE_BUTTON_CROSS, "Cross"},
	{KEYCODE_BUTTON_CROSS_PS3, "Cross3"},
	{KEYCODE_BUTTON_TRIANGLE, "Triangle"},
	{KEYCODE_BUTTON_SQUARE, "Square"},
	{KEYCODE_BUTTON_THUMBL, "ThumbL"},
	{KEYCODE_BUTTON_THUMBR, "ThumbR"},
	{KEYCODE_BUTTON_MODE, "Mode"},

	{KEYCODE_EXT_PIPE, "|"},
	{KEYCODE_NUMPAD_DIVIDE, "Num/"},
	{KEYCODE_NUMPAD_MULTIPLY, "Num*"},
	{KEYCODE_NUMPAD_ADD, "Num+"},
	{KEYCODE_NUMPAD_SUBTRACT, "Num-"},
	{KEYCODE_NUMPAD_DOT, "Num."},
	{KEYCODE_NUMPAD_COMMA, "Num,"},
	{KEYCODE_NUMPAD_ENTER, "NumEnter"},
	{KEYCODE_NUMPAD_EQUALS, "Num="},
	{KEYCODE_NUMPAD_LEFT_PAREN, "Num("},
	{KEYCODE_NUMPAD_RIGHT_PAREN, "Num)"},
	{KEYCODE_NUMPAD_0, "Num0"},
	{KEYCODE_NUMPAD_1, "Num1"},
	{KEYCODE_NUMPAD_2, "Num2"},
	{KEYCODE_NUMPAD_3, "Num3"},
	{KEYCODE_NUMPAD_4, "Num4"},
	{KEYCODE_NUMPAD_5, "Num5"},
	{KEYCODE_NUMPAD_6, "Num6"},
	{KEYCODE_NUMPAD_7, "Num7"},
	{KEYCODE_NUMPAD_8, "Num8"},
	{KEYCODE_NUMPAD_9, "Num9"},

	{KEYCODE_EXT_MOUSEBUTTON_1, "MB1"},
	{KEYCODE_EXT_MOUSEBUTTON_2, "MB2"},
	{KEYCODE_EXT_MOUSEBUTTON_3, "MB3"},
	{KEYCODE_EXT_MOUSEWHEEL_UP, "MWheelU"},
	{KEYCODE_EXT_MOUSEWHEEL_DOWN, "MWheelD"},
};

const KeyMap_IntStrPair axis_names[] = {
	{JOYSTICK_AXIS_X, "X Axis"},
	{JOYSTICK_AXIS_Y, "Y Axis"},
	{JOYSTICK_AXIS_PRESSURE, "Pressure"},
	{JOYSTICK_AXIS_SIZE, "Size"},
	{JOYSTICK_AXIS_TOUCH_MAJOR, "Touch Major"},
	{JOYSTICK_AXIS_TOUCH_MINOR, "Touch Minor"},
	{JOYSTICK_AXIS_TOOL_MAJOR, "Tool Major"},
	{JOYSTICK_AXIS_TOOL_MINOR, "Tool Minor"},
	{JOYSTICK_AXIS_ORIENTATION, "Orient"},
	{JOYSTICK_AXIS_VSCROLL, "Vert Scroll"},
	{JOYSTICK_AXIS_HSCROLL, "Horiz Scroll"},
	{JOYSTICK_AXIS_Z, "Z Axis"},  // Also used as second stick X on many controllers - rename?
	{JOYSTICK_AXIS_RX, "X Rotation"},
	{JOYSTICK_AXIS_RY, "Y Rotation"},
	{JOYSTICK_AXIS_RZ, "Z Rotation"},  // Also used as second stick Y on many controllers - rename?
	{JOYSTICK_AXIS_HAT_X, "X HAT"},
	{JOYSTICK_AXIS_HAT_Y, "Y HAT"},
	{JOYSTICK_AXIS_LTRIGGER, "TriggerL"},
	{JOYSTICK_AXIS_RTRIGGER, "TriggerR"},
	{JOYSTICK_AXIS_THROTTLE, "Throttle"},
	{JOYSTICK_AXIS_RUDDER, "Rudder"},
	{JOYSTICK_AXIS_WHEEL, "Wheel"},
	{JOYSTICK_AXIS_GAS, "Gas"},
	{JOYSTICK_AXIS_BRAKE, "Brake"},
	{JOYSTICK_AXIS_DISTANCE, "Distance"},
	{JOYSTICK_AXIS_TILT, "Tilt"}
};

static std::string unknown_key_name = "??";
const KeyMap_IntStrPair psp_button_names[] = {
	{CTRL_CIRCLE, "O"},
	{CTRL_CROSS, "X"},
	{CTRL_SQUARE, "[ ]"},
	{CTRL_TRIANGLE, "/\\"},
	{CTRL_LTRIGGER, "L"},
	{CTRL_RTRIGGER, "R"},
	{CTRL_START, "Start"},
	{CTRL_SELECT, "Select"},
	{CTRL_UP, "Up"},
	{CTRL_DOWN, "Down"},
	{CTRL_LEFT, "Left"},
	{CTRL_RIGHT, "Right"},

	{VIRTKEY_AXIS_X_MIN, "An.Left"},
	{VIRTKEY_AXIS_X_MAX, "An.Right"},
	{VIRTKEY_AXIS_Y_MIN, "An.Down"},
	{VIRTKEY_AXIS_Y_MAX, "An.Up"},

	{VIRTKEY_AXIS_RIGHT_X_MIN, "RightAn.Left"},
	{VIRTKEY_AXIS_RIGHT_X_MAX, "RightAn.Right"},
	{VIRTKEY_AXIS_RIGHT_Y_MIN, "RightAn.Down"},
	{VIRTKEY_AXIS_RIGHT_Y_MAX, "RightAn.Up"},

	{VIRTKEY_RAPID_FIRE, "RapidFire"},
	{VIRTKEY_UNTHROTTLE, "Unthrottle"},
	{VIRTKEY_SPEED_TOGGLE, "SpeedToggle"},
	{VIRTKEY_PAUSE, "Pause"},
};

const int AXIS_BIND_KEYCODE_START = 4000;

static std::string FindName(int key, const KeyMap_IntStrPair list[], size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (list[i].key == key)
			return list[i].name;

	return unknown_key_name;
}

std::string GetKeyName(int keyCode)
{
	return FindName(keyCode, key_names, ARRAY_SIZE(key_names));
}

std::string GetPspButtonName(int btn)
{
	return FindName(btn, psp_button_names, ARRAY_SIZE(psp_button_names));
}

int TranslateKeyCodeToAxis(int keyCode, int &direction)
{
	if (keyCode < AXIS_BIND_KEYCODE_START)
		return 0;
	int v = keyCode - AXIS_BIND_KEYCODE_START;

	// Even/odd for direction.
	direction = v & 1 ? -1 : 1;
	return v / 2;
}

int TranslateKeyCodeFromAxis(int axisId, int direction)
{
	direction = direction < 0 ? 1 : 0;
	return AXIS_BIND_KEYCODE_START + axisId * 2 + direction;
}

KeyDef AxisDef(int deviceId, int axisId, int direction) {
	return KeyDef(deviceId, TranslateKeyCodeFromAxis(axisId, direction));
}


static bool FindKeyMapping(int deviceId, int key, int *psp_button)
{
	for (size_t i = 0; i < controllerMaps.size(); i++) {
		if (!controllerMaps[i].active)
			continue;

		auto iter = controllerMaps[i].keys.find(KeyDef(deviceId, key));
		if (iter != controllerMaps[i].keys.end()) {
			*psp_button = iter->second;
			return true;
		}
	}
	return false;
}

int KeyToPspButton(int deviceId, int key)
{
	int search_start_layer = 0;
	int psp_button;

	if (FindKeyMapping(deviceId, key, &psp_button))
		return psp_button;

	return KEYMAP_ERROR_UNKNOWN_KEY;
}

bool KeyFromPspButton(int controllerMap, int btn, int *deviceId, int *keyCode)
{
	int search_start_layer = 0;

	for (auto iter = controllerMaps[controllerMap].keys.begin(); iter != controllerMaps[controllerMap].keys.end(); ++iter) {
		if (iter->second == btn) {
			*deviceId = iter->first.deviceId;
			*keyCode = iter->first.keyCode;
			return true;
		}
	}
	return false;
}

int AxisToPspButton(int deviceId, int axisId, int direction)
{
	int key = TranslateKeyCodeFromAxis(axisId, direction);
	return KeyToPspButton(deviceId, key);
}

bool AxisFromPspButton(int controllerMap, int btn, int *deviceId, int *axisId, int *direction)
{
	int search_start_layer = 0;

	for (auto iter = controllerMaps[controllerMap].keys.begin(); iter != controllerMaps[controllerMap].keys.end(); ++iter) {
		if (iter->second == btn && iter->first.keyCode >= AXIS_BIND_KEYCODE_START) {
			*deviceId = iter->first.deviceId;
			*axisId = TranslateKeyCodeToAxis(iter->first.keyCode, *direction);
			return true;
		}
	}
	return false;
}

std::string NameKeyFromPspButton(int controllerMap, int btn) {
	int deviceId;
	int axisId;
	int direction;
	int keyCode;
	if (AxisFromPspButton(controllerMap, btn, &deviceId, &axisId, &direction)) {
		return GetAxisName(axisId) + (direction < 0 ? "-" : "+");
	}
	if (KeyFromPspButton(controllerMap, btn, &deviceId, &keyCode)) {
		return GetKeyName(keyCode);
	}
	return "unknown";
}

std::string NameDeviceFromPspButton(int controllerMap, int btn) {
	int deviceId;
	int axisId;
	int direction;
	int keyCode;
	if (AxisFromPspButton(controllerMap, btn, &deviceId, &axisId, &direction)) {
		return GetDeviceName(deviceId);
	}
	if (KeyFromPspButton(controllerMap, btn, &deviceId, &keyCode)) {
		return GetDeviceName(deviceId);
	}
	return "unknown";
}

bool IsMappedKey(int deviceId, int key)
{
	return KeyToPspButton(deviceId, key) != KEYMAP_ERROR_UNKNOWN_KEY;
}

std::string NamePspButtonFromKey(int deviceId, int key)
{
	return GetPspButtonName(KeyToPspButton(deviceId, key));
}

void RemoveButtonMapping(int map, int btn) {
	for (auto iter = controllerMaps[map].keys.begin(); iter != controllerMaps[map].keys.end(); ++iter)	{
		if (iter->second == btn) {
			controllerMaps[map].keys.erase(iter);
			return;
		}
	}
}

void SetKeyMapping(int map, int deviceId, int key, int btn)
{
	RemoveButtonMapping(map, btn);
	controllerMaps[map].keys[KeyDef(deviceId, key)] = btn;
}

std::string GetAxisName(int axisId)
{
	return FindName(axisId, axis_names, ARRAY_SIZE(axis_names));
}

bool IsMappedAxis(int deviceId, int axisId, int direction)
{
	int key = TranslateKeyCodeFromAxis(axisId, direction);
	return KeyToPspButton(deviceId, key) != KEYMAP_ERROR_UNKNOWN_KEY;
}

std::string NamePspButtonFromAxis(int deviceId, int axisId, int direction)
{
	int key = TranslateKeyCodeFromAxis(axisId, direction);
	return GetPspButtonName(KeyToPspButton(deviceId, key));
}

void SetAxisMapping(int map, int deviceId, int axisId, int direction, int btn)
{
	int key = TranslateKeyCodeFromAxis(axisId, direction);
	SetKeyMapping(map, deviceId, key, btn);
}

void RestoreDefault() {
	controllerMaps = DefaultKeyMap::KeyMap;
}

// TODO: Make the ini format nicer.
void LoadFromIni(IniFile &file) {
	if (!file.HasSection("ControlMapping")) {
		controllerMaps = DefaultKeyMap::KeyMap;
		return;
	}

	controllerMaps.clear();

	IniFile::Section *controls = file.GetOrCreateSection("ControlMapping");
	std::vector<std::string> maps;
	controls->Get("ControllerMaps", maps);
	if (!maps.size()) {
		controllerMaps = DefaultKeyMap::KeyMap;
		return;
	}

	for (auto x = maps.begin(); x != maps.end(); ++x) {
		ControllerMap newMap;
		newMap.name = *x;
		IniFile::Section *map = file.GetOrCreateSection(newMap.name.c_str());
		map->Get("Active", &newMap.active, true);
		std::map<std::string, std::string> strmap = map->ToMap();

		for (auto x = strmap.begin(); x != strmap.end(); ++x) {
			std::vector<std::string> keyParts;
			SplitString(x->first, '-', keyParts);
			if (keyParts.size() != 2) 
				continue;
			int deviceId = atoi(keyParts[0].c_str());
			int keyCode = atoi(keyParts[1].c_str());
			newMap.keys[KeyDef(deviceId, keyCode)] = atoi(x->second.c_str());
		}
		controllerMaps.push_back(newMap);
	}
}

void SaveToIni(IniFile &file) {
	IniFile::Section *controls = file.GetOrCreateSection("ControlMapping");
	std::vector<std::string> maps;
	for (auto x = controllerMaps.begin(); x != controllerMaps.end(); ++x) {
		maps.push_back(x->name);
	}
	controls->Set("ControllerMaps", maps);

	for (auto x = controllerMaps.begin(); x != controllerMaps.end(); ++x) {
		IniFile::Section *map = file.GetOrCreateSection(x->name.c_str());
		map->Clear();
		map->Set("Active", x->active);
		for (auto iter = x->keys.begin(); iter != x->keys.end(); ++iter) {
			char key[128];
			sprintf(key, "%i-%i", iter->first.deviceId, iter->first.keyCode);
			char value[128];
			sprintf(value, "%i", iter->second);
			map->Set(key, value);
		}
	}
}

}  // KeyMap

std::vector<ControllerMap> controllerMaps = KeyMap::DefaultKeyMap::KeyMap;

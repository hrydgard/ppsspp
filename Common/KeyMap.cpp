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

#ifdef _WIN32
#include <windows.h>
#endif

#include "file/ini_file.h"
#include "input/input_state.h"
#include "base/NativeApp.h"

#include "KeyMap.h"
#include "../Core/HLE/sceUtility.h"
#include "../Core/Config.h"

#include <algorithm>

namespace KeyMap {

KeyDef AxisDef(int deviceId, int axisId, int direction);

struct DefMappingStruct {
	int pspKey;
	int key;
	int direction;
};

KeyMapping g_controllerMap;

static const DefMappingStruct defaultQwertyKeyboardKeyMap[] = {
	{CTRL_SQUARE, NKCODE_A},
	{CTRL_TRIANGLE, NKCODE_S},
	{CTRL_CIRCLE, NKCODE_X},
	{CTRL_CROSS, NKCODE_Z},
	{CTRL_LTRIGGER, NKCODE_Q},
	{CTRL_RTRIGGER, NKCODE_W},

	{CTRL_START, NKCODE_SPACE},
#ifdef _WIN32
	{CTRL_SELECT, NKCODE_V},
#else
	{CTRL_SELECT, NKCODE_ENTER},
#endif
	{CTRL_UP   , NKCODE_DPAD_UP},
	{CTRL_DOWN , NKCODE_DPAD_DOWN},
	{CTRL_LEFT , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT, NKCODE_DPAD_RIGHT},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_I},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_K},
	{VIRTKEY_AXIS_X_MIN, NKCODE_J},
	{VIRTKEY_AXIS_X_MAX, NKCODE_L},
	{VIRTKEY_RAPID_FIRE  , NKCODE_SHIFT_LEFT},
	{VIRTKEY_UNTHROTTLE  , NKCODE_TAB},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_GRAVE},
	{VIRTKEY_PAUSE       , NKCODE_ESCAPE},
	{VIRTKEY_REWIND      , NKCODE_DEL},
};

static const DefMappingStruct defaultAzertyKeyboardKeyMap[] = {
	{CTRL_SQUARE, NKCODE_Q},
	{CTRL_TRIANGLE, NKCODE_S},
	{CTRL_CIRCLE, NKCODE_X},
	{CTRL_CROSS, NKCODE_W},
	{CTRL_LTRIGGER, NKCODE_A},
	{CTRL_RTRIGGER, NKCODE_Z},

	{CTRL_START, NKCODE_SPACE},
#ifdef _WIN32
	{CTRL_SELECT, NKCODE_V},
#else
	{CTRL_SELECT, NKCODE_ENTER},
#endif
	{CTRL_UP   , NKCODE_DPAD_UP},
	{CTRL_DOWN , NKCODE_DPAD_DOWN},
	{CTRL_LEFT , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT, NKCODE_DPAD_RIGHT},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_I},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_K},
	{VIRTKEY_AXIS_X_MIN, NKCODE_J},
	{VIRTKEY_AXIS_X_MAX, NKCODE_L},
	{VIRTKEY_RAPID_FIRE  , NKCODE_SHIFT_LEFT},
	{VIRTKEY_UNTHROTTLE  , NKCODE_TAB},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_GRAVE},
	{VIRTKEY_PAUSE       , NKCODE_ESCAPE},
	{VIRTKEY_REWIND      , NKCODE_DEL},
};

static const DefMappingStruct defaultQwertzKeyboardKeyMap[] = {
	{CTRL_SQUARE, NKCODE_A},
	{CTRL_TRIANGLE, NKCODE_S},
	{CTRL_CIRCLE, NKCODE_X},
	{CTRL_CROSS, NKCODE_Y},
	{CTRL_LTRIGGER, NKCODE_Q},
	{CTRL_RTRIGGER, NKCODE_W},

	{CTRL_START, NKCODE_SPACE},
#ifdef _WIN32
	{CTRL_SELECT, NKCODE_V},
#else
	{CTRL_SELECT, NKCODE_ENTER},
#endif
	{CTRL_UP   , NKCODE_DPAD_UP},
	{CTRL_DOWN , NKCODE_DPAD_DOWN},
	{CTRL_LEFT , NKCODE_DPAD_LEFT},
	{CTRL_RIGHT, NKCODE_DPAD_RIGHT},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_I},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_K},
	{VIRTKEY_AXIS_X_MIN, NKCODE_J},
	{VIRTKEY_AXIS_X_MAX, NKCODE_L},
	{VIRTKEY_RAPID_FIRE  , NKCODE_SHIFT_LEFT},
	{VIRTKEY_UNTHROTTLE  , NKCODE_TAB},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_GRAVE},
	{VIRTKEY_PAUSE       , NKCODE_ESCAPE},
	{VIRTKEY_REWIND      , NKCODE_DEL},
};

static const DefMappingStruct default360KeyMap[] = {
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, -1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, +1},
	{CTRL_CROSS          , NKCODE_BUTTON_A},
	{CTRL_CIRCLE         , NKCODE_BUTTON_B},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_START},
	{CTRL_SELECT         , NKCODE_BUTTON_SELECT},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_UNTHROTTLE  , JOYSTICK_AXIS_RTRIGGER, +1},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_BUTTON_THUMBR},
	{VIRTKEY_PAUSE       , JOYSTICK_AXIS_LTRIGGER, +1},
	{VIRTKEY_PAUSE,        NKCODE_HOME},
};

static const DefMappingStruct defaultShieldKeyMap[] = {
	{CTRL_CROSS, NKCODE_BUTTON_A},
	{CTRL_CIRCLE   ,NKCODE_BUTTON_B},
	{CTRL_SQUARE   ,NKCODE_BUTTON_X},
	{CTRL_TRIANGLE ,NKCODE_BUTTON_Y},
	{CTRL_START,  NKCODE_BUTTON_START},
	{CTRL_SELECT, JOYSTICK_AXIS_LTRIGGER, +1},
	{CTRL_LTRIGGER, NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER, NKCODE_BUTTON_R1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
	{CTRL_LEFT, JOYSTICK_AXIS_HAT_X, -1},
	{CTRL_RIGHT, JOYSTICK_AXIS_HAT_X, +1},
	{CTRL_UP, JOYSTICK_AXIS_HAT_Y, -1},
	{CTRL_DOWN, JOYSTICK_AXIS_HAT_Y, +1},
	{VIRTKEY_UNTHROTTLE, JOYSTICK_AXIS_RTRIGGER, +1 },
	{VIRTKEY_PAUSE, NKCODE_BACK },
};

static const DefMappingStruct defaultBlackberryQWERTYKeyMap[] = {
	{CTRL_SQUARE, NKCODE_J},
	{CTRL_TRIANGLE, NKCODE_I},
	{CTRL_CIRCLE, NKCODE_L},
	{CTRL_CROSS, NKCODE_K},
	{CTRL_LTRIGGER, NKCODE_Q},
	{CTRL_RTRIGGER, NKCODE_W},
	{CTRL_START, NKCODE_SPACE},
	{CTRL_SELECT, NKCODE_ENTER},
	{CTRL_UP   , NKCODE_W},
	{CTRL_DOWN , NKCODE_S},
	{CTRL_LEFT , NKCODE_A},
	{CTRL_RIGHT, NKCODE_D},
	{VIRTKEY_AXIS_Y_MAX, NKCODE_W},
	{VIRTKEY_AXIS_Y_MIN, NKCODE_S},
	{VIRTKEY_AXIS_X_MIN, NKCODE_A},
	{VIRTKEY_AXIS_X_MAX, NKCODE_D},
	{VIRTKEY_RAPID_FIRE  , NKCODE_SHIFT_LEFT},
	{VIRTKEY_UNTHROTTLE  , NKCODE_TAB},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_GRAVE},
	{VIRTKEY_PAUSE       , NKCODE_ESCAPE},
	{VIRTKEY_REWIND      , NKCODE_DEL},
};

static const DefMappingStruct defaultPadMap[] = {
#if defined(ANDROID) || defined(BLACKBERRY)
	{CTRL_CROSS          , NKCODE_BUTTON_A},
	{CTRL_CIRCLE         , NKCODE_BUTTON_B},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	{CTRL_UP             , NKCODE_DPAD_UP}, 
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN}, 
	{CTRL_LEFT           , NKCODE_DPAD_LEFT}, 
	{CTRL_START          , NKCODE_BUTTON_START}, 
	{CTRL_SELECT         , NKCODE_BUTTON_SELECT},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1}, 
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1}, 
	{VIRTKEY_UNTHROTTLE  , NKCODE_BUTTON_R2}, 
	{VIRTKEY_PAUSE       , NKCODE_BUTTON_THUMBR},
	{VIRTKEY_SPEED_TOGGLE, NKCODE_BUTTON_L2}, 
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1}, 
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1}, 
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1}, 
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1}, 
#else
	{CTRL_CROSS          , NKCODE_BUTTON_2}, 
	{CTRL_CIRCLE         , NKCODE_BUTTON_3}, 
	{CTRL_SQUARE         , NKCODE_BUTTON_4}, 
	{CTRL_TRIANGLE       , NKCODE_BUTTON_1}, 
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_10},
	{CTRL_SELECT         , NKCODE_BUTTON_9}, 
	{CTRL_LTRIGGER       , NKCODE_BUTTON_7}, 
	{CTRL_RTRIGGER       , NKCODE_BUTTON_8}, 
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1}, 
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1}, 
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1}, 
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1}, 
#endif
};

static const DefMappingStruct defaultOuyaMap[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_A},
	{CTRL_CIRCLE         , NKCODE_BUTTON_B},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_R2},
	{CTRL_SELECT         , NKCODE_BUTTON_L2},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_UNTHROTTLE  , NKCODE_BUTTON_THUMBL},
	{VIRTKEY_PAUSE       , NKCODE_BUTTON_THUMBR},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, -1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, +1},
};

static const DefMappingStruct defaultXperiaPlay[] = {
	{CTRL_CROSS          , NKCODE_BUTTON_CROSS},
	{CTRL_CIRCLE         , NKCODE_BUTTON_CIRCLE},
	{CTRL_SQUARE         , NKCODE_BUTTON_X},
	{CTRL_TRIANGLE       , NKCODE_BUTTON_Y},
	{CTRL_UP             , NKCODE_DPAD_UP},
	{CTRL_RIGHT          , NKCODE_DPAD_RIGHT},
	{CTRL_DOWN           , NKCODE_DPAD_DOWN},
	{CTRL_LEFT           , NKCODE_DPAD_LEFT},
	{CTRL_START          , NKCODE_BUTTON_START},
	{CTRL_SELECT         , NKCODE_BUTTON_SELECT},
	{CTRL_LTRIGGER       , NKCODE_BUTTON_L1},
	{CTRL_RTRIGGER       , NKCODE_BUTTON_R1},
	{VIRTKEY_AXIS_X_MIN, JOYSTICK_AXIS_X, -1},
	{VIRTKEY_AXIS_X_MAX, JOYSTICK_AXIS_X, +1},
	{VIRTKEY_AXIS_Y_MIN, JOYSTICK_AXIS_Y, -1},
	{VIRTKEY_AXIS_Y_MAX, JOYSTICK_AXIS_Y, +1},
};

static void KeyCodesFromPspButton(int btn, std::vector<keycode_t> *keycodes) {
	for (auto i = g_controllerMap[btn].begin(), end = g_controllerMap[btn].end(); i != end; ++i) {
		keycodes->push_back((keycode_t)i->keyCode);
	}
}

void UpdateConfirmCancelKeys() {
	std::vector<keycode_t> confirmKeys, cancelKeys;
	std::vector<keycode_t> tabLeft, tabRight;

	int confirmKey = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CROSS : CTRL_CIRCLE;
	int cancelKey = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CIRCLE : CTRL_CROSS;

	KeyCodesFromPspButton(confirmKey, &confirmKeys);
	KeyCodesFromPspButton(cancelKey, &cancelKeys);
	KeyCodesFromPspButton(CTRL_LTRIGGER, &tabLeft);
	KeyCodesFromPspButton(CTRL_RTRIGGER, &tabRight);

	// Push several hard-coded keys before submitting to native.
	const keycode_t hardcodedConfirmKeys[] = {
		NKCODE_SPACE,
		NKCODE_ENTER,
	};

	// If they're not already bound, add them in.
	for (size_t i = 0; i < ARRAY_SIZE(hardcodedConfirmKeys); i++) {
		if (std::find(confirmKeys.begin(), confirmKeys.end(), hardcodedConfirmKeys[i]) == confirmKeys.end())
			confirmKeys.push_back(hardcodedConfirmKeys[i]);
	}

	const keycode_t hardcodedCancelKeys[] = {
		NKCODE_ESCAPE,
		NKCODE_BACK,
	};

	for (size_t i = 0; i < ARRAY_SIZE(hardcodedCancelKeys); i++) {
		if (std::find(cancelKeys.begin(), cancelKeys.end(), hardcodedCancelKeys[i]) == cancelKeys.end())
			cancelKeys.push_back(hardcodedCancelKeys[i]);
	}

	SetConfirmCancelKeys(confirmKeys, cancelKeys);
	SetTabLeftRightKeys(tabLeft, tabRight);
}

static void SetDefaultKeyMap(int deviceId, const DefMappingStruct *array, size_t count, bool replace) {
	for (size_t i = 0; i < count; i++) {
		if (array[i].direction == 0)
			SetKeyMapping(array[i].pspKey, KeyDef(deviceId, array[i].key), replace);
		else
			SetAxisMapping(array[i].pspKey, deviceId, array[i].key, array[i].direction, replace);
	}
}

void SetDefaultKeyMap(DefaultMaps dmap, bool replace) {
	switch (dmap) {
	case DEFAULT_MAPPING_KEYBOARD:
		{
			bool azerty = false;
			bool qwertz = false;
#ifdef _WIN32
			HKL localeId = GetKeyboardLayout(0);
			// TODO: Is this list complete enough?
			switch ((int)localeId & 0xFFFF) {
			case 0x407:
				qwertz = true;
				break;
			case 0x040c:
			case 0x080c:
			case 0x1009:
				azerty = true;
				break;
			default:
				break;
			}
#endif
			if (azerty) {
				SetDefaultKeyMap(DEVICE_ID_KEYBOARD, defaultAzertyKeyboardKeyMap, ARRAY_SIZE(defaultAzertyKeyboardKeyMap), replace);
			} else if (qwertz) {
				SetDefaultKeyMap(DEVICE_ID_KEYBOARD, defaultQwertzKeyboardKeyMap, ARRAY_SIZE(defaultQwertzKeyboardKeyMap), replace);
			} else {
				SetDefaultKeyMap(DEVICE_ID_KEYBOARD, defaultQwertyKeyboardKeyMap, ARRAY_SIZE(defaultQwertyKeyboardKeyMap), replace);
			}
		}
		break;
	case DEFAULT_MAPPING_X360:
		SetDefaultKeyMap(DEVICE_ID_X360_0, default360KeyMap, ARRAY_SIZE(default360KeyMap), replace);
		break;
	case DEFAULT_MAPPING_SHIELD:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultShieldKeyMap, ARRAY_SIZE(defaultShieldKeyMap), replace);
		break;
	case DEFAULT_MAPPING_BLACKBERRY_QWERTY:
		SetDefaultKeyMap(DEVICE_ID_KEYBOARD, defaultBlackberryQWERTYKeyMap, ARRAY_SIZE(defaultBlackberryQWERTYKeyMap), replace);
		replace = false;
	case DEFAULT_MAPPING_PAD:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultPadMap, ARRAY_SIZE(defaultPadMap), replace);
		break;
	case DEFAULT_MAPPING_OUYA:
		SetDefaultKeyMap(DEVICE_ID_PAD_0, defaultOuyaMap, ARRAY_SIZE(defaultOuyaMap), replace);
		break;
	case DEFAULT_MAPPING_XPERIA_PLAY:
		SetDefaultKeyMap(DEVICE_ID_DEFAULT, defaultXperiaPlay, ARRAY_SIZE(defaultXperiaPlay), replace);
		break;
	}

	UpdateConfirmCancelKeys();
}

const KeyMap_IntStrPair key_names[] = {
	{NKCODE_A, "A"},
	{NKCODE_B, "B"},
	{NKCODE_C, "C"},
	{NKCODE_D, "D"},
	{NKCODE_E, "E"},
	{NKCODE_F, "F"},
	{NKCODE_G, "G"},
	{NKCODE_H, "H"},
	{NKCODE_I, "I"},
	{NKCODE_J, "J"},
	{NKCODE_K, "K"},
	{NKCODE_L, "L"},
	{NKCODE_M, "M"},
	{NKCODE_N, "N"},
	{NKCODE_O, "O"},
	{NKCODE_P, "P"},
	{NKCODE_Q, "Q"},
	{NKCODE_R, "R"},
	{NKCODE_S, "S"},
	{NKCODE_T, "T"},
	{NKCODE_U, "U"},
	{NKCODE_V, "V"},
	{NKCODE_W, "W"},
	{NKCODE_X, "X"},
	{NKCODE_Y, "Y"},
	{NKCODE_Z, "Z"},

	{NKCODE_0, "0"},
	{NKCODE_1, "1"},
	{NKCODE_2, "2"},
	{NKCODE_3, "3"},
	{NKCODE_4, "4"},
	{NKCODE_5, "5"},
	{NKCODE_6, "6"},
	{NKCODE_7, "7"},
	{NKCODE_8, "8"},
	{NKCODE_9, "9"},

	{NKCODE_F1, "F1"},
	{NKCODE_F2, "F2"},
	{NKCODE_F3, "F3"},
	{NKCODE_F4, "F4"},
	{NKCODE_F5, "F5"},
	{NKCODE_F6, "F6"},
	{NKCODE_F7, "F7"},
	{NKCODE_F8, "F8"},
	{NKCODE_F9, "F9"},
	{NKCODE_F10, "F10"},
	{NKCODE_F11, "F11"},
	{NKCODE_F12, "F12"},

	{NKCODE_GRAVE, "`"},
	{NKCODE_SLASH, "/"},
	{NKCODE_BACKSLASH, "\\"},
	{NKCODE_SEMICOLON, ";"},
	{NKCODE_COMMA, ","},
	{NKCODE_PERIOD, "."},
	{NKCODE_LEFT_BRACKET, "["},
	{NKCODE_RIGHT_BRACKET, "]"},
	{NKCODE_APOSTROPHE, "'"},
	{NKCODE_MINUS, "-"},
	{NKCODE_PLUS, "+"},
	{NKCODE_SYSRQ, "Print"},
	{NKCODE_SCROLL_LOCK, "ScrLock"},
	{NKCODE_BREAK, "Pause"},

	{NKCODE_BACK, "Back"},
	{NKCODE_TAB, "Tab"},
	{NKCODE_ENTER, "Enter"},
	{NKCODE_SHIFT_LEFT, "LShift"},
	{NKCODE_SHIFT_RIGHT, "RShift"},
	{NKCODE_CTRL_LEFT, "LCtrl"},
	{NKCODE_CTRL_RIGHT, "RCtrl"},
	{NKCODE_ALT_LEFT, "LAlt"},
	{NKCODE_ALT_RIGHT, "RAlt"},
	{NKCODE_SPACE, "Space"},
	{NKCODE_WINDOW, "Windows"},
	{NKCODE_DEL, "Backspace"},
	{NKCODE_FORWARD_DEL, "Delete"},
	{NKCODE_MOVE_HOME, "Home"},
	{NKCODE_MOVE_END, "End"},
	{NKCODE_ESCAPE, "Esc"},
	{NKCODE_CAPS_LOCK, "CapsLock"},

	{NKCODE_VOLUME_UP, "Vol +"},
	{NKCODE_VOLUME_DOWN, "Vol -"},
	{NKCODE_HOME, "Home"},
	{NKCODE_INSERT, "Ins"},
	{NKCODE_PAGE_UP, "PgUp"},
	{NKCODE_PAGE_DOWN, "PgDn"},
	{NKCODE_CLEAR, "Clear"}, // 5 when numlock off
	{NKCODE_CALL, "Call"},
	{NKCODE_ENDCALL, "End Call"},

	{NKCODE_DPAD_LEFT, "Left"},
	{NKCODE_DPAD_UP, "Up"},
	{NKCODE_DPAD_RIGHT, "Right"},
	{NKCODE_DPAD_DOWN, "Down"},

	{NKCODE_BUTTON_L1, "L1"},
	{NKCODE_BUTTON_L2, "L2"},
	{NKCODE_BUTTON_R1, "R1"},
	{NKCODE_BUTTON_R2, "R2"},

	{NKCODE_BUTTON_A, "[A]"},
	{NKCODE_BUTTON_B, "[B]"},
	{NKCODE_BUTTON_C, "[C]"},
	{NKCODE_BUTTON_X, "[X]"},
	{NKCODE_BUTTON_Y, "[Y]"},
	{NKCODE_BUTTON_Z, "[Z]"},
	{NKCODE_BUTTON_1, "b1"},
	{NKCODE_BUTTON_2, "b2"},
	{NKCODE_BUTTON_3, "b3"},
	{NKCODE_BUTTON_4, "b4"},
	{NKCODE_BUTTON_5, "b5"},
	{NKCODE_BUTTON_6, "b6"},
	{NKCODE_BUTTON_7, "b7"},
	{NKCODE_BUTTON_8, "b8"},
	{NKCODE_BUTTON_9, "b9"},
	{NKCODE_BUTTON_10, "b10"},
	{NKCODE_BUTTON_11, "b11"},
	{NKCODE_BUTTON_12, "b12"},
	{NKCODE_BUTTON_13, "b13"},
	{NKCODE_BUTTON_14, "b14"},
	{NKCODE_BUTTON_15, "b15"},
	{NKCODE_BUTTON_16, "b16"},
	{NKCODE_BUTTON_START, "Start"},
	{NKCODE_BUTTON_SELECT, "Select"},
	{NKCODE_BUTTON_CIRCLE, "Circle"},
	{NKCODE_BUTTON_CIRCLE_PS3, "Circle3"},
	{NKCODE_BUTTON_CROSS, "Cross"},
	{NKCODE_BUTTON_CROSS_PS3, "Cross3"},
	{NKCODE_BUTTON_TRIANGLE, "Triangle"},
	{NKCODE_BUTTON_SQUARE, "Square"},
	{NKCODE_BUTTON_THUMBL, "ThumbL"},
	{NKCODE_BUTTON_THUMBR, "ThumbR"},
	{NKCODE_BUTTON_MODE, "Mode"},

	{NKCODE_EXT_PIPE, "|"},
	{NKCODE_NUMPAD_DIVIDE, "Num/"},
	{NKCODE_NUMPAD_MULTIPLY, "Num*"},
	{NKCODE_NUMPAD_ADD, "Num+"},
	{NKCODE_NUMPAD_SUBTRACT, "Num-"},
	{NKCODE_NUMPAD_DOT, "Num."},
	{NKCODE_NUMPAD_COMMA, "Num,"},
	{NKCODE_NUMPAD_ENTER, "NumEnter"},
	{NKCODE_NUMPAD_EQUALS, "Num="},
	{NKCODE_NUMPAD_LEFT_PAREN, "Num("},
	{NKCODE_NUMPAD_RIGHT_PAREN, "Num)"},
	{NKCODE_NUMPAD_0, "Num0"},
	{NKCODE_NUMPAD_1, "Num1"},
	{NKCODE_NUMPAD_2, "Num2"},
	{NKCODE_NUMPAD_3, "Num3"},
	{NKCODE_NUMPAD_4, "Num4"},
	{NKCODE_NUMPAD_5, "Num5"},
	{NKCODE_NUMPAD_6, "Num6"},
	{NKCODE_NUMPAD_7, "Num7"},
	{NKCODE_NUMPAD_8, "Num8"},
	{NKCODE_NUMPAD_9, "Num9"},
	
	{NKCODE_LANGUAGE_SWITCH, "Language"},
	{NKCODE_MANNER_MODE, "Manner"},
	{NKCODE_3D_MODE, "3D Mode"},
	{NKCODE_CONTACTS, "Contacts"},
	{NKCODE_CALENDAR, "Calendar"},
	{NKCODE_MUSIC, "Music"},
	{NKCODE_CALCULATOR, "Calc"},
	{NKCODE_ZENKAKU_HANKAKU, "Zenkaku"},
	{NKCODE_EISU, "Eisu"},
	{NKCODE_MUHENKAN, "Muhenkan"},
	{NKCODE_HENKAN, "Henkan"},
	{NKCODE_KATAKANA_HIRAGANA, "Katakana"},
	{NKCODE_YEN, "Yen"},
	{NKCODE_RO, "Ro"},
	{NKCODE_KANA, "Kana"},
	{NKCODE_ASSIST, "Assist"},

	{NKCODE_EXT_MOUSEBUTTON_1, "MB1"},
	{NKCODE_EXT_MOUSEBUTTON_2, "MB2"},
	{NKCODE_EXT_MOUSEBUTTON_3, "MB3"},
	{NKCODE_EXT_MOUSEWHEEL_UP, "MWheelU"},
	{NKCODE_EXT_MOUSEWHEEL_DOWN, "MWheelD"},
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
	{JOYSTICK_AXIS_TILT, "Tilt"},
	{JOYSTICK_AXIS_MOUSE_REL_X, "MouseDX"},
	{JOYSTICK_AXIS_MOUSE_REL_Y, "MouseDY"},
	{JOYSTICK_AXIS_ACCELEROMETER_X, "AccelX"},
	{JOYSTICK_AXIS_ACCELEROMETER_Y, "AccelY"},
	{JOYSTICK_AXIS_ACCELEROMETER_Z, "AccelZ"},
};

static std::string unknown_key_name = "??";

const KeyMap_IntStrPair psp_button_names[] = {
	{CTRL_UP, "Up"},
	{CTRL_DOWN, "Down"},
	{CTRL_LEFT, "Left"},
	{CTRL_RIGHT, "Right"},
	{CTRL_CIRCLE, "Circle"},
	{CTRL_CROSS, "Cross"},
	{CTRL_SQUARE, "Square"},
	{CTRL_TRIANGLE, "Triangle"},
	{CTRL_START, "Start"},
	{CTRL_SELECT, "Select"},
	{CTRL_LTRIGGER, "L"},
	{CTRL_RTRIGGER, "R"},

	{VIRTKEY_AXIS_Y_MAX, "An.Up"},
	{VIRTKEY_AXIS_Y_MIN, "An.Down"},
	{VIRTKEY_AXIS_X_MIN, "An.Left"},
	{VIRTKEY_AXIS_X_MAX, "An.Right"},

	{VIRTKEY_RAPID_FIRE, "RapidFire"},
	{VIRTKEY_UNTHROTTLE, "Unthrottle"},
	{VIRTKEY_SPEED_TOGGLE, "SpeedToggle"},
	{VIRTKEY_PAUSE, "Pause"},
#ifndef USING_GLES2
	{VIRTKEY_REWIND, "Rewind"},
#endif
	{VIRTKEY_SAVE_STATE, "Save State"},
	{VIRTKEY_LOAD_STATE, "Load State"},
	{VIRTKEY_NEXT_SLOT,  "Next Slot"},
#if !defined(_WIN32) && !defined(USING_GLES2)
	{VIRTKEY_TOGGLE_FULLSCREEN, "Toggle Fullscreen"},
#endif

	{VIRTKEY_AXIS_RIGHT_Y_MAX, "RightAn.Up"},
	{VIRTKEY_AXIS_RIGHT_Y_MIN, "RightAn.Down"},
	{VIRTKEY_AXIS_RIGHT_X_MIN, "RightAn.Left"},
	{VIRTKEY_AXIS_RIGHT_X_MAX, "RightAn.Right"},
};

const int AXIS_BIND_NKCODE_START = 4000;

static std::string FindName(int key, const KeyMap_IntStrPair list[], size_t size) {
	for (size_t i = 0; i < size; i++)
		if (list[i].key == key)
			return list[i].name;

	return unknown_key_name;
}

std::string GetKeyName(int keyCode) {
	return FindName(keyCode, key_names, ARRAY_SIZE(key_names));
}

std::string GetKeyOrAxisName(int keyCode) {
	if (keyCode >= AXIS_BIND_NKCODE_START) {
		int direction;
		int axis = TranslateKeyCodeToAxis(keyCode, direction);
		std::string temp = GetAxisName(axis);
		if (direction == 1)
			temp += "+";
		else if (direction == -1)
			temp += "-";
		return temp;
	}
	return FindName(keyCode, key_names, ARRAY_SIZE(key_names));
}

std::string GetAxisName(int axisId) {
	return FindName(axisId, axis_names, ARRAY_SIZE(axis_names));
}

std::string GetPspButtonName(int btn) {
	return FindName(btn, psp_button_names, ARRAY_SIZE(psp_button_names));
}

std::vector<KeyMap_IntStrPair> GetMappableKeys() {
	std::vector<KeyMap_IntStrPair> temp;
	for (size_t i = 0; i < ARRAY_SIZE(psp_button_names); i++) {
		temp.push_back(psp_button_names[i]);
	}
	return temp;
}

int TranslateKeyCodeToAxis(int keyCode, int &direction) {
	if (keyCode < AXIS_BIND_NKCODE_START)
		return 0;

	int v = keyCode - AXIS_BIND_NKCODE_START;
	// Even/odd for direction.
	direction = v & 1 ? -1 : 1;
	return v / 2;
}

int TranslateKeyCodeFromAxis(int axisId, int direction) {
	direction = direction < 0 ? 1 : 0;
	return AXIS_BIND_NKCODE_START + axisId * 2 + direction;
}

KeyDef AxisDef(int deviceId, int axisId, int direction) {
	return KeyDef(deviceId, TranslateKeyCodeFromAxis(axisId, direction));
}

static bool FindKeyMapping(int deviceId, int key, std::vector<int> *psp_button) {
	// Brute force, let's optimize later
	for (auto iter = g_controllerMap.begin(); iter != g_controllerMap.end(); ++iter) {
		for (auto iter2 = iter->second.begin(); iter2 != iter->second.end(); ++iter2) {
			if (*iter2 == KeyDef(deviceId, key)) {
				psp_button->push_back(iter->first);
			}
		}
	}
	return psp_button->size() > 0;
}

bool KeyToPspButton(int deviceId, int key, std::vector<int> *pspKeys) {
	return FindKeyMapping(deviceId, key, pspKeys);
}

// TODO: vector output
bool KeyFromPspButton(int btn, std::vector<KeyDef> *keys) {
	int search_start_layer = 0;

	for (auto iter = g_controllerMap.begin(); iter != g_controllerMap.end(); ++iter) {
		if (iter->first == btn) {
			for (auto iter2 = iter->second.begin(); iter2 != iter->second.end(); ++iter2) {
				keys->push_back(*iter2);
			}
		}
	}
	return false;
}

bool AxisToPspButton(int deviceId, int axisId, int direction, std::vector<int> *pspKeys) {
	int key = TranslateKeyCodeFromAxis(axisId, direction);
	return KeyToPspButton(deviceId, key, pspKeys);
}

bool AxisFromPspButton(int btn, int *deviceId, int *axisId, int *direction) {
	int search_start_layer = 0;

	for (auto iter = g_controllerMap.begin(); iter != g_controllerMap.end(); ++iter) {
		for (auto iter2 = iter->second.begin(); iter2 != iter->second.end(); ++iter2) {
			if (iter->first == btn && iter2->keyCode >= AXIS_BIND_NKCODE_START) {
				*deviceId = iter2->deviceId;
				*axisId = TranslateKeyCodeToAxis(iter2->keyCode, *direction);
				return true;
			}
		}
	}
	return false;
}

void RemoveButtonMapping(int btn) {
	for (auto iter = g_controllerMap.begin(); iter != g_controllerMap.end(); ++iter)	{
		if (iter->first == btn) {
			g_controllerMap.erase(iter);
			return;
		}
	}
}

void SetKeyMapping(int btn, KeyDef key, bool replace) {
	if (key.keyCode < 0)
		return;
	if (replace) {
		RemoveButtonMapping(btn);
		g_controllerMap[btn].clear();
		g_controllerMap[btn].push_back(key);
	} else {
		for (auto iter = g_controllerMap[btn].begin(); iter != g_controllerMap[btn].end(); ++iter) {
			if (*iter == key)
				return;
		}
		g_controllerMap[btn].push_back(key);
	}

	UpdateConfirmCancelKeys();
}

void SetAxisMapping(int btn, int deviceId, int axisId, int direction, bool replace) {
	int key = TranslateKeyCodeFromAxis(axisId, direction);
	SetKeyMapping(btn, KeyDef(deviceId, key), replace);
}

// Note that it's easy to add other defaults if desired.
void RestoreDefault() {
	g_controllerMap.clear();
#if defined(_WIN32)
	SetDefaultKeyMap(DEFAULT_MAPPING_KEYBOARD, true);
	SetDefaultKeyMap(DEFAULT_MAPPING_X360, false);
	SetDefaultKeyMap(DEFAULT_MAPPING_PAD, false);
#elif defined(ANDROID)
	// Autodetect a few common devices
	std::string name = System_GetProperty(SYSPROP_NAME);
	if (IsNvidiaShield(name)) {
		SetDefaultKeyMap(DEFAULT_MAPPING_SHIELD, true);
	} else if (IsOuya(name)) {  // TODO: check!
		SetDefaultKeyMap(DEFAULT_MAPPING_OUYA, true);
	} else if (IsXperiaPlay(name)) {
		SetDefaultKeyMap(DEFAULT_MAPPING_XPERIA_PLAY, true);
	} else {
		SetDefaultKeyMap(DEFAULT_MAPPING_PAD, true);
	}
#elif defined(BLACKBERRY)
	std::string name = System_GetProperty(SYSPROP_NAME);
	if (IsBlackberryQWERTY(name)) {
		SetDefaultKeyMap(DEFAULT_MAPPING_BLACKBERRY_QWERTY, true);
	} else {
		SetDefaultKeyMap(DEFAULT_MAPPING_PAD, true);
	}
#else
	SetDefaultKeyMap(DEFAULT_MAPPING_KEYBOARD, true);
	SetDefaultKeyMap(DEFAULT_MAPPING_PAD, false);
#endif
}

// TODO: Make the ini format nicer.
void LoadFromIni(IniFile &file) {
	RestoreDefault();
	if (!file.HasSection("ControlMapping")) {
		return;
	}

	IniFile::Section *controls = file.GetOrCreateSection("ControlMapping");
	for (size_t i = 0; i < ARRAY_SIZE(psp_button_names); i++) {
		std::string value;
		controls->Get(psp_button_names[i].name.c_str(), &value, "");

		// Erase default mapping
		g_controllerMap.erase(psp_button_names[i].key);
		if (value.empty()) 
			continue;

		std::vector<std::string> mappings;
		SplitString(value, ',', mappings);

		for (size_t j = 0; j < mappings.size(); j++) {
			std::vector<std::string> parts;
			SplitString(mappings[j], '-', parts);
			int deviceId = atoi(parts[0].c_str());
			int keyCode = atoi(parts[1].c_str());

			SetKeyMapping(psp_button_names[i].key, KeyDef(deviceId, keyCode), false);
		}
	}

	UpdateConfirmCancelKeys();
}

void SaveToIni(IniFile &file) {
	IniFile::Section *controls = file.GetOrCreateSection("ControlMapping");

	for (size_t i = 0; i < ARRAY_SIZE(psp_button_names); i++) {
		std::vector<KeyDef> keys;
		KeyFromPspButton(psp_button_names[i].key, &keys);

		std::string value;
		for (size_t j = 0; j < keys.size(); j++) {
			char temp[128];
			sprintf(temp, "%i-%i", keys[j].deviceId, keys[j].keyCode);
			value += temp;
			if (j != keys.size() - 1)
				value += ",";
		}

		controls->Set(psp_button_names[i].name.c_str(), value, "");
	}
}

bool IsOuya(const std::string &name) {
	return name == "OUYA:OUYA Console";
}

bool IsNvidiaShield(const std::string &name) {
	return name == "NVIDIA:SHIELD";
}

bool IsXperiaPlay(const std::string &name) {
	return name == "Sony Ericsson:R800a" || name == "Sony Ericsson:R800i" || name == "Sony Ericsson:R800x" || name == "Sony Ericsson:R800at" || name == "Sony Ericsson:SO-01D" || name == "Sony Ericsson:zeus";
}

bool IsBlackberryQWERTY(const std::string &name) {
	return name == "Blackberry10:QWERTY";
}

bool HasBuiltinController(const std::string &name) {
	return IsOuya(name) || IsXperiaPlay(name) || IsNvidiaShield(name) || IsBlackberryQWERTY(name);
}

}  // KeyMap

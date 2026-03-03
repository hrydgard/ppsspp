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

#include <algorithm>
#include <set>
#include <unordered_map>
#include <mutex>

#include "ppsspp_config.h"

#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/Input/InputState.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Core/HLE/sceUtility.h"
#include "Core/HLE/sceCtrl.h"   // psp keys
#include "Core/Config.h"
#include "Core/KeyMap.h"
#include "Core/KeyMapDefaults.h"

namespace KeyMap {

// We actually need to lock g_controllerMap since it can be modified! Crashes will probably be rare though,
// but I've seen one. Let's just protect it with a mutex.
std::recursive_mutex g_controllerMapLock;
KeyMapping g_controllerMap;

// Incremented on modification, so we know when to update menus.
int g_controllerMapGeneration = 0;
std::set<std::string> g_seenPads;
std::map<InputDeviceID, std::string> g_padNames;
std::set<InputDeviceID> g_seenDeviceIds;

AxisType GetAxisType(InputAxis input) {
	switch (input) {
	case JOYSTICK_AXIS_GAS:
	case JOYSTICK_AXIS_BRAKE:
	case JOYSTICK_AXIS_LTRIGGER:
	case JOYSTICK_AXIS_RTRIGGER:
		return AxisType::TRIGGER;
	case JOYSTICK_AXIS_X:
	case JOYSTICK_AXIS_Y:
	case JOYSTICK_AXIS_Z:
	case JOYSTICK_AXIS_RX:
	case JOYSTICK_AXIS_RY:
	case JOYSTICK_AXIS_RZ:
		return AxisType::STICK;
	default:
		return AxisType::OTHER;
	}
}

// Utility for UI navigation
void SingleInputMappingFromPspButton(int btn, std::vector<InputMapping> *mappings, bool ignoreMouse) {
	std::vector<MultiInputMapping> multiMappings;
	InputMappingsFromPspButton(btn, &multiMappings, ignoreMouse);
	mappings->clear();
	for (auto &mapping : multiMappings) {
		if (!mapping.empty()) {
			mappings->push_back(mapping.mappings[0]);
		} else {
			WARN_LOG(Log::Common, "Encountered empty mapping in multi-mapping for button %d", btn);
		}
	}
}

// TODO: This is such a mess...
void UpdateNativeMenuKeys() {
	std::vector<InputMapping> confirmKeys, cancelKeys;
	std::vector<InputMapping> tabLeft, tabRight;
	std::vector<InputMapping> upKeys, downKeys, leftKeys, rightKeys;
	std::vector<InputMapping> infoKeys;

	// Mouse mapping might be problematic in UI, so let's ignore mouse for UI

	int confirmKey = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CROSS : CTRL_CIRCLE;
	int cancelKey = g_Config.iButtonPreference == PSP_SYSTEMPARAM_BUTTON_CROSS ? CTRL_CIRCLE : CTRL_CROSS;

	SingleInputMappingFromPspButton(confirmKey, &confirmKeys, true);
	SingleInputMappingFromPspButton(cancelKey, &cancelKeys, true);
	SingleInputMappingFromPspButton(CTRL_TRIANGLE, &infoKeys, true);
	SingleInputMappingFromPspButton(CTRL_LTRIGGER, &tabLeft, true);
	SingleInputMappingFromPspButton(CTRL_RTRIGGER, &tabRight, true);
	SingleInputMappingFromPspButton(CTRL_UP, &upKeys, true);
	SingleInputMappingFromPspButton(CTRL_DOWN, &downKeys, true);
	SingleInputMappingFromPspButton(CTRL_LEFT, &leftKeys, true);
	SingleInputMappingFromPspButton(CTRL_RIGHT, &rightKeys, true);

#ifdef __ANDROID__
	// Hardcode DPAD on Android
	upKeys.push_back(InputMapping(DEVICE_ID_ANY, NKCODE_DPAD_UP));
	downKeys.push_back(InputMapping(DEVICE_ID_ANY, NKCODE_DPAD_DOWN));
	leftKeys.push_back(InputMapping(DEVICE_ID_ANY, NKCODE_DPAD_LEFT));
	rightKeys.push_back(InputMapping(DEVICE_ID_ANY, NKCODE_DPAD_RIGHT));
#endif

	// Push several hard-coded keys before submitting to native.
	const InputMapping hardcodedConfirmKeys[] = {
		InputMapping(DEVICE_ID_KEYBOARD, NKCODE_SPACE),
		InputMapping(DEVICE_ID_KEYBOARD, NKCODE_ENTER),
		InputMapping(DEVICE_ID_KEYBOARD, NKCODE_NUMPAD_ENTER),
		InputMapping(DEVICE_ID_ANY, NKCODE_BUTTON_A),
		InputMapping(DEVICE_ID_PAD_0, NKCODE_DPAD_CENTER),  // A number of Android devices.
	};

	// If they're not already bound, add them in.
	for (size_t i = 0; i < ARRAY_SIZE(hardcodedConfirmKeys); i++) {
		if (std::find(confirmKeys.begin(), confirmKeys.end(), hardcodedConfirmKeys[i]) == confirmKeys.end())
			confirmKeys.push_back(hardcodedConfirmKeys[i]);
	}

	const InputMapping hardcodedCancelKeys[] = {
		InputMapping(DEVICE_ID_KEYBOARD, NKCODE_ESCAPE),
		InputMapping(DEVICE_ID_ANY, NKCODE_BACK),
		InputMapping(DEVICE_ID_ANY, NKCODE_BUTTON_B),
		InputMapping(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_4),
	};

	for (size_t i = 0; i < ARRAY_SIZE(hardcodedCancelKeys); i++) {
		if (std::find(cancelKeys.begin(), cancelKeys.end(), hardcodedCancelKeys[i]) == cancelKeys.end())
			cancelKeys.push_back(hardcodedCancelKeys[i]);
	}

	const InputMapping hardcodedInfoKeys[] = {
		InputMapping(DEVICE_ID_KEYBOARD, NKCODE_S),
		InputMapping(DEVICE_ID_KEYBOARD, NKCODE_NUMPAD_ADD),
		InputMapping(DEVICE_ID_PAD_0, NKCODE_BUTTON_Y),  // Also triangle
	};

	for (size_t i = 0; i < ARRAY_SIZE(hardcodedInfoKeys); i++) {
		if (std::find(infoKeys.begin(), infoKeys.end(), hardcodedInfoKeys[i]) == infoKeys.end())
			infoKeys.push_back(hardcodedInfoKeys[i]);
	}

	SetDPadKeys(upKeys, downKeys, leftKeys, rightKeys);
	SetConfirmCancelKeys(confirmKeys, cancelKeys);
	SetTabLeftRightKeys(tabLeft, tabRight);
	SetInfoKeys(infoKeys);

	std::unordered_map<InputDeviceID, int> flipYByDeviceId;
	for (InputDeviceID deviceId : g_seenDeviceIds) {
		auto analogs = MappedAxesForDevice(deviceId);
		flipYByDeviceId[deviceId] = analogs.leftY.direction;
	}
	SetAnalogFlipY(flipYByDeviceId);
}

static const KeyMap_IntStrPair key_names[] = {
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
	{NKCODE_PRINTSCREEN, "Print"},
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
	{NKCODE_META_LEFT, "LMeta"},
	{NKCODE_META_RIGHT, "RMeta"},
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
	{NKCODE_EXT_MOUSEBUTTON_4, "MB4"},
	{NKCODE_EXT_MOUSEBUTTON_5, "MB5"},
	{NKCODE_EXT_MOUSEWHEEL_UP, "MWheelU"},
	{NKCODE_EXT_MOUSEWHEEL_DOWN, "MWheelD"},

	{NKCODE_START_QUESTION, "¿"},
	{NKCODE_LEFTBRACE, "{"},
	{NKCODE_RIGHTBRACE, "}"},

	{NKCODE_GUIDE, "Guide"},
	{NKCODE_INFO, "Info"},
};

static const KeyMap_IntStrPair axis_names[] = {
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

	{VIRTKEY_ANALOG_ROTATE_CW, "Rotate Analog (CW)"},
	{VIRTKEY_ANALOG_ROTATE_CCW, "Rotate Analog (CCW)"},
	{VIRTKEY_ANALOG_LIGHTLY, "Analog limiter"},
	{VIRTKEY_RAPID_FIRE, "RapidFire"},
	{VIRTKEY_AXIS_SWAP, "AxisSwap"},

	{VIRTKEY_FASTFORWARD, "Fast-forward"},
	{VIRTKEY_PAUSE, "Pause"},
	{VIRTKEY_PAUSE_NO_MENU, "Pause (no menu)"},

	{VIRTKEY_SPEED_TOGGLE, "SpeedToggle"},
	{VIRTKEY_SPEED_CUSTOM1, "Alt speed 1"},
	{VIRTKEY_SPEED_CUSTOM2, "Alt speed 2"},
	{VIRTKEY_SPEED_ANALOG, "Analog speed"},
	{VIRTKEY_RESET_EMULATION, "Reset"},
	{VIRTKEY_FRAME_ADVANCE, "Frame Advance"},
#if !defined(MOBILE_DEVICE)
	{VIRTKEY_RECORD, "Audio/Video Recording" },
#endif
	{VIRTKEY_REWIND, "Rewind"},
	{VIRTKEY_SAVE_STATE, "Save State"},
	{VIRTKEY_LOAD_STATE, "Load State"},
	{VIRTKEY_PREVIOUS_SLOT, "Previous Slot"},
	{VIRTKEY_NEXT_SLOT, "Next Slot"},
#if !defined(MOBILE_DEVICE)
	{VIRTKEY_TOGGLE_FULLSCREEN, "Toggle Fullscreen"},
#endif
	{VIRTKEY_TOGGLE_DEBUGGER, "Toggle Debugger"},
	{VIRTKEY_TOGGLE_TILT, "Toggle tilt control"},
	{VIRTKEY_SWAP_LAYOUT, "Swap layout"},

	{VIRTKEY_OPENCHAT, "OpenChat" },

	{VIRTKEY_DEVMENU, "DevMenu"},
	{VIRTKEY_TEXTURE_DUMP, "Texture Dumping"},
	{VIRTKEY_TEXTURE_REPLACE, "Texture Replacement"},
	{VIRTKEY_SCREENSHOT, "Screenshot"},
	{VIRTKEY_MUTE_TOGGLE, "Mute toggle"},

#ifdef OPENXR
	{VIRTKEY_VR_CAMERA_ADJUST, "VR camera adjust"},
	{VIRTKEY_VR_CAMERA_RESET, "VR camera reset"},
#else
	{VIRTKEY_SCREEN_ROTATION_VERTICAL, "Display Portrait"},
	{VIRTKEY_SCREEN_ROTATION_VERTICAL180, "Display Portrait Reversed"},
	{VIRTKEY_SCREEN_ROTATION_HORIZONTAL, "Display Landscape"},
	{VIRTKEY_SCREEN_ROTATION_HORIZONTAL180, "Display Landscape Reversed"},
#endif

	{VIRTKEY_TOGGLE_WLAN, "Toggle WLAN"},
	{VIRTKEY_EXIT_APP, "Exit App"},

	{VIRTKEY_TOGGLE_MOUSE, "Toggle mouse input"},
	{VIRTKEY_TOGGLE_TOUCH_CONTROLS, "Toggle touch controls"},

	{VIRTKEY_AXIS_RIGHT_Y_MAX, "RightAn.Up"},
	{VIRTKEY_AXIS_RIGHT_Y_MIN, "RightAn.Down"},
	{VIRTKEY_AXIS_RIGHT_X_MIN, "RightAn.Left"},
	{VIRTKEY_AXIS_RIGHT_X_MAX, "RightAn.Right"},

	{CTRL_HOME, "Home"},
	{CTRL_HOLD, "Hold"},
	{CTRL_WLAN, "Wlan"},
	{CTRL_REMOTE_HOLD, "Remote hold"},
	{CTRL_VOL_UP, "Vol +"},
	{CTRL_VOL_DOWN, "Vol -"},
	{CTRL_SCREEN, "Screen"},
	{CTRL_NOTE, "Note"},
	{CTRL_L2, "Dev-kit L2"},
	{CTRL_L3, "Dev-kit L3"},
	{CTRL_R2, "Dev-kit R2"},
	{CTRL_R3, "Dev-kit R3"},
};

// key here can be other things than InputKeyCode.
static std::string FindName(int key, const KeyMap_IntStrPair list[], size_t size) {
	for (size_t i = 0; i < size; i++) {
		if (list[i].key == key)
			return list[i].name;
	}
	return StringFromFormat("%02x?", key);
}

std::string GetKeyName(InputKeyCode keyCode) {
	return FindName(keyCode, key_names, ARRAY_SIZE(key_names));
}

std::string GetKeyOrAxisName(const InputMapping &mapping) {
	if (mapping.IsAxis()) {
		int direction;
		int axis = mapping.Axis(&direction);
		std::string temp = GetAxisName(axis);
		if (direction == 1)
			temp += "+";
		else if (direction == -1)
			temp += "-";
		return temp;
	} else {
		return FindName(mapping.keyCode, key_names, ARRAY_SIZE(key_names));
	}
}

std::string GetAxisName(int axisId) {
	return FindName(axisId, axis_names, ARRAY_SIZE(axis_names));
}

std::string GetPspButtonName(int btn) {
	return FindName(btn, psp_button_names, ARRAY_SIZE(psp_button_names));
}

const char* GetPspButtonNameCharPointer(int btn) {
	for (size_t i = 0; i < ARRAY_SIZE(psp_button_names); i++)
		if (psp_button_names[i].key == btn)
			return psp_button_names[i].name;
	return nullptr;
}

const KeyMap::KeyMap_IntStrPair *GetMappableKeys(size_t *count) {
	*count = ARRAY_SIZE(psp_button_names);
	return psp_button_names;
}

bool InputMappingToPspButton(const InputMapping &mapping, std::vector<int> *pspButtons) {
	bool found = false;
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	for (auto iter = g_controllerMap.begin(); iter != g_controllerMap.end(); ++iter) {
		for (auto iter2 = iter->second.begin(); iter2 != iter->second.end(); ++iter2) {
			if (iter2->EqualsSingleMapping(mapping)) {
				if (pspButtons)
					pspButtons->push_back(iter->first);
				found = true;
			}
		}
	}
	return found;
}

// This is the main workhorse of the ControlMapper.
bool InputMappingsFromPspButtonNoLock(int btn, std::vector<MultiInputMapping> *mappings, bool ignoreMouse) {
	auto iter = g_controllerMap.find(btn);
	if (iter == g_controllerMap.end()) {
		return false;
	}
	bool mapped = false;
	if (mappings) {
		mappings->clear();
	}
	for (auto &iter2 : iter->second) {
		bool ignore = ignoreMouse && iter2.HasMouse();
		if (!ignore) {
			mapped = true;
			if (mappings) {
				mappings->push_back(iter2);
			}
		}
	}
	return mapped;
}

bool InputMappingsFromPspButton(int btn, std::vector<MultiInputMapping> *mappings, bool ignoreMouse) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	return InputMappingsFromPspButtonNoLock(btn, mappings, ignoreMouse);
}

void LockMappings() {
	g_controllerMapLock.lock();
}

void UnlockMappings() {
	g_controllerMapLock.unlock();
}

bool PspButtonHasMappings(int btn) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	auto iter = g_controllerMap.find(btn);
	if (iter == g_controllerMap.end()) {
		return false;
	}
	return !iter->second.empty();
}

MappedAnalogAxes MappedAxesForDevice(InputDeviceID deviceId) {
	// Find the axisId mapped for a specific virtual button.
	auto findAxisId = [&](int btn) -> MappedAnalogAxis {
		MappedAnalogAxis info{ -1 };
		for (const auto &key : g_controllerMap[btn]) {
			// Only consider single mappings, combos don't make much sense for these.
			if (key.mappings.empty()) continue;
			auto &mapping = key.mappings[0];
			if (mapping.deviceId == deviceId) {
				info.axisId = TranslateKeyCodeToAxis(mapping.keyCode, &info.direction);
				return info;
			}
		}
		return info;
	};

	// Find the axisId of a pair of opposing buttons.
	auto findAxisIdPair = [&](int minBtn, int maxBtn) -> MappedAnalogAxis {
		MappedAnalogAxis foundMin = findAxisId(minBtn);
		MappedAnalogAxis foundMax = findAxisId(maxBtn);
		if (foundMin.axisId == foundMax.axisId) {
			return foundMax;
		}
		return MappedAnalogAxis{ -1 };
	};

	MappedAnalogAxes result;
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	result.leftX = findAxisIdPair(VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX);
	result.leftY = findAxisIdPair(VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_Y_MAX);
	result.rightX = findAxisIdPair(VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX);
	result.rightY = findAxisIdPair(VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_Y_MAX);
	return result;
}

void RemoveButtonMapping(int btn) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	for (auto iter = g_controllerMap.begin(); iter != g_controllerMap.end(); ++iter)	{
		if (iter->first == btn) {
			g_controllerMap.erase(iter);
			return;
		}
	}
}

bool IsKeyMapped(InputDeviceID device, int key) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	for (auto &iter : g_controllerMap) {
		for (auto &mappedKey : iter.second) {
			if (mappedKey.mappings.contains(InputMapping(device, key))) {
				return true;
			}
		}
	}
	return false;
}

bool ReplaceSingleKeyMapping(int btn, int index, const MultiInputMapping &key) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	// Check for duplicate
	for (int i = 0; i < (int)g_controllerMap[btn].size(); ++i) {
		if (i != index && g_controllerMap[btn][i] == key) {
			g_controllerMap[btn].erase(g_controllerMap[btn].begin()+index);
			g_controllerMapGeneration++;

			UpdateNativeMenuKeys();
			return false;
		}
	}

	if (key.empty()) {
		return false;
	}

	KeyMap::g_controllerMap[btn][index] = key;
	g_controllerMapGeneration++;

	for (auto &mapping : key.mappings) {
		g_seenDeviceIds.insert(mapping.deviceId);
	}
	UpdateNativeMenuKeys();
	return true;
}

void DeleteNthMapping(int key, int number) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	auto iter = g_controllerMap.find(key);
	if (iter != g_controllerMap.end()) {
		if (number < iter->second.size()) {
			iter->second.erase(iter->second.begin() + number);
			g_controllerMapGeneration++;
		}
	}
}

void SetInputMapping(int btn, const MultiInputMapping &key, bool replace) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	if (key.empty()) {
		g_controllerMap.erase(btn);
		return;
	}
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
	g_controllerMapGeneration++;

	for (auto &mapping : key.mappings) {
		g_seenDeviceIds.insert(mapping.deviceId);
	}
}

void RestoreDefault() {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	g_controllerMap.clear();
	g_controllerMapGeneration++;

	if (IsVREnabled()) {
		SetDefaultKeyMap(DEFAULT_MAPPING_VR_HEADSET, false);
		return;
	}

#if PPSSPP_PLATFORM(WINDOWS)
	SetDefaultKeyMap(DEFAULT_MAPPING_KEYBOARD, true);
	SetDefaultKeyMap(DEFAULT_MAPPING_XINPUT, false);
	SetDefaultKeyMap(DEFAULT_MAPPING_PAD, false);
#elif PPSSPP_PLATFORM(ANDROID)
	// Autodetect a few common (and less common) devices
	// Note that here we check the device name, not the controller name. We don't get
	// the controller name until a button has been pressed so can't use it to set defaults.
	std::string name = System_GetProperty(SYSPROP_NAME);
	if (IsNvidiaShield(name)) {
		SetDefaultKeyMap(DEFAULT_MAPPING_SHIELD, false);
	} else if (IsXperiaPlay(name)) {
		SetDefaultKeyMap(DEFAULT_MAPPING_XPERIA_PLAY, false);
	} else if (IsMOQII7S(name)) {
		SetDefaultKeyMap(DEFAULT_MAPPING_MOQI_I7S, false);
	} else if (IsRetroid(name)) {
		SetDefaultKeyMap(DEFAULT_MAPPING_RETROID_CONTROLLER, false);
	} else {
		SetDefaultKeyMap(DEFAULT_MAPPING_ANDROID_PAD, false);
	}
#elif PPSSPP_PLATFORM(IOS)
	SetDefaultKeyMap(DEFAULT_MAPPING_IOS_PAD, false);
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

	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);

	Section *controls = file.GetOrCreateSection("ControlMapping");
	for (size_t i = 0; i < ARRAY_SIZE(psp_button_names); i++) {
		std::string value;
		controls->Get(psp_button_names[i].name, &value);

		// Erase default mapping
		g_controllerMap.erase(psp_button_names[i].key);
		if (value.empty())
			continue;

		std::vector<std::string> mappings;
		SplitString(value, ',', mappings);

		for (size_t j = 0; j < mappings.size(); j++) {
			MultiInputMapping input = MultiInputMapping::FromConfigString(mappings[j]);
			if (input.empty()) {
				continue;  // eat empty mappings, however they arose, so they can't keep haunting us.
			}
			SetInputMapping(psp_button_names[i].key, input, false);
			for (auto mapping : input.mappings) {
				g_seenDeviceIds.insert(mapping.deviceId);
			}
		}
	}

	UpdateNativeMenuKeys();
}

void SaveToIni(IniFile &file) {
	Section *controls = file.GetOrCreateSection("ControlMapping");

	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);

	for (size_t i = 0; i < ARRAY_SIZE(psp_button_names); i++) {
		std::vector<MultiInputMapping> keys;
		InputMappingsFromPspButton(psp_button_names[i].key, &keys, false);

		std::string value;
		for (size_t j = 0; j < keys.size(); j++) {
			value += keys[j].ToConfigString();
			if (j != keys.size() - 1)
				value += ",";
		}

		controls->Set(psp_button_names[i].name, value, "");
	}
}

void ClearAllMappings() {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	g_controllerMap.clear();
	g_controllerMapGeneration++;
}

bool IsNvidiaShield(std::string_view name) {
	return name == "NVIDIA:SHIELD";
}

bool IsRetroid(std::string_view name) {
	// TODO: Not sure if there are differences between different Retroid devices.
	// The one I have is a "Retroid Pocket 2+".
	return startsWith(name, "Retroid:");
}

bool IsNvidiaShieldTV(std::string_view name) {
	return name == "NVIDIA:SHIELD Android TV";
}

bool IsXperiaPlay(std::string_view name) {
	return name == "Sony Ericsson:R800a" || name == "Sony Ericsson:R800i" || name == "Sony Ericsson:R800x" || name == "Sony Ericsson:R800at" || name == "Sony Ericsson:SO-01D" || name == "Sony Ericsson:zeus";
}

bool IsMOQII7S(std::string_view name) {
	return name == "MOQI:I7S";
}

bool HasBuiltinController(std::string_view name) {
	return IsXperiaPlay(name) || IsNvidiaShield(name) || IsMOQII7S(name) || IsRetroid(name);
}

void NotifyPadConnected(InputDeviceID deviceId, std::string_view name) {
	{
		std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
		g_seenPads.insert(std::string(name));
		g_padNames[deviceId] = name;

		// Don't notify within the first 5 seconds, to avoid notification spam on startup.
		// Also for some reason we get some strange things on Android... "Virtual"?
		if (time_now_d() >= 5.0 && name != "Virtual") {
			auto co = GetI18NCategory(I18NCat::CONTROLS);
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, ApplySafeSubstitutions("%1: %2", co->T("Game controller connected"), name), "", "I_CONTROLLER", 2.0f, "controller_connected");
		}
	}

	System_Notify(SystemNotification::PAD_STATE_CHANGED);
}

void NotifyPadDisconnected(InputDeviceID deviceId) {
	{
		std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
		auto iter = g_padNames.find(deviceId);
		if (iter != g_padNames.end()) {
			auto co = GetI18NCategory(I18NCat::CONTROLS);
			g_OSD.Show(OSDType::MESSAGE_WARNING, ApplySafeSubstitutions("%1: %2", co->T("Game controller disconnected"), iter->second), "", "I_CONTROLLER", 2.0f, "controller_connected");
			g_seenPads.erase(iter->second);
		}
		g_padNames.erase(deviceId);
	}
	System_Notify(SystemNotification::PAD_STATE_CHANGED);
}

void ClearControlsWithDeviceId(InputDeviceID deviceId) {
	bool modified = false;
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	for (auto iter = g_controllerMap.begin(); iter != g_controllerMap.end(); ++iter) {
		auto &mappings = iter->second;
		for (auto mapIter = mappings.begin(); mapIter != mappings.end(); ) {
			bool found = false;
			for (auto &mapping : mapIter->mappings) {
				if (mapping.deviceId == deviceId) {
					found = true;
					break;
				}
			}
			if (found) {
				mapIter = mappings.erase(mapIter);
				modified = true;
			} else {
				++mapIter;
			}
		}
	}

	if (modified) {
		g_controllerMapGeneration++;
	}
}

void AutoConfForPad(std::string_view name) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);

	InputDeviceID deviceId = DEVICE_ID_PAD_0;
	for (auto [padDeviceId, padName] : g_padNames) {
		if (padName == name) {
			// Already configured.
			deviceId = padDeviceId;
		}
	}
	ClearControlsWithDeviceId(deviceId);

#if PPSSPP_PLATFORM(ANDROID)
	if (name.find("Xbox") != std::string::npos) {
		SetDefaultKeyMap(DEFAULT_MAPPING_ANDROID_XBOX, false);
	} else if (name == "Retro Station Controller") {
		SetDefaultKeyMap(DEFAULT_MAPPING_RETROID_CONTROLLER, false);
	} else {
		SetDefaultKeyMap(DEFAULT_MAPPING_ANDROID_PAD, false);
	}
#else
#if PPSSPP_PLATFORM(WINDOWS)
	const bool platformSupportsXinput = true;
#else
	const bool platformSupportsXinput = false;
#endif
	if (platformSupportsXinput && name.find("Xbox") != std::string::npos) {
		SetDefaultKeyMap(DEFAULT_MAPPING_XINPUT, false);
	} else {
		SetDefaultKeyMap(DEFAULT_MAPPING_PAD, false);
	}
#endif

	// Add a couple of convenient keyboard mappings by default, too.
#if !defined(MOBILE_DEVICE)
	g_controllerMap[VIRTKEY_PAUSE].push_back(MultiInputMapping(InputMapping(DEVICE_ID_KEYBOARD, NKCODE_ESCAPE)));
	g_controllerMap[VIRTKEY_FASTFORWARD].push_back(MultiInputMapping(InputMapping(DEVICE_ID_KEYBOARD, NKCODE_TAB)));
#endif
	g_controllerMapGeneration++;
}

const std::set<std::string> &GetSeenPads() {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	return g_seenPads;
}

std::string PadName(InputDeviceID deviceId) {
	std::lock_guard<std::recursive_mutex> guard(g_controllerMapLock);
	auto it = g_padNames.find(deviceId);
	if (it != g_padNames.end())
		return it->second;
	return "";
}

bool HasChanged(int &prevGeneration) {
	if (prevGeneration != g_controllerMapGeneration) {
		prevGeneration = g_controllerMapGeneration;
		return true;
	}
	return false;
}

MultiInputMapping MultiInputMapping::FromConfigString(std::string_view str) {
	MultiInputMapping out;
	std::vector<std::string_view> parts;
	SplitString(str, ':', parts);
	for (auto iter : parts) {
		out.mappings.push_back(InputMapping::FromConfigString(iter));
	}
	return out;
}

std::string MultiInputMapping::ToConfigString() const {
	std::string out;
	for (auto iter : mappings) {
		out += iter.ToConfigString() + ":";
	}
	out.pop_back();  // remove the last ':'
	return out;
}

std::string MultiInputMapping::ToVisualString() const {
	std::string out;
	for (auto iter : mappings) {
		out += std::string(GetDeviceName(iter.deviceId)) + "." + GetKeyOrAxisName(iter) + " + ";
	}
	if (!out.empty()) {
		// remove the last ' + '
		out.pop_back();
		out.pop_back();
		out.pop_back();
	}
	return out;
}

}  // KeyMap

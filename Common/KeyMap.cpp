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

#include "input/input_state.h"
#include "Core/Config.h"
#include "KeyMap.h"

using namespace KeyMap;

// Platform specific
// default
std::map<int,int> *platform_keymap = NULL;

// Default key mapping
// Ugly, yet the cleanest way
// I could find to create a
// static map.
// Still nicer than what
// I once did in C.
struct DefaultKeyMap {
	static std::map<int,int> init()
	{
		std::map<int,int> m;
		m[KEYCODE_X] = CTRL_SQUARE;
		m[KEYCODE_Z] = CTRL_TRIANGLE;
		m[KEYCODE_S] = CTRL_CIRCLE;
		m[KEYCODE_A] = CTRL_CROSS;
		m[KEYCODE_Q] = CTRL_LTRIGGER;
		m[KEYCODE_W] = CTRL_RTRIGGER;
		m[KEYCODE_SPACE] = CTRL_START;
		m[KEYCODE_ENTER] = CTRL_SELECT;
		m[KEYCODE_DPAD_UP] = CTRL_UP;
		m[KEYCODE_DPAD_DOWN] = CTRL_DOWN;
		m[KEYCODE_DPAD_LEFT] = CTRL_LEFT;
		m[KEYCODE_DPAD_RIGHT] = CTRL_RIGHT;
		return m;
	}
	static std::map<int,int> KeyMap;
};

std::map<int,int> DefaultKeyMap::KeyMap = DefaultKeyMap::init();

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

	{KEYCODE_1, "1"},
	{KEYCODE_2, "2"},
	{KEYCODE_3, "3"},
	{KEYCODE_4, "4"},
	{KEYCODE_5, "5"},
	{KEYCODE_6, "6"},
	{KEYCODE_7, "7"},
	{KEYCODE_8, "8"},
	{KEYCODE_9, "9"},
	{KEYCODE_0, "0"},


	{KEYCODE_BACK, "Back"},
	{KEYCODE_TAB, "Tab"},
	{KEYCODE_ENTER, "Enter"},
	{KEYCODE_SHIFT_LEFT, "Shift"},
	{KEYCODE_SHIFT_RIGHT, "Shift"},
	{KEYCODE_CTRL_LEFT, "Ctrl"},
	{KEYCODE_CTRL_RIGHT, "Ctrl"},
	{KEYCODE_ALT_LEFT, "Alt"},
	{KEYCODE_ALT_RIGHT, "Alt"},
	{KEYCODE_SPACE, "Space"},
	{KEYCODE_WINDOW, "Windows"},

	{KEYCODE_VOLUME_UP, "Vol Up"},
	{KEYCODE_VOLUME_DOWN, "Vol Down"},
	{KEYCODE_HOME, "Home"},
	{KEYCODE_CALL, "Start Call"},
	{KEYCODE_ENDCALL, "End Call"},

	{KEYCODE_DPAD_LEFT, "Left"},
	{KEYCODE_DPAD_UP, "Up"},
	{KEYCODE_DPAD_RIGHT, "Right"},
	{KEYCODE_DPAD_DOWN, "Down"},
};
static int key_names_count = sizeof(key_names) / sizeof(key_names[0]);
static std::string unknown_key_name = "Unknown";
const KeyMap_IntStrPair psp_button_names[] = {
	{CTRL_CIRCLE, "○"},
	{CTRL_CROSS, "⨯"},
	{CTRL_SQUARE, "□"},
	{CTRL_TRIANGLE, "△"},
	{CTRL_LTRIGGER, "L"},
	{CTRL_RTRIGGER, "R"},
	{CTRL_START, "Start"},
	{CTRL_SELECT, "Select"},
	{CTRL_UP, "Up"},
	{CTRL_DOWN, "Down"},
	{CTRL_LEFT, "Left"},
	{CTRL_RIGHT, "Right"},
};
static int psp_button_names_count = sizeof(psp_button_names) / sizeof(psp_button_names[0]);


static std::string FindName(int key, const KeyMap_IntStrPair list[], int size)
{
	for (int i = 0; i < size; i++)
		if (list[i].key == key)
			return list[i].name;

	return unknown_key_name;
}

std::string KeyMap::GetKeyName(int key)
{
	return FindName(key, key_names, key_names_count);
}

std::string KeyMap::GetPspButtonName(int btn)
{
	return FindName(btn, psp_button_names, psp_button_names_count);
}

static bool FindKeyMapping(int key, int *map_id, int *psp_button)
{
	std::map<int,int>::iterator it;
	if (*map_id <= 0) {
		// check user configuration
		std::map<int,int> user_map = g_Config.iMappingMap;
		it = user_map.find(key);
		if (it != user_map.end()) {
			*map_id = 0;
			*psp_button = it->second;
			return true;
		}
	}

	if (*map_id <= 1 && platform_keymap != NULL) {
		// check optional platform specific keymap
		std::map<int,int> port_map = *platform_keymap;
		it = port_map.find(key);
		if (it != port_map.end()) {
			*map_id = 1;
			*psp_button = it->second;
			return true;
		}
	}

	if (*map_id <= 2) {
		// check default keymap
		const std::map<int,int> default_map = DefaultKeyMap::KeyMap;
		const std::map<int,int>::const_iterator const_it = default_map.find(key);
		if (const_it != default_map.end()) {
			*map_id = 2;
			*psp_button = const_it->second;
			return true;
		}
	}

	*map_id = -1;
	return false;
}



int KeyMap::KeyToPspButton(const int key)
{
	int search_start_layer = 0;
	int psp_button;

	if (FindKeyMapping(key, &search_start_layer, &psp_button))
		return psp_button;

	return KEYMAP_ERROR_UNKNOWN_KEY;
}

bool KeyMap::IsMappedKey(int key)
{
	return KeyMap::KeyToPspButton(key) != KEYMAP_ERROR_UNKNOWN_KEY;
}


std::string KeyMap::NamePspButtonFromKey(int key)
{
	return KeyMap::GetPspButtonName(KeyMap::KeyToPspButton(key));
}

std::string KeyMap::NameKeyFromPspButton(int btn)
{
	// We drive our iteration
	// with the list of key names.
	for (int i = 0; i < key_names_count; i++) {
		const struct KeyMap_IntStrPair key_name = key_names[i];
		if (btn == KeyMap::KeyToPspButton(key_name.key))
			return key_name.name;
	}

	// all psp buttons are mapped from some key
	// but it appears we do not have a name
	// for this key.
	return unknown_key_name;
}

int KeyMap::SetKeyMapping(int key, int btn)
{
	if (KeyMap::IsMappedKey(key))
		return KEYMAP_ERROR_KEY_ALREADY_USED;

	g_Config.iMappingMap[key] = btn;
	return btn;
}

int KeyMap::RegisterPlatformDefaultKeyMap(std::map<int,int> *overriding_map)
{
	if (overriding_map == NULL)
		return 1;
	platform_keymap = overriding_map;
	return 0;
}

void KeyMap::DeregisterPlatformDefaultKeyMap(void)
{
	platform_keymap = NULL;
	return;
}


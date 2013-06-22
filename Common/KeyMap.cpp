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
		m[KEY_x] = PAD_BUTTON_A;
		m[KEY_z] = PAD_BUTTON_B;
		m[KEY_s] = PAD_BUTTON_X;
		m[KEY_a] = PAD_BUTTON_Y;
		m[KEY_q] = PAD_BUTTON_LBUMPER;
		m[KEY_w] = PAD_BUTTON_RBUMPER;
		m[KEY_SPACE] = PAD_BUTTON_START;
		m[KEY_ENTER] = PAD_BUTTON_SELECT;
		m[KEY_ARROW_UP] = PAD_BUTTON_UP;
		m[KEY_ARROW_DOWN] = PAD_BUTTON_DOWN;
		m[KEY_ARROW_LEFT] = PAD_BUTTON_LEFT;
		m[KEY_ARROW_RIGHT] = PAD_BUTTON_RIGHT;
		m[KEY_TAB] = PAD_BUTTON_MENU;
		m[KEY_BACKSPACE] = PAD_BUTTON_BACK;
		m[KEY_ANALOG_UP] = PAD_BUTTON_JOY_UP;
		m[KEY_ANALOG_DOWN] = PAD_BUTTON_JOY_DOWN;
		m[KEY_ANALOG_LEFT] = PAD_BUTTON_JOY_LEFT;
		m[KEY_ANALOG_RIGHT] = PAD_BUTTON_JOY_RIGHT;
		m[KEY_CTRL_LEFT] = PAD_BUTTON_LEFT_THUMB;
		m[KEY_ALT_LEFT] = PAD_BUTTON_RIGHT_THUMB;
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
	{KEY_a, "a"},
	{KEY_b, "b"},
	{KEY_c, "c"},
	{KEY_d, "d"},
	{KEY_e, "e"},
	{KEY_f, "f"},
	{KEY_g, "g"},
	{KEY_h, "h"},
	{KEY_i, "i"},
	{KEY_j, "j"},
	{KEY_k, "k"},
	{KEY_l, "l"},
	{KEY_m, "m"},
	{KEY_n, "n"},
	{KEY_o, "o"},
	{KEY_p, "p"},
	{KEY_q, "q"},
	{KEY_r, "r"},
	{KEY_s, "s"},
	{KEY_t, "t"},
	{KEY_u, "u"},
	{KEY_v, "v"},
	{KEY_w, "w"},
	{KEY_x, "x"},
	{KEY_y, "y"},
	{KEY_z, "z"},

	{KEY_A, "A"},
	{KEY_B, "B"},
	{KEY_C, "C"},
	{KEY_D, "D"},
	{KEY_E, "E"},
	{KEY_F, "F"},
	{KEY_G, "G"},
	{KEY_H, "H"},
	{KEY_I, "I"},
	{KEY_J, "J"},
	{KEY_K, "K"},
	{KEY_L, "L"},
	{KEY_M, "M"},
	{KEY_N, "N"},
	{KEY_O, "O"},
	{KEY_P, "P"},
	{KEY_Q, "Q"},
	{KEY_R, "R"},
	{KEY_S, "S"},
	{KEY_T, "T"},
	{KEY_U, "U"},
	{KEY_V, "V"},
	{KEY_W, "W"},
	{KEY_X, "X"},
	{KEY_Y, "Y"},
	{KEY_Z, "Z"},

	{KEY_1, "1"},
	{KEY_2, "2"},
	{KEY_3, "3"},
	{KEY_4, "4"},
	{KEY_5, "5"},
	{KEY_6, "6"},
	{KEY_7, "7"},
	{KEY_8, "8"},
	{KEY_9, "9"},
	{KEY_0, "0"},


	{KEY_BACKSPACE, "Backspace"},
	{KEY_TAB, "Tab"},
	{KEY_ENTER, "Enter"},
	{KEY_SHIFT_LEFT, "Shift"},
	{KEY_SHIFT_RIGHT, "Shift"},
	{KEY_CTRL_LEFT, "Ctrl"},
	{KEY_CTRL_RIGHT, "Ctrl"},
	{KEY_ALT_LEFT, "Alt"},
	{KEY_ALT_RIGHT, "Alt"},
	{KEY_SPACE, "Space"},
	{KEY_SUPER, "Super"},
	{KEY_SPACE, "Space"},

	{KEY_VOLUME_UP, "Vol Up"},
	{KEY_VOLUME_DOWN, "Vol Down"},
	{KEY_HOME, "Home"},
	{KEY_CALL_START, "Start Call"},
	{KEY_CALL_END, "End Call"},

	{KEY_FASTFORWARD, "Fast foward"},

	{KEY_ARROW_LEFT, "Left"},
	{KEY_ARROW_UP, "Up"},
	{KEY_ARROW_RIGHT, "Right"},
	{KEY_ARROW_DOWN, "Down"},

	{KEY_ANALOG_LEFT, "Analog Left"},
	{KEY_ANALOG_UP, "Analog Up"},
	{KEY_ANALOG_RIGHT, "Analog Right"},
	{KEY_ANALOG_DOWN, "Analog Down"},

	{KEY_ANALOG_ALT_LEFT, "Alt analog Left"},
	{KEY_ANALOG_ALT_UP, "Alt analog Up"},
	{KEY_ANALOG_ALT_RIGHT, "Alt analog Right"},
	{KEY_ANALOG_ALT_DOWN, "Alt analog Down"},

	{KEY_EXTRA1, "Extra1"},
	{KEY_EXTRA2, "Extra2"},
	{KEY_EXTRA3, "Extra3"},
	{KEY_EXTRA4, "Extra4"},
	{KEY_EXTRA5, "Extra5"},
	{KEY_EXTRA6, "Extra6"},
	{KEY_EXTRA7, "Extra7"},
	{KEY_EXTRA8, "Extra8"},
	{KEY_EXTRA9, "Extra9"},
	{KEY_EXTRA0, "Extra0"},
};
static int key_names_count = sizeof(key_names) / sizeof(key_names[0]);
static std::string unknown_key_name = "Unknown";
const KeyMap_IntStrPair psp_button_names[] = {
	{PAD_BUTTON_A, "○"},
	{PAD_BUTTON_B, "⨯"},
	{PAD_BUTTON_X, "□"},
	{PAD_BUTTON_Y, "△"},
	{PAD_BUTTON_LBUMPER, "L"},
	{PAD_BUTTON_RBUMPER, "R"},
	{PAD_BUTTON_START, "Start"},
	{PAD_BUTTON_SELECT, "Select"},
	{PAD_BUTTON_UP, "Up"},
	{PAD_BUTTON_DOWN, "Down"},
	{PAD_BUTTON_LEFT, "Left"},
	{PAD_BUTTON_RIGHT, "Right"},

	{PAD_BUTTON_MENU, "Menu"},
	{PAD_BUTTON_BACK, "Back"},

	{PAD_BUTTON_JOY_UP, "Analog Up"},
	{PAD_BUTTON_JOY_DOWN, "Analog Down"},
	{PAD_BUTTON_JOY_LEFT, "Analog Left"},
	{PAD_BUTTON_JOY_RIGHT, "Analog Right"},

	{PAD_BUTTON_LEFT_THUMB, "Left analog click"},
	{PAD_BUTTON_RIGHT_THUMB, "Right analog click"},
};
static int psp_button_names_count = sizeof(psp_button_names) / sizeof(psp_button_names[0]);


static std::string FindName(int key, const KeyMap_IntStrPair list[], int size)
{
	for (int i = 0; i < size; i++)
		if (list[i].key == key)
			return list[i].name;

	return unknown_key_name;
}

static std::string KeyMap::GetKeyName(KeyMap::Key key)
{
	return FindName((int)key, key_names, key_names_count);
}

static std::string KeyMap::GetPspButtonName(int btn)
{
	return FindName(btn, key_names, key_names_count);
}

static bool FindKeyMapping(int key, int *map_id, int *psp_button)
{
	std::map<int,int>::iterator it;
	if (*map_id >= 0) {
		// check user configuration
		std::map<int,int> user_map = g_Config.iMappingMap;
		it = user_map.find(key);
		if (it != user_map.end()) {
			*map_id = 0;
			*psp_button = it->second;
			return true;
		}
	}

	if (*map_id >= 1 && platform_keymap != NULL) {
		// check optional platform specific keymap
		std::map<int,int> port_map = *platform_keymap;
		it = port_map.find(key);
		if (it != port_map.end()) {
			*map_id = 1;
			*psp_button = it->second;
			return true;
		}
	}

	if (*map_id >= 2) {
		// check default keymap
		const std::map<int,int> default_map = DefaultKeyMap::KeyMap;
		const std::map<int,int>::const_iterator it = default_map.find(key);
		if (it != default_map.end()) {
			*map_id = 2;
			*psp_button = it->second;
			return true;
		}
	}

	*map_id = -1;
	return false;
}



static int KeyMap::KeyToPspButton(const KeyMap::Key key)
{
	int search_start_layer = 0;
	int psp_button;

	if (FindKeyMapping((int)key, &search_start_layer, &psp_button))
		return psp_button;

	return KEYMAP_ERROR_UNKNOWN_KEY;
}

static bool KeyMap::IsMappedKey(Key key)
{
	return KeyMap::KeyToPspButton(key) != KEYMAP_ERROR_UNKNOWN_KEY;
}


static std::string KeyMap::NamePspButtonFromKey(KeyMap::Key key)
{
	return KeyMap::GetPspButtonName(KeyMap::KeyToPspButton(key));
}

static std::string KeyMap::NameKeyFromPspButton(int btn)
{
	// We drive our iteration
	// with the list of key names.
	for (int i = 0; i < key_names_count; i++) {
		const struct KeyMap_IntStrPair *key_name = key_names + i;
		if (btn == KeyMap::KeyToPspButton((KeyMap::Key)key_name->key))
			return key_name->name;
	}

	// all psp buttons are mapped from some key
	// but it appears we do not have a name
	// for this key.
	return unknown_key_name;
}

static int KeyMap::SetKeyMapping(KeyMap::Key key, int btn)
{
	if (KeyMap::IsMappedKey(key))
		return KEYMAP_ERROR_KEY_ALREADY_USED;

	g_Config.iMappingMap[key] = btn;
	return btn;
}

static int KeyMap::RegisterPlatformDefaultKeyMap(std::map<int,int> *overriding_map)
{
	if (overriding_map == NULL)
		return 1;
	platform_keymap = overriding_map;
	return 0;
}

static void KeyMap::DeregisterPlatformDefaultKeyMap(void)
{
	platform_keymap = NULL;
	return;
}


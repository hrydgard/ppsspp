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

#pragma once

#include <string>
#include <map>
#include <vector>
#include <set>
#include "input/input_state.h" // KeyDef
#include "input/keycodes.h"     // keyboard keys
#include "../Core/HLE/sceCtrl.h"   // psp keys

#define KEYMAP_ERROR_KEY_ALREADY_USED -1
#define KEYMAP_ERROR_UNKNOWN_KEY 0

enum {
	VIRTKEY_FIRST = 0x40000001,
	VIRTKEY_AXIS_X_MIN = 0x40000001,
	VIRTKEY_AXIS_Y_MIN = 0x40000002,
	VIRTKEY_AXIS_X_MAX = 0x40000003,
	VIRTKEY_AXIS_Y_MAX = 0x40000004,
	VIRTKEY_RAPID_FIRE = 0x40000005,
	VIRTKEY_UNTHROTTLE = 0x40000006,
	VIRTKEY_PAUSE = 0x40000007,
	VIRTKEY_SPEED_TOGGLE = 0x40000008,
	VIRTKEY_AXIS_RIGHT_X_MIN = 0x40000009,
	VIRTKEY_AXIS_RIGHT_Y_MIN = 0x4000000a,
	VIRTKEY_AXIS_RIGHT_X_MAX = 0x4000000b,
	VIRTKEY_AXIS_RIGHT_Y_MAX = 0x4000000c,
	VIRTKEY_REWIND = 0x4000000d,
	VIRTKEY_SAVE_STATE = 0x4000000e,
	VIRTKEY_LOAD_STATE = 0x4000000f,
	VIRTKEY_NEXT_SLOT = 0x40000010,
	VIRTKEY_TOGGLE_FULLSCREEN = 0x40000011,
	VIRTKEY_ANALOG_LIGHTLY = 0x40000012,
	VIRTKEY_AXIS_SWAP = 0x40000013,
	VIRTKEY_DEVMENU = 0x40000014,
	VIRTKEY_FRAME_ADVANCE = 0x40000015,
	VIRTKEY_RECORD = 0x40000016,
	VIRTKEY_SPEED_CUSTOM1 = 0x40000017,
	VIRTKEY_SPEED_CUSTOM2 = 0x40000018,
	VIRTKEY_LAST,
	VIRTKEY_COUNT = VIRTKEY_LAST - VIRTKEY_FIRST
};

enum DefaultMaps {
	DEFAULT_MAPPING_KEYBOARD,
	DEFAULT_MAPPING_PAD,
	DEFAULT_MAPPING_X360,
	DEFAULT_MAPPING_SHIELD,
	DEFAULT_MAPPING_OUYA,
	DEFAULT_MAPPING_XPERIA_PLAY,
};

const float AXIS_BIND_THRESHOLD = 0.75f;

typedef std::map<int, std::vector<KeyDef>> KeyMapping;

// KeyMap
// A translation layer for key assignment. Provides
// integration with Core's config state.
//
// Does not handle input state managment.
//
// Platform ports should map their platform's keys to KeyMap's keys (NKCODE_*).
//
// Then have KeyMap transform those into psp buttons.

class IniFile;

namespace KeyMap {
	extern KeyMapping g_controllerMap;

	// Key & Button names
	struct KeyMap_IntStrPair {
		int key;
		const char *name;
	};

	// Use if you need to display the textual name
	std::string GetKeyName(int keyCode);
	std::string GetKeyOrAxisName(int keyCode);
	std::string GetAxisName(int axisId);
	std::string GetPspButtonName(int btn);

	std::vector<KeyMap_IntStrPair> GetMappableKeys();

	// Use to translate KeyMap Keys to PSP
	// buttons. You should have already translated
	// your platform's keys to KeyMap keys.
	bool KeyToPspButton(int deviceId, int key, std::vector<int> *pspKeys);
	bool KeyFromPspButton(int btn, std::vector<KeyDef> *keys);

	int TranslateKeyCodeToAxis(int keyCode, int &direction);
	int TranslateKeyCodeFromAxis(int axisId, int direction);

	// Configure the key mapping.
	// Any configuration will be saved to the Core config.
	void SetKeyMapping(int psp_key, KeyDef key, bool replace);

	// Configure an axis mapping, saves the configuration.
	// Direction is negative or positive.
	void SetAxisMapping(int btn, int deviceId, int axisId, int direction, bool replace);

	bool AxisToPspButton(int deviceId, int axisId, int direction, std::vector<int> *pspKeys);
	bool AxisFromPspButton(int btn, int *deviceId, int *axisId, int *direction);
	std::string NamePspButtonFromAxis(int deviceId, int axisId, int direction);

	void LoadFromIni(IniFile &iniFile);
	void SaveToIni(IniFile &iniFile);

	void SetDefaultKeyMap(DefaultMaps dmap, bool replace);

	void RestoreDefault();

	void SwapAxis();
	void UpdateNativeMenuKeys();

	void NotifyPadConnected(const std::string &name);
	bool IsNvidiaShield(const std::string &name);
	bool IsNvidiaShieldTV(const std::string &name);
	bool IsXperiaPlay(const std::string &name);
	bool IsOuya(const std::string &name);
	bool HasBuiltinController(const std::string &name);

	const std::set<std::string> &GetSeenPads();
	void AutoConfForPad(const std::string &name);
}

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
#include "input/keycodes.h"     // keyboard keys
#include "../Core/HLE/sceCtrl.h"   // psp keys

#define KEYMAP_ERROR_KEY_ALREADY_USED -1
#define KEYMAP_ERROR_UNKNOWN_KEY 0

enum {
	VIRTKEY_FIRST = 0x10000,
	VIRTKEY_AXIS_X_MIN = 0x10000,
	VIRTKEY_AXIS_Y_MIN = 0x10001,
	VIRTKEY_AXIS_X_MAX = 0x10002,
	VIRTKEY_AXIS_Y_MAX = 0x10003,
	VIRTKEY_RAPID_FIRE = 0x10004,
	VIRTKEY_UNTHROTTLE = 0x10005,
	VIRTKEY_PAUSE = 0x10006,
	VIRTKEY_SPEED_TOGGLE = 0x10007,
	VIRTKEY_AXIS_RIGHT_X_MIN = 0x10008,
	VIRTKEY_AXIS_RIGHT_Y_MIN = 0x10009,
	VIRTKEY_AXIS_RIGHT_X_MAX = 0x1000a,
	VIRTKEY_AXIS_RIGHT_Y_MAX = 0x1000b,
	VIRTKEY_REWIND = 0x1000c,
	VIRTKEY_SAVE_STATE = 0x1000d,
	VIRTKEY_LOAD_STATE = 0x1000e,
	VIRTKEY_NEXT_SLOT  = 0x1000f,
	VIRTKEY_TOGGLE_FULLSCREEN = 0x10010,
	VIRTKEY_LAST,
	VIRTKEY_COUNT = VIRTKEY_LAST - VIRTKEY_FIRST
};

enum DefaultMaps {
	DEFAULT_MAPPING_KEYBOARD,
	DEFAULT_MAPPING_PAD,
	DEFAULT_MAPPING_X360,
	DEFAULT_MAPPING_SHIELD,
	DEFAULT_MAPPING_BLACKBERRY_QWERTY,
	DEFAULT_MAPPING_OUYA,
	DEFAULT_MAPPING_XPERIA_PLAY,
};

const float AXIS_BIND_THRESHOLD = 0.75f;

class KeyDef {
public:
	KeyDef() : deviceId(0), keyCode(0) {}
	KeyDef(int devId, int k) : deviceId(devId), keyCode(k) {}
	int deviceId;
	int keyCode;

	bool operator < (const KeyDef &other) const {
		if (deviceId < other.deviceId) return true;
		if (deviceId > other.deviceId) return false;
		if (keyCode < other.keyCode) return true;
		return false;
	}
	bool operator == (const KeyDef &other) const {
		if (deviceId != other.deviceId) return false;
		if (keyCode != other.keyCode) return false;
		return true;
	}
};

struct AxisPos {
	int axis;
	float position;

	bool operator < (const AxisPos &other) const {
		if (axis < other.axis) return true;
		if (axis > other.axis) return false;
		return position < other.position;
	}
	bool operator == (const AxisPos &other) const {
		return axis == other.axis && position == other.position;
	}
};

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
		std::string name;
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

	void UpdateConfirmCancelKeys();

	bool IsNvidiaShield(const std::string &name);
	bool IsBlackberryQWERTY(const std::string &name);
	bool IsXperiaPlay(const std::string &name);
	bool IsOuya(const std::string &name);
	bool HasBuiltinController(const std::string &name);
}

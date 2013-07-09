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
	VIRTKEY_LAST,
	VIRTKEY_COUNT = VIRTKEY_LAST - VIRTKEY_FIRST
};

const float AXIS_BIND_THRESHOLD = 0.75f;

class KeyDef {
public:
	KeyDef(int devId, int k) : deviceId(devId), keyCode(k) {}
	int deviceId;
	int keyCode;

	bool operator < (const KeyDef &other) const {
		if (deviceId < other.deviceId) return true;
		if (deviceId > other.deviceId) return false;
		if (keyCode < other.keyCode) return true;
		return false;
	}
};

struct AxisPos {
	int axis;
	float position;
};

typedef std::map<KeyDef, int> KeyMapping;
typedef std::map<KeyDef, AxisPos> AxisMapping;


// Multiple maps can be active at the same time.
class ControllerMap {
public:
	ControllerMap() : active(true) {}
	bool active;
	KeyMapping keys;
	AxisMapping axis;  // TODO
	std::string name;
};


extern std::vector<ControllerMap> controllerMaps;

// KeyMap
// A translation layer for key assignment. Provides
// integration with Core's config state.
// 
// Does not handle input state managment.
// 
// Platform ports should map their platform's keys to KeyMap's keys (KEYCODE_*).
//
// Then have KeyMap transform those into psp buttons.

class IniFile;

namespace KeyMap {
	// Use if you need to display the textual name 
	std::string GetKeyName(int keyCode);
	std::string GetPspButtonName(int btn);

	// Use if to translate KeyMap Keys to PSP
	// buttons. You should have already translated
	// your platform's keys to KeyMap keys.
	//
	// Returns KEYMAP_ERROR_UNKNOWN_KEY
	// for any unmapped key
	int KeyToPspButton(int deviceId, int key);

	bool IsMappedKey(int deviceId, int key);

	// Might be useful if you want to provide hints to users
	// about mapping conflicts
	std::string NamePspButtonFromKey(int deviceId, int key);

	bool KeyFromPspButton(int controllerMap, int btn, int *deviceId, int *keyCode);
	std::string NameKeyFromPspButton(int controllerMap, int btn);
	std::string NameDeviceFromPspButton(int controllerMap, int btn);

	// Configure the key mapping.
	// Any configuration will be saved to the Core config.
	void SetKeyMapping(int map, int deviceId, int keyCode, int psp_key);

	std::string GetAxisName(int axisId);
	int AxisToPspButton(int deviceId, int axisId, int direction);
	bool AxisFromPspButton(int controllerMap, int btn, int *deviceId, int *axisId, int *direction);
	bool IsMappedAxis(int deviceId, int axisId, int direction);
	std::string NamePspButtonFromAxis(int deviceId, int axisId, int direction);

	// Configure an axis mapping, saves the configuration.
	// Direction is negative or positive.
	void SetAxisMapping(int map, int deviceId, int axisId, int direction, int btn);

	void LoadFromIni(IniFile &iniFile);
	void SaveToIni(IniFile &iniFile);
	void RestoreDefault();
}


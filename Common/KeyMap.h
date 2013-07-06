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
#include "input/keycodes.h"     // keyboard keys
#include "../Core/HLE/sceCtrl.h"   // psp keys

#define KEYMAP_ERROR_KEY_ALREADY_USED -1
#define KEYMAP_ERROR_UNKNOWN_KEY 0

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
	// 
	// Returns KEYMAP_ERROR_KEY_ALREADY_USED
	// for mapping conflicts. 0 otherwise.
	int SetKeyMapping(int map, int deviceId, int keyCode, int psp_key);
}


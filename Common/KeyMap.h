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


// sceCtrl does not use all 32 bits
// We will use some bits to define
// our virtual buttons.
// Provides room for 2^7 virt keys
#define VIRTBIT1        0x0002
#define VIRTBIT2        0x0004
#define VIRTBIT3        0x0040
#define VIRTBIT4        0x0080
#define VIRTBIT5        0x0400
#define VIRTBIT6        0x0800
#define VIRT_BTN_MASK \
	(VIRTBIT1 | VIRTBIT2 | VIRTBIT3 | VIRTBIT4 |\
	 VIRTBIT5 | VIRTBIT6)
#define CTRL_BTN_MASK (~VIRT_BTN_MASK)

// Virtual PSP Buttons
// Can be mapped to
// but are never sent
// Core
#define VIRT_UNTHROTTLE        (VIRTBIT1)
#define VIRT_CYCLE_THROTTLE    (VIRTBIT1 | VIRTBIT2)
#define VIRT_ANALOG_UP         (VIRTBIT1 | VIRTBIT3)
#define VIRT_ANALOG_DOWN       (VIRTBIT1 | VIRTBIT4)
#define VIRT_ANALOG_LEFT       (VIRTBIT1 | VIRTBIT5)
#define VIRT_ANALOG_RIGHT      (VIRTBIT1 | VIRTBIT6)
#define VIRT_BACK              (VIRTBIT2 | VIRTBIT3)
#define VIRT_PAUSE             (VIRTBIT2 | VIRTBIT4)

// KeyMap error codes
#define KEYMAP_ERROR_KEY_ALREADY_USED -1
#define KEYMAP_ERROR_UNKNOWN_KEY 0

// KeyMap
// A translation layer for
// key assignment. Provides
// integration with Core's
// config state.
// 
// Does not handle input
// state managment.
// 
// Platform ports should
// map their platform's
// keys to KeyMap's keys.
// Then have KeyMap transform
// those into psp buttons.
namespace KeyMap {
		// Use if you need to
		// display the textual
		// name 
		// These functions are not
		// fast, do not call them
		// a million times.
		std::string GetKeyName(int);
		std::string GetPspButtonName(int);

		// Use if to translate
		// KeyMap Keys to PSP
		// buttons.
		// You should have
		// already translated
		// your platform's keys
		// to KeyMap keys.
		//
		// Returns KEYMAP_ERROR_UNKNOWN_KEY
		// for any unmapped key
		int KeyToPspButton(int);

		bool IsMappedKey(int);
		bool IsUserDefined(int);

		// Might be usful if you want
		// to provide hints to users
		// upon mapping conflicts
		std::string NamePspButtonFromKey(int);

		// Use for showing the existing
		// key mapping.
		std::string NameKeyFromPspButton(int);

		// Configure the key mapping.
		// Any configuration will
		// be saved to the Core
		// config.
		// 
		// Returns KEYMAP_ERROR_KEY_ALREADY_USED
		//  for mapping conflicts. 0 otherwise.
		int SetKeyMapping(int kb_key, int psp_key);

		// Platform specific keymaps
		// override KeyMap's defaults.
		// They do not override user's
		// configuration.
		// A platform default keymap
		// does not need to cover
		// all psp buttons.
		// Any buttons missing will
		// fallback to KeyMap's keymap.
		int RegisterPlatformDefaultKeyMap(std::map<int,int> *);
		void DeregisterPlatformDefaultKeyMap(void);
}


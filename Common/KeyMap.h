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
		enum Key {
			// Lower class latin
			KEY_q = 1, // top row
			KEY_w,
			KEY_e,
			KEY_r,
			KEY_t,
			KEY_y,
			KEY_u,
			KEY_i,
			KEY_o,
			KEY_p,

			KEY_a, // mid row
			KEY_s,
			KEY_d,
			KEY_f,
			KEY_g,
			KEY_h,
			KEY_j,
			KEY_k,
			KEY_l,

			KEY_z, // low row
			KEY_x,
			KEY_c,
			KEY_v,
			KEY_b,
			KEY_n,
			KEY_m,

			// Upper class latin
			KEY_Q, // top row
			KEY_W,
			KEY_E,
			KEY_R,
			KEY_T,
			KEY_Y,
			KEY_U,
			KEY_I,
			KEY_O,
			KEY_P,

			KEY_A, // mid row
			KEY_S,
			KEY_D,
			KEY_F,
			KEY_G,
			KEY_H,
			KEY_J,
			KEY_K,
			KEY_L,

			KEY_Z, // low row
			KEY_X,
			KEY_C,
			KEY_V,
			KEY_B,
			KEY_N,
			KEY_M,


			// Numeric
			KEY_1,
			KEY_2,
			KEY_3,
			KEY_4,
			KEY_5,
			KEY_6,
			KEY_7,
			KEY_8,
			KEY_9,
			KEY_0,

			// Special keys
			KEY_ARROW_LEFT,
			KEY_ARROW_RIGHT,
			KEY_ARROW_UP,
			KEY_ARROW_DOWN,

			KEY_ANALOG_LEFT,
			KEY_ANALOG_RIGHT,
			KEY_ANALOG_UP,
			KEY_ANALOG_DOWN,

			KEY_ANALOG_ALT_LEFT,
			KEY_ANALOG_ALT_RIGHT,
			KEY_ANALOG_ALT_UP,
			KEY_ANALOG_ALT_DOWN,

			KEY_SPACE,
			KEY_ENTER,
			KEY_CTRL_LEFT,
			KEY_CTRL_RIGHT,
			KEY_SHIFT_LEFT,
			KEY_SHIFT_RIGHT,
			KEY_ALT_LEFT,
			KEY_ALT_RIGHT,
			KEY_BACKSPACE,
			KEY_SUPER,
			KEY_TAB,

			// Mobile Keys
			KEY_VOLUME_UP,
			KEY_VOLUME_DOWN,
			KEY_HOME,
			KEY_CALL_START,
			KEY_CALL_END,

			// Special PPSSPP keys
			KEY_FASTFORWARD,

			// Extra keys
			// Use for platform specific keys.
			// Example: android's back btn
			KEY_EXTRA1,
			KEY_EXTRA2,
			KEY_EXTRA3,
			KEY_EXTRA4,
			KEY_EXTRA5,
			KEY_EXTRA6,
			KEY_EXTRA7,
			KEY_EXTRA8,
			KEY_EXTRA9,
			KEY_EXTRA0,

			// TODO: Add any missing keys.
			// Many can be found in the
			// window's port's keyboard
			// files.
		};

		// Use if you need to
		// display the textual
		// name 
		// These functions are not
		// fast, do not call them
		// a million times.
		static std::string GetKeyName(Key);
		static std::string GetPspButtonName(int);

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
		static int KeyToPspButton(Key);

		static bool IsMappedKey(Key);

		// Might be usful if you want
		// to provide hints to users
		// upon mapping conflicts
		static std::string NamePspButtonFromKey(Key);

		// Use for showing the existing
		// key mapping.
		static std::string NameKeyFromPspButton(int);

		// Configure the key mapping.
		// Any configuration will
		// be saved to the Core
		// config.
		// 
		// Returns KEYMAP_ERROR_KEY_ALREADY_USED
		//  for mapping conflicts. 0 otherwise.
		static int SetKeyMapping(Key, int);

		// Platform specific keymaps
		// override KeyMap's defaults.
		// They do not override user's
		// configuration.
		// A platform default keymap
		// does not need to cover
		// all psp buttons.
		// Any buttons missing will
		// fallback to KeyMap's keymap.
		static int RegisterPlatformDefaultKeyMap(std::map<int,int> *);
		static void DeregisterPlatformDefaultKeyMap(void);
}


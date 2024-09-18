// Copyright (c) 2012- PPSSPP Project.

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

#include <vector>
#include <InitGuid.h>
#define DIRECTINPUT_VERSION 0x0800
#define DIRECTINPUT_RGBBUTTONS_MAX 128
#include "InputDevice.h"
#include "dinput.h"

class DinputDevice final :
	public InputDevice
{
public:
	//instantiates device number devnum as explored by the first call to
	//getDevices(), enumerates all devices if not done yet
	DinputDevice(int devnum);
	~DinputDevice();
	int UpdateState() override;
	static size_t getNumPads();
	static void CheckDevices() {
		needsCheck_ = true;
	}

private:
	void ApplyButtons(DIJOYSTATE2 &state);
	//unfortunate and unclean way to keep only one DirectInput instance around
	static LPDIRECTINPUT8 getPDI();
	//unfortunate and unclean way to keep track of the number of devices and the
	//GUIDs of the plugged in devices. This function will only search for devices
	//if none have been found yet and will only list plugged in devices
	//also, it excludes the devices that are compatible with XInput
	static void getDevices(bool refresh);
	//callback for the WinAPI to call
	static BOOL CALLBACK DevicesCallback(
	                LPCDIDEVICEINSTANCE lpddi,
	                LPVOID pvRef
	            );
	static unsigned int     pInstances;
	static std::vector<DIDEVICEINSTANCE> devices;
	static LPDIRECTINPUT8   pDI;
	static bool needsCheck_;
	int                     pDevNum;
	LPDIRECTINPUTDEVICE8    pJoystick;
	DIJOYSTATE2             pPrevState;
	bool                    analog;
	BYTE                    lastButtons_[128];
	WORD                    lastPOV_[4];
	int                     last_lX_;
	int                     last_lY_;
	int                     last_lZ_;
	int                     last_lRx_;
	int                     last_lRy_;
	int                     last_lRz_;
};

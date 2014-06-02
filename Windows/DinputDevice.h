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
#include <InitGuid.h>
#define DIRECTINPUT_VERSION 0x0800
#define DIRECTINPUT_RGBBUTTONS_MAX 128
#include "InputDevice.h"
#include "dinput.h"

class DinputDevice :
	public InputDevice
{
public:
	//instantiates device number devnum as explored by the first call to
	//getDevices(), enumerates all devices if not done yet
	DinputDevice(int devnum);
	~DinputDevice();
	virtual int UpdateState(InputState &input_state);
	virtual bool IsPad() { return true; }
	static size_t getNumPads();
private:
	void ApplyButtons(DIJOYSTATE2 &state, InputState &input_state);
	//unfortunate and unclean way to keep only one DirectInput instance around
	static LPDIRECTINPUT8 getPDI();
	//unfortunate and unclean way to keep track of the number of devices and the
	//GUIDs of the plugged in devices. This function will only search for devices
	//if none have been found yet and will only list plugged in devices
	//also, it excludes the devices that are compatible with XInput
	static void getDevices();
	//callback for the WinAPI to call
	static BOOL CALLBACK DevicesCallback(
	                LPCDIDEVICEINSTANCE lpddi,
	                LPVOID pvRef
	            );
	static unsigned int     pInstances;
	static std::vector<DIDEVICEINSTANCE> devices;
	static LPDIRECTINPUT8   pDI;
	int                     pDevNum;
	LPDIRECTINPUTDEVICE8    pJoystick;
	DIJOYSTATE2             pPrevState;
	bool                    analog;
	BYTE                    lastButtons_[128];
	WORD                    lastPOV_[4];
	short                   last_lX_;
	short                   last_lY_;
	short                   last_lZ_;
	short                   last_lRx_;
	short                   last_lRy_;
	short                   last_lRz_;
};

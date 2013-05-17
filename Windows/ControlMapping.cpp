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

#include "ControlMapping.h"
#include "DinputDevice.h"
#include "XinputDevice.h"

extern unsigned int key_pad_map[];
extern unsigned short analog_ctrl_map[];
extern unsigned int dinput_ctrl_map[];
extern unsigned int xinput_ctrl_map[];

#define DINPUT_CAST(inst) static_cast<std::shared_ptr<DinputDevice>>(inst)

inline UINT* ControlMapping::GetDeviceButtonsMap(UINT curDevice)
{
	switch (curDevice)
	{
		case CONTROLS_KEYBOARD_INDEX: 
			return pButtonsMap;
		case CONTROLS_DIRECT_INPUT_INDEX: 
			return pButtonsMap + buttonsCount;
		case CONTROLS_XINPUT_INDEX: 
			return pButtonsMap + (buttonsCount * 2);
	}
	return NULL;
}

ControlMapping * ControlMapping::CreateInstance(UINT nButtons)
{
	if (nButtons != (key_pad_map_size / sizeof(key_pad_map[0]) / 2))
		return FALSE;
	if (nButtons != (dinput_ctrl_map_size / sizeof(dinput_ctrl_map[0]) / 2))
		return FALSE;
	if (nButtons != (xinput_ctrl_map_size / sizeof(xinput_ctrl_map[0]) / 2))
		return FALSE;
	if (4 != (analog_ctrl_map_size / sizeof(analog_ctrl_map[0]) / 2))
		return FALSE;
	auto inst = new ControlMapping(nButtons);
	if (!inst->pButtonsMap)
		return FALSE;
	return inst;
}

ControlMapping::ControlMapping(UINT nButtons) :
	currentDevicePage(-1),
	currentButtonIndex(-1),
	pButtonsMap(0),
	buttonsCount(nButtons),
	dinput(0),
	xinput(0)
{
	dinput = std::shared_ptr<DinputDevice>(new DinputDevice());
	xinput = std::shared_ptr<XinputDevice>(new XinputDevice());

	pButtonsMap = new UINT[3 * nButtons];
	ZeroMemory(pButtonsMap, sizeof(UINT) * CONTROLS_DEVICE_NUM * nButtons);
	for (int i = 0; i < nButtons; i++) {
		*(GetDeviceButtonsMap(CONTROLS_KEYBOARD_INDEX) + i) = key_pad_map[i * 2];
		*(GetDeviceButtonsMap(CONTROLS_DIRECT_INPUT_INDEX) + i) = dinput_ctrl_map[i * 2];
		*(GetDeviceButtonsMap(CONTROLS_XINPUT_INDEX) + i) = xinput_ctrl_map[i * 2];
	}

	ZeroMemory(&keyboardAnalogMap, sizeof(keyboardAnalogMap));
	for (int i = 0; i < 4; i++) {
		keyboardAnalogMap[i] = analog_ctrl_map[i * 2];
	}
}

ControlMapping::~ControlMapping()
{
	if (pButtonsMap) {
		delete [] pButtonsMap;
		pButtonsMap = NULL;
	}
}

void ControlMapping::UpdateState()
{
	rawState.button = -1;
	switch(currentDevicePage)
	{
		case CONTROLS_KEYBOARD_INDEX: // keyboard
			{
				; // leave it to KeyboardProc.
			}
			break;
		case CONTROLS_DIRECT_INPUT_INDEX: // directinput
			{

				dinput->UpdateRawStateSingle(rawState);
				UINT newButton = (rawState.button != rawState.prevButton && rawState.prevButton == -1)
							   ? rawState.button : -1;
				if (newButton != -1) {
					SetBindCode(newButton);
				}
			}
			break;
		case CONTROLS_XINPUT_INDEX: // xinput
			{
				xinput->UpdateRawStateSingle(rawState);
				UINT newButton = (rawState.button != rawState.prevButton && rawState.prevButton == -1)
							   ? rawState.button : -1;
				if (newButton != -1) {
					SetBindCode(newButton);
				}
			}
			break;
	}
	rawState.prevButton = rawState.button;
}

void ControlMapping::BindToDevices()
{
	for (int i = 0; i < buttonsCount; i++) {
		key_pad_map[i * 2] = *(GetDeviceButtonsMap(CONTROLS_KEYBOARD_INDEX) + i);
		dinput_ctrl_map[i * 2] = *(GetDeviceButtonsMap(CONTROLS_DIRECT_INPUT_INDEX) + i);
		xinput_ctrl_map[i * 2] = *(GetDeviceButtonsMap(CONTROLS_XINPUT_INDEX) + i);
	}
	for (int i = 0; i < 4; i++) {
		analog_ctrl_map[i * 2] = keyboardAnalogMap[i];
	}
}

void  ControlMapping::SetBindCode(UINT newCode)
{
	SetBindCode(newCode, currentDevicePage, currentButtonIndex);
}

void ControlMapping::SetBindCode(UINT newCode, UINT buttonIdx)
{
	SetBindCode(newCode, currentDevicePage, buttonIdx);
}

void ControlMapping::SetBindCode(UINT newCode, UINT deviceIdx, UINT buttonIdx)
{
	if (deviceIdx < CONTROLS_DEVICE_NUM && buttonIdx < buttonsCount)
		*(GetDeviceButtonsMap(deviceIdx) + buttonIdx) = newCode;
}

UINT ControlMapping::GetBindCode(UINT deviceIdx, UINT buttonIdx)
{
	if (deviceIdx < CONTROLS_DEVICE_NUM && buttonIdx < buttonsCount)
		return *(GetDeviceButtonsMap(deviceIdx) + buttonIdx);
	return -1;
}

UINT  ControlMapping::GetBindCode(UINT buttonIdx)
{
	return GetBindCode(currentDevicePage, buttonIdx);
}

UINT ControlMapping::GetBindCode()
{
	return GetBindCode(currentDevicePage, currentButtonIndex);
}

UINT ControlMapping::GetTargetDevice()
{
	return currentDevicePage;
}

void ControlMapping::SetTargetDevice(UINT deviceIdx)
{
	rawState.prevButton = -1;
	currentDevicePage = deviceIdx;
}

UINT ControlMapping::GetTargetButton()
{
	return currentButtonIndex;
}

void ControlMapping::SetTargetButton(UINT buttonIdx)
{
	currentButtonIndex = buttonIdx;
}

bool saveControlsToFile() {
	FILE *wfp = fopen("PPSSPPControls.dat", "wb");
	if (!wfp)
		return false;

	fwrite(key_pad_map, 1, key_pad_map_size, wfp);
	fwrite(analog_ctrl_map, 1, analog_ctrl_map_size, wfp);
	fwrite(dinput_ctrl_map, 1, dinput_ctrl_map_size, wfp);
	fwrite(xinput_ctrl_map, 1, xinput_ctrl_map_size, wfp);
	fclose(wfp);
	return true;
}

bool loadControlsFromFile() {
	FILE *rfp = fopen("PPSSPPControls.dat", "rb");
	if (!rfp)
		return false;
	fseek(rfp, 0, SEEK_END);
	fpos_t fsize = 0;
	fgetpos(rfp, &fsize);
	if (fsize != (key_pad_map_size + analog_ctrl_map_size + dinput_ctrl_map_size + xinput_ctrl_map_size))
		return false;
	fseek(rfp, 0, SEEK_SET);
	fread(key_pad_map, 1, key_pad_map_size, rfp);
	fread(analog_ctrl_map, 1, analog_ctrl_map_size, rfp);
	fread(dinput_ctrl_map, 1, dinput_ctrl_map_size, rfp);
	fread(xinput_ctrl_map, 1, xinput_ctrl_map_size, rfp);
	fclose(rfp);
	return true;
}


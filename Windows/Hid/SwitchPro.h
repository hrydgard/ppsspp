#pragma once

#include "Common/CommonWindows.h"

#include "Windows/Hid/HidCommon.h"

bool InitializeSwitchPro(HANDLE handle);
void GetSwitchButtonInputMappings(const ButtonInputMapping **mappings, size_t *size);
bool ReadSwitchProInput(HANDLE handle, HIDControllerState *state);

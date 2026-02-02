#pragma once

#include "Common/CommonWindows.h"

#include "Windows/Hid/HidCommon.h"

bool InitializeSwitchPro(HANDLE handle);
void GetSwitchInputMappings(const ButtonInputMapping **mappings, size_t *size);
bool ReadSwitchProInput(HANDLE handle, HIDControllerState *state);

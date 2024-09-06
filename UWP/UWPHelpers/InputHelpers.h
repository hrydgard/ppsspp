// Copyright (c) 2023- PPSSPP Project.

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

// Input Devices
bool isKeyboardAvailable();
bool isTouchAvailable();

// Input Pane
void PrepareInputPane();
bool isInputPaneVisible();
bool ShowInputPane();
bool HideInputPane();

// Text Edit
bool isTextEditActive();
void ActivateTextEditInput(bool byFocus = false);
void DeactivateTextEditInput(bool byFocus = false);
bool IgnoreInput(int keyCode);

// Input DPad
void DPadInputState(bool inputState);
bool isDPadActive();

// Keys Status
bool IsCapsLockOn();
bool IsShiftOnHold();
bool IsCtrlOnHold();

// Misc
std::string GetLangRegion();
bool IsXBox();
bool IsMobile();

// Get major, minor, build, revision
void GetVersionInfo(uint32_t& major, uint32_t& minor, uint32_t& build, uint32_t& revision);

// Get formatted windows info as string (for preview)
std::string GetSystemName();
std::string GetWindowsBuild();

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


#include "InputHelpers.h"
#include "UWPUtil.h"
#include "NKCodeFromWindowsSystem.h"
#include "Common/Log.h"

using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::ViewManagement;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;

#pragma region Input Keyboard

bool isKeybaordAvailable() {
	Windows::Devices::Input::KeyboardCapabilities^ keyboardCapabilities = ref new Windows::Devices::Input::KeyboardCapabilities();
	bool hasKeyboard = keyboardCapabilities->KeyboardPresent != 0;
	return hasKeyboard;
}

bool isTouchAvailable() {
	Windows::Devices::Input::TouchCapabilities^ touchCapabilities = ref new Windows::Devices::Input::TouchCapabilities();
	bool hasTouch = touchCapabilities->TouchPresent != 0;
	return hasTouch;
}

bool keyboardVisible = false;
bool InputPaneVisible() {
	return keyboardVisible;
}


void ShowInputPane() {
	VERBOSE_LOG(COMMON, "ShowInputKeyboard");
	InputPane::GetForCurrentView()->TryShow();
	keyboardVisible = true;
}

void HideInputPane() {
	VERBOSE_LOG(COMMON, "HideInputKeyboard");
	InputPane::GetForCurrentView()->TryHide();
	keyboardVisible = false;
}

#pragma endregion

#pragma region Keys Status
bool IsCapsLockOn() {
	auto capsLockState = CoreApplication::MainView->CoreWindow->GetKeyState(VirtualKey::CapitalLock);
	return (capsLockState == CoreVirtualKeyStates::Locked);
}
bool IsShiftOnHold() {
	auto shiftState = CoreApplication::MainView->CoreWindow->GetKeyState(VirtualKey::Shift);
	return (shiftState == CoreVirtualKeyStates::Down);
}
bool IsCtrlOnHold() {
	auto ctrlState = CoreApplication::MainView->CoreWindow->GetKeyState(VirtualKey::Control);
	return (ctrlState == CoreVirtualKeyStates::Down);
}
#pragma endregion

#pragma region Misc
std::string GetLangRegion() {
	std::string langRegion = "en_US";
	wchar_t lcCountry[256];

	if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcCountry, 256) != FALSE) {
		langRegion = ConvertWStringToUTF8(lcCountry);
		for (size_t i = 0; i < langRegion.size(); i++) {
			if (langRegion[i] == '-')
				langRegion[i] = '_';
		}
	}
	return langRegion;
}
#pragma endregion

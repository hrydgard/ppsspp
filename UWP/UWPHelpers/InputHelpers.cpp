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

#include <list>

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

#pragma region Extenstions
template<typename T>
bool findInList(std::list<T>& inputList, T& str) {
	return (std::find(inputList.begin(), inputList.end(), str) != inputList.end());
};
#pragma endregion

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

bool keyboardActive = false;
bool inputPaneVisible = false;
bool isInputPaneVisible() {
	// On Xbox we can check this using input pan
	if (IsXBox()) {
		return InputPane::GetForCurrentView()->Visible;
	}
	else {
		return inputPaneVisible;
	}
}

bool isKeyboardActive() {
	return keyboardActive;
}

void ActivateKeyboardInput() {
	DEBUG_LOG(COMMON, "Activate input keyboard");
	// When no
	inputPaneVisible = InputPane::GetForCurrentView()->TryShow();
	keyboardActive = true;

	if (inputPaneVisible) {
		DEBUG_LOG(COMMON, "Input pane: TryShow accepted");
	}
	else {
		DEBUG_LOG(COMMON, "Input pane: (TryShow is not accepted or pane is not supported)");

	}
}

void DeactivateKeyboardInput() {
	DEBUG_LOG(COMMON, "Deactivate input keyboard");
	if (InputPane::GetForCurrentView()->TryHide()) {
		inputPaneVisible = false;
		DEBUG_LOG(COMMON, "Input pane: TryHide accepted");
	}
	else {
		DEBUG_LOG(COMMON, "Input pane: TryHide is not accepted, or pane is not visible");
	}
	keyboardActive = false;
}

bool IgnoreInput(int keyCode) {
	// When keyboard mode active and char is passed this function return 'true'
	// it will help to prevent KeyDown from sending the same code again
	bool ignoreInput = false;
	if (isKeyboardActive() && !IsCtrlOnHold()) {
		// To avoid bothering KeyDown to check this case always
		// we don't get here unless keyboard mode is active
		std::list<int> nonCharList = { 
			NKCODE_CTRL_LEFT, 
			NKCODE_CTRL_RIGHT,
			NKCODE_MOVE_HOME,
			NKCODE_PAGE_UP,
			NKCODE_MOVE_END,
			NKCODE_PAGE_DOWN,
			NKCODE_FORWARD_DEL,
			NKCODE_DEL,
			NKCODE_ENTER,
			NKCODE_NUMPAD_ENTER,
			NKCODE_EXT_MOUSEBUTTON_1,
			NKCODE_EXT_MOUSEBUTTON_2,
			NKCODE_EXT_MOUSEBUTTON_3,
			NKCODE_EXT_MOUSEBUTTON_4,
			NKCODE_EXT_MOUSEBUTTON_5,
			NKCODE_ESCAPE 
		};
		if (!isInputPaneVisible()) {
			// Keyboard active but no on-screen keyboard
			// allow arrow keys for navigation
			nonCharList.push_back(NKCODE_DPAD_UP);
			nonCharList.push_back(NKCODE_DPAD_DOWN);
			nonCharList.push_back(NKCODE_DPAD_LEFT);
			nonCharList.push_back(NKCODE_DPAD_RIGHT);
			nonCharList.push_back(NKCODE_BACK);
		}

		ignoreInput = !findInList(nonCharList, keyCode);
	}

	return ignoreInput;
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

bool IsXBox() {
	auto deviceInfo = Windows::System::Profile::AnalyticsInfo::VersionInfo;
	return deviceInfo->DeviceFamily == "Windows.Xbox";
}

bool IsMobile() {
	auto deviceInfo = Windows::System::Profile::AnalyticsInfo::VersionInfo;
	return deviceInfo->DeviceFamily == "Windows.Mobile";
}
#pragma endregion

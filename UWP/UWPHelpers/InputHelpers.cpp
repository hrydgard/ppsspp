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
#include "Common/OSVersion.h"

#include <ppl.h>
#include <ppltasks.h>

using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::ViewManagement;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;
using namespace Windows::UI::ViewManagement;

#pragma region Extenstions
template<typename T>
bool findInList(std::list<T>& inputList, T& str) {
	return (std::find(inputList.begin(), inputList.end(), str) != inputList.end());
};
#pragma endregion

#pragma region Input Devices
bool isKeyboardAvailable() {
	Windows::Devices::Input::KeyboardCapabilities^ keyboardCapabilities = ref new Windows::Devices::Input::KeyboardCapabilities();
	bool hasKeyboard = keyboardCapabilities->KeyboardPresent != 0;
	return hasKeyboard;
}

bool isTouchAvailable() {
	Windows::Devices::Input::TouchCapabilities^ touchCapabilities = ref new Windows::Devices::Input::TouchCapabilities();
	bool hasTouch = touchCapabilities->TouchPresent != 0;
	return hasTouch;
}
#pragma endregion

#pragma region Input Keyboard

bool dPadInputActive = false;
bool textEditActive = false;
bool inputPaneVisible = false;
Platform::Agile<Windows::UI::ViewManagement::InputPane> inputPane = nullptr;

void OnShowing(InputPane^ pane, InputPaneVisibilityEventArgs^ args) {
	inputPaneVisible = true;
}
void OnHiding(InputPane^ pane, InputPaneVisibilityEventArgs^ args) {
	inputPaneVisible = false;
}

void PrepareInputPane() {
	inputPane = InputPane::GetForCurrentView();
	inputPane->Showing += ref new Windows::Foundation::TypedEventHandler<InputPane^, InputPaneVisibilityEventArgs^>(&OnShowing);
	inputPane->Hiding += ref new Windows::Foundation::TypedEventHandler<InputPane^, InputPaneVisibilityEventArgs^>(&OnHiding);
}

// Show input pane (OSK)
bool ShowInputPane() {
	return !isInputPaneVisible() ? inputPane->TryShow() : true;
}
// Hide input pane (OSK)
bool HideInputPane() {
	return isInputPaneVisible() ? inputPane->TryHide() : true;
}

// Check if input pane (OSK) visible
bool isInputPaneVisible() {
	return inputPaneVisible;
}

// Check if text edit active (got focus)
bool isTextEditActive() {
	return textEditActive;
}

// Set if the current input is DPad
void DPadInputState(bool inputState) {
	dPadInputActive = inputState;
}

// Check if the current input is DPad
bool isDPadActive() {
	return dPadInputActive;
}

void ActivateTextEditInput(bool byFocus) {
	// Must be performed from UI thread
	Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
	CoreDispatcherPriority::Normal,
	ref new Windows::UI::Core::DispatchedHandler([=]()
	{
		if (byFocus) {
			// Why we should delay? (Mostly happen on XBox)
			// once the popup appear, UI is reporting 3 focus events for text edit (got, lost, got)
			// it might be caused by the input pane it self but anyway..
			// because this has to on UI thread and async, we will end with input pane hidden
			// the small delay will ensure that last recieved event is (got focus)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		
		if (!isInputPaneVisible() && (isDPadActive() || !IsXBox())) {
			if (ShowInputPane()) {
				DEBUG_LOG(Log::Common, "Input pane: TryShow accepted");
			}
			else {
				DEBUG_LOG(Log::Common, "Input pane: (TryShow is not accepted or not supported)");
			}
		}
		DEBUG_LOG(Log::Common, "Text edit active");
		textEditActive = true;
	}));
}

void DeactivateTextEditInput(bool byFocus) {
	// Must be performed from UI thread
	Windows::ApplicationModel::Core::CoreApplication::MainView->CoreWindow->Dispatcher->RunAsync(
	CoreDispatcherPriority::Normal,
	ref new Windows::UI::Core::DispatchedHandler([=]()
	{
		if (isInputPaneVisible()) {
			if (HideInputPane()) {
				DEBUG_LOG(Log::Common, "Input pane: TryHide accepted");
			}
			else {
				DEBUG_LOG(Log::Common, "Input pane: TryHide is not accepted, or not supported");
			}
		}
		if (isTextEditActive()) {
			DEBUG_LOG(Log::Common, "Text edit inactive");
			textEditActive = false;
		}
	}));
}

bool IgnoreInput(int keyCode) {
	// When text edit active and char is passed this function return 'true'
	// it will help to prevent KeyDown from sending the same code again
	bool ignoreInput = false;
	// TODO: Add ` && !IsCtrlOnHold()` once it's ready and implemented
	if (isTextEditActive()) {
		// To avoid bothering KeyDown to check this case always
		// we don't get here unless text edit is active
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
		};
		if (!isInputPaneVisible()) {
			// Keyboard active but no on-screen keyboard
			// allow arrow keys for navigation
			nonCharList.push_back(NKCODE_DPAD_UP);
			nonCharList.push_back(NKCODE_DPAD_DOWN);
			nonCharList.push_back(NKCODE_DPAD_LEFT);
			nonCharList.push_back(NKCODE_DPAD_RIGHT);
			nonCharList.push_back(NKCODE_BACK);
			nonCharList.push_back(NKCODE_ESCAPE);
		}

		ignoreInput = !findInList(nonCharList, keyCode);
	}

	return ignoreInput;
}
#pragma endregion

#pragma region Keys Status
bool IsCapsLockOn() {
	// TODO: Perform this on UI thread, delayed as currently `KeyDown` don't detect those anyway
	auto capsLockState = CoreApplication::MainView->CoreWindow->GetKeyState(VirtualKey::CapitalLock);
	return (capsLockState == CoreVirtualKeyStates::Locked);
}
bool IsShiftOnHold() {
	// TODO: Perform this on UI thread, delayed as currently `KeyDown` don't detect those anyway
	auto shiftState = CoreApplication::MainView->CoreWindow->GetKeyState(VirtualKey::Shift);
	return (shiftState == CoreVirtualKeyStates::Down);
}
bool IsCtrlOnHold() {
	// TODO: Perform this on UI thread, delayed as currently `KeyDown` don't detect those anyway
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

void GetVersionInfo(uint32_t& major, uint32_t& minor, uint32_t& build, uint32_t& revision) {
	Platform::String^ deviceFamilyVersion = Windows::System::Profile::AnalyticsInfo::VersionInfo->DeviceFamilyVersion;
	uint64_t version = std::stoull(deviceFamilyVersion->Data());

	major = static_cast<uint32_t>((version & 0xFFFF000000000000L) >> 48);
	minor = static_cast<uint32_t>((version & 0x0000FFFF00000000L) >> 32);
	build = static_cast<uint32_t>((version & 0x00000000FFFF0000L) >> 16);
	revision = static_cast<uint32_t>(version & 0x000000000000FFFFL);
}

std::string GetSystemName() {
	std::string osName = "Microsoft Windows 10";

	if (IsXBox()) {
		osName = "Xbox OS";
	}
	else {
		uint32_t major = 0, minor = 0, build = 0, revision = 0;
		GetVersionInfo(major, minor, build, revision);

		if (build >= 22000) {
			osName = "Microsoft Windows 11";
		}
	}
	return osName + " " + GetWindowsSystemArchitecture();
}

std::string GetWindowsBuild() {
	uint32_t major = 0, minor = 0, build = 0, revision = 0;
	GetVersionInfo(major, minor, build, revision);

	char buffer[50];
	sprintf_s(buffer, sizeof(buffer), "%u.%u.%u (rev. %u)", major, minor, build, revision);
	return std::string(buffer);
}
#pragma endregion

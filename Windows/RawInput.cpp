// Copyright (c) 2014- PPSSPP Project.

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

#include <set>
#include <algorithm>
#include "base/NativeApp.h"
#include "input/input_state.h"
#include "Windows/RawInput.h"
#include "Windows/KeyboardDevice.h"
#include "Windows/WndMainWindow.h"
#include "Windows/WindowsHost.h"
#include "Common/CommonFuncs.h"
#include "Core/Config.h"

#ifndef HID_USAGE_PAGE_GENERIC
#define HID_USAGE_PAGE_GENERIC         ((USHORT) 0x01)
#endif
#ifndef HID_USAGE_GENERIC_POINTER
#define HID_USAGE_GENERIC_POINTER      ((USHORT) 0x01)
#endif
#ifndef HID_USAGE_GENERIC_MOUSE
#define HID_USAGE_GENERIC_MOUSE        ((USHORT) 0x02)
#endif
#ifndef HID_USAGE_GENERIC_JOYSTICK
#define HID_USAGE_GENERIC_JOYSTICK     ((USHORT) 0x04)
#endif
#ifndef HID_USAGE_GENERIC_GAMEPAD
#define HID_USAGE_GENERIC_GAMEPAD      ((USHORT) 0x05)
#endif
#ifndef HID_USAGE_GENERIC_KEYBOARD
#define HID_USAGE_GENERIC_KEYBOARD     ((USHORT) 0x06)
#endif
#ifndef HID_USAGE_GENERIC_KEYPAD
#define HID_USAGE_GENERIC_KEYPAD       ((USHORT) 0x07)
#endif
#ifndef HID_USAGE_GENERIC_MULTIAXIS
#define HID_USAGE_GENERIC_MULTIAXIS    ((USHORT) 0x07)
#endif

extern InputState input_state;

namespace WindowsRawInput {
	static std::set<int> keyboardKeysDown;
	static void *rawInputBuffer;
	static size_t rawInputBufferSize;
	static bool menuActive;

	void Init() {
		RAWINPUTDEVICE dev[3];
		memset(dev, 0, sizeof(dev));

		dev[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
		dev[0].usUsage = HID_USAGE_GENERIC_KEYBOARD;
		dev[0].dwFlags = g_Config.bIgnoreWindowsKey ? RIDEV_NOHOTKEYS : 0;

		dev[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
		dev[1].usUsage = HID_USAGE_GENERIC_MOUSE;
		dev[1].dwFlags = 0;

		dev[2].usUsagePage = HID_USAGE_PAGE_GENERIC;
		dev[2].usUsage = HID_USAGE_GENERIC_JOYSTICK;
		dev[2].dwFlags = 0;

		if (!RegisterRawInputDevices(dev, 3, sizeof(RAWINPUTDEVICE))) {
			WARN_LOG(COMMON, "Unable to register raw input devices: %s", GetLastErrorMsg());
		}
	}

	bool UpdateMenuActive() {
		MENUBARINFO info;
		memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		if (GetMenuBarInfo(MainWindow::GetHWND(), OBJID_MENU, 0, &info) != 0) {
			menuActive = info.fBarFocused != FALSE;
		}
		return menuActive;
	}

	static int GetTrueVKey(const RAWKEYBOARD &kb) {
		switch (kb.VKey) {
		case VK_SHIFT:
			return MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);

		case VK_CONTROL:
			if (kb.Flags & RI_KEY_E0)
				return VK_RCONTROL;
			else
				return VK_LCONTROL;

		case VK_MENU:
			if (kb.Flags & RI_KEY_E0)
				return VK_RMENU;  // Right Alt / AltGr
			else
				return VK_LMENU;  // Left Alt

		default:
			return kb.VKey;
		}
	}

	void ProcessKeyboard(RAWINPUT *raw, bool foreground) {
		if (menuActive && UpdateMenuActive()) {
			// Ignore keyboard input while a menu is active, it's probably interacting with the menu.
			return;
		}

		KeyInput key;
		key.deviceId = DEVICE_ID_KEYBOARD;

		if (raw->data.keyboard.Message == WM_KEYDOWN || raw->data.keyboard.Message == WM_SYSKEYDOWN) {
			key.flags = KEY_DOWN;
			key.keyCode = windowsTransTable[GetTrueVKey(raw->data.keyboard)];

			if (key.keyCode) {
				NativeKey(key);
				keyboardKeysDown.insert(key.keyCode);
			}
		} else if (raw->data.keyboard.Message == WM_KEYUP) {
			key.flags = KEY_UP;
			key.keyCode = windowsTransTable[GetTrueVKey(raw->data.keyboard)];

			if (key.keyCode) {
				NativeKey(key);

				auto keyDown = std::find(keyboardKeysDown.begin(), keyboardKeysDown.end(), key.keyCode);
				if (keyDown != keyboardKeysDown.end())
					keyboardKeysDown.erase(keyDown);
			}
		}
	}

	void ProcessMouse(RAWINPUT *raw, bool foreground) {
		if (menuActive && UpdateMenuActive()) {
			// Ignore mouse input while a menu is active, it's probably interacting with the menu.
			return;
		}

		TouchInput touch;
		touch.id = 0;
		touch.flags = TOUCH_MOVE;
		touch.x = input_state.pointer_x[0];
		touch.y = input_state.pointer_y[0];

		KeyInput key;
		key.deviceId = DEVICE_ID_MOUSE;

		mouseDeltaX += raw->data.mouse.lLastX;
		mouseDeltaY += raw->data.mouse.lLastY;

		if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
			key.flags = KEY_DOWN;
			key.keyCode = windowsTransTable[VK_RBUTTON];
			NativeTouch(touch);
			NativeKey(key);
		} else if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) {
			key.flags = KEY_UP;
			key.keyCode = windowsTransTable[VK_RBUTTON];
			NativeTouch(touch);
			NativeKey(key);
		}

		// TODO : Smooth and translate to an axis every frame.
		// NativeAxis()
	}

	void ProcessHID(RAWINPUT *raw, bool foreground) {
		// TODO: Use hidparse or something to understand the data.
	}

	LRESULT Process(HWND hWnd, WPARAM wParam, LPARAM lParam) {
		UINT dwSize;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		if (!rawInputBuffer) {
			rawInputBuffer = malloc(dwSize);
			rawInputBufferSize = dwSize;
		}
		if (dwSize > rawInputBufferSize) {
			rawInputBuffer = realloc(rawInputBuffer, dwSize);
		}
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawInputBuffer, &dwSize, sizeof(RAWINPUTHEADER));
		RAWINPUT *raw = (RAWINPUT *)rawInputBuffer;
		bool foreground = GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUT;

		switch (raw->header.dwType) {
		case RIM_TYPEKEYBOARD:
			ProcessKeyboard(raw, foreground);
			break;

		case RIM_TYPEMOUSE:
			ProcessMouse(raw, foreground);
			break;

		case RIM_TYPEHID:
			ProcessHID(raw, foreground);
			break;
		}

		// Docs say to call DefWindowProc to perform necessary cleanup.
		return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
	}

	void LoseFocus() {
		// Force-release all held keys on the keyboard to prevent annoying stray inputs.
		KeyInput key;
		key.deviceId = DEVICE_ID_KEYBOARD;
		key.flags = KEY_UP;
		for (auto i = keyboardKeysDown.begin(); i != keyboardKeysDown.end(); ++i) {
			key.keyCode = *i;
			NativeKey(key);
		}
	}

	void NotifyMenu() {
		UpdateMenuActive();
	}

	void Shutdown() {
		if (rawInputBuffer) {
			free(rawInputBuffer);
		}
		rawInputBuffer = 0;
	}
};
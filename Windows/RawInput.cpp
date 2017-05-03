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
#include <vector>

#include "base/NativeApp.h"
#include "base/display.h"
#include "input/input_state.h"
#include "Common/Log.h"
#include "Windows/RawInput.h"
#include "Windows/KeyboardDevice.h"
#include "Windows/MainWindow.h"
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

namespace WindowsRawInput {
	static std::set<int> keyboardKeysDown;
	static void *rawInputBuffer;
	static size_t rawInputBufferSize;
	static bool menuActive;
	static bool focused = true;
	static bool mouseDown[5] = { false, false, false, false, false }; //left, right, middle, 4, 5
	static float mouseX = 0.0f;
	static float mouseY = 0.0f;

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
			WARN_LOG(SYSTEM, "Unable to register raw input devices: %s", GetLastErrorMsg());
		}
	}

	bool UpdateMenuActive() {
		MENUBARINFO info;
		memset(&info, 0, sizeof(info));
		info.cbSize = sizeof(info);
		if (GetMenuBarInfo(MainWindow::GetHWND(), OBJID_MENU, 0, &info) != 0) {
			menuActive = info.fBarFocused != FALSE;
		} else {
			// In fullscreen mode, we remove the menu
			menuActive = false;
		}
		return menuActive;
	}

	static int GetTrueVKey(const RAWKEYBOARD &kb) {
		int vKey = kb.VKey;
		switch (kb.VKey) {
		case VK_SHIFT:
			vKey = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
			break;

		case VK_CONTROL:
			if (kb.Flags & RI_KEY_E0)
				vKey = VK_RCONTROL;
			else
				vKey = VK_LCONTROL;
			break;

		case VK_MENU:
			if (kb.Flags & RI_KEY_E0)
				vKey = VK_RMENU;  // Right Alt / AltGr
			else
				vKey = VK_LMENU;  // Left Alt
			break;

		//case VK_RETURN:
			// if (kb.Flags & RI_KEY_E0)
			//	vKey = VK_RETURN;  // Numeric return - no code for this. Can special case.
		//	break;

		// Source: http://molecularmusings.wordpress.com/2011/09/05/properly-handling-keyboard-input/
		case VK_NUMLOCK:
			// correct PAUSE/BREAK and NUM LOCK silliness, and set the extended bit
			vKey = MapVirtualKey(kb.VKey, MAPVK_VK_TO_VSC) | 0x100;
			break;

		default:
			break;
		}

		return windowsTransTable[vKey];
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
			key.keyCode = GetTrueVKey(raw->data.keyboard);

			if (key.keyCode) {
				NativeKey(key);
				keyboardKeysDown.insert(key.keyCode);
			}
		} else if (raw->data.keyboard.Message == WM_KEYUP) {
			key.flags = KEY_UP;
			key.keyCode = GetTrueVKey(raw->data.keyboard);

			if (key.keyCode) {
				NativeKey(key);

				auto keyDown = std::find(keyboardKeysDown.begin(), keyboardKeysDown.end(), key.keyCode);
				if (keyDown != keyboardKeysDown.end())
					keyboardKeysDown.erase(keyDown);
			}
		}
	}

	LRESULT ProcessChar(HWND hWnd, WPARAM wParam, LPARAM lParam) {
		KeyInput key;
		key.keyCode = wParam;  // Note that this is NOT a NKCODE but a Unicode character!
		key.flags = KEY_CHAR;
		key.deviceId = DEVICE_ID_KEYBOARD;
		NativeKey(key);
		return 0;
	}

	static bool MouseInWindow(HWND hWnd) {
		POINT pt;
		if (GetCursorPos(&pt) != 0) {
			RECT rt;
			if (GetWindowRect(hWnd, &rt) != 0) {
				return PtInRect(&rt, pt) != 0;
			}
		}
		return true;
	}

	void ProcessMouse(HWND hWnd, RAWINPUT *raw, bool foreground) {
		if (menuActive && UpdateMenuActive()) {
			// Ignore mouse input while a menu is active, it's probably interacting with the menu.
			return;
		}

		TouchInput touch;
		touch.id = 0;
		touch.flags = TOUCH_MOVE;
		touch.x = mouseX;
		touch.y = mouseY;

		KeyInput key;
		key.deviceId = DEVICE_ID_MOUSE;

		g_mouseDeltaX += raw->data.mouse.lLastX;
		g_mouseDeltaY += raw->data.mouse.lLastY;

		const int rawInputDownID[5] = {
			RI_MOUSE_LEFT_BUTTON_DOWN,
			RI_MOUSE_RIGHT_BUTTON_DOWN,
			RI_MOUSE_BUTTON_3_DOWN,
			RI_MOUSE_BUTTON_4_DOWN,
			RI_MOUSE_BUTTON_5_DOWN
		};
		const int rawInputUpID[5] = {
			RI_MOUSE_LEFT_BUTTON_UP,
			RI_MOUSE_RIGHT_BUTTON_UP,
			RI_MOUSE_BUTTON_3_UP,
			RI_MOUSE_BUTTON_4_UP,
			RI_MOUSE_BUTTON_5_UP
		};
		const int vkInputID[5] = {
			VK_LBUTTON,
			VK_RBUTTON,
			VK_MBUTTON,
			VK_XBUTTON1,
			VK_XBUTTON2
		};

		for (int i = 0; i < 5; i++) {
			if (i > 0 || (g_Config.bMouseControl && (GetUIState() == UISTATE_INGAME || g_Config.bMapMouse))) {
				if (raw->data.mouse.usButtonFlags & rawInputDownID[i]) {
					key.flags = KEY_DOWN;
					key.keyCode = windowsTransTable[vkInputID[i]];
					NativeTouch(touch);
					if (MouseInWindow(hWnd)) {
						NativeKey(key);
					}
					mouseDown[i] = true;
				} else if (raw->data.mouse.usButtonFlags & rawInputUpID[i]) {
					key.flags = KEY_UP;
					key.keyCode = windowsTransTable[vkInputID[i]];
					NativeTouch(touch);
					if (MouseInWindow(hWnd)) {
						if (!mouseDown[i]) {
							// This means they were focused outside, and clicked inside.
							// Seems intentional, so send a down first.
							key.flags = KEY_DOWN;
							NativeKey(key);
							key.flags = KEY_UP;
							NativeKey(key);
						} else {
							NativeKey(key);
						}
					}
					mouseDown[i] = false;
				}
			}
		}
	}

	void ProcessHID(RAWINPUT *raw, bool foreground) {
		// TODO: Use hidparse or something to understand the data.
	}

	LRESULT Process(HWND hWnd, WPARAM wParam, LPARAM lParam) {
		UINT dwSize;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		if (!rawInputBuffer) {
			rawInputBuffer = malloc(dwSize);
			memset(rawInputBuffer, 0, dwSize);
			rawInputBufferSize = dwSize;
		}
		if (dwSize > rawInputBufferSize) {
			rawInputBuffer = realloc(rawInputBuffer, dwSize);
			memset(rawInputBuffer, 0, dwSize);
		}
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, rawInputBuffer, &dwSize, sizeof(RAWINPUTHEADER));
		RAWINPUT *raw = (RAWINPUT *)rawInputBuffer;
		bool foreground = GET_RAWINPUT_CODE_WPARAM(wParam) == RIM_INPUT;

		switch (raw->header.dwType) {
		case RIM_TYPEKEYBOARD:
			ProcessKeyboard(raw, foreground);
			break;

		case RIM_TYPEMOUSE:
			ProcessMouse(hWnd, raw, foreground);
			break;

		case RIM_TYPEHID:
			ProcessHID(raw, foreground);
			break;
		}

		// Docs say to call DefWindowProc to perform necessary cleanup.
		return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
	}

	void SetMousePos(float x, float y) {
		mouseX = x;
		mouseY = y;
	}

	void GainFocus() {
		focused = true;
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
		focused = false;
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

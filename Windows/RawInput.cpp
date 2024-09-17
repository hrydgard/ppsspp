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

#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Common/Input/InputState.h"
#include "Common/Log.h"
#include "Windows/RawInput.h"
#include "Windows/MainWindow.h"
#include "Windows/WindowsHost.h"
#include "Common/CommonFuncs.h"
#include "Common/SysError.h"
#include "Core/Config.h"
#include "Core/HLE/Plugins.h"

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
	static std::set<InputKeyCode> keyboardKeysDown;
	static void *rawInputBuffer;
	static size_t rawInputBufferSize;
	static bool menuActive;
	static bool focused = true;
	static bool mouseDown[5] = { false, false, false, false, false }; //left, right, middle, 4, 5
	static float mouseX = 0.0f;
	static float mouseY = 0.0f;

	// TODO: More keys need to be added, but this is more than
	// a fair start.
	static std::map<int, InputKeyCode> windowsTransTable = {
		{ 'A', NKCODE_A },
		{ 'B', NKCODE_B },
		{ 'C', NKCODE_C },
		{ 'D', NKCODE_D },
		{ 'E', NKCODE_E },
		{ 'F', NKCODE_F },
		{ 'G', NKCODE_G },
		{ 'H', NKCODE_H },
		{ 'I', NKCODE_I },
		{ 'J', NKCODE_J },
		{ 'K', NKCODE_K },
		{ 'L', NKCODE_L },
		{ 'M', NKCODE_M },
		{ 'N', NKCODE_N },
		{ 'O', NKCODE_O },
		{ 'P', NKCODE_P },
		{ 'Q', NKCODE_Q },
		{ 'R', NKCODE_R },
		{ 'S', NKCODE_S },
		{ 'T', NKCODE_T },
		{ 'U', NKCODE_U },
		{ 'V', NKCODE_V },
		{ 'W', NKCODE_W },
		{ 'X', NKCODE_X },
		{ 'Y', NKCODE_Y },
		{ 'Z', NKCODE_Z },
		{ '0', NKCODE_0 },
		{ '1', NKCODE_1 },
		{ '2', NKCODE_2 },
		{ '3', NKCODE_3 },
		{ '4', NKCODE_4 },
		{ '5', NKCODE_5 },
		{ '6', NKCODE_6 },
		{ '7', NKCODE_7 },
		{ '8', NKCODE_8 },
		{ '9', NKCODE_9 },
		{ VK_OEM_PERIOD, NKCODE_PERIOD },
		{ VK_OEM_COMMA, NKCODE_COMMA },
		{ VK_NUMPAD0, NKCODE_NUMPAD_0 },
		{ VK_NUMPAD1, NKCODE_NUMPAD_1 },
		{ VK_NUMPAD2, NKCODE_NUMPAD_2 },
		{ VK_NUMPAD3, NKCODE_NUMPAD_3 },
		{ VK_NUMPAD4, NKCODE_NUMPAD_4 },
		{ VK_NUMPAD5, NKCODE_NUMPAD_5 },
		{ VK_NUMPAD6, NKCODE_NUMPAD_6 },
		{ VK_NUMPAD7, NKCODE_NUMPAD_7 },
		{ VK_NUMPAD8, NKCODE_NUMPAD_8 },
		{ VK_NUMPAD9, NKCODE_NUMPAD_9 },
		{ VK_DECIMAL, NKCODE_NUMPAD_DOT },
		{ VK_DIVIDE, NKCODE_NUMPAD_DIVIDE },
		{ VK_MULTIPLY, NKCODE_NUMPAD_MULTIPLY },
		{ VK_SUBTRACT, NKCODE_NUMPAD_SUBTRACT },
		{ VK_ADD, NKCODE_NUMPAD_ADD },
		{ VK_SEPARATOR, NKCODE_NUMPAD_COMMA },
		{ VK_OEM_MINUS, NKCODE_MINUS },
		{ VK_OEM_PLUS, NKCODE_PLUS },
		{ VK_LCONTROL, NKCODE_CTRL_LEFT },
		{ VK_RCONTROL, NKCODE_CTRL_RIGHT },
		{ VK_LSHIFT, NKCODE_SHIFT_LEFT },
		{ VK_RSHIFT, NKCODE_SHIFT_RIGHT },
		{ VK_LMENU, NKCODE_ALT_LEFT },
		{ VK_RMENU, NKCODE_ALT_RIGHT },
		{ VK_BACK, NKCODE_DEL },  // yes! http://stackoverflow.com/questions/4886858/android-edittext-deletebackspace-key-event
		{ VK_SPACE, NKCODE_SPACE },
		{ VK_ESCAPE, NKCODE_ESCAPE },
		{ VK_UP, NKCODE_DPAD_UP },
		{ VK_INSERT, NKCODE_INSERT },
		{ VK_HOME, NKCODE_MOVE_HOME },
		{ VK_PRIOR, NKCODE_PAGE_UP },
		{ VK_NEXT, NKCODE_PAGE_DOWN },
		{ VK_DELETE, NKCODE_FORWARD_DEL },
		{ VK_END, NKCODE_MOVE_END },
		{ VK_TAB, NKCODE_TAB },
		{ VK_DOWN, NKCODE_DPAD_DOWN },
		{ VK_LEFT, NKCODE_DPAD_LEFT },
		{ VK_RIGHT, NKCODE_DPAD_RIGHT },
		{ VK_CAPITAL, NKCODE_CAPS_LOCK },
		{ VK_CLEAR, NKCODE_CLEAR },
		{ VK_SNAPSHOT, NKCODE_SYSRQ },
		{ VK_SCROLL, NKCODE_SCROLL_LOCK },
		{ VK_OEM_1, NKCODE_SEMICOLON },
		{ VK_OEM_2, NKCODE_SLASH },
		{ VK_OEM_3, NKCODE_GRAVE },
		{ VK_OEM_4, NKCODE_LEFT_BRACKET },
		{ VK_OEM_5, NKCODE_BACKSLASH },
		{ VK_OEM_6, NKCODE_RIGHT_BRACKET },
		{ VK_OEM_7, NKCODE_APOSTROPHE },
		{ VK_RETURN, NKCODE_ENTER },
		{ VK_APPS, NKCODE_MENU }, // Context menu key, let's call this "menu".
		{ VK_PAUSE, NKCODE_BREAK },
		{ VK_F1, NKCODE_F1 },
		{ VK_F2, NKCODE_F2 },
		{ VK_F3, NKCODE_F3 },
		{ VK_F4, NKCODE_F4 },
		{ VK_F5, NKCODE_F5 },
		{ VK_F6, NKCODE_F6 },
		{ VK_F7, NKCODE_F7 },
		{ VK_F8, NKCODE_F8 },
		{ VK_F9, NKCODE_F9 },
		{ VK_F10, NKCODE_F10 },
		{ VK_F11, NKCODE_F11 },
		{ VK_F12, NKCODE_F12 },
		{ VK_OEM_102, NKCODE_EXT_PIPE },
		{ VK_LBUTTON, NKCODE_EXT_MOUSEBUTTON_1 },
		{ VK_RBUTTON, NKCODE_EXT_MOUSEBUTTON_2 },
		{ VK_MBUTTON, NKCODE_EXT_MOUSEBUTTON_3 },
		{ VK_XBUTTON1, NKCODE_EXT_MOUSEBUTTON_4 },
		{ VK_XBUTTON2, NKCODE_EXT_MOUSEBUTTON_5 },
	};

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
			WARN_LOG(Log::System, "Unable to register raw input devices: %s", GetLastErrorMsg().c_str());
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

	static InputKeyCode GetTrueVKey(const RAWKEYBOARD &kb) {
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

	void ProcessKeyboard(const RAWINPUT *raw, bool foreground) {
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
				auto keyDown = keyboardKeysDown.find(key.keyCode);
				if (keyDown != keyboardKeysDown.end())
					keyboardKeysDown.erase(keyDown);
			}
		}
	}

	LRESULT ProcessChar(HWND hWnd, WPARAM wParam, LPARAM lParam) {
		KeyInput key;
		key.unicodeChar = (int)wParam;  // Note that this is NOT a NKCODE but a Unicode character!
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

	void ProcessMouse(HWND hWnd, const RAWINPUT *raw, bool foreground) {
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

		NativeMouseDelta(raw->data.mouse.lLastX, raw->data.mouse.lLastY);

		static const int rawInputDownID[5] = {
			RI_MOUSE_LEFT_BUTTON_DOWN,
			RI_MOUSE_RIGHT_BUTTON_DOWN,
			RI_MOUSE_BUTTON_3_DOWN,
			RI_MOUSE_BUTTON_4_DOWN,
			RI_MOUSE_BUTTON_5_DOWN
		};
		static const int rawInputUpID[5] = {
			RI_MOUSE_LEFT_BUTTON_UP,
			RI_MOUSE_RIGHT_BUTTON_UP,
			RI_MOUSE_BUTTON_3_UP,
			RI_MOUSE_BUTTON_4_UP,
			RI_MOUSE_BUTTON_5_UP
		};
		static const int vkInputID[5] = {
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
		UINT dwSize = 0;
		GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
		if (!rawInputBuffer) {
			rawInputBuffer = malloc(dwSize);
			if (!rawInputBuffer)
				return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
			memset(rawInputBuffer, 0, dwSize);
			rawInputBufferSize = dwSize;
		}
		if (dwSize > rawInputBufferSize) {
			void *newBuf = realloc(rawInputBuffer, dwSize);
			if (!newBuf)
				return DefWindowProc(hWnd, WM_INPUT, wParam, lParam);
			rawInputBuffer = newBuf;
			rawInputBufferSize = dwSize;
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
		free(rawInputBuffer);
		rawInputBuffer = 0;
	}
};

#include "ext/imgui/imgui.h"
#include "Common/File/Path.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/System/Display.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"

#include "imgui_impl_platform.h"

static ImGuiMouseCursor g_cursor = ImGuiMouseCursor_Arrow;
Bounds g_imguiCentralNodeBounds;

void ImGui_ImplPlatform_KeyEvent(const KeyInput &key) {
	ImGuiIO &io = ImGui::GetIO();

	if (key.flags & KEY_DOWN) {
		// Specially handle scroll events and any other special keys.
		switch (key.keyCode) {
		case NKCODE_EXT_MOUSEWHEEL_UP:
			io.AddMouseWheelEvent(0, key.Delta() * 0.010f);
			break;
		case NKCODE_EXT_MOUSEWHEEL_DOWN:
			io.AddMouseWheelEvent(0, -key.Delta() * 0.010f);
			break;
		default:
		{
			ImGuiKey keyCode = KeyCodeToImGui(key.keyCode);
			if (keyCode != ImGuiKey_None) {
				io.AddKeyEvent(keyCode, true);
			}
			break;
		}
		}
	}
	if (key.flags & KEY_UP) {
		ImGuiKey keyCode = KeyCodeToImGui(key.keyCode);
		if (keyCode != ImGuiKey_None) {
			io.AddKeyEvent(keyCode, false);
		}
	}
	if (key.flags & KEY_CHAR) {
		const int unichar = key.unicodeChar;

		if (unichar >= 0x20) {
			// Insert it! (todo: do it with a string insert)
			char buf[16];
			buf[u8_wc_toutf8(buf, unichar)] = '\0';
			io.AddInputCharactersUTF8(buf);
		}
	}
}

void ImGui_ImplPlatform_TouchEvent(const TouchInput &touch) {
	ImGuiIO& io = ImGui::GetIO();

	// We use real pixels in the imgui, no DPI adjustment yet.
	float x = touch.x / g_display.dpi_scale_x;
	float y = touch.y / g_display.dpi_scale_y;

	if (touch.flags & TOUCH_MOVE) {
		io.AddMousePosEvent(x, y);
	}
	if (touch.flags & TOUCH_DOWN) {
		io.AddMousePosEvent(x, y);
		if (touch.flags & TOUCH_MOUSE) {
			if (touch.buttons & 1)
				io.AddMouseButtonEvent(0, true);
			if (touch.buttons & 2)
				io.AddMouseButtonEvent(1, true);
		} else {
			io.AddMouseButtonEvent(0, true);
		}
	}
	if (touch.flags & TOUCH_UP) {
		io.AddMousePosEvent(x, y);
		if (touch.flags & TOUCH_MOUSE) {
			if (touch.buttons & 1)
				io.AddMouseButtonEvent(0, false);
			if (touch.buttons & 2)
				io.AddMouseButtonEvent(1, false);
		} else {
			io.AddMouseButtonEvent(0, false);
		}
	}
}

void ImGui_ImplPlatform_Init(const Path &configPath) {
	static char path[1024];
	truncate_cpy(path, configPath.ToString());
	ImGui::GetIO().IniFilename = path;
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
}

void ImGui_ImplPlatform_AxisEvent(const AxisInput &axis) {
	// Ignore for now.
}

void ImGui_ImplPlatform_NewFrame() {
	static double lastTime = 0.0;
	if (lastTime == 0.0) {
		lastTime = time_now_d();
	}

	double now = time_now_d();

	g_cursor = ImGui::GetMouseCursor();
	ImGuiIO &io = ImGui::GetIO();
	io.DisplaySize = ImVec2(g_display.pixel_xres, g_display.pixel_yres);
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	io.DeltaTime = now - lastTime;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	lastTime = now;
}

ImGuiMouseCursor ImGui_ImplPlatform_GetCursor() {
	return g_cursor;
}

// Written by chatgpt
ImGuiKey KeyCodeToImGui(InputKeyCode keyCode) {
	switch (keyCode) {
	case NKCODE_DPAD_UP: return ImGuiKey_UpArrow;
	case NKCODE_DPAD_DOWN: return ImGuiKey_DownArrow;
	case NKCODE_DPAD_LEFT: return ImGuiKey_LeftArrow;
	case NKCODE_DPAD_RIGHT: return ImGuiKey_RightArrow;
	case NKCODE_ENTER: return ImGuiKey_Enter;
	case NKCODE_ESCAPE: return ImGuiKey_Escape;
	case NKCODE_SHIFT_LEFT: return ImGuiKey_LeftShift;
	case NKCODE_SHIFT_RIGHT: return ImGuiKey_RightShift;
	case NKCODE_ALT_LEFT: return ImGuiKey_LeftAlt;
	case NKCODE_ALT_RIGHT: return ImGuiKey_RightAlt;
	case NKCODE_CTRL_LEFT: return ImGuiKey_LeftCtrl;
	case NKCODE_CTRL_RIGHT: return ImGuiKey_RightCtrl;
	case NKCODE_TAB: return ImGuiKey_Tab;
	case NKCODE_MENU: return ImGuiKey_Menu;
	case NKCODE_DEL: return ImGuiKey_Backspace;
	case NKCODE_FORWARD_DEL: return ImGuiKey_Delete;
	case NKCODE_CAPS_LOCK: return ImGuiKey_CapsLock;
	case NKCODE_SPACE: return ImGuiKey_Space;
	case NKCODE_PAGE_UP: return ImGuiKey_PageUp;
	case NKCODE_PAGE_DOWN: return ImGuiKey_PageDown;
	case NKCODE_MOVE_HOME: return ImGuiKey_Home;
	case NKCODE_MOVE_END: return ImGuiKey_End;
	case NKCODE_INSERT: return ImGuiKey_Insert;

	// Numeric keys
	case NKCODE_0: return ImGuiKey_0;
	case NKCODE_1: return ImGuiKey_1;
	case NKCODE_2: return ImGuiKey_2;
	case NKCODE_3: return ImGuiKey_3;
	case NKCODE_4: return ImGuiKey_4;
	case NKCODE_5: return ImGuiKey_5;
	case NKCODE_6: return ImGuiKey_6;
	case NKCODE_7: return ImGuiKey_7;
	case NKCODE_8: return ImGuiKey_8;
	case NKCODE_9: return ImGuiKey_9;

	// Letter keys
	case NKCODE_A: return ImGuiKey_A;
	case NKCODE_B: return ImGuiKey_B;
	case NKCODE_C: return ImGuiKey_C;
	case NKCODE_D: return ImGuiKey_D;
	case NKCODE_E: return ImGuiKey_E;
	case NKCODE_F: return ImGuiKey_F;
	case NKCODE_G: return ImGuiKey_G;
	case NKCODE_H: return ImGuiKey_H;
	case NKCODE_I: return ImGuiKey_I;
	case NKCODE_J: return ImGuiKey_J;
	case NKCODE_K: return ImGuiKey_K;
	case NKCODE_L: return ImGuiKey_L;
	case NKCODE_M: return ImGuiKey_M;
	case NKCODE_N: return ImGuiKey_N;
	case NKCODE_O: return ImGuiKey_O;
	case NKCODE_P: return ImGuiKey_P;
	case NKCODE_Q: return ImGuiKey_Q;
	case NKCODE_R: return ImGuiKey_R;
	case NKCODE_S: return ImGuiKey_S;
	case NKCODE_T: return ImGuiKey_T;
	case NKCODE_U: return ImGuiKey_U;
	case NKCODE_V: return ImGuiKey_V;
	case NKCODE_W: return ImGuiKey_W;
	case NKCODE_X: return ImGuiKey_X;
	case NKCODE_Y: return ImGuiKey_Y;
	case NKCODE_Z: return ImGuiKey_Z;

	// Symbols
	case NKCODE_COMMA: return ImGuiKey_Comma;
	case NKCODE_PERIOD: return ImGuiKey_Period;
	case NKCODE_MINUS: return ImGuiKey_Minus;
	case NKCODE_PLUS: return ImGuiKey_Equal;  // Hmm
	case NKCODE_EQUALS: return ImGuiKey_Equal;
	case NKCODE_LEFT_BRACKET: return ImGuiKey_LeftBracket;
	case NKCODE_RIGHT_BRACKET: return ImGuiKey_RightBracket;
	case NKCODE_BACKSLASH: return ImGuiKey_Backslash;
	case NKCODE_SEMICOLON: return ImGuiKey_Semicolon;
	case NKCODE_APOSTROPHE: return ImGuiKey_Apostrophe;
	case NKCODE_SLASH: return ImGuiKey_Slash;
	case NKCODE_GRAVE: return ImGuiKey_GraveAccent;

	// Function keys
	case NKCODE_F1: return ImGuiKey_F1;
	case NKCODE_F2: return ImGuiKey_F2;
	case NKCODE_F3: return ImGuiKey_F3;
	case NKCODE_F4: return ImGuiKey_F4;
	case NKCODE_F5: return ImGuiKey_F5;
	case NKCODE_F6: return ImGuiKey_F6;
	case NKCODE_F7: return ImGuiKey_F7;
	case NKCODE_F8: return ImGuiKey_F8;
	case NKCODE_F9: return ImGuiKey_F9;
	case NKCODE_F10: return ImGuiKey_F10;
	case NKCODE_F11: return ImGuiKey_F11;
	case NKCODE_F12: return ImGuiKey_F12;

	// Keypad
	case NKCODE_NUMPAD_0: return ImGuiKey_Keypad0;
	case NKCODE_NUMPAD_1: return ImGuiKey_Keypad1;
	case NKCODE_NUMPAD_2: return ImGuiKey_Keypad2;
	case NKCODE_NUMPAD_3: return ImGuiKey_Keypad3;
	case NKCODE_NUMPAD_4: return ImGuiKey_Keypad4;
	case NKCODE_NUMPAD_5: return ImGuiKey_Keypad5;
	case NKCODE_NUMPAD_6: return ImGuiKey_Keypad6;
	case NKCODE_NUMPAD_7: return ImGuiKey_Keypad7;
	case NKCODE_NUMPAD_8: return ImGuiKey_Keypad8;
	case NKCODE_NUMPAD_9: return ImGuiKey_Keypad9;
	case NKCODE_NUMPAD_DIVIDE: return ImGuiKey_KeypadDivide;
	case NKCODE_NUMPAD_MULTIPLY: return ImGuiKey_KeypadMultiply;
	case NKCODE_NUMPAD_SUBTRACT: return ImGuiKey_KeypadSubtract;
	case NKCODE_NUMPAD_ADD: return ImGuiKey_KeypadAdd;
	case NKCODE_NUMPAD_ENTER: return ImGuiKey_KeypadEnter;
	case NKCODE_NUMPAD_EQUALS: return ImGuiKey_KeypadEqual;

	// Lock keys
	case NKCODE_NUM_LOCK: return ImGuiKey_NumLock;
	case NKCODE_SCROLL_LOCK: return ImGuiKey_ScrollLock;

	case NKCODE_EXT_MOUSEBUTTON_1:
	case NKCODE_EXT_MOUSEBUTTON_2:
	case NKCODE_EXT_MOUSEBUTTON_3:
	case NKCODE_EXT_MOUSEBUTTON_4:
	case NKCODE_EXT_MOUSEBUTTON_5:
	case NKCODE_EXT_MOUSEWHEEL_DOWN:
	case NKCODE_EXT_MOUSEWHEEL_UP:
		// Keys ignored for imgui
	 	return ImGuiKey_None;
	case NKCODE_PRINTSCREEN:
		return ImGuiKey_PrintScreen;

	case NKCODE_EXT_PIPE:
		// No valid mapping exists.
		return ImGuiKey_None;

	default:
		WARN_LOG(Log::System, "Unmapped ImGui keycode conversion from %d", keyCode);
	 	return ImGuiKey_None;
	}
}

#include "input/input_state.h"
#include "ControlMapping.h"
#include "Windows/WndMainWindow.h"
#include "KeyboardDevice.h"
#include "../Common/CommonTypes.h"
#include "../Core/HLE/sceCtrl.h"
#include "WinUser.h"

unsigned int key_pad_map[] = {
	VK_ESCAPE,PAD_BUTTON_MENU,        // Open PauseScreen
	VK_BACK,  PAD_BUTTON_BACK,        // Toggle PauseScreen & Back Setting Page
	'Z',      PAD_BUTTON_A,
	'X',      PAD_BUTTON_B,
	'A',      PAD_BUTTON_X,
	'S',      PAD_BUTTON_Y,
	'V',      PAD_BUTTON_SELECT,
	VK_SPACE, PAD_BUTTON_START,
	'Q',      PAD_BUTTON_LBUMPER,
	'W',      PAD_BUTTON_RBUMPER,
	VK_F3,    PAD_BUTTON_LEFT_THUMB,  // Toggle Turbo
	VK_PAUSE, PAD_BUTTON_RIGHT_THUMB, // Open PauseScreen
	VK_UP,    PAD_BUTTON_UP,
	VK_DOWN,  PAD_BUTTON_DOWN,
	VK_LEFT,  PAD_BUTTON_LEFT,
	VK_RIGHT, PAD_BUTTON_RIGHT,
};
const unsigned int key_pad_map_size = sizeof(key_pad_map);

unsigned short analog_ctrl_map[] = {
	'I', CTRL_UP,
	'K', CTRL_DOWN,
	'J', CTRL_LEFT,
	'L', CTRL_RIGHT,
};
const unsigned int analog_ctrl_map_size = sizeof(analog_ctrl_map);

int KeyboardDevice::UpdateState(InputState &input_state) {
	if (MainWindow::GetHWND() != GetForegroundWindow()) return -1;
	bool alternate = GetAsyncKeyState(VK_SHIFT) != 0;
	static u32 alternator = 0;
	bool doAlternate = alternate && (alternator++ % 10) < 5;

	// This button isn't customizable.  Also, if alt is held, we ignore it (alt-tab is common.)
	if (GetAsyncKeyState(VK_TAB) && !GetAsyncKeyState(VK_MENU)) {
		input_state.pad_buttons |= PAD_BUTTON_UNTHROTTLE;
	}

	for (int i = 0; i < sizeof(key_pad_map)/sizeof(key_pad_map[0]); i += 2) {
		if (!GetAsyncKeyState(key_pad_map[i])) {
			continue;
		}
		if (!doAlternate || key_pad_map[i + 1] > PAD_BUTTON_SELECT) {
			input_state.pad_buttons |= key_pad_map[i+1];
		}
	}

	float analogX = 0;
	float analogY = 0;
	for (int i = 0; i < sizeof(analog_ctrl_map)/sizeof(analog_ctrl_map[0]); i += 2) {
		if (!GetAsyncKeyState(analog_ctrl_map[i])) {
			continue;
		}

		switch (analog_ctrl_map[i + 1]) {
		case CTRL_UP:
			analogY += 1.0f;
			break;
		case CTRL_DOWN:
			analogY -= 1.0f;
			break;
		case CTRL_LEFT:
			analogX -= 1.0f;
			break;
		case CTRL_RIGHT:
			analogX += 1.0f;
			break;
		}
	}
	
	// keyboard device
	input_state.pad_lstick_x += analogX;
	input_state.pad_lstick_y += analogY;
	return 0;
}

struct key_name {
	unsigned char key;
	char name[10];
};

const key_name key_name_map[] = {
	{'A', "A"},
	{'B', "B"},
	{'C', "C"},
	{'D', "D"},
	{'E', "E"},
	{'F', "F"},
	{'G', "G"},
	{'H', "H"},
	{'I', "I"},
	{'J', "J"},
	{'K', "K"},
	{'L', "L"},
	{'M', "M"},
	{'N', "N"},
	{'O', "O"},
	{'P', "P"},
	{'Q', "Q"},
	{'R', "R"},
	{'S', "S"},
	{'T', "T"},
	{'U', "U"},
	{'V', "V"},
	{'W', "W"},
	{'X', "X"},
	{'Y', "Y"},
	{'Z', "Z"},
	{'1', "1"},
	{'2', "2"},
	{'3', "3"},
	{'4', "4"},
	{'5', "5"},
	{'6', "6"},
	{'7', "7"},
	{'8', "8"},
	{'9', "9"},
	{'0', "0"},
	{VK_BACK, "Backspace"},
	{VK_TAB, "Tab"},
	{VK_CLEAR, "Clear"},
	{VK_RETURN, "Return"},
	{VK_SHIFT, "Shift"},
	{VK_CONTROL, "Ctrl"},
	{VK_MENU, "Alt"},
	{VK_PAUSE, "Pause"},
	{VK_CAPITAL, "Caps"},
	{VK_ESCAPE, "Esc"},
	{VK_SPACE, "Space"},
	{VK_PRIOR, "PgUp"},
	{VK_NEXT, "PgDown"},
	{VK_END, "End"},
	{VK_HOME, "Home"},
	{VK_LEFT, "Left"},
	{VK_UP, "Up"},
	{VK_RIGHT, "Right"},
	{VK_DOWN, "Down"},
	{VK_SELECT, "Select"},
	{VK_INSERT, "Insert"},
	{VK_DELETE, "Delete"},
	{VK_HELP, "Help"},
	{VK_NUMPAD0, "Num 0"},
	{VK_NUMPAD1, "Num 1"},
	{VK_NUMPAD2, "Num 2"},
	{VK_NUMPAD3, "Num 3"},
	{VK_NUMPAD4, "Num 4"},
	{VK_NUMPAD5, "Num 5"},
	{VK_NUMPAD6, "Num 6"},
	{VK_NUMPAD7, "Num 7"},
	{VK_NUMPAD8, "Num 8"},
	{VK_NUMPAD9, "Num 9"},
	{VK_MULTIPLY, "Num *"},
	{VK_ADD, "Num +"},
	{VK_SEPARATOR, "Num Sep"},
	{VK_SUBTRACT, "Num -"},
	{VK_DECIMAL, "Num ."},
	{VK_DIVIDE, "Num /"},
	{VK_F1, "F1"},
	{VK_F2, "F2"},
	{VK_F3, "F3"},
	{VK_F4, "F4"},
	{VK_F5, "F5"},
	{VK_F6, "F6"},
	{VK_F7, "F7"},
	{VK_F8, "F8"},
	{VK_F9, "F9"},
	{VK_F10, "F10"},
	{VK_F11, "F11"},
	{VK_F12, "F12"},
	{VK_OEM_NEC_EQUAL, "Num ="},
	{VK_OEM_1, ";"},
	{VK_OEM_PLUS, "+"},
	{VK_OEM_COMMA, ","},
	{VK_OEM_MINUS, "-"},
	{VK_OEM_PERIOD, "."},
	{VK_OEM_2, "?"},
	{VK_OEM_3, "~"},
	{VK_OEM_4, "["},
	{VK_OEM_5, "|"},
	{VK_OEM_6, "]"},
	{VK_OEM_7, "'"}
};

const int key_name_map_size = sizeof(key_name_map) / sizeof(key_name);

const char * getVirtualKeyName(unsigned char key)
{
	for (int i = 0; i < key_name_map_size; i++) {
		if (key_name_map[i].key == key)
			return key_name_map[i].name;
	}
	return 0;
}


#include "KeyboardDevice.h"
#include "../Common/CommonTypes.h"
#include "../Core/HLE/sceCtrl.h"
#include "WinUser.h"

static const unsigned short key_ctrl_map[] = {
	VK_SPACE, CTRL_START,
	'V',      CTRL_SELECT,
	'A',      CTRL_SQUARE,
	'S',      CTRL_TRIANGLE,
	'X',      CTRL_CIRCLE,
	'Z',      CTRL_CROSS,
	'Q',      CTRL_LTRIGGER,
	'W',      CTRL_RTRIGGER,
	VK_UP,    CTRL_UP,
	VK_DOWN,  CTRL_DOWN,
	VK_LEFT,  CTRL_LEFT,
	VK_RIGHT, CTRL_RIGHT,
};

static const unsigned short analog_ctrl_map[] = {
	'I', CTRL_UP,
	'K', CTRL_DOWN,
	'J', CTRL_LEFT,
	'L', CTRL_RIGHT,
};

int KeyboardDevice::UpdateState() {
	bool alternate = GetAsyncKeyState(VK_SHIFT) != 0;
	static u32 alternator = 0;
	bool doAlternate = alternate && (alternator++ % 10) < 5;

	for (int i = 0; i < sizeof(key_ctrl_map)/sizeof(key_ctrl_map[0]); i += 2) {
		if (!GetAsyncKeyState(key_ctrl_map[i]) || doAlternate)
			__CtrlButtonUp(key_ctrl_map[i+1]);
		else {
			__CtrlButtonDown(key_ctrl_map[i+1]);
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

	__CtrlSetAnalog(analogX, analogY);
	return 0;
}
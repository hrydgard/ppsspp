#include "input_state.h"
#include <iostream>

const char *GetDeviceName(int deviceId) {
	switch (deviceId) {
	case DEVICE_ID_DEFAULT: return "built-in";
	case DEVICE_ID_KEYBOARD: return "kbd";
	case DEVICE_ID_PAD_0: return "pad";
	case DEVICE_ID_X360_0: return "x360";
	case DEVICE_ID_ACCELEROMETER: return "accelerometer";
	default:
		return "unknown";
	}
}

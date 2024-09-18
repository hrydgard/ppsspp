#pragma once

// For more detailed and configurable input, implement NativeTouch, NativeKey and NativeAxis and do your
// own mapping. Might later move the mapping system from PPSSPP to native.

#include <unordered_map>
#include <vector>
#include <string>

#include "Common/Input/KeyCodes.h"
#include "Common/Log.h"

// Default device IDs

enum InputDeviceID {
	DEVICE_ID_ANY = -1,  // Represents any device ID
	DEVICE_ID_DEFAULT = 0,  // Old Android
	DEVICE_ID_KEYBOARD = 1,  // PC keyboard, android keyboards
	DEVICE_ID_MOUSE = 2,  // PC mouse only (not touchscreen!)
	DEVICE_ID_PAD_0 = 10,  // Generic joypads
	DEVICE_ID_PAD_1 = 11,  // these should stay as contiguous numbers
	DEVICE_ID_PAD_2 = 12,
	DEVICE_ID_PAD_3 = 13,
	DEVICE_ID_PAD_4 = 14,
	DEVICE_ID_PAD_5 = 15,
	DEVICE_ID_PAD_6 = 16,
	DEVICE_ID_PAD_7 = 17,
	DEVICE_ID_PAD_8 = 18,
	DEVICE_ID_PAD_9 = 19,
	DEVICE_ID_XINPUT_0 = 20,  // XInput joypads
	DEVICE_ID_XINPUT_1 = 21,
	DEVICE_ID_XINPUT_2 = 22,
	DEVICE_ID_XINPUT_3 = 23,
	DEVICE_ID_ACCELEROMETER = 30,  // no longer used
	DEVICE_ID_XR_CONTROLLER_LEFT = 40,
	DEVICE_ID_XR_CONTROLLER_RIGHT = 41,
	DEVICE_ID_TOUCH = 42,
	DEVICE_ID_COUNT,
};

inline InputDeviceID operator +(InputDeviceID deviceID, int addend) {
	return (InputDeviceID)((int)deviceID + addend);
}

//number of contiguous generic joypad IDs
const int MAX_NUM_PADS = 10;

const char *GetDeviceName(int deviceId);

#ifndef MAX_KEYQUEUESIZE
#define MAX_KEYQUEUESIZE 20
#endif

// Represents a single bindable key
static const int AXIS_BIND_NKCODE_START = 4000;

inline int TranslateKeyCodeToAxis(int keyCode, int *direction) {
	if (keyCode < AXIS_BIND_NKCODE_START)
		return 0;
	int k = keyCode - AXIS_BIND_NKCODE_START;
	// Even/odd for direction.
	if (direction)
		*direction = k & 1 ? -1 : 1;
	return k / 2;
}

class InputMapping {
private:
	static inline int TranslateKeyCodeFromAxis(int axisId, int direction) {
		return AXIS_BIND_NKCODE_START + axisId * 2 + (direction < 0 ? 1 : 0);
	}
public:
	InputMapping() : deviceId(DEVICE_ID_DEFAULT), keyCode(0) {}
	// From a key mapping
	InputMapping(InputDeviceID _deviceId, int key) : deviceId(_deviceId), keyCode(key) {}
	// From an axis
	InputMapping(InputDeviceID _deviceId, int axis, int direction) : deviceId(_deviceId), keyCode(TranslateKeyCodeFromAxis(axis, direction)) {
		_dbg_assert_(direction != 0);
	}

	static InputMapping FromConfigString(std::string_view str);
	std::string ToConfigString() const;

	InputDeviceID deviceId;
	int keyCode;  // Can also represent an axis with direction, if encoded properly.

	bool IsAxis() const {
		return keyCode >= AXIS_BIND_NKCODE_START;
	}

	int Axis(int *direction) const {
		_dbg_assert_(IsAxis());
		return TranslateKeyCodeToAxis(keyCode, direction);
	}

	InputMapping FlipDirection() const {
		_dbg_assert_(IsAxis());
		InputMapping other = *this;
		other.keyCode ^= 1;
		return other;
	}

	// If you want to use std::find and match ANY, you need to perform an explicit search for that.
	bool operator < (const InputMapping &other) const {
		if (deviceId < other.deviceId) return true;
		if (deviceId > other.deviceId) return false;
		if (keyCode < other.keyCode) return true;
		return false;
	}
	// Needed for composition.
	bool operator > (const InputMapping &other) const {
		if (deviceId > other.deviceId) return true;
		if (deviceId < other.deviceId) return false;
		if (keyCode > other.keyCode) return true;
		return false;
	}

	// This one is iffy with the != ANY checks. Should probably be a named method.
	bool operator == (const InputMapping &other) const {
		if (deviceId != other.deviceId && deviceId != DEVICE_ID_ANY && other.deviceId != DEVICE_ID_ANY) return false;
		if (keyCode != other.keyCode) return false;
		return true;
	}
	bool operator != (const InputMapping &other) const {
		return !(*this == other);
	}

	void FormatDebug(char *buffer, size_t bufSize) const;
};

enum {
	TOUCH_MOVE = 1 << 0,
	TOUCH_DOWN = 1 << 1,
	TOUCH_UP = 1 << 2,
	TOUCH_CANCEL = 1 << 3,  // Sent by scrollviews to their children when they detect a scroll
	TOUCH_WHEEL = 1 << 4,  // Scrollwheel event. Usually only affects Y but can potentially affect X.
	TOUCH_MOUSE = 1 << 5,  // Identifies that this touch event came from a mouse
	TOUCH_RELEASE_ALL = 1 << 6,  // Useful for app focus switches when events may be lost.

	// These are the Android getToolType() codes, shifted by 10.
	TOUCH_TOOL_MASK = 7 << 10,
	TOUCH_TOOL_UNKNOWN = 0 << 10,
	TOUCH_TOOL_FINGER = 1 << 10,
	TOUCH_TOOL_STYLUS = 2 << 10,
	TOUCH_TOOL_MOUSE = 3 << 10,
	TOUCH_TOOL_ERASER = 4 << 10,
};

// Used for asynchronous touch input.
// DOWN is always on its own. 
// MOVE and UP can be combined.
struct TouchInput {
	float x;
	float y;
	int id; // Needs to be <= GestureDetector::MAX_PTRS (10.)
	int flags;
	double timestamp;
};

#undef KEY_DOWN
#undef KEY_UP

enum {
	KEY_DOWN = 1 << 0,
	KEY_UP = 1 << 1,
	KEY_HASWHEELDELTA = 1 << 2,
	KEY_IS_REPEAT = 1 << 3,
	KEY_CHAR = 1 << 4,  // Unicode character input. Cannot detect keyups of these so KEY_DOWN and KEY_UP are zero when this is set.
};

struct KeyInput {
	KeyInput() {}
	KeyInput(InputDeviceID devId, InputKeyCode code, int fl) : deviceId(devId), keyCode(code), flags(fl) {}
	KeyInput(InputDeviceID devId, int unicode) : deviceId(devId), unicodeChar(unicode), flags(KEY_CHAR) {}
	InputDeviceID deviceId;
	union {
		InputKeyCode keyCode;  // Android keycodes are the canonical keycodes, everyone else map to them.
		int unicodeChar;  // for KEY_CHAR
	};
	int flags;
};

struct AxisInput {
	InputDeviceID deviceId;
	InputAxis axisId;
	float value;
};

// Is there a nicer place for this stuff? It's here to avoid dozens of linking errors in UnitTest..
extern std::vector<InputMapping> dpadKeys;
extern std::vector<InputMapping> confirmKeys;
extern std::vector<InputMapping> cancelKeys;
extern std::vector<InputMapping> infoKeys;
extern std::vector<InputMapping> tabLeftKeys;
extern std::vector<InputMapping> tabRightKeys;
void SetDPadKeys(const std::vector<InputMapping> &leftKey, const std::vector<InputMapping> &rightKey,
		const std::vector<InputMapping> &upKey, const std::vector<InputMapping> &downKey);
void SetConfirmCancelKeys(const std::vector<InputMapping> &confirm, const std::vector<InputMapping> &cancel);
void SetTabLeftRightKeys(const std::vector<InputMapping> &tabLeft, const std::vector<InputMapping> &tabRight);
void SetInfoKeys(const std::vector<InputMapping> &info);

// 0 means unknown (attempt autodetect), -1 means flip, 1 means original direction.
void SetAnalogFlipY(const std::unordered_map<InputDeviceID, int> &flipYByDeviceId);
int GetAnalogYDirection(InputDeviceID deviceId);

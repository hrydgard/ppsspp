#pragma once

// InputState is the simple way of getting input. All the input we have is collected
// to a canonical Xbox360-style pad fully automatically.
//
// Recommended for use in game UIs and games that don't have advanced needs.
//
// For more detailed and configurable input, implement NativeTouch, NativeKey and NativeAxis and do your
// own mapping. Might later move the mapping system from PPSSPP to native.

#include "math/lin/vec3.h"
#include "base/mutex.h"
#include "base/basictypes.h"
#include "input/keycodes.h"
#include <map>
#include <vector>

// Default device IDs

enum {
	DEVICE_ID_DEFAULT = 0,  // Old Android
	DEVICE_ID_KEYBOARD = 1,  // PC keyboard, android keyboards
	DEVICE_ID_MOUSE = 2,  // PC mouse only (not touchscreen!)
	DEVICE_ID_PAD_0 = 10,  // Generic joypads
	DEVICE_ID_X360_0 = 20,  // XInput joypads
	DEVICE_ID_ACCELEROMETER = 30,
};

const char *GetDeviceName(int deviceId);

enum {
	PAD_BUTTON_A = 1,
	PAD_BUTTON_B = 2,
	PAD_BUTTON_X = 4,
	PAD_BUTTON_Y = 8,
	PAD_BUTTON_LBUMPER = 16,
	PAD_BUTTON_RBUMPER = 32,
	PAD_BUTTON_START = 64,
	PAD_BUTTON_SELECT = 128,
	PAD_BUTTON_UP = 256,
	PAD_BUTTON_DOWN = 512,
	PAD_BUTTON_LEFT = 1024,
	PAD_BUTTON_RIGHT = 2048,

	PAD_BUTTON_MENU = 4096,
	PAD_BUTTON_BACK = 8192,

	// For Blackberry and Qt
	PAD_BUTTON_JOY_UP = 1<<14,
	PAD_BUTTON_JOY_DOWN = 1<<15,
	PAD_BUTTON_JOY_LEFT = 1<<16,
	PAD_BUTTON_JOY_RIGHT = 1<<17,

	PAD_BUTTON_LEFT_THUMB = 1 << 18,   // Click left thumb stick on X360
	PAD_BUTTON_RIGHT_THUMB = 1 << 19,   // Click right thumb stick on X360

	PAD_BUTTON_LEFT_TRIGGER = 1 << 21,   // Click left thumb stick on X360
	PAD_BUTTON_RIGHT_TRIGGER = 1 << 22,   // Click left thumb stick on X360

	PAD_BUTTON_UNTHROTTLE = 1 << 20, // Click Tab to unthrottle
};

#define MAX_POINTERS 10

#ifndef MAX_KEYQUEUESIZE
#define MAX_KEYQUEUESIZE 20
#endif

// Collection of all possible inputs, and automatically computed
// deltas where applicable.
struct InputState {
	// Lock this whenever you access the data in this struct.
	mutable recursive_mutex lock;
	InputState()
		: pad_buttons(0),
			pad_last_buttons(0),
			pad_buttons_down(0),
			pad_buttons_up(0),
			mouse_valid(false),
			accelerometer_valid(false) {
		memset(pointer_down, 0, sizeof(pointer_down));
	}

	// Gamepad style input. For ease of use.
	int pad_buttons; // bitfield
	int pad_last_buttons;
	int pad_buttons_down;	// buttons just pressed this frame
	int pad_buttons_up;	// buttons just pressed last frame
	float pad_lstick_x;
	float pad_lstick_y;
	float pad_rstick_x;
	float pad_rstick_y;
	float pad_ltrigger;
	float pad_rtrigger;

	// Mouse/touch style input
	// There are up to 8 mice / fingers.
	volatile bool mouse_valid;

	int pointer_x[MAX_POINTERS];
	int pointer_y[MAX_POINTERS];
	bool pointer_down[MAX_POINTERS];

	// Accelerometer
	bool accelerometer_valid;
	Vec3 acc;

private:
	DISALLOW_COPY_AND_ASSIGN(InputState);
};

void UpdateInputState(InputState *input, bool merge = false);
void EndInputState(InputState *input);

enum {
	TOUCH_MOVE = 1,
	TOUCH_DOWN = 2,
	TOUCH_UP = 4,
	TOUCH_CANCEL = 8,  // Sent by scrollviews to their children when they detect a scroll
	TOUCH_WHEEL = 16,  // Scrollwheel event. Usually only affects Y.
};

// Used for asynchronous touch input.
// DOWN is always on its own. 
// MOVE and UP can be combined.
struct TouchInput {
	float x;
	float y;
	int id;  // can be relied upon to be 0...MAX_POINTERS
	int flags;
	double timestamp;
};

#undef KEY_DOWN
#undef KEY_UP

enum {
	KEY_DOWN = 1,
	KEY_UP = 2,
	KEY_HASWHEELDELTA = 4,
};

struct KeyInput {
	KeyInput() {}
	KeyInput(int devId, int code, int fl) : deviceId(devId), keyCode(code), flags(fl) {}
	int deviceId;
	int keyCode;  // Android keycodes are the canonical keycodes, everyone else map to them.
	int flags;
};

struct AxisInput {
	int deviceId;
	int axisId;  // Android axis Ids are the canonical ones.
	float value;
	int flags;
};


class ButtonTracker {
public:
	ButtonTracker() { Reset(); }
	void Reset() { 
		pad_buttons_ = 0;
		pad_buttons_async_set = 0;
		pad_buttons_async_clear = 0;
	}
	void Process(const KeyInput &input);
	uint32_t Update();
	uint32_t GetPadButtons() const { return pad_buttons_; }

private:
	uint32_t pad_buttons_;
	uint32_t pad_buttons_async_set;
	uint32_t pad_buttons_async_clear;
};

// Platforms should call g_buttonTracker.Process().
extern ButtonTracker g_buttonTracker;

// Is there a nicer place for this stuff? It's here to avoid dozens of linking errors in UnitTest..
extern std::vector<keycode_t> confirmKeys;
extern std::vector<keycode_t> cancelKeys;
void SetConfirmCancelKeys(std::vector<keycode_t> confirm, std::vector<keycode_t> cancel);

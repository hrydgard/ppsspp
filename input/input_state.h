#pragma once

#include "math/lin/vec3.h"
#include "base/mutex.h"
#include "base/basictypes.h"

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

#ifndef MAX_POINTERS
#define MAX_POINTERS 8
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

	// Gamepad style input
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

	// TODO: Add key arrays

private:
	DISALLOW_COPY_AND_ASSIGN(InputState);
};

inline void UpdateInputState(InputState *input) {
	input->pad_buttons_down = (input->pad_last_buttons ^ input->pad_buttons) & input->pad_buttons;
	input->pad_buttons_up = (input->pad_last_buttons ^ input->pad_buttons) & input->pad_last_buttons;
}

inline void EndInputState(InputState *input) {
	input->pad_last_buttons = input->pad_buttons;
}

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


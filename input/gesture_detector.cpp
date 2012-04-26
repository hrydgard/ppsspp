// Unfinished.
// TODO:
// Zoom gesture a la http://www.zdnet.com/blog/burnette/how-to-use-multi-touch-in-android-2-part-6-implementing-the-pinch-zoom-gesture/1847

#include "input/gesture_detector.h"

namespace GestureDetector {

struct Finger {
	bool down;
	float X;
	float Y;
	float lastX;
	float lastY;
	float downX;
	float downY;
	float deltaX;
	float deltaY;
	float smoothDeltaX;
	float smoothDeltaY;
};

// State
#define MAX_FINGERS 4

static Finger fingers[MAX_FINGERS];

void update(const InputState &state) {
	// Mouse / 1-finger-touch control.
	if (state.mouse_down[0]) {
		fingers[0].down = true;
		fingers[0].downX = state.mouse_x[0];
		fingers[0].downY = state.mouse_y[0];
	} else {
		fingers[0].down = false;
	}

	fingers[0].lastX = fingers[0].X;
	fingers[0].lastY = fingers[0].Y;

	// TODO: real multitouch
}

bool down(int i, float *xdelta, float *ydelta) {
	if (!fingers[i].down) {
		return false;
	}
	*xdelta = fingers[i].downX;
	*ydelta = fingers[i].downY;
}

bool dragDistance(int i, float *xdelta, float *ydelta) {
	if (!fingers[i].down)
		return false;

	*xdelta = fingers[i].X - fingers[i].downX;
	*ydelta = fingers[i].Y - fingers[i].downY;
}

bool dragDelta(int i, float *xdelta, float *ydelta) {
	if (!fingers[i].down)
		return false;

	*xdelta = fingers[i].X - fingers[i].lastX;
	*ydelta = fingers[i].Y - fingers[i].lastY;
}

}

#include "input/input_state.h"

// WIP - doesn't do much yet
// Mainly for detecting (multi-)touch gestures but also useable for left button mouse dragging etc.


enum Gesture {
	GESTURE_DRAG_VERTICAL = 1,
	GESTURE_DRAG_HORIZONTAL = 2,
	GESTURE_TWO_FINGER_ZOOM = 4,
	GESTURE_TWO_FINGER_ZOOM_ROTATE = 4,
};

// May track multiple gestures at the same time. You simply call GetGestureInfo
// with the gesture you are interested in.
class GestureDetector
{
public:
	void Update(const TouchInput &touch);
	bool IsGestureActive(Gesture gesture) const;
	void GetGestureInfo(Gesture gesture, float info[4]);

private:
	// jazzhands!
	enum Locals {
		MAX_PTRS = 10 
	};

	struct Pointer {
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

	Pointer pointers[MAX_PTRS];
	// ...
};

#pragma once

#include <cstdint>
#include "Common/Input/InputState.h"
#include "Common/Math/geom2d.h"

// Mainly for detecting (multi-)touch gestures but also useable for left button mouse dragging etc.
// Currently only supports simple scroll-drags with inertia.
// TODO: Two-finger zoom/rotate etc.

enum Gesture {
	GESTURE_DRAG_VERTICAL = 1,
	GESTURE_DRAG_HORIZONTAL = 2,
	GESTURE_TWO_FINGER_ZOOM = 4,
	GESTURE_TWO_FINGER_ZOOM_ROTATE = 8,
};

// May track multiple gestures at the same time. You simply call GetGestureInfo
// with the gesture you are interested in.
class GestureDetector {
public:
	GestureDetector();
	TouchInput Update(const TouchInput &touch, const Bounds &bounds);
	void UpdateFrame();
	bool IsGestureActive(Gesture gesture, int touchId) const;
	bool GetGestureInfo(Gesture gesture, int touchId, float info[4]) const;

private:
	enum Locals {
		MAX_PTRS = 10,
	};

	struct Pointer {
		bool down;
		double downTime;
		float lastX;
		float lastY;
		float downX;
		float downY;
		float deltaX;
		float deltaY;
		float distanceX;
		float distanceY;
		float estimatedInertiaX;
		float estimatedInertiaY;

		uint32_t active;
	};

	Pointer pointers[MAX_PTRS]{};
};

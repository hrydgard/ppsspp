// Unfinished.
// TODO:
// Zoom gesture a la http://www.zdnet.com/blog/burnette/how-to-use-multi-touch-in-android-2-part-6-implementing-the-pinch-zoom-gesture/1847

#include <cstring>

#include "Common/TimeUtil.h"
#include "Common/Input/GestureDetector.h"

const float estimatedInertiaDamping = 0.75f;

GestureDetector::GestureDetector() {
	memset(pointers, 0, sizeof(pointers));
}

TouchInput GestureDetector::Update(const TouchInput &touch, const Bounds &bounds) {
	if (touch.id < 0 || touch.id >= MAX_PTRS) {
		return touch;
	}
	// Mouse / 1-finger-touch control.
	Pointer &p = pointers[touch.id];
	if ((touch.flags & TOUCH_DOWN) && bounds.Contains(touch.x, touch.y)) {
		p.down = true;
		p.downTime = time_now_d();
		p.downX = touch.x;
		p.downY = touch.y;
		p.lastX = touch.x;
		p.lastY = touch.y;
		p.distanceX = 0.0f;
		p.distanceY = 0.0f;
		p.estimatedInertiaX = 0.0f;
		p.estimatedInertiaY = 0.0f;
	} else if (touch.flags & TOUCH_UP) {
		p.down = false;
	} else {
		p.distanceX += fabsf(touch.x - p.lastX);
		p.distanceY += fabsf(touch.y - p.lastY);

		p.estimatedInertiaX += touch.x - p.lastX;
		p.estimatedInertiaY += touch.y - p.lastY;
		p.estimatedInertiaX *= estimatedInertiaDamping;
		p.estimatedInertiaY *= estimatedInertiaDamping;

		p.lastX = touch.x;
		p.lastY = touch.y;
	}

	if (p.distanceY > p.distanceX) {
		if (p.down) {
			double timeDown = time_now_d() - p.downTime;
			if (!p.active && p.distanceY * timeDown > 3) {
				p.active |= GESTURE_DRAG_VERTICAL;
				// Kill the drag. TODO: Only cancel the drag in one direction.
				TouchInput inp2 = touch;
				inp2.flags = TOUCH_UP | TOUCH_CANCEL;
				return inp2;
			}
		} else {
			p.active = 0;
		}
	}

	if (p.distanceX > p.distanceY) {
		if (p.down) {
			double timeDown = time_now_d() - p.downTime;
			if (!p.active && p.distanceX * timeDown > 3) {
				p.active |= GESTURE_DRAG_HORIZONTAL;
				// Kill the drag. TODO: Only cancel the drag in one direction.
				TouchInput inp2 = touch;
				inp2.flags = TOUCH_UP | TOUCH_CANCEL;
				return inp2;
			}
		} else {
			p.active = 0;
		}
	}

	return touch;
}

void GestureDetector::UpdateFrame() {
	for (int i = 0; i < MAX_PTRS; i++) {
		pointers[i].estimatedInertiaX *= estimatedInertiaDamping;
		pointers[i].estimatedInertiaY *= estimatedInertiaDamping;
	}
}

bool GestureDetector::IsGestureActive(Gesture gesture, int touchId) const {
	if (touchId < 0 || touchId >= MAX_PTRS)
		return false;
	return (pointers[touchId].active & gesture) != 0;
}

bool GestureDetector::GetGestureInfo(Gesture gesture, int touchId, float info[4]) const {
	if (touchId < 0 || touchId >= MAX_PTRS)
		return false;
	memset(info, 0, sizeof(float) * 4);
	if (!(pointers[touchId].active & gesture)) {
		return false;
	}

	switch (gesture) {
	case GESTURE_DRAG_HORIZONTAL:
		info[0] = pointers[touchId].lastX - pointers[touchId].downX;
		info[1] = pointers[touchId].estimatedInertiaX;
		return true;
	case GESTURE_DRAG_VERTICAL:
		info[0] = pointers[touchId].lastY - pointers[touchId].downY;
		info[1] = pointers[touchId].estimatedInertiaY;
		return true;
	default:
		return false;
	}
}

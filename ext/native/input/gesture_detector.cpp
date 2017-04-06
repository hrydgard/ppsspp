// Unfinished.
// TODO:
// Zoom gesture a la http://www.zdnet.com/blog/burnette/how-to-use-multi-touch-in-android-2-part-6-implementing-the-pinch-zoom-gesture/1847

#include "base/logging.h"
#include "base/timeutil.h"
#include "input/gesture_detector.h"


const float estimatedInertiaDamping = 0.75f;

GestureDetector::GestureDetector()
	: active_(0),
		estimatedInertiaX_(0.0f),
		estimatedInertiaY_(0.0f) {
	memset(pointers, 0, sizeof(pointers));
}

TouchInput GestureDetector::Update(const TouchInput &touch, int scrollTouchId, const Bounds &bounds) {
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
		estimatedInertiaX_ = 0.0f;
		estimatedInertiaY_ = 0.0f;
	} else if (touch.flags & TOUCH_UP) {
		p.down = false;
	} else {
		p.distanceX += fabsf(touch.x - p.lastX);
		p.distanceY += fabsf(touch.y - p.lastY);

		estimatedInertiaX_ += touch.x - p.lastX;
		estimatedInertiaY_ += touch.y - p.lastY;
		estimatedInertiaX_ *= estimatedInertiaDamping;
		estimatedInertiaY_ *= estimatedInertiaDamping;

		p.lastX = touch.x;
		p.lastY = touch.y;
	}

	if (touch.id == scrollTouchId && p.distanceY > p.distanceX) {
		if (p.down) {
			double timeDown = time_now_d() - p.downTime;
			if (!active_ && p.distanceY * timeDown > 3) {
				active_ |= GESTURE_DRAG_VERTICAL;
				// Kill the drag
				TouchInput inp2 = touch;
				inp2.flags = TOUCH_UP | TOUCH_CANCEL;
				return inp2;
			}
		} else {
			active_ = 0;
		}
	}

	if (touch.id == scrollTouchId && p.distanceX > p.distanceY) {
		if (p.down) {
			double timeDown = time_now_d() - p.downTime;
			if (!active_ && p.distanceX * timeDown > 3) {
				active_ |= GESTURE_DRAG_HORIZONTAL;
				// Kill the drag
				TouchInput inp2 = touch;
				inp2.flags = TOUCH_UP | TOUCH_CANCEL;
				return inp2;
			}
		} else {
			active_ = 0;
		}
	}

	return touch;
}

void GestureDetector::UpdateFrame() {
	estimatedInertiaX_ *= estimatedInertiaDamping;
	estimatedInertiaY_ *= estimatedInertiaDamping;
}

bool GestureDetector::IsGestureActive(Gesture gesture) const {
	return (active_ & gesture) != 0;
}

bool GestureDetector::GetGestureInfo(Gesture gesture, int touchId, float info[4]) const {
	memset(info, 0, sizeof(float) * 4);
	if (!(active_ & gesture)) {
		return false;
	}

	switch (gesture) {
	case GESTURE_DRAG_HORIZONTAL:
		info[0] = pointers[touchId].lastX - pointers[touchId].downX;
		info[1] = estimatedInertiaX_;
		return true;
	case GESTURE_DRAG_VERTICAL:
		info[0] = pointers[touchId].lastY - pointers[touchId].downY;
		info[1] = estimatedInertiaY_;
		return true;
	default:
		return false;
	}
}

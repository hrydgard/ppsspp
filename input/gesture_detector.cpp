// Unfinished.
// TODO:
// Zoom gesture a la http://www.zdnet.com/blog/burnette/how-to-use-multi-touch-in-android-2-part-6-implementing-the-pinch-zoom-gesture/1847

#include "base/timeutil.h"
#include "input/gesture_detector.h"

GestureDetector::GestureDetector() : active_(0) {

}

TouchInput GestureDetector::Update(const TouchInput &touch, const Bounds &bounds) {
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
	} else if (touch.flags & TOUCH_UP) {
		p.down = false;
	} else {
		p.distanceX += fabsf(touch.x - p.lastX);
		p.distanceY += fabsf(touch.y - p.lastY);
		p.lastX = touch.x;
		p.lastY = touch.y;
	}

	if (touch.id == 0 && p.distanceY > p.distanceX) {
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
	return touch;
}

bool GestureDetector::IsGestureActive(Gesture gesture) const {
	return (active_ & gesture) != 0;
}

void GestureDetector::GetGestureInfo(Gesture gesture, float info[4]) {
	if (gesture == GESTURE_DRAG_VERTICAL) {
		info[0] = pointers[0].lastY - pointers[0].downY;
	}
/*

bool down(int i, float *xdelta, float *ydelta) {
	if (!pointers[i].down) {
		return false;
	}
	*xdelta = pointers[i].downX;
	*ydelta = pointers[i].downY;
	return true;
}

bool dragDistance(int i, float *xdelta, float *ydelta) {
	if (!pointers[i].down)
		return false;

	*xdelta = pointers[i].X - pointers[i].downX;
	*ydelta = pointers[i].Y - pointers[i].downY;
	return true;
}

bool dragDelta(int i, float *xdelta, float *ydelta) {
	if (!pointers[i].down)
		return false;

	*xdelta = pointers[i].X - pointers[i].lastX;
	*ydelta = pointers[i].Y - pointers[i].lastY;
	return true;
}
*/

}

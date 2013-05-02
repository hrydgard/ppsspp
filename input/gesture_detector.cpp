// Unfinished.
// TODO:
// Zoom gesture a la http://www.zdnet.com/blog/burnette/how-to-use-multi-touch-in-android-2-part-6-implementing-the-pinch-zoom-gesture/1847

#include "input/gesture_detector.h"

void GestureDetector::Update(const TouchInput &touch) {
	// Mouse / 1-finger-touch control.
	if (touch.flags & TOUCH_DOWN) {
		pointers[0].down = true;
		pointers[0].downX = touch.x;
		pointers[0].downY = touch.y;
	} else if (touch.flags & TOUCH_UP) {
		pointers[0].down = false;
	}

	pointers[0].lastX = pointers[0].X;
	pointers[0].lastY = pointers[0].Y;

	// TODO: real multitouch
}

bool GestureDetector::IsGestureActive(Gesture gesture) const {
	// TODO
	return false;
}

void GestureDetector::GetGestureInfo(Gesture gesture, float info[4])
{
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

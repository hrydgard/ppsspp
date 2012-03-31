#include "input/input_state.h"

// WIP - doesn't do much yet
// Mainly for detecting (multi-)touch gestures but also useable for left button mouse dragging etc.

namespace GestureDetector
{
	void update(const InputState &state);

	bool down(int finger, float *xdown, float *ydown);

	// x/ydelta is difference from current location to the start of the drag.
	// Returns true if button/finger is down, for convenience.
	bool dragDistance(int finger, float *xdelta, float *ydelta);
	
	// x/ydelta is (smoothed?) difference from current location to the position from the last frame.
	// Returns true if button/finger is down, for convenience.
	bool dragDelta(int finger, float *xdelta, float *ydelta);


};


#include <math.h>

#include "math/math_util.h"
#include "curves.h"

float linearInOut(int t, int fadeInLength, int solidLength, int fadeOutLength) {
	if (t < 0) return 0;
	if (t < fadeInLength) {
		return (float)t / fadeInLength;
	}
	t -= fadeInLength;
	if (t < solidLength) {
		return 1.0f;
	}
	t -= solidLength;
	if (t < fadeOutLength) {
		return 1.0f - (float)t / fadeOutLength;
	}
	return 0.0f;
}

float linearIn(int t, int fadeInLength) {
	if (t < 0) return 0;
	if (t < fadeInLength) {
		return (float)t / fadeInLength;
	}
	return 1.0f;
}

float linearOut(int t, int fadeOutLength) {
	return 1.0f - linearIn(t, fadeOutLength);
}

float ease(float val) {
	if (val > 1.0f) return 1.0f;
	if (val < 0.0f) return 0.0f;
	return ((-cosf(val * PI)) + 1.0f) * 0.5;
}

float ease(int t, int fadeLength)
{
	if (t < 0) return 0.0f;
	if (t >= fadeLength) return 1.0f;
	return ease((float)t / (float)fadeLength);
}

float sawtooth(int t, int period) {
	return (t % period) * (1.0f / (period - 1));
}

float passWithPause(int t, int fadeInLength, int pauseLength, int fadeOutLength)
{
	if (t < fadeInLength) {
		return -1.0f + (float)t / fadeInLength;
	}
	t -= fadeInLength;
	if (t < pauseLength) {
		return 0.0f;
	}
	t -= pauseLength;
	if (t < fadeOutLength) {
		return (float)t / fadeOutLength;
	}
	return 1.0f;
}

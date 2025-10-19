#pragma once

#include "Common/GPU/thin3d.h"

// See big comment in the CPP file.

namespace Draw {
class DrawContext;
}

class FrameTiming {
public:
	void DeferWaitUntil(double until, double *curTimePtr);
	void PostSubmit();
	void Reset(Draw::DrawContext *draw);

	// Some backends won't allow changing this willy nilly.
	Draw::PresentMode presentMode;

private:
	double waitUntil_;
	double *curTimePtr_;
};

extern FrameTiming g_frameTiming;

Draw::PresentMode ComputePresentMode(Draw::DrawContext *draw);

void WaitUntil(double now, double timestamp, const char *reason);

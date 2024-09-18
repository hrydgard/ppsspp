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
	int presentInterval;

private:
	double waitUntil_;
	double *curTimePtr_;
};

extern FrameTiming g_frameTiming;

Draw::PresentMode ComputePresentMode(Draw::DrawContext *draw, int *interval);

void WaitUntil(double now, double timestamp);

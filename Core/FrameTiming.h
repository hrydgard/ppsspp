#pragma once

#include "Common/GPU/thin3d.h"

// See big comment in the CPP file.

namespace Draw {
class DrawContext;
}

struct FrameTiming {
	// Some backends won't allow changing this willy nilly.
	Draw::PresentMode presentMode;
	int presentInterval;

	// The new timing method.
	bool usePresentTiming;

	double cpuSliceStartTime;
	double cpuTime;
	double timeStep;
	double postSleep;

	double lastPresentTime;
	double nextPresentTime;

	void Reset(Draw::DrawContext *draw);

	void BeforeCPUSlice();
	void EndOfCPUSlice(float scaledTimeStep);
	void BeforePresent();
	void AfterPresent();
};

extern FrameTiming g_frameTiming;

Draw::PresentMode ComputePresentMode(Draw::DrawContext *draw, int *interval);

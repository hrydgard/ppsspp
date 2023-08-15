#pragma once

#include "Common/GPU/thin3d.h"

// See big comment in the CPP file.

namespace Draw {
class DrawContext;
}

class FrameTiming {
public:
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

	void BeforeCPUSlice(const FrameHistoryBuffer &frameHistory);
	void SetTimeStep(float scaledTimeStep);
	void AfterCPUSlice();
	void BeforePresent();
	void AfterPresent();

private:
	bool setTimestepCalled_ = false;
	double nudge_ = 0.0;
};

extern FrameTiming g_frameTiming;

Draw::PresentMode ComputePresentMode(Draw::DrawContext *draw, int *interval);

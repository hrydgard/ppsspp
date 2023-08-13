#pragma once

#include "Common/GPU/thin3d.h"

namespace Draw {
class DrawContext;
}

struct FrameTiming {
	// Some backends won't allow changing this willy nilly.
	Draw::PresentMode presentMode;
	int presentInterval;

	void Reset(Draw::DrawContext *draw);
};

extern FrameTiming g_frameTiming;

Draw::PresentMode ComputePresentMode(Draw::DrawContext *draw, int *interval);

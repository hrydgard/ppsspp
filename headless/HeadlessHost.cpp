// Copyright (c) 2017- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "headless/Compare.h"
#include "headless/HeadlessHost.h"

void HeadlessHost::SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h) {
	// Only if we're actually comparing.
	if (comparisonScreenshot_.empty()) {
		return;
	}

	// We ignore the current framebuffer parameters and just grab the full screen.
	const static u32 FRAME_STRIDE = 512;
	const static u32 FRAME_WIDTH = 480;
	const static u32 FRAME_HEIGHT = 272;

	GPUDebugBuffer buffer;
	gpuDebug->GetCurrentFramebuffer(buffer, GPU_DBG_FRAMEBUF_DISPLAY);
	const std::vector<u32> pixels = TranslateDebugBufferToCompare(&buffer, 512, 272);

	ScreenshotComparer comparer(pixels, FRAME_STRIDE, FRAME_WIDTH, FRAME_HEIGHT);
	double errors = comparer.Compare(comparisonScreenshot_);
	if (errors < 0)
		SendAndCollectOutput(comparer.GetError() + "\n");

	if (errors > maxScreenshotError_)
		SendAndCollectOutput(StringFromFormat("Screenshot MSE: %f\n", errors));

	if (errors > maxScreenshotError_ && writeFailureScreenshot_) {
		if (comparer.SaveActualBitmap(Path("__testfailure.bmp")))
			SendAndCollectOutput("Actual output written to: __testfailure.bmp\n");
		comparer.SaveVisualComparisonPNG(Path("__testcompare.png"));
	}
}

void HeadlessHost::SendAndCollectOutput(const std::string &output) {
	SendDebugOutput(output);
	if (PSP_CoreParameter().collectDebugOutput)
		*PSP_CoreParameter().collectDebugOutput += output;
}

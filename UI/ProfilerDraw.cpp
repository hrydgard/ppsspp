// Copyright (c) 2014- PPSSPP Project.

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

#include <algorithm>
#include <inttypes.h>

#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/Profiler/Profiler.h"

#ifdef USE_PROFILER
static const uint32_t nice_colors[] = {
	0xFF8040,
	0x80FF40,
	0x8040FF,
	0xFFFF40,

	0x40FFFF,
	0xFF70FF,
	0xc0c0c0,
	0xb040c0,

	0x184099,
	0xCC3333,
	0xFF99CC,
	0x3399CC,

	0x990000,
	0x003366,
	0xF8F8F8,
	0x33FFFF,
};
#endif

enum ProfileCatStatus {
	PROFILE_CAT_VISIBLE = 0,
	PROFILE_CAT_IGNORE = 1,
	PROFILE_CAT_NOLEGEND = 2,
};

void DrawProfile(UIContext &ui) {
#ifdef USE_PROFILER
	PROFILE_THIS_SCOPE("timing");
	int numCategories = Profiler_GetNumCategories();
	int numThreads = Profiler_GetNumThreads();
	int historyLength = Profiler_GetHistoryLength();

	ui.SetFontStyle(ui.theme->uiFont);

	static float lastMaxVal = 1.0f / 60.0f;
	float legendMinVal = lastMaxVal * (1.0f / 120.0f);

	std::vector<float> history(historyLength);
	std::vector<int> slowestThread(historyLength);
	std::vector<ProfileCatStatus> catStatus(numCategories);

	Profiler_GetSlowestThreads(&slowestThread[0], historyLength);

	float rowH = 30.0f;
	float legendHeight = 0.0f;
	float legendWidth = 80.0f;
	for (int i = 0; i < numCategories; i++) {
		const char *name = Profiler_GetCategoryName(i);
		if (!strcmp(name, "timing")) {
			catStatus[i] = PROFILE_CAT_IGNORE;
			continue;
		}

		Profiler_GetSlowestHistory(i, &slowestThread[0], &history[0], historyLength);
		catStatus[i] = PROFILE_CAT_NOLEGEND;
		for (int j = 0; j < historyLength; ++j) {
			if (history[j] > legendMinVal) {
				catStatus[i] = PROFILE_CAT_VISIBLE;
				break;
			}
		}

		// So they don't move horizontally, we always measure.
		float w = 0.0f, h = 0.0f;
		ui.MeasureText(ui.GetFontStyle(), 1.0f, 1.0f, name, &w, &h);
		if (w > legendWidth) {
			legendWidth = w;
		}
		legendHeight += rowH;
	}
	legendWidth += 20.0f;

	if (legendHeight > ui.GetBounds().h) {
		legendHeight = ui.GetBounds().h;
	}

	float legendStartY = legendHeight > ui.GetBounds().centerY() ? ui.GetBounds().y2() - legendHeight : ui.GetBounds().centerY();
	float legendStartX = ui.GetBounds().x2() - std::min(legendWidth, 200.0f);

	const uint32_t opacity = 140 << 24;

	int legendNum = 0;
	for (int i = 0; i < numCategories; i++) {
		const char *name = Profiler_GetCategoryName(i);
		uint32_t color = nice_colors[i % ARRAY_SIZE(nice_colors)];

		if (catStatus[i] == PROFILE_CAT_VISIBLE) {
			float y = legendStartY + legendNum++ * rowH;
			ui.FillRect(UI::Drawable(opacity | color), Bounds(legendStartX, y, rowH - 2, rowH - 2));
			ui.DrawTextShadow(name, legendStartX + rowH + 2, y, 0xFFFFFFFF, ALIGN_VBASELINE);
		}
	}

	float graphWidth = ui.GetBounds().x2() - legendWidth - 20.0f;
	float graphHeight = ui.GetBounds().h * 0.8f;

	float dx = graphWidth / historyLength;

	/*
	ui.Flush();

	ui.BeginNoTex();
	*/

	bool area = true;
	float minVal = 0.0f;
	float maxVal = lastMaxVal;  // TODO - adjust to frame length
	if (maxVal < 0.001f)
		maxVal = 0.001f;
	if (maxVal > 1.0f / 15.0f)
		maxVal = 1.0f / 15.0f;

	float scale = (graphHeight) / (maxVal - minVal);

	float y_60th = ui.GetBounds().y2() - 10 - (1.0f / 60.0f) * scale;
	float y_1ms = ui.GetBounds().y2() - 10 - (1.0f / 1000.0f) * scale;

	ui.FillRect(UI::Drawable(0x80FFFF00), Bounds(0, y_60th, graphWidth, 2));
	ui.FillRect(UI::Drawable(0x80FFFF00), Bounds(0, y_1ms, graphWidth, 2));
	ui.DrawTextShadow("1/60s", 5, y_60th, 0x80FFFF00);
	ui.DrawTextShadow("1ms", 5, y_1ms, 0x80FFFF00);

	std::vector<float> total;
	total.resize(historyLength);

	maxVal = 0.0f;
	float maxTotal = 0.0f;
	for (int i = 0; i < numCategories; i++) {
		if (catStatus[i] == PROFILE_CAT_IGNORE) {
			continue;
		}
		Profiler_GetSlowestHistory(i, &slowestThread[0], &history[0], historyLength);

		float x = 10;
		uint32_t col = nice_colors[i % ARRAY_SIZE(nice_colors)];
		if (area)
			col = opacity | (col & 0xFFFFFF);
		UI::Drawable color(col);
		UI::Drawable outline((opacity >> 1) | 0xFFFFFF);

		float bottom = ui.GetBounds().y2();
		if (area) {
			for (int n = 0; n < historyLength; n++) {
				float val = history[n];
				float valY1 = bottom - 10 - (val + total[n]) * scale;
				float valY2 = bottom - 10 - total[n] * scale;
				ui.FillRect(outline, Bounds(x, valY2, dx, 1.0f));
				ui.FillRect(color, Bounds(x, valY1, dx, valY2 - valY1));
				x += dx;
				total[n] += val;
			}
		} else {
			for (int n = 0; n < historyLength; n++) {
				float val = history[n];
				if (val > maxVal)
					maxVal = val;
				float valY = bottom - 10 - history[n] * scale;
				ui.FillRect(color, Bounds(x, valY, dx, 5));
				x += dx;
			}
		}
	}

	for (int n = 0; n < historyLength; n++) {
		if (total[n] > maxTotal)
			maxTotal = total[n];
	}

	if (area) {
		maxVal = maxTotal;
	}

	lastMaxVal = lastMaxVal * 0.95f + maxVal * 0.05f;
#endif
}

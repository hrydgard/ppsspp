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
#include <cmath>
#include <inttypes.h>

#include "Common/Log.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Profiler/EventStream.h"
#include "Common/TimeUtil.h"

#include "UI/ProfilerDraw.h"

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

EventProfilerView::EventProfilerView(UI::LayoutParams *layoutParams) : UI::AnchorLayout(layoutParams) {
	using namespace UI;

	LinearLayout *buttonRow = new LinearLayout(UI::ORIENT_HORIZONTAL, new AnchorLayoutParams(10.0f, NONE, NONE, 10.0f));
	Add(buttonRow);

	// Can create buttons and stuff here, I think.
	// Let's place these at the bottom of the screen to be out of the way for now. will do something better later.
	buttonRow->Add(new Button("Pause"))->OnClick.Add([=](UI::EventParams &) {
		g_eventStreamManager.SetRunning(!g_eventStreamManager.IsRunning());
		return UI::EVENT_DONE;
	});

	buttonRow->Add(new Button("Zoom In"))->OnClick.Add([=](UI::EventParams &) {
		ZoomAtCursor(2.0f);
		return UI::EVENT_DONE;
	});

	buttonRow->Add(new Button("Zoom Out"))->OnClick.Add([=](UI::EventParams &) {
		ZoomAtCursor(0.5f);
		return UI::EVENT_DONE;
	});
}

void EventProfilerView::ZoomAtCursor(float zoomAmount) {
	// Zoom around the hover X
	float fractionX = hoverX_ / bounds_.w;

	float oldTimeSpan = timeSpan_;
	timeSpan_ *= 1.0 / zoomAmount;

	float diff = oldTimeSpan - timeSpan_;

	startTime_ += diff * fractionX;
	// float delta = hoverTime_ - startTime_;
	UpdateScale();
}

bool EventProfilerView::Key(const KeyInput &input) {
	float zoomAmount = 1.0f;

	switch (input.keyCode) {
	case NKCODE_EXT_MOUSEWHEEL_UP:
		zoomAmount = 2.0f;
		break;
	case NKCODE_EXT_MOUSEWHEEL_DOWN:
		zoomAmount = 0.5f;
		break;
	}

	if (zoomAmount != 1.0f) {
		ZoomAtCursor(zoomAmount);
	}

	return AnchorLayout::Key(input);
}

void EventProfilerView::Touch(const TouchInput &input) {
	AnchorLayout::Touch(input);

	if ((input.flags & TOUCH_DOWN) && input.id == 0) {
		dragging_ = true;
		dragStartX_ = input.x;
		dragStartY_ = input.y;
		dragStartScrollY_ = scrollY_;
		dragStartTime_ = startTime_;
		hoverX_ = input.x;
		hoverY_ = input.y;
	}

	if (input.flags & (TOUCH_MOVE | TOUCH_HOVER_MOVE)) {
		if (dragging_) {
			double dx = input.x - dragStartX_;
			double dy = input.y - dragStartY_;

			startTime_ = dragStartTime_ - dx / secondsToPixelsScale_;
			scrollY_ = dragStartScrollY_ + dy;
		} else {
			hoverX_ = input.x;
			hoverY_ = input.y;
			hoverTime_ = startTime_ + hoverX_ / secondsToPixelsScale_;
			hoverStream_ = (hoverY_ - scrollY_) / streamHeight_;
		}
	}

	if ((input.flags & TOUCH_UP) && input.id == 0) {
		dragging_ = false;
	}
}

void EventProfilerView::UpdateScale() {
	secondsToPixelsScale_ = bounds_.w / timeSpan_;
}

void EventProfilerView::Update() {
	AnchorLayout::Update();

	if (g_eventStreamManager.IsRunning() && !dragging_) {
		// Scroll to the current time, put it exactly on the right edge of the screen.
		Instant now = Instant::Now();
		startTime_ = now.ToSeconds() - timeSpan_;
	}

	if (hoverStream_ >= 0 && hoverStream_ < g_eventStreamManager.NumStreams()) {
		hoverEvent_ = g_eventStreamManager.GetByIndex(hoverStream_)->GetEventIdAt(Instant::FromSeconds(hoverTime_));
	}

	UpdateScale();
}

void EventProfilerView::Draw(UIContext &ui) {
	AnchorLayout::Draw(ui);

	const Bounds &bounds = ui.GetBounds();

	uint32_t frameColors[3] = { 0xFF5060FF, 0xFFFF8030, 0xFF30CC50 };

	std::vector<EventStream *> streams = g_eventStreamManager.GetAll();

	double y = scrollY_;

	ui.SetFontScale(0.4f, 0.4f);

	Instant startTime = Instant::FromSeconds(startTime_);
	Instant endTime = startTime.AddSeconds(timeSpan_);

	char temp[256];

	EventStream::Event hoveredEvent{};

	// Draw millisecond ticks at the top.
	double tickSize = 0.001;  // 1 ms
	int tickCount = (int)(timeSpan_ / tickSize) + 2;

	// If there are too many ticks, keep every 10th.
	while (tickCount >= bounds_.w / 10.0) {
		tickSize *= 10.0;
		tickCount /= 10;
	}

	double firstTickTime = startTime.ToSeconds() - fmod(startTime.ToSeconds(), tickSize);

	for (int i = 0; i < tickCount; i++) {
		double t = firstTickTime + i * tickSize;
		double x = (t - startTime_) * secondsToPixelsScale_;
		ui.VLine(x, y - 5.0, y, 0xFFFFFFFF);
	}

	for (size_t i = 0; i < streams.size(); i++) {
		int64_t firstEventId = -1;
		std::vector<EventStream::Event> events = streams[i]->GetEventRange(startTime, endTime, &firstEventId);

		if (events.empty()) {
			continue;
		}

		double lastTime = events.back().endTime.ToSeconds();

		// Background
		Bounds hoverStreamBounds;
		hoverStreamBounds.y = y;
		hoverStreamBounds.h = streamHeight_;
		hoverStreamBounds.x = 0.0f;
		hoverStreamBounds.w = bounds.w;
		// Highlight the hovered stream.
		bool hoverStream = hoverStream_ == i;
		UI::Drawable color(hoverStream ? 0x50FFFFFF : 0x50000000);
		ui.FillRect(color, hoverStreamBounds);

		for (size_t e = 0; e < events.size(); e++) {
			const EventStream::Event &event = events[e];
			int64_t eventId = firstEventId + e;

			bool hover = hoverStream && hoverEvent_ == eventId && hoverTime_ <= event.endTime.ToSeconds();
			if (hover) {
				hoveredEvent = event;
			}

			float inset = 4.0f;
			u32 alphaMask = 0xFFFFFFFF;
			if (event.type == EventStream::EventType::BLOCK) {
				inset = 12.0f;
				alphaMask = 0xC0FFFFFF;
			}

			Bounds rectBounds;
			rectBounds.y = y + inset;
			rectBounds.x = secondsToPixelsScale_ * (event.startTime.ToSeconds() - startTime_);
			rectBounds.w = secondsToPixelsScale_ * (event.startTime.DifferenceInSeconds(event.endTime));
			rectBounds.h = streamHeight_ - inset * 2;

			if (hover) {
				Bounds outlineRectBounds = rectBounds.Expand(2.0f, 2.0f);
				ui.FillRect(UI::Drawable(0xFFFFFFFF & alphaMask), outlineRectBounds);
			}

			UI::Drawable color(frameColors[event.frameId % ARRAY_SIZE(frameColors)] & alphaMask);
			ui.FillRect(color, rectBounds);

			snprintf(temp, sizeof(temp), "%s %d", event.name, (int)event.frameId);
			ui.DrawTextRect(temp, rectBounds, 0xFFFFFFFF, ALIGN_VCENTER | FLAG_DYNAMIC_ASCII);
		}

		y += streamHeight_;
	}

	// Draw a vertical line at the hover time, if paused.
	if (!g_eventStreamManager.IsRunning()) {
		float secsIntoView = (hoverTime_ - startTime_);
		ui.VLine(secsIntoView * secondsToPixelsScale_, scrollY_, y, 0xCCFFFFFF);
	}

	ui.Flush();

	// Draw info about the hovered item at the bottom.
	if (hoveredEvent.name) {
		EventStream *stream = g_eventStreamManager.GetByIndex(hoverStream_);
		if (stream) {
			snprintf(temp, sizeof(temp), "%s (%d): %0.2fms\n%s",
				hoveredEvent.name, (int)hoveredEvent.frameId, hoveredEvent.startTime.DifferenceInSeconds(hoveredEvent.endTime) * 1000.0,
				stream->Identifier());
			ui.DrawTextShadow(temp, hoverX_, y, 0xFFFFFFFF);
		}
	}

	ui.SetFontScale(1.0f, 1.0f);
}

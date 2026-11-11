// Copyright (c) 2012- PPSSPP Project.

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
#include <cstring>
#include <vector>

#include "ext/imgui/imgui.h"

#include "Common/Common.h"
#include "Common/Profiler/Profiler.h"
#include "UI/ImDebugger/ImDebugger.h"
#include "UI/ImDebugger/ImProfiler.h"

#ifdef USE_PROFILER

namespace {

const ImU32 categoryColors[] = {
	IM_COL32(0x40, 0x80, 0xFF, 255),
	IM_COL32(0x40, 0xC0, 0x60, 255),
	IM_COL32(0xD0, 0x60, 0x40, 255),
	IM_COL32(0xB0, 0x50, 0xC0, 255),
	IM_COL32(0x30, 0xA0, 0xA0, 255),
	IM_COL32(0xC0, 0xA0, 0x30, 255),
	IM_COL32(0x80, 0x60, 0x40, 255),
	IM_COL32(0x60, 0x70, 0xA0, 255),
	IM_COL32(0xE0, 0x40, 0x80, 255),
	IM_COL32(0x90, 0xD0, 0x40, 255),
	IM_COL32(0x40, 0xD0, 0xE0, 255),
	IM_COL32(0xE0, 0x90, 0x20, 255),
	IM_COL32(0x80, 0x40, 0xE0, 255),
	IM_COL32(0x20, 0x80, 0x50, 255),
	IM_COL32(0xA0, 0x30, 0x30, 255),
	IM_COL32(0xD0, 0xD0, 0x90, 255),
};

// Names containing these are waits, drawn grey.
bool IsWaitCategory(const char *name) {
	return strstr(name, "sleep") || strstr(name, "wait") || strstr(name, "idle");
}

ImU32 CategoryColor(int category) {
	if (IsWaitCategory(Profiler_GetCategoryName(category))) {
		return IM_COL32(0x70, 0x70, 0x70, 255);
	}
	return categoryColors[category % ARRAY_SIZE(categoryColors)];
}

struct TimelineView {
	double spanSeconds = 0.05;
	// Right edge of the view, used when not following.
	uint64_t end = 0;
	bool follow = true;
	// Deepest level seen per thread, so lanes don't jump in height.
	std::vector<int> laneDepth;
};

TimelineView g_view;

// The first time a profiler window shows. After that, its Record checkbox decides.
void TurnOnOnce() {
	static bool done = false;
	if (!done) {
		Profiler_SetEnabled(true);
		done = true;
	}
}

// How many scopes all threads left in the last frame, and on average over the history: what the profiler
// costs to run, and while off, about how many loads and branches it adds.
void DrawScopeCount() {
	const int historyLength = Profiler_GetHistoryLength();
	std::vector<int> counts(historyLength);
	std::vector<int> total(historyLength);
	for (int t = 0; t < Profiler_GetNumThreads(); t++) {
		for (int c = 0; c < Profiler_GetNumCategories(); c++) {
			Profiler_GetHistoryCounts(c, t, counts.data(), historyLength);
			for (int i = 0; i < historyLength; i++) {
				total[i] += counts[i];
			}
		}
	}
	long long sum = 0;
	for (int n : total) {
		sum += n;
	}
	ImGui::Text("Scopes per frame: %d in the last one, %0.0f on average.", total[historyLength - 1], (double)sum / historyLength);
}

std::vector<ProfilerEvent> g_events;

void DrawTimeline() {
	TimelineView &view = g_view;
	const uint64_t now = Profiler_Now();
	if (view.follow && Profiler_IsEnabled()) {
		view.end = now;
	}
	const int64_t span = std::max<int64_t>(Profiler_SecondsToTicks(view.spanSeconds), 1);
	const uint64_t viewEnd = view.end;
	const uint64_t viewStart = viewEnd > (uint64_t)span ? viewEnd - span : 0;

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float nameWidth = 150.0f;
	const float rowH = ImGui::GetTextLineHeight() + 4.0f;
	const float x0 = origin.x + nameWidth;
	const float width = std::max(avail.x - nameWidth, 50.0f);
	auto toX = [&](uint64_t ticks) {
		return x0 + (float)((double)((int64_t)ticks - (int64_t)viewStart) / (double)span) * width;
	};

	const int numThreads = Profiler_GetNumThreads();
	if ((int)view.laneDepth.size() < numThreads) {
		view.laneDepth.resize(numThreads, 1);
	}
	float totalH = rowH;  // ruler
	for (int t = 0; t < numThreads; t++) {
		totalH += rowH * view.laneDepth[t] + 4.0f;
	}

	ImGui::InvisibleButton("timeline", ImVec2(avail.x, std::max(totalH, 50.0f)));
	const bool hovered = ImGui::IsItemHovered();
	ImGuiIO &io = ImGui::GetIO();
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) && io.MouseDelta.x != 0.0f) {
		view.follow = false;
		const int64_t delta = (int64_t)(-io.MouseDelta.x / width * span);
		view.end = (uint64_t)std::max<int64_t>((int64_t)view.end + delta, span);
	}
	if (hovered && io.MouseWheel != 0.0f) {
		// Zoom around the mouse.
		const double frac = std::clamp((io.MousePos.x - x0) / width, 0.0f, 1.0f);
		const uint64_t anchor = viewStart + (uint64_t)(frac * span);
		const double newSpan = std::clamp(view.spanSeconds * pow(0.85, io.MouseWheel), 0.0001, 2.0);
		const int64_t newSpanTicks = Profiler_SecondsToTicks(newSpan);
		view.spanSeconds = newSpan;
		if (!view.follow || !Profiler_IsEnabled()) {
			view.end = anchor + (uint64_t)((1.0 - frac) * newSpanTicks);
		}
	}

	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + std::max(totalH, 50.0f)), true);

	// Ruler, in seconds since the profiler started, with a mark per power of ten of milliseconds that
	// gives at most about 10 marks.
	const double spanMs = Profiler_TicksToSeconds(span) * 1000.0;
	const double stepMs = pow(10.0, floor(log10(std::max(spanMs, 0.001) / 2.0)));
	const int decimals = std::max(0, 3 - (int)floor(log10(stepMs)));
	char buf[64];
	const double startMs = Profiler_TicksToSeconds((int64_t)(viewStart - Profiler_GetStartTicks())) * 1000.0;
	for (double ms = ceil(startMs / stepMs) * stepMs; ms < startMs + spanMs; ms += stepMs) {
		const float x = x0 + (float)((ms - startMs) / spanMs) * width;
		dl->AddLine(ImVec2(x, origin.y + rowH - 4.0f), ImVec2(x, origin.y + rowH), IM_COL32(180, 180, 180, 255));
		snprintf(buf, sizeof(buf), "%.*f s", decimals, ms / 1000.0);
		dl->AddText(ImVec2(x + 2.0f, origin.y), IM_COL32(180, 180, 180, 255), buf);
	}

	struct Open {
		uint64_t start;
		const char *label;
		bool valid;
	};
	const char *hoveredName = nullptr;
	const char *hoveredLabel = nullptr;
	double hoveredMs = 0.0;
	float y = origin.y + rowH;
	for (int t = 0; t < numThreads; t++) {
		const float laneH = rowH * view.laneDepth[t];
		dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + avail.x, y + laneH), (t & 1) ? IM_COL32(40, 40, 40, 255) : IM_COL32(32, 32, 32, 255));
		dl->AddText(ImVec2(origin.x + 4.0f, y + 2.0f), IM_COL32(220, 220, 220, 255), Profiler_GetThreadName(t, buf, sizeof(buf)));

		g_events.clear();
		Profiler_GetEvents(t, viewStart, viewEnd, &g_events);

		Open open[32]{};
		int maxLevel = 0;
		auto drawBar = [&](int level, int category, uint64_t from, uint64_t to, const char *label) {
			maxLevel = std::max(maxLevel, level);
			if (level >= view.laneDepth[t]) {
				return;
			}
			const float xa = std::max(toX(from), x0);
			const float xb = std::max(toX(to), xa + 1.0f);
			const float ya = y + rowH * level;
			const ImVec2 a(xa, ya + 1.0f), b(xb, ya + rowH - 1.0f);
			const char *name = Profiler_GetCategoryName(category);
			dl->AddRectFilled(a, b, CategoryColor(category));
			if (xb - xa > 30.0f) {
				dl->PushClipRect(a, b, true);
				dl->AddText(ImVec2(xa + 2.0f, ya + 2.0f), IM_COL32(255, 255, 255, 255), name);
				dl->PopClipRect();
			}
			if (hovered && io.MousePos.x >= a.x && io.MousePos.x < b.x && io.MousePos.y >= a.y && io.MousePos.y < b.y) {
				hoveredName = name;
				hoveredLabel = label;
				hoveredMs = Profiler_TicksToSeconds((int64_t)(to - from)) * 1000.0;
			}
		};
		for (const ProfilerEvent &e : g_events) {
			if (e.type == ProfilerEventType::Begin) {
				open[e.level] = Open{ e.ticks, e.label, true };
			} else {
				// Without its Begin, the scope started before the view.
				drawBar(e.level, e.category, open[e.level].valid ? open[e.level].start : viewStart, e.ticks, open[e.level].valid ? open[e.level].label : nullptr);
				open[e.level].valid = false;
			}
		}
		// Still open at the right edge.
		for (int level = 0; level < (int)ARRAY_SIZE(open); level++) {
			if (open[level].valid) {
				int category = 0;
				for (auto it = g_events.rbegin(); it != g_events.rend(); ++it) {
					if (it->type == ProfilerEventType::Begin && it->level == level) {
						category = it->category;
						break;
					}
				}
				drawBar(level, category, open[level].start, std::min(viewEnd, now), open[level].label);
			}
		}
		if (maxLevel + 1 > view.laneDepth[t]) {
			view.laneDepth[t] = maxLevel + 1;
		}
		y += laneH + 4.0f;
	}

	// Frame ends, over the lanes.
	std::vector<uint64_t> frameEnds;
	Profiler_GetFrameEnds(&frameEnds);
	for (uint64_t t : frameEnds) {
		if (t >= viewStart && t <= viewEnd) {
			const float x = toX(t);
			dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + totalH), IM_COL32(255, 255, 0, 160));
		}
	}
	dl->PopClipRect();

	if (hoveredName) {
		ImGui::SetTooltip("%s%s%s\n%0.3f ms", hoveredName, hoveredLabel ? ": " : "", hoveredLabel ? hoveredLabel : "", hoveredMs);
	}
}

void DrawFrameTotals() {
	const int numThreads = Profiler_GetNumThreads();
	const int numCategories = Profiler_GetNumCategories();
	const int historyLength = Profiler_GetHistoryLength();
	std::vector<float> history(historyLength);
	std::vector<int> counts(historyLength);
	char buf[64];

	ImGui::TextUnformatted("Time spent in each category itself, in the last frame and on average over the history.");
	for (int t = 0; t < numThreads; t++) {
		if (!ImGui::CollapsingHeader(Profiler_GetThreadName(t, buf, sizeof(buf)), ImGuiTreeNodeFlags_DefaultOpen)) {
			continue;
		}
		ImGui::PushID(t);
		if (ImGui::BeginTable("totals", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Last ms");
			ImGui::TableSetupColumn("Last count");
			ImGui::TableSetupColumn("Avg ms");
			ImGui::TableSetupColumn("Max ms");
			ImGui::TableHeadersRow();
			for (int c = 0; c < numCategories; c++) {
				Profiler_GetHistory(c, t, history.data(), historyLength);
				Profiler_GetHistoryCounts(c, t, counts.data(), historyLength);
				float sum = 0.0f, maxVal = 0.0f;
				for (float v : history) {
					sum += v;
					maxVal = std::max(maxVal, v);
				}
				if (maxVal <= 0.0f) {
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Profiler_GetCategoryName(c));
				ImGui::TableNextColumn();
				ImGui::Text("%0.2f", history[historyLength - 1] * 1000.0f);
				ImGui::TableNextColumn();
				ImGui::Text("%d", counts[historyLength - 1]);
				ImGui::TableNextColumn();
				ImGui::Text("%0.2f", sum / historyLength * 1000.0f);
				ImGui::TableNextColumn();
				ImGui::Text("%0.2f", maxVal * 1000.0f);
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
	}
}

}  // namespace

#endif

void DrawFrameProfile(ImConfig &cfg) {
	ImGui::SetNextWindowSize(ImVec2(800, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Frame profile", &cfg.frameProfileOpen)) {
		ImGui::End();
		return;
	}
#ifdef USE_PROFILER
	TurnOnOnce();
	const int numCategories = Profiler_GetNumCategories();
	const int numThreads = Profiler_GetNumThreads();
	const int historyLength = Profiler_GetHistoryLength();

	// history[thread][category][frame]. Threads and categories with no time in the history are left out.
	std::vector<std::vector<std::vector<float>>> history(numThreads);
	std::vector<std::vector<float>> totals(numThreads);
	std::vector<int> shownThreads;
	std::vector<bool> categoryShown(numCategories);
	float maxTotal = 1.0f / 60.0f;
	for (int t = 0; t < numThreads; t++) {
		history[t].resize(numCategories);
		totals[t].resize(historyLength);
		for (int c = 0; c < numCategories; c++) {
			history[t][c].resize(historyLength);
			Profiler_GetHistory(c, t, history[t][c].data(), historyLength);
			for (int i = 0; i < historyLength; i++) {
				totals[t][i] += history[t][c][i];
				if (history[t][c][i] > 0.0f) {
					categoryShown[c] = true;
				}
			}
		}
		const float threadMax = *std::max_element(totals[t].begin(), totals[t].end());
		if (threadMax > 0.0f) {
			shownThreads.push_back(t);
			maxTotal = std::max(maxTotal, threadMax);
		}
	}

	ImGui::Text("Time per frame in each category, per thread. Scale: %0.1f ms.", maxTotal * 1000.0f);
	DrawScopeCount();
	const float legendW = 140.0f;
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const float graphW = std::max(avail.x - legendW, 100.0f);
	const float lineH = ImGui::GetTextLineHeightWithSpacing();
	const float graphH = std::max((avail.y - lineH * shownThreads.size()) / std::max((int)shownThreads.size(), 1) - 4.0f, 40.0f);
	const float dx = graphW / historyLength;
	const float scale = graphH / maxTotal;
	char buf[64];

	ImGui::BeginGroup();
	for (int t : shownThreads) {
		ImGui::TextUnformatted(Profiler_GetThreadName(t, buf, sizeof(buf)));
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImGui::PushID(t);
		ImGui::InvisibleButton("graph", ImVec2(graphW, graphH));
		ImGui::PopID();
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(origin, ImVec2(origin.x + graphW, origin.y + graphH), IM_COL32(25, 25, 25, 255));

		const float bottom = origin.y + graphH;
		for (int i = 0; i < historyLength; i++) {
			float stacked = 0.0f;
			const float x = origin.x + i * dx;
			for (int c = 0; c < numCategories; c++) {
				const float v = history[t][c][i];
				if (v <= 0.0f) {
					continue;
				}
				dl->AddRectFilled(ImVec2(x, bottom - (stacked + v) * scale), ImVec2(x + std::max(dx - 1.0f, 1.0f), bottom - stacked * scale), CategoryColor(c));
				stacked += v;
			}
		}
		// 60 fps line.
		const float y60 = bottom - (1.0f / 60.0f) * scale;
		dl->AddLine(ImVec2(origin.x, y60), ImVec2(origin.x + graphW, y60), IM_COL32(255, 255, 0, 160));
		dl->AddText(ImVec2(origin.x + 4.0f, y60 - ImGui::GetTextLineHeight()), IM_COL32(255, 255, 0, 200), "1/60 s");

		if (hovered) {
			const int i = std::clamp((int)((ImGui::GetIO().MousePos.x - origin.x) / dx), 0, historyLength - 1);
			ImGui::BeginTooltip();
			ImGui::Text("%s, %d frames ago: %0.2f ms", buf, historyLength - 1 - i, totals[t][i] * 1000.0f);
			for (int c = numCategories - 1; c >= 0; c--) {
				if (history[t][c][i] > 0.0f) {
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(CategoryColor(c)), "%s: %0.2f ms", Profiler_GetCategoryName(c), history[t][c][i] * 1000.0f);
				}
			}
			ImGui::EndTooltip();
		}
	}
	ImGui::EndGroup();

	// Legend, in stacking order from the top.
	ImGui::SameLine();
	ImGui::BeginGroup();
	for (int c = numCategories - 1; c >= 0; c--) {
		if (!categoryShown[c]) {
			continue;
		}
		ImGui::ColorButton(Profiler_GetCategoryName(c), ImGui::ColorConvertU32ToFloat4(CategoryColor(c)), ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
		ImGui::SameLine();
		ImGui::TextUnformatted(Profiler_GetCategoryName(c));
	}
	ImGui::EndGroup();
#else
	ImGui::TextUnformatted("Build with USE_PROFILER (Common/Profiler/Profiler.h) to use this.");
#endif
	ImGui::End();
}

void DrawCpuProfiler(ImConfig &cfg) {
	ImGui::SetNextWindowSize(ImVec2(800, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("CPU profiler", &cfg.cpuProfilerOpen)) {
		ImGui::End();
		return;
	}
#ifdef USE_PROFILER
	TurnOnOnce();
	bool record = Profiler_IsEnabled();
	if (ImGui::Checkbox("Record", &record)) {
		Profiler_SetEnabled(record);
	}
	ImGui::SameLine();
	ImGui::Checkbox("Follow", &g_view.follow);
	ImGui::SameLine();
	ImGui::Text("%0.1f ms shown. Wheel zooms, dragging pans.", g_view.spanSeconds * 1000.0);
	DrawScopeCount();
	if (ImGui::BeginTabBar("profiler")) {
		if (ImGui::BeginTabItem("Timeline")) {
			DrawTimeline();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Frame totals")) {
			DrawFrameTotals();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
#else
	ImGui::TextUnformatted("Build with USE_PROFILER (Common/Profiler/Profiler.h) to use this.");
#endif
	ImGui::End();
}

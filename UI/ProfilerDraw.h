#pragma once

#include "UI/View.h"
#include "UI/ViewGroup.h"

class UIContext;

#ifdef USE_PROFILER

void DrawProfile(UIContext &ui);

#endif

class EventProfilerView : public UI::AnchorLayout {
public:
	EventProfilerView(UI::LayoutParams *layoutParams = nullptr);
	void Draw(UIContext &dc) override;
	void Update() override;

	bool Key(const KeyInput &input);
	void Touch(const TouchInput &input);

private:
	void UpdateScale();
	void ZoomAtCursor(float zoomAmount);

	// Params
	double streamHeight_ = 40.0f;

	// Currently zoomed range.
	double startTime_ = 0.0;
	double timeSpan_ = 0.1;

	float scrollY_ = 50.0f;

	double secondsToPixelsScale_ = 1.0f;

	bool dragging_ = false;

	double dragStartTime_ = 0.0f;
	float dragStartScrollY_ = 0.0f;
	double dragStartY_ = 0.0f;
	float dragStartX_ = -1.0f;
	// Hover
	// Which stream the mouse is hovering above.
	double hoverX_ = 0.0;
	double hoverY_ = 0.0;
	double hoverTime_ = 0.0;
	int hoverStream_ = -1;
	int64_t hoverEvent_ = -1;
};

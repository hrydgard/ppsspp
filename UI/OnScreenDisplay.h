#pragma once

#include <string>
#include <list>
#include <mutex>

#include "Common/Math/geom2d.h"
#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/System/System.h"

#ifdef ERROR
#undef ERROR
#endif

class DrawBuffer;

// Infrastructure for rendering overlays.

class OnScreenMessagesView : public UI::InertView {
public:
	OnScreenMessagesView(UI::LayoutParams *layoutParams = nullptr) : UI::InertView(layoutParams) {}
	void Draw(UIContext &dc) override;
	bool Dismiss(float x, float y);  // Not reusing Touch since it's asynchronous.
	std::string DescribeText() const override;
private:
	struct ClickZone {
		int index;
		Bounds bounds;
	};

	// Argh, would really like to avoid this.
	std::mutex clickMutex_;
	std::vector<ClickZone> clickZones_;
};

class OSDOverlayScreen : public UIScreen {
public:
	OSDOverlayScreen() {
		ignoreInsets_ = false;
	}
	const char *tag() const override { return "OSDOverlayScreen"; }

	bool UnsyncTouch(const TouchInput &touch) override;

	void CreateViews() override;
	void DrawForeground(UIContext &ui) override;
	void update() override;

private:
	OnScreenMessagesView *osmView_ = nullptr;
};


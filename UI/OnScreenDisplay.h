#pragma once

#include <string>
#include <list>
#include <mutex>

#include "Common/Math/geom2d.h"
#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/System/System.h"

class DrawBuffer;

// Infrastructure for rendering overlays.

class OnScreenMessagesView : public UI::InertView {
public:
	OnScreenMessagesView(UI::LayoutParams *layoutParams = nullptr) : UI::InertView(layoutParams) {}
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override;
};

class OSDOverlayScreen : public UIScreen {
public:
	const char *tag() const override { return "OSDOverlayScreen"; }
	void CreateViews() override;
};

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
	std::string DescribeText() const override;
};

class OSDOverlayScreen : public UIScreen {
public:
	const char *tag() const override { return "OSDOverlayScreen"; }
	void CreateViews() override;
	void render() override;
};

enum class NoticeLevel {
	SUCCESS,
	INFO,
	WARN,
	ERROR,
};

class NoticeView : public UI::InertView {
public:
	NoticeView(NoticeLevel level, const std::string &text, const std::string &detailsText, UI::LayoutParams *layoutParams = 0)
		: InertView(layoutParams), level_(level), text_(text), detailsText_(detailsText), iconName_("") {}

	void SetIconName(const std::string &name) {
		iconName_ = name;
	}

	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

private:
	std::string text_;
	std::string detailsText_;
	std::string iconName_;
	NoticeLevel level_;
	mutable float height1_ = 0.0f;
};

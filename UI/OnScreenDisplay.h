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
	const char *tag() const override { return "OSDOverlayScreen"; }

	bool UnsyncTouch(const TouchInput &touch) override;

	void CreateViews() override;
	void DrawForeground(UIContext &ui) override;
	void update() override;

private:
	OnScreenMessagesView *osmView_ = nullptr;
};

enum class NoticeLevel {
	SUCCESS,
	INFO,
	WARN,
	ERROR,
};

class NoticeView : public UI::InertView {
public:
	NoticeView(NoticeLevel level, std::string_view text, std::string_view detailsText, UI::LayoutParams *layoutParams = 0)
		: InertView(layoutParams), level_(level), text_(text), detailsText_(detailsText), iconName_("") {}

	void SetIconName(std::string_view name) {
		iconName_ = name;
	}
	void SetText(std::string_view text) {
		text_ = text;
	}
	void SetLevel(NoticeLevel level) {
		level_ = level;
	}
	void SetSquishy(bool squishy) {
		squishy_ = squishy;
	}

	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override;
	void Draw(UIContext &dc) override;

private:
	std::string text_;
	std::string detailsText_;
	std::string iconName_;
	NoticeLevel level_;
	mutable float height1_ = 0.0f;
	bool squishy_ = false;
};

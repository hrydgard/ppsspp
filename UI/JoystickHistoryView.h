#pragma once

#include <deque>

#include "Common/UI/View.h"

enum class StickHistoryViewType {
	INPUT,
	OUTPUT,
	OTHER,
};

class JoystickHistoryView : public UI::InertView {
public:
	JoystickHistoryView(StickHistoryViewType type, std::string_view title, UI::LayoutParams *layoutParams = nullptr)
		: UI::InertView(layoutParams), title_(title), type_(type) {}

	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return "Analog Stick View"; }
	void Update() override;
	void SetXY(float x, float y) {
		curX_ = x;
		curY_ = y;
	}

private:
	struct Location {
		float x;
		float y;
	};

	float curX_ = 0.0f;
	float curY_ = 0.0f;

	std::deque<Location> locations_;
	int maxCount_ = 500;
	std::string title_;
	StickHistoryViewType type_;
};

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Common/UI/View.h"
#include "Common/System/OSD.h"

#undef ERROR

enum class NoticeLevel {
	SUCCESS,
	INFO,
	WARN,
	ERROR,
};

class NoticeView : public UI::InertView {
public:
	NoticeView(NoticeLevel level, std::string_view text, std::string_view detailsText, UI::LayoutParams *layoutParams = 0)
		: InertView(layoutParams), level_(level), text_(text), detailsText_(detailsText) {}

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
	void SetWrapText(bool wrapText) {
		wrapText_ = wrapText;
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
	bool wrapText_ = false;
};

ImageID GetOSDIcon(NoticeLevel level);
NoticeLevel GetNoticeLevel(OSDType type);
uint32_t GetNoticeBackgroundColor(NoticeLevel type);
void MeasureNotice(const UIContext &dc, NoticeLevel level, std::string_view text, std::string_view details, std::string_view iconName, int align, float maxWidth, float *width, float *height, float *height1);
void RenderNotice(UIContext &dc, Bounds bounds, float height1, NoticeLevel level, std::string_view text, std::string_view details, std::string_view iconName, int align, float alpha, OSDMessageFlags flags, float timeVal);

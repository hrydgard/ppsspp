#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/System/Request.h"
#include "Common/System/Display.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "UI/MiscViews.h"

TextWithImage::TextWithImage(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams) {
	using namespace UI;
	SetSpacing(8.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
	if (imageID.isValid()) {
		Add(new ImageView(imageID, "", UI::IS_DEFAULT, new LinearLayoutParams(0.0f, UI::Gravity::G_VCENTER)));
	}
	Add(new TextView(text, new LinearLayoutParams(1.0f, UI::Gravity::G_VCENTER)));
}

CopyableText::CopyableText(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams) {
	using namespace UI;
	SetSpacing(8.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
	if (imageID.isValid()) {
		Add(new ImageView(imageID, "", UI::IS_DEFAULT, new LinearLayoutParams(0.0f, UI::Gravity::G_VCENTER)));
	}
	Add(new TextView(text, new LinearLayoutParams(1.0f, UI::Gravity::G_VCENTER)))->SetBig(true);

	std::string textStr(text);  // We need to store the text in the lambda context.
	Add(new Choice(ImageID("I_FILE_COPY"), new LinearLayoutParams()))->OnClick.Add([textStr](UI::EventParams &) {
		System_CopyStringToClipboard(textStr);
	});
}

TopBar::TopBar(const UIContext &ctx, bool usePortraitLayout, std::string_view title, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams) {
	using namespace UI;
	SetSpacing(10.0f);
	if (!layoutParams) {
		layoutParams_->width = UI::FILL_PARENT;
		layoutParams_->height = 64.0f;
	}

	auto dlg = GetI18NCategory(I18NCat::DIALOG);
	backButton_ = Add(new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams()));
	backButton_ ->OnClick.Add([](UI::EventParams &e) {
		e.bubbleResult = DR_BACK;
	});
	if (!usePortraitLayout) {
		backButton_->SetText(dlg->T("Back"));
	}

	if (!title.empty()) {
		TextView *titleView = Add(new TextView(title, ALIGN_VCENTER | FLAG_WRAP_TEXT, false, new LinearLayoutParams(1.0f, G_VCENTER)));
		titleView->SetTextColor(ctx.GetTheme().itemDownStyle.fgColor);
		// If using HCENTER, to balance the centering, add a spacer on the right.
		// Add(new Spacer(50.0f));
	}
	SetBG(ctx.GetTheme().itemDownStyle.background);
}

SettingInfoMessage::SettingInfoMessage(int align, float cutOffY, UI::AnchorLayoutParams *lp)
	: UI::LinearLayout(ORIENT_HORIZONTAL, lp), cutOffY_(cutOffY) {
	using namespace UI;
	SetSpacing(0.0f);
	Add(new Spacer(10.0f));
	text_ = Add(new TextView("", align, false, new LinearLayoutParams(1.0, Margins(0, 10))));
	Add(new Spacer(10.0f));
}

void SettingInfoMessage::Show(std::string_view text, const UI::View *refView) {
	if (refView) {
		Bounds b = refView->GetBounds();
		const UI::AnchorLayoutParams *lp = GetLayoutParams()->As<UI::AnchorLayoutParams>();
		if (lp) {
			if (cutOffY_ != -1.0f && b.y >= cutOffY_) {
				ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, 80.0f, lp->right, lp->bottom, lp->center));
			} else {
				ReplaceLayoutParams(new UI::AnchorLayoutParams(lp->width, lp->height, lp->left, g_display.dp_yres - 80.0f - 40.0f, lp->right, lp->bottom, lp->center));
			}
		}
	}
	if (text_) {
		text_->SetText(text);
	}
	timeShown_ = time_now_d();
}

void SettingInfoMessage::Draw(UIContext &dc) {
	static const double FADE_TIME = 1.0;
	static const float MAX_ALPHA = 0.9f;

	// Let's show longer messages for more time (guesstimate at reading speed.)
	// Note: this will give multibyte characters more time, but they often have shorter words anyway.
	double timeToShow = std::max(1.5, text_->GetText().size() * 0.05);

	double sinceShow = time_now_d() - timeShown_;
	float alpha = MAX_ALPHA;
	if (timeShown_ == 0.0 || sinceShow > timeToShow + FADE_TIME) {
		alpha = 0.0f;
	} else if (sinceShow > timeToShow) {
		alpha = MAX_ALPHA - MAX_ALPHA * (float)((sinceShow - timeToShow) / FADE_TIME);
	}

	UI::Style style = dc.GetTheme().tooltipStyle;

	if (alpha >= 0.001f) {
		uint32_t bgColor = alphaMul(style.background.color, alpha);
		dc.FillRect(UI::Drawable(bgColor), bounds_);
	}

	uint32_t textColor = alphaMul(style.fgColor, alpha);
	text_->SetTextColor(textColor);
	ViewGroup::Draw(dc);
	showing_ = sinceShow <= timeToShow; // Don't consider fade time
}

std::string SettingInfoMessage::GetText() const {
	return (showing_ && text_) ? text_->GetText() : "";
}

void ShinyIcon::Draw(UIContext &dc) {
	UI::DrawIconShine(dc, bounds_, 1.0f, animated_);
	UI::ImageView::Draw(dc);
}

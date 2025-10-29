#pragma once

#include "Common/UI/View.h"
#include "UI/ViewGroup.h"

// Compound view, showing a text with an icon.
class TextWithImage : public UI::LinearLayout {
public:
	TextWithImage(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams = nullptr);
};

// Compound view, showing a copyable string.
class CopyableText : public UI::LinearLayout {
public:
	CopyableText(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams = nullptr);
};

class TopBar : public UI::LinearLayout {
public:
	// The context is needed to get the theme for the background.
	TopBar(const UIContext &ctx, bool usePortraitLayout, std::string_view title, UI::LayoutParams *layoutParams = nullptr);
	UI::View *GetBackButton() const { return backButton_; }

private:
	UI::Choice *backButton_ = nullptr;
};

class SettingInfoMessage : public UI::LinearLayout {
public:
	SettingInfoMessage(int align, float cutOffY, UI::AnchorLayoutParams *lp);

	void Show(std::string_view text, const UI::View *refView = nullptr);

	void Draw(UIContext &dc) override;
	std::string GetText() const;

private:
	UI::TextView *text_ = nullptr;
	double timeShown_ = 0.0;
	float cutOffY_;
	bool showing_ = false;
};

class ShinyIcon : public UI::ImageView {
public:
	ShinyIcon(ImageID atlasImage, UI::LayoutParams *layoutParams = 0) : UI::ImageView(atlasImage, "", UI::IS_DEFAULT, layoutParams) {}
	void Draw(UIContext &dc) override;
	void SetAnimated(bool anim) { animated_ = anim; }
private:
	bool animated_ = true;
};

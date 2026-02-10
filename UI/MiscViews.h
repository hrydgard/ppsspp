#pragma once

#include "Common/Common.h"
#include "Common/UI/View.h"
#include "UI/ViewGroup.h"
#include "UI/GameInfoCache.h"

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

enum class TopBarFlags {
	Default = 0,
	Portrait = 1,
	ContextMenuButton = 2,
	NoBackButton = 4,
};
ENUM_CLASS_BITOPS(TopBarFlags);

class TopBar : public UI::LinearLayout {
public:
	// The context is needed to get the theme for the background.
	TopBar(const UIContext &ctx, TopBarFlags flags, std::string_view title, UI::LayoutParams *layoutParams = nullptr);
	UI::View *GetBackButton() const { return backButton_; }
	UI::View *GetContextMenuButton() const { return contextMenuButton_; }

	UI::Event OnContextMenuClick;

private:
	UI::Choice *backButton_ = nullptr;
	UI::Choice *contextMenuButton_ = nullptr;
	TopBarFlags flags_ = TopBarFlags::Default;
};

class ShinyIcon : public UI::ImageView {
public:
	ShinyIcon(ImageID atlasImage, UI::LayoutParams *layoutParams = 0) : UI::ImageView(atlasImage, "", UI::IS_DEFAULT, layoutParams) {}
	void Draw(UIContext &dc) override;
	void SetAnimated(bool anim) { animated_ = anim; }
private:
	bool animated_ = true;
};

// Title at the top of a scrolling pane of settings. May later include a back button, but currently doesn't.
// The gamePath_ is used to draw the icon of the game who's settings are currently being edited.
// settingsCategory is used to build the help URL.
class PaneTitleBar : public UI::LinearLayout {
public:
	PaneTitleBar(const Path &gamePath, std::string_view title, const std::string_view settingsCategory, UI::LayoutParams *layoutParams = nullptr);

private:
	Path gamePath_;
};

class GameImageView : public UI::InertView {
public:
	GameImageView(const Path &gamePath, GameInfoFlags image, float scale, UI::LayoutParams *layoutParams = 0)
		: InertView(layoutParams), image_(image), gamePath_(gamePath), scale_(scale) {
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return ""; }

private:
	GameInfoTex *GetTex(std::shared_ptr<GameInfo> info) const;

	Path gamePath_;
	GameInfoFlags image_;
	float scale_ = 1.0f;
};

class GameInfoBGView : public UI::InertView {
public:
	GameInfoBGView(const Path &gamePath, UI::LayoutParams *layoutParams) : InertView(layoutParams), gamePath_(gamePath) {}

	void Draw(UIContext &dc) override;
	std::string DescribeText() const override { return ""; }
	void SetColor(uint32_t c) { color_ = c; }

protected:
	Path gamePath_;
	uint32_t color_ = 0xFFC0C0C0;
};

void AddRotationPicker(ScreenManager *screenManager, UI::ViewGroup *parent, bool text);

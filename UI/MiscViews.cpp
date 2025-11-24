#include "Common/UI/View.h"
#include "Common/UI/Context.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/System/Request.h"
#include "Common/System/Display.h"
#include "Common/TimeUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Math/curves.h"
#include "Common/StringUtils.h"
#include "UI/MiscViews.h"
#include "UI/GameInfoCache.h"
#include "Core/Config.h"

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

TopBar::TopBar(const UIContext &ctx, TopBarFlags flags, std::string_view title, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams), flags_(flags) {
	using namespace UI;
	SetSpacing(10.0f);
	if (!layoutParams) {
		layoutParams_->width = UI::FILL_PARENT;
		layoutParams_->height = 64.0f;
	}

	auto dlg = GetI18NCategory(I18NCat::DIALOG);
	backButton_ = Add(new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)));
	backButton_->OnClick.Add([](UI::EventParams &e) {
		e.bubbleResult = DR_BACK;
	});

	if (!title.empty()) {
		TextView *titleView = Add(new TextView(title, ALIGN_VCENTER, false, new LinearLayoutParams(1.0f, Gravity::G_VCENTER)));
		titleView->SetTextColor(ctx.GetTheme().itemDownStyle.fgColor);
		titleView->SetBig(true);
		// If using HCENTER, to balance the centering, add a spacer on the right.
		// Add(new Spacer(50.0f));
	}

	if (flags & TopBarFlags::ContextMenuButton) {
		contextMenuButton_ = Add(new Choice(ImageID("I_THREE_DOTS"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)));
		contextMenuButton_->OnClick.Add([this](UI::EventParams &e) {
			this->OnContextMenuClick.Trigger(e);
		});
	}
}

void ShinyIcon::Draw(UIContext &dc) {
	UI::DrawIconShine(dc, bounds_, 1.0f, animated_);
	UI::ImageView::Draw(dc);
}

class SimpleGameIconView : public UI::InertView {
public:
	SimpleGameIconView(const Path &gamePath, UI::LayoutParams *layoutParams = 0)
		: InertView(layoutParams), gamePath_(gamePath) {
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		h = UI::ITEM_HEIGHT;
		float aspect = 1.0f;
		if (textureHeight_ > 0) {
			aspect = static_cast<float>(textureWidth_) / static_cast<float>(textureHeight_);
		}
		w = h * aspect;
	}

	void Draw(UIContext &dc) override {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, GameInfoFlags::ICON);
		if (!info->Ready(GameInfoFlags::ICON) || !info->icon.texture) {
			return;
		}

		Draw::Texture *texture = info->icon.texture;

		textureWidth_ = texture->Width();
		textureHeight_ = texture->Height();

		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, texture);
		dc.Draw()->Rect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, 0xFFFFFFFF);
		dc.Flush();
		dc.RebindTexture();

		// Draw the gear icon
		const AtlasImage *gearImage = dc.Draw()->GetAtlas()->getImage(ImageID("I_GEAR_SMALL"));
		dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x + 1, bounds_.y2() - gearImage->h - 1, 1.0f);
	}

	std::string DescribeText() const override { return ""; }

private:
	Path gamePath_;
	int textureWidth_ = 0;
	int textureHeight_ = 0;
};

PaneTitleBar::PaneTitleBar(const Path &gamePath, std::string_view title, const std::string_view settingsCategory, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams), gamePath_(gamePath) {
	using namespace UI;
	SetSpacing(10.0f);
	if (!layoutParams) {
		layoutParams_->width = UI::FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}

	auto dlg = GetI18NCategory(I18NCat::DIALOG);

	if (!title.empty()) {
		SimpleTextView *titleView = Add(new SimpleTextView(title, new LinearLayoutParams(0.0f, Gravity::G_VCENTER, Margins(10, 0, 20, 0))));
		titleView->SetBig(true);
		// If using HCENTER, to balance the centering, add a spacer on the right.
	}

	Add(new Spacer(10.0f, new LinearLayoutParams(1.0f)));

	// Now add the game icon.
	if (!gamePath.empty() && g_Config.IsGameSpecific()) {
		Add(new SimpleGameIconView(gamePath_, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	}

	if (!settingsCategory.empty()) {
		std::string settingsUrl;
		if (settingsCategory[0] == '/') {
			settingsUrl = join("https://www.ppsspp.org", settingsCategory);
		} else {
			settingsUrl = join("https://www.ppsspp.org/docs/settings/", settingsCategory);
		}
		Choice *helpButton = Add(new Choice(ImageID("I_INFO"), new LinearLayoutParams()));
		helpButton->OnClick.Add([settingsUrl](UI::EventParams &) {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, settingsUrl);
		});
	}
}

void GameImageView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = textureWidth_;
	h = textureHeight_;
}

void GameImageView::Draw(UIContext &dc) {
	using namespace UI;
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, image_);
	if (!info->Ready(image_) || !info->icon.texture) {
		return;
	}

	Draw::Texture *texture = nullptr;
	switch (image_) {
	case GameInfoFlags::ICON:
		texture = info->icon.texture;
		break;
	case GameInfoFlags::PIC0:
		texture = info->pic0.texture;
		break;
	case GameInfoFlags::PIC1:
		texture = info->pic1.texture;
		break;
	}

	if (!texture) {
		return;
	}

	textureWidth_ = texture->Width() * scale_;
	textureHeight_ = texture->Height() * scale_;

	// Fade icon with the backgrounds.
	double loadTime = info->icon.timeLoaded;
	auto pic = info->GetPIC1();
	if (pic) {
		loadTime = std::max(loadTime, pic->timeLoaded);
	}
	uint32_t color = whiteAlpha(ease((time_now_d() - loadTime) * 3));

	// Adjust size so we don't stretch the image vertically or horizontally.
	// Make sure it's not wider than 144 (like Doom Legacy homebrew), ugly in the grid mode.
	float nw = std::min(bounds_.h * textureWidth_ / textureHeight_, (float)bounds_.w);
	int x = bounds_.x + (bounds_.w - nw) / 2.0f;

	dc.Flush();
	dc.GetDrawContext()->BindTexture(0, texture);
	dc.Draw()->Rect(x, bounds_.y, nw, bounds_.h, color);
	dc.Flush();
	dc.RebindTexture();
}

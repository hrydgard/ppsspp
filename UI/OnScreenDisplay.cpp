#include <algorithm>
#include <sstream>

#include "UI/OnScreenDisplay.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Math/math_util.h"

#include "Common/UI/Context.h"
#include "Common/System/System.h"

#include "Common/TimeUtil.h"
#include "Common/Net/HTTPClient.h"
#include "Core/Config.h"

static uint32_t GetOSDBackgroundColor(OSDType type) {
	// Colors from Infima
	switch (type) {
	case OSDType::MESSAGE_ERROR:
	case OSDType::MESSAGE_ERROR_DUMP: return 0x3530d5;  // danger-darker
	case OSDType::MESSAGE_WARNING: return 0x009ed9;  // warning-darker
	case OSDType::MESSAGE_INFO: return 0x706760;  // gray-700
	case OSDType::MESSAGE_SUCCESS: return 0x008b00;
	default: return 0x606770;
	}
}

ImageID GetOSDIcon(OSDType type) {
	switch (type) {
	case OSDType::MESSAGE_INFO: return ImageID::invalid(); //  return ImageID("I_INFO");
	case OSDType::MESSAGE_ERROR: return ImageID("I_CROSS");
	case OSDType::MESSAGE_WARNING: return ImageID("I_WARNING");
	case OSDType::MESSAGE_SUCCESS: return ImageID("I_CHECKEDBOX");
	default: return ImageID::invalid();
	}
}

static const float iconSize = 36.0f;

static const float extraTextScale = 0.7f;

// Align only matters here for the ASCII-only flag.
static void MeasureOSDEntry(UIContext &dc, const OnScreenDisplay::Entry &entry, int align, float *width, float *height, float *height1) {
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, entry.text.c_str(), width, height, align);
	*height1 = *height;

	float width2 = 0.0f, height2 = 0.0f;
	if (!entry.text2.empty()) {
		dc.MeasureText(dc.theme->uiFont, extraTextScale, extraTextScale, entry.text2.c_str(), &width2, &height2, align);
		*width = std::max(*width, width2);
		*height += 5.0f + height2;
	}

	if (!GetOSDIcon(entry.type).isInvalid()) {
		*width += iconSize + 5.0f;
	}

	*width += 12.0f;
	*height = std::max(*height, iconSize + 5.0f);
}

static void RenderOSDEntry(UIContext &dc, const OnScreenDisplay::Entry &entry, Bounds bounds, float height1, int align, float alpha) {
	UI::Drawable background = UI::Drawable(colorAlpha(GetOSDBackgroundColor(entry.type), alpha));

	uint32_t foreGround = whiteAlpha(alpha);

	Bounds shadowBounds = bounds.Expand(10.0f);

	dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, shadowBounds.x, shadowBounds.y + 4.0f, shadowBounds.x2(), shadowBounds.y2(), alphaMul(0xFF000000, 0.9f * alpha), 1.0f);

	dc.FillRect(background, bounds);
	dc.SetFontStyle(dc.theme->uiFont);

	ImageID iconID = GetOSDIcon(entry.type);

	if (iconID.isValid()) {
		dc.DrawImageVGradient(iconID, foreGround, foreGround, Bounds(bounds.x + 2.5f, bounds.y + 2.5f, iconSize, iconSize));

		// Make room
		bounds.x += iconSize + 5.0f;
		bounds.w -= iconSize + 5.0f;
	}

	dc.DrawTextShadowRect(entry.text.c_str(), bounds.Inset(0.0f, 1.0f, 0.0f, 0.0f), colorAlpha(0xFFFFFFFF, alpha), (align & FLAG_DYNAMIC_ASCII));

	if (!entry.text2.empty()) {
		Bounds bottomTextBounds = bounds.Inset(3.0f, height1 + 5.0f, 3.0f, 3.0f);
		UI::Drawable backgroundDark = UI::Drawable(colorAlpha(darkenColor(GetOSDBackgroundColor(entry.type)), alpha));
		dc.FillRect(backgroundDark, bottomTextBounds);
		dc.SetFontScale(extraTextScale, extraTextScale);
		dc.DrawTextRect(entry.text2.c_str(), bottomTextBounds, colorAlpha(0xFFFFFFFF, alpha), (align & FLAG_DYNAMIC_ASCII) | ALIGN_LEFT);
		dc.SetFontScale(1.0f, 1.0f);
	}
}

static void MeasureOSDProgressBar(UIContext &dc, const OnScreenDisplay::ProgressBar &bar, float *width, float *height) {
	*height = 36;
	*width = 450.0f;
}

static void RenderOSDProgressBar(UIContext &dc, const OnScreenDisplay::ProgressBar &entry, Bounds bounds, int align, float alpha) {
	uint32_t foreGround = whiteAlpha(alpha);

	Bounds shadowBounds = bounds.Expand(10.0f);

	dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, shadowBounds.x, shadowBounds.y + 4.0f, shadowBounds.x2(), shadowBounds.y2(), alphaMul(0xFF000000, 0.9f * alpha), 1.0f);

	uint32_t backgroundColor = colorAlpha(0x806050, alpha);
	uint32_t progressBackgroundColor = colorAlpha(0xa08070, alpha);

	if (entry.maxValue > entry.minValue) {
		// Normal progress bar

		UI::Drawable background = UI::Drawable(backgroundColor);
		UI::Drawable progressBackground = UI::Drawable(progressBackgroundColor);

		float ratio = (float)(entry.progress - entry.minValue) / (float)entry.maxValue;

		Bounds boundLeft = bounds;
		Bounds boundRight = bounds;

		boundLeft.w *= ratio;
		boundRight.x += ratio * boundRight.w;
		boundRight.w *= (1.0f - ratio);

		dc.FillRect(progressBackground, boundLeft);
		dc.FillRect(background, boundRight);
	} else {
		// Indeterminate spinner
		float alpha = cos(time_now_d() * 5.0) * 0.5f + 0.5f;
		uint32_t pulse = colorBlend(backgroundColor, progressBackgroundColor, alpha);
		UI::Drawable background = UI::Drawable(pulse);
		dc.FillRect(background, bounds);
	}

	dc.SetFontStyle(dc.theme->uiFont);

	dc.DrawTextShadowRect(entry.message.c_str(), bounds, colorAlpha(0xFFFFFFFF, alpha), (align & FLAG_DYNAMIC_ASCII) | ALIGN_CENTER);
}

void OnScreenMessagesView::Draw(UIContext &dc) {
	if (!g_Config.bShowOnScreenMessages) {
		return;
	}

	double now = time_now_d();

	// Get height
	float w, h;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, "Wg", &w, &h);

	float y = 10.0f;
	// Then draw them all. 
	const std::vector<OnScreenDisplay::ProgressBar> bars = g_OSD.ProgressBars();
	for (auto &bar : bars) {
		float tw, th;
		MeasureOSDProgressBar(dc, bar, &tw, &th);
		Bounds b(0.0f, y, tw, th);
		b.x = (bounds_.w - b.w) * 0.5f;

		float alpha = Clamp((float)(bar.endTime - now) * 4.0f, 0.0f, 1.0f);
		RenderOSDProgressBar(dc, bar, b, 0, alpha);
		y += (b.h + 4.0f) * alpha;  // including alpha here gets us smooth animations.
	}

	const std::vector<OnScreenDisplay::Entry> entries = g_OSD.Entries();
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		dc.SetFontScale(1.0f, 1.0f);
		// Messages that are wider than the screen are left-aligned instead of centered.

		int align = 0;
		// If we have newlines, we may be looking at ASCII debug output.  But let's verify.
		if (iter->text.find('\n') != 0) {
			if (!UTF8StringHasNonASCII(iter->text.c_str()))
				align |= FLAG_DYNAMIC_ASCII;
		}

		float tw, th, h1;
		MeasureOSDEntry(dc, *iter, align, &tw, &th, &h1);

		Bounds b(0.0f, y, tw, th);

		if (tw > bounds_.w) {
			// Left-aligned
			b.x = 2;
		} else {
			// Centered
			b.x = (bounds_.w - b.w) * 0.5f;
		}

		// Scale down if height doesn't fit.
		float scale = 1.0f;
		if (th > bounds_.h - y) {
			// Scale down!
			scale = std::max(0.15f, (bounds_.h - y) / th);
			dc.SetFontScale(scale, scale);
			b.w *= scale;
			b.h *= scale;
		}

		float alpha = Clamp((float)(iter->endTime - now) * 4.0f, 0.0f, 1.0f);
		RenderOSDEntry(dc, *iter, b, h1, align, alpha);
		y += (b.h * scale + 4.0f) * alpha;  // including alpha here gets us smooth animations.
	}

	// Thin bar at the top of the screen.
	std::vector<float> progress = g_DownloadManager.GetCurrentProgress();
	if (!progress.empty()) {
		static const uint32_t colors[4] = {
			0xFFFFFFFF,
			0xFFCCCCCC,
			0xFFAAAAAA,
			0xFF777777,
		};

		dc.Begin();
		int h = 5;
		for (size_t i = 0; i < progress.size(); i++) {
			float barWidth = 10 + (dc.GetBounds().w - 10) * progress[i];
			Bounds bounds(0, h * i, barWidth, h);
			UI::Drawable solid(colors[i & 3]);
			dc.FillRect(solid, bounds);
		}
		dc.Flush();
	}
}

std::string OnScreenMessagesView::DescribeText() const {
	std::stringstream ss;
	const auto &entries = g_OSD.Entries();
	for (auto iter = entries.begin(); iter != entries.end(); ++iter) {
		if (iter != entries.begin()) {
			ss << "\n";
		}
		ss << iter->text;
	}
	return ss.str();
}

void OSDOverlayScreen::CreateViews() {
	root_ = new UI::AnchorLayout();
	root_->Add(new OnScreenMessagesView(new UI::AnchorLayoutParams(0.0f, 0.0f, 0.0f, 0.0f)));
}

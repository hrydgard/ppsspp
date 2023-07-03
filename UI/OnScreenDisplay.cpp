#include <algorithm>
#include <sstream>

#include "UI/OnScreenDisplay.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Math/math_util.h"
#include "Common/UI/IconCache.h"
#include "UI/RetroAchievementScreens.h"

#include "Common/UI/Context.h"
#include "Common/System/OSD.h"

#include "Common/TimeUtil.h"
#include "Common/Net/HTTPClient.h"
#include "Core/Config.h"

static const float g_atlasIconSize = 36.0f;
static const float extraTextScale = 0.7f;

static uint32_t GetNoticeBackgroundColor(NoticeLevel type) {
	// Colors from Infima
	switch (type) {
	case NoticeLevel::ERROR: return 0x3530d5;  // danger-darker
	case NoticeLevel::WARN: return 0x009ed9;  // warning-darker
	case NoticeLevel::INFO: return 0x706760;  // gray-700
	case NoticeLevel::SUCCESS: return 0x008b00;  // nice green
	default: return 0x606770;
	}
}

static ImageID GetOSDIcon(NoticeLevel level) {
	switch (level) {
	case NoticeLevel::INFO: return ImageID::invalid(); //  return ImageID("I_INFO");
	case NoticeLevel::ERROR: return ImageID("I_CROSS");
	case NoticeLevel::WARN: return ImageID("I_WARNING");
	case NoticeLevel::SUCCESS: return ImageID("I_CHECKEDBOX");
	default: return ImageID::invalid();
	}
}

static NoticeLevel GetNoticeLevel(OSDType type) {
	switch (type) {
	case OSDType::MESSAGE_INFO: return NoticeLevel::INFO;
	case OSDType::MESSAGE_ERROR:
	case OSDType::MESSAGE_ERROR_DUMP: return NoticeLevel::ERROR;
	case OSDType::MESSAGE_WARNING: return NoticeLevel::WARN;
	case OSDType::MESSAGE_SUCCESS: return NoticeLevel::SUCCESS;
	default: return NoticeLevel::SUCCESS;
	}
}

// Align only matters here for the ASCII-only flag.
static void MeasureNotice(const UIContext &dc, NoticeLevel level, const std::string &text, const std::string &details, const std::string &iconName, int align, float *width, float *height, float *height1) {
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, text.c_str(), width, height, align);

	*height1 = *height;

	float width2 = 0.0f, height2 = 0.0f;
	if (!details.empty()) {
		dc.MeasureText(dc.theme->uiFont, extraTextScale, extraTextScale, details.c_str(), &width2, &height2, align);
		*width = std::max(*width, width2);
		*height += 5.0f + height2;
	}

	float iconSize = 0.0f;

	if (!iconName.empty()) {
		// Normal entry but with a cached icon.
		int iconWidth, iconHeight;
		if (g_iconCache.GetDimensions(iconName, &iconWidth, &iconHeight)) {
			*width += 5.0f + iconWidth;
			iconSize = iconWidth + 5.0f;
		}
	} else if (!GetOSDIcon(level).isInvalid()) {
		// Atlas icon.
		iconSize = g_atlasIconSize + 5.0f;
	}

	*width += iconSize + 12.0f;
	*height = std::max(*height, iconSize + 5.0f);
}

// Align only matters here for the ASCII-only flag.
static void MeasureOSDEntry(const UIContext &dc, const OnScreenDisplay::Entry &entry, int align, float *width, float *height, float *height1) {
	if (entry.type == OSDType::ACHIEVEMENT_UNLOCKED) {
		const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
		MeasureAchievement(dc, achievement, AchievementRenderStyle::UNLOCKED, width, height);
		*width = 550.0f;
		*height1 = *height;
	} else {
		MeasureNotice(dc, GetNoticeLevel(entry.type), entry.text, entry.text2, entry.iconName, align, width, height, height1);
	}
}

static void RenderNotice(UIContext &dc, Bounds bounds, float height1, NoticeLevel level, const std::string &text, const std::string &details, const std::string &iconName, int align, float alpha) {
	UI::Drawable background = UI::Drawable(colorAlpha(GetNoticeBackgroundColor(level), alpha));

	uint32_t foreGround = whiteAlpha(alpha);

	dc.DrawRectDropShadow(bounds, 12.0f, 0.7f * alpha);
	dc.FillRect(background, bounds);

	ImageID iconID = GetOSDIcon(level);

	float iconSize = 0.0f;
	if (!iconName.empty()) {
		dc.Flush();
		// Normal entry but with a cached icon.
		Draw::Texture *texture = g_iconCache.BindIconTexture(&dc, iconName);
		if (texture) {
			iconSize = texture->Width();
			dc.Draw()->DrawTexRect(Bounds(bounds.x + 2.5f, bounds.y + 2.5f, iconSize, iconSize), 0.0f, 0.0f, 1.0f, 1.0f, foreGround);
			dc.Flush();
			dc.RebindTexture();
		}
		dc.Begin();
	} else if (iconID.isValid()) {
		// Atlas icon.
		dc.DrawImageVGradient(iconID, foreGround, foreGround, Bounds(bounds.x + 2.5f, bounds.y + 2.5f, g_atlasIconSize, g_atlasIconSize));
		iconSize = g_atlasIconSize;
	}

	// Make room
	bounds.x += iconSize + 5.0f;
	bounds.w -= iconSize + 5.0f;

	dc.DrawTextShadowRect(text.c_str(), bounds.Inset(0.0f, 1.0f, 0.0f, 0.0f), foreGround, (align & FLAG_DYNAMIC_ASCII));

	if (!details.empty()) {
		Bounds bottomTextBounds = bounds.Inset(3.0f, height1 + 5.0f, 3.0f, 3.0f);
		UI::Drawable backgroundDark = UI::Drawable(colorAlpha(darkenColor(GetNoticeBackgroundColor(level)), alpha));
		dc.FillRect(backgroundDark, bottomTextBounds);
		dc.SetFontScale(extraTextScale, extraTextScale);
		dc.DrawTextRect(details.c_str(), bottomTextBounds, foreGround, (align & FLAG_DYNAMIC_ASCII) | ALIGN_LEFT);
	}
	dc.SetFontScale(1.0f, 1.0f);
}

static void RenderOSDEntry(UIContext &dc, const OnScreenDisplay::Entry &entry, Bounds bounds, float height1, int align, float alpha) {
	if (entry.type == OSDType::ACHIEVEMENT_UNLOCKED) {
		const rc_client_achievement_t * achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
		if (achievement) {
			RenderAchievement(dc, achievement, AchievementRenderStyle::UNLOCKED, bounds, alpha, entry.startTime, time_now_d());
		}
		return;
	} else {
		RenderNotice(dc, bounds, height1, GetNoticeLevel(entry.type), entry.text, entry.text2, entry.iconName, align, alpha);
	}
}

static void MeasureOSDProgressBar(const UIContext &dc, const OnScreenDisplay::ProgressBar &bar, float *width, float *height) {
	*height = 36;
	*width = 450.0f;
}

static void RenderOSDProgressBar(UIContext &dc, const OnScreenDisplay::ProgressBar &entry, Bounds bounds, int align, float alpha) {
	uint32_t foreGround = whiteAlpha(alpha);

	dc.DrawRectDropShadow(bounds, 12.0f, 0.7f * alpha);

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
	dc.SetFontScale(1.0f, 1.0f);

	dc.DrawTextShadowRect(entry.message.c_str(), bounds, colorAlpha(0xFFFFFFFF, alpha), (align & FLAG_DYNAMIC_ASCII) | ALIGN_CENTER);
}

static void MeasureLeaderboardTracker(UIContext &dc, const std::string &text, float *width, float *height) {
	dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, text.c_str(), width, height);
	*width += 10.0f;
	*height += 10.0f;
}

static void RenderLeaderboardTracker(UIContext &dc, const Bounds &bounds, const std::string &text, float alpha) {
	// TODO: Awful color.
	uint32_t backgroundColor = colorAlpha(0x806050, alpha);
	UI::Drawable background = UI::Drawable(backgroundColor);
	dc.DrawRectDropShadow(bounds, 12.0f, 0.7f * alpha);
	dc.FillRect(background, bounds);
	dc.SetFontStyle(dc.theme->uiFont);
	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextShadowRect(text.c_str(), bounds.Inset(5.0f, 5.0f), colorAlpha(0xFFFFFFFF, alpha), ALIGN_VCENTER | ALIGN_LEFT);
}

void OnScreenMessagesView::Draw(UIContext &dc) {
	if (!g_Config.bShowOnScreenMessages) {
		return;
	}

	dc.Flush();

	double now = time_now_d();

	float y = 10.0f;

	const float fadeoutCoef = 1.0f / OnScreenDisplay::FadeoutTime();

	// Draw side entries. Top entries should apply on top of them if there's a collision, so drawing
	// these first makes sense.
	const std::vector<OnScreenDisplay::Entry> sideEntries = g_OSD.SideEntries();
	for (auto &entry : sideEntries) {
		float tw, th;

		const rc_client_achievement_t *achievement = nullptr;
		AchievementRenderStyle style;

		switch (entry.type) {
		case OSDType::ACHIEVEMENT_PROGRESS:
		{
			achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
			if (!achievement)
				continue;
			style = AchievementRenderStyle::PROGRESS_INDICATOR;
			MeasureAchievement(dc, achievement, style, &tw, &th);
			break;
		}
		case OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR:
		{
			achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
			if (!achievement)
				continue;
			style = AchievementRenderStyle::CHALLENGE_INDICATOR;
			MeasureAchievement(dc, achievement, style, &tw, &th);
			break;
		}
		case OSDType::LEADERBOARD_TRACKER:
		{
			MeasureLeaderboardTracker(dc, entry.text, &tw, &th);
			break;
		}
		default:
			continue;
		}
		Bounds b(10.0f, y, tw, th);
		float alpha = Clamp((float)(entry.endTime - now) * fadeoutCoef, 0.0f, 1.0f);
		// OK, render the thing.

		switch (entry.type) {
		case OSDType::ACHIEVEMENT_PROGRESS:
		case OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR:
		{
			RenderAchievement(dc, achievement, style, b, alpha, entry.startTime, now);
			break;
		}
		case OSDType::LEADERBOARD_TRACKER:
			RenderLeaderboardTracker(dc, b, entry.text, alpha);
			break;
		default:
			continue;
		}

		y += (b.h + 4.0f) * alpha;  // including alpha here gets us smooth animations.
	}

	// Get height
	float w, h;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, "Wg", &w, &h);

	y = 10.0f;

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
	for (const auto &entry : entries) {
		dc.SetFontScale(1.0f, 1.0f);
		// Messages that are wider than the screen are left-aligned instead of centered.

		int align = 0;
		// If we have newlines, we may be looking at ASCII debug output.  But let's verify.
		if (entry.text.find('\n') != 0) {
			if (!UTF8StringHasNonASCII(entry.text.c_str()))
				align |= FLAG_DYNAMIC_ASCII;
		}

		float tw, th = 0.0f, h1 = 0.0f;

		switch (entry.type) {
		case OSDType::ACHIEVEMENT_UNLOCKED:
		{
			const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
			if (achievement) {
				MeasureAchievement(dc, achievement, AchievementRenderStyle::UNLOCKED, &tw, &th);
				h1 = th;
			}
			tw = 550.0f;
			break;
		}
		default:
			MeasureOSDEntry(dc, entry, align, &tw, &th, &h1);
			break;
		}

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

		float alpha = Clamp((float)(entry.endTime - now) * 4.0f, 0.0f, 1.0f);
		RenderOSDEntry(dc, entry, b, h1, align, alpha);
		y += (b.h * scale + 4.0f) * alpha;  // including alpha here gets us smooth animations.
	}

	// Thin bar at the top of the screen.
	// TODO: Remove and replace with "proper" progress bars.
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
	root_->SetTag("OSDOverlayScreen");
	root_->Add(new OnScreenMessagesView(new UI::AnchorLayoutParams(0.0f, 0.0f, 0.0f, 0.0f)));
}

void NoticeView::GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const {
	Bounds bounds(0, 0, layoutParams_->width, layoutParams_->height);
	if (bounds.w < 0) {
		// If there's no size, let's grow as big as we want.
		bounds.w = horiz.size;
	}
	if (bounds.h < 0) {
		bounds.h = vert.size;
	}

	ApplyBoundsBySpec(bounds, horiz, vert);
	MeasureNotice(dc, level_, text_, detailsText_, iconName_, 0, &w, &h, &height1_);
}

void NoticeView::Draw(UIContext &dc) {
	RenderNotice(dc, bounds_, height1_, level_, text_, detailsText_, iconName_, 0, 1.0f);
}

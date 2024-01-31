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
#include "UI/DebugOverlay.h"
#include "UI/Root.h"

#include "Common/UI/Context.h"
#include "Common/System/OSD.h"

#include "Common/TimeUtil.h"
#include "Common/Net/HTTPClient.h"
#include "Core/Config.h"

static inline const char *DeNull(const char *ptr) {
	return ptr ? ptr : "";
}

extern bool g_TakeScreenshot;

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
	case NoticeLevel::INFO: return ImageID("I_INFO");
	case NoticeLevel::ERROR: return ImageID("I_CROSS");
	case NoticeLevel::WARN: return ImageID("I_WARNING");
	case NoticeLevel::SUCCESS: return ImageID("I_CHECKMARK");
	default: return ImageID::invalid();
	}
}

static NoticeLevel GetNoticeLevel(OSDType type) {
	switch (type) {
	case OSDType::MESSAGE_INFO:
		return NoticeLevel::INFO;
	case OSDType::MESSAGE_ERROR:
	case OSDType::MESSAGE_ERROR_DUMP:
	case OSDType::MESSAGE_CENTERED_ERROR:
		return NoticeLevel::ERROR;
	case OSDType::MESSAGE_WARNING:
	case OSDType::MESSAGE_CENTERED_WARNING:
		return NoticeLevel::WARN;
	case OSDType::MESSAGE_SUCCESS:
		return NoticeLevel::SUCCESS;
	default:
		return NoticeLevel::SUCCESS;
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

	float iconW = 0.0f;
	float iconH = 0.0f;
	if (!iconName.empty() && !startsWith(iconName, "I_")) {  // Check for atlas image. Bit hacky, but we choose prefixes for icon IDs anyway in a way that this is safe.
		// Normal entry but with a cached icon.
		int iconWidth, iconHeight;
		if (g_iconCache.GetDimensions(iconName, &iconWidth, &iconHeight)) {
			*width += 5.0f + iconWidth;
			iconW = iconWidth;
			iconH = iconHeight;
		}
	} else {
		ImageID iconID = iconName.empty() ? GetOSDIcon(level) : ImageID(iconName.c_str());
		if (iconID.isValid()) {
			dc.Draw()->GetAtlas()->measureImage(iconID, &iconW, &iconH);
		}
	}

	iconW += 5.0f;

	*width += iconW + 12.0f;
	*height = std::max(*height, iconH + 5.0f);
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

	float iconW = 0.0f;
	float iconH = 0.0f;
	if (!iconName.empty() && !startsWith(iconName, "I_")) {
		dc.Flush();
		// Normal entry but with a cached icon.
		Draw::Texture *texture = g_iconCache.BindIconTexture(&dc, iconName);
		if (texture) {
			iconW = texture->Width();
			iconH = texture->Height();
			dc.Draw()->DrawTexRect(Bounds(bounds.x + 2.5f, bounds.y + 2.5f, iconW, iconH), 0.0f, 0.0f, 1.0f, 1.0f, foreGround);
			dc.Flush();
			dc.RebindTexture();
		}
		dc.Begin();
	} else {
		ImageID iconID = iconName.empty() ? GetOSDIcon(level) : ImageID(iconName.c_str());
		if (iconID.isValid()) {
			// Atlas icon.
			dc.Draw()->GetAtlas()->measureImage(iconID, &iconW, &iconH);
			if (!iconName.empty()) {
				Bounds iconBounds = Bounds(bounds.x + 2.5f, bounds.y + 2.5f, iconW, iconH);
				// If it's not a preset OSD icon, give it some background to blend in. The RA icon for example
				// easily melts into the orange of warnings otherwise.
				dc.FillRect(UI::Drawable(0x50000000), iconBounds.Expand(2.0f));
			}
			dc.DrawImageVGradient(iconID, foreGround, foreGround, Bounds(bounds.x + 2.5f, bounds.y + 2.5f, iconW, iconH));
		}
	}

	// Make room
	bounds.x += iconW + 5.0f;
	bounds.w -= iconW + 5.0f;

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
			RenderAchievement(dc, achievement, AchievementRenderStyle::UNLOCKED, bounds, alpha, entry.startTime, time_now_d(), false);
		}
		return;
	} else {
		RenderNotice(dc, bounds, height1, GetNoticeLevel(entry.type), entry.text, entry.text2, entry.iconName, align, alpha);
	}
}

static void MeasureOSDProgressBar(const UIContext &dc, const OnScreenDisplay::Entry &bar, float *width, float *height) {
	*height = 36;
	*width = 450.0f;
}

static void RenderOSDProgressBar(UIContext &dc, const OnScreenDisplay::Entry &entry, Bounds bounds, int align, float alpha) {
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

	dc.DrawTextShadowRect(entry.text.c_str(), bounds, colorAlpha(0xFFFFFFFF, alpha), (align & FLAG_DYNAMIC_ASCII) | ALIGN_CENTER);
}

static void MeasureLeaderboardTracker(UIContext &dc, const std::string &text, float *width, float *height) {
	dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, text.c_str(), width, height);
	*width += 16.0f;
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
	dc.DrawTextShadowRect(text.c_str(), bounds.Inset(5.0f, 5.0f), colorAlpha(0xFFFFFFFF, alpha), ALIGN_VCENTER | ALIGN_HCENTER);
}

void OnScreenMessagesView::Draw(UIContext &dc) {
	if (!g_Config.bShowOnScreenMessages || g_TakeScreenshot) {
		return;
	}

	dc.Flush();

	double now = time_now_d();

	const float padding = 5.0f;

	const float fadeinCoef = 1.0f / OnScreenDisplay::FadeinTime();
	const float fadeoutCoef = 1.0f / OnScreenDisplay::FadeoutTime();

	float sidebarAlpha = g_OSD.SidebarAlpha();

	struct LayoutEdge {
		float height;
		float maxWidth;
		float alpha;
	};

	struct MeasuredEntry {
		float w;
		float h;
		float h1;
		float alpha;
		int align;
		int align2;
		AchievementRenderStyle style;
	};

	// Grab all the entries. Makes a copy so we can release the lock ASAP.
	const std::vector<OnScreenDisplay::Entry> entries = g_OSD.Entries();

	std::vector<MeasuredEntry> measuredEntries;
	measuredEntries.resize(entries.size());

	// Indexed by the enum ScreenEdgePosition.
	LayoutEdge edges[(size_t)ScreenEdgePosition::VALUE_COUNT]{};
	for (size_t i = 0; i < (size_t)ScreenEdgePosition::VALUE_COUNT; i++) {
		edges[i].alpha = sidebarAlpha;
	}
	edges[(size_t)ScreenEdgePosition::TOP_CENTER].alpha = 1.0f;

	ScreenEdgePosition typeEdges[(size_t)OSDType::VALUE_COUNT]{};
	// Default to top.
	for (int i = 0; i < (size_t)OSDType::VALUE_COUNT; i++) {
		typeEdges[i] = ScreenEdgePosition::TOP_CENTER;
	}

	typeEdges[(size_t)OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR] = (ScreenEdgePosition)g_Config.iAchievementsChallengePos;
	typeEdges[(size_t)OSDType::ACHIEVEMENT_PROGRESS] = (ScreenEdgePosition)g_Config.iAchievementsProgressPos;
	typeEdges[(size_t)OSDType::LEADERBOARD_TRACKER] = (ScreenEdgePosition)g_Config.iAchievementsLeaderboardTrackerPos;
	typeEdges[(size_t)OSDType::LEADERBOARD_STARTED_FAILED] = (ScreenEdgePosition)g_Config.iAchievementsLeaderboardStartedOrFailedPos;
	typeEdges[(size_t)OSDType::LEADERBOARD_SUBMITTED] = (ScreenEdgePosition)g_Config.iAchievementsLeaderboardSubmittedPos;
	typeEdges[(size_t)OSDType::ACHIEVEMENT_UNLOCKED] = (ScreenEdgePosition)g_Config.iAchievementsUnlockedPos;
	typeEdges[(size_t)OSDType::MESSAGE_CENTERED_WARNING] = ScreenEdgePosition::CENTER;
	typeEdges[(size_t)OSDType::MESSAGE_CENTERED_ERROR] = ScreenEdgePosition::CENTER;

	dc.SetFontScale(1.0f, 1.0f);

	// First pass: Measure all the sides.
	for (size_t i = 0; i < entries.size(); i++) {
		const auto &entry = entries[i];
		auto &measuredEntry = measuredEntries[i];

		ScreenEdgePosition pos = typeEdges[(size_t)entry.type];
		if (pos == ScreenEdgePosition::VALUE_COUNT || pos == (ScreenEdgePosition)-1) {
			// NONE.
			continue;
		}

		measuredEntry.align = 0;
		measuredEntry.align2 = 0;
		// If we have newlines, we may be looking at ASCII debug output.  But let's verify.
		if (entry.text.find('\n') != std::string::npos) {
			if (!UTF8StringHasNonASCII(entry.text.c_str()))
				measuredEntry.align |= FLAG_DYNAMIC_ASCII;
		}
		if (entry.text2.find('\n') != std::string::npos) {
			if (!UTF8StringHasNonASCII(entry.text2.c_str()))
				measuredEntry.align2 |= FLAG_DYNAMIC_ASCII;
		}

		switch (entry.type) {
		case OSDType::ACHIEVEMENT_PROGRESS:
		{
			const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
			if (!achievement)
				continue;
			measuredEntry.style = AchievementRenderStyle::PROGRESS_INDICATOR;
			MeasureAchievement(dc, achievement, measuredEntry.style, &measuredEntry.w, &measuredEntry.h);
			break;
		}
		case OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR:
		{
			const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
			if (!achievement)
				continue;
			measuredEntry.style = AchievementRenderStyle::CHALLENGE_INDICATOR;
			MeasureAchievement(dc, achievement, measuredEntry.style, &measuredEntry.w, &measuredEntry.h);
			break;
		}
		case OSDType::LEADERBOARD_TRACKER:
		{
			MeasureLeaderboardTracker(dc, entry.text, &measuredEntry.w, &measuredEntry.h);
			break;
		}
		case OSDType::ACHIEVEMENT_UNLOCKED:
		{
			const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
			if (!achievement)
				continue;
			measuredEntry.style = AchievementRenderStyle::UNLOCKED;
			MeasureAchievement(dc, achievement, AchievementRenderStyle::UNLOCKED, &measuredEntry.w, &measuredEntry.h);
			measuredEntry.h1 = measuredEntry.h;
			measuredEntry.w = 550.0f;
			break;
		}
		case OSDType::PROGRESS_BAR:
			MeasureOSDProgressBar(dc, entry, &measuredEntry.w, &measuredEntry.h);
			break;
		default:
			MeasureOSDEntry(dc, entry, measuredEntry.align, &measuredEntry.w, &measuredEntry.h, &measuredEntry.h1);
			break;
		}

		float enterAlpha = saturatef((float)(now - entry.startTime) * fadeoutCoef);
		float leaveAlpha = saturatef((float)(entry.endTime - now) * fadeoutCoef);
		float alpha = std::min(enterAlpha, leaveAlpha);
		measuredEntry.alpha = alpha;

		edges[(size_t)pos].height += (measuredEntry.h + 4.0f) * alpha;
		edges[(size_t)pos].maxWidth = std::max(edges[(size_t)pos].maxWidth, measuredEntry.w);
	}

	std::vector<ClickZone> dismissZones;

	// Now, perform layout for all 8 edges.
	for (size_t i = 0; i < (size_t)ScreenEdgePosition::VALUE_COUNT; i++) {
		if (edges[i].height == 0.0f) {
			// Nothing on this side, ignore it entirely.
			continue;
		}

		// First, compute the start position.
		float y = padding;
		int horizAdj = 0;
		int vertAdj = 0;
		switch ((ScreenEdgePosition)i) {
		case ScreenEdgePosition::TOP_LEFT:    horizAdj = -1; vertAdj = -1; break;
		case ScreenEdgePosition::CENTER_LEFT: horizAdj = -1; break;
		case ScreenEdgePosition::BOTTOM_LEFT: horizAdj = -1; vertAdj = 1; break;
		case ScreenEdgePosition::TOP_RIGHT:    horizAdj = 1; vertAdj = -1; break;
		case ScreenEdgePosition::CENTER_RIGHT: horizAdj = 1; break;
		case ScreenEdgePosition::BOTTOM_RIGHT: horizAdj = 1; vertAdj = 1; break;
		case ScreenEdgePosition::TOP_CENTER:  vertAdj = -1; break;
		case ScreenEdgePosition::BOTTOM_CENTER: vertAdj = 1; break;
		case ScreenEdgePosition::CENTER: break;
		default: break;
		}

		if (vertAdj == 0) {
			// Center vertically
			y = (bounds_.h - edges[i].height) * 0.5f;
		} else if (vertAdj == 1) {
			y = (bounds_.h - edges[i].height);
		}

		// Then, loop through the entries and those belonging here, get rendered here.
		for (size_t j = 0; j < (size_t)entries.size(); j++) {
			auto &entry = entries[j];
			if (typeEdges[(size_t)entry.type] != (ScreenEdgePosition)i) {  // yes, i
				continue;
			}
			auto &measuredEntry = measuredEntries[j];
			float alpha = measuredEntry.alpha * edges[i].alpha;

			Bounds b(padding, y, measuredEntry.w, measuredEntry.h);

			if (horizAdj == 0) {
				// Centered
				b.x = (bounds_.w - b.w) * 0.5f;
			} else if (horizAdj == 1) {
				// Right-aligned
				b.x = bounds_.w - (b.w + padding);
			}

			switch (entry.type) {
			case OSDType::ACHIEVEMENT_PROGRESS:
			case OSDType::ACHIEVEMENT_CHALLENGE_INDICATOR:
			{
				const rc_client_achievement_t *achievement = rc_client_get_achievement_info(Achievements::GetClient(), entry.numericID);
				RenderAchievement(dc, achievement, measuredEntry.style, b, alpha, entry.startTime, now, false);
				break;
			}
			case OSDType::LEADERBOARD_TRACKER:
				RenderLeaderboardTracker(dc, b, entry.text, alpha);
				break;
			case OSDType::PROGRESS_BAR:
				RenderOSDProgressBar(dc, entry, b, 0, alpha);
				break;
			default:
			{
				// Scale down if height doesn't fit.
				float scale = 1.0f;
				if (measuredEntry.h > bounds_.h - y) {
					// Scale down!
					scale = std::max(0.15f, (bounds_.h - y) / measuredEntry.h);
					dc.SetFontScale(scale, scale);
					b.w *= scale;
					b.h *= scale;
				}

				float alpha = Clamp((float)(entry.endTime - now) * 4.0f, 0.0f, 1.0f);
				RenderOSDEntry(dc, entry, b, measuredEntry.h1, measuredEntry.align, alpha);

				switch (entry.type) {
				case OSDType::MESSAGE_INFO:
				case OSDType::MESSAGE_SUCCESS:
				case OSDType::MESSAGE_WARNING:
				case OSDType::MESSAGE_ERROR:
				case OSDType::MESSAGE_CENTERED_ERROR:
				case OSDType::MESSAGE_CENTERED_WARNING:
				case OSDType::MESSAGE_ERROR_DUMP:
				case OSDType::MESSAGE_FILE_LINK:
				case OSDType::ACHIEVEMENT_UNLOCKED:
					// Save the location of the popup, for easy dismissal.
					dismissZones.push_back(ClickZone{ (int)j, b });
					break;
				default:
					break;
				}
				break;
			}
			}


			y += (measuredEntry.h + 4.0f) * measuredEntry.alpha;
		}
	}

	std::lock_guard<std::mutex> lock(clickMutex_);
	clickZones_ = dismissZones;
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

// Asynchronous!
bool OnScreenMessagesView::Dismiss(float x, float y) {
	bool dismissed = false;
	std::lock_guard<std::mutex> lock(clickMutex_);
	double now = time_now_d();
	for (auto &zone : clickZones_) {
		if (zone.bounds.Contains(x, y)) {
			g_OSD.DismissEntry(zone.index, now);
			dismissed = true;
		}
	}
	return dismissed;
}

bool OSDOverlayScreen::UnsyncTouch(const TouchInput &touch) {
	// Don't really need to forward.
	// UIScreen::UnsyncTouch(touch);
	if ((touch.flags & TOUCH_DOWN) && osmView_) {
		return osmView_->Dismiss(touch.x, touch.y);
	} else {
		return false;
	}
}

void OSDOverlayScreen::CreateViews() {
	root_ = new UI::AnchorLayout();
	root_->SetTag("OSDOverlayScreen");
	osmView_ = root_->Add(new OnScreenMessagesView(new UI::AnchorLayoutParams(0.0f, 0.0f, 0.0f, 0.0f)));
}

void OSDOverlayScreen::DrawForeground(UIContext &ui) {
	DebugOverlay debugOverlay = (DebugOverlay)g_Config.iDebugOverlay;

	// Special case control for now, since it uses the control mapper that's owned by EmuScreen.
	if (debugOverlay != DebugOverlay::OFF && debugOverlay != DebugOverlay::CONTROL) {
		UIContext *uiContext = screenManager()->getUIContext();
		DrawDebugOverlay(uiContext, uiContext->GetLayoutBounds(), debugOverlay);
	}
}

void OSDOverlayScreen::update() {
	// Partial version of UIScreen::update() but doesn't do event processing to avoid duplicate event processing.
	bool vertical = UseVerticalLayout();
	if (vertical != lastVertical_) {
		RecreateViews();
		lastVertical_ = vertical;
	}

	DoRecreateViews();
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
	// Layout hack! Some weird problems with the layout that I can't figure out right now..
	if (squishy_) {
		w = 50.0;
	}
}

void NoticeView::Draw(UIContext &dc) {
	dc.PushScissor(bounds_);
	RenderNotice(dc, bounds_, height1_, level_, text_, detailsText_, iconName_, 0, 1.0f);
	dc.PopScissor();
}

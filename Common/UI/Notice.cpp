#include "Common/UI/UI.h"
#include "Common/UI/Notice.h"
#include "Common/UI/IconCache.h"
#include "Common/UI/Context.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"

uint32_t GetNoticeBackgroundColor(NoticeLevel type) {
	// Colors from Infima
	switch (type) {
	case NoticeLevel::ERROR: return 0x3530d5;  // danger-darker
	case NoticeLevel::WARN: return 0x009ed9;  // warning-darker
	case NoticeLevel::INFO: return 0x706760;  // gray-700
	case NoticeLevel::SUCCESS: return 0x008b00;  // nice green
	default: return 0x606770;
	}
}

ImageID GetOSDIcon(NoticeLevel level) {
	switch (level) {
	case NoticeLevel::INFO: return ImageID("I_INFO");
	case NoticeLevel::ERROR: return ImageID("I_CROSS");
	case NoticeLevel::WARN: return ImageID("I_WARNING");
	case NoticeLevel::SUCCESS: return ImageID("I_CHECKMARK");
	default: return ImageID::invalid();
	}
}

NoticeLevel GetNoticeLevel(OSDType type) {
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
void MeasureNotice(const UIContext &dc, NoticeLevel level, std::string_view text, std::string_view details, std::string_view iconName, int align, float maxWidth, float *width, float *height, float *height1) {
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
		ImageID iconID = iconName.empty() ? GetOSDIcon(level) : ImageID(iconName);
		if (iconID.isValid()) {
			dc.Draw()->GetAtlas()->measureImage(iconID, &iconW, &iconH);
		}
	}

	float chromeWidth = iconW + 5.0f + 12.0f;
	float availableWidth = maxWidth - chromeWidth;

	// OK, now that we have figured out how much space we have for the text, we can measure it (with wrapping if needed).
	// We currently don't wrap the title.

	float titleWidth, titleHeight;
	dc.MeasureTextRect(dc.GetTheme().uiFont, 1.0f, 1.0f, text, availableWidth, &titleWidth, &titleHeight, align);

	*width = std::min(titleWidth, availableWidth);
	*height1 = titleHeight;

	float width2 = 0.0f, height2 = 0.0f;
	if (!details.empty()) {
		dc.MeasureTextRect(dc.GetTheme().uiFontSmall, 1.0f, 1.0f, details, availableWidth, &width2, &height2, align | FLAG_WRAP_TEXT);
		*width = std::max(*width, width2);
	}

	*width += chromeWidth;
	if (height2 == 0.0f && iconH < 2.0f * *height1) {
		// Center vertically using the icon.
		*height1 = std::max(*height1, iconH + 2.0f);
	}
	*height = std::max(*height1 + height2 + 8.0f, iconH + 5.0f);
}

void RenderNotice(UIContext &dc, Bounds bounds, float height1, NoticeLevel level, std::string_view text, std::string_view details, std::string_view iconName, int align, float alpha, OSDMessageFlags flags, float timeVal) {
	UI::Drawable background = UI::Drawable(colorAlpha(GetNoticeBackgroundColor(level), alpha));

	dc.SetFontStyle(dc.GetTheme().uiFont);

	uint32_t foreGround = whiteAlpha(alpha);

	if (!(flags & OSDMessageFlags::Transparent)) {
		dc.DrawRectDropShadow(bounds, 12.0f, 0.7f * alpha);
		dc.FillRect(background, bounds);
	}

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
		ImageID iconID = iconName.empty() ? GetOSDIcon(level) : ImageID(iconName);
		if (iconID.isValid()) {
			// Atlas icon.
			dc.Draw()->GetAtlas()->measureImage(iconID, &iconW, &iconH);
			if (!iconName.empty()) {
				Bounds iconBounds = Bounds(bounds.x + 2.5f, bounds.y + 2.5f, iconW, iconH);
				// HACK: The RA icon needs some background.
				if (equals(iconName, "I_RETROACHIEVEMENTS_LOGO")) {
					dc.FillRect(UI::Drawable(0x50000000), iconBounds.Expand(2.0f));
				}
			}

			if (flags & (OSDMessageFlags::SpinLeft | OSDMessageFlags::SpinRight)) {
				const float direction = (flags & OSDMessageFlags::SpinLeft) ? -1.5f : 1.5f;
				dc.DrawImageRotated(iconID, bounds.x + 2.5f + iconW * 0.5f, bounds.y + 2.5f + iconW * 0.5f, 1.0f, direction * timeVal, foreGround, false);
			} else {
				dc.DrawImageVGradient(iconID, foreGround, foreGround, Bounds(bounds.x + 2.5f, bounds.y + 2.5f, iconW, iconH));
			}
		}
	}

	// Make room
	bounds.x += iconW + 5.0f;
	bounds.w -= iconW + 5.0f;

	Bounds primaryBounds = bounds;
	primaryBounds.h = height1;

	int titleAlign = (align & (FLAG_DYNAMIC_ASCII | FLAG_WRAP_TEXT)) | ALIGN_VCENTER | FLAG_ELLIPSIZE_TEXT;
	if (!(titleAlign & FLAG_WRAP_TEXT)) {
		titleAlign |= FLAG_ELLIPSIZE_TEXT;
	}

	dc.DrawTextShadowRect(text, primaryBounds.Inset(2.0f, 0.0f, 1.0f, 0.0f), foreGround, titleAlign);

	if (!details.empty()) {
		Bounds bottomTextBounds = bounds.Inset(3.0f, height1 + 5.0f, 3.0f, 3.0f);
		if (!(flags & OSDMessageFlags::Transparent)) {
			UI::Drawable backgroundDark = UI::Drawable(colorAlpha(darkenColor(GetNoticeBackgroundColor(level)), alpha));
			dc.FillRect(backgroundDark, bottomTextBounds);
		}
		dc.SetFontStyle(dc.GetTheme().uiFontSmall);
		dc.DrawTextRect(details, bottomTextBounds.Inset(1.0f, 1.0f), foreGround, (align & FLAG_DYNAMIC_ASCII) | ALIGN_LEFT | FLAG_WRAP_TEXT);
	}
	dc.SetFontStyle(dc.GetTheme().uiFont);
}

// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.


#include <algorithm>
#include <cmath>

#include "Common/Data/Text/I18n.h"
#include "Common/Math/curves.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Context.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Core/Util/VideoPlayer.h"
#include "UI/GameButton.h"
#include "UI/GameInfoCache.h"
#include "UI/GamePacker.h"


// Where the text starts in the list modes.
static float ListTextX(GameBrowserViewMode viewMode) {
	if (viewMode == GameBrowserViewMode::LARGE_LIST) {
		return LARGE_LIST_LEFT_PAD + LARGE_LIST_ICON_W * g_Config.fGameGridScale + 20.0f;
	}
	return 150.0f;
}

// Space to the right of the text in the list modes, for the info button.
static float ListTextRightMargin(GameBrowserViewMode viewMode) {
	if (viewMode == GameBrowserViewMode::LARGE_LIST) {
		return LARGE_LIST_INFO_SIZE + LARGE_LIST_PAD * 2;
	}
	return 0.0f;
}


void GameButton::UpdateVideo(const std::shared_ptr<GameInfo> &ginfo, bool wantVideo) {
	if (!wantVideo) {
		video_.reset();
		return;
	}
	if (video_ || !ginfo->Ready(GameInfoFlags::ICON1_PMF)) {
		return;
	}
	std::string data;
	{
		std::lock_guard<std::mutex> guard(ginfo->lock);
		data = ginfo->icon1pmf;
	}
	if (!data.empty()) {
		video_ = std::make_unique<PMFVideoTexture>(std::move(data));
		videoStart_ = Instant::Now();
	}
}

// These are the built-in icons used for file types that don't have one (or simply files missing one).
static ImageID ChooseGameIcon(const std::shared_ptr<GameInfo> &ginfo) {
	if (ginfo->GetFilePath().Type() == PathType::HTTP) {
		return ginfo->fileType == IdentifiedFileType::UNKNOWN ? ImageID("I_LINK_OUT_QUESTION") : ImageID("I_LINK_OUT");
	}

	switch (ginfo->fileType) {
	case IdentifiedFileType::PSP_ELF:
		if (ginfo->region == GameRegion::VSH) {
			return ImageID::invalid();  // it has a (preset) icon.
		}
		// The XMB is an ELF too, but has an icon.
		return ImageID("I_APP");
	case IdentifiedFileType::UNKNOWN_ELF: return ImageID("I_APP"); break;
	case IdentifiedFileType::PPSSPP_GE_DUMP: return ImageID("I_DISPLAY"); break;
	case IdentifiedFileType::PSX_ISO:
	case IdentifiedFileType::PSP_PS1_PBP: return ImageID("I_PSX_ISO"); break;
	case IdentifiedFileType::PS2_ISO: return ImageID("I_PS2_ISO"); break;
	case IdentifiedFileType::PS3_ISO: return ImageID("I_PS3_ISO"); break;
	case IdentifiedFileType::PSP_UMD_VIDEO_ISO: return ImageID("I_UMD_VIDEO_ISO"); break;
	case IdentifiedFileType::UNKNOWN_ISO: return ImageID("I_UNKNOWN_ISO"); break;
	case IdentifiedFileType::PPSSPP_SAVESTATE:
	case IdentifiedFileType::ERROR_IDENTIFYING:
	case IdentifiedFileType::UNKNOWN_BIN: return ImageID("I_FILE"); break;
	case IdentifiedFileType::ARCHIVE_ZIP: return ImageID("I_ARCHIVE_ZIP"); break;
	case IdentifiedFileType::ARCHIVE_7Z: return ImageID("I_ARCHIVE_7Z"); break;
	case IdentifiedFileType::ARCHIVE_RAR: return ImageID("I_ARCHIVE_RAR"); break;
	case IdentifiedFileType::PSP_PKG: return ImageID("I_FOLDER_UPLOAD"); break;
	case IdentifiedFileType::NORMAL_DIRECTORY: return ImageID("I_FOLDER"); break;

	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
		return ImageID("I_APP");  // This will practically always be replaced by a real icon.

	default:
		return ImageID::invalid();
	}
}

void GameButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	switch (viewMode_) {
	case GameBrowserViewMode::GRID:
		w = 144 * g_Config.fGameGridScale;
		h = 80 * g_Config.fGameGridScale;
		break;
	case GameBrowserViewMode::LARGE_LIST:
		w = 500;
		if (type_ == GameButtonType::Dir) {
			h = UI::ITEM_HEIGHT;
		} else {
			h = LARGE_LIST_ICON_H * g_Config.fGameGridScale + LARGE_LIST_PAD * 2;
		}
		break;
	default:  // List
		w = 500;
		h = 56;
		break;
	}
}

static const ImageID regionIcons[(int)GameRegion::COUNT] = {
	ImageID("I_FLAG_JP"),
	ImageID("I_FLAG_US"),
	ImageID("I_FLAG_EU"),
	ImageID("I_FLAG_HK"),
	ImageID("I_FLAG_AS"),
	ImageID("I_FLAG_KO"),
};

void GameButton::Draw(UIContext &dc) {
	// Like the XMB, only the selected game plays its video. Only keyboard/gamepad focus selects, since with
	// touch or the mouse, getting a video to play without launching the game was too fiddly.
	const bool wantVideo = viewMode_ == GameBrowserViewMode::LARGE_LIST && HasFocus() && pmf_player_available();

	std::shared_ptr<GameInfo> ginfo;

	std::string text;
	std::string id_version;
	Draw::Texture *texture = nullptr;
	ImageID imageIcon;
	GameRegion region = GameRegion::UNKNOWN;
	IdentifiedFileType fileType = IdentifiedFileType::UNKNOWN;

	if (type_ == GameButtonType::Dir) {
		text = gamePath_.GetFilename();
		imageIcon = ImageID(IsPinned() ? "I_FOLDER_PINNED" : "I_FOLDER");
		if (gamePath_.IsNetworkRoot()) {
			imageIcon = ImageID("I_NETWORK");
			auto mm = GetI18NCategory(I18NCat::MAINMENU);
			text = mm->T("Network");
		} else if (text == "..") {
			imageIcon = ImageID("I_UP_DIRECTORY");
			text = "";
		}
		fileType = IdentifiedFileType::NORMAL_DIRECTORY;
	} else {
		GameInfoFlags infoFlags = GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON;
		if (wantVideo) {
			infoFlags |= GameInfoFlags::ICON1_PMF;
		}
		ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, infoFlags);
		UpdateVideo(ginfo, wantVideo);
		imageIcon = ChooseGameIcon(ginfo);
		if (ginfo->Ready(GameInfoFlags::ICON)) {
			if (ginfo->icon.texture) {
				texture = ginfo->icon.texture;
				// The type icon is only for when there's no picture, and would be drawn on top of it.
				imageIcon = ImageID::invalid();
			} else {
				// No icon, but drawBackground is set. Let's show a plain icon depending on type.
				imageIcon = ChooseGameIcon(ginfo);
			}
		} else if (gamePath_.Type() == PathType::HTTP) {
			// Make sure that pending HTTP icons show something, instead of just blank.
			// Maybe we should show something for other file types too.
			imageIcon = ImageID("I_LINK_OUT_QUESTION");
		}
		text = ginfo->GetTitle();
		text = ReplaceAll(text, "\n", " ");

		id_version = ginfo->id_version;
		region = ginfo->region;
		fileType = ginfo->fileType;

		if (ginfo->disc_total > 1) {
			text += StringFromFormat("-DISC%d", ginfo->disc_number);
		}
	}

	if (video_) {
		Draw::Texture *frame = video_->Update(dc.GetDrawContext(), videoStart_.ElapsedSeconds());
		if (frame) {
			texture = frame;
		}
	}

	u32 color = 0, shadowColor = 0;
	using namespace UI;

	UI::Style style = dc.GetTheme().itemStyle;
	const bool rowFocused = HasFocus() && !infoSelected_;
	if (rowFocused) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

	if (viewMode_ != GameBrowserViewMode::GRID || !texture) {
		Drawable bg = style.background;
		dc.Draw()->Flush();
		dc.RebindTexture();
		dc.FillRect(bg, bounds_);
		dc.Draw()->Flush();
	}

	// Some types we just draw a default icon for.
	Bounds iconBounds = bounds_;
	iconBounds.w = viewMode_ == GameBrowserViewMode::GRID ? bounds_.w : 144;
	if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
		iconBounds.x += LARGE_LIST_LEFT_PAD;
		iconBounds.y += LARGE_LIST_PAD;
		iconBounds.w = LARGE_LIST_ICON_W * g_Config.fGameGridScale;
		iconBounds.h = LARGE_LIST_ICON_H * g_Config.fGameGridScale;
	}
	const float textX = ListTextX(viewMode_);

	if (texture && ginfo) {
		color = whiteAlpha(ease((time_now_d() - ginfo->icon.timeLoaded) * 2));
		shadowColor = blackAlpha(ease((time_now_d() - ginfo->icon.timeLoaded) * 2));
		float tw = texture->Width();
		float th = texture->Height();

		// Adjust position so we don't stretch the image vertically or horizontally.
		// Make sure it's not wider than 144 (like Doom Legacy homebrew), ugly in the grid mode.
		float nw = std::min(iconBounds.h * tw / th, (float)iconBounds.w);
		iconBounds.x += (iconBounds.w - nw) / 2.0f;
		iconBounds.w = nw;
	}

	int txOffset = down_ ? 4 : 0;
	if (viewMode_ != GameBrowserViewMode::GRID) {
		txOffset = 0;
	}

	Bounds overlayBounds = bounds_;

	// Render the icon, all modes have this.
	int dropsize = 10;
	if (texture) {
		if (viewMode_ == GameBrowserViewMode::LIST) {
			iconBounds.x += 4;
		}
		if (txOffset) {
			dropsize = 3;
			iconBounds.y += txOffset * 2;
			overlayBounds.y += txOffset * 2;
		}
		if (HasFocus()) {
			dc.Draw()->Flush();
			dc.RebindTexture();
			float pulse = sin(time_now_d() * 7.0) * 0.25 + 0.8;

			dc.DrawRectDropShadow(iconBounds, 10.0f, pulse, alphaMul(color, pulse));
			dc.Draw()->Flush();
		} else {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.DrawRectDropShadow(iconBounds, 8.0f, 0.7f, shadowColor);
			// dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid, x - dropsize, y - dropsize * 0.5f, x + w + dropsize, y + h + dropsize * 1.5, alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}

		dc.Draw()->Flush();
		dc.GetDrawContext()->BindTexture(0, texture);
		dc.Draw()->DrawTexRect(iconBounds, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
	}

	if (viewMode_ == GameBrowserViewMode::GRID && g_Config.bShowIDOnGameIcon && fileType != IdentifiedFileType::PSP_ELF && !id_version.empty()) {
		std::string_view idStr = id_version;
		if (fileType == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY) {
			auto ga = GetI18NCategory(I18NCat::GAME);
			idStr = ga->T("SaveData");
		}
		dc.SetFontScale(0.5f * g_Config.fGameGridScale, 0.5f * g_Config.fGameGridScale);
		dc.DrawText(idStr, bounds_.x + 5, iconBounds.y + 1, 0xFF000000, ALIGN_TOPLEFT);
		dc.DrawText(idStr, bounds_.x + 4, iconBounds.y, dc.GetTheme().infoStyle.fgColor, ALIGN_TOPLEFT);
		dc.SetFontScale(1.0f, 1.0f);
	}

	if (viewMode_ == GameBrowserViewMode::GRID) {
		if (imageIcon.isValid()) {
			Bounds bounds = bounds_;

			float tw, th;
			dc.MeasureText(dc.GetFontStyle(), g_Config.fGameGridScale, g_Config.fGameGridScale, text, &tw, &th, 0);
			dc.PushScissor(bounds);
			const FontStyle *fontStyle = GetTextStyle(dc, UI::TextSize::Small);
			dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Small));

			int iconSize = imageIcon == ImageID("I_UP_DIRECTORY") ? (float)dc.Draw()->GetAtlas()->getImage(imageIcon)->h : bounds.h * 0.3f;
			float textWidth = 0.0f;
			float textHeight = 0;
			dc.MeasureTextRect(*fontStyle, 1.0f, 1.0f, text, bounds.w - 10, &textWidth, &textHeight, ALIGN_HCENTER | FLAG_WRAP_TEXT);

			int totalHeight = iconSize + (int)textHeight;

			const float y = std::max(0.0f, bounds.h / 2.0f - totalHeight / 2.0f);

			const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(imageIcon);
			if (img && img->h > 0) {
				dc.RebindTexture();
				dc.Draw()->DrawImage(imageIcon, bounds.centerX(), bounds.y + y + 2, iconSize / (float)img->h, style.fgColor, ALIGN_TOP | ALIGN_HCENTER);
			}

			if (imageIcon != ImageID("I_UP_DIRECTORY") && imageIcon != ImageID("I_PIN") && imageIcon != ImageID("I_UNPIN")) {
				dc.DrawTextRect(text, bounds.Inset(5, y + iconSize + 4, 5, 2), style.fgColor, ALIGN_HCENTER | FLAG_WRAP_TEXT);
			}
			dc.PopScissor();
			dc.SetFontStyle(dc.GetTheme().uiFont);
		} else if (!texture) {
			// Just draw the text, centered.
			dc.Draw()->Flush();
			dc.PushScissor(bounds_);
			dc.DrawText(title_, bounds_.x + 4, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
			dc.Draw()->Flush();
			dc.PopScissor();
		}
	} else {
		Bounds bounds = bounds_;

		std::string_view rightText = g_Config.bShowIDOnGameIcon ? std::string_view(id_version) : std::string_view();

		float tw, th;
		dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, text, &tw, &th, 0);

		if (imageIcon.isValid()) {
			const float textX = ListTextX(viewMode_);
			if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
				const float scale = g_Config.fGameGridScale;
				dc.Draw()->DrawImage(imageIcon, bounds.x + LARGE_LIST_LEFT_PAD + LARGE_LIST_ICON_W * 0.5f * scale, bounds.centerY(), 0.88f * scale, style.fgColor, ALIGN_CENTER);
			} else {
				dc.Draw()->DrawImage(imageIcon, bounds.x + 72, bounds.centerY(), 0.88f, style.fgColor, ALIGN_CENTER);
			}
		}

		float tx = textX;
		int availableWidth = bounds.w - textX - ListTextRightMargin(viewMode_);
		if (!rightText.empty()) {
			float vw, vh;
			dc.MeasureText(dc.GetFontStyle(), 0.7f, 0.7f, rightText, &vw, &vh, 0);
			availableWidth -= vw + 20;
			dc.SetFontScale(0.7f, 0.7f);
			dc.DrawText(rightText, bounds.x + availableWidth + textX + 10, bounds.centerY(), style.fgColor, ALIGN_VCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
		float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;
		if (availableWidth < tw) {
			tx -= (1.0f + sin(time_now_d() * 1.5f)) * sineWidth;
			Bounds tb = bounds;
			tb.x = bounds.x + textX;
			tb.w = availableWidth;
			dc.PushScissor(tb);
		}
		dc.DrawText(text, bounds.x + tx, bounds.centerY(), style.fgColor, ALIGN_VCENTER | FLAG_WRAP_TEXT);
		if (availableWidth < tw) {
			dc.PopScissor();
		}
	}

	if (ginfo && ginfo->hasConfig && !ginfo->id.empty()) {
		const AtlasImage *gearImage = dc.Draw()->GetAtlas()->getImage(ImageID("I_GEAR_SMALL"));
		if (gearImage) {
			if (viewMode_ == GameBrowserViewMode::GRID) {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x, iconBounds.y2() - gearImage->h * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x + LARGE_LIST_LEFT_PAD, iconBounds.y2() - gearImage->h * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x + 4, iconBounds.y, 1.0f);
			}
		}
	}

	const int regionIndex = (int)region;
	if (g_Config.bShowRegionOnGameIcon && region != GameRegion::UNKNOWN && regionIndex >= 0 && regionIndex < (int)GameRegion::COUNT) {
		const AtlasImage *regionImage = dc.Draw()->GetAtlas()->getImage(regionIcons[regionIndex]);
		if (regionImage) {
			if (viewMode_ == GameBrowserViewMode::GRID) {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + bounds_.w - (regionImage->w + 5) * g_Config.fGameGridScale,
					iconBounds.y2() - (regionImage->h + 5) * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + LARGE_LIST_LEFT_PAD + (LARGE_LIST_ICON_W - regionImage->w - 5) * g_Config.fGameGridScale,
					iconBounds.y2() - (regionImage->h + 5) * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + 4, iconBounds.y2() - regionImage->h - 5, 1.0f);
			}
		}
	}

	if (HasInfoButton()) {
		const Bounds infoBounds = InfoButtonBounds();
		Style infoStyle = HasFocus() ? dc.GetTheme().itemFocusedStyle : dc.GetTheme().itemStyle;
		if (infoButtonDown_) {
			infoStyle = dc.GetTheme().itemDownStyle;
			dc.FillRoundRect(infoStyle.background, infoBounds, dc.GetTheme().choiceRoundRadius);
		} else if (HasFocus() && infoSelected_) {
			dc.FillRoundRect(infoStyle.background, infoBounds, dc.GetTheme().choiceRoundRadius);
		}
		dc.Draw()->DrawImage(ImageID("I_INFO"), infoBounds.centerX(), infoBounds.centerY(), 1.0f, infoStyle.fgColor, ALIGN_CENTER);
	}

	u32 overlayColor = 0;
	if (holdEnabled_ && holdStart_ != 0.0) {
		double time_held = time_now_d() - holdStart_;
		overlayColor = whiteAlpha(time_held / 2.5f);
		if (holdStart_ != 0.0) {
			double time_held = time_now_d() - holdStart_;
			int holdFrameCount = (int)(time_held * 60.0f);
			if (holdFrameCount > 60) {
				// Blink before launching by holding
				if (((holdFrameCount >> 3) & 1) == 0)
					overlayColor = 0x0;
			}
		}
	}

	if (overlayColor) {
		dc.FillRect(Drawable(overlayColor), overlayBounds);
	}

	// Covers the icon while the game is being compressed. Clicking opens the game screen instead of playing.
	GamePacker::Status packStatus;
	if (GamePacker::IsPacking(gamePath_) && GamePacker::GetStatus(&packStatus)) {
		auto ga = GetI18NCategory(I18NCat::GAME);
		dc.FillRect(Drawable(0xD0505050), iconBounds);
		const float scale = viewMode_ == GameBrowserViewMode::GRID || viewMode_ == GameBrowserViewMode::LARGE_LIST ? 0.8f * g_Config.fGameGridScale : 0.6f;
		dc.SetFontStyle(dc.GetTheme().uiFont);
		dc.SetFontScale(scale, scale);
		const std::string text = std::string(ga->T("Compressing...")) + StringFromFormat("\n%d%%", (int)(packStatus.progress * 100.0f));
		dc.DrawTextRectSqueeze(text, iconBounds, 0xFFFFFFFF, ALIGN_CENTER);
		dc.SetFontScale(1.0f, 1.0f);
	}

	dc.RebindTexture();
}

std::string GameButton::DescribeText() const {
	// LOW: search calls this for every game in the list, which mustn't hold up the visible ones.
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO, nullptr, GameInfoFlags::EMPTY, TaskPriority::LOW);
	if (!ginfo->Ready(GameInfoFlags::PARAM_SFO))
		return "...";
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ginfo->GetTitle();
}

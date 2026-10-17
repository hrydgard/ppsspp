// Copyright (c) 2013- PPSSPP Project.

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

#include "ppsspp_config.h"

#include "Common/System/Display.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Root.h"
#include "Common/UI/ScreenManager.h"

#include "Common/Math/curves.h"
#include "Common/Net/URL.h"
#include "Common/File/FileUtil.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Core/System.h"
#include "Core/Util/RecentFiles.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/ELF/PBPReader.h"
#include "Core/FileLoaders/PS1EbootFileLoader.h"
#include "Core/MemMap.h"
#include "Core/Util/VideoPlayer.h"

#include "UI/GameBrowser.h"
#include "UI/GamePacker.h"
#include "UI/RemoteISOScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/Store.h"
#include "UI/UploadScreen.h"
#include "UI/Background.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Util/DarwinFileSystemServices.h" // For the browser

// Icon dimensions in the large list, and its padding around the icon.
static constexpr float LARGE_LIST_ICON_W = 144.0f;
static constexpr float LARGE_LIST_ICON_H = 80.0f;
static constexpr float LARGE_LIST_PAD = 6.0f;
static constexpr float LARGE_LIST_LEFT_PAD = 11.0f;
static constexpr float LARGE_LIST_INFO_SIZE = 64.0f;
// Folders get bigger lines too, though not as big as the games.
static constexpr float LARGE_LIST_FOLDER_H = 50.0f;

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

// In the list views, rightText (like a game ID) goes at the right edge, as for a game with a picture.
static void DrawIconWithText(UIContext &dc, ImageID image, std::string_view text, const Bounds &bounds, GameBrowserViewMode viewMode, const UI::Style &style, std::string_view rightText = {}) {
	const bool gridStyle = viewMode == GameBrowserViewMode::GRID;
	float tw, th;
	dc.MeasureText(dc.GetFontStyle(), gridStyle ? g_Config.fGameGridScale : 1.0, gridStyle ? g_Config.fGameGridScale : 1.0, text, &tw, &th, 0);

	const bool compact = bounds.w < 180 * (gridStyle ? g_Config.fGameGridScale : 1.0);
	if (compact) {
		dc.PushScissor(bounds);
		const FontStyle *fontStyle = GetTextStyle(dc, UI::TextSize::Small);
		dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Small));

		int iconSize = image == ImageID("I_UP_DIRECTORY") ? (float)dc.Draw()->GetAtlas()->getImage(image)->h : bounds.h * 0.3f;
		float textWidth = 0.0f;
		float textHeight = 0;
		dc.MeasureTextRect(*fontStyle, 1.0f, 1.0f, text, bounds.w - 10, &textWidth, &textHeight, ALIGN_HCENTER | FLAG_WRAP_TEXT);

		int totalHeight = iconSize + (int)textHeight;

		const float y = std::max(0.0f, bounds.h / 2.0f - totalHeight / 2.0f);

		if (image.isValid()) {
			const AtlasImage *img = dc.Draw()->GetAtlas()->getImage(image);
			if (img && img->h > 0) {
				dc.RebindTexture();
				dc.Draw()->DrawImage(image, bounds.centerX(), bounds.y + y + 2, iconSize / (float)img->h, style.fgColor, ALIGN_TOP | ALIGN_HCENTER);
			}
		}

		if (image != ImageID("I_UP_DIRECTORY") && image != ImageID("I_PIN") && image != ImageID("I_UNPIN")) {
			dc.DrawTextRect(text, bounds.Inset(5, y + iconSize + 4, 5, 2), style.fgColor, ALIGN_HCENTER | FLAG_WRAP_TEXT);
		}
		dc.SetFontStyle(dc.GetTheme().uiFont);
		dc.PopScissor();
	} else {
		const float textX = ListTextX(viewMode);
		if (viewMode == GameBrowserViewMode::LARGE_LIST) {
			const float scale = g_Config.fGameGridScale;
			dc.Draw()->DrawImage(image, bounds.x + LARGE_LIST_LEFT_PAD + LARGE_LIST_ICON_W * 0.5f * scale, bounds.centerY(), 0.88f * scale, style.fgColor, ALIGN_CENTER);
		} else {
			dc.Draw()->DrawImage(image, bounds.x + 72, bounds.centerY(), 0.88f * (gridStyle ? g_Config.fGameGridScale : 1.0), style.fgColor, ALIGN_CENTER);
		}

		float tx = textX;
		int availableWidth = bounds.w - textX - ListTextRightMargin(viewMode);
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
}

class GameButton : public UI::Clickable {
public:
	GameButton(const Path &gamePath, GameBrowserViewMode viewMode, UI::LayoutParams *layoutParams = nullptr)
		: UI::Clickable(layoutParams), viewMode_(viewMode), gamePath_(gamePath) {}

	void Draw(UIContext &dc) override;
	std::string DescribeText() const override;
	std::string SearchAlias() const override {
		// Without the extension, or searching for "iso" would match every ISO. Only three or more
		// letters counts as one, so a folder like "Game v1.2" keeps its version number.
		std::string filename = gamePath_.GetFilename();
		const std::string ext = gamePath_.GetFileExtension();
		if (ext.size() >= 4 && ext.size() <= filename.size() && std::all_of(ext.begin() + 1, ext.end(), [](char c) { return c >= 'a' && c <= 'z'; })) {
			filename.resize(filename.size() - ext.size());
		}
		return filename;
	}
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		switch (viewMode_) {
		case GameBrowserViewMode::GRID:
			w = 144 * g_Config.fGameGridScale;
			h = 80 * g_Config.fGameGridScale;
			break;
		case GameBrowserViewMode::LARGE_LIST:
			w = 500;
			h = LARGE_LIST_ICON_H * g_Config.fGameGridScale + LARGE_LIST_PAD * 2;
			break;
		default:  // List
			w = 500;
			h = 50;
			break;
		}
	}
	const Path &GamePath() const { return gamePath_; }

	void SetHoldEnabled(bool hold) {
		holdEnabled_ = hold;
	}
	bool Touch(const TouchInput &input) override {
		hovering_ = bounds_.Contains(input.x, input.y);
		if (HasInfoButton() && TouchInfoButton(input)) {
			return true;
		}
		const bool retval = UI::Clickable::Touch(input);
		if (hovering_ && (input.flags & TouchInputFlags::DOWN)) {
			holdStart_ = time_now_d();
		}
		if (input.flags & TouchInputFlags::UP) {
			holdStart_ = 0;
		}
		return retval;
	}

	bool Key(const KeyInput &key) override {
		bool showInfo = false;

		if (HasFocus() && UI::IsInfoKey(key)) {
			// If it's the button that's mapped to triangle, then show the info.
			if (key.flags & KeyInputFlags::DOWN) {
				infoKeyPressed_ = true;
			}
			if ((key.flags & KeyInputFlags::UP) && infoKeyPressed_) {
				showInfo = true;
				infoKeyPressed_ = false;
			}
		} else if (hovering_ && key.deviceId == DEVICE_ID_MOUSE && key.keyCode == NKCODE_EXT_MOUSEBUTTON_2) {
			// If it's the right mouse button, and it's not otherwise mapped, show the info also.
			if (key.flags & KeyInputFlags::DOWN) {
				showInfoPressed_ = true;
			}
			if ((key.flags & KeyInputFlags::UP) && showInfoPressed_) {
				showInfo = true;
				showInfoPressed_ = false;
			}
		}

		if (showInfo) {
			TriggerOnHoldClick();
			return true;
		}

		if (infoSelected_ && HasFocus() && UI::IsAcceptKey(key)) {
			if (key.flags & KeyInputFlags::DOWN) {
				infoButtonDown_ = true;
			}
			if ((key.flags & KeyInputFlags::UP) && infoButtonDown_) {
				infoButtonDown_ = false;
				TriggerOnHoldClick();
			}
			return true;
		}

		// Holding the accept button works like a long press, so a gamepad can do it too.
		if (holdEnabled_ && HasFocus() && UI::IsAcceptKey(key) && !(key.flags & KeyInputFlags::IS_REPEAT)) {
			if (key.flags & KeyInputFlags::DOWN) {
				holdStart_ = time_now_d();
			}
			if (key.flags & KeyInputFlags::UP) {
				holdStart_ = 0.0;
			}
		}

		return Clickable::Key(key);
	}

	void Update() override {
		// Hold button for 1.5 seconds to launch the game options
		if (holdEnabled_ && holdStart_ != 0.0 && holdStart_ < time_now_d() - 1.5) {
			TriggerOnHoldClick();
		}
	}

	void FocusChanged(UI::FocusFlags focusFlags) override {
		UI::Clickable::FocusChanged(focusFlags);
		if (focusFlags & UI::FocusFlags::LOST_FOCUS) {
			// Don't let a held accept button carry over from a button we've left.
			holdStart_ = 0.0;
			// Moving to another game selects the game, not its info button.
			infoSelected_ = false;
			infoButtonDown_ = false;
		}
		TriggerOnHighlight(focusFlags);
	}

	// Left and right move between the game and its info button, so up and down still go from game to game.
	bool CanMoveFocus(UI::FocusMove dir) const override {
		if (!HasInfoButton()) {
			return true;
		}
		// We handle these moves ourselves, so MoveFocus won't play the sound for them.
		if (dir == UI::FocusMove::RIGHT && !infoSelected_) {
			infoSelected_ = true;
			UI::PlayUISound(UI::UISound::SELECT);
			return false;
		}
		if (dir == UI::FocusMove::LEFT && infoSelected_) {
			infoSelected_ = false;
			UI::PlayUISound(UI::UISound::SELECT);
			return false;
		}
		return true;
	}

	void DeviceLost() override {
		if (video_) {
			video_->DeviceLost();
		}
	}

	UI::Event OnHoldClick;
	UI::Event OnHighlight;

private:
	void TriggerOnHoldClick() {
		holdStart_ = 0.0;
		UI::EventParams e{};
		e.v = this;
		e.s = gamePath_.ToString();
		down_ = false;
		OnHoldClick.Trigger(e);
	}
	void TriggerOnHighlight(UI::FocusFlags focusFlags) {
		UI::EventParams e{};
		e.v = this;
		e.s = gamePath_.ToString();
		e.a = (u32)focusFlags;
		OnHighlight.Trigger(e);
	}

	void UpdateVideo(const std::shared_ptr<GameInfo> &ginfo, bool wantVideo);

	// The large list has room for an info button on each line, doing the same as a long press.
	bool HasInfoButton() const {
		return viewMode_ == GameBrowserViewMode::LARGE_LIST && holdEnabled_;
	}
	Bounds InfoButtonBounds() const {
		return Bounds(bounds_.x2() - LARGE_LIST_INFO_SIZE - LARGE_LIST_PAD, bounds_.centerY() - LARGE_LIST_INFO_SIZE * 0.5f, LARGE_LIST_INFO_SIZE, LARGE_LIST_INFO_SIZE);
	}
	// Returns true if the info button took the input.
	bool TouchInfoButton(const TouchInput &input) {
		if ((input.flags & TouchInputFlags::MOUSE) && (input.buttons & 1) == 0) {
			return false;
		}
		const bool inside = InfoButtonBounds().Contains(input.x, input.y);
		if (input.flags & TouchInputFlags::DOWN) {
			infoButtonDown_ = inside;
		}
		if (!infoButtonDown_) {
			return false;
		}
		if (input.flags & TouchInputFlags::UP) {
			infoButtonDown_ = false;
			if (inside && !(input.flags & TouchInputFlags::CANCEL)) {
				TriggerOnHoldClick();
			}
		}
		return true;
	}

	const GameBrowserViewMode viewMode_;
	const Path gamePath_;
	std::string title_;

	// The ICON1.PMF video, played in place of the icon in the large list, like the XMB does.
	std::unique_ptr<PMFVideoTexture> video_;
	Instant videoStart_;

	double holdStart_ = 0.0;
	bool holdEnabled_ = true;
	bool showInfoPressed_ = false;
	bool infoKeyPressed_ = false;
	bool hovering_ = false;
	bool infoButtonDown_ = false;
	// The info button has the keyboard focus, rather than the game. Changed from CanMoveFocus, hence mutable.
	mutable bool infoSelected_ = false;
};

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

void GameButton::Draw(UIContext &dc) {
	// Like the XMB, only the selected game plays its video. Only keyboard/gamepad focus selects, since with
	// touch or the mouse, getting a video to play without launching the game was too fiddly.
	const bool wantVideo = viewMode_ == GameBrowserViewMode::LARGE_LIST && HasFocus() && pmf_player_available();

	GameInfoFlags infoFlags = GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON;
	if (wantVideo) {
		infoFlags |= GameInfoFlags::ICON1_PMF;
	}
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, infoFlags);
	UpdateVideo(ginfo, wantVideo);
	Draw::Texture *texture = nullptr;
	u32 color = 0, shadowColor = 0;
	using namespace UI;

	UI::Style style = dc.GetTheme().itemStyle;
	if (down_) {
		style = dc.GetTheme().itemDownStyle;
	}

	// Some types we just draw a default icon for.
	ImageID imageIcon = ImageID::invalid();
	bool drawBackground = true;
	switch (ginfo->fileType) {
	case IdentifiedFileType::PSP_ELF:
		imageIcon = ImageID("I_APP");
		// The XMB is an ELF too, but has an icon.
		drawBackground = ginfo->region == GameRegion::VSH;
		break;
	case IdentifiedFileType::UNKNOWN_ELF: imageIcon = ImageID("I_APP"); drawBackground = false; break;
	case IdentifiedFileType::PPSSPP_GE_DUMP: imageIcon = ImageID("I_DISPLAY"); break;
	case IdentifiedFileType::PSX_ISO:
	case IdentifiedFileType::PSP_PS1_PBP: imageIcon = ImageID("I_PSX_ISO"); break;
	case IdentifiedFileType::PS2_ISO: imageIcon = ImageID("I_PS2_ISO"); break;
	case IdentifiedFileType::PS3_ISO: imageIcon = ImageID("I_PS3_ISO"); break;
	case IdentifiedFileType::PSP_UMD_VIDEO_ISO: imageIcon = ImageID("I_UMD_VIDEO_ISO"); break;
	case IdentifiedFileType::UNKNOWN_ISO: imageIcon = ImageID("I_UNKNOWN_ISO"); break;
	case IdentifiedFileType::PPSSPP_SAVESTATE:
	case IdentifiedFileType::ERROR_IDENTIFYING:
	case IdentifiedFileType::UNKNOWN_BIN: imageIcon = ImageID("I_FILE"); break;
	case IdentifiedFileType::ARCHIVE_ZIP: imageIcon = ImageID("I_ARCHIVE_ZIP"); drawBackground = false; break;
	case IdentifiedFileType::ARCHIVE_7Z: imageIcon = ImageID("I_ARCHIVE_7Z"); drawBackground = false; break;
	case IdentifiedFileType::ARCHIVE_RAR: imageIcon = ImageID("I_ARCHIVE_RAR"); drawBackground = false; break;
	case IdentifiedFileType::PSP_PKG: imageIcon = ImageID("I_FOLDER_UPLOAD"); drawBackground = false; break;
	default: break;
	}

	if (ginfo->Ready(GameInfoFlags::ICON)) {
		if (ginfo->icon.texture && drawBackground) {
			texture = ginfo->icon.texture;
			// The type icon is only for when there's no picture, and would be drawn on top of it.
			imageIcon = ImageID::invalid();
		} else if (drawBackground) {
			// No icon, but drawBackground is set. Let's show a plain icon depending on type.
			if (ginfo->fileType == IdentifiedFileType::PSP_ISO || ginfo->fileType == IdentifiedFileType::PSP_ISO_NP) {
				imageIcon = ImageID("I_UMD");
			} else if (ginfo->fileType == IdentifiedFileType::PSX_ISO || ginfo->fileType == IdentifiedFileType::PSP_PS1_PBP) {
				imageIcon = ImageID("I_PSX_ISO");
			} else if (gamePath_.Type() == PathType::HTTP) {
				imageIcon = ginfo->fileType == IdentifiedFileType::UNKNOWN ? ImageID("I_LINK_OUT_QUESTION") : ImageID("I_LINK_OUT");
			} else {
				imageIcon = ImageID("I_APP");
			}
		}
	} else {
		// Make sure that pending HTTP icons show something, instead of just blank.
		if (gamePath_.Type() == PathType::HTTP) {
			imageIcon = ImageID("I_LINK_OUT_QUESTION");
		}
	}

	Bounds overlayBounds = bounds_;
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

	int x = bounds_.x;
	int y = bounds_.y;
	int w = viewMode_ == GameBrowserViewMode::GRID ? bounds_.w : 144;
	int h = bounds_.h;
	if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
		x += LARGE_LIST_LEFT_PAD;
		y += LARGE_LIST_PAD;
		w = LARGE_LIST_ICON_W * g_Config.fGameGridScale;
		h = LARGE_LIST_ICON_H * g_Config.fGameGridScale;
	}
	const float textX = ListTextX(viewMode_);

	const bool rowFocused = HasFocus() && !infoSelected_;
	if (viewMode_ != GameBrowserViewMode::GRID || !texture) {
		if (rowFocused)
			style = down_ ? dc.GetTheme().itemDownStyle : dc.GetTheme().itemFocusedStyle;

		Drawable bg = style.background;

		dc.Draw()->Flush();
		dc.RebindTexture();
		dc.FillRect(bg, bounds_);
		dc.Draw()->Flush();
	}

	if (texture && video_) {
		Draw::Texture *frame = video_->Update(dc.GetDrawContext(), videoStart_.ElapsedSeconds());
		if (frame) {
			texture = frame;
		}
	}

	if (texture) {
		color = whiteAlpha(ease((time_now_d() - ginfo->icon.timeLoaded) * 2));
		shadowColor = blackAlpha(ease((time_now_d() - ginfo->icon.timeLoaded) * 2));
		float tw = texture->Width();
		float th = texture->Height();

		// Adjust position so we don't stretch the image vertically or horizontally.
		// Make sure it's not wider than 144 (like Doom Legacy homebrew), ugly in the grid mode.
		float nw = std::min(h * tw / th, (float)w);
		x += (w - nw) / 2.0f;
		w = nw;
	}

	int txOffset = down_ ? 4 : 0;
	if (viewMode_ != GameBrowserViewMode::GRID) txOffset = 0;

	// Render button
	int dropsize = 10;
	if (texture) {
		if (viewMode_ == GameBrowserViewMode::LIST) {
			x += 4;
		}
		if (txOffset) {
			dropsize = 3;
			y += txOffset * 2;
			overlayBounds.y += txOffset * 2;
		}
		if (HasFocus()) {
			dc.Draw()->Flush();
			dc.RebindTexture();
			float pulse = sin(time_now_d() * 7.0) * 0.25 + 0.8;
			dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid, x - dropsize * 1.5f, y - dropsize * 1.5f, x + w + dropsize * 1.5f, y + h + dropsize * 1.5f, alphaMul(color, pulse), 1.0f);
			dc.Draw()->Flush();
		} else {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid, x - dropsize, y - dropsize * 0.5f, x + w + dropsize, y + h + dropsize * 1.5, alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}

		dc.Draw()->Flush();
		dc.GetDrawContext()->BindTexture(0, texture);
		dc.Draw()->DrawTexRect(x, y, x + w, y + h, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
	}

	if (viewMode_ == GameBrowserViewMode::GRID && g_Config.bShowIDOnGameIcon && ginfo->fileType != IdentifiedFileType::PSP_ELF && !ginfo->id_version.empty()) {
		std::string_view idStr = ginfo->id_version;
		if (ginfo->fileType == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY) {
			auto ga = GetI18NCategory(I18NCat::GAME);
			idStr = ga->T("SaveData");
		}
		dc.SetFontScale(0.5f * g_Config.fGameGridScale, 0.5f * g_Config.fGameGridScale);
		dc.DrawText(idStr, bounds_.x + 5, y + 1, 0xFF000000, ALIGN_TOPLEFT);
		dc.DrawText(idStr, bounds_.x + 4, y, dc.GetTheme().infoStyle.fgColor, ALIGN_TOPLEFT);
		dc.SetFontScale(1.0f, 1.0f);
	}

	if (imageIcon.isValid()) {
		Style style = dc.GetTheme().itemStyle;

		if (rowFocused) style = dc.GetTheme().itemFocusedStyle;
		if (down_) style = dc.GetTheme().itemDownStyle;
		if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

		DrawIconWithText(dc, imageIcon, ginfo->GetTitle(), bounds_, viewMode_, style, g_Config.bShowIDOnGameIcon ? std::string_view(ginfo->id_version) : std::string_view());

		if (overlayColor) {
			dc.FillRect(Drawable(overlayColor), overlayBounds);
		}
	} else {
		char discNumInfo[8];
		if (ginfo->disc_total > 1)
			snprintf(discNumInfo, sizeof(discNumInfo), "-DISC%d", ginfo->disc_number);
		else
			discNumInfo[0] = '\0';

		dc.Draw()->Flush();
		dc.RebindTexture();
		dc.SetFontStyle(dc.GetTheme().uiFont);
		if (viewMode_ != GameBrowserViewMode::GRID) {
			float tw, th;
			dc.Draw()->Flush();
			dc.PushScissor(bounds_);
			const std::string currentTitle = ginfo->GetTitle();
			if (!currentTitle.empty()) {
				title_ = ReplaceAll(currentTitle, "\n", " ");
			}

			dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, title_, &tw, &th, 0);

			int availableWidth = bounds_.w - textX - ListTextRightMargin(viewMode_);
			if (g_Config.bShowIDOnGameIcon) {
				float vw, vh;
				dc.MeasureText(dc.GetFontStyle(), 0.7f, 0.7f, ginfo->id_version, &vw, &vh, 0);
				availableWidth -= vw + 20;
				dc.SetFontScale(0.7f, 0.7f);
				dc.DrawText(ginfo->id_version, bounds_.x + availableWidth + textX + 10, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
				dc.SetFontScale(1.0f, 1.0f);
			}
			float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;

			float tx = textX;
			if (availableWidth < tw) {
				tx -= (1.0f + sin(time_now_d() * 1.5f)) * sineWidth;
				Bounds tb = bounds_;
				tb.x = bounds_.x + textX;
				tb.w = availableWidth;
				dc.PushScissor(tb);
			}
			dc.DrawText(title_, bounds_.x + tx, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
			if (availableWidth < tw) {
				dc.PopScissor();
			}
			dc.Draw()->Flush();
			dc.PopScissor();
		} else if (!texture) {
			dc.Draw()->Flush();
			dc.PushScissor(bounds_);
			dc.DrawText(title_, bounds_.x + 4, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
			dc.Draw()->Flush();
			dc.PopScissor();
		} else {
			dc.Draw()->Flush();
		}
	}

	if (ginfo->hasConfig && !ginfo->id.empty()) {
		const AtlasImage *gearImage = dc.Draw()->GetAtlas()->getImage(ImageID("I_GEAR_SMALL"));
		if (gearImage) {
			if (viewMode_ == GameBrowserViewMode::GRID) {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x, y + h - gearImage->h * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x + LARGE_LIST_LEFT_PAD, y + h - gearImage->h * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x + 4, y, 1.0f);
			}
		}
	}

	const int regionIndex = (int)ginfo->region;
	if (g_Config.bShowRegionOnGameIcon && regionIndex >= 0 && regionIndex < (int)GameRegion::COUNT) {
		const ImageID regionIcons[(int)GameRegion::COUNT] = {
			ImageID("I_FLAG_JP"),
			ImageID("I_FLAG_US"),
			ImageID("I_FLAG_EU"),
			ImageID("I_FLAG_HK"),
			ImageID("I_FLAG_AS"),
			ImageID("I_FLAG_KO"),
		};
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(regionIcons[regionIndex]);
		if (image) {
			if (viewMode_ == GameBrowserViewMode::GRID) {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + bounds_.w - (image->w + 5) * g_Config.fGameGridScale,
					y + h - (image->h + 5) * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + LARGE_LIST_LEFT_PAD + (LARGE_LIST_ICON_W - image->w - 5) * g_Config.fGameGridScale,
					y + h - (image->h + 5) * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + 4, y + h - image->h - 5, 1.0f);
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

	if (overlayColor) {
		dc.FillRect(Drawable(overlayColor), overlayBounds);
	}

	// Covers the icon while the game is being compressed. Clicking opens the game screen instead of playing.
	GamePacker::Status packStatus;
	if (GamePacker::IsPacking(gamePath_) && GamePacker::GetStatus(&packStatus)) {
		auto ga = GetI18NCategory(I18NCat::GAME);
		const Bounds iconBounds(x, y, w, h);
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

class DirButton : public UI::Button {
public:
	DirButton(const Path &path, GameBrowserViewMode viewMode, UI::LayoutParams *layoutParams)
		: UI::Button(path.ToString(), layoutParams), path_(path), viewMode_(viewMode), absolute_(false) {}
	DirButton(const Path &path, const std::string &text, GameBrowserViewMode viewMode, UI::LayoutParams *layoutParams = 0)
		: UI::Button(text, layoutParams), path_(path), viewMode_(viewMode), absolute_(true) {}

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		UI::Button::GetContentDimensions(dc, w, h);
		if (viewMode_ == GameBrowserViewMode::LARGE_LIST) {
			h = std::max(h, LARGE_LIST_FOLDER_H * g_Config.fGameGridScale + LARGE_LIST_PAD * 2);
		}
	}

	const Path &GetPath() const {
		return path_;
	}

	bool PathAbsolute() const {
		return absolute_;
	}

	void SetPinned(bool pin) {
		pinned_ = pin;
	}

	std::string DescribeText() const override {
		return std::string(Button::GetText());  // From Button
	}

private:
	Path path_;
	GameBrowserViewMode viewMode_;
	bool absolute_;
	bool pinned_ = false;
};

void DirButton::Draw(UIContext &dc) {
	using namespace UI;

	std::string_view text(GetText());
	ImageID image = ImageID(pinned_ ? "I_FOLDER_PINNED" : "I_FOLDER");
	if (path_.IsNetworkRoot()) {
		image = ImageID("I_NETWORK");
	} else if (text == "..") {
		image = ImageID("I_UP_DIRECTORY");
		text = "";
	}

	Style style = dc.GetTheme().itemStyle;

	if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

	dc.FillRect(style.background, bounds_);
	DrawIconWithText(dc, image, text, bounds_, viewMode_, style);
}

GameBrowser::GameBrowser(int token, const Path &path, BrowseFlags browseFlags, bool portrait, int *viewMode, ScreenManager *screenManager, std::string_view lastText, std::string_view lastLink, UI::LayoutParams *layoutParams)
	: LinearLayout(ORIENT_VERTICAL, layoutParams), viewMode_(viewMode), browseFlags_(browseFlags), portrait_(portrait), lastText_(lastText), lastLink_(lastLink), screenManager_(screenManager), token_(token) {
	using namespace UI;
	path_.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));
	Path memstickRoot = GetSysDirectory(DIRECTORY_MEMSTICK_ROOT);
	aliasMatch_ = memstickRoot;
	if (memstickRoot == GetSysDirectory(DIRECTORY_PSP)) {
		aliasDisplay_ = "ms:/PSP/";
	} else {
		aliasDisplay_ = "ms:/";
	}
	if (System_GetPropertyBool(SYSPROP_LIMITED_FILE_BROWSING) &&
		(path.Type() == PathType::NATIVE || path.Type() == PathType::CONTENT_URI)) {
		// Note: We don't restrict if the path is HTTPS, otherwise remote disc streaming breaks!
		path_.RestrictToRoot(GetSysDirectory(DIRECTORY_MEMSTICK_ROOT));
	}
	path_.SetPath(path);
	Refresh();
}

void GameBrowser::FocusGame(const Path &gamePath) {
	// If the button's still there, just focus it. That keeps the search results and scroll position.
	if (gameList_) {
		for (int i = 0; i < gameList_->GetNumSubviews(); i++) {
			GameButton *button = dynamic_cast<GameButton *>(gameList_->GetViewByIndex(i));
			if (button && button->GamePath() == gamePath && button->GetVisibility() == UI::V_VISIBLE) {
				button->SetFocus(UI::FocusFlags::CAUSE_RESTORE);
				return;
			}
		}
	}
	// Otherwise, Refresh focuses it when it recreates the buttons.
	focusPath_ = gamePath;
	refreshPending_ = true;
}

void GameBrowser::SetPath(const Path &path) {
	path_.SetPath(path);
	g_Config.currentDirectory = path_.GetPath();
	refreshPending_ = true;
}

bool GameBrowser::Key(const KeyInput &input) {
	// Alt+Up goes to the parent directory, as in file managers.
	if ((input.flags & KeyInputFlags::DOWN) && (input.flags & KeyInputFlags::ModAlt) && input.keyCode == NKCODE_DPAD_UP && input.deviceId == DEVICE_ID_KEYBOARD) {
		if ((browseFlags_ & BrowseFlags::NAVIGATE) && path_.CanNavigateUp()) {
			NavigateUp();
			return true;
		}
	}

	// Backspace with no search going also goes to the parent directory, as in file managers. Never on
	// a repeat, or holding it would climb to the root (or carry on up after clearing a search). Not
	// every platform flags repeats, so this also tracks whether it's held.
	if (input.keyCode == NKCODE_DEL && input.deviceId == DEVICE_ID_KEYBOARD) {
		if (input.flags & KeyInputFlags::UP) {
			backspaceHeld_ = false;
		} else if (input.flags & KeyInputFlags::DOWN) {
			const bool repeat = backspaceHeld_ || (input.flags & KeyInputFlags::IS_REPEAT);
			backspaceHeld_ = true;
			if (!repeat && search_.searchFilter.empty() && (browseFlags_ & BrowseFlags::NAVIGATE) && path_.CanNavigateUp()) {
				NavigateUp();
				return true;
			}
		}
	}

	bool retval = LinearLayout::Key(input);
	if (retval) {
		return true;
	}

	return search_.Key(gameList_, input);
}

void GameBrowser::SetSearchFilter(const std::string &filter, bool setKeyboardFocus) {
	search_.searchFilter = filter;
	// We don't refresh because game info loads asynchronously anyway.
	search_.ApplySearchFilter(gameList_, setKeyboardFocus);
}

GameBrowserViewMode GameBrowser::ViewMode() const {
	if (*viewMode_ < (int)GameBrowserViewMode::GRID || *viewMode_ > (int)GameBrowserViewMode::LARGE_LIST) {
		return GameBrowserViewMode::GRID;
	}
	return (GameBrowserViewMode)*viewMode_;
}

void GameBrowser::LayoutChange(UI::EventParams &e) {
	*viewMode_ = e.a;
	refreshPending_ = true;
}

void GameBrowser::LastClick(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, lastLink_.c_str());
}

void GameBrowser::BrowseClick(UI::EventParams &e) {
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	System_BrowseForFolder(token_, mm->T("Choose folder"), path_.GetPath(), [this](std::string_view filename, int) {
		SetPath(Path(filename));
	});
}

void GameBrowser::StorageClick(UI::EventParams &e) {
	std::vector<std::string> storageDirs = System_GetPropertyStringVec(SYSPROP_ADDITIONAL_STORAGE_DIRS);
	if (storageDirs.empty()) {
		// Shouldn't happen - this button shouldn't be clickable.
		return;
	}
	if (storageDirs.size() == 1) {
		SetPath(Path(storageDirs[0]));
	} else {
		// TODO: We should popup a dialog letting the user choose one.
		SetPath(Path(storageDirs[0]));
	}
}

void GameBrowser::OnHomeClick(UI::EventParams &e) {
	if (path_.GetPath().Type() == PathType::CONTENT_URI) {
		Path rootPath = path_.GetPath().GetRootVolume();
		if (rootPath != path_.GetPath()) {
			SetPath(rootPath);
			return;
		}
		if (System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
			// There'll be no sensible home, ignore.
			return;
		}
	}

	SetPath(HomePath());
}

// TODO: This doesn't make that much sense for Android, especially after scoped storage..
// Maybe we should have no home directory in this case. Or it should just navigate to the root
// of the current folder tree.
Path GameBrowser::HomePath() {
	if (!homePath_.empty()) {
		return homePath_;
	}
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH) || PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(IOS)
	return g_Config.memStickDirectory;
#else
	return Path(getenv("HOME"));
#endif
}

void GameBrowser::PinToggleClick(UI::EventParams &e) {
	auto &pinnedPaths = g_Config.vPinnedPaths;
	const std::string path = File::ResolvePath(path_.GetPath().ToString());
	if (IsCurrentPathPinned()) {
		pinnedPaths.erase(std::remove(pinnedPaths.begin(), pinnedPaths.end(), path), pinnedPaths.end());
	} else {
		pinnedPaths.push_back(path);
	}
	refreshPending_ = true;
}

bool GameBrowser::HasSpecialFiles(std::vector<Path> &filenames) {
	if (path_.GetPath().ToString() == "!RECENT") {
		filenames.clear();
		for (auto &str : g_recentFiles.GetRecentFiles()) {
			filenames.emplace_back(str);
		}
		return true;
	}
	return false;
}

void GameBrowser::Update() {
	LinearLayout::Update();
	if (refreshPending_) {
		path_.Refresh();
	}
	if ((listingPending_ && path_.IsListingReady()) || refreshPending_) {
		Refresh();
		refreshPending_ = false;
	}
	if (search_.searchPending) {
		search_.ApplySearchFilter(gameList_, false);
	}
}

void GameBrowser::Draw(UIContext &dc) {
	using namespace UI;

	if (lastScale_ != g_Config.fGameGridScale || lastViewMode_ != ViewMode()) {
		refreshPending_ = true;
	}

	if (hasDropShadow_) {
		// Darken things behind.
		dc.FillRect(UI::Drawable(0x60000000), dc.GetBounds().Expand(dropShadowExpand_));
		float dropsize = 30.0f;
		dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid,
			bounds_.x - dropsize, bounds_.y,
			bounds_.x2() + dropsize, bounds_.y2() + dropsize * 1.5f, 0xDF000000, 3.0f);
	}

	if (clip_) {
		dc.PushScissor(bounds_);
	}

	dc.FillRect(bg_, bounds_);
	for (View *view : views_) {
		if (view->GetVisibility() == V_VISIBLE) {
			// Check if bounds are in current scissor rectangle.
			if (dc.GetScissorBounds().Intersects(dc.TransformBounds(view->GetBounds())))
				view->Draw(dc);
		}
	}

	if (clip_) {
		dc.PopScissor();
	}
}

void GameBrowser::SetSearchBar(SearchBar *searchBar) {
	search_.searchBar = searchBar;
	search_.searchBar->OnCancel.Add([this](UI::EventParams &) {
		SetSearchFilter("", false);
	});
}

static bool IsValidPBP(const Path &path, bool allowHomebrew) {
	if (!File::Exists(path))
		return false;

	std::unique_ptr<FileLoader> loader(ConstructFileLoader(path));

	PBPReader pbp(loader.get());
	// Some old homebrew (like Ozone and Trig Wars) ships a plain ELF named EBOOT.PBP. It has no
	// PARAM.SFO, but Identify_File accepts it and it boots.
	if (pbp.IsELF()) {
		return true;
	}

	std::vector<u8> sfoData;
	if (!pbp.GetSubFile(PBP_PARAM_SFO, &sfoData))
		return false;

	ParamSFOData sfo;
	sfo.ReadSFO(sfoData);
	if (!allowHomebrew && sfo.GetValueString("DISC_ID").empty())
		return false;

	// PS1 games, which need the Media Engine.
	if (sfo.GetValueString("CATEGORY") == "ME" && !Memory::CanCreateMESpace())
		return false;

	return true;
}

void GameBrowser::Refresh() {
	using namespace UI;

	lastScale_ = g_Config.fGameGridScale;
	lastViewMode_ = ViewMode();
	const bool gridStyle = lastViewMode_ == GameBrowserViewMode::GRID;

	// Kill all the contents
	Clear();
	search_.searchStates.clear();

	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	gameList_ = nullptr;

	LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 12, 8, 0)));
	Add(topBar);

	// Recent isn't a real folder, so it gets no path or refresh.
	const bool isRecent = path_.GetPath().ToString() == "!RECENT";

	std::string pathStr = path_.GetPath().IsNetworkRoot() ? std::string(mm->T("Network")) : GetFriendlyPath(path_.GetPath(), aliasMatch_, aliasDisplay_);

	if (browseFlags_ & BrowseFlags::NAVIGATE) {
		Choice *homeChoice = topBar->Add(new Choice(ImageID("I_HOME_ICON"), new LayoutParams(WRAP_CONTENT, 64.0f)));
		homeChoice->SetRoundCorners(RoundCorners::All);
		homeChoice->OnClick.Handle(this, &GameBrowser::OnHomeClick);
		if (System_GetPropertyBool(SYSPROP_HAS_ADDITIONAL_STORAGE)) {
			Choice *sdCardChoice = topBar->Add(new Choice(ImageID("I_SDCARD"), new LayoutParams(WRAP_CONTENT, 64.0f)));
			sdCardChoice->SetRoundCorners(RoundCorners::All);
			sdCardChoice->OnClick.Handle(this, &GameBrowser::StorageClick);
		}
#if PPSSPP_PLATFORM(IOS_APP_STORE)
		// Don't show a browse button, not meaningful to browse outside the documents folder it seems,
		// as we can't list things like document folders of another app, as far as I can tell.
		// However, we do show a Load.. button for picking individual files, that seems to work.
#elif PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
		// on Darwin, we don't show the 'Browse' text alongside the image
		// we show just the image, because we don't need to emphasize the button on Darwin
		// (tvOS has no folder browser.)
		if (System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER)) {
			Choice *browseChoice = topBar->Add(new Choice(ImageID("I_FOLDER_OPEN"), new LayoutParams(WRAP_CONTENT, 64.0f)));
			browseChoice->SetRoundCorners(RoundCorners::All);
			browseChoice->OnClick.Handle(this, &GameBrowser::BrowseClick);
		}
#else
		if ((browseFlags_ & BrowseFlags::BROWSE) && System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER)) {
			// Collapse the button title on very small screens (Retroid Pocket) or portrait mode.
			std::string_view browseTitle = (g_display.dp_xres <= 550 || portrait_) ? "" : mm->T("Browse");
			Choice *browseChoice = topBar->Add(new Choice(browseTitle, ImageID("I_FOLDER_OPEN"), new LayoutParams(WRAP_CONTENT, 64.0f)));
			browseChoice->SetRoundCorners(RoundCorners::All);
			browseChoice->OnClick.Handle(this, &GameBrowser::BrowseClick);
		}
		if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV) {
			Choice *enterPathChoice = topBar->Add(new Choice(mm->T("Enter Path"), new LayoutParams(WRAP_CONTENT, 64.0f)));
			enterPathChoice->SetRoundCorners(RoundCorners::All);
			enterPathChoice->OnClick.Add([=](UI::EventParams &) {
				auto mm = GetI18NCategory(I18NCat::MAINMENU);
				System_InputBoxGetString(token_, mm->T("Enter Path"), path_.GetPath().ToString(), false, [=](std::string_view responseString, int responseValue) {
					this->SetPath(Path(responseString));
					});
				});
		}
#endif
	}

	if (!isRecent) {
		Add(new TextView(pathStr, ALIGN_VCENTER | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 0, 8, 0))));
	}

	if (browseFlags_ & BrowseFlags::HOMEBREW_STORE) {
		Choice *hbStore = topBar->Add(new Choice(mm->T("Homebrew store"), ImageID("I_HOMEBREW_STORE"), new UI::LinearLayoutParams(WRAP_CONTENT, 64.0f)));
		hbStore->OnClick.Handle(this, &GameBrowser::OnHomebrewStore);
		hbStore->SetRoundCorners(RoundCorners::All);
	}

	if (browseFlags_ & BrowseFlags::UPLOAD_BUTTON) {
		Choice *uploadChoice = topBar->Add(new Choice(ImageID("I_FOLDER_UPLOAD"), new UI::LinearLayoutParams(WRAP_CONTENT, 64.0f)));
		uploadChoice->OnClick.Add([this](UI::EventParams &e) {
			screenManager_->push(new UploadScreen(path_.GetPath()));
		});
		// Nowhere to put the files until a game folder has been chosen.
		uploadChoice->SetEnabledFunc([this]() {
			return !path_.GetPath().empty();
		});
		uploadChoice->SetRoundCorners(RoundCorners::All);
	}

	ChoiceStrip *layoutChoice = topBar->Add(new ChoiceStrip(ORIENT_HORIZONTAL));
	layoutChoice->AddChoice(ImageID("I_GRID"));
	layoutChoice->AddChoice(ImageID("I_LINES"));
	layoutChoice->AddChoice(ImageID("I_LARGE_LIST"));
	layoutChoice->SetSelection((int)lastViewMode_, false);
	layoutChoice->ApplyRoundCorners();
	layoutChoice->OnChoice.Handle(this, &GameBrowser::LayoutChange);
	if (!isRecent) {
		Choice *rotateChoice = topBar->Add(new Choice(ImageID("I_ROTATE_LEFT"), new LayoutParams(64.0f, 64.0f)));
		rotateChoice->SetRoundCorners(RoundCorners::All);
		rotateChoice->OnClick.Add([=](UI::EventParams &e) {
			path_.Refresh();
			refreshPending_ = true;
		});
	}

	Choice *gridSettingsChoice = topBar->Add(new Choice(ImageID("I_GEAR"), new LayoutParams(64.0f, 64.0f)));
	gridSettingsChoice->SetRoundCorners(RoundCorners::All);
	gridSettingsChoice->OnClick.Handle(this, &GameBrowser::GridSettingsClick);

	if (gridStyle) {
		gameList_ = new UI::GridLayoutList(UI::GridLayoutSettings(150 * g_Config.fGameGridScale, 85 * g_Config.fGameGridScale), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 0, 6, 0)));
	} else {
		UI::LinearLayout *gl = new UI::LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		gl->SetSpacing(4.0f);
		gameList_ = gl;
	}
	Add(gameList_);

	// Find games in the current directory and create new ones.
	std::vector<DirButton *> dirButtons;
	std::vector<GameButton *> gameButtons;

	listingPending_ = !path_.IsListingReady();

	// TODO: If listing failed, show a special error message.

	std::vector<Path> filenames;
	if (HasSpecialFiles(filenames)) {
		for (size_t i = 0; i < filenames.size(); i++) {
			gameButtons.push_back(new GameButton(filenames[i], lastViewMode_, new UI::LinearLayoutParams(gridStyle ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
		}
	} else if (!listingPending_) {
		std::vector<File::FileInfo> fileInfo;
		path_.GetListing(fileInfo, "iso:cso:chd:pbp:elf:prx:ppdmp:cue:ccd:mds:bin:img:");
		// PS1 discs: a .bin or .img shows up as its cue sheet or .ccd, when it has one.
		RemoveDescribedDiscImages(&fileInfo);
		// Drives and servers aren't games, and probing a server that's gone away can take seconds.
		const bool probeDirs = !path_.GetPath().IsRoot() && !path_.GetPath().IsNetworkRoot();
		for (size_t i = 0; i < fileInfo.size(); i++) {
			bool isGame = !fileInfo[i].isDirectory;
			bool isSaveData = false;
			// Check if eboot directory. TODO: Should ideally be done off-thread somehow.
			if (!probeDirs) {
				// Just a directory.
			} else if (!isGame && path_.GetPath().size() >= 4 && IsValidPBP(path_.GetPath() / fileInfo[i].name / "EBOOT.PBP", true))
				isGame = true;
			else if (!isGame && File::Exists(path_.GetPath() / fileInfo[i].name / "PSP_GAME/SYSDIR"))
				isGame = true;
			else if (!isGame && File::Exists(path_.GetPath() / fileInfo[i].name / "PARAM.SFO"))
				isSaveData = true;

			if (!isGame && !isSaveData) {
				if (browseFlags_ & BrowseFlags::NAVIGATE) {
					const std::string name = fileInfo[i].fullName.IsNetworkRoot() ? std::string(mm->T("Network")) : fileInfo[i].name;
					dirButtons.push_back(new DirButton(fileInfo[i].fullName, name, lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
				}
			} else {
				gameButtons.push_back(new GameButton(fileInfo[i].fullName, lastViewMode_, new UI::LinearLayoutParams(gridStyle ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
			}
		}

		// Put RAR/ZIP files at the end to get them out of the way. Game update packages go here
		// too - they're not something to boot, they're something to install.
		// We do support unpacking some of them automatically.
		if (browseFlags_ & BrowseFlags::ARCHIVES) {
			fileInfo.clear();
			path_.GetListing(fileInfo, "zip:rar:r00:r01:7z:pkg:");
			if (!fileInfo.empty()) {
				for (size_t i = 0; i < fileInfo.size(); i++) {
					if (!fileInfo[i].isDirectory) {
						GameButton *b = new GameButton(fileInfo[i].fullName, lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT));
						b->SetHoldEnabled(false);
						gameButtons.push_back(b);
					}
				}
			}
		}
	}

	DirButton *upButton = nullptr;
	if (browseFlags_ & BrowseFlags::NAVIGATE) {
		if (path_.CanNavigateUp()) {
			upButton = gameList_->Add(new DirButton(Path(".."), lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
			upButton->OnClick.Handle(this, &GameBrowser::NavigateClick);
		}

		// Add any pinned paths before other directories.
		auto pinnedPaths = GetPinnedPaths();
		for (const auto &pinnedPath : pinnedPaths) {
			DirButton *pinnedDir = gameList_->Add(new DirButton(pinnedPath, pinnedPath.GetFilename(), lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
			pinnedDir->OnClick.Handle(this, &GameBrowser::NavigateClick);
			pinnedDir->SetPinned(true);
		}
	}

	if (listingPending_) {
		gameList_->Add(new UI::TextView(mm->T("Loading..."), ALIGN_CENTER, false, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
	}

	for (size_t i = 0; i < dirButtons.size(); i++) {
		gameList_->Add(dirButtons[i])->OnClick.Handle(this, &GameBrowser::NavigateClick);
		if (!focusPath_.empty() && dirButtons[i]->GetPath() == focusPath_ && dirButtons[i]->SetFocus(FocusFlags::CAUSE_RESTORE)) {
			UI::ScrollFocusedViewIntoView();
		}
	}

	for (size_t i = 0; i < gameButtons.size(); i++) {
		GameButton *b = gameList_->Add(gameButtons[i]);
		b->OnClick.Handle(this, &GameBrowser::GameButtonClick);
		b->OnHoldClick.Handle(this, &GameBrowser::GameButtonHoldClick);
		b->OnHighlight.Handle(this, &GameBrowser::GameButtonHighlight);

		if (!focusPath_.empty() && b->GamePath() == focusPath_ && b->SetFocus(FocusFlags::CAUSE_RESTORE)) {
			UI::ScrollFocusedViewIntoView();
		}
	}
	// A listing that's still loading has no buttons to focus yet.
	if (!listingPending_) {
		if (focusFirstItem_) {
			// The folder's own contents rather than "..", which only shows up first if it's empty.
			UI::View *first = !dirButtons.empty() ? (UI::View *)dirButtons[0] : !gameButtons.empty() ? (UI::View *)gameButtons[0] : upButton;
			if (first && first->SetFocus(FocusFlags::CAUSE_RESTORE)) {
				UI::ScrollFocusedViewIntoView();
			}
		}
		focusFirstItem_ = false;
		focusPath_.clear();
	}

	// Show a button to toggle pinning at the very end.
	if ((browseFlags_ & BrowseFlags::PIN) && !path_.GetPath().empty()) {
		std::string caption = ""; // IsCurrentPathPinned() ? "-" : "+";
		if (!gridStyle) {
			caption = IsCurrentPathPinned() ? mm->T("UnpinPath", "Unpin") : mm->T("PinPath", "Pin");
		}
		UI::Button *pinButton = gameList_->Add(new Button(caption, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
		pinButton->OnClick.Handle(this, &GameBrowser::PinToggleClick);
		pinButton->SetImageID(ImageID(IsCurrentPathPinned() ? "I_UNPIN" : "I_PIN"));
	}

	if (path_.GetPath().empty()) {
		Add(new TextView(mm->T("UseBrowseOrLoad", "Use Browse to choose a folder, or Load to choose a file.")));
	}

	if (!lastText_.empty()) {
		Add(new Spacer());
		Add(new Choice(lastText_, ImageID("I_LINK_OUT"), new UI::LinearLayoutParams(UI::WRAP_CONTENT, UI::WRAP_CONTENT, Margins(10, 0, 0, 10))))->OnClick.Handle(this, &GameBrowser::LastClick);
	}

	// The new buttons are all visible, so filter them again if a search is active.
	if (!search_.searchFilter.empty()) {
		search_.ApplySearchFilter(gameList_, false);
	}
}

bool GameBrowser::IsCurrentPathPinned() {
	const auto &paths = g_Config.vPinnedPaths;
	if (paths.empty()) {
		return false;
	}
	std::string resolved = File::ResolvePath(path_.GetPath().ToString());
	return std::find(paths.begin(), paths.end(), resolved) != paths.end();
}

std::vector<Path> GameBrowser::GetPinnedPaths() const {
#ifndef _WIN32
	static const std::string sepChars = "/";
#else
	static const std::string sepChars = "/\\";
#endif
	if (g_Config.vPinnedPaths.empty()) {
		// Early-out.
		return std::vector<Path>();
	}

	const std::string currentPath = File::ResolvePath(path_.GetPath().ToString());
	const std::vector<std::string> paths = g_Config.vPinnedPaths;
	std::vector<Path> results;
	for (size_t i = 0; i < paths.size(); ++i) {
		// We want to exclude the current path, and its direct children.
		if (paths[i] == currentPath) {
			continue;
		}
		if (startsWith(paths[i], currentPath)) {
			std::string descendant = paths[i].substr(currentPath.size());
			// If there's only one separator (or none), its a direct child.
			if (descendant.find_last_of(sepChars) == descendant.find_first_of(sepChars)) {
				continue;
			}
		}

		results.push_back(Path(paths[i]));
	}
	return results;
}

void GameBrowser::GameButtonClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2{};
	e2.s = button->GamePath().ToString();
	OnChoice.Trigger(e2);
}

void GameBrowser::GameButtonHoldClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2{};
	e2.s = button->GamePath().ToString();
	OnHoldChoice.Trigger(e2);
}

void GameBrowser::GameButtonHighlight(UI::EventParams &e) {
	OnHighlight.Trigger(e);
}

void GameBrowser::NavigateClick(UI::EventParams &e) {
	DirButton *button = static_cast<DirButton *>(e.v);
	Path text = button->GetPath();
	if (button->PathAbsolute()) {
		path_.SetPath(text);
	} else if (text.ToString() == "..") {
		NavigateUp();
		return;
	} else {
		path_.Navigate(text.ToString());
	}
	focusFirstItem_ = true;
	PathChanged();
}

void GameBrowser::NavigateUp() {
	// Focus the directory we came out of, in the parent's listing.
	focusPath_ = path_.GetPath();
	path_.Navigate("..");
	PathChanged();
}

void GameBrowser::PathChanged() {
	g_Config.currentDirectory = path_.GetPath();
	// Clear the search filter. This allow for smooth directory navigation using search
	// (although there's no good way of going up...)
	SetSearchFilter("", false);
	refreshPending_ = true;
}

void GameBrowser::GridSettingsClick(UI::EventParams &e) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto gridSettings = new GridSettingsPopupScreen(sy->T("Games list settings"));
	gridSettings->OnRecentChanged.Handle(this, &GameBrowser::OnRecentClear);
	if (e.v)
		gridSettings->SetPopupOrigin(e.v);

	screenManager_->push(gridSettings);
}

void GameBrowser::OnRecentClear(UI::EventParams &e) {
	screenManager_->RecreateAllViews();
	System_Notify(SystemNotification::UI);
}

void GameBrowser::OnHomebrewStore(UI::EventParams &e) {
	screenManager_->push(new StoreScreen());
}

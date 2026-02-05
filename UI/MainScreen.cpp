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
#include <sstream>

#include "ppsspp_config.h"

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/PathBrowser.h"
#include "Common/Math/curves.h"
#include "Common/Net/URL.h"
#include "Common/File/FileUtil.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Core/System.h"
#include "Core/Util/RecentFiles.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Util/GameManager.h"

#include "UI/BackgroundAudio.h"
#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/BaseScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/IAPScreen.h"
#include "UI/RemoteISOScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/Store.h"
#include "UI/UploadScreen.h"
#include "UI/InstallZipScreen.h"
#include "UI/Background.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "Common/Data/Text/I18n.h"

#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
#include "UI/DarwinFileSystemServices.h" // For the browser
#endif

#include "Core/HLE/sceUmd.h"

bool MainScreen::showHomebrewTab = false;

static void LaunchFile(ScreenManager *screenManager, Screen *currentScreen, const Path &path) {
	if (path.GetFileExtension() == ".zip") {
		// If is a zip file, we have a screen for that.
		screenManager->push(new InstallZipScreen(path));
	} else {
		// Check if we already know that this game isn't playable.
		auto info = g_gameInfoCache->GetInfo(nullptr, path, GameInfoFlags::FILE_TYPE);

		if (info->fileType == IdentifiedFileType::PSP_UMD_VIDEO_ISO) {
			// We show info about it.
			screenManager->push(new GameScreen(path, false));
			return;
		}

		if (currentScreen) {
			screenManager->cancelScreensAbove(currentScreen);
		}
		// Otherwise let the EmuScreen take care of it, including error handling.
		screenManager->switchScreen(new EmuScreen(path));
	}
}

static bool IsTempPath(const Path &str) {
	std::string item = str.ToString();

#ifdef _WIN32
	// Normalize slashes.
	item = ReplaceAll(item, "/", "\\");
#endif

	std::vector<std::string> tempPaths = System_GetPropertyStringVec(SYSPROP_TEMP_DIRS);
	for (auto temp : tempPaths) {
#ifdef _WIN32
		temp = ReplaceAll(temp, "/", "\\");
		if (!temp.empty() && temp[temp.size() - 1] != '\\')
			temp += "\\";
#else
		if (!temp.empty() && temp[temp.size() - 1] != '/')
			temp += "/";
#endif
		if (startsWith(item, temp))
			return true;
	}

	return false;
}

static void DrawIconWithText(UIContext &dc, ImageID image, std::string_view text, const Bounds &bounds, bool gridStyle, const UI::Style &style) {
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
		bool scissor = false;
		if (tw + 150 > bounds.w) {
			dc.PushScissor(bounds);
			scissor = true;
		}
		dc.Draw()->DrawImage(image, bounds.x + 72, bounds.centerY(), 0.88f * (gridStyle ? g_Config.fGameGridScale : 1.0), style.fgColor, ALIGN_CENTER);
		dc.DrawText(text, bounds.x + 150, bounds.centerY(), style.fgColor, ALIGN_VCENTER | FLAG_WRAP_TEXT);

		if (scissor) {
			dc.PopScissor();
		}
	}
}

class GameButton : public UI::Clickable {
public:
	GameButton(const Path &gamePath, bool gridStyle, UI::LayoutParams *layoutParams = nullptr)
		: UI::Clickable(layoutParams), gridStyle_(gridStyle), gamePath_(gamePath) {}

	void Draw(UIContext &dc) override;
	std::string DescribeText() const override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		if (gridStyle_) {
			w = 144*g_Config.fGameGridScale;
			h = 80*g_Config.fGameGridScale;
		} else {
			w = 500;
			h = 50;
		}
	}

	const Path &GamePath() const { return gamePath_; }

	void SetHoldEnabled(bool hold) {
		holdEnabled_ = hold;
	}
	bool Touch(const TouchInput &input) override {
		const bool retval = UI::Clickable::Touch(input);
		hovering_ = bounds_.Contains(input.x, input.y);
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

		return Clickable::Key(key);
	}

	void Update() override {
		// Hold button for 1.5 seconds to launch the game options
		if (holdEnabled_ && holdStart_ != 0.0 && holdStart_ < time_now_d() - 1.5) {
			TriggerOnHoldClick();
		}
	}

	void FocusChanged(int focusFlags) override {
		UI::Clickable::FocusChanged(focusFlags);
		TriggerOnHighlight(focusFlags);
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
	void TriggerOnHighlight(int focusFlags) {
		UI::EventParams e{};
		e.v = this;
		e.s = gamePath_.ToString();
		e.a = focusFlags;
		OnHighlight.Trigger(e);
	}

	bool gridStyle_;
	Path gamePath_;
	std::string title_;

	double holdStart_ = 0.0;
	bool holdEnabled_ = true;
	bool showInfoPressed_ = false;
	bool infoKeyPressed_ = false;
	bool hovering_ = false;
};

void GameButton::Draw(UIContext &dc) {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON);
	Draw::Texture *texture = nullptr;
	u32 color = 0, shadowColor = 0;
	using namespace UI;

	UI::Style style = dc.GetTheme().itemStyle;
	if (down_) {
		style = dc.GetTheme().itemDownStyle;
	}

	// Some types we just draw a default icon for.
	ImageID imageIcon = ImageID::invalid();
	switch (ginfo->fileType) {
	case IdentifiedFileType::UNKNOWN_ELF: imageIcon = ImageID("I_DEBUGGER"); break;
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
	default: break;
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

	if (ginfo->Ready(GameInfoFlags::ICON) && ginfo->icon.texture) {
		texture = ginfo->icon.texture;
	}

	int x = bounds_.x;
	int y = bounds_.y;
	int w = gridStyle_ ? bounds_.w : 144;
	int h = bounds_.h;

	if (!gridStyle_ || !texture) {
		if (HasFocus())
			style = down_ ? dc.GetTheme().itemDownStyle : dc.GetTheme().itemFocusedStyle;

		Drawable bg = style.background;

		dc.Draw()->Flush();
		dc.RebindTexture();
		dc.FillRect(bg, bounds_);
		dc.Draw()->Flush();
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
	if (!gridStyle_) txOffset = 0;

	// Render button
	int dropsize = 10;
	if (texture) {
		if (!gridStyle_) {
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
			dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid, x - dropsize*1.5f, y - dropsize*1.5f, x + w + dropsize*1.5f, y + h + dropsize*1.5f, alphaMul(color, pulse), 1.0f);
			dc.Draw()->Flush();
		} else {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid, x - dropsize, y - dropsize*0.5f, x+w + dropsize, y+h+dropsize*1.5, alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}

		dc.Draw()->Flush();
		dc.GetDrawContext()->BindTexture(0, texture);
		dc.Draw()->DrawTexRect(x, y, x+w, y+h, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
	}

	if (imageIcon.isValid()) {
		Style style = dc.GetTheme().itemStyle;

		if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
		if (down_) style = dc.GetTheme().itemDownStyle;
		if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

		DrawIconWithText(dc, imageIcon, ginfo->GetTitle(), bounds_, gridStyle_, style);

		if (overlayColor) {
			dc.FillRect(Drawable(overlayColor), overlayBounds);
		}
		return;
	}

	char discNumInfo[8];
	if (ginfo->disc_total > 1)
		snprintf(discNumInfo, sizeof(discNumInfo), "-DISC%d", ginfo->disc_number);
	else
		discNumInfo[0] = '\0';

	dc.Draw()->Flush();
	dc.RebindTexture();
	dc.SetFontStyle(dc.GetTheme().uiFont);
	if (gridStyle_ && ginfo->fileType == IdentifiedFileType::PPSSPP_GE_DUMP) {
		// Super simple drawing for GE dumps (no icon, just the filename).
		dc.PushScissor(bounds_);
		const std::string currentTitle = ginfo->GetTitle();
		dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Small));
		dc.DrawText(title_, bounds_.x + 4.0f, bounds_.centerY(), style.fgColor, ALIGN_VCENTER | ALIGN_LEFT);
		dc.SetFontStyle(dc.GetTheme().uiFont);
		title_ = currentTitle;
		dc.Draw()->Flush();
		dc.PopScissor();
	} else if (!gridStyle_) {
		float tw, th;
		dc.Draw()->Flush();
		dc.PushScissor(bounds_);
		const std::string currentTitle = ginfo->GetTitle();
		if (!currentTitle.empty()) {
			title_ = ReplaceAll(currentTitle, "\n", " ");
		}

		dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, title_, &tw, &th, 0);

		int availableWidth = bounds_.w - 150;
		if (g_Config.bShowIDOnGameIcon) {
			float vw, vh;
			dc.MeasureText(dc.GetFontStyle(), 0.7f, 0.7f, ginfo->id_version, &vw, &vh, 0);
			availableWidth -= vw + 20;
			dc.SetFontScale(0.7f, 0.7f);
			dc.DrawText(ginfo->id_version, bounds_.x + availableWidth + 160, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
		float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;

		float tx = 150;
		if (availableWidth < tw) {
			tx -= (1.0f + sin(time_now_d() * 1.5f)) * sineWidth;
			Bounds tb = bounds_;
			tb.x = bounds_.x + 150;
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

	if (ginfo->hasConfig && !ginfo->id.empty()) {
		const AtlasImage *gearImage = dc.Draw()->GetAtlas()->getImage(ImageID("I_GEAR_SMALL"));
		if (gearImage) {
			if (gridStyle_) {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x, y + h - gearImage->h*g_Config.fGameGridScale, g_Config.fGameGridScale);
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
			if (gridStyle_) {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + bounds_.w - (image->w + 5)*g_Config.fGameGridScale,
							y + h - (image->h + 5)*g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + 4, y + h - image->h - 5, 1.0f);
			}
		}
	}

	if (gridStyle_ && g_Config.bShowIDOnGameIcon) {
		dc.SetFontScale(0.5f*g_Config.fGameGridScale, 0.5f*g_Config.fGameGridScale);
		dc.DrawText(ginfo->id_version, bounds_.x+5, y+1, 0xFF000000, ALIGN_TOPLEFT);
		dc.DrawText(ginfo->id_version, bounds_.x+4, y, dc.GetTheme().infoStyle.fgColor, ALIGN_TOPLEFT);
		dc.SetFontScale(1.0f, 1.0f);
	}

	if (overlayColor) {
		dc.FillRect(Drawable(overlayColor), overlayBounds);
	}

	dc.RebindTexture();
}

std::string GameButton::DescribeText() const {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
	if (!ginfo->Ready(GameInfoFlags::PARAM_SFO))
		return "...";
	auto u = GetI18NCategory(I18NCat::UI_ELEMENTS);
	return ApplySafeSubstitutions(u->T("%1 button"), ginfo->GetTitle());
}

class DirButton : public UI::Button {
public:
	DirButton(const Path &path, bool gridStyle, UI::LayoutParams *layoutParams)
		: UI::Button(path.ToString(), layoutParams), path_(path), gridStyle_(gridStyle), absolute_(false) {}
	DirButton(const Path &path, const std::string &text, bool gridStyle, UI::LayoutParams *layoutParams = 0)
		: UI::Button(text, layoutParams), path_(path), gridStyle_(gridStyle), absolute_(true) {}

	void Draw(UIContext &dc) override;

	const Path &GetPath() const {
		return path_;
	}

	bool PathAbsolute() const {
		return absolute_;
	}

	void SetPinned(bool pin) {
		pinned_ = pin;
	}

private:
	Path path_;
	bool gridStyle_;
	bool absolute_;
	bool pinned_ = false;
};

void DirButton::Draw(UIContext &dc) {
	using namespace UI;

	std::string_view text(GetText());
	ImageID image = ImageID(pinned_ ? "I_FOLDER_PINNED" : "I_FOLDER");
	if (text == "..") {
		image = ImageID("I_UP_DIRECTORY");
		text = "";
	}

	Style style = dc.GetTheme().itemStyle;

	if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
	if (down_) style = dc.GetTheme().itemDownStyle;
	if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

	dc.FillRect(style.background, bounds_);
	DrawIconWithText(dc, image, text, bounds_, gridStyle_, style);
}

GameBrowser::GameBrowser(int token, const Path &path, BrowseFlags browseFlags, bool portrait, bool *gridStyle, ScreenManager *screenManager, std::string_view lastText, std::string_view lastLink, UI::LayoutParams *layoutParams)
	: LinearLayout(ORIENT_VERTICAL, layoutParams), gridStyle_(gridStyle), browseFlags_(browseFlags), portrait_(portrait), lastText_(lastText), lastLink_(lastLink), screenManager_(screenManager), token_(token) {
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
	focusGamePath_ = gamePath;
	Refresh();
	focusGamePath_.clear();
}

void GameBrowser::SetPath(const Path &path) {
	path_.SetPath(path);
	g_Config.currentDirectory = path_.GetPath();
	Refresh();
}

void GameBrowser::ApplySearchFilter(const std::string &filter) {
	searchFilter_ = filter;
	std::transform(searchFilter_.begin(), searchFilter_.end(), searchFilter_.begin(), tolower);

	// We don't refresh because game info loads asynchronously anyway.
	ApplySearchFilter();
}

void GameBrowser::ApplySearchFilter() {
	if (searchFilter_.empty() && searchStates_.empty()) {
		// We haven't hidden anything, and we're not searching, so do nothing.
		searchPending_ = false;
		return;
	}

	searchPending_ = false;
	// By default, everything is matching.
	searchStates_.resize(gameList_->GetNumSubviews(), SearchState::MATCH);

	if (searchFilter_.empty()) {
		// Just quickly mark anything we hid as visible again.
		for (int i = 0; i < gameList_->GetNumSubviews(); ++i) {
			UI::View *v = gameList_->GetViewByIndex(i);
			if (searchStates_[i] != SearchState::MATCH)
				v->SetVisibility(UI::V_VISIBLE);
		}

		searchStates_.clear();
		return;
	}

	for (int i = 0; i < gameList_->GetNumSubviews(); ++i) {
		UI::View *v = gameList_->GetViewByIndex(i);
		std::string label = v->DescribeText();
		// TODO: Maybe we should just save the gameButtons list, though nice to search dirs too?
		// This is a bit of a hack to recognize a pending game title.
		if (label == "...") {
			searchPending_ = true;
			// Hide anything pending while, we'll pop-in search results as they match.
			// Note: we leave it at MATCH if gone before, so we don't show it again.
			if (v->GetVisibility() == UI::V_VISIBLE) {
				if (searchStates_[i] == SearchState::MATCH)
					v->SetVisibility(UI::V_GONE);
				searchStates_[i] = SearchState::PENDING;
			}
			continue;
		}

		std::transform(label.begin(), label.end(), label.begin(), tolower);
		bool match = v->CanBeFocused() && label.find(searchFilter_) != label.npos;
		if (match && searchStates_[i] != SearchState::MATCH) {
			// It was previously visible and force hidden, so show it again.
			v->SetVisibility(UI::V_VISIBLE);
			searchStates_[i] = SearchState::MATCH;
		} else if (!match && searchStates_[i] == SearchState::MATCH && v->GetVisibility() == UI::V_VISIBLE) {
			v->SetVisibility(UI::V_GONE);
			searchStates_[i] = SearchState::MISMATCH;
		}
	}
}

void GameBrowser::LayoutChange(UI::EventParams &e) {
	*gridStyle_ = e.a == 0 ? true : false;
	Refresh();
}

void GameBrowser::LastClick(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, lastLink_.c_str());
}

void GameBrowser::BrowseClick(UI::EventParams &e) {
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	System_BrowseForFolder(token_, mm->T("Choose folder"), path_.GetPath(), [this](const std::string &filename, int) {
		this->SetPath(Path(filename));
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
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH) || defined(USING_WIN_UI) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(IOS)
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
	Refresh();
}

bool GameBrowser::DisplayTopBar() {
	return path_.GetPath().ToString() != "!RECENT";
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
	if (searchPending_) {
		ApplySearchFilter();
	}
}

void GameBrowser::Draw(UIContext &dc) {
	using namespace UI;

	if (lastScale_ != g_Config.fGameGridScale || lastLayoutWasGrid_ != *gridStyle_) {
		Refresh();
	}

	if (hasDropShadow_) {
		// Darken things behind.
		dc.FillRect(UI::Drawable(0x60000000), dc.GetBounds().Expand(dropShadowExpand_));
		float dropsize = 30.0f;
		dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid,
			bounds_.x - dropsize, bounds_.y,
			bounds_.x2() + dropsize, bounds_.y2()+dropsize*1.5f, 0xDF000000, 3.0f);
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

static bool IsValidPBP(const Path &path, bool allowHomebrew) {
	if (!File::Exists(path))
		return false;

	std::unique_ptr<FileLoader> loader(ConstructFileLoader(path));
	PBPReader pbp(loader.get());
	std::vector<u8> sfoData;
	if (!pbp.GetSubFile(PBP_PARAM_SFO, &sfoData))
		return false;

	ParamSFOData sfo;
	sfo.ReadSFO(sfoData);
	if (!allowHomebrew && sfo.GetValueString("DISC_ID").empty())
		return false;

	if (sfo.GetValueString("CATEGORY") == "ME")
		return false;

	return true;
}

void GameBrowser::Refresh() {
	using namespace UI;

	lastScale_ = g_Config.fGameGridScale;
	lastLayoutWasGrid_ = *gridStyle_;

	// Kill all the contents
	Clear();
	searchStates_.clear();

	Add(new Spacer(1.0f));
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	// No topbar on recent screen
	gameList_ = nullptr;

	if (DisplayTopBar()) {
		LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 0, 8, 0)));
		Add(topBar);

		const bool pathOnSeparateLine = g_display.dp_xres < 1050 || portrait_;

		std::string pathStr = GetFriendlyPath(path_.GetPath(), aliasMatch_, aliasDisplay_);

		if (pathOnSeparateLine) {
			Add(new TextView(pathStr, ALIGN_VCENTER | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 0, 8, 0))));
		}
		if (browseFlags_ & BrowseFlags::NAVIGATE) {
			if (!pathOnSeparateLine) {
				topBar->Add(new Spacer(2.0f));
				topBar->Add(new TextView(pathStr, ALIGN_VCENTER | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, 64.0f, 1.0f)));
			}
			topBar->Add(new Choice(ImageID("I_HOME"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::OnHomeClick);
			if (System_GetPropertyBool(SYSPROP_HAS_ADDITIONAL_STORAGE)) {
				topBar->Add(new Choice(ImageID("I_SDCARD"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::StorageClick);
			}
#if PPSSPP_PLATFORM(IOS_APP_STORE)
			// Don't show a browse button, not meaningful to browse outside the documents folder it seems,
			// as we can't list things like document folders of another app, as far as I can tell.
			// However, we do show a Load.. button for picking individual files, that seems to work.
#elif PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
			// on Darwin, we don't show the 'Browse' text alongside the image
			// we show just the image, because we don't need to emphasize the button on Darwin
			topBar->Add(new Choice(ImageID("I_FOLDER_OPEN"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::BrowseClick);
#else
			if ((browseFlags_ & BrowseFlags::BROWSE) && System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER)) {
				// Collapse the button title on very small screens (Retroid Pocket) or portrait mode.
				std::string_view browseTitle = (g_display.dp_xres <= 550 || portrait_) ? "" : mm->T("Browse");
				topBar->Add(new Choice(browseTitle, ImageID("I_FOLDER_OPEN"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::BrowseClick);
			}
			if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV) {
				topBar->Add(new Choice(mm->T("Enter Path"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Add([=](UI::EventParams &) {
					auto mm = GetI18NCategory(I18NCat::MAINMENU);
					System_InputBoxGetString(token_, mm->T("Enter Path"), path_.GetPath().ToString(), false, [=](const char *responseString, int responseValue) {
						this->SetPath(Path(responseString));
					});
				});
			}
#endif
		} else if (!pathOnSeparateLine) {
			topBar->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 64.0f, 1.0f)));
		}

		if (browseFlags_ & BrowseFlags::HOMEBREW_STORE) {
			topBar->Add(new Choice(mm->T("Homebrew store"), ImageID("I_HOMEBREW_STORE"), new UI::LinearLayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::OnHomebrewStore);
		}

		if (browseFlags_ & BrowseFlags::UPLOAD_BUTTON) {
			topBar->Add(new Choice(ImageID("I_FOLDER_UPLOAD"), new UI::LinearLayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Add([this](UI::EventParams &e) {
				screenManager_->push(new UploadScreen(path_.GetPath()));
			});
		}

		ChoiceStrip *layoutChoice = topBar->Add(new ChoiceStrip(ORIENT_HORIZONTAL));
		layoutChoice->AddChoice(ImageID("I_GRID"));
		layoutChoice->AddChoice(ImageID("I_LINES"));
		layoutChoice->SetSelection(*gridStyle_ ? 0 : 1, false);
		layoutChoice->OnChoice.Handle(this, &GameBrowser::LayoutChange);
		topBar->Add(new Choice(ImageID("I_ROTATE_LEFT"), new LayoutParams(64.0f, 64.0f)))->OnClick.Add([=](UI::EventParams &e) {
			path_.Refresh();
			Refresh();
		});
		topBar->Add(new Choice(ImageID("I_GEAR"), new LayoutParams(64.0f, 64.0f)))->OnClick.Handle(this, &GameBrowser::GridSettingsClick);

		if (*gridStyle_) {
			gameList_ = new UI::GridLayoutList(UI::GridLayoutSettings(150*g_Config.fGameGridScale, 85*g_Config.fGameGridScale), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(10, 0, 0, 0)));
		} else {
			UI::LinearLayout *gl = new UI::LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			gl->SetSpacing(4.0f);
			gameList_ = gl;
		}
	} else {
		if (*gridStyle_) {
			gameList_ = new UI::GridLayoutList(UI::GridLayoutSettings(150*g_Config.fGameGridScale, 85*g_Config.fGameGridScale), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(10, 0, 0, 0)));
		} else {
			UI::LinearLayout *gl = new UI::LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			gl->SetSpacing(4.0f);
			gameList_ = gl;
		}
		// Until we can come up with a better space to put it (next to the tabs?) let's get rid of the icon config
		// button on the Recent tab, it's ugly. You can use the button from the other tabs.

		// LinearLayout *gridOptionColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(64.0, 64.0f));
		// gridOptionColumn->Add(new Spacer(12.0));
		// gridOptionColumn->Add(new Choice(ImageID("I_GEAR"), new LayoutParams(64.0f, 64.0f)))->OnClick.Handle(this, &GameBrowser::GridSettingsClick);
		// LinearLayout *grid = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		// gameList_->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.75));
		// grid->Add(gameList_);
		// grid->Add(gridOptionColumn);
		// Add(grid);
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
			gameButtons.push_back(new GameButton(filenames[i], *gridStyle_, new UI::LinearLayoutParams(*gridStyle_ == true ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
		}
	} else if (!listingPending_) {
		std::vector<File::FileInfo> fileInfo;
		path_.GetListing(fileInfo, "iso:cso:chd:pbp:elf:prx:ppdmp:");
		for (size_t i = 0; i < fileInfo.size(); i++) {
			bool isGame = !fileInfo[i].isDirectory;
			bool isSaveData = false;
			// Check if eboot directory
			if (!isGame && path_.GetPath().size() >= 4 && IsValidPBP(path_.GetPath() / fileInfo[i].name / "EBOOT.PBP", true))
				isGame = true;
			else if (!isGame && File::Exists(path_.GetPath() / fileInfo[i].name / "PSP_GAME/SYSDIR"))
				isGame = true;
			else if (!isGame && File::Exists(path_.GetPath() / fileInfo[i].name / "PARAM.SFO"))
				isSaveData = true;

			if (!isGame && !isSaveData) {
				if (browseFlags_ & BrowseFlags::NAVIGATE) {
					dirButtons.push_back(new DirButton(fileInfo[i].fullName, fileInfo[i].name, *gridStyle_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
				}
			} else {
				gameButtons.push_back(new GameButton(fileInfo[i].fullName, *gridStyle_, new UI::LinearLayoutParams(*gridStyle_ == true ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
			}
		}
		// Put RAR/ZIP files at the end to get them out of the way. They're only shown so that people
		// can click them and get an explanation that they need to unpack them. This is necessary due
		// to a flood of support email...
		if (browseFlags_ & BrowseFlags::ARCHIVES) {
			fileInfo.clear();
			path_.GetListing(fileInfo, "zip:rar:r01:7z:");
			if (!fileInfo.empty()) {
				UI::LinearLayout *zl = new UI::LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
				zl->SetSpacing(4.0f);
				Add(zl);
				for (size_t i = 0; i < fileInfo.size(); i++) {
					if (!fileInfo[i].isDirectory) {
						GameButton *b = zl->Add(new GameButton(fileInfo[i].fullName, false, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT)));
						b->OnClick.Handle(this, &GameBrowser::GameButtonClick);
						b->SetHoldEnabled(false);
					}
				}
			}
		}
	}

	if (browseFlags_ & BrowseFlags::NAVIGATE) {
		if (path_.CanNavigateUp()) {
			gameList_->Add(new DirButton(Path(".."), *gridStyle_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
				OnClick.Handle(this, &GameBrowser::NavigateClick);
		}

		// Add any pinned paths before other directories.
		auto pinnedPaths = GetPinnedPaths();
		for (const auto &pinnedPath : pinnedPaths) {
			DirButton *pinnedDir = gameList_->Add(new DirButton(pinnedPath, pinnedPath.GetFilename(), *gridStyle_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
			pinnedDir->OnClick.Handle(this, &GameBrowser::NavigateClick);
			pinnedDir->SetPinned(true);
		}
	}

	if (listingPending_) {
		gameList_->Add(new UI::TextView(mm->T("Loading..."), ALIGN_CENTER, false, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
	}

	for (size_t i = 0; i < dirButtons.size(); i++) {
		gameList_->Add(dirButtons[i])->OnClick.Handle(this, &GameBrowser::NavigateClick);
	}

	for (size_t i = 0; i < gameButtons.size(); i++) {
		GameButton *b = gameList_->Add(gameButtons[i]);
		b->OnClick.Handle(this, &GameBrowser::GameButtonClick);
		b->OnHoldClick.Handle(this, &GameBrowser::GameButtonHoldClick);
		b->OnHighlight.Handle(this, &GameBrowser::GameButtonHighlight);

		if (!focusGamePath_.empty() && b->GamePath() == focusGamePath_) {
			b->SetFocus();
		}
	}

	// Show a button to toggle pinning at the very end.
	if ((browseFlags_ & BrowseFlags::PIN) && !path_.GetPath().empty()) {
		std::string caption = ""; // IsCurrentPathPinned() ? "-" : "+";
		if (!*gridStyle_) {
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
	} else {
		path_.Navigate(text.ToString());
	}
	g_Config.currentDirectory = path_.GetPath();
	Refresh();
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

MainScreen::MainScreen() {
	g_BackgroundAudio.SetGame(Path());
	ignoreBottomInset_ = true;
}

MainScreen::~MainScreen() {
	g_BackgroundAudio.SetGame(Path());
}

#if PPSSPP_PLATFORM(IOS)
constexpr std::string_view getGamesUri = "https://www.ppsspp.org/getgames_ios";
constexpr std::string_view getHomebrewUri = "https://www.ppsspp.org/gethomebrew_ios";
#else
constexpr std::string_view getGamesUri = "https://www.ppsspp.org/getgames";
constexpr std::string_view getHomebrewUri = "https://www.ppsspp.org/gethomebrew";
#endif
constexpr std::string_view remoteGamesUri = "https://www.ppsspp.org/docs/reference/disc-streaming";

void MainScreen::CreateRecentTab() {
	using namespace UI;
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollRecentGames->SetTag("MainScreenRecentGames");

	bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	GameBrowser *tabRecentGames = new GameBrowser(GetRequesterToken(),
		Path("!RECENT"), BrowseFlags::NONE, portrait, &g_Config.bGridView1, screenManager(), "", "",
		new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	scrollRecentGames->Add(tabRecentGames);
	gameBrowsers_.push_back(tabRecentGames);

	tabHolder_->AddTab(mm->T("Recent"), ImageID::invalid(), scrollRecentGames);
	tabRecentGames->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
	tabRecentGames->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
	tabRecentGames->OnHighlight.Handle(this, &MainScreen::OnGameHighlight);
}

GameBrowser *MainScreen::CreateBrowserTab(const Path &path, std::string_view title, std::string_view howToTitle, std::string_view howToUri, BrowseFlags browseFlags, bool *bGridView, float *scrollPos) {
	using namespace UI;
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	ScrollView *scrollView = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollView->SetTag(title);  // Re-use title as tag, should be fine.

	GameBrowser *gameBrowser = new GameBrowser(GetRequesterToken(), path, browseFlags, portrait, bGridView, screenManager(),
		mm->T(howToTitle), howToUri,
		new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	scrollView->Add(gameBrowser);
	gameBrowsers_.push_back(gameBrowser);

	tabHolder_->AddTab(mm->T(title), ImageID::invalid(), scrollView);
	if (scrollPos) {
		scrollView->RememberPosition(scrollPos);
	}

	gameBrowser->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
	gameBrowser->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
	gameBrowser->OnHighlight.Handle(this, &MainScreen::OnGameHighlight);

	return gameBrowser;
}

class LogoView : public UI::AnchorLayout {
public:
	LogoView(bool portrait, UI::LayoutParams *layoutParams) : UI::AnchorLayout(layoutParams), portrait_(portrait) {}
	void Draw(UIContext &dc) override {
		using namespace UI;
		UI::AnchorLayout::Draw(dc);

		const AtlasImage *iconImg = dc.Draw()->GetAtlas()->getImage(GetIconID());
		const AtlasImage *logoImg = dc.Draw()->GetAtlas()->getImage(ImageID("I_LOGO"));
		if (!iconImg) {
			return;
		}

		dc.Draw()->DrawImage(GetIconID(), bounds_.x, bounds_.y, 1.0f);

		if (bounds_.w < iconImg->w + logoImg->w + 36) {
			return;
		}

		dc.Draw()->DrawImage(ImageID("I_LOGO"), bounds_.x + iconImg->w + 8, bounds_.y + 4, 1.0f);

		std::string versionString = PPSSPP_GIT_VERSION;
		// Strip the 'v' from the displayed version, and shorten the commit hash.
		if (versionString.size() > 2) {
			if (versionString[0] == 'v' && isdigit(versionString[1])) {
				versionString = versionString.substr(1);
			}
			if (CountChar(versionString, '-') == 2) {
				// Shorten the commit hash.
				size_t cutPos = versionString.find_last_of('-') + 8;
				versionString = versionString.substr(0, std::min(cutPos, versionString.size()));
			}
		}
		dc.Flush();

		const bool tiny = versionString.size() > 10;

		const FontStyle *style = GetTextStyle(dc, tiny ? TextSize::Tiny : TextSize::Small);
		dc.SetFontStyle(*style);
		dc.DrawText(versionString,
			bounds_.x + iconImg->w + 8,
			bounds_.y + logoImg->h + (tiny ? 8 : 6),
			dc.GetTheme().itemStyle.fgColor);
		dc.SetFontStyle(dc.GetTheme().uiFont);
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *iconImg = dc.Draw()->GetAtlas()->getImage(GetIconID());
		w = iconImg->w;
		h = iconImg->h;
	}

	bool Touch(const TouchInput &touch) override {
		bool retval = UI::AnchorLayout::Touch(touch);
		if (!portrait_ && (touch.flags & TouchInputFlags::DOWN) && bounds_.Contains(touch.x, touch.y) && touch.y >= bounds_.y2() - 20) {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			System_CopyStringToClipboard(PPSSPP_GIT_VERSION);
			g_OSD.Show(OSDType::MESSAGE_INFO, ApplySafeSubstitutions(di->T("Copied to clipboard: %1"), PPSSPP_GIT_VERSION), 1.0f, "copyToClip");
			return true;
		}
		return retval;
	}

private:
	ImageID GetIconID() const {
		return System_GetPropertyBool(SYSPROP_APP_GOLD) ? ImageID("I_ICON_GOLD") : ImageID("I_ICON");
	}

	const bool portrait_;
};

void MainScreen::CreateMainButtons(UI::ViewGroup *parent, bool portrait) {
	using namespace UI;
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	if (portrait) {
		parent->Add(new Spacer(1.0f, new LinearLayoutParams(1.0f)));
	}
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		parent->Add(portrait ? new Choice(ImageID("I_FOLDER_OPEN"), portrait ? new LinearLayoutParams() : nullptr) : new Choice(mm->T("Load", "Load...")))->OnClick.Handle(this, &MainScreen::OnLoadFile);
	}
	parent->Add(portrait ? new Choice(ImageID("I_GEAR"), portrait ? new LinearLayoutParams() : nullptr) : new Choice(mm->T("Game Settings", "Settings")))->OnClick.Handle(this, &MainScreen::OnGameSettings);
	parent->Add(portrait ? new Choice(ImageID("I_INFO"), portrait ? new LinearLayoutParams() : nullptr) : new Choice(mm->T("About PPSSPP")))->OnClick.Handle(this, &MainScreen::OnCredits);

	if (!portrait) {
		parent->Add(new Choice(mm->T("www.ppsspp.org")))->OnClick.Handle(this, &MainScreen::OnPPSSPPOrg);
	}

	if (!System_GetPropertyBool(SYSPROP_APP_GOLD) && (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_VR)) {
		Choice *gold = parent->Add(portrait ? new Choice(ImageID("I_ICON_GOLD"), portrait ? new LinearLayoutParams() : nullptr) : new Choice(mm->T("Buy PPSSPP Gold")));
		gold->OnClick.Add([this](UI::EventParams &) {
			LaunchBuyGold(this->screenManager());
		});
		gold->SetIconRight(ImageID("I_ICON_GOLD"), 0.5f);
		gold->SetImageScale(0.6f);  // for the left-icon in case of vertical.
		gold->SetShine(true);
	}

	if (!portrait) {
		parent->Add(new Spacer(16.0));
	}

	// Remove the exit button in vertical layout on all platforms, just no space.
	bool showExitButton = !portrait;
	// Also, always hide the exit button on mobile platforms that are not supposed to have one.
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	showExitButton = false;
#elif PPSSPP_PLATFORM(ANDROID)
	// Allow it in Android TV only.
	showExitButton = System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV;
#endif
	// Officially, iOS apps should not have exit buttons. Remove it to maximize app store review chances.
	// Additionally, the Exit button creates problems on Android.
	if (showExitButton) {
		parent->Add(new Choice(mm->T("Exit")))->OnClick.Handle(this, &MainScreen::OnExit);
	}
}

void MainScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	const bool vertical = GetDeviceOrientation() == DeviceOrientation::Portrait;

	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 64, TabHolderFlags::Default, nullptr, nullptr, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	ViewGroup *leftColumn = tabHolder_;
	tabHolder_->SetTag("MainScreenGames");
	gameBrowsers_.clear();

	tabHolder_->SetClip(true);

	bool showRecent = g_Config.iMaxRecent > 0;
	bool hasStorageAccess = !System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS) ||
		System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE) == PERMISSION_STATUS_GRANTED;
	bool storageIsTemporary = IsTempPath(GetSysDirectory(DIRECTORY_SAVEDATA)) && !confirmedTemporary_;
	if (showRecent && !hasStorageAccess) {
		showRecent = g_recentFiles.HasAny();
	}

	if (showRecent) {
		CreateRecentTab();
	}

	Button *focusButton = nullptr;
	if (hasStorageAccess) {
		CreateBrowserTab(Path(g_Config.currentDirectory), "Games", "How to get games", getGamesUri, BrowseFlags::STANDARD, &g_Config.bGridView2, &g_Config.fGameListScrollPosition);
		CreateBrowserTab(GetSysDirectory(DIRECTORY_GAME), "Homebrew & Demos", "How to get homebrew & demos", getHomebrewUri, BrowseFlags::HOMEBREW_STORE, &g_Config.bGridView3, &g_Config.fHomebrewScrollPosition);

		if (g_Config.bRemoteTab && !g_Config.sLastRemoteISOServer.empty()) {
			Path remotePath(FormatRemoteISOUrl(g_Config.sLastRemoteISOServer.c_str(), g_Config.iLastRemoteISOPort, RemoteSubdir().c_str()));
			GameBrowser *remoteBrowser = CreateBrowserTab(remotePath, "Remote disc streaming", "Remote disc streaming", remoteGamesUri, BrowseFlags::NAVIGATE, &g_Config.bGridView4, &g_Config.fRemoteScrollPosition);
			remoteBrowser->SetHomePath(remotePath);
		}

		if (g_recentFiles.HasAny()) {
			tabHolder_->SetCurrentTab(std::clamp(g_Config.iDefaultTab, 0, g_Config.bRemoteTab ? 3 : 2), true);
		} else if (g_Config.iMaxRecent > 0) {
			tabHolder_->SetCurrentTab(1, true);	
		}

		if (backFromStore_ || showHomebrewTab) {
			tabHolder_->SetCurrentTab(2, true);
			backFromStore_ = false;
			showHomebrewTab = false;
		}

		if (storageIsTemporary) {
			LinearLayout *buttonHolder = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
			buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));
			focusButton = new Button(mm->T("SavesAreTemporaryIgnore", "Ignore warning"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
			focusButton->SetPadding(32, 16);
			buttonHolder->Add(focusButton)->OnClick.Add([this](UI::EventParams &e) {
				confirmedTemporary_ = true;
				RecreateViews();
			});
			buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));

			leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
			leftColumn->Add(new TextView(mm->T("SavesAreTemporary", "PPSSPP saving in temporary storage"), ALIGN_HCENTER, false));
			leftColumn->Add(new TextView(mm->T("SavesAreTemporaryGuidance", "Extract PPSSPP somewhere to save permanently"), ALIGN_HCENTER, false));
			leftColumn->Add(new Spacer(10.0f));
			leftColumn->Add(buttonHolder);
			leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
		}
	} else {
		if (!showRecent) {
			leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
			// Just so it's destroyed on recreate.
			leftColumn->Add(tabHolder_);
			tabHolder_->SetVisibility(V_GONE);
		}

		LinearLayout *buttonHolder = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));
		focusButton = new Button(mm->T("Give PPSSPP permission to access storage"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT));
		focusButton->SetPadding(32, 16);
		buttonHolder->Add(focusButton)->OnClick.Handle(this, &MainScreen::OnAllowStorage);
		buttonHolder->Add(new Spacer(new LinearLayoutParams(1.0f)));

		leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
		leftColumn->Add(buttonHolder);
		leftColumn->Add(new Spacer(10.0f));
		leftColumn->Add(new TextView(mm->T("PPSSPP can't load games or save right now"), ALIGN_HCENTER, false));
		leftColumn->Add(new Spacer(new LinearLayoutParams(0.1f)));
	}

	if (vertical) {
		LinearLayout *header = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 8, 8, 16)));
		header->SetSpacing(5.0f);
		header->Add(new LogoView(true, new LinearLayoutParams(1.0f)));

		LinearLayout *buttonGroup = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 1.0f, UI::Gravity::G_VCENTER));

		CreateMainButtons(buttonGroup, vertical);
		header->Add(buttonGroup);

		LinearLayout *rootLayout = new LinearLayout(ORIENT_VERTICAL);
		rootLayout->SetSpacing(0.0f);

		leftColumn->ReplaceLayoutParams(new LinearLayoutParams(1.0f));
		rootLayout->Add(header);
		rootLayout->Add(leftColumn);
		root_ = rootLayout;
	} else {
		const Margins actionMenuMargins(0, 10, 10, 0);
		ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
		LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		rightColumnItems->SetSpacing(0.0f);
		ViewGroup *logo = new LogoView(false, new LinearLayoutParams(FILL_PARENT, 80.0f));
#if !defined(MOBILE_DEVICE)
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		ImageID icon(g_Config.bFullScreen ? "I_RESTORE" : "I_FULLSCREEN");
		fullscreenButton_ = logo->Add(new Button(gr->T("FullScreen", "Full Screen"), icon, new AnchorLayoutParams(48, 48, NONE, 0, 0, NONE, Centering::None)));
		fullscreenButton_->SetIgnoreText(true);
		fullscreenButton_->OnClick.Add([this](UI::EventParams &e) {
			if (fullscreenButton_) {
				fullscreenButton_->SetImageID(ImageID(!g_Config.bFullScreen ? "I_RESTORE" : "I_FULLSCREEN"));
			}
			g_Config.bFullScreen = !g_Config.bFullScreen;
			System_ApplyFullscreenState();
		});
#endif
		rightColumnItems->Add(logo);

		LinearLayout *rightColumnChoices = rightColumnItems;
		CreateMainButtons(rightColumnChoices, vertical);

		rightColumn->Add(rightColumnItems);

		root_ = new LinearLayout(ORIENT_HORIZONTAL);
		root_->Add(leftColumn);
		root_->Add(rightColumn);
	}

	if (focusButton) {
		root_->SetDefaultFocusView(focusButton);
	} else if (tabHolder_->GetVisibility() != V_GONE) {
		root_->SetDefaultFocusView(tabHolder_);
	}

	root_->SetTag("mainroot");
}

bool MainScreen::key(const KeyInput &touch) {
	if (touch.flags & KeyInputFlags::DOWN) {
		if (touch.keyCode == NKCODE_CTRL_LEFT || touch.keyCode == NKCODE_CTRL_RIGHT)
			searchKeyModifier_ = true;
		if (touch.keyCode == NKCODE_F && searchKeyModifier_ && System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
			auto se = GetI18NCategory(I18NCat::SEARCH);
			System_InputBoxGetString(GetRequesterToken(), se->T("Search term"), searchFilter_, false, [&](const std::string &value, int) {
				searchFilter_ = StripSpaces(value);
				searchChanged_ = true;
			});
		}
	} else if (touch.flags & KeyInputFlags::UP) {
		if (touch.keyCode == NKCODE_CTRL_LEFT || touch.keyCode == NKCODE_CTRL_RIGHT)
			searchKeyModifier_ = false;
	}

	return UIBaseScreen::key(touch);
}

void MainScreen::OnAllowStorage(UI::EventParams &e) {
	System_AskForPermission(SYSTEM_PERMISSION_STORAGE);
}

void MainScreen::sendMessage(UIMessage message, const char *value) {
	// Always call the base class method first to handle the most common messages.
	UIBaseScreen::sendMessage(message, value);

	if (message == UIMessage::REQUEST_GAME_BOOT) {
		LaunchFile(screenManager(), this, Path(value));
	} else if (message == UIMessage::PERMISSION_GRANTED && !strcmp(value, "storage")) {
		RecreateViews();
	} else if (message == UIMessage::RECENT_FILES_CHANGED) {
		RecreateViews();
	}
}

void MainScreen::update() {
	UIScreen::update();
	UpdateUIState(UISTATE_MENU);

	if (searchChanged_) {
		for (auto browser : gameBrowsers_)
			browser->ApplySearchFilter(searchFilter_);
		searchChanged_ = false;
	}
}

void MainScreen::OnLoadFile(UI::EventParams &e) {
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		System_BrowseForFile(GetRequesterToken(), mm->T("Load"), BrowseFileType::BOOTABLE, [](const std::string &value, int) {
			System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, value);
		});
	}
}

void MainScreen::DrawBackground(UIContext &dc) {
	if (highlightedGamePath_.empty() && prevHighlightedGamePath_.empty()) {
		return;
	}

	if (DrawBackgroundFor(dc, prevHighlightedGamePath_, 1.0f - prevHighlightProgress_)) {
		if (prevHighlightProgress_ < 1.0f) {
			prevHighlightProgress_ += 1.0f / 20.0f;
		}
	}
	if (!highlightedGamePath_.empty()) {
		if (DrawBackgroundFor(dc, highlightedGamePath_, highlightProgress_)) {
			if (highlightProgress_ < 1.0f) {
				highlightProgress_ += 1.0f / 20.0f;
			}
		}
	}
}

bool MainScreen::DrawBackgroundFor(UIContext &dc, const Path &gamePath, float progress) {
	::DrawGameBackground(dc, gamePath, Lin::Vec3(0.f, 0.f, 0.f), progress);
	return true;
}

void MainScreen::OnGameSelected(UI::EventParams &e) {
	Path path(e.s);
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, path, GameInfoFlags::FILE_TYPE);
	if (ginfo->fileType == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY) {
		return;
	}
	if (g_GameManager.GetState() == GameManagerState::INSTALLING)
		return;

	// Restore focus if it was highlighted (e.g. by gamepad.)
	restoreFocusGamePath_ = highlightedGamePath_;
	g_BackgroundAudio.SetGame(path);
	lockBackgroundAudio_ = true;
	screenManager()->push(new GameScreen(path, false));
}

void MainScreen::OnGameHighlight(UI::EventParams &e) {
	using namespace UI;

	Path path(e.s);

	// Don't change when re-highlighting what's already highlighted.
	if (path != highlightedGamePath_ || e.a == FF_LOSTFOCUS) {
		if (!highlightedGamePath_.empty()) {
			if (prevHighlightedGamePath_.empty() || prevHighlightProgress_ >= 0.75f) {
				prevHighlightedGamePath_ = highlightedGamePath_;
				prevHighlightProgress_ = 1.0 - highlightProgress_;
			}
			highlightedGamePath_.clear();
		}
		if (e.a == FF_GOTFOCUS) {
			highlightedGamePath_ = path;
			highlightProgress_ = 0.0f;
		}
	}

	if ((!highlightedGamePath_.empty() || e.a == FF_LOSTFOCUS) && !lockBackgroundAudio_) {
		g_BackgroundAudio.SetGame(highlightedGamePath_);
	}

	lockBackgroundAudio_ = false;
}

void MainScreen::OnGameSelectedInstant(UI::EventParams &e) {
	ScreenManager *screen = screenManager();
	LaunchFile(screen, nullptr, Path(e.s));
}

void MainScreen::OnGameSettings(UI::EventParams &e) {
	// Not passing a game ID, changing the global settings.
	screenManager()->push(new GameSettingsScreen(Path()));
}

void MainScreen::OnCredits(UI::EventParams &e) {
	screenManager()->push(new CreditsScreen());
}

void LaunchBuyGold(ScreenManager *screenManager) {
	if (System_GetPropertyBool(SYSPROP_USE_IAP)) {
		screenManager->push(new IAPScreen(true));
	} else if (System_GetPropertyBool(SYSPROP_USE_APP_STORE)) {
		screenManager->push(new IAPScreen(false));
	} else {
#if PPSSPP_PLATFORM(IOS_APP_STORE)
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/buygold_ios");
#else
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/buygold");
#endif
	}
}

void MainScreen::OnPPSSPPOrg(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org");
}

void MainScreen::OnForums(UI::EventParams &e) {
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://forums.ppsspp.org");
}

void MainScreen::OnExit(UI::EventParams &e) {
	// Let's make sure the config was saved, since it may not have been.
	if (!g_Config.Save("MainScreen::OnExit")) {
		System_Toast("Failed to save settings!\nCheck permissions, or try to restart the device.");
	}

	// Request the framework to exit cleanly.
	System_ExitApp();

	UpdateUIState(UISTATE_EXIT);
}

void MainScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	std::string tag = dialog->tag();
	if (tag == "Store") {
		backFromStore_ = true;
		RecreateViews();
	} else if (tag == "Game") {
		if (!restoreFocusGamePath_.empty() && UI::IsFocusMovementEnabled()) {
			// Prevent the background from fading, since we just were displaying it.
			highlightedGamePath_ = restoreFocusGamePath_;
			highlightProgress_ = 1.0f;

			// Refocus the game button itself.
			int tab = tabHolder_->GetCurrentTab();
			if (tab >= 0 && tab < (int)gameBrowsers_.size()) {
				gameBrowsers_[tab]->FocusGame(restoreFocusGamePath_);
			}

			// Don't get confused next time.
			restoreFocusGamePath_.clear();
		} else {
			// Not refocusing, so we need to stop the audio.
			g_BackgroundAudio.SetGame(Path());
		}
	} else if (tag == "InstallZip") {
		INFO_LOG(Log::System, "InstallZip finished, refreshing");
		if (gameBrowsers_.size() >= 2) {
			gameBrowsers_[1]->RequestRefresh();
		}
	} else if (tag == "IAP") {
		// Gold status may have changed.
		RecreateViews();
	} else if (tag == "Upload") {
		// Files may have been uploaded.
		RecreateViews();
	}
}

void UmdReplaceScreen::CreateViews() {
	using namespace UI;
	Margins actionMenuMargins(0, 100, 15, 0);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, TabHolderFlags::Default, nullptr, nullptr, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0));
	leftColumn->SetTag("UmdReplace");
	leftColumn->SetClip(true);

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(270, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	if (g_Config.iMaxRecent > 0) {
		ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		scrollRecentGames->SetTag("UmdReplaceRecentGames");
		GameBrowser *tabRecentGames = new GameBrowser(GetRequesterToken(),
			Path("!RECENT"), BrowseFlags::NONE, portrait, &g_Config.bGridView1, screenManager(), "", "",
			new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		scrollRecentGames->Add(tabRecentGames);
		leftColumn->AddTab(mm->T("Recent"), ImageID::invalid(), scrollRecentGames);
		tabRecentGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);
		tabRecentGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);
	}
	ScrollView *scrollAllGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollAllGames->SetTag("UmdReplaceAllGames");

	GameBrowser *tabAllGames = new GameBrowser(GetRequesterToken(), Path(g_Config.currentDirectory), BrowseFlags::STANDARD, portrait, &g_Config.bGridView2, screenManager(),
		mm->T("How to get games"), "https://www.ppsspp.org/getgames.html",
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	scrollAllGames->Add(tabAllGames);

	leftColumn->AddTab(mm->T("Games"), ImageID::invalid(), scrollAllGames);

	tabAllGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);

	tabAllGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);

	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		rightColumnItems->Add(new Choice(mm->T("Load", "Load...")))->OnClick.Add([&](UI::EventParams &e) {
			auto mm = GetI18NCategory(I18NCat::MAINMENU);
			System_BrowseForFile(GetRequesterToken(), mm->T("Load"), BrowseFileType::BOOTABLE, [this](const std::string &value, int) {
				__UmdReplace(Path(value));
				TriggerFinish(DR_OK);
			});
		});
	}

	rightColumnItems->Add(new Choice(di->T("Cancel")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnCancel);
	rightColumnItems->Add(new Spacer());
	rightColumnItems->Add(new Choice(mm->T("Game Settings")))->OnClick.Handle(this, &UmdReplaceScreen::OnGameSettings);

	if (g_recentFiles.HasAny()) {
		leftColumn->SetCurrentTab(0, true);
	} else if (g_Config.iMaxRecent > 0) {
		leftColumn->SetCurrentTab(1, true);
	}

	root_ = new LinearLayout(ORIENT_HORIZONTAL);
	root_->Add(leftColumn);
	root_->Add(rightColumn);
}

void UmdReplaceScreen::update() {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update();
}

void UmdReplaceScreen::OnGameSelected(UI::EventParams &e) {
	__UmdReplace(Path(e.s));
	TriggerFinish(DR_OK);
}

void UmdReplaceScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(Path()));
}

void GridSettingsPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayoutList(ORIENT_VERTICAL);

	items->Add(new CheckBox(&g_Config.bGridView1, sy->T("Display Recent on a grid")));
	items->Add(new CheckBox(&g_Config.bGridView2, sy->T("Display Games on a grid")));
	items->Add(new CheckBox(&g_Config.bGridView3, sy->T("Display Homebrew on a grid")));
	static const char *defaultTabs[] = { "Recent", "Games", "Homebrew & Demos" };
	PopupMultiChoice *beziersChoice = items->Add(new PopupMultiChoice(&g_Config.iDefaultTab, sy->T("Default tab"), defaultTabs, 0, ARRAY_SIZE(defaultTabs), I18NCat::MAINMENU, screenManager()));

	items->Add(new ItemHeader(sy->T("Grid icon size")));
	items->Add(new Choice(sy->T("Increase size")))->OnClick.Handle(this, &GridSettingsPopupScreen::GridPlusClick);
	items->Add(new Choice(sy->T("Decrease size")))->OnClick.Handle(this, &GridSettingsPopupScreen::GridMinusClick);

	items->Add(new ItemHeader(sy->T("Display Extra Info")));
	items->Add(new CheckBox(&g_Config.bShowIDOnGameIcon, sy->T("Show ID")));
	items->Add(new CheckBox(&g_Config.bShowRegionOnGameIcon, sy->T("Show region flag")));

	if (g_Config.iMaxRecent > 0) {
		items->Add(new ItemHeader(sy->T("Clear Recent")));
		items->Add(new Choice(sy->T("Clear Recent Games List")))->OnClick.Handle(this, &GridSettingsPopupScreen::OnRecentClearClick);
	}

	scroll->Add(items);
	parent->Add(scroll);
}

void GridSettingsPopupScreen::GridPlusClick(UI::EventParams &e) {
	g_Config.fGameGridScale = std::min(g_Config.fGameGridScale*1.25f, MAX_GAME_GRID_SCALE);
}

void GridSettingsPopupScreen::GridMinusClick(UI::EventParams &e) {
	g_Config.fGameGridScale = std::max(g_Config.fGameGridScale/1.25f, MIN_GAME_GRID_SCALE);
}

void GridSettingsPopupScreen::OnRecentClearClick(UI::EventParams &e) {
	g_recentFiles.Clear();
	OnRecentChanged.Trigger(e);
	TriggerFinish(DR_OK);
}

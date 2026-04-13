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

#include "UI/GameBrowser.h"
#include "UI/RemoteISOScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/Store.h"
#include "UI/UploadScreen.h"
#include "UI/Background.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Util/DarwinFileSystemServices.h" // For the browser

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
		dc.Draw()->DrawImage(image, bounds.x + 72, bounds.centerY(), 0.88f * (gridStyle ? g_Config.fGameGridScale : 1.0), style.fgColor, ALIGN_CENTER);

		float tx = 150;
		int availableWidth = bounds.w - 150;
		float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;
		if (availableWidth < tw) {
			tx -= (1.0f + sin(time_now_d() * 1.5f)) * sineWidth;
			Bounds tb = bounds;
			tb.x = bounds.x + 150;
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
	GameButton(const Path &gamePath, bool gridStyle, UI::LayoutParams *layoutParams = nullptr)
		: UI::Clickable(layoutParams), gridStyle_(gridStyle), gamePath_(gamePath) {}

	void Draw(UIContext &dc) override;
	std::string DescribeText() const override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		if (gridStyle_) {
			w = 144 * g_Config.fGameGridScale;
			h = 80 * g_Config.fGameGridScale;
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
	bool drawBackground = true;
	switch (ginfo->fileType) {
	case IdentifiedFileType::PSP_ELF: imageIcon = ImageID("I_APP"); drawBackground = false; break;
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
	default: break;
	}

	if (ginfo->Ready(GameInfoFlags::ICON)) {
		if (ginfo->icon.texture && drawBackground) {
			texture = ginfo->icon.texture;
		} else if (drawBackground) {
			// No icon, but drawBackground is set. Let's show a plain icon depending on type.
			imageIcon = (ginfo->fileType == IdentifiedFileType::PSP_ISO || ginfo->fileType == IdentifiedFileType::PSP_ISO_NP) ? ImageID("I_UMD") : ImageID("I_APP");
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

	if (gridStyle_ && g_Config.bShowIDOnGameIcon && ginfo->fileType != IdentifiedFileType::PSP_ELF && !ginfo->id_version.empty()) {
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

		if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
		if (down_) style = dc.GetTheme().itemDownStyle;
		if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;

		DrawIconWithText(dc, imageIcon, ginfo->GetTitle(), bounds_, gridStyle_, style);

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
		if (!gridStyle_) {
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
	}

	if (ginfo->hasConfig && !ginfo->id.empty()) {
		const AtlasImage *gearImage = dc.Draw()->GetAtlas()->getImage(ImageID("I_GEAR_SMALL"));
		if (gearImage) {
			if (gridStyle_) {
				dc.Draw()->DrawImage(ImageID("I_GEAR_SMALL"), bounds_.x, y + h - gearImage->h * g_Config.fGameGridScale, g_Config.fGameGridScale);
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
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + bounds_.w - (image->w + 5) * g_Config.fGameGridScale,
					y + h - (image->h + 5) * g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(regionIcons[regionIndex], bounds_.x + 4, y + h - image->h - 5, 1.0f);
			}
		}
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

bool GameBrowser::Key(const KeyInput &input) {
	bool retval = LinearLayout::Key(input);
	if (retval) {
		return true;
	}

	// Only one is visible at a time, so we can just grab all Char input.
	if (input.flags & KeyInputFlags::CHAR) {
		const int unichar = input.keyCode;
		if (unichar >= 0x20) {
			// Insert it! (todo: do it with a string insert)
			char buf[8];
			buf[u8_wc_toutf8(buf, unichar)] = '\0';
			searchFilter_ += buf;
			ApplySearchFilter();
			retval = true;
		}
	} else if (input.flags & KeyInputFlags::DOWN) {
		if (input.keyCode == NKCODE_DEL) {
			if (!searchFilter_.empty()) {
				searchFilter_.pop_back();
				ApplySearchFilter();
				retval = true;
			}
		} else if (!searchFilter_.empty() && input.keyCode == NKCODE_ESCAPE) {
			searchFilter_.clear();
			ApplySearchFilter();
			retval = true;
		}
	}
	return retval;
}

void GameBrowser::SetSearchFilter(const std::string &filter) {
	searchFilter_ = filter;
	// We don't refresh because game info loads asynchronously anyway.
	ApplySearchFilter();
}

void GameBrowser::ApplySearchFilter() {
	if (searchBar_) {
		searchBar_->SetSearchFilter(searchFilter_);
		searchBar_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);
	}

	if (searchFilter_.empty() && searchStates_.empty()) {
		// We haven't hidden anything, and we're not searching, so do nothing.
		searchPending_ = false;
		return;
	}

	std::string filter = searchFilter_;
	std::transform(filter.begin(), filter.end(), filter.begin(), tolower);

	searchPending_ = false;
	// By default, everything is matching.
	searchStates_.resize(gameList_->GetNumSubviews(), SearchState::MATCH);

	if (filter.empty()) {
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
		bool match = v->CanBeFocused() && label.find(filter) != label.npos;
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

void SearchBar::Draw(UIContext &dc) {
	using namespace UI;
	Bounds overlayBounds = bounds_.Inset(12, 0, 12, 0);
	dc.FillRect(dc.GetTheme().itemStyle.background, overlayBounds);
	dc.DrawText(searchFilter_, overlayBounds.x + 10, overlayBounds.centerY(), 0xFFFFFFFF, ALIGN_VCENTER);
}

void SearchBar::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 0;
	h = 0;
	dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, searchFilter_, &w, &h);
	w += 20;  // Padding
	h += 10;
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
			gameList_ = new UI::GridLayoutList(UI::GridLayoutSettings(150 * g_Config.fGameGridScale, 85 * g_Config.fGameGridScale), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(10, 0, 6, 0)));
		} else {
			UI::LinearLayout *gl = new UI::LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			gl->SetSpacing(4.0f);
			gameList_ = gl;
		}
	} else {
		if (*gridStyle_) {
			gameList_ = new UI::GridLayoutList(UI::GridLayoutSettings(150 * g_Config.fGameGridScale, 85 * g_Config.fGameGridScale), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(10, 0, 6, 0)));
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

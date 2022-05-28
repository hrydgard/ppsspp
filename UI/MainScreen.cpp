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
#include "Core/System.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Util/GameManager.h"

#include "UI/BackgroundAudio.h"
#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/Store.h"
#include "UI/InstallZipScreen.h"
#include "Core/Config.h"
#include "Core/Loaders.h"
#include "GPU/GPUInterface.h"
#include "Common/Data/Text/I18n.h"

#include "Core/HLE/sceUmd.h"

bool MainScreen::showHomebrewTab = false;

bool LaunchFile(ScreenManager *screenManager, const Path &path) {
	// Depending on the file type, we don't want to launch EmuScreen at all.
	auto loader = ConstructFileLoader(path);
	if (!loader) {
		return false;
	}

	std::string errorString;
	IdentifiedFileType type = Identify_File(loader, &errorString);
	delete loader;

	switch (type) {
	case IdentifiedFileType::ARCHIVE_ZIP:
		screenManager->push(new InstallZipScreen(path));
		break;
	default:
		// Let the EmuScreen take care of it.
		screenManager->switchScreen(new EmuScreen(path));
		break;
	}
	return true;
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

class GameButton : public UI::Clickable {
public:
	GameButton(const Path &gamePath, bool gridStyle, UI::LayoutParams *layoutParams = 0)
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
	void Touch(const TouchInput &input) override {
		UI::Clickable::Touch(input);
		hovering_ = bounds_.Contains(input.x, input.y);
		if (hovering_ && (input.flags & TOUCH_DOWN)) {
			holdStart_ = time_now_d();
		}
		if (input.flags & TOUCH_UP) {
			holdStart_ = 0;
		}
	}

	bool Key(const KeyInput &key) override {
		std::vector<int> pspKeys;
		bool showInfo = false;

		if (KeyMap::KeyToPspButton(key.deviceId, key.keyCode, &pspKeys)) {
			for (auto it = pspKeys.begin(), end = pspKeys.end(); it != end; ++it) {
				// If the button mapped to triangle, then show the info.
				if (HasFocus() && (key.flags & KEY_UP) && *it == CTRL_TRIANGLE) {
					showInfo = true;
				}
			}
		} else if (hovering_ && key.deviceId == DEVICE_ID_MOUSE && key.keyCode == NKCODE_EXT_MOUSEBUTTON_2) {
			// If it's the right mouse button, and it's not otherwise mapped, show the info also.
			if (key.flags & KEY_DOWN) {
				showInfoPressed_ = true;
			}
			if ((key.flags & KEY_UP) && showInfoPressed_) {
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
	bool hovering_ = false;
};

void GameButton::Draw(UIContext &dc) {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath_, 0);
	Draw::Texture *texture = 0;
	u32 color = 0, shadowColor = 0;
	using namespace UI;

	if (ginfo->icon.texture) {
		texture = ginfo->icon.texture->GetTexture();
	}

	int x = bounds_.x;
	int y = bounds_.y;
	int w = gridStyle_ ? bounds_.w : 144;
	int h = bounds_.h;

	UI::Style style = dc.theme->itemStyle;
	if (down_)
		style = dc.theme->itemDownStyle;

	if (!gridStyle_ || !texture) {
		h = 50;
		if (HasFocus())
			style = down_ ? dc.theme->itemDownStyle : dc.theme->itemFocusedStyle;

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

	Bounds overlayBounds = bounds_;
	u32 overlayColor = 0;
	if (holdEnabled_ && holdStart_ != 0.0) {
		double time_held = time_now_d() - holdStart_;
		overlayColor = whiteAlpha(time_held / 2.5f);
	}

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
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize*1.5f, y - dropsize*1.5f, x + w + dropsize*1.5f, y + h + dropsize*1.5f, alphaMul(color, pulse), 1.0f);
			dc.Draw()->Flush();
		} else {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize, y - dropsize*0.5f, x+w + dropsize, y+h+dropsize*1.5, alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}

		dc.Draw()->Flush();
		dc.GetDrawContext()->BindTexture(0, texture);
		if (holdStart_ != 0.0) {
			double time_held = time_now_d() - holdStart_;
			int holdFrameCount = (int)(time_held * 60.0f);
			if (holdFrameCount > 60) {
				// Blink before launching by holding
				if (((holdFrameCount >> 3) & 1) == 0)
					color = darkenColor(color);
			}
		}
		dc.Draw()->DrawTexRect(x, y, x+w, y+h, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
	}

	char discNumInfo[8];
	if (ginfo->disc_total > 1)
		sprintf(discNumInfo, "-DISC%d", ginfo->disc_number);
	else
		strcpy(discNumInfo, "");

	dc.Draw()->Flush();
	dc.RebindTexture();
	dc.SetFontStyle(dc.theme->uiFont);
	if (!gridStyle_) {
		float tw, th;
		dc.Draw()->Flush();
		dc.PushScissor(bounds_);
		const std::string currentTitle = ginfo->GetTitle();
		if (!currentTitle.empty()) {
			title_ = ReplaceAll(currentTitle + discNumInfo, "&", "&&");
			title_ = ReplaceAll(title_, "\n", " ");
		}

		dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, title_.c_str(), &tw, &th, 0);

		int availableWidth = bounds_.w - 150;
		if (g_Config.bShowIDOnGameIcon) {
			float vw, vh;
			dc.MeasureText(dc.GetFontStyle(), 0.7f, 0.7f, ginfo->id_version.c_str(), &vw, &vh, 0);
			availableWidth -= vw + 20;
			dc.SetFontScale(0.7f, 0.7f);
			dc.DrawText(ginfo->id_version.c_str(), bounds_.x + availableWidth + 160, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
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
		dc.DrawText(title_.c_str(), bounds_.x + tx, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
		if (availableWidth < tw) {
			dc.PopScissor();
		}
		dc.Draw()->Flush();
		dc.PopScissor();
	} else if (!texture) {
		dc.Draw()->Flush();
		dc.PushScissor(bounds_);
		dc.DrawText(title_.c_str(), bounds_.x + 4, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
		dc.Draw()->Flush();
		dc.PopScissor();
	} else {
		dc.Draw()->Flush();
	}
	if (ginfo->hasConfig && !ginfo->id.empty()) {
		const AtlasImage *gearImage = dc.Draw()->GetAtlas()->getImage(ImageID("I_GEAR"));
		if (gearImage) {
			if (gridStyle_) {
				dc.Draw()->DrawImage(ImageID("I_GEAR"), x, y + h - gearImage->h*g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(ImageID("I_GEAR"), x - gearImage->w, y, 1.0f);
			}
		}
	}
	if (g_Config.bShowRegionOnGameIcon && ginfo->region >= 0 && ginfo->region < GAMEREGION_MAX && ginfo->region != GAMEREGION_OTHER) {
		const ImageID regionIcons[GAMEREGION_MAX] = {
			ImageID("I_FLAG_JP"),
			ImageID("I_FLAG_US"),
			ImageID("I_FLAG_EU"),
			ImageID("I_FLAG_HK"),
			ImageID("I_FLAG_AS"),
			ImageID("I_FLAG_KO"),
			ImageID::invalid(),
		};
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(regionIcons[ginfo->region]);
		if (image) {
			if (gridStyle_) {
				dc.Draw()->DrawImage(regionIcons[ginfo->region], x + w - (image->w + 5)*g_Config.fGameGridScale,
							y + h - (image->h + 5)*g_Config.fGameGridScale, g_Config.fGameGridScale);
			} else {
				dc.Draw()->DrawImage(regionIcons[ginfo->region], x - 2 - image->w - 3, y + h - image->h - 5, 1.0f);
			}
		}
	}
	if (gridStyle_ && g_Config.bShowIDOnGameIcon) {
		dc.SetFontScale(0.5f*g_Config.fGameGridScale, 0.5f*g_Config.fGameGridScale);
		dc.DrawText(ginfo->id_version.c_str(), x+5, y+1, 0xFF000000, ALIGN_TOPLEFT);
		dc.DrawText(ginfo->id_version.c_str(), x+4, y, dc.theme->infoStyle.fgColor, ALIGN_TOPLEFT);
		dc.SetFontScale(1.0f, 1.0f);
	}
	if (overlayColor) {
		dc.FillRect(Drawable(overlayColor), overlayBounds);
	}
	dc.RebindTexture();
}

std::string GameButton::DescribeText() const {
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
	auto u = GetI18NCategory("UI Elements");
	return ReplaceAll(u->T("%1 button"), "%1", ginfo->GetTitle());
}

class DirButton : public UI::Button {
public:
	DirButton(const Path &path, bool gridStyle, UI::LayoutParams *layoutParams)
		: UI::Button(path.ToString(), layoutParams), path_(path), gridStyle_(gridStyle), absolute_(false) {}
	DirButton(const Path &path, const std::string &text, bool gridStyle, UI::LayoutParams *layoutParams = 0)
		: UI::Button(text, layoutParams), path_(path), gridStyle_(gridStyle), absolute_(true) {}

	virtual void Draw(UIContext &dc);

	const Path &GetPath() const {
		return path_;
	}

	bool PathAbsolute() const {
		return absolute_;
	}

private:
	Path path_;
	bool gridStyle_;
	bool absolute_;
};

void DirButton::Draw(UIContext &dc) {
	using namespace UI;
	Style style = dc.theme->itemStyle;

	if (HasFocus()) style = dc.theme->itemFocusedStyle;
	if (down_) style = dc.theme->itemDownStyle;
	if (!IsEnabled()) style = dc.theme->itemDisabledStyle;

	dc.FillRect(style.background, bounds_);

	const std::string text = GetText();

	ImageID image = ImageID("I_FOLDER");
	if (text == "..") {
		image = ImageID("I_UP_DIRECTORY");
	}

	float tw, th;
	dc.MeasureText(dc.GetFontStyle(), gridStyle_ ? g_Config.fGameGridScale : 1.0, gridStyle_ ? g_Config.fGameGridScale : 1.0, text.c_str(), &tw, &th, 0);

	bool compact = bounds_.w < 180 * (gridStyle_ ? g_Config.fGameGridScale : 1.0);

	if (gridStyle_) {
		dc.SetFontScale(g_Config.fGameGridScale, g_Config.fGameGridScale);
	}
	if (compact) {
		// No icon, except "up"
		dc.PushScissor(bounds_);
		if (image == ImageID("I_FOLDER")) {
			dc.DrawText(text.c_str(), bounds_.x + 5, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
		} else {
			dc.Draw()->DrawImage(image, bounds_.centerX(), bounds_.centerY(), gridStyle_ ? g_Config.fGameGridScale : 1.0, style.fgColor, ALIGN_CENTER);
		}
		dc.PopScissor();
	} else {
		bool scissor = false;
		if (tw + 150 > bounds_.w) {
			dc.PushScissor(bounds_);
			scissor = true;
		}
		dc.Draw()->DrawImage(image, bounds_.x + 72, bounds_.centerY(), 0.88f*(gridStyle_ ? g_Config.fGameGridScale : 1.0), style.fgColor, ALIGN_CENTER);
		dc.DrawText(text.c_str(), bounds_.x + 150, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);

		if (scissor) {
			dc.PopScissor();
		}
	}
	if (gridStyle_) {
		dc.SetFontScale(1.0, 1.0);
	}
}

GameBrowser::GameBrowser(const Path &path, BrowseFlags browseFlags, bool *gridStyle, ScreenManager *screenManager, std::string lastText, std::string lastLink, UI::LayoutParams *layoutParams)
	: LinearLayout(UI::ORIENT_VERTICAL, layoutParams), path_(path), gridStyle_(gridStyle), browseFlags_(browseFlags), lastText_(lastText), lastLink_(lastLink), screenManager_(screenManager) {
	using namespace UI;
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

UI::EventReturn GameBrowser::LayoutChange(UI::EventParams &e) {
	*gridStyle_ = e.a == 0 ? true : false;
	Refresh();
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::LastClick(UI::EventParams &e) {
	LaunchBrowser(lastLink_.c_str());
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::BrowseClick(UI::EventParams &e) {
	System_SendMessage("browse_folder", "");
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::StorageClick(UI::EventParams &e) {
	std::vector<std::string> storageDirs = System_GetPropertyStringVec(SYSPROP_ADDITIONAL_STORAGE_DIRS);
	if (storageDirs.empty()) {
		// Shouldn't happen - this button shouldn't be clickable.
		return UI::EVENT_DONE;
	}
	if (storageDirs.size() == 1) {
		SetPath(Path(storageDirs[0]));
	} else {
		// TODO: We should popup a dialog letting the user choose one.
		SetPath(Path(storageDirs[0]));
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::OnHomeClick(UI::EventParams &e) {
	if (path_.GetPath().Type() == PathType::CONTENT_URI) {
		Path rootPath = path_.GetPath().GetRootVolume();
		if (rootPath != path_.GetPath()) {
			SetPath(rootPath);
			return UI::EVENT_DONE;
		}
		if (System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
			// There'll be no sensible home, ignore.
			return UI::EVENT_DONE;
		}
	}

	SetPath(HomePath());
	return UI::EVENT_DONE;
}

// TODO: This doesn't make that much sense for Android, especially after scoped storage..
// Maybe we should have no home directory in this case. Or it should just navigate to the root
// of the current folder tree.
Path GameBrowser::HomePath() {
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH) || defined(USING_WIN_UI) || PPSSPP_PLATFORM(UWP)
	return g_Config.memStickDirectory;
#else
	return Path(getenv("HOME"));
#endif
}

UI::EventReturn GameBrowser::PinToggleClick(UI::EventParams &e) {
	auto &pinnedPaths = g_Config.vPinnedPaths;
	const std::string path = File::ResolvePath(path_.GetPath().ToString());
	if (IsCurrentPathPinned()) {
		pinnedPaths.erase(std::remove(pinnedPaths.begin(), pinnedPaths.end(), path), pinnedPaths.end());
	} else {
		pinnedPaths.push_back(path);
	}
	Refresh();
	return UI::EVENT_DONE;
}

bool GameBrowser::DisplayTopBar() {
	return path_.GetPath().ToString() != "!RECENT";
}

bool GameBrowser::HasSpecialFiles(std::vector<Path> &filenames) {
	if (path_.GetPath().ToString() == "!RECENT") {
		filenames.clear();
		for (auto &str : g_Config.recentIsos) {
			filenames.push_back(Path(str));
		}
		return true;
	}
	return false;
}

void GameBrowser::Update() {
	LinearLayout::Update();
	if (listingPending_ && path_.IsListingReady()) {
		Refresh();
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
		dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid,
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

	Add(new Spacer(1.0f));
	auto mm = GetI18NCategory("MainMenu");

	// No topbar on recent screen
	if (DisplayTopBar()) {
		LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		if (browseFlags_ & BrowseFlags::NAVIGATE) {
			topBar->Add(new Spacer(2.0f));
			topBar->Add(new TextView(path_.GetFriendlyPath().c_str(), ALIGN_VCENTER | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, 64.0f, 1.0f)));
			topBar->Add(new Choice(ImageID("I_HOME"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::OnHomeClick);
			if (System_GetPropertyBool(SYSPROP_HAS_ADDITIONAL_STORAGE)) {
				topBar->Add(new Choice(ImageID("I_SDCARD"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::StorageClick);
			}
			if (System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER)) {
				topBar->Add(new Choice(mm->T("Browse"), ImageID("I_FOLDER_OPEN"), new LayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::BrowseClick);
			}
		} else {
			topBar->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 64.0f, 1.0f)));
		}

		if (browseFlags_ & BrowseFlags::HOMEBREW_STORE) {
			topBar->Add(new Choice(mm->T("PPSSPP Homebrew Store"), new UI::LinearLayoutParams(WRAP_CONTENT, 64.0f)))->OnClick.Handle(this, &GameBrowser::OnHomebrewStore);
		}

		ChoiceStrip *layoutChoice = topBar->Add(new ChoiceStrip(ORIENT_HORIZONTAL));
		layoutChoice->AddChoice(ImageID("I_GRID"));
		layoutChoice->AddChoice(ImageID("I_LINES"));
		layoutChoice->SetSelection(*gridStyle_ ? 0 : 1, false);
		layoutChoice->OnChoice.Handle(this, &GameBrowser::LayoutChange);
		topBar->Add(new Choice(ImageID("I_GEAR"), new LayoutParams(64.0f, 64.0f)))->OnClick.Handle(this, &GameBrowser::GridSettingsClick);
		Add(topBar);

		if (*gridStyle_) {
			gameList_ = new UI::GridLayoutList(UI::GridLayoutSettings(150*g_Config.fGameGridScale, 85*g_Config.fGameGridScale), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			Add(gameList_);
		} else {
			UI::LinearLayout *gl = new UI::LinearLayoutList(UI::ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
			gl->SetSpacing(4.0f);
			gameList_ = gl;
			Add(gameList_);
		}
	} else {
		if (*gridStyle_) {
			gameList_ = new UI::GridLayoutList(UI::GridLayoutSettings(150*g_Config.fGameGridScale, 85*g_Config.fGameGridScale), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		} else {
			UI::LinearLayout *gl = new UI::LinearLayout(UI::ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
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
		Add(gameList_);
	}

	// Find games in the current directory and create new ones.
	std::vector<DirButton *> dirButtons;
	std::vector<GameButton *> gameButtons;

	listingPending_ = !path_.IsListingReady();

	std::vector<Path> filenames;
	if (HasSpecialFiles(filenames)) {
		for (size_t i = 0; i < filenames.size(); i++) {
			gameButtons.push_back(new GameButton(filenames[i], *gridStyle_, new UI::LinearLayoutParams(*gridStyle_ == true ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
		}
	} else if (!listingPending_) {
		std::vector<File::FileInfo> fileInfo;
		path_.GetListing(fileInfo, "iso:cso:pbp:elf:prx:ppdmp:");
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
				UI::LinearLayout *zl = new UI::LinearLayoutList(UI::ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
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
			gameList_->Add(new DirButton(Path(std::string("..")), *gridStyle_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
				OnClick.Handle(this, &GameBrowser::NavigateClick);
		}

		// Add any pinned paths before other directories.
		auto pinnedPaths = GetPinnedPaths();
		for (auto it = pinnedPaths.begin(), end = pinnedPaths.end(); it != end; ++it) {
			gameList_->Add(new DirButton(*it, GetBaseName((*it).ToString()), *gridStyle_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
				OnClick.Handle(this, &GameBrowser::NavigateClick);
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
		std::string caption = IsCurrentPathPinned() ? "-" : "+";
		if (!*gridStyle_) {
			caption = IsCurrentPathPinned() ? mm->T("UnpinPath", "Unpin") : mm->T("PinPath", "Pin");
		}
		gameList_->Add(new UI::Button(caption, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
			OnClick.Handle(this, &GameBrowser::PinToggleClick);
	}

	if (path_.GetPath().empty()) {
		Add(new TextView(mm->T("UseBrowseOrLoad", "Use Browse to choose a folder, or Load to choose a file.")));
	}

	if (!lastText_.empty() && gameButtons.empty()) {
		Add(new Spacer());
		Add(new Choice(lastText_, new UI::LinearLayoutParams(UI::WRAP_CONTENT, UI::WRAP_CONTENT)))->OnClick.Handle(this, &GameBrowser::LastClick);
	}
}

bool GameBrowser::IsCurrentPathPinned() {
	const auto paths = g_Config.vPinnedPaths;
	return std::find(paths.begin(), paths.end(), File::ResolvePath(path_.GetPath().ToString())) != paths.end();
}

const std::vector<Path> GameBrowser::GetPinnedPaths() {
#ifndef _WIN32
	static const std::string sepChars = "/";
#else
	static const std::string sepChars = "/\\";
#endif

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

const std::string GameBrowser::GetBaseName(const std::string &path) {
#ifndef _WIN32
	static const std::string sepChars = "/";
#else
	static const std::string sepChars = "/\\";
#endif

	auto trailing = path.find_last_not_of(sepChars);
	if (trailing != path.npos) {
		size_t start = path.find_last_of(sepChars, trailing);
		if (start != path.npos) {
			return path.substr(start + 1, trailing - start);
		}
		return path.substr(0, trailing);
	}

	size_t start = path.find_last_of(sepChars);
	if (start != path.npos) {
		return path.substr(start + 1);
	}
	return path;
}

UI::EventReturn GameBrowser::GameButtonClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2{};
	e2.s = button->GamePath().ToString();
	// Insta-update - here we know we are already on the right thread.
	OnChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::GameButtonHoldClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2{};
	e2.s = button->GamePath().ToString();
	// Insta-update - here we know we are already on the right thread.
	OnHoldChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::GameButtonHighlight(UI::EventParams &e) {
	// Insta-update - here we know we are already on the right thread.
	OnHighlight.Trigger(e);
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::NavigateClick(UI::EventParams &e) {
	DirButton *button = static_cast<DirButton *>(e.v);
	Path text = button->GetPath();
	if (button->PathAbsolute()) {
		path_.SetPath(text);
	} else {
		path_.Navigate(text.ToString());
	}
	g_Config.currentDirectory = path_.GetPath();
	Refresh();
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::GridSettingsClick(UI::EventParams &e) {
	auto sy = GetI18NCategory("System");
	auto gridSettings = new GridSettingsScreen(sy->T("Games list settings"));
	gridSettings->OnRecentChanged.Handle(this, &GameBrowser::OnRecentClear);
	if (e.v)
		gridSettings->SetPopupOrigin(e.v);

	screenManager_->push(gridSettings);
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::OnRecentClear(UI::EventParams &e) {
	screenManager_->RecreateAllViews();
	if (host) {
		host->UpdateUI();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::OnHomebrewStore(UI::EventParams &e) {
	screenManager_->push(new StoreScreen());
	return UI::EVENT_DONE;
}

MainScreen::MainScreen() {
	System_SendMessage("event", "mainscreen");
	g_BackgroundAudio.SetGame(Path());
	lastVertical_ = UseVerticalLayout();
}

MainScreen::~MainScreen() {
	g_BackgroundAudio.SetGame(Path());
}

void MainScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	bool vertical = UseVerticalLayout();

	auto mm = GetI18NCategory("MainMenu");

	Margins actionMenuMargins(0, 10, 10, 0);

	tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	ViewGroup *leftColumn = tabHolder_;
	tabHolder_->SetTag("MainScreenGames");
	gameBrowsers_.clear();

	tabHolder_->SetClip(true);

	bool showRecent = g_Config.iMaxRecent > 0;
	bool hasStorageAccess = !System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS) ||
		System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE) == PERMISSION_STATUS_GRANTED;
	bool storageIsTemporary = IsTempPath(GetSysDirectory(DIRECTORY_SAVEDATA)) && !confirmedTemporary_;
	if (showRecent && !hasStorageAccess) {
		showRecent = !g_Config.recentIsos.empty();
	}

	if (showRecent) {
		ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		scrollRecentGames->SetTag("MainScreenRecentGames");
		GameBrowser *tabRecentGames = new GameBrowser(
			Path("!RECENT"), BrowseFlags::NONE, &g_Config.bGridView1, screenManager(), "", "",
			new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		scrollRecentGames->Add(tabRecentGames);
		gameBrowsers_.push_back(tabRecentGames);

		tabHolder_->AddTab(mm->T("Recent"), scrollRecentGames);
		tabRecentGames->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
		tabRecentGames->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
		tabRecentGames->OnHighlight.Handle(this, &MainScreen::OnGameHighlight);
	}

	Button *focusButton = nullptr;
	if (hasStorageAccess) {
		scrollAllGames_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		scrollAllGames_->SetTag("MainScreenAllGames");
		ScrollView *scrollHomebrew = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		scrollHomebrew->SetTag("MainScreenHomebrew");

		GameBrowser *tabAllGames = new GameBrowser(Path(g_Config.currentDirectory), BrowseFlags::STANDARD, &g_Config.bGridView2, screenManager(),
			mm->T("How to get games"), "https://www.ppsspp.org/getgames.html",
			new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		GameBrowser *tabHomebrew = new GameBrowser(GetSysDirectory(DIRECTORY_GAME), BrowseFlags::HOMEBREW_STORE, &g_Config.bGridView3, screenManager(),
			mm->T("How to get homebrew & demos", "How to get homebrew && demos"), "https://www.ppsspp.org/gethomebrew.html",
			new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

		scrollAllGames_->Add(tabAllGames);
		gameBrowsers_.push_back(tabAllGames);
		scrollHomebrew->Add(tabHomebrew);
		gameBrowsers_.push_back(tabHomebrew);

		tabHolder_->AddTab(mm->T("Games"), scrollAllGames_);
		tabHolder_->AddTab(mm->T("Homebrew & Demos"), scrollHomebrew);
		scrollAllGames_->RememberPosition(&g_Config.fGameListScrollPosition);

		tabAllGames->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
		tabHomebrew->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);

		tabAllGames->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
		tabHomebrew->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);

		tabAllGames->OnHighlight.Handle(this, &MainScreen::OnGameHighlight);
		tabHomebrew->OnHighlight.Handle(this, &MainScreen::OnGameHighlight);

		if (g_Config.recentIsos.size() > 0) {
			tabHolder_->SetCurrentTab(0, true);
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
				return UI::EVENT_DONE;
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
		scrollAllGames_ = nullptr;
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

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL);
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	char versionString[256];
	sprintf(versionString, "%s", PPSSPP_GIT_VERSION);
	rightColumnItems->SetSpacing(0.0f);
	AnchorLayout *logos = new AnchorLayout(new AnchorLayoutParams(FILL_PARENT, 60.0f, false));
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		logos->Add(new ImageView(ImageID("I_ICONGOLD"), "", IS_DEFAULT, new AnchorLayoutParams(64, 64, 0, 0, NONE, NONE, false)));
	} else {
		logos->Add(new ImageView(ImageID("I_ICON"), "", IS_DEFAULT, new AnchorLayoutParams(64, 64, 0, 0, NONE, NONE, false)));
	}
	logos->Add(new ImageView(ImageID("I_LOGO"), "PPSSPP", IS_DEFAULT, new AnchorLayoutParams(180, 64, 64, -5.0f, NONE, NONE, false)));

#if !defined(MOBILE_DEVICE)
	if (!g_Config.UseFullScreen()) {
		auto gr = GetI18NCategory("Graphics");
		ImageID icon(g_Config.UseFullScreen() ? "I_RESTORE" : "I_FULLSCREEN");
		fullscreenButton_ = logos->Add(new Button(gr->T("FullScreen", "Full Screen"), icon, new AnchorLayoutParams(48, 48, NONE, 0, 0, NONE, false)));
		fullscreenButton_->SetIgnoreText(true);
		fullscreenButton_->OnClick.Handle(this, &MainScreen::OnFullScreenToggle);
	}
#endif

	rightColumnItems->Add(logos);
	TextView *ver = rightColumnItems->Add(new TextView(versionString, new LinearLayoutParams(Margins(70, -6, 0, 0))));
	ver->SetSmall(true);
	ver->SetClip(false);

	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		rightColumnItems->Add(new Choice(mm->T("Load", "Load...")))->OnClick.Handle(this, &MainScreen::OnLoadFile);
	}
	rightColumnItems->Add(new Choice(mm->T("Game Settings", "Settings")))->OnClick.Handle(this, &MainScreen::OnGameSettings);
	rightColumnItems->Add(new Choice(mm->T("Credits")))->OnClick.Handle(this, &MainScreen::OnCredits);
	rightColumnItems->Add(new Choice(mm->T("www.ppsspp.org")))->OnClick.Handle(this, &MainScreen::OnPPSSPPOrg);
	if (!System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		Choice *gold = rightColumnItems->Add(new Choice(mm->T("Buy PPSSPP Gold")));
		gold->OnClick.Handle(this, &MainScreen::OnSupport);
		gold->SetIcon(ImageID("I_ICONGOLD"), 0.5f);
	}

#if !PPSSPP_PLATFORM(UWP)
	// Having an exit button is against UWP guidelines.
	rightColumnItems->Add(new Spacer(25.0));
	rightColumnItems->Add(new Choice(mm->T("Exit")))->OnClick.Handle(this, &MainScreen::OnExit);
#endif

	if (vertical) {
		root_ = new LinearLayout(ORIENT_VERTICAL);
		rightColumn->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.75));
		root_->Add(rightColumn);
		root_->Add(leftColumn);
	} else {
		root_ = new LinearLayout(ORIENT_HORIZONTAL);
		rightColumn->ReplaceLayoutParams(new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
		root_->Add(leftColumn);
		root_->Add(rightColumn);
	}

	if (focusButton) {
		root_->SetDefaultFocusView(focusButton);
	} else if (tabHolder_->GetVisibility() != V_GONE) {
		root_->SetDefaultFocusView(tabHolder_);
	}

	auto u = GetI18NCategory("Upgrade");

	upgradeBar_ = 0;
	if (!g_Config.upgradeMessage.empty()) {
		upgradeBar_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

		UI::Margins textMargins(10, 5);
		UI::Margins buttonMargins(0, 0);
		UI::Drawable solid(0xFFbd9939);
		upgradeBar_->SetBG(solid);
		upgradeBar_->Add(new TextView(u->T("New version of PPSSPP available") + std::string(": ") + g_Config.upgradeVersion, new LinearLayoutParams(1.0f, textMargins)));
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(WINDOWS)
		upgradeBar_->Add(new Button(u->T("Download"), new LinearLayoutParams(buttonMargins)))->OnClick.Handle(this, &MainScreen::OnDownloadUpgrade);
#else
		upgradeBar_->Add(new Button(u->T("Details"), new LinearLayoutParams(buttonMargins)))->OnClick.Handle(this, &MainScreen::OnDownloadUpgrade);
#endif
		upgradeBar_->Add(new Button(u->T("Dismiss"), new LinearLayoutParams(buttonMargins)))->OnClick.Handle(this, &MainScreen::OnDismissUpgrade);

		// Slip in under root_
		LinearLayout *newRoot = new LinearLayout(ORIENT_VERTICAL);
		newRoot->Add(root_);
		newRoot->Add(upgradeBar_);
		root_->ReplaceLayoutParams(new LinearLayoutParams(1.0));
		root_ = newRoot;
	}
}

UI::EventReturn MainScreen::OnAllowStorage(UI::EventParams &e) {
	System_AskForPermission(SYSTEM_PERMISSION_STORAGE);
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnDownloadUpgrade(UI::EventParams &e) {
#if PPSSPP_PLATFORM(ANDROID)
	// Go to app store
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		LaunchBrowser("market://details?id=org.ppsspp.ppssppgold");
	} else {
		LaunchBrowser("market://details?id=org.ppsspp.ppsspp");
	}
#elif PPSSPP_PLATFORM(WINDOWS)
	LaunchBrowser("https://www.ppsspp.org/downloads.html");
#else
	// Go directly to ppsspp.org and let the user sort it out
	// (for details and in case downloads doesn't have their platform.)
	LaunchBrowser("https://www.ppsspp.org/");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnDismissUpgrade(UI::EventParams &e) {
	g_Config.DismissUpgrade();
	upgradeBar_->SetVisibility(UI::V_GONE);
	return UI::EVENT_DONE;
}

void MainScreen::sendMessage(const char *message, const char *value) {
	// Always call the base class method first to handle the most common messages.
	UIScreenWithBackground::sendMessage(message, value);

	if (screenManager()->topScreen() == this) {
		if (!strcmp(message, "boot")) {
			LaunchFile(screenManager(), Path(std::string(value)));
		}
		if (!strcmp(message, "browse_fileSelect")) {
			INFO_LOG(SYSTEM, "Attempting to launch: '%s'", value);
			LaunchFile(screenManager(), Path(std::string(value)));
		}
		if (!strcmp(message, "browse_folderSelect")) {
			std::string filename = value;
			INFO_LOG(SYSTEM, "Got folder: '%s'", filename.c_str());
			int tab = tabHolder_->GetCurrentTab();
			// Don't allow browsing in the other tabs (I don't think it's possible to reach the option though)
			if (tab == 1) {
				gameBrowsers_[tab]->SetPath(Path(filename));
			}
		}
	}
	if (!strcmp(message, "permission_granted") && !strcmp(value, "storage")) {
		RecreateViews();
	}
}

void MainScreen::update() {
	UIScreen::update();
	UpdateUIState(UISTATE_MENU);
	bool vertical = UseVerticalLayout();
	if (vertical != lastVertical_) {
		RecreateViews();
		lastVertical_ = vertical;
	}
}

bool MainScreen::UseVerticalLayout() const {
	return dp_yres > dp_xres * 1.1f;
}

UI::EventReturn MainScreen::OnLoadFile(UI::EventParams &e) {
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		System_SendMessage("browse_file", "");
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnFullScreenToggle(UI::EventParams &e) {
	if (g_Config.iForceFullScreen != -1)
		g_Config.bFullScreen = g_Config.UseFullScreen();
	if (fullscreenButton_) {
		fullscreenButton_->SetImageID(ImageID(!g_Config.UseFullScreen() ? "I_RESTORE" : "I_FULLSCREEN"));
	}
#if !defined(MOBILE_DEVICE)
	g_Config.bFullScreen = !g_Config.bFullScreen;
	System_SendMessage("toggle_fullscreen", "");
#endif
	return UI::EVENT_DONE;
}

void MainScreen::DrawBackground(UIContext &dc) {
	UIScreenWithBackground::DrawBackground(dc);
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
	dc.Flush();

	std::shared_ptr<GameInfo> ginfo;
	if (!gamePath.empty()) {
		ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), gamePath, GAMEINFO_WANTBG);
		// Loading texture data may bind a texture.
		dc.RebindTexture();

		// Let's not bother if there's no picture.
		if (!ginfo || (!ginfo->pic1.texture && !ginfo->pic0.texture)) {
			return false;
		}
	} else {
		return false;
	}

	auto pic = ginfo->GetBGPic();
	Draw::Texture *texture = pic ? pic->texture->GetTexture() : nullptr;

	uint32_t color = whiteAlpha(ease(progress)) & 0xFFc0c0c0;
	if (texture) {
		dc.GetDrawContext()->BindTexture(0, texture);
		dc.Draw()->DrawTexRect(dc.GetBounds(), 0, 0, 1, 1, color);
		dc.Flush();
		dc.RebindTexture();
	}
	return true;
}

UI::EventReturn MainScreen::OnGameSelected(UI::EventParams &e) {
	g_Config.Save("MainScreen::OnGameSelected");
	Path path(e.s);
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, path, GAMEINFO_WANTBG);
	if (ginfo && ginfo->fileType == IdentifiedFileType::PSP_SAVEDATA_DIRECTORY) {
		return UI::EVENT_DONE;
	}

	if (g_GameManager.GetState() == GameManagerState::INSTALLING)
		return UI::EVENT_DONE;

	// Restore focus if it was highlighted (e.g. by gamepad.)
	restoreFocusGamePath_ = highlightedGamePath_;
	g_BackgroundAudio.SetGame(path);
	lockBackgroundAudio_ = true;
	screenManager()->push(new GameScreen(path));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameHighlight(UI::EventParams &e) {
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
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSelectedInstant(UI::EventParams &e) {
	g_Config.Save("MainScreen::OnGameSelectedInstant");
	ScreenManager *screen = screenManager();
	LaunchFile(screen, Path(e.s));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(Path(), ""));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnCredits(UI::EventParams &e) {
	screenManager()->push(new CreditsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnSupport(UI::EventParams &e) {
#ifdef __ANDROID__
	LaunchBrowser("market://details?id=org.ppsspp.ppssppgold");
#else
	LaunchBrowser("https://central.ppsspp.org/buygold");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnPPSSPPOrg(UI::EventParams &e) {
	LaunchBrowser("https://www.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnForums(UI::EventParams &e) {
	LaunchBrowser("https://forums.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnExit(UI::EventParams &e) {
	// Let's make sure the config was saved, since it may not have been.
	if (!g_Config.Save("MainScreen::OnExit")) {
		System_SendMessage("toast", "Failed to save settings!\nCheck permissions, or try to restart the device.");
	}

	// Request the framework to exit cleanly.
	System_SendMessage("finish", "");

	UpdateUIState(UISTATE_EXIT);
	return UI::EVENT_DONE;
}

void MainScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (dialog->tag() == "store") {
		backFromStore_ = true;
		RecreateViews();
	}
	if (dialog->tag() == "game") {
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
	}
}

void UmdReplaceScreen::CreateViews() {
	using namespace UI;
	Margins actionMenuMargins(0, 100, 15, 0);
	auto mm = GetI18NCategory("MainMenu");
	auto di = GetI18NCategory("Dialog");

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0));
	leftColumn->SetTag("UmdReplace");
	leftColumn->SetClip(true);

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(270, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	if (g_Config.iMaxRecent > 0) {
		ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		scrollRecentGames->SetTag("UmdReplaceRecentGames");
		GameBrowser *tabRecentGames = new GameBrowser(
			Path("!RECENT"), BrowseFlags::NONE, &g_Config.bGridView1, screenManager(), "", "",
			new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		scrollRecentGames->Add(tabRecentGames);
		leftColumn->AddTab(mm->T("Recent"), scrollRecentGames);
		tabRecentGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelectedInstant);
		tabRecentGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);
	}
	ScrollView *scrollAllGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollAllGames->SetTag("UmdReplaceAllGames");

	GameBrowser *tabAllGames = new GameBrowser(Path(g_Config.currentDirectory), BrowseFlags::STANDARD, &g_Config.bGridView2, screenManager(),
		mm->T("How to get games"), "https://www.ppsspp.org/getgames.html",
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	scrollAllGames->Add(tabAllGames);

	leftColumn->AddTab(mm->T("Games"), scrollAllGames);

	tabAllGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelectedInstant);

	tabAllGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);

	rightColumnItems->Add(new Choice(di->T("Cancel")))->OnClick.Handle(this, &UmdReplaceScreen::OnCancel);
	rightColumnItems->Add(new Choice(mm->T("Game Settings")))->OnClick.Handle(this, &UmdReplaceScreen::OnGameSettings);

	if (g_Config.recentIsos.size() > 0) {
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

UI::EventReturn UmdReplaceScreen::OnGameSelected(UI::EventParams &e) {
	__UmdReplace(Path(e.s));
	TriggerFinish(DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn UmdReplaceScreen::OnCancel(UI::EventParams &e) {
	TriggerFinish(DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn UmdReplaceScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(Path()));
	return UI::EVENT_DONE;
}

UI::EventReturn UmdReplaceScreen::OnGameSelectedInstant(UI::EventParams &e) {
	__UmdReplace(Path(e.s));
	TriggerFinish(DR_OK);
	return UI::EVENT_DONE;
}

void GridSettingsScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory("Dialog");
	auto sy = GetI18NCategory("System");

	ScrollView *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0f));
	LinearLayout *items = new LinearLayoutList(ORIENT_VERTICAL);

	items->Add(new CheckBox(&g_Config.bGridView1, sy->T("Display Recent on a grid")));
	items->Add(new CheckBox(&g_Config.bGridView2, sy->T("Display Games on a grid")));
	items->Add(new CheckBox(&g_Config.bGridView3, sy->T("Display Homebrew on a grid")));

	items->Add(new ItemHeader(sy->T("Grid icon size")));
	items->Add(new Choice(sy->T("Increase size")))->OnClick.Handle(this, &GridSettingsScreen::GridPlusClick);
	items->Add(new Choice(sy->T("Decrease size")))->OnClick.Handle(this, &GridSettingsScreen::GridMinusClick);

	items->Add(new ItemHeader(sy->T("Display Extra Info")));
	items->Add(new CheckBox(&g_Config.bShowIDOnGameIcon, sy->T("Show ID")));
	items->Add(new CheckBox(&g_Config.bShowRegionOnGameIcon, sy->T("Show region flag")));

	if (g_Config.iMaxRecent > 0) {
		items->Add(new ItemHeader(sy->T("Clear Recent")));
		items->Add(new Choice(sy->T("Clear Recent Games List")))->OnClick.Handle(this, &GridSettingsScreen::OnRecentClearClick);
	}

	scroll->Add(items);
	parent->Add(scroll);
}

UI::EventReturn GridSettingsScreen::GridPlusClick(UI::EventParams &e) {
	g_Config.fGameGridScale = std::min(g_Config.fGameGridScale*1.25f, MAX_GAME_GRID_SCALE);
	return UI::EVENT_DONE;
}

UI::EventReturn GridSettingsScreen::GridMinusClick(UI::EventParams &e) {
	g_Config.fGameGridScale = std::max(g_Config.fGameGridScale/1.25f, MIN_GAME_GRID_SCALE);
	return UI::EVENT_DONE;
}

UI::EventReturn GridSettingsScreen::OnRecentClearClick(UI::EventParams &e) {
	g_Config.recentIsos.clear();
	OnRecentChanged.Trigger(e);
	return UI::EVENT_DONE;
}

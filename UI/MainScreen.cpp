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

#include <cmath>
#include <algorithm>

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "file/path.h"
#include "gfx_es2/draw_buffer.h"
#include "math/curves.h"
#include "base/stringutil.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

#include "Common/FileUtil.h"
#include "Core/System.h"
#include "Core/Host.h"
#include "Core/SaveState.h"

#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/CwCheatScreen.h"
#include "UI/MiscScreens.h"
#include "UI/ControlMappingScreen.h"
#include "UI/Store.h"
#include "UI/ui_atlas.h"
#include "Core/Config.h"
#include "GPU/GPUInterface.h"
#include "i18n/i18n.h"

#include "Core/HLE/sceUmd.h"

#ifdef _WIN32
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/WndMainWindow.h"
#endif

#ifdef USING_QT_UI
#include <QFileDialog>
#include <QFile>
#include <QDir>
#endif

#include <sstream>

bool MainScreen::showHomebrewTab = false;

class GameButton : public UI::Clickable {
public:
	GameButton(const std::string &gamePath, bool gridStyle, UI::LayoutParams *layoutParams = 0) 
		: UI::Clickable(layoutParams), gridStyle_(gridStyle), gamePath_(gamePath), holdFrameCount_(0), holdEnabled_(true) {}

	virtual void Draw(UIContext &dc);
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const {
		if (gridStyle_) {
			w = 144;
			h = 80;
		} else {
			w = 500;
			h = 50;
		}
	}

	const std::string &GamePath() const { return gamePath_; }

	void SetHoldEnabled(bool hold) {
		holdEnabled_ = hold;
	}
	virtual void Touch(const TouchInput &input) {
		UI::Clickable::Touch(input);
		hovering_ = bounds_.Contains(input.x, input.y);
		if (input.flags & TOUCH_UP) {
			holdFrameCount_ = 0;
		}
	}

	virtual void Key(const KeyInput &key) {
		std::vector<int> pspKeys;
		bool showInfo = false;

		if (HasFocus() && (key.flags & KEY_UP) && KeyMap::KeyToPspButton(key.deviceId, key.keyCode, &pspKeys)) {
			for (auto it = pspKeys.begin(), end = pspKeys.end(); it != end; ++it) {
				// If the button mapped to triangle, then show the info.
				if (*it == CTRL_TRIANGLE) {
					showInfo = true;
				}
			}
		} else if (hovering_ && key.deviceId == DEVICE_ID_MOUSE && key.keyCode == NKCODE_EXT_MOUSEBUTTON_2) {
			// If it's the right mouse button, and it's not otherwise mapped, show the info also.
			if (key.flags & KEY_UP) {
				showInfo = true;
			}
		}

		if (showInfo) {
			TriggerOnHoldClick();
			return;
		}

		Clickable::Key(key);
	}

	virtual void Update(const InputState &input_state) {
		if (down_ && holdEnabled_)
			holdFrameCount_++;
		else
			holdFrameCount_ = 0;
		// Hold button for 1.5 seconds to launch the game options
		if (holdFrameCount_ > 90) {
			TriggerOnHoldClick();
		}
	}

	UI::Event OnHoldClick;

private:
	void TriggerOnHoldClick() {
		holdFrameCount_ = 0;
		UI::EventParams e;
		e.v = this;
		e.s = gamePath_;
		down_ = false;
		OnHoldClick.Trigger(e);
	}

	bool gridStyle_;
	std::string gamePath_;
	std::string title_;

	int holdFrameCount_;
	bool holdEnabled_;
	bool hovering_;
};

void GameButton::Draw(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, false);
	Texture *texture = 0;
	u32 color = 0, shadowColor = 0;

	if (ginfo->iconTexture) {
		texture = ginfo->iconTexture;
	}

	int x = bounds_.x;
	int y = bounds_.y;
	int w = 144;
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
		color = whiteAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 2));
		shadowColor = blackAlpha(ease((time_now_d() - ginfo->timeIconWasLoaded) * 2));
		float tw = texture->Width();
		float th = texture->Height();

		// Adjust position so we don't stretch the image vertically or horizontally.
		// TODO: Add a param to specify fit?  The below assumes it's never too wide.
		float nw = h * tw / th;
		x += (w - nw) / 2.0f;
		w = nw;
	}

	int txOffset = down_ ? 4 : 0;
	if (!gridStyle_) txOffset = 0;

	Bounds overlayBounds = bounds_;
	u32 overlayColor = 0;
	if (holdEnabled_)
		overlayColor = whiteAlpha((holdFrameCount_ - 15) * 0.01f);

	// Render button
	int dropsize = 10;
	if (texture) {
		if (txOffset) {
			dropsize = 3;
			y += txOffset * 2;
			overlayBounds.y += txOffset * 2;
		}
		if (HasFocus()) {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize*1.5f, y - dropsize*1.5f, x+w + dropsize*1.5f, y+h+dropsize*1.5f, alphaMul(color, 1.0f), 1.0f);
			dc.Draw()->Flush();
		} else {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(dc.theme->dropShadow4Grid, x - dropsize, y - dropsize*0.5f, x+w + dropsize, y+h+dropsize*1.5, alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}
	}

	if (texture) {
		dc.Draw()->Flush();
		texture->Bind(0);
		if (holdFrameCount_ > 60) {
			// Blink before launching by holding
			if (((holdFrameCount_ >> 3) & 1) == 0)
				color = darkenColor(color);
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
		if (title_.empty() && !ginfo->title.empty()) {
			title_ = ReplaceAll(ginfo->title + discNumInfo, "&", "&&");
			title_ = ReplaceAll(title_, "\n", " ");
		}

		dc.MeasureText(dc.GetFontStyle(), title_.c_str(), &tw, &th, 0);

		int availableWidth = bounds_.w - 150;
		float sineWidth = std::max(0.0f, (tw - availableWidth)) / 2.0f;

		float tx = 150;
		if (availableWidth < tw) {
			tx -= (1.0f + sin(time_now_d() * 1.5f)) * sineWidth;
			Bounds tb = bounds_;
			tb.x = bounds_.x + 150;
			tb.w = bounds_.w - 150;
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
	if (overlayColor) {
		dc.FillRect(Drawable(overlayColor), overlayBounds);
	}
	dc.RebindTexture();
}

enum GameBrowserFlags {
	FLAG_HOMEBREWSTOREBUTTON = 1
};


class DirButton : public UI::Button {
public:
	DirButton(const std::string &path, UI::LayoutParams *layoutParams)
		: UI::Button(path, layoutParams), path_(path), absolute_(false) {}
	DirButton(const std::string &path, const std::string &text, LayoutParams *layoutParams = 0)
		: UI::Button(text, layoutParams), path_(path), absolute_(true) {}

	virtual void Draw(UIContext &dc);

	const std::string GetPath() const {
		return path_;
	}

	bool PathAbsolute() const {
		return absolute_;
	}

private:

	std::string path_;
	bool absolute_;
};

void DirButton::Draw(UIContext &dc) {
	Style style = dc.theme->buttonStyle;

	if (HasFocus()) style = dc.theme->buttonFocusedStyle;
	if (down_) style = dc.theme->buttonDownStyle;
	if (!IsEnabled()) style = dc.theme->buttonDisabledStyle;

	dc.FillRect(style.background, bounds_);

	const std::string text = GetText();

	int image = I_FOLDER;
	if (text == "..") {
		image = I_UP_DIRECTORY;
	}
	
	float tw, th;
	dc.MeasureText(dc.GetFontStyle(), text.c_str(), &tw, &th, 0);

	bool compact = bounds_.w < 180;

	if (compact) {
		// No icon, except "up"
		dc.PushScissor(bounds_);
		if (image == I_FOLDER) {
			dc.DrawText(text.c_str(), bounds_.x + 5, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
		} else {
			dc.Draw()->DrawImage(image, bounds_.centerX(), bounds_.centerY(), 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
		}
		dc.PopScissor();
	} else {
		bool scissor = false;
		if (tw + 150 > bounds_.w) {
			dc.PushScissor(bounds_);
			scissor = true;
		}

		dc.Draw()->DrawImage(image, bounds_.x + 72, bounds_.centerY(), .88f, 0xFFFFFFFF, ALIGN_CENTER);
		dc.DrawText(text.c_str(), bounds_.x + 150, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);

		if (scissor) {
			dc.PopScissor();
		}
	}
}

class GameBrowser : public UI::LinearLayout {
public:
	GameBrowser(std::string path, bool allowBrowsing, bool *gridStyle_, std::string lastText, std::string lastLink, int flags = 0, UI::LayoutParams *layoutParams = 0);

	UI::Event OnChoice;
	UI::Event OnHoldChoice;

	UI::Choice *HomebrewStoreButton() { return homebrewStoreButton_; }
private:
	void Refresh();
	bool IsCurrentPathPinned();
	const std::vector<std::string> GetPinnedPaths();
	const std::string GetBaseName(const std::string &path);

	UI::EventReturn GameButtonClick(UI::EventParams &e);
	UI::EventReturn GameButtonHoldClick(UI::EventParams &e);
	UI::EventReturn NavigateClick(UI::EventParams &e);
	UI::EventReturn LayoutChange(UI::EventParams &e);
	UI::EventReturn LastClick(UI::EventParams &e);
	UI::EventReturn HomeClick(UI::EventParams &e);
	UI::EventReturn PinToggleClick(UI::EventParams &e);

	UI::ViewGroup *gameList_;
	PathBrowser path_;
	bool *gridStyle_;
	bool allowBrowsing_;
	std::string lastText_;
	std::string lastLink_;
	int flags_;
	UI::Choice *homebrewStoreButton_;
};

GameBrowser::GameBrowser(std::string path, bool allowBrowsing, bool *gridStyle, std::string lastText, std::string lastLink, int flags, UI::LayoutParams *layoutParams)
	: LinearLayout(UI::ORIENT_VERTICAL, layoutParams), gameList_(0), path_(path), gridStyle_(gridStyle), allowBrowsing_(allowBrowsing), lastText_(lastText), lastLink_(lastLink), flags_(flags) {
	using namespace UI;
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

UI::EventReturn GameBrowser::HomeClick(UI::EventParams &e) {
#ifdef ANDROID
	path_.SetPath(g_Config.memCardDirectory);
#elif defined(USING_QT_UI)
	I18NCategory *m = GetI18NCategory("MainMenu");
	QString fileName = QFileDialog::getExistingDirectory(NULL, "Browse for Folder", g_Config.currentDirectory.c_str());
	if (QDir(fileName).exists())
		path_.SetPath(fileName.toStdString());
	else
		return UI::EVENT_DONE;
#elif defined(_WIN32)
	I18NCategory *m = GetI18NCategory("MainMenu");
	std::string folder = W32Util::BrowseForFolder(MainWindow::GetHWND(), m->T("Choose folder"));
	if (!folder.size())
		return UI::EVENT_DONE;
	path_.SetPath(folder);
#elif defined(BLACKBERRY)
	path_.SetPath(std::string(getenv("PERIMETER_HOME")) + "/shared/misc");
#else
	path_.SetPath(getenv("HOME"));
#endif

	g_Config.currentDirectory = path_.GetPath();
	Refresh();
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::PinToggleClick(UI::EventParams &e) {
	auto &pinnedPaths = g_Config.vPinnedPaths;
	if (IsCurrentPathPinned()) {
		pinnedPaths.erase(std::remove(pinnedPaths.begin(), pinnedPaths.end(), path_.GetPath()), pinnedPaths.end());
	} else {
		pinnedPaths.push_back(path_.GetPath());
	}
	Refresh();
	return UI::EVENT_DONE;
}

void GameBrowser::Refresh() {
	using namespace UI;

	homebrewStoreButton_ = 0;
	// Kill all the contents
	Clear();

	Add(new Spacer(1.0f));
	I18NCategory *m = GetI18NCategory("MainMenu");

	// No topbar on recent screen
	if (path_.GetPath() != "!RECENT") {
		LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		if (allowBrowsing_) {
			topBar->Add(new Spacer(2.0f));
			Margins pathMargins(5, 0);
			topBar->Add(new TextView(path_.GetFriendlyPath().c_str(), ALIGN_VCENTER, true, new LinearLayoutParams(1.0f)));
#if defined(_WIN32) || defined(USING_QT_UI)
			topBar->Add(new Choice(m->T("Browse", "Browse...")))->OnClick.Handle(this, &GameBrowser::HomeClick);
#else
			topBar->Add(new Choice(m->T("Home")))->OnClick.Handle(this, &GameBrowser::HomeClick);
#endif
		} else {
			topBar->Add(new Spacer(new LinearLayoutParams(1.0f)));
		}

		ChoiceStrip *layoutChoice = topBar->Add(new ChoiceStrip(ORIENT_HORIZONTAL));
		layoutChoice->AddChoice(I_GRID);
		layoutChoice->AddChoice(I_LINES);
		layoutChoice->SetSelection(*gridStyle_ ? 0 : 1);
		layoutChoice->OnChoice.Handle(this, &GameBrowser::LayoutChange);
		Add(topBar);
	}

	if (*gridStyle_) {
		gameList_ = new UI::GridLayout(UI::GridLayoutSettings(150, 85), new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	} else {
		UI::LinearLayout *gl = new UI::LinearLayout(UI::ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		gl->SetSpacing(4.0f);
		gameList_ = gl;
	}
	Add(gameList_);

	// Find games in the current directory and create new ones.
	std::vector<DirButton *> dirButtons;
	std::vector<GameButton *> gameButtons;

	if (path_.GetPath() == "!RECENT") {
		for (size_t i = 0; i < g_Config.recentIsos.size(); i++) {
			gameButtons.push_back(new GameButton(g_Config.recentIsos[i], *gridStyle_, new UI::LinearLayoutParams(*gridStyle_ == true ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
		}
	} else {
		std::vector<FileInfo> fileInfo;
		path_.GetListing(fileInfo, "iso:cso:pbp:elf:prx:");
		for (size_t i = 0; i < fileInfo.size(); i++) {
			bool isGame = !fileInfo[i].isDirectory;
			// Check if eboot directory
			if (!isGame && path_.GetPath().size() >= 4 && File::Exists(path_.GetPath() + fileInfo[i].name + "/EBOOT.PBP"))
				isGame = true;
			else if (!isGame && File::Exists(path_.GetPath() + fileInfo[i].name + "/PSP_GAME/SYSDIR"))
				isGame = true;

			if (!isGame) {
				if (allowBrowsing_) {
					dirButtons.push_back(new DirButton(fileInfo[i].name, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
				}
			} else {
				gameButtons.push_back(new GameButton(fileInfo[i].fullName, *gridStyle_, new UI::LinearLayoutParams(*gridStyle_ == true ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
			}
		}
		// Put RAR/ZIP files at the end to get them out of the way. They're only shown so that people
		// can click them and get an explanation that they need to unpack them. This is necessary due
		// to a flood of support email...
		if (allowBrowsing_) {
			fileInfo.clear();
			path_.GetListing(fileInfo, "zip:rar:r01:");
			if (!fileInfo.empty()) {
				UI::LinearLayout *zl = new UI::LinearLayout(UI::ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
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

	if (allowBrowsing_) {
		gameList_->Add(new DirButton("..", new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
			OnClick.Handle(this, &GameBrowser::NavigateClick);

		// Add any pinned paths before other directories.
		auto pinnedPaths = GetPinnedPaths();
		for (auto it = pinnedPaths.begin(), end = pinnedPaths.end(); it != end; ++it) {
			gameList_->Add(new DirButton(*it, GetBaseName(*it), new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
				OnClick.Handle(this, &GameBrowser::NavigateClick);
		}
	}

	for (size_t i = 0; i < dirButtons.size(); i++) {
		gameList_->Add(dirButtons[i])->OnClick.Handle(this, &GameBrowser::NavigateClick);
	}

	for (size_t i = 0; i < gameButtons.size(); i++) {
		GameButton *b = gameList_->Add(gameButtons[i]);
		b->OnClick.Handle(this, &GameBrowser::GameButtonClick);
		b->OnHoldClick.Handle(this, &GameBrowser::GameButtonHoldClick);
	}

	// Show a button to toggle pinning at the very end.
	if (allowBrowsing_) {
		std::string caption = IsCurrentPathPinned() ? "-" : "+";
		if (!*gridStyle_) {
			caption = IsCurrentPathPinned() ? m->T("UnpinPath", "Unpin") : m->T("PinPath", "Pin");
		}
		gameList_->Add(new UI::Button(caption, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
			OnClick.Handle(this, &GameBrowser::PinToggleClick);
	}

	if (g_Config.bHomebrewStore && (flags_ & FLAG_HOMEBREWSTOREBUTTON)) {
		Add(new Spacer());
		homebrewStoreButton_ = Add(new Choice(m->T("DownloadFromStore", "Download from the PPSSPP Homebrew Store"), new UI::LinearLayoutParams(UI::WRAP_CONTENT, UI::WRAP_CONTENT)));
	} else {
		homebrewStoreButton_ = 0;
	}

	if (!lastText_.empty() && gameButtons.empty()) {
		Add(new Spacer());
		Add(new Choice(lastText_, new UI::LinearLayoutParams(UI::WRAP_CONTENT, UI::WRAP_CONTENT)))->OnClick.Handle(this, &GameBrowser::LastClick);
	}
}

bool GameBrowser::IsCurrentPathPinned() {
	const auto paths = g_Config.vPinnedPaths;
	return std::find(paths.begin(), paths.end(), path_.GetPath()) != paths.end();
}

const std::vector<std::string> GameBrowser::GetPinnedPaths() {
#ifndef _WIN32
	static const std::string sepChars = "/";
#else
	static const std::string sepChars = "/\\";
#endif

	const std::string currentPath = path_.GetPath();
	const std::vector<std::string> paths = g_Config.vPinnedPaths;
	std::vector<std::string> results;
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

		results.push_back(paths[i]);
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
	UI::EventParams e2;
	e2.s = button->GamePath();
	// Insta-update - here we know we are already on the right thread.
	OnChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::GameButtonHoldClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2;
	e2.s = button->GamePath();
	// Insta-update - here we know we are already on the right thread.
	OnHoldChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::NavigateClick(UI::EventParams &e) {
	DirButton *button  = static_cast<DirButton *>(e.v);
	std::string text = button->GetPath();
	if (button->PathAbsolute()) {
		path_.SetPath(text);
	} else {
		path_.Navigate(text);
	}
	g_Config.currentDirectory = path_.GetPath();
	Refresh();
	return UI::EVENT_DONE;
}

MainScreen::MainScreen() : backFromStore_(false) {
	System_SendMessage("event", "mainscreen");
}

void MainScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	// Vertical mode is not finished.
	bool vertical = false;  // dp_yres > dp_xres;

	I18NCategory *m = GetI18NCategory("MainMenu");

	Margins actionMenuMargins(0, 10, 10, 0);

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64);
	tabHolder_ = leftColumn;
	leftColumn->SetClip(true);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	ScrollView *scrollAllGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	ScrollView *scrollHomebrew = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	GameBrowser *tabRecentGames = new GameBrowser(
		"!RECENT", false, &g_Config.bGridView1, "", "", 0,
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	GameBrowser *tabAllGames = new GameBrowser(g_Config.currentDirectory, true, &g_Config.bGridView2,
		m->T("How to get games"), "http://www.ppsspp.org/getgames.html", 0,
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	GameBrowser *tabHomebrew = new GameBrowser(GetSysDirectory(DIRECTORY_GAME), false, &g_Config.bGridView3,
		m->T("How to get homebrew & demos", "How to get homebrew && demos"), "http://www.ppsspp.org/gethomebrew.html",
		FLAG_HOMEBREWSTOREBUTTON,
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	Choice *hbStore = tabHomebrew->HomebrewStoreButton();
	if (hbStore) {
		hbStore->OnClick.Handle(this, &MainScreen::OnHomebrewStore);
	}

	scrollRecentGames->Add(tabRecentGames);
	scrollAllGames->Add(tabAllGames);
	scrollHomebrew->Add(tabHomebrew);

	leftColumn->AddTab(m->T("Recent"), scrollRecentGames);
	leftColumn->AddTab(m->T("Games"), scrollAllGames);
	leftColumn->AddTab(m->T("Homebrew & Demos"), scrollHomebrew);

	tabRecentGames->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
	tabAllGames->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
	tabHomebrew->OnChoice.Handle(this, &MainScreen::OnGameSelectedInstant);
	tabRecentGames->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
	tabAllGames->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);
	tabHomebrew->OnHoldChoice.Handle(this, &MainScreen::OnGameSelected);

	if (g_Config.recentIsos.size() > 0) {
		leftColumn->SetCurrentTab(0);
	} else {
		leftColumn->SetCurrentTab(1);
	}

	if (backFromStore_ || showHomebrewTab) {
		leftColumn->SetCurrentTab(2);
		backFromStore_ = false;
		showHomebrewTab = false;
	}

/* if (info) {
		texvGameIcon_ = leftColumn->Add(new TextureView(0, IS_DEFAULT, new AnchorLayoutParams(144 * 2, 80 * 2, 10, 10, NONE, NONE)));
		tvTitle_ = leftColumn->Add(new TextView(0, info->title, ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 200, NONE, NONE)));
		tvGameSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 250, NONE, NONE)));
		tvSaveDataSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 290, NONE, NONE)));
	} */

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL);
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	char versionString[256];
	sprintf(versionString, "%s", PPSSPP_GIT_VERSION);
	rightColumnItems->SetSpacing(0.0f);
	LinearLayout *logos = new LinearLayout(ORIENT_HORIZONTAL);
#ifdef GOLD
	logos->Add(new ImageView(I_ICONGOLD, IS_DEFAULT, new AnchorLayoutParams(64, 64, 10, 10, NONE, NONE, false)));
#else
	logos->Add(new ImageView(I_ICON, IS_DEFAULT, new AnchorLayoutParams(64, 64, 10, 10, NONE, NONE, false)));
#endif
	logos->Add(new ImageView(I_LOGO, IS_DEFAULT, new LinearLayoutParams(Margins(-12, 0, 0, 0))));
	rightColumnItems->Add(logos);
	rightColumnItems->Add(new TextView(versionString, new LinearLayoutParams(Margins(70, -6, 0, 0))))->SetSmall(true);
#if defined(_WIN32) || defined(USING_QT_UI)
	rightColumnItems->Add(new Choice(m->T("Load","Load...")))->OnClick.Handle(this, &MainScreen::OnLoadFile);
#endif
	rightColumnItems->Add(new Choice(m->T("Game Settings", "Settings")))->OnClick.Handle(this, &MainScreen::OnGameSettings);
	rightColumnItems->Add(new Choice(m->T("Credits")))->OnClick.Handle(this, &MainScreen::OnCredits);
#ifndef __SYMBIAN32__
	rightColumnItems->Add(new Choice(m->T("www.ppsspp.org")))->OnClick.Handle(this, &MainScreen::OnPPSSPPOrg);
#endif
#ifndef GOLD
	Choice *gold = rightColumnItems->Add(new Choice(m->T("Support PPSSPP")));
	gold->OnClick.Handle(this, &MainScreen::OnSupport);
	gold->SetIcon(I_ICONGOLD);
#endif
	rightColumnItems->Add(new Choice(m->T("Exit")))->OnClick.Handle(this, &MainScreen::OnExit);

	if (vertical) {
		root_ = new LinearLayout(ORIENT_VERTICAL);
		rightColumn->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		leftColumn->ReplaceLayoutParams(new LinearLayoutParams(1.0));
		root_->Add(rightColumn);
		root_->Add(leftColumn);
	} else {
		root_ = new LinearLayout(ORIENT_HORIZONTAL);
		leftColumn->ReplaceLayoutParams(new LinearLayoutParams(1.0));
		rightColumn->ReplaceLayoutParams(new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
		root_->Add(leftColumn);
		root_->Add(rightColumn);
	}

	I18NCategory *u = GetI18NCategory("Upgrade");

	upgradeBar_ = 0;
	if (!g_Config.upgradeMessage.empty()) {
		upgradeBar_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

		UI::Margins textMargins(10, 5);
		UI::Margins buttonMargins(0, 0);
		UI::Drawable solid(0xFFbd9939);
		upgradeBar_->SetBG(solid);
		upgradeBar_->Add(new TextView(u->T("New version of PPSSPP available") + std::string(": ") + g_Config.upgradeVersion, new LinearLayoutParams(1.0f, textMargins)));
		upgradeBar_->Add(new Button(u->T("Download"), new LinearLayoutParams(buttonMargins)))->OnClick.Handle(this, &MainScreen::OnDownloadUpgrade);
		upgradeBar_->Add(new Button(u->T("Dismiss"), new LinearLayoutParams(buttonMargins)))->OnClick.Handle(this, &MainScreen::OnDismissUpgrade);

		// Slip in under root_
		LinearLayout *newRoot = new LinearLayout(ORIENT_VERTICAL);
		newRoot->Add(root_);
		newRoot->Add(upgradeBar_);
		root_->ReplaceLayoutParams(new LinearLayoutParams(1.0));
		root_ = newRoot;
	}
}

UI::EventReturn MainScreen::OnDownloadUpgrade(UI::EventParams &e) {
#ifdef ANDROID
	// Go to app store
#ifdef GOLD
	LaunchBrowser("market://details?id=org.ppsspp.ppssppgold");
#else
	LaunchBrowser("market://details?id=org.ppsspp.ppsspp");
#endif
#else
	// Go directly to ppsspp.org and let the user sort it out
	LaunchBrowser("http://www.ppsspp.org/downloads.html");
#endif
	return EVENT_DONE;
}

UI::EventReturn MainScreen::OnDismissUpgrade(UI::EventParams &e) {
	g_Config.DismissUpgrade();
	upgradeBar_->SetVisibility(V_GONE);
	return EVENT_DONE;
}

void MainScreen::sendMessage(const char *message, const char *value) {
	// Always call the base class method first to handle the most common messages.
	UIScreenWithBackground::sendMessage(message, value);

	if (!strcmp(message, "boot")) {
		screenManager()->switchScreen(new EmuScreen(value));
	}
	if (!strcmp(message, "control mapping")) {
		UpdateUIState(UISTATE_MENU);
		screenManager()->push(new ControlMappingScreen());
	}
	if (!strcmp(message, "settings")) {
		UpdateUIState(UISTATE_MENU);
		screenManager()->push(new GameSettingsScreen(""));
	}
}

void MainScreen::update(InputState &input) {
	UIScreen::update(input);
	UpdateUIState(UISTATE_MENU);
}

UI::EventReturn MainScreen::OnLoadFile(UI::EventParams &e) {
#if defined(USING_QT_UI)
	QString fileName = QFileDialog::getOpenFileName(NULL, "Load ROM", g_Config.currentDirectory.c_str(), "PSP ROMs (*.iso *.cso *.pbp *.elf *.zip)");
	if (QFile::exists(fileName)) {
		QDir newPath;
		g_Config.currentDirectory = newPath.filePath(fileName).toStdString();
		g_Config.Save();
		screenManager()->switchScreen(new EmuScreen(fileName.toStdString()));
	}
#elif defined(_WIN32)
	MainWindow::BrowseAndBoot("");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSelected(UI::EventParams &e) {
	#ifdef _WIN32
	std::string path = ReplaceAll(e.s, "\\", "/");
#else
	std::string path = e.s;
#endif
	screenManager()->push(new GameScreen(path));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSelectedInstant(UI::EventParams &e) {
	#ifdef _WIN32
	std::string path = ReplaceAll(e.s, "\\", "/");
#else
	std::string path = e.s;
#endif
	// Go directly into the game.
	screenManager()->switchScreen(new EmuScreen(path));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSettings(UI::EventParams &e) {
	// screenManager()->push(new SettingsScreen());
	auto gameSettings = new GameSettingsScreen("", "");
	gameSettings->OnRecentChanged.Handle(this, &MainScreen::OnRecentChange);
	screenManager()->push(gameSettings);
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnRecentChange(UI::EventParams &e) {
	RecreateViews();
	if (host) {
		host->UpdateUI();
	}
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnCredits(UI::EventParams &e) {
	screenManager()->push(new CreditsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnHomebrewStore(UI::EventParams &e) {
	screenManager()->push(new StoreScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnSupport(UI::EventParams &e) {
#ifdef ANDROID
	LaunchBrowser("market://details?id=org.ppsspp.ppssppgold");
#else
	LaunchBrowser("http://central.ppsspp.org/buygold");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnPPSSPPOrg(UI::EventParams &e) {
	LaunchBrowser("http://www.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnForums(UI::EventParams &e) {
	LaunchBrowser("http://forums.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnExit(UI::EventParams &e) {
	System_SendMessage("event", "exitprogram");
	NativeShutdown();
	exit(0);
	return UI::EVENT_DONE;
}

void MainScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (dialog->tag() == "store") {
		backFromStore_ = true;
		RecreateViews();
	}
}

void GamePauseScreen::update(InputState &input) {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update(input);
}

void DrawBackground(float alpha);

void GamePauseScreen::DrawBackground(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, true);
	dc.Flush();

	if (ginfo) {
		bool hasPic = false;
		if (ginfo->pic1Texture) {
			ginfo->pic1Texture->Bind(0);
			hasPic = true;
		} else if (ginfo->pic0Texture) {
			ginfo->pic0Texture->Bind(0);
			hasPic = true;
		}
		if (hasPic) {
			uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
			dc.Draw()->DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1, color);
			dc.Flush();
			dc.RebindTexture();
		} else {
			::DrawBackground(1.0f);
			dc.RebindTexture();
			dc.Flush();
		}
	}
}

GamePauseScreen::~GamePauseScreen() {
	if (saveSlots_ != NULL) {
		g_Config.iCurrentStateSlot = saveSlots_->GetSelection();
		g_Config.Save();
	}
}

void GamePauseScreen::CreateViews() {
	static const int NUM_SAVESLOTS = 5;

	using namespace UI;
	Margins actionMenuMargins(0, 100, 15, 0);
	I18NCategory *gs = GetI18NCategory("Graphics");
	I18NCategory *i = GetI18NCategory("Pause");

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(leftColumn);

	root_->Add(new Spacer(new LinearLayoutParams(1.0)));

	ViewGroup *leftColumnItems = new LinearLayout(ORIENT_VERTICAL);
	leftColumn->Add(leftColumnItems);

	saveSlots_ = leftColumnItems->Add(new ChoiceStrip(ORIENT_HORIZONTAL, new LinearLayoutParams(300, WRAP_CONTENT)));
	for (int i = 0; i < NUM_SAVESLOTS; i++){
		std::stringstream saveSlotText;
		saveSlotText << " " << i + 1 << " ";
		saveSlots_->AddChoice(saveSlotText.str());
		if (SaveState::HasSaveInSlot(i)) {
			saveSlots_->HighlightChoice(i);
		}
	}

	saveSlots_->SetSelection(g_Config.iCurrentStateSlot);
	saveSlots_->OnChoice.Handle(this, &GamePauseScreen::OnStateSelected);

	saveStateButton_ = leftColumnItems->Add(new Choice(i->T("Save State")));
	saveStateButton_->OnClick.Handle(this, &GamePauseScreen::OnSaveState);

	loadStateButton_ = leftColumnItems->Add(new Choice(i->T("Load State")));
	loadStateButton_->OnClick.Handle(this, &GamePauseScreen::OnLoadState);

	if (g_Config.iRewindFlipFrequency > 0) {
		UI::Choice *rewindButton = leftColumnItems->Add(new Choice(i->T("Rewind")));
		rewindButton->SetEnabled(SaveState::CanRewind());
		rewindButton->OnClick.Handle(this, &GamePauseScreen::OnRewind);
	}

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);

	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

	rightColumnItems->SetSpacing(0.0f);
	if (getUMDReplacePermit()) {
		rightColumnItems->Add(new Choice(i->T("Switch UMD")))->OnClick.Handle(this, &GamePauseScreen::OnSwitchUMD);
	}
	rightColumnItems->Add(new Choice(i->T("Continue")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	rightColumnItems->Add(new Choice(i->T("Game Settings")))->OnClick.Handle(this, &GamePauseScreen::OnGameSettings);
	if (g_Config.bEnableCheats) {
		rightColumnItems->Add(new Choice(i->T("Cheats")))->OnClick.Handle(this, &GamePauseScreen::OnCwCheat);
	}
	rightColumnItems->Add(new Choice(i->T("Exit to menu")))->OnClick.Handle(this, &GamePauseScreen::OnExitToMenu);

	UI::EventParams e;
	e.a = g_Config.iCurrentStateSlot;
	saveSlots_->OnChoice.Trigger(e);
}

UI::EventReturn GamePauseScreen::OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(gamePath_));
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnStateSelected(UI::EventParams &e) {
	int st = e.a;
	loadStateButton_->SetEnabled(SaveState::HasSaveInSlot(st));
	return UI::EVENT_DONE;
}

void GamePauseScreen::onFinish(DialogResult result) {
	// Do we really always need to "gpu->Resized" here?
	if (gpu)
		gpu->Resized();
}

UI::EventReturn GamePauseScreen::OnExitToMenu(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnLoadState(UI::EventParams &e) {
	SaveState::LoadSlot(saveSlots_->GetSelection(), 0, 0);

	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnSaveState(UI::EventParams &e) {
	SaveState::SaveSlot(saveSlots_->GetSelection(), 0, 0);

	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnRewind(UI::EventParams &e) {
	SaveState::Rewind(0, 0);

	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnCwCheat(UI::EventParams &e) {
	screenManager()->push(new CwCheatScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn GamePauseScreen::OnSwitchUMD(UI::EventParams &e) {
	screenManager()->push(new UmdReplaceScreen());
	return UI::EVENT_DONE;
}

void GamePauseScreen::sendMessage(const char *message, const char *value) {
	// Since the language message isn't allowed to be in native, we have to have add this
	// to every screen which directly inherits from UIScreen(which are few right now, luckily).
	if (!strcmp(message, "language")) {
		screenManager()->RecreateAllViews();
	}
}

void UmdReplaceScreen::CreateViews() {
	Margins actionMenuMargins(0, 100, 15, 0);
	I18NCategory *m = GetI18NCategory("MainMenu");
	I18NCategory *d = GetI18NCategory("Dialog");

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(1.0));
	leftColumn->SetClip(true);

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(270, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	ScrollView *scrollAllGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	GameBrowser *tabRecentGames = new GameBrowser(
		"!RECENT", false, &g_Config.bGridView1, "", "", 0,
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	GameBrowser *tabAllGames = new GameBrowser(g_Config.currentDirectory, true, &g_Config.bGridView2,
		m->T("How to get games"), "http://www.ppsspp.org/getgames.html", 0,
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	scrollRecentGames->Add(tabRecentGames);
	scrollAllGames->Add(tabAllGames);

	leftColumn->AddTab(m->T("Recent"), scrollRecentGames);
	leftColumn->AddTab(m->T("Games"), scrollAllGames);

	tabRecentGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelectedInstant);
	tabAllGames->OnChoice.Handle(this, &UmdReplaceScreen::OnGameSelectedInstant);
	tabRecentGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);
	tabAllGames->OnHoldChoice.Handle(this, &UmdReplaceScreen::OnGameSelected);

	rightColumnItems->Add(new Choice(d->T("Cancel")))->OnClick.Handle(this, &UmdReplaceScreen::OnCancel);
	rightColumnItems->Add(new Choice(m->T("Game Settings")))->OnClick.Handle(this, &UmdReplaceScreen::OnGameSettings);

	if (g_Config.recentIsos.size() > 0) {
		leftColumn->SetCurrentTab(0);
	}else{
		leftColumn->SetCurrentTab(1);
	}

	root_ = new LinearLayout(ORIENT_HORIZONTAL);
	root_->Add(leftColumn);
	root_->Add(rightColumn);
}

void UmdReplaceScreen::update(InputState &input) {
	UpdateUIState(UISTATE_PAUSEMENU);
	UIScreen::update(input);
}

UI::EventReturn UmdReplaceScreen::OnGameSelected(UI::EventParams &e) {
	__UmdReplace(e.s);
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn UmdReplaceScreen:: OnCancel(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_CANCEL);
	return UI::EVENT_DONE;
}

UI::EventReturn UmdReplaceScreen:: OnGameSettings(UI::EventParams &e) {
	screenManager()->push(new GameSettingsScreen(""));
	return UI::EVENT_DONE;
}
UI::EventReturn UmdReplaceScreen:: OnGameSelectedInstant(UI::EventParams &e) {
	__UmdReplace(e.s);
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

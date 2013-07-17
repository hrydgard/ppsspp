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

#include "Common/FileUtil.h"
#include "Core/System.h"

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "math/curves.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/MenuScreens.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/ui_atlas.h"
#include "Core/Config.h"


#ifdef _WIN32
namespace MainWindow {
	void BrowseAndBoot(std::string defaultPath);
}
#endif

class GameButton : public UI::Clickable {
public:
	GameButton(const std::string &gamePath, UI::LayoutParams *layoutParams = 0) 
		: UI::Clickable(layoutParams), gamePath_(gamePath) {}

	virtual void Draw(UIContext &dc);
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const {
		w = 144;
		h = 80;
	}
	const std::string &GamePath() const { return gamePath_; }
	
private:
	std::string gamePath_;
};

void GameButton::Draw(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, false);
	Texture *texture = 0;
	u32 color = 0, shadowColor = 0;

	if (ginfo->iconTexture) {
		texture = ginfo->iconTexture;
	} else {
		return;
	}

	int x = bounds_.x;
	int y = bounds_.y;
	int w = bounds_.w;
	int h = bounds_.h;

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

	// Render button
	int dropsize = 10;
	if (texture) {
		if (HasFocus()) {
			// dc.Draw()->DrawImage4Grid(I_DROP_SHADOW, x - dropsize, y, x+w + dropsize, y+h+dropsize*1.5, 	alphaMul(color, 0.5f), 1.0f);
			// dc.Draw()->Flush();
		} else {
			if (txOffset) {
				dropsize = 3;
				y += txOffset * 2;
			}
			dc.Draw()->DrawImage4Grid(I_DROP_SHADOW, x - dropsize, y, x+w + dropsize, y+h+dropsize*1.5, 	alphaMul(shadowColor, 0.5f), 1.0f);
			dc.Draw()->Flush();
		}
	}

	if (texture) {
		dc.Draw()->Flush();
		texture->Bind(0);
		dc.Draw()->DrawTexRect(x, y, x+w, y+h, 0, 0, 1, 1, color);
		dc.Draw()->Flush();
		dc.RebindTexture();
	} else {
		dc.FillRect(dc.theme->buttonStyle.background, bounds_);
	}
}

// Abstraction above path that lets you navigate easily.
// "/" is a special path that means the root of the file system. On Windows,
// listing this will yield drives.
class PathBrowser {
public:
	PathBrowser() {}
	PathBrowser(std::string path) { SetPath(path); }

	void SetPath(const std::string &path);
	void GetListing(std::vector<FileInfo> &fileInfo, const char *filter = 0);
	
	void Navigate(const std::string &path);

	std::string GetPath() {
		if (path_ != "/")
			return path_;
		else
			return "";
	}

	std::string path_;
};

// Normalize slashes.
void PathBrowser::SetPath(const std::string &path) { 
	if (path[0] == '!') {
		path_ = path;
		return;
	}
	path_ = path;
	for (size_t i = 0; i < path_.size(); i++) {
		if (path_[i] == '\\') path_[i] = '/';
	}
	if (!path_.size() || (path_[path_.size() - 1] != '/'))
		path_ += "/";
}

void PathBrowser::GetListing(std::vector<FileInfo> &fileInfo, const char *filter) {
#ifdef _WIN32
	if (path_ == "/") {
		// Special path that means root of file system.
		std::vector<std::string> drives = getWindowsDrives();
		for (auto drive = drives.begin(); drive != drives.end(); ++drive) 
		{
			FileInfo fake;
			fake.fullName = *drive;
			fake.name = *drive;
			fake.isDirectory = true;
			fake.exists = true;
			fake.size = 0;
			fake.isWritable = false;
			fileInfo.push_back(fake);
		}
	}
#endif

	getFilesInDir(path_.c_str(), &fileInfo, filter);
}

// TODO: Support paths like "../../hello"
void PathBrowser::Navigate(const std::string &path) {
	if (path[0] == '!')
		return;

	if (path == ".")
		return;
	if (path == "..") {
		// Upwards.
		// Check for windows drives.
		if (path_.size() == 3 && path_[1] == ':') {
			path_ = "/";
		} else {
			size_t slash = path_.rfind('/', path_.size() - 2);
			if (slash != std::string::npos)
				path_ = path_.substr(0, slash + 1);
		}
	}
	else {
		if (path[1] == ':' && path_ == "/")
			path_ = path;
		else
			path_ = path_ + path;
		if (path_[path_.size() - 1] != '/')
			path_ += "/";
	}
}

class GameBrowser : public UI::GridLayout {
public:
	GameBrowser(std::string path, bool allowBrowsing, UI::LayoutParams *layoutParams = 0);

	UI::Event OnChoice;
	
	virtual void Update(const InputState &input_state) {
		UI::GridLayout::Update(input_state);
	}

private:
	void Refresh();

	UI::EventReturn GameButtonClick(UI::EventParams &e);
	UI::EventReturn NavigateClick(UI::EventParams &e);

	PathBrowser path_;
	bool allowBrowsing_;
};

GameBrowser::GameBrowser(std::string path, bool allowBrowsing, UI::LayoutParams *layoutParams) 
	: UI::GridLayout(UI::GridLayoutSettings(150, 85), layoutParams), path_(path), allowBrowsing_(allowBrowsing) {
	Refresh();

}

void GameBrowser::Refresh() {
	// Kill all the buttons
	for (size_t i = 0; i < views_.size(); i++) {
		delete views_[i];
	}
	views_.clear();

	// Find games in the current directory and create new ones.
	std::vector<UI::Button *> dirButtons;
	std::vector<GameButton *> gameButtons;

	if (path_.GetPath() == "!RECENT") {
		for (size_t i = 0; i < g_Config.recentIsos.size(); i++) {
			gameButtons.push_back(new GameButton(g_Config.recentIsos[i]));
		}
	} else {
		std::vector<FileInfo> fileInfo;
		path_.GetListing(fileInfo, "iso:cso:pbp:elf:prx:");
		for (size_t i = 0; i < fileInfo.size(); i++) {
			if (fileInfo[i].isDirectory && !File::Exists(path_.GetPath() + fileInfo[i].name + "/EBOOT.PBP")) {
				// Check if eboot directory
				if (allowBrowsing_)
					dirButtons.push_back(new UI::Button(fileInfo[i].name.c_str(), new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
			} else {
				gameButtons.push_back(new GameButton(fileInfo[i].fullName));
			}
		}
	}

	if (allowBrowsing_)
		Add(new UI::Button("..", new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
			OnClick.Handle(this, &GameBrowser::NavigateClick);

	for (size_t i = 0; i < dirButtons.size(); i++) {
		Add(dirButtons[i])->OnClick.Handle(this, &GameBrowser::NavigateClick);
	}

	for (size_t i = 0; i < gameButtons.size(); i++) {
		Add(gameButtons[i])->OnClick.Handle(this, &GameBrowser::GameButtonClick);
	}
}

UI::EventReturn GameBrowser::GameButtonClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2;
	e2.s = button->GamePath();
	// Insta-update - here we know we are already on the right thread.
	OnChoice.Trigger(e2);
	return UI::EVENT_DONE;
}

UI::EventReturn GameBrowser::NavigateClick(UI::EventParams &e) {
	UI::Button *button  = static_cast<UI::Button *>(e.v);
	std::string text = button->GetText();
	path_.Navigate(text);
	g_Config.currentDirectory = path_.GetPath();
	Refresh();
	return UI::EVENT_DONE;
}


void MainScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(1.0));
	root_->Add(leftColumn);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	ScrollView *scrollAllGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	ScrollView *scrollHomebrew = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	GameBrowser *tabRecentGames = new GameBrowser("!RECENT", false, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	GameBrowser *tabAllGames = new GameBrowser(g_Config.currentDirectory, true, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	GameBrowser *tabHomebrew = new GameBrowser(g_Config.memCardDirectory + "PSP/GAME/", false, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	scrollRecentGames->Add(tabRecentGames);
	scrollAllGames->Add(tabAllGames);
	scrollHomebrew->Add(tabHomebrew);

	leftColumn->AddTab("Recent", scrollRecentGames);
	leftColumn->AddTab("Games", scrollAllGames);
	leftColumn->AddTab("Homebrew & Demos", scrollHomebrew);

	tabRecentGames->OnChoice.Handle(this, &MainScreen::OnGameSelected);
	tabAllGames->OnChoice.Handle(this, &MainScreen::OnGameSelected);
	tabHomebrew->OnChoice.Handle(this, &MainScreen::OnGameSelected);

/*
	if (info) {
		texvGameIcon_ = leftColumn->Add(new TextureView(0, IS_DEFAULT, new AnchorLayoutParams(144 * 2, 80 * 2, 10, 10, NONE, NONE)));
		tvTitle_ = leftColumn->Add(new TextView(0, info->title, ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 200, NONE, NONE)));
		tvGameSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 250, NONE, NONE)));
		tvSaveDataSize_ = leftColumn->Add(new TextView(0, "...", ALIGN_LEFT, 1.0f, new AnchorLayoutParams(10, 290, NONE, NONE)));
	}
	*/

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);
	
	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

#ifdef _WIN32
	rightColumnItems->Add(new Choice("Load..."))->OnClick.Handle(this, &MainScreen::OnLoadFile);
#endif
	rightColumnItems->Add(new Choice("Settings"))->OnClick.Handle(this, &MainScreen::OnSettings);
	rightColumnItems->Add(new Choice("Exit"))->OnClick.Handle(this, &MainScreen::OnExit);
	rightColumnItems->Add(new Choice("Credits"))->OnClick.Handle(this, &MainScreen::OnCredits);
	rightColumnItems->Add(new Choice("Support PPSSPP"))->OnClick.Handle(this, &MainScreen::OnSupport);
}

void MainScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "boot")) {
		screenManager()->switchScreen(new EmuScreen(value));
	}
}

void DrawBackground(float alpha);

void MainScreen::DrawBackground(UIContext &dc) {
	globalUIState = UISTATE_MENU;
	::DrawBackground(1.0f);
	dc.Flush();
}

UI::EventReturn MainScreen::OnLoadFile(UI::EventParams &e) {
#ifdef _WIN32
	MainWindow::BrowseAndBoot("");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSelected(UI::EventParams &e) {
	screenManager()->push(new GameScreen(e.s));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnSettings(UI::EventParams &e) {
	// screenManager()->push(new SettingsScreen());
	screenManager()->push(new GlobalSettingsScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnCredits(UI::EventParams &e) {
	screenManager()->push(new CreditsScreen());
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

UI::EventReturn MainScreen::OnExit(UI::EventParams &e) {
	NativeShutdown();
	exit(0);
	return UI::EVENT_DONE;
}

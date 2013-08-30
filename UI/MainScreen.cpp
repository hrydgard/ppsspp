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

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"
#include "math/curves.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

#include "Common/FileUtil.h"
#include "Core/System.h"
#include "Core/SaveState.h"

#include "UI/EmuScreen.h"
#include "UI/MainScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/CwCheatScreen.h"
#include "UI/MiscScreens.h"
#include "UI/ui_atlas.h"
#include "Core/Config.h"
#include "GPU/GPUInterface.h"
#include "i18n/i18n.h"

#ifdef USING_QT_UI
#include <QFileDialog>
#include <QFile>
#include <QDir>
#endif

#ifdef _WIN32
namespace MainWindow {
	void BrowseAndBoot(std::string defaultPath, bool browseDirectory = false);
}
#endif

class GameButton : public UI::Clickable {
public:
	GameButton(const std::string &gamePath, bool gridStyle, UI::LayoutParams *layoutParams = 0) 
		: UI::Clickable(layoutParams), gridStyle_(gridStyle), gamePath_(gamePath), holdFrameCount_(0) {}

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
	virtual void Touch(const TouchInput &input) {
		UI::Clickable::Touch(input);
		if (input.flags & TOUCH_UP) {
			holdFrameCount_ = 0;
		}
	}
	virtual void Update(const InputState &input_state) {
		if (down_)
			holdFrameCount_++;
		else
			holdFrameCount_ = 0;
		// Hold button for 1.5 seconds to launch the game directly
		if (holdFrameCount_ > 90) {
			holdFrameCount_ = 0;
			UI::EventParams e;
			e.v = this;
			e.s = gamePath_;
			down_ = false;
			OnHoldClick.Trigger(e);
		}
	}

	UI::Event OnHoldClick;

private:
	bool gridStyle_;
	std::string gamePath_;
	
	int holdFrameCount_;
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
		// w = 144 * 80 / 50;
		h = 50;
		if (HasFocus())
			style = down_ ? dc.theme->itemDownStyle : dc.theme->itemFocusedStyle;

		dc.Draw()->Flush();
		dc.RebindTexture();
		dc.FillRect(style.background, bounds_);
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

	// Render button
	int dropsize = 10;
	if (texture) {
		if (txOffset) {
			dropsize = 3;
			y += txOffset * 2;
		}
		if (HasFocus()) {
			dc.Draw()->Flush();
			dc.RebindTexture();
			dc.Draw()->DrawImage4Grid(I_DROP_SHADOW, x - dropsize*1.5f, y - dropsize*1.5f, x+w + dropsize*1.5f, y+h+dropsize*1.5f, alphaMul(color, 1.0f), 1.0f);
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

	dc.Draw()->Flush();
	dc.RebindTexture();
	dc.SetFontStyle(dc.theme->uiFont);
	if (!gridStyle_) {
		dc.Draw()->Flush();
		dc.PushScissor(bounds_);
		dc.DrawText(ginfo->title.c_str(), bounds_.x + 150, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
		dc.Draw()->Flush();
		dc.PopScissor();
	} else if (!texture) {
		dc.Draw()->Flush();
		dc.PushScissor(bounds_);
		dc.DrawText(ginfo->title.c_str(), bounds_.x + 4, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
		dc.Draw()->Flush();
		dc.PopScissor();
	} else {
		dc.Draw()->Flush();
	}
	dc.RebindTexture();
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
	std::string GetFriendlyPath() {
		std::string str = GetPath();
		/*
#ifdef ANDROID
		if (!memcmp(str.c_str(), g_Config.memCardDirectory.c_str(), g_Config.memCardDirectory.size()))
		{
			str = str.substr(g_Config.memCardDirectory.size());
		}
#endif*/
		return str;
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
		for (auto drive = drives.begin(); drive != drives.end(); ++drive) {
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

static std::string GetMemCardDirectory() {
	std::string memCardDirectory, ignore;
	GetSysDirectories(memCardDirectory, ignore);
	return memCardDirectory;
}

class GameBrowser : public UI::LinearLayout {
public:
	GameBrowser(std::string path, bool allowBrowsing, bool *gridStyle_, std::string lastText, std::string lastLink, UI::LayoutParams *layoutParams = 0);

	UI::Event OnChoice;
	UI::Event OnHoldChoice;
	
private:
	void Refresh();

	UI::EventReturn GameButtonClick(UI::EventParams &e);
	UI::EventReturn GameButtonHoldClick(UI::EventParams &e);
	UI::EventReturn NavigateClick(UI::EventParams &e);
	UI::EventReturn LayoutChange(UI::EventParams &e);
	UI::EventReturn LastClick(UI::EventParams &e);
	UI::EventReturn HomeClick(UI::EventParams &e);

	UI::ViewGroup *gameList_;
	PathBrowser path_;
	bool *gridStyle_;
	bool allowBrowsing_;
	std::string lastText_;
	std::string lastLink_; 
};

GameBrowser::GameBrowser(std::string path, bool allowBrowsing, bool *gridStyle, std::string lastText, std::string lastLink, UI::LayoutParams *layoutParams) 
	: LinearLayout(UI::ORIENT_VERTICAL, layoutParams), path_(path), allowBrowsing_(allowBrowsing), gridStyle_(gridStyle), gameList_(0), lastText_(lastText), lastLink_(lastLink) {
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
	path_.SetPath(GetMemCardDirectory());
	g_Config.currentDirectory = path_.GetPath();
	Refresh();
	return UI::EVENT_DONE;
}

void GameBrowser::Refresh() {
	using namespace UI;

	// Kill all the contents
	Clear();

	Add(new Spacer(5.0f));
	I18NCategory *m = GetI18NCategory("MainMenu");

	if (allowBrowsing_) {
		LinearLayout *topBar = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		topBar->Add(new TextView(path_.GetFriendlyPath().c_str(), ALIGN_VCENTER, 0.7f, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 1.0f)));
#ifdef ANDROID
		topBar->Add(new Choice(m->T("Home")))->OnClick.Handle(this, &GameBrowser::HomeClick);
#endif
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
	std::vector<UI::Button *> dirButtons;
	std::vector<GameButton *> gameButtons;

	if (path_.GetPath() == "!RECENT") {
		for (size_t i = 0; i < g_Config.recentIsos.size(); i++) {
			gameButtons.push_back(new GameButton(g_Config.recentIsos[i], *gridStyle_, new UI::LinearLayoutParams(*gridStyle_ == true ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
		}
	} else {
		std::vector<FileInfo> fileInfo;
		path_.GetListing(fileInfo, "iso:cso:pbp:elf:prx:");
		for (size_t i = 0; i < fileInfo.size(); i++) {
			if (fileInfo[i].isDirectory && (path_.GetPath().size() < 4 || !File::Exists(path_.GetPath() + fileInfo[i].name + "/EBOOT.PBP"))) {
				// Check if eboot directory
				if (allowBrowsing_)
					dirButtons.push_back(new UI::Button(fileInfo[i].name.c_str(), new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
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
			for (size_t i = 0; i < fileInfo.size(); i++) {
				if (!fileInfo[i].isDirectory) {
					gameButtons.push_back(new GameButton(fileInfo[i].fullName, *gridStyle_, new UI::LinearLayoutParams(*gridStyle_ == true ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
				}
			}
		}
	}

	if (allowBrowsing_)
		gameList_->Add(new UI::Button("..", new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)))->
			OnClick.Handle(this, &GameBrowser::NavigateClick);

	for (size_t i = 0; i < dirButtons.size(); i++) {
		gameList_->Add(dirButtons[i])->OnClick.Handle(this, &GameBrowser::NavigateClick);
	}

	for (size_t i = 0; i < gameButtons.size(); i++) {
		GameButton *b = gameList_->Add(gameButtons[i]);
		b->OnClick.Handle(this, &GameBrowser::GameButtonClick);
		b->OnHoldClick.Handle(this, &GameBrowser::GameButtonHoldClick);
	}

	if (!lastText_.empty() && gameButtons.empty()) {
		Add(new Spacer());
		Add(new Choice(lastText_, new UI::LinearLayoutParams(UI::WRAP_CONTENT, UI::WRAP_CONTENT)))->OnClick.Handle(this, &GameBrowser::LastClick);
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

UI::EventReturn GameBrowser::GameButtonHoldClick(UI::EventParams &e) {
	GameButton *button = static_cast<GameButton *>(e.v);
	UI::EventParams e2;
	e2.s = button->GamePath();
	// Insta-update - here we know we are already on the right thread.
	OnHoldChoice.Trigger(e2);
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

	I18NCategory *m = GetI18NCategory("MainMenu");

	Margins actionMenuMargins(0, 10, 10, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(1.0));
	leftColumn->SetClip(true);
	root_->Add(leftColumn);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	ScrollView *scrollAllGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	ScrollView *scrollHomebrew = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	GameBrowser *tabRecentGames = new GameBrowser(
		"!RECENT", false, &g_Config.bGridView1, "", "",
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	GameBrowser *tabAllGames = new GameBrowser(g_Config.currentDirectory, true, &g_Config.bGridView2, 
		m->T("How to get games"), "http://www.ppsspp.org/getgames.html",
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	GameBrowser *tabHomebrew = new GameBrowser(GetMemCardDirectory() + "PSP/GAME/", false, &g_Config.bGridView3,
		m->T("How to get homebrew & demos"), "http://www.ppsspp.org/gethomebrew.html",
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

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
	
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

	char versionString[256];
	sprintf(versionString, "%s", PPSSPP_GIT_VERSION);
	rightColumnItems->SetSpacing(0.0f);
	LinearLayout *logos = new LinearLayout(ORIENT_HORIZONTAL);
#ifdef GOLD
	logos->Add(new ImageView(I_ICONGOLD, new AnchorLayoutParams(64, 64, 10, 10, NONE, NONE, false)));
#else
	logos->Add(new ImageView(I_ICON, IS_DEFAULT, new AnchorLayoutParams(64, 64, 10, 10, NONE, NONE, false)));
#endif
	logos->Add(new ImageView(I_LOGO, IS_DEFAULT, new LinearLayoutParams(Margins(-12, 0, 0, 0))));
	rightColumnItems->Add(logos);
	rightColumnItems->Add(new TextView(versionString, new LinearLayoutParams(Margins(70, -6, 0, 0))))->SetSmall(true);
#if defined(_WIN32) || defined(USING_QT_UI)
	rightColumnItems->Add(new Choice(m->T("Load","Load...")))->OnClick.Handle(this, &MainScreen::OnLoadFile);
#endif
	rightColumnItems->Add(new Choice(m->T("Game Settings")))->OnClick.Handle(this, &MainScreen::OnGameSettings);
	rightColumnItems->Add(new Choice(m->T("Exit")))->OnClick.Handle(this, &MainScreen::OnExit);
	rightColumnItems->Add(new Choice(m->T("Credits")))->OnClick.Handle(this, &MainScreen::OnCredits);
	rightColumnItems->Add(new Choice(m->T("www.ppsspp.org")))->OnClick.Handle(this, &MainScreen::OnPPSSPPOrg);
	rightColumnItems->Add(new Choice(m->T("Support PPSSPP")))->OnClick.Handle(this, &MainScreen::OnSupport);
}

void MainScreen::sendMessage(const char *message, const char *value) {
	if (!strcmp(message, "boot")) {
		screenManager()->switchScreen(new EmuScreen(value));
	}
}

void MainScreen::update(InputState &input) {
	UIScreen::update(input);
	globalUIState = UISTATE_MENU;
}

UI::EventReturn MainScreen::OnLoadFile(UI::EventParams &e) {
#if defined(USING_QT_UI) && !defined(MEEGO_EDITION_HARMATTAN)
	QString fileName = QFileDialog::getOpenFileName(NULL, "Load ROM", g_Config.currentDirectory.c_str(), "PSP ROMs (*.iso *.cso *.pbp *.elf)");
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
	screenManager()->push(new GameScreen(e.s));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSelectedInstant(UI::EventParams &e) {
	// Go directly into the game.
	screenManager()->switchScreen(new EmuScreen(e.s));
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnGameSettings(UI::EventParams &e) {
	// screenManager()->push(new SettingsScreen());
	screenManager()->push(new GameSettingsScreen("",""));
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

UI::EventReturn MainScreen::OnPPSSPPOrg(UI::EventParams &e) {
	LaunchBrowser("http://www.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnForums(UI::EventParams &e) {
	LaunchBrowser("http://forums.ppsspp.org");
	return UI::EVENT_DONE;
}

UI::EventReturn MainScreen::OnExit(UI::EventParams &e) {
	NativeShutdown();
	exit(0);
	return UI::EVENT_DONE;
}

void GamePauseScreen::update(InputState &input) {
	globalUIState = UISTATE_PAUSEMENU;
	UIScreen::update(input);
}

void GamePauseScreen::key(const KeyInput &key) {
	if ((key.flags & KEY_DOWN) && UI::IsEscapeKeyCode(key.keyCode)) {
		screenManager()->finishDialog(this, DR_CANCEL);
	} else {
		UIScreen::key(key);
	}
}

void GamePauseScreen::DrawBackground(UIContext &dc) {
	GameInfo *ginfo = g_gameInfoCache.GetInfo(gamePath_, true);
	dc.Flush();

	if (ginfo && ginfo->pic1Texture) {
		ginfo->pic1Texture->Bind(0);
		uint32_t color = whiteAlpha(ease((time_now_d() - ginfo->timePic1WasLoaded) * 3)) & 0xFFc0c0c0;
		dc.Draw()->DrawTexRect(0,0,dp_xres, dp_yres, 0,0,1,1,color);
		dc.Flush();
		dc.RebindTexture();
	}
}

GamePauseScreen::~GamePauseScreen() {
	g_Config.iCurrentStateSlot = saveSlots_->GetSelection();
	g_Config.Save();
}

void GamePauseScreen::CreateViews() {
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
	saveSlots_->AddChoice("  1  ");
	saveSlots_->AddChoice("  2  ");
	saveSlots_->AddChoice("  3  ");
	saveSlots_->AddChoice("  4  ");
	saveSlots_->SetSelection(g_Config.iCurrentStateSlot);
	saveSlots_->OnChoice.Handle(this, &GamePauseScreen::OnStateSelected);
	
	saveStateButton_ = leftColumnItems->Add(new Choice(i->T("Save State")));
	saveStateButton_->OnClick.Handle(this, &GamePauseScreen::OnSaveState);

	loadStateButton_ = leftColumnItems->Add(new Choice(i->T("Load State")));
	loadStateButton_->OnClick.Handle(this, &GamePauseScreen::OnLoadState);

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumn);

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumn->Add(rightColumnItems);

	rightColumnItems->Add(new Choice(i->T("Continue")))->OnClick.Handle(this, &GamePauseScreen::OnContinue);
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

UI::EventReturn GamePauseScreen::OnContinue(UI::EventParams &e) {
	screenManager()->finishDialog(this, DR_CANCEL);
	if (gpu) gpu->Resized();
	return UI::EVENT_DONE;
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
UI::EventReturn GamePauseScreen::OnCwCheat(UI::EventParams &e) {
	screenManager()->push(new CwCheatScreen());
	return UI::EVENT_DONE;
}

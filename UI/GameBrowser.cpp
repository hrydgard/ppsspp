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

#include "UI/GameBrowser.h"
#include "UI/GameButton.h"
#include "UI/RemoteISOScreen.h"
#include "UI/SavedataScreen.h"
#include "UI/Store.h"
#include "UI/UploadScreen.h"
#include "UI/Background.h"
#include "Core/Config.h"
#include "Common/Data/Text/I18n.h"
#include "Core/Util/DarwinFileSystemServices.h" // For the browser

// Folders get bigger lines too, though not as big as the games.
static constexpr float LARGE_LIST_FOLDER_H = 50.0f;

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

void GameBrowser::FocusGame(const Path &GetPath) {
	// If the button's still there, just focus it. That keeps the search results and scroll position.
	if (gameList_) {
		for (int i = 0; i < gameList_->GetNumSubviews(); i++) {
			GameButton *button = dynamic_cast<GameButton *>(gameList_->GetViewByIndex(i));
			if (button && button->GetPath() == GetPath && button->GetVisibility() == UI::V_VISIBLE) {
				button->SetFocus(UI::FocusFlags::CAUSE_RESTORE);
				return;
			}
		}
	}
	// Otherwise, Refresh focuses it when it recreates the buttons.
	focusPath_ = GetPath;
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
	std::vector<GameButton *> dirButtons;
	std::vector<GameButton *> gameButtons;

	listingPending_ = !path_.IsListingReady();

	// TODO: If listing failed, show a special error message.

	std::vector<Path> filenames;
	if (HasSpecialFiles(filenames)) {
		for (size_t i = 0; i < filenames.size(); i++) {
			gameButtons.push_back(new GameButton(GameButtonType::Game, filenames[i], lastViewMode_, new UI::LinearLayoutParams(gridStyle ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
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
					dirButtons.push_back(new GameButton(GameButtonType::Dir, fileInfo[i].fullName, lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
					if (fileInfo[i].fullName.IsNetworkRoot()) {
						dirButtons.back()->SetAsAbsolute();
					}
				}
			} else {
				gameButtons.push_back(new GameButton(GameButtonType::Game, fileInfo[i].fullName, lastViewMode_, new UI::LinearLayoutParams(gridStyle ? UI::WRAP_CONTENT : UI::FILL_PARENT, UI::WRAP_CONTENT)));
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
						GameButton *b = new GameButton(GameButtonType::Other, fileInfo[i].fullName, lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::WRAP_CONTENT));
						gameButtons.push_back(b);
					}
				}
			}
		}
	}

	GameButton *upButton = nullptr;
	if (browseFlags_ & BrowseFlags::NAVIGATE) {
		if (path_.CanNavigateUp()) {
			upButton = gameList_->Add(new GameButton(GameButtonType::Dir, Path(".."), lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
			upButton->OnClick.Handle(this, &GameBrowser::NavigateClick);
		}

		// Add any pinned paths before other directories.
		auto pinnedPaths = GetPinnedPaths();
		for (const auto &pinnedPath : pinnedPaths) {
			GameButton *pinnedDir = gameList_->Add(new GameButton(GameButtonType::Dir, pinnedPath, lastViewMode_, new UI::LinearLayoutParams(UI::FILL_PARENT, UI::FILL_PARENT)));
			pinnedDir->OnClick.Handle(this, &GameBrowser::NavigateClick);
			pinnedDir->SetPinned(true);
			pinnedDir->SetAsAbsolute();
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

		if (!focusPath_.empty() && b->GetPath() == focusPath_ && b->SetFocus(FocusFlags::CAUSE_RESTORE)) {
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
	// TODO: That's not very friendly.
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
	GameButton *button = dynamic_cast<GameButton *>(e.v);
	if (!button) {
		return;
	}
	UI::EventParams e2{};
	e2.s = button->GetPath().ToString();
	OnChoice.Trigger(e2);
}

void GameBrowser::GameButtonHoldClick(UI::EventParams &e) {
	GameButton *button = dynamic_cast<GameButton *>(e.v);
	if (!button) {
		return;
	}
	UI::EventParams e2{};
	e2.s = button->GetPath().ToString();
	OnHoldChoice.Trigger(e2);
}

void GameBrowser::GameButtonHighlight(UI::EventParams &e) {
	OnHighlight.Trigger(e);
}

void GameBrowser::NavigateClick(UI::EventParams &e) {
	GameButton *button = dynamic_cast<GameButton *>(e.v);
	if (!button) {
		return;
	}
	Path text = button->GetPath();
	if (button->NavigateAsAbsolute()) {
		path_.SetPath(text);
	} else if (text.ToString() == "..") {
		NavigateUp();
		return;
	} else {
		path_.Navigate(text.GetFilename());
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

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

#include "ppsspp_config.h"

#include <algorithm>
#include <cmath>

#include "android/jni/app-android.h"

#include "Common/Log.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"

#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"

#include "Common/File/AndroidStorage.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DiskFree.h"

#include "Common/Thread/ThreadManager.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Notice.h"

#include "Core/Config.h"
#include "Core/Util/MemStick.h"

#include "UI/MemStickScreen.h"
#include "UI/MainScreen.h"
#include "UI/BaseScreens.h"

static std::string FormatSpaceString(int64_t space) {
	if (space >= 0) {
		char buffer[50];
		NiceSizeFormat(space, buffer, sizeof(buffer));
		return buffer;
	} else {
		return "N/A";
	}
}

// The memory stick folder in the app's own storage.
static Path PrivateMemstickFolder() {
#if PPSSPP_PLATFORM(UWP)
	return g_Config.internalDataDirectory;
#else
	return Path(g_extFilesDir);
#endif
}

std::string GetMemstickFolderDisplayName(const Path &folder) {
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);
	if (!folder.empty() && folder == PrivateMemstickFolder()) {
		return std::string(ms->T("App private data"));
	}
	std::string volume;
	std::string inVolume;
	if (!SplitStorageLocation(folder, &volume, &inVolume)) {
		return folder.ToVisualString();
	}
	std::string name;
	if (volume == "primary") {
		name = ms->T("Internal storage");
	} else {
		// Could also be a USB drive, which is rare enough. The ID tells them apart.
		name = ApplySafeSubstitutions(ms->T("SD card (%1)"), volume);
	}
	return inVolume.empty() ? name : name + " / " + ReplaceAll(inVolume, "/", " / ");
}

MemStickScreen::MemStickScreen(bool initialSetup)
	: UITwoPaneBaseDialogScreen(Path(), 
		TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll | (initialSetup ? TwoPaneFlags::NoBackButton : TwoPaneFlags::Default)),
		initialSetup_(initialSetup) {
#if PPSSPP_PLATFORM(ANDROID)
	// Let's only offer the browse-for-folder choice on Android 10 or later.
	// Earlier versions often don't really have working folder browsers.
	storageBrowserWorking_ = System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 29;
#else
	// For testing UI only
	storageBrowserWorking_ = true;
#endif

	if (initialSetup_) {
		// Preselect current choice.
		if (System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
			choice_ = CHOICE_BROWSE_FOLDER;
		} else {
			WARN_LOG(Log::System, "Scoped storage not enabled - shouldn't be in MemStickScreen at initial setup");
			choice_ = CHOICE_STORAGE_ROOT;
			// Shouldn't really be here in initial setup.
		}
	} else {
		// Detect the current choice, so it's preselected in the UI.
#if PPSSPP_PLATFORM(UWP)
		if (g_Config.memStickDirectory == g_Config.internalDataDirectory) {
#else
		if (g_Config.memStickDirectory == Path(g_extFilesDir)) {
#endif
			choice_ = CHOICE_PRIVATE_DIRECTORY;
		} else if (g_Config.memStickDirectory == Path(g_externalDir)) {
			choice_ = CHOICE_STORAGE_ROOT;
		} else if (storageBrowserWorking_) {
			choice_ = CHOICE_BROWSE_FOLDER;
		} else {
			choice_ = CHOICE_SET_MANUAL;
		}
	}
}

static std::string GetExplanationString(MemStickScreen::Choice choice) {
	auto iz = GetI18NCategory(I18NCat::MEMSTICK);
	using namespace UI;

	int flags = FLAG_WRAP_TEXT;

	std::string str;
	std::string_view bullet = "• ";
	switch (choice) {
	case MemStickScreen::CHOICE_STORAGE_ROOT:
		// Old school choice
		str += bullet;
		str += iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP");
		str += "\n";
		str += bullet;
		str += iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold");
		str += "\n";
		str += bullet;
		str += iz->T("EasyUSBAccess", "Easy USB access");
		str += "\n";
		break;
	case MemStickScreen::CHOICE_BROWSE_FOLDER:
		str += bullet;
		str += iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP");
		str += "\n";
		str += bullet;
		str += iz->T("DataCanBeShared", "Data can be shared between PPSSPP regular/Gold");
		str += "\n";
		str += bullet;
		str += iz->T("EasyUSBAccess", "Easy USB access");
		str += "\n";
		break;
	case MemStickScreen::CHOICE_PRIVATE_DIRECTORY:
		// Consider https://www.compart.com/en/unicode/U+26A0 (unicode warning sign?)? or a graphic?
		str += bullet;
		str += iz->T("DataWillBeLostOnUninstall", "Warning! Data will be lost when you uninstall PPSSPP!");
		str += "\n";
		str += bullet;
		str += iz->T("DataCannotBeShared", "Data CANNOT be shared between PPSSPP regular/Gold!");
		str += "\n";
#if !PPSSPP_PLATFORM(UWP)
		if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
			str += bullet;
			str += iz->T("USBAccessThroughGold", "USB access through Android/data/org.ppsspp.ppssppgold/files");
			str += "\n";
		} else {
			str += bullet;
			str += iz->T("USBAccessThrough", "USB access through Android/data/org.ppsspp.ppsspp/files");
			str += "\n";
		}
#endif
		break;
	case MemStickScreen::CHOICE_SET_MANUAL:
		str += bullet;
		str += iz->T("DataWillStay", "Data will stay even if you uninstall PPSSPP");
		str += "\n";
		str += bullet;
		str += iz->T("EasyUSBAccess", "Easy USB access");
		str += "\n";
		break;
	default:
		str += bullet;
		str += iz->T("EasyUSBAccess", "Easy USB access");
		str += "\n";
		break;
	}
	// The last line's newline would leave an empty line at the bottom of the hint.
	while (!str.empty() && str.back() == '\n') {
		str.pop_back();
	}
	return str;
}

std::string_view MemStickScreen::GetTitle() const {
	if (initialSetup_) {
		return T(I18NCat::MEMSTICK, "Welcome to PPSSPP!");
	}
	return T(I18NCat::SYSTEM, "Memory Stick folder");
}

void MemStickScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto ms = GetI18NCategory(I18NCat::MEMSTICK);

	ChoiceGroupList *groups = parent->Add(new ChoiceGroupList());

	groups->Add(new TextView(ms->T("MemoryStickDescription", "Choose where to keep PSP data (Memory Stick)"), ALIGN_LEFT | FLAG_WRAP_TEXT, false));

	ChoiceGroup *choiceGroup = groups->Add(new ChoiceGroup());

	// For legacy Android systems, so you can switch back to the old ways if you move to SD or something.
	// Trying to avoid needing a scroll view, so only showing the explanation for one option at a time.
#if !PPSSPP_PLATFORM(UWP)
	if (!System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
		RadioButton *pspFolder = choiceGroup->Add(new RadioButton(&choice_, CHOICE_STORAGE_ROOT, ms->T("Use PSP folder at root of storage")));
		pspFolder->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);
		pspFolder->SetHintText(GetExplanationString(CHOICE_STORAGE_ROOT));
	}
#endif

	if (storageBrowserWorking_) {
		RadioButton *browseFolder = choiceGroup->Add(new RadioButton(&choice_, CHOICE_BROWSE_FOLDER, ms->T("Create or Choose a PSP folder")));
		browseFolder->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);
		browseFolder->SetHintText(GetExplanationString(CHOICE_BROWSE_FOLDER));
		// TODO: Show current folder here if we have one set.
	}

#ifdef ANDROID_LEGACY
	constexpr bool legacy = true;
#else
	constexpr bool legacy = false;
#endif

	if (!storageBrowserWorking_ || legacy) {
		RadioButton *manualFolder = choiceGroup->Add(new RadioButton(&choice_, CHOICE_SET_MANUAL, ms->T("Manually specify PSP folder")));
		manualFolder->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);
		manualFolder->SetHintText(GetExplanationString(CHOICE_SET_MANUAL));
	}

	std::string privateString(ms->T("Use App Private Data"));

	if (initialSetup_) {
		privateString = StringFromFormat("%s (%s)", ms->T_cstr("Skip for now"), privateString.c_str());
	}

	RadioButton *privateFolder = choiceGroup->Add(new RadioButton(&choice_, CHOICE_PRIVATE_DIRECTORY, privateString));
	privateFolder->OnClick.Handle(this, &MemStickScreen::OnChoiceClick);
	privateFolder->SetHintText(GetExplanationString(CHOICE_PRIVATE_DIRECTORY));

	if ((choice_ == CHOICE_BROWSE_FOLDER || choice_ == CHOICE_SET_MANUAL) && !g_Config.memStickDirectory.empty()) {
		groups->Add(new TextView(StringFromFormat("%s: %s", ms->T_cstr("Current"), GetMemstickFolderDisplayName(g_Config.memStickDirectory).c_str()), ALIGN_LEFT | FLAG_WRAP_TEXT, false));
	}

	INFO_LOG(Log::System, "MemStickScreen: initialSetup=%d", (int)initialSetup_);
}

void MemStickScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);

	std::string_view confirmButtonText;
	ImageID confirmButtonImage = ImageID::invalid();
	switch (choice_) {
	case CHOICE_BROWSE_FOLDER:
		confirmButtonText = di->T("OK");
		confirmButtonImage = ImageID("I_FOLDER_OPEN");
		break;
	case CHOICE_PRIVATE_DIRECTORY:
		if (initialSetup_) {
			confirmButtonText = di->T("Skip");
			confirmButtonImage = ImageID("I_WARNING");
		} else {
			confirmButtonText = di->T("OK");
		}
		break;
	case CHOICE_STORAGE_ROOT:
	case CHOICE_SET_MANUAL:
	default:
		confirmButtonText = di->T("OK");
		break;
	}

	ChoiceGroupList *groups = parent->Add(new ChoiceGroupList());

	ChoiceGroup *confirmGroup = groups->Add(new ChoiceGroup());
	confirmGroup->Add(new UI::Choice(confirmButtonText, confirmButtonImage))->OnClick.Handle<MemStickScreen>(this, &MemStickScreen::OnConfirmClick);

	// The top bar has the back button.
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) != DEVICE_TYPE_TV) {
		ChoiceGroup *helpGroup = groups->Add(new ChoiceGroup());
		helpGroup->Add(new UI::Choice(ms->T("WhatsThis", "What's this?"), ImageID("I_LINK_OUT_QUESTION")))->OnClick.Handle<MemStickScreen>(this, &MemStickScreen::OnHelp);
	}
}

void MemStickScreen::OnHelp(UI::EventParams &params) {
	// I'm letting the old redirect handle this one, as the target is within /docs on the website,
	// and that structure may change a bit.
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/guide_storage.html");
}

void MemStickScreen::OnChoiceClick(UI::EventParams &params) {
	// Change the confirm button to match the choice,
	// and change the text that we show.
	RecreateViews();
}

void MemStickScreen::OnConfirmClick(UI::EventParams &params) {
	switch (choice_) {
	case CHOICE_SET_MANUAL:
		return SetFolderManually(params);
	case CHOICE_STORAGE_ROOT:
		return UseStorageRoot(params);
	case CHOICE_PRIVATE_DIRECTORY:
		return UseInternalStorage(params);
	case CHOICE_BROWSE_FOLDER:
		return Browse(params);
	}
}

void MemStickScreen::SetFolderManually(UI::EventParams &params) {
	// The old way, from before scoped storage. Write in the full path.
#if PPSSPP_PLATFORM(ANDROID) || PPSSPP_PLATFORM(SWITCH)
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	System_InputBoxGetString(GetRequesterToken(), sy->T("Memory Stick Folder"), g_Config.memStickDirectory.ToString(), false, [this](std::string_view value, int) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		auto di = GetI18NCategory(I18NCat::DIALOG);

		std::string newPath(value);
		// Apparently some users got used to leaving out the leading /.
		if (!newPath.empty() && newPath[0] != '/') {
			newPath = "/" + newPath;
		}
		size_t pos = newPath.find_last_not_of('/');
		// Gotta have at least something but a /.
		if (pos == std::string::npos) {
			g_OSD.Show(OSDType::MESSAGE_WARNING, sy->T("ChangingMemstickPathInvalid", "That path couldn't be used to save Memory Stick files."), 5.0f);
			return;
		}
		if (pos != newPath.size() - 1) {
			newPath = newPath.substr(0, pos + 1);
		}

		if (newPath.empty()) {
			// Reuse below message instead of adding yet another string.
			System_Toast(sy->T("Path does not exist!"));
			return;
		}

		Path pendingMemStickFolder(newPath);

		if (!File::Exists(pendingMemStickFolder) && pendingMemStickFolder.Type() == PathType::NATIVE) {
			// Still no path? Try to automatically fix the case.
			std::string oldNewPath = newPath;
			FixPathCase(Path(""), newPath, FixPathCaseBehavior::FPC_FILE_MUST_EXIST);
			if (oldNewPath != newPath) {
				NOTICE_LOG(Log::IO, "Fixed path case: %s -> %s", oldNewPath.c_str(), newPath.c_str());
				pendingMemStickFolder = Path(newPath);
			} else {
				NOTICE_LOG(Log::IO, "Failed to fix case of path %s (result: %s)", newPath.c_str(), oldNewPath.c_str());
			}
		}

		if (pendingMemStickFolder == g_Config.memStickDirectory) {
			// Same directory as before - all good. Nothing to do.
			TriggerFinish(DialogResult::DR_OK);
			return;
		}

		if (!File::Exists(pendingMemStickFolder)) {
			System_Toast(sy->T("Path does not exist!"));
			return;
		}

		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
	});
#endif
}

void MemStickScreen::UseInternalStorage(UI::EventParams &params) {
	Path pendingMemStickFolder = PrivateMemstickFolder();

	if (initialSetup_) {
		// There's not gonna be any files here in this case since it's a fresh install.
		// Let's just accept it and move on. No need to move files either.
		if (!SwitchMemstickFolderTo(pendingMemStickFolder)) {
			ERROR_LOG(Log::System, "Could not switch memstick path in setup (internal)");
			g_OSD.Show(OSDType::MESSAGE_ERROR, T(I18NCat::MEMSTICK, "That folder doesn't work as a memstick folder."));
			return;
		}
		// Don't have a confirmation dialog that would otherwise do it for us, need to just switch directly to the main screen.
		screenManager()->switchScreen(new MainScreen());
	} else if (pendingMemStickFolder != g_Config.memStickDirectory) {
		// Always ask for confirmation when called from the UI. Likely there's already some data.
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
	} else {
		// User chose the same directory it's already in. Let's just bail.
		TriggerFinish(DialogResult::DR_OK);
	}
}

void MemStickScreen::UseStorageRoot(UI::EventParams &params) {
	Path pendingMemStickFolder = Path(g_externalDir);

	if (initialSetup_) {
		// There's not gonna be any files here in this case since it's a fresh install.
		// Let's just accept it and move on. No need to move files either.
		if (SwitchMemstickFolderTo(pendingMemStickFolder)) {
			TriggerFinish(DialogResult::DR_OK);
		} else {
			// This can't really happen?? Not worth making an error message.
			ERROR_LOG(Log::System, "Could not switch memstick path in setup");
		}
	} else if (pendingMemStickFolder != g_Config.memStickDirectory) {
		// Always ask for confirmation when called from the UI. Likely there's already some data.
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, false));
	} else {
		// User chose the same directory it's already in. Let's just bail.
		TriggerFinish(DialogResult::DR_OK);
	}
}

void MemStickScreen::Browse(UI::EventParams &params) {
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	System_BrowseForFolder(GetRequesterToken(), mm->T("Choose folder"), g_Config.memStickDirectory, [this](std::string_view value, int) {
		Path pendingMemStickFolder(value);
		INFO_LOG(Log::System, "Got folder: '%s' (old: %s)", pendingMemStickFolder.c_str(), g_Config.memStickDirectory.c_str());
		if (pendingMemStickFolder.empty()) {
			return;
		}
		// Browse finished. Let's pop up the confirmation dialog.
		if (!pendingMemStickFolder.empty() && pendingMemStickFolder == g_Config.memStickDirectory && File::IsDirectory(pendingMemStickFolder)) {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			// Not sure how this could happen, but let's go with it.
			g_OSD.Show(OSDType::MESSAGE_SUCCESS, di->T("Done!"));
			TriggerFinish(DialogResult::DR_OK);
			return;
		}
		screenManager()->push(new ConfirmMemstickMoveScreen(pendingMemStickFolder, initialSetup_));
	}, [this](int responseValue) {
		WARN_LOG(Log::System, "Folder browse cancelled: %d", responseValue);

		if (responseValue == 1) {
			// Show a message box explaining what went wrong.
			auto di = GetI18NCategory(I18NCat::DIALOG);
			auto ms = GetI18NCategory(I18NCat::MEMSTICK);
			auto nw = GetI18NCategory(I18NCat::NETWORKING);
			screenManager()->push(new UI::MessagePopupScreen(
				nw->T("Error"),  // from the networking category of strings.
				ms->T("MissingFileBrowserError", "This device is missing a file/folder browser. If you disabled the 'Files' app, please enable it and try again."),
				di->T("More info"), di->T("Cancel"), [](bool ok) {
					if (!ok) {
						return;
					}
					System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/troubleshooting/cant-pick-folder/");
				}
			));
		}
	});
}

void MemStickScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	// Other popups, like the missing file browser error, also finish with DR_OK.
	if (result == DialogResult::DR_OK && !strcmp(dialog->tag(), "ConfirmMemstickMove")) {
		INFO_LOG(Log::System, "Confirmation screen done - moving on.");
		TriggerFinish(DialogResult::DR_OK);
	}
	// otherwise, we just keep going.
}

ConfirmMemstickMoveScreen::ConfirmMemstickMoveScreen(const Path &newMemstickFolder, bool initialSetup)
	: UITwoPaneBaseDialogScreen(Path(), TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll),
		newMemstickFolder_(newMemstickFolder), initialSetup_(initialSetup), progressReporter_() {
	const Path &oldMemstickFolder = g_Config.memStickDirectory;
	existingFilesInNewFolder_ = FolderSeemsToBeUsed(newMemstickFolder);
	check_ = CheckNewMemstickFolder(newMemstickFolder, oldMemstickFolder);
	INFO_LOG(Log::System, "Old: '%s'", oldMemstickFolder.c_str());
	INFO_LOG(Log::System, "New: '%s' (check: %d)", newMemstickFolder.c_str(), (int)check_);

	// Moving into a folder that already has data would overwrite it, so that has to be asked for.
	if (initialSetup_ || existingFilesInNewFolder_ || !CanMoveData()) {
		moveData_ = false;
	}

	newSpaceTask_ = Promise<SpaceResult *>::Spawn(&g_threadManager, [newMemstickFolder]() -> SpaceResult * {
		int64_t freeSpaceNew;
		INFO_LOG(Log::System, "Computing free space in '%s'", newMemstickFolder.c_str());
		free_disk_space(newMemstickFolder, freeSpaceNew);
		return new SpaceResult{ freeSpaceNew };
	}, TaskType::IO_BLOCKING, TaskPriority::HIGH);
}

ConfirmMemstickMoveScreen::~ConfirmMemstickMoveScreen() {
	// We should no longer end up blocking here since the back button is disabled until the tasks are done.
	if (moveDataTask_) {
		INFO_LOG(Log::System, "Move Data task still running, blocking on it");
		MoveResult *result = moveDataTask_->BlockUntilReady();
		// Replaced mid-move (another app opening a game, say). The data is in the new folder now, and the
		// next start will use it, so this run has to as well.
		if (result && result->success && !SwitchMemstickFolderTo(newMemstickFolder_)) {
			ERROR_LOG(Log::System, "Couldn't switch to '%s' after the move", newMemstickFolder_.c_str());
		}
		delete result;
		delete moveDataTask_;
	}
	// This we just cancel / leak.
	if (newSpaceTask_) {
		newSpaceTask_->Cancel();
		delete newSpaceTask_;
	}
}

// A row like an InfoItem's, for text that can be too long for one line (a folder's location, or what the
// move is doing).
class WrappedTextItem : public UI::Item {
public:
	// With minLines, it's at least that many lines high, and the text starts at the top, so that text that
	// changes (like the move's progress) doesn't make it jump as it wraps onto more or fewer lines.
	WrappedTextItem(std::string_view text, int minLines = 1) : UI::Item(nullptr), text_(text), minLines_(minLines) {}

	void SetText(std::string_view text) {
		text_ = text;
	}

	void GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const override {
		const float availableWidth = horiz.type == UI::MeasureSpecType::UNSPECIFIED ? 10000.0f : horiz.size - PADDING_X * 2;
		float tw, th;
		dc.MeasureTextRect(dc.GetTheme().uiFont, 1.0f, 1.0f, text_, availableWidth, &tw, &th, FLAG_WRAP_TEXT);
		if (minLines_ > 1) {
			float lw, lineHeight;
			dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, "Wg", &lw, &lineHeight);
			th = std::max(th, lineHeight * minLines_);
		}
		w = tw + PADDING_X * 2;
		h = std::max(th + PADDING_Y * 2, UI::ITEM_HEIGHT);
	}

	void Draw(UIContext &dc) override {
		const UI::Style &style = dc.GetTheme().itemStyle;
		dc.FillRoundRect(style.background, bounds_, dc.GetTheme().choiceRoundRadius, roundCorners_);
		dc.SetFontStyle(dc.GetTheme().uiFont);
		if (minLines_ > 1) {
			dc.DrawTextRect(text_, bounds_.Expand(-PADDING_X, -PADDING_Y), style.fgColor, FLAG_WRAP_TEXT);
		} else {
			dc.DrawTextRect(text_, bounds_.Expand(-PADDING_X, 0), style.fgColor, ALIGN_VCENTER | FLAG_WRAP_TEXT);
		}
	}

	std::string DescribeText() const override {
		return text_;
	}

private:
	static constexpr float PADDING_X = 12.0f;
	static constexpr float PADDING_Y = 8.0f;
	std::string text_;
	int minLines_;
};

std::string_view ConfirmMemstickMoveScreen::GetTitle() const {
	return T(I18NCat::SYSTEM, "Memory Stick folder");
}

// Status and warnings first, then the folders.
void ConfirmMemstickMoveScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	doneNotice_ = nullptr;
	progressView_ = nullptr;
	progressBar_ = nullptr;

	ChoiceGroupList *groups = parent->Add(new ChoiceGroupList());

	if (restartTime_ > 0.0) {
		doneNotice_ = groups->Add(new NoticeView(NoticeLevel::SUCCESS, di->T("Done!"), RestartCountdownText()));
	} else if (moveDataTask_) {
		// What it's doing and the count on the first line, the file it's on below.
		ChoiceGroup *progressGroup = groups->Add(new ChoiceGroup());
		// The status line, and two for the path, which usually wraps in portrait.
		progressView_ = progressGroup->Add(new WrappedTextItem(progressReporter_.Format(), 3));
		progressBar_ = groups->Add(new ProgressBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		progressBar_->SetRoundCorners();
		progressBar_->SetProgress(std::max(0.0f, progressReporter_.Fraction()));
	}

	if (!error_.empty()) {
		groups->Add(new NoticeView(NoticeLevel::ERROR, error_, ""));
	}

	switch (check_) {
	case MemstickFolderCheck::INSIDE_CURRENT:
		groups->Add(new NoticeView(NoticeLevel::ERROR, ms->T("The new folder is inside the previous one"), ""));
		break;
	case MemstickFolderCheck::INSIDE_OTHER:
		groups->Add(new NoticeView(NoticeLevel::ERROR, ms->T("InsideOtherMemstick", "This folder is inside the PSP folder of another memory stick. Choose that memory stick's folder instead."), ""));
		break;
	case MemstickFolderCheck::LOOKS_LIKE_PSP_FOLDER:
		groups->Add(new NoticeView(NoticeLevel::ERROR, ms->T("LooksLikePSPFolder", "This looks like a PSP folder. Name it PSP if it isn't, and choose the folder that contains it instead."), ""));
		break;
	case MemstickFolderCheck::SAME_PSP_FOLDER:
		groups->Add(new NoticeView(NoticeLevel::INFO, ms->T("SamePSPFolder", "Contains the same PSP folder as the current one. Nothing will be moved."), ""));
		break;
	case MemstickFolderCheck::CONTAINS_CURRENT:
		groups->Add(new NoticeView(NoticeLevel::WARN, ms->T("ContainsCurrentMemstick", "The current folder is inside this one, so its data can't be moved here"), ""));
		break;
	case MemstickFolderCheck::OK:
		break;
	}

	// Only while it can still be changed.
	const bool busy = moveDataTask_ || restartTime_ > 0.0;
	if (!busy) {
		if (existingFilesInNewFolder_) {
			groups->Add(new NoticeView(NoticeLevel::SUCCESS, ms->T("Already contains PSP data"), ""));
			if (!moveData_) {
				groups->Add(new NoticeView(NoticeLevel::INFO, ms->T("No data will be changed"), ""));
			} else {
				groups->Add(new NoticeView(NoticeLevel::WARN, ms->T("MoveReplacesFiles", "Previously existing files in the new folder will be replaced by moved files with the same names"), ""));
			}
		}
		if (!initialSetup_ && !IsBlocked()) {
			groups->Add(new NoticeView(NoticeLevel::INFO, ms->T("PPSSPP will restart after the change"), ""));
		}
	}

	ChoiceGroup *newGroup = groups->Add(new ChoiceGroup(ms->T("Selected PSP Data Folder")));
	newGroup->Add(new WrappedTextItem(GetMemstickFolderDisplayName(newMemstickFolder_)));
	// Views get recreated after the free space has come in, so the text can't just be set when it does.
	const std::string freeSpace = freeSpace_ == FREE_SPACE_PENDING ? std::string("...") : FormatSpaceString(freeSpace_);
	newFreeSpaceView_ = newGroup->Add(new InfoItem(ms->T("Free space"), freeSpace));

	const Path &oldMemstickFolder = g_Config.memStickDirectory;
	if (!oldMemstickFolder.empty()) {
		ChoiceGroup *currentGroup = groups->Add(new ChoiceGroup(ms->T("Current")));
		currentGroup->Add(new WrappedTextItem(GetMemstickFolderDisplayName(oldMemstickFolder)));
	}
}

void ConfirmMemstickMoveScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	ChoiceGroupList *groups = parent->Add(new ChoiceGroupList());
	// Nothing to choose while it's moving, or done.
	if (moveDataTask_ || restartTime_ > 0.0) {
		return;
	}

	const bool canChooseMove = !initialSetup_ && CanMoveData() && !dataMoved_;
	if (!canChooseMove && IsBlocked()) {
		return;
	}
	ChoiceGroup *confirmGroup = groups->Add(new ChoiceGroup());
	if (!IsBlocked()) {
		confirmGroup->Add(new Choice(di->T("OK")))->OnClick.Handle(this, &ConfirmMemstickMoveScreen::OnConfirm);
	}
	ChoiceGroup *moveGroup = groups->Add(new ChoiceGroup());
	if (canChooseMove) {
		moveGroup->Add(new CheckBox(&moveData_, ms->T("Move Data")))->OnClick.Handle(this, &ConfirmMemstickMoveScreen::OnMoveDataClick);
	}
}

std::string ConfirmMemstickMoveScreen::RestartCountdownText() const {
	const int seconds = std::max(1, (int)ceil(restartTime_ - time_now_d()));
	return ApplySafeSubstitutions(T(I18NCat::MEMSTICK, "Restarting app in %1 seconds"), seconds);
}

void ConfirmMemstickMoveScreen::OnMoveDataClick(UI::EventParams &params) {
	RecreateViews();
}

// Back does nothing during a move: closing the screen would block the UI thread in the destructor until the
// move is done, and skip FinishFolderMove(). Nor during the countdown to the restart.
bool ConfirmMemstickMoveScreen::key(const KeyInput &key) {
	// Swallowed here, since the dialog marks itself finished before calling TriggerFinish().
	if ((moveDataTask_ || restartTime_ > 0.0) && UI::IsEscapeKey(key)) {
		return true;
	}
	return UITwoPaneBaseDialogScreen::key(key);
}

// The top bar's back button.
void ConfirmMemstickMoveScreen::TriggerFinish(DialogResult result) {
	if (moveDataTask_ || restartTime_ > 0.0) {
		return;
	}
	UITwoPaneBaseDialogScreen::TriggerFinish(result);
}

void ConfirmMemstickMoveScreen::onFinish(DialogResult result) {
	// Leaving after the data moved but the switch failed. The move already saved the new folder as the one
	// to use, so restart onto it rather than stay on the emptied old one.
	if (dataMoved_ && result != DialogResult::DR_OK) {
		INFO_LOG(Log::System, "Leaving after moving data, restarting onto '%s'", newMemstickFolder_.c_str());
		System_RestartApp("");
	}
}

void ConfirmMemstickMoveScreen::update() {
	UITwoPaneBaseDialogScreen::update();
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);

	if (restartTime_ > 0.0) {
		if (time_now_d() >= restartTime_) {
			restartTime_ = 0.0;
			System_RestartApp("");
			return;
		}
		if (doneNotice_) {
			doneNotice_->SetDetailsText(RestartCountdownText());
		}
	}

	if (moveDataTask_) {
		if (progressView_) {
			progressView_->SetText(progressReporter_.Format());
		}
		if (progressBar_) {
			progressBar_->SetProgress(std::max(0.0f, progressReporter_.Fraction()));
		}

		MoveResult *result = moveDataTask_->Poll();

		if (result) {
			if (result->success) {
				progressReporter_.SetProgress(T(I18NCat::DIALOG, "Done!"));
				INFO_LOG(Log::System, "Move data task finished successfully!");
				// Succeeded! If the switch below fails, OK only retries the switch.
				dataMoved_ = true;
				moveData_ = false;
				FinishFolderMove();
			} else {
				INFO_LOG(Log::System, "Move data task failed: %s (partly moved: %d)", result->errorMessage.c_str(), (int)result->partlyMoved);
				// The move puts back what it changed, so this is only rare.
				if (result->partlyMoved) {
					error_ = ms->T("MoveFailedDataSplit", "Some of the data couldn't be moved, or moved back. It's now partly in each folder. Please try again.");
				} else {
					error_ = ms->T("MoveFailedNothingChanged", "The data couldn't be moved. Nothing was changed.");
				}
				RecreateViews();
			}
			delete result;
			delete moveDataTask_;
			moveDataTask_ = nullptr;
		}
	}

	if (newSpaceTask_ && newFreeSpaceView_) {
		SpaceResult *result = newSpaceTask_->Poll();
		if (result) {
			freeSpace_ = result->bytesFree;
			newFreeSpaceView_->SetRightText(FormatSpaceString(freeSpace_));
			delete newSpaceTask_;
			newSpaceTask_ = nullptr;
		}
	}
}

void ConfirmMemstickMoveScreen::OnConfirm(UI::EventParams &params) {
	// Transfer all the files in /PSP from the original directory.
	// Should probably be done on a background thread so we can show some UI.
	// So we probably need another screen for this with a progress bar..
	// If the directory itself is called PSP, don't go below.
	if (IsBlocked() || moveDataTask_) {
		return;
	}

	// Only the switch is left to retry, and its own failure message says the data moved.
	if (dataMoved_) {
		FinishFolderMove();
		return;
	}

	// Find out now if the switch would fail, rather than after moving the data away.
	if (!CanSwitchMemstickFolderTo(newMemstickFolder_)) {
		error_ = T(I18NCat::MEMSTICK, "That folder doesn't work as a memstick folder.");
		RecreateViews();
		return;
	}

	if (moveData_ && CanMoveData()) {
		error_.clear();
		progressReporter_.SetProgress(T(I18NCat::MEMSTICK, "Starting move..."));

		moveDataTask_ = Promise<MoveResult *>::Spawn(&g_threadManager, [&]() -> MoveResult * {
			Path moveSrc = g_Config.memStickDirectory;
			Path moveDest = newMemstickFolder_;
			// Once copied, the new folder is saved as the one to use before the originals are deleted.
			MoveResult *result = MoveDirectoryContentsSafe(moveSrc, moveDest, progressReporter_, [moveDest]() {
				return WriteMemstickDirFile(moveDest);
			});
			NOTICE_LOG(Log::System, "Move task finished: %d", (int)(result != nullptr));
			return result;
		}, TaskType::IO_BLOCKING, TaskPriority::HIGH);

		RecreateViews();
	} else {
		FinishFolderMove();
	}
}

void ConfirmMemstickMoveScreen::FinishFolderMove() {
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);

	Path oldMemstickFolder = g_Config.memStickDirectory;

	// Successful so far, switch the memstick folder.
	if (!SwitchMemstickFolderTo(newMemstickFolder_)) {
		if (dataMoved_) {
			error_ = ms->T("DataMovedButNotSwitched", "Your data was moved to the new folder, but PPSSPP couldn't switch to it. Please try again.");
		} else {
			error_ = ms->T("That folder doesn't work as a memstick folder.");
		}
		RecreateViews();
		return;
	}

	INFO_LOG(Log::System, "Move from '%s' to '%s' complete. Updating config.", oldMemstickFolder.c_str(), newMemstickFolder_.c_str());

	// If the chosen folder already had a config, reload it! Otherwise keep the current settings, and
	// write them there.
	if (File::Exists(g_Config.IniFilename())) {
		g_Config.Load();
	} else {
		INFO_LOG(Log::System, "No config at '%s', keeping the current settings", g_Config.IniFilename().c_str());
		g_Config.Save("MemstickPathChanged");
	}

	// If the current browser directory is the old memstick folder, drop it.
	if (g_Config.currentDirectory == oldMemstickFolder) {
		g_Config.currentDirectory = g_Config.defaultCurrentDirectory;
	}

	PostLoadConfig();

	if (!initialSetup_) {
		// We restart the app to get the new settings, once the user has seen that it worked.
		INFO_LOG(Log::System, "Not initial setup. Restarting in a moment.");
		restartTime_ = time_now_d() + RESTART_DELAY_SECONDS;
		RecreateViews();
	} else {
		// This is initial setup, we now switch to the main screen, if we were successful
		// (which we better have been...)
		if (g_Config.Save("MemstickPathChanged")) {
			INFO_LOG(Log::System, "Initial setup succeeded. Switching to main screen!");
			// TriggerFinish(DialogResult::DR_OK);
			screenManager()->switchScreen(new MainScreen());
		} else {
			INFO_LOG(Log::System, "Initial setup failed.");
			error_ = ms->T("Failed to save config");
			RecreateViews();
		}
	}
}

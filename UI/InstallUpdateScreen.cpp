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

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Thread/Promise.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/ViewGroup.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Util/PathUtil.h"

#include "UI/InstallUpdateScreen.h"
#include "UI/MiscViews.h"
#include "UI/EmuScreen.h"

InstallUpdateScreen::InstallUpdateScreen(const Path &path, std::string_view title, bool allowRun, u64 archiveSize)
	: UITwoPaneBaseDialogScreen(Path(), TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll),
	path_(path), title_(title), allowRun_(allowRun) {
	destination_ = GetSysDirectory(DIRECTORY_NAND);

	fileSize_ = archiveSize;
	File::FileInfo fileInfo;
	if (fileSize_ == 0 && File::GetFileInfo(path_, &fileInfo)) {
		fileSize_ = fileInfo.size;
	}
	// There's no practical way to merge two firmwares, so an install replaces whatever is there.
	ReadInstalledFirmwareInfo(destination_, &installed_, false);
	overwrites_ = installed_.anythingInstalled;
}

std::string_view InstallUpdateScreen::GetTitle() const {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	return iz->T("PSP firmware update");
}

void InstallUpdateScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto st = GetI18NCategory(I18NCat::STORE);  // Borrow "Size" from here, like GameScreen does.

	LinearLayout *container = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(600, WRAP_CONTENT, 0.0f, UI::Gravity::G_HCENTER, Margins(10))));

	container->Add(new TextView(iz->T("Install PSP firmware update?"), ALIGN_LEFT, false))->SetBig(true);
	container->Add(new Spacer(8.0f));

	if (!title_.empty()) {
		// The updater's own title, which spells out the firmware version.
		container->Add(new TextWithImage(ImageID("I_INFO"), title_));
	}
	container->Add(new TextWithImage(ImageID("I_FILE"), GetFriendlyPath(path_)));
	if (fileSize_ > 0) {
		container->Add(new TextView(StringFromFormat("%s: %s", st->T_cstr("Size"), NiceSizeFormat(fileSize_).c_str())));
	}

	container->Add(new Spacer(12.0f));
	container->Add(new TextView(iz->T("Install into folder")));
	container->Add(new TextView(GetFriendlyPath(destination_)))->SetAlign(FLAG_WRAP_TEXT);

	// Show a warning in cases where the existing firmware seems valid (and not just fonts-only for example).
	if (overwrites_ && !installed_.version.empty()) {
		std::string newVersion = VersionFromUpdaterTitle(title_);
		if (newVersion.find('.') == std::string::npos || newVersion[0] < '0' || newVersion[0] > '9') {
			newVersion.clear();
		}

		if (newVersion != installed_.version) {
			const std::string_view unknown = "N/A";
			container->Add(new NoticeView(NoticeLevel::WARN, di->T("Confirm Overwrite"),
				ApplySafeSubstitutions(
					iz->T("ReplaceFirmware", "Firmware %1 is installed. It will be erased and replaced with %2."),
					installed_.version.empty() ? unknown : std::string_view(installed_.version),
					newVersion.empty() ? unknown : std::string_view(newVersion))));
		}
	}

	// leave space at the bottom so settings pane can contain actions and progress
	container->Add(new Spacer(12.0f));
}

void InstallUpdateScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	LinearLayout *container = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.0f, UI::Gravity::G_HCENTER, Margins(10))));
	installChoice_ = container->Add(new Choice(iz->T("Install"), ImageID("I_FOLDER_UPLOAD")));
	installChoice_->OnClick.Add([this](UI::EventParams &e) {
		StartInstall();
	});

	if (allowRun_) {
		Choice *runChoice = container->Add(new Choice(dev->T("Run"), ImageID("I_PLAY")));
		runChoice->OnClick.Add([this](UI::EventParams &e) {
			screenManager()->switchScreen(new EmuScreen(path_));
		});
	}

	progressBar_ = container->Add(new ProgressBar());
	progressBar_->SetVisibility(V_GONE);

	resultView_ = container->Add(new NoticeView(NoticeLevel::SUCCESS, "", ""));
	resultView_->SetVisibility(V_GONE);

	// The screen can get recreated mid-install (a rotation, say), so pick the status back up.
	RefreshStatus();
}

void InstallUpdateScreen::StartInstall() {
	if (state_ && !state_->done) {
		// Already running. A previous attempt that failed can be retried, though.
		return;
	}

	state_ = std::make_shared<InstallState>();
	reportedDone_ = false;

	PSARUnpackOptions options;
	options.model = EmulatedModelGeneration();

	g_threadManager.EnqueueTask(new IndependentTask(TaskType::IO_BLOCKING, TaskPriority::NORMAL,
		[state = state_, path = path_, destination = destination_, options]() mutable {
		options.progress = [state](float progress) {
			state->progress = progress;
		};
		state->success = InstallFirmware(path, destination, options, &state->stats, &state->error);
		// Everything above is published by this store - see the atomic in InstallState.
		state->done = true;
	}));

	RefreshStatus();
}

void InstallUpdateScreen::RefreshStatus() {
	using namespace UI;

	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);

	const bool installing = state_ && !state_->done;
	const bool succeeded = state_ && state_->done && state_->success;

	if (installChoice_) {
		// There's no point installing the same firmware twice, but a failure can be retried.
		installChoice_->SetEnabled(!installing && !succeeded);
	}
	if (progressBar_) {
		progressBar_->SetVisibility(installing ? V_VISIBLE : V_GONE);
		if (installing) {
			progressBar_->SetProgress(state_->progress);
		}
	}
	if (resultView_) {
		if (!state_ || !state_->done) {
			resultView_->SetVisibility(V_GONE);
		} else if (state_->success) {
			resultView_->SetLevelAndText(NoticeLevel::SUCCESS, iz->T("Installed!"));
			resultView_->SetDetailsText(StringFromFormat("%s - %d files", state_->stats.firmwareVersion.c_str(), state_->stats.written));
			resultView_->SetVisibility(V_VISIBLE);
		} else {
			resultView_->SetLevelAndText(NoticeLevel::ERROR, iz->T("Installation failed"));
			resultView_->SetDetailsText(state_->error);
			resultView_->SetVisibility(V_VISIBLE);
		}
	}
}

bool InstallUpdateScreen::key(const KeyInput &key) {
	// Ignore key presses while installing, so the user can't escape out mid-write.
	if (state_ && !state_->done) {
		return false;
	}
	return UITwoPaneBaseDialogScreen::key(key);
}

void InstallUpdateScreen::update() {
	UITwoPaneBaseDialogScreen::update();

	if (!state_) {
		return;
	}
	if (state_->done && !reportedDone_) {
		reportedDone_ = true;
		if (state_->success) {
			INFO_LOG(Log::Loader, "Installed firmware %s: %d files, %d failed", state_->stats.firmwareVersion.c_str(),
				state_->stats.written, state_->stats.failed);
		} else {
			ERROR_LOG(Log::Loader, "Failed to install the updater: %s", state_->error.c_str());
		}
	}
	RefreshStatus();
}

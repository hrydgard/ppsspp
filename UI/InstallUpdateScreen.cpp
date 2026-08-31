// Copyright (c) 2026- PPSSPP Project.

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

// An updater carries one file list per hardware revision, and anything the chosen model's list
// doesn't name isn't part of its firmware. Unpacking the model we claim to be keeps flash0
// consistent with what the emulator reports to games.
static PSPModelGeneration EmulatedModelGeneration() {
	return g_Config.iPSPModel == PSP_MODEL_FAT ? PSPModelGeneration::PSP_1000 : PSPModelGeneration::PSP_2000;
}

InstallUpdateScreen::InstallUpdateScreen(const Path &path, std::string_view title)
	: UISimpleBaseDialogScreen(Path(), SimpleDialogFlags::ContentsCanScroll), path_(path), title_(title) {
	destination_ = GetSysDirectory(DIRECTORY_NAND);

	File::FileInfo fileInfo;
	if (File::GetFileInfo(path_, &fileInfo)) {
		fileSize_ = fileInfo.size;
	}
	// There's no practical way to merge two firmwares, so an install replaces whatever is there.
	overwrites_ = File::Exists(destination_ / "flash0");
}

std::string_view InstallUpdateScreen::GetTitle() const {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	return iz->T("PSP firmware update");
}

void InstallUpdateScreen::CreateDialogViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto st = GetI18NCategory(I18NCat::STORE);  // Borrow "Size" from here, like GameScreen does.
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

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

	if (overwrites_) {
		container->Add(new NoticeView(NoticeLevel::WARN, di->T("Confirm Overwrite"), ""));
	}

	container->Add(new Spacer(12.0f));

	installChoice_ = container->Add(new Choice(iz->T("Install"), ImageID("I_FOLDER_UPLOAD")));
	installChoice_->OnClick.Add([this](UI::EventParams &e) {
		StartInstall();
	});

	Choice *runChoice = container->Add(new Choice(dev->T("Run"), ImageID("I_PLAY")));
	runChoice->OnClick.Add([this](UI::EventParams &e) {
		screenManager()->switchScreen(new EmuScreen(path_));
	});

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

	INFO_LOG(Log::Loader, "Unpacking the updater %s into %s (model %s)", path_.c_str(),
		destination_.c_str(), PSPModelGenerationToString(options.model));

	g_threadManager.EnqueueTask(new IndependentTask(TaskType::IO_BLOCKING, TaskPriority::NORMAL,
		[state = state_, path = path_, destination = destination_, options]() mutable {
		options.progress = [state](float progress) {
			state->progress = progress;
		};
		state->success = UnpackUpdater(path, destination, options, &state->stats, &state->error);
		if (state->success && state->stats.written == 0) {
			// Nothing came out, so the archive had no file list for the model we asked for -
			// old firmwares predate the later models. Not something to call a success.
			state->success = false;
			if (state->error.empty()) {
				state->error = "The updater has no firmware for this PSP model";
			}
		}
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
	return UISimpleBaseDialogScreen::key(key);
}

void InstallUpdateScreen::update() {
	UISimpleBaseDialogScreen::update();

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

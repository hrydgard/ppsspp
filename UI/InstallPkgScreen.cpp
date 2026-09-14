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

#include <memory>

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/DiskFree.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/UI/Context.h"
#include "Common/UI/UI.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Core/Loaders.h"
#include "Core/System.h"
#include "Core/Util/GameManager.h"
#include "Core/Util/PathUtil.h"

#include "UI/InstallPkgScreen.h"

InstallPkgScreen::InstallPkgScreen(const Path &pkgPath)
	: UITwoPaneBaseDialogScreen(Path(), TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll), pkgPath_(pkgPath) {
	g_GameManager.ResetInstallError();

	// Reading the package is cheap - the item table and two small PARAM.SFOs. We only keep the
	// info; the install re-opens the file on its own thread.
	std::unique_ptr<FileLoader> loader(ConstructFileLoader(pkgPath_));
	PkgReader reader;
	if (!loader || !reader.Open(loader.get(), &pkgError_)) {
		return;
	}
	pkgInfo_ = reader.Info();
	if (!pkgInfo_.isGameUpdate) {
		// Everything we can read is a game update; anything else got rejected above with a better
		// message than this.
		pkgError_ = "This PKG file isn't a PSP game update";
		return;
	}

	canInstall_ = true;
	installSize_ = PkgInstalledSize(pkgInfo_);
	destination_ = GetSysDirectory(DIRECTORY_GAME) / pkgInfo_.discId;
	alreadyInstalled_ = File::Exists(destination_ / "PBOOT.PBP");

	int64_t space = 0;
	if (free_disk_space(GetSysDirectory(DIRECTORY_GAME), space)) {
		freeSpace_ = space;
	}
}

std::string_view InstallPkgScreen::GetTitle() const {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	return iz->T("Game update");
}

void InstallPkgScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);

	installChoice_ = nullptr;

	if (canInstall_) {
		installChoice_ = parent->Add(new Choice(iz->T("Install"), ImageID("I_FOLDER_UPLOAD")));
		installChoice_->OnClick.Handle(this, &InstallPkgScreen::OnInstall);
	}

	if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
		parent->Add(new Spacer(12.0f));
		parent->Add(new Choice(di->T("Show in folder")))->OnClick.Add([this](UI::EventParams &) {
			System_ShowFileInFolder(pkgPath_);
		});
	}

	if (canInstall_) {
		parent->Add(new Spacer(12.0f));
		parent->Add(new CheckBox(&deletePkgFile_, iz->T("Delete PKG file")));
	}
}

void InstallPkgScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);
	auto er = GetI18NCategory(I18NCat::ERRORS);

	LinearLayout *leftColumn = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8))));

	if (!canInstall_) {
		leftColumn->Add(new TextView(GetFriendlyPath(pkgPath_)));
		leftColumn->Add(new NoticeView(NoticeLevel::ERROR, iz->T(pkgError_.empty() ? "This PKG file isn't a PSP game update" : pkgError_), ""));
		doneView_ = leftColumn->Add(new NoticeView(NoticeLevel::SUCCESS, "", ""));
		doneView_->SetVisibility(Visibility::V_GONE);
		return;
	}

	leftColumn->Add(new TextView(iz->T("Install game update?")))->SetBig(true);
	leftColumn->Add(new TextView(pkgPath_.GetFilename()));
	if (!pkgInfo_.title.empty()) {
		leftColumn->Add(new TextView(pkgInfo_.title));
	}
	if (!pkgInfo_.pbootTitle.empty()) {
		leftColumn->Add(new TextView(pkgInfo_.pbootTitle));
	}

	leftColumn->Add(new Spacer(8.0f));

	// What it patches, and to what. The disc version is the one the update was built against.
	leftColumn->Add(new TextView(ApplySafeSubstitutions("%1: %2 (%3)", iz->T("Game"), pkgInfo_.discId, pkgInfo_.discVersion)));
	if (!pkgInfo_.appVer.empty()) {
		leftColumn->Add(new TextView(ApplySafeSubstitutions("%1: %2", iz->T("Update version"), pkgInfo_.appVer)));
	}
	if (!pkgInfo_.systemVer.empty()) {
		leftColumn->Add(new TextView(ApplySafeSubstitutions("%1: %2", iz->T("Requires firmware"), pkgInfo_.systemVer)));
	}

	// Package contents aren't compressed, so this is what it'll actually take up.
	leftColumn->Add(new TextView(ApplySafeSubstitutions("%1: %2", iz->T("Space needed"), NiceSizeFormat(installSize_))));
	if (freeSpace_ >= 0) {
		leftColumn->Add(new TextView(ApplySafeSubstitutions("%1: %2", iz->T("Free space"), NiceSizeFormat((u64)freeSpace_))));
	}

	leftColumn->Add(new Spacer(8.0f));
	leftColumn->Add(new TextView(iz->T("Install into folder")));
	leftColumn->Add(new TextView(GetFriendlyPath(destination_)))->SetAlign(FLAG_WRAP_TEXT);

	doneView_ = leftColumn->Add(new NoticeView(NoticeLevel::SUCCESS, "", ""));
	doneView_->SetVisibility(Visibility::V_GONE);

	if (freeSpace_ >= 0 && (u64)freeSpace_ < installSize_) {
		leftColumn->Add(new NoticeView(NoticeLevel::ERROR, er->T("Not enough free space"), ""));
	}
	if (alreadyInstalled_) {
		leftColumn->Add(new NoticeView(NoticeLevel::WARN, di->T("Confirm Overwrite"), iz->T("An update for this game is already installed")));
	}
}

bool InstallPkgScreen::key(const KeyInput &key) {
	// Ignore key presses while installing, so the user can't escape mid-write.
	if (g_GameManager.GetState() == GameManagerState::IDLE) {
		return UIDialogScreen::key(key);
	}
	return false;
}

void InstallPkgScreen::OnInstall(UI::EventParams &params) {
	if (!canInstall_) {
		return;
	}
	if (g_GameManager.InstallPkgOnThread(pkgPath_, deletePkgFile_)) {
		installStarted_ = true;
		if (installChoice_) {
			installChoice_->SetEnabled(false);
		}
	}
}

void InstallPkgScreen::update() {
	auto iz = GetI18NCategory(I18NCat::INSTALLZIP);

	using namespace UI;
	if (g_GameManager.GetState() == GameManagerState::IDLE && doneView_) {
		const std::string err = g_GameManager.GetInstallError();
		if (!err.empty()) {
			doneView_->SetLevelAndText(NoticeLevel::ERROR, iz->T(err));
			doneView_->SetVisibility(Visibility::V_VISIBLE);
		} else if (installStarted_) {
			doneView_->SetLevelAndText(NoticeLevel::SUCCESS, iz->T("Installed!"));
			doneView_->SetVisibility(Visibility::V_VISIBLE);
		}
	}
	UIBaseDialogScreen::update();
}

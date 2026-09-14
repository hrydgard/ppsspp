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

#pragma once

#include "Common/File/Path.h"
#include "Common/UI/Notice.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/View.h"

#include "Core/Util/PkgUnpack.h"
#include "UI/BaseScreens.h"
#include "UI/SimpleDialogScreen.h"

// Offers to install a PSP game update from a .pkg file, the way InstallZipScreen does for zips.
// The package goes into PSP/GAME/<DISC_ID>, and from then on booting that disc runs the update -
// see FindGameUpdatePBOOT() in Core/PSPLoaders.cpp.
class InstallPkgScreen : public UITwoPaneBaseDialogScreen {
public:
	InstallPkgScreen(const Path &pkgPath);

	void update() override;
	bool key(const KeyInput &key) override;

	const char *tag() const override { return "InstallPkg"; }

protected:
	void CreateSettingsViews(UI::ViewGroup *parent) override;
	void CreateContentViews(UI::ViewGroup *parent) override;
	std::string_view GetTitle() const override;
	ViewLayoutMode LayoutMode() const override {
		return ViewLayoutMode::ApplyInsets;
	}

private:
	void OnInstall(UI::EventParams &params);

	Path pkgPath_;
	Path destination_;

	PkgInfo pkgInfo_;
	std::string pkgError_;      // Why the package can't be installed, if it can't.
	bool canInstall_ = false;
	u64 installSize_ = 0;
	s64 freeSpace_ = -1;        // Negative if we couldn't find out.
	bool alreadyInstalled_ = false;

	UI::Choice *installChoice_ = nullptr;
	NoticeView *doneView_ = nullptr;
	bool installStarted_ = false;
	bool deletePkgFile_ = false;
};

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

#pragma once

#include <functional>

#include "Common/File/Path.h"

#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"

#include "UI/MiscScreens.h"

class SavedataView;

class InstallZipScreen : public UIDialogScreenWithBackground {
public:
	InstallZipScreen(const Path &zipPath);
	void update() override;
	bool key(const KeyInput &key) override;

	const char *tag() const override { return "InstallZip"; }

protected:
	void CreateViews() override;

private:
	UI::EventReturn OnInstall(UI::EventParams &params);

	UI::Choice *installChoice_ = nullptr;
	UI::Choice *backChoice_ = nullptr;
	UI::TextView *doneView_ = nullptr;
	SavedataView *existingSaveView_ = nullptr;
	Path savedataToOverwrite_;
	Path zipPath_;
	std::vector<Path> destFolders_;
	int destFolderChoice_ = 0;
	ZipFileInfo zipFileInfo_{};
	bool returnToHomebrew_ = true;
	bool installStarted_ = false;
	bool deleteZipFile_ = false;
};


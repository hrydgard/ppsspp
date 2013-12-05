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

#include "base/logging.h"
#include "i18n/i18n.h"
#include "ui/ui.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/ui_atlas.h"
#include "file/file_util.h"

#include "Core/Util/GameManager.cpp"
#include "UI/InstallZipScreen.h"

void InstallZipScreen::CreateViews() {
	using namespace UI;

	FileInfo fileInfo;
	bool success = getFileInfo(zipPath_.c_str(), &fileInfo);

	I18NCategory *di = GetI18NCategory("Dialog");

	Margins actionMenuMargins(0, 100, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	ViewGroup *leftColumn = new AnchorLayout(new LinearLayoutParams(1.0f));
	root_->Add(leftColumn);

	leftColumn->Add(new TextView(di->T("Install game from ZIP file?"), ALIGN_LEFT, false, new AnchorLayoutParams(10, 10, NONE, NONE)));
	leftColumn->Add(new TextView(zipPath_, ALIGN_LEFT, false, new AnchorLayoutParams(10, 60, NONE, NONE)));

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	root_->Add(rightColumnItems);

	installChoice_ = rightColumnItems->Add(new Choice(di->T("Install")));
	installChoice_->OnClick.Handle(this, &InstallZipScreen::OnInstall);
	rightColumnItems->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnOK);  // OK so that EmuScreen will handle it right
	rightColumnItems->Add(new CheckBox(&deleteZipFile_, di->T("Delete ZIP file")));
}

UI::EventReturn InstallZipScreen::OnInstall(UI::EventParams &params) {
	installChoice_->SetEnabled(false);
	if (g_GameManager.InstallGameOnThread(zipPath_)) {
		screenManager()->finishDialog(this, DR_OK);
	}
	return UI::EVENT_DONE;
}

void InstallZipScreen::update(InputState &input) {
	UIScreen::update(input);
}

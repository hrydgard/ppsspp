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

#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "math/curves.h"
#include "util/text/utf8.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "UI/SavedataScreen.h"
#include "UI/MainScreen.h"

#include "Core/Host.h"
#include "Core/Config.h"
#include "Core/SaveState.h"
#include "Core/System.h"

class SaveDataPopup : public UIScreenWithGameBackground {
public:
	SaveDataPopup(std::string savePath) : UIScreenWithGameBackground(savePath) {

	}

	void CreateViews() override {

	}
};

SavedataScreen::SavedataScreen(std::string gamePath) : UIScreenWithGameBackground(gamePath) {
	
}

void SavedataScreen::CreateViews() {
	using namespace UI;
	std::string dir = GetSysDirectory(DIRECTORY_SAVEDATA);
	gridStyle_ = false;
	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	//ScrollView *scroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT)));
	browser_ = root_->Add(new GameBrowser(dir, false, &gridStyle_, "", "", 0, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
}

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

#include <functional>

#include "ui/view.h"
#include "ui/ui_screen.h"
#include "ui/ui_context.h"
#include "UI/MiscScreens.h"

extern std::string activeCheatFile;
extern std::string gameTitle;

class CwCheatScreen : public UIDialogScreenWithBackground {
public:
	CwCheatScreen(std::string gamePath);

	void CreateCodeList();
	void processFileOn(std::string activatedCheat);
	void processFileOff(std::string deactivatedCheat);

	UI::EventReturn OnAddCheat(UI::EventParams &params);
	UI::EventReturn OnImportCheat(UI::EventParams &params);
	UI::EventReturn OnEditCheatFile(UI::EventParams &params);
	UI::EventReturn OnEnableAll(UI::EventParams &params);

	void onFinish(DialogResult result) override;
protected:
	void CreateViews() override;

private:
	UI::EventReturn OnCheckBox(UI::EventParams &params);
	std::vector<std::string> formattedList_;
	UI::ScrollView *rightScroll_;
};

class CheatCheckBox : public UI::CheckBox {
public:
	CheatCheckBox(bool *toggle, const std::string &text, UI::LayoutParams *layoutParams = nullptr)
		: UI::CheckBox(toggle, text, "", layoutParams), text_(text) {
	}

	std::string Text() {
		return text_;
	}

private:
	std::string text_;
};

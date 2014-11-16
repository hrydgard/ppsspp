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

#include <deque>
#include "Common/FileUtil.h"

#include "base/functional.h"
#include "ui/view.h"
#include "ui/ui_screen.h"
#include "ui/ui_context.h"
#include "../Core/CwCheat.h"
#include "UI/MiscScreens.h"
#include "UI/GameSettingsScreen.h"

using namespace UI;
extern std::string activeCheatFile;
extern std::string gameTitle;

class CwCheatScreen : public UIDialogScreenWithBackground {
public:
	CwCheatScreen() {}
	void CreateCodeList();
	void processFileOn(std::string activatedCheat);
	void processFileOff(std::string deactivatedCheat);
	const char * name;
	std::string activatedCheat, deactivatedCheat;
	UI::EventReturn OnBack(UI::EventParams &params);
	UI::EventReturn OnAddCheat(UI::EventParams &params);
	UI::EventReturn OnImportCheat(UI::EventParams &params);
	UI::EventReturn OnEditCheatFile(UI::EventParams &params);
	UI::EventReturn OnEnableAll(UI::EventParams &params);

	virtual void onFinish(DialogResult result);
protected:
	virtual void CreateViews();

private:
	UI::EventReturn OnCheckBox(UI::EventParams &params);
	std::vector<std::string> formattedList_;
	bool anythingChanged_;
};

// TODO: Instead just hook the OnClick event on a regular checkbox.
class CheatCheckBox : public ClickableItem, public CwCheatScreen {
public:
	CheatCheckBox(bool *toggle, const std::string &text, const std::string &smallText = "", LayoutParams *layoutParams = 0)
		: ClickableItem(layoutParams), toggle_(toggle), text_(text) {
			OnClick.Handle(this, &CheatCheckBox::OnClicked);
	}

	virtual void Draw(UIContext &dc);

	EventReturn OnClicked(EventParams &e) {
		if (toggle_) {
			*toggle_ = !(*toggle_);
		}
		bool temp;
		temp = *toggle_;
		if (temp) {
			activatedCheat = text_;
			processFileOn(activatedCheat);
		} else {
			deactivatedCheat = text_;
			processFileOff(deactivatedCheat);
		}
		return EVENT_DONE;
	}

private:
	bool *toggle_;
	std::string text_;
	std::string smallText_;
};

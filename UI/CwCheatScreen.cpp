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

#include "android/app-android.h"


#include "input/input_state.h"
#include "ui/ui.h"
#include "i18n/i18n.h"

#include "Core/Core.h"

#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"
#include "UI/GamepadEmu.h"
#include "UI/UIShader.h"

#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "UI/CwCheatScreen.h"
#include "UI/view.h"

static std::vector<std::string> cheatList;
extern void DrawBackground(float alpha);
static CWCheatEngine *cheatEngine2;

std::vector<std::string> CwCheatScreen::CreateCodeList() {
	cheatEngine2 = new CWCheatEngine();
	cheatList = cheatEngine2->GetCodesList();
	int j = 0;
	for (size_t i = 0; i < cheatList.size(); i++) {
		if (cheatList[i].substr(0, 3) == "_C1") {
			formattedList.push_back(cheatList[i].substr(4));
			enableCheat[j++] = true;
			locations.push_back(i);
		}
		if (cheatList[i].substr(0, 3) == "_C0") {
			formattedList.push_back(cheatList[i].substr(4));
			enableCheat[j++] = false;
		}

	}
	delete cheatEngine2;
	return formattedList;
}
void CwCheatScreen::CreateViews() {
	using namespace UI;
	std::vector<std::string> formattedList;
	I18NCategory *k = GetI18NCategory("CwCheats");
	formattedList = CreateCodeList();
	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT));
	leftColumn->Add(new Choice(k->T("Back")))->OnClick.Handle<CwCheatScreen>(this, &CwCheatScreen::OnBack);


	ScrollView *rightScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll->SetScrollToTop(false);
	LinearLayout *rightColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	rightScroll->Add(rightColumn);
	
	root_->Add(leftColumn);
	root_->Add(rightScroll);
	rightColumn->Add(new ItemHeader(k->T("Cheats")));
	for (size_t i = 0; i < formattedList.size(); i++) {
		name = formattedList[i].c_str();
		rightColumn->Add(new CheatCheckBox(&enableCheat[i], k->T(name)))->OnClick.Handle(this, &CwCheatScreen::OnCheckBox);
		}
	
}
UI::EventReturn CwCheatScreen::OnBack(UI::EventParams &params)
{
	screenManager()->finishDialog(this, DR_OK);
	g_Config.bReloadCheats = true;
	return UI::EVENT_DONE;
}
UI::EventReturn CwCheatScreen::OnCheckBox(UI::EventParams &params) {

	return UI::EVENT_DONE;
}
void CwCheatScreen::processFileOn(std::string activatedCheat) {
	
	for (int i = 0; i < cheatList.size(); i++) {
		if (cheatList[i].substr(4) == activatedCheat) {
			cheatList[i] = "_C1 " + activatedCheat;
		}
	}
	
	os.open(activeCheatFile.c_str());
	for (int j = 0; j < cheatList.size(); j++) {
		os << cheatList[j];
		if (j < cheatList.size() - 1) {
			os << "\n";
		}
	}
	os.close();
	
}
void CwCheatScreen::processFileOff(std::string deactivatedCheat) {
	
	for (int i = 0; i < cheatList.size(); i++) {
		if (cheatList[i].substr(4) == deactivatedCheat) {
			cheatList[i] = "_C0 " + deactivatedCheat;
		}
	}

	os.open(activeCheatFile.c_str());
	for (int j = 0; j < cheatList.size(); j++) {
		os << cheatList[j];
		if (j < cheatList.size() - 1) {
			os << "\n";
		}
	}
	os.close();
	
}

void CheatCheckBox::Draw(UIContext &dc) {
	ClickableItem::Draw(dc);
	int paddingX = 12;
	int paddingY = 8;

	int image = *toggle_ ? dc.theme->checkOn : dc.theme->checkOff;

	Style style = dc.theme->itemStyle;
	if (!IsEnabled())
		style = dc.theme->itemDisabledStyle;

	dc.Draw()->DrawText(dc.theme->uiFont, text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
	dc.Draw()->DrawImage(image, bounds_.x2() - paddingX, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}
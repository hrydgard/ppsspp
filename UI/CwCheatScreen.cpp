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

#include <deque>
#include "input/input_state.h"
#include "ui/ui.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"

#include "Common/FileUtil.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CwCheat.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"

#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "UI/CwCheatScreen.h"

static bool enableAll = false;
static std::vector<std::string> cheatList;
static CWCheatEngine *cheatEngine2;
static std::deque<bool> bEnableCheat;
static std::string gamePath_;


CwCheatScreen::CwCheatScreen(std::string gamePath)
	: UIDialogScreenWithBackground() {
	gamePath_ = gamePath;
}

void CwCheatScreen::CreateCodeList() {
	GameInfo *info = g_gameInfoCache->GetInfo(NULL, gamePath_, 0);
	if (info && info->paramSFOLoaded) {
		gameTitle = info->paramSFO.GetValueString("DISC_ID");
	}
	std::size_t lslash = gamePath_.find_last_of("/");
	std::size_t lastdot = gamePath_.find_last_of(".");
	std::string extension = gamePath_.substr(lastdot + 1);
	for (size_t i = 0; i < extension.size(); i++) {
		extension[i] = tolower(extension[i]);
	}
	if ((extension != "iso" && extension != "cso" && extension != "pbp") || gameTitle == "") {
		if (extension == "elf") {
			gameTitle = "ELF000000";
		} else {
			gameTitle = gamePath_.substr(lslash + 1);
		}
	}
	cheatEngine2 = new CWCheatEngine();
	cheatEngine2->CreateCheatFile();
	cheatList = cheatEngine2->GetCodesList();

	bEnableCheat.clear();
	formattedList_.clear();
	for (size_t i = 0; i < cheatList.size(); i++) {
		if (cheatList[i].substr(0, 3) == "_C1") {
			formattedList_.push_back(cheatList[i].substr(4));
			bEnableCheat.push_back(true);
		}
		if (cheatList[i].substr(0, 3) == "_C0") {
			formattedList_.push_back(cheatList[i].substr(4));
			bEnableCheat.push_back(false);
		}
	}
	delete cheatEngine2;
}

void CwCheatScreen::CreateViews() {
	using namespace UI;
	I18NCategory *cw = GetI18NCategory("CwCheats");
	I18NCategory *di = GetI18NCategory("Dialog");
	CreateCodeList();
	g_Config.bReloadCheats = true;
	root_ = new LinearLayout(ORIENT_HORIZONTAL);
	Margins actionMenuMargins(50, -15, 15, 0);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(400, FILL_PARENT));
	leftColumn->Add(new ItemHeader(cw->T("Options")));
	leftColumn->Add(new Choice(di->T("Back")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	//leftColumn->Add(new Choice(cw->T("Add Cheat")))->OnClick.Handle(this, &CwCheatScreen::OnAddCheat);
	leftColumn->Add(new Choice(cw->T("Import Cheats")))->OnClick.Handle(this, &CwCheatScreen::OnImportCheat);
#if !defined(MOBILE_DEVICE)
	leftColumn->Add(new Choice(cw->T("Edit Cheat File")))->OnClick.Handle(this, &CwCheatScreen::OnEditCheatFile);
#endif
	leftColumn->Add(new Choice(cw->T("Enable/Disable All")))->OnClick.Handle(this, &CwCheatScreen::OnEnableAll);
	leftColumn->Add(new PopupSliderChoice(&g_Config.iCwCheatRefreshRate, 1, 1000, cw->T("Refresh Rate"), 1, screenManager()));

	ScrollView *rightScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(0.5f));
	rightScroll->SetTag("CwCheats");
	rightScroll->SetScrollToTop(false);
	LinearLayout *rightColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT, actionMenuMargins));
	LayoutParams *layout = new LayoutParams(500, 50, LP_PLAIN);
	rightScroll->Add(rightColumn);

	root_->Add(leftColumn);
	root_->Add(rightScroll);
	rightColumn->Add(new ItemHeader(cw->T("Cheats")));
	for (size_t i = 0; i < formattedList_.size(); i++) {
		name = formattedList_[i].c_str();
		rightColumn->Add(new CheatCheckBox(&bEnableCheat[i], cw->T(name), ""))->OnClick.Handle(this, &CwCheatScreen::OnCheckBox);
	}
}

void CwCheatScreen::onFinish(DialogResult result) {
	std::fstream fs;
	if (result != DR_BACK) // This only works for BACK here.
		return;
	File::OpenCPPFile(fs, activeCheatFile, std::ios::out);
	for (int j = 0; j < (int)cheatList.size(); j++) {
		fs << cheatList[j];
		if (j < (int)cheatList.size() - 1) {
			fs << "\n";
		}
	}
	fs.close();
	g_Config.bReloadCheats = true;
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
}

UI::EventReturn CwCheatScreen::OnEnableAll(UI::EventParams &params) {
	std::fstream fs;
	enableAll = !enableAll;
	File::OpenCPPFile(fs, activeCheatFile, std::ios::out);
	for (int j = 0; j < (int)cheatList.size(); j++) {
		if (enableAll == 1 && cheatList[j].substr(0, 3) == "_C0"){
			cheatList[j].replace(0, 3, "_C1");
		}
		else if (enableAll == 0 && cheatList[j].substr(0, 3) == "_C1") {
			cheatList[j].replace(0, 3, "_C0");
		}
	}
	for (size_t y = 0; y < bEnableCheat.size(); y++) {
		bEnableCheat[y] = enableAll;
	}
	for (int i = 0; i < (int)cheatList.size(); i++) {
		fs << cheatList[i];
		if (i < (int)cheatList.size() - 1) {
			fs << "\n";
		}
	}
	fs.close();

	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnAddCheat(UI::EventParams &params) {
	screenManager()->finishDialog(this, DR_OK);
	g_Config.bReloadCheats = true;
	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnEditCheatFile(UI::EventParams &params) {
	g_Config.bReloadCheats = true;
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
	screenManager()->finishDialog(this, DR_OK);
	File::openIniFile(activeCheatFile);
	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnImportCheat(UI::EventParams &params) {
	if (gameTitle.length() != 9) {
		WARN_LOG(COMMON, "CWCHEAT: Incorrect ID(%s) - can't import cheats.", gameTitle.c_str());
		return UI::EVENT_DONE;
	}
	std::string line;
	std::vector<std::string> title;
	bool finished = false, skip = false;
	std::vector<std::string> newList;

	std::string cheatFile = GetSysDirectory(DIRECTORY_CHEATS) + "cheat.db";

	std::fstream fs;
	File::OpenCPPFile(fs, cheatFile, std::ios::in);

	if (!fs.is_open()) {
		WARN_LOG(COMMON, "Unable to open %s\n", cheatFile.c_str());
	}

	while (fs.good()) {
		getline(fs, line); // get line from file
		if (line == "_S " + gameTitle.substr(0, 4) + "-" + gameTitle.substr(4)) {
			title.push_back(line);
			getline(fs, line);
			title.push_back(line);
			getline(fs, line);
			do {
				if (finished == false){
					getline(fs, line);
				}
				if (line.substr(0, 3) == "_C0" || line.substr(0, 3) == "_C1") {
					//Test if cheat already exists in cheatList
					for (size_t j = 0; j < formattedList_.size(); j++) {
						if (line.substr(4) == formattedList_[j]) {
							finished = false;
							goto loop;
						}
					}

					newList.push_back(line);
					getline(fs, line);
					do {
						newList.push_back(line);
						getline(fs, line);
					} while (line.substr(0, 2) == "_L");
					finished = true;
				} else {
					continue;
				}
			loop:;
			} while (line.substr(0, 2) != "_S");
			finished = true;
		}
		if (finished == true)
			break;
	}
	fs.close();
	std::string title2;
	File::OpenCPPFile(fs, activeCheatFile, std::ios::in);
	getline(fs, title2);
	fs.close();
	File::OpenCPPFile(fs, activeCheatFile, std::ios::out | std::ios::app);

	auto it = title.begin();
	if (title2.substr(0, 2) != "_S" && it != title.end() && (++it) != title.end()) {
		fs << title[0] << "\n" << title[1];
	}

	NOTICE_LOG(COMMON, "Imported %u entries from %s.\n", (int)newList.size(), cheatFile.c_str());
	if (newList.size() != 0) {
		fs << "\n";
	}

	for (int i = 0; i < (int)newList.size(); i++) {
		fs << newList[i];
		if (i < (int)newList.size() - 1) {
			fs << "\n";
		}
	}
	fs.close();
	g_Config.bReloadCheats = true;
	//Need a better way to refresh the screen, rather than exiting and having to re-enter.
	screenManager()->finishDialog(this, DR_OK);
	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnCheckBox(UI::EventParams &params) {
	return UI::EVENT_DONE;
}

void CwCheatScreen::processFileOn(std::string activatedCheat) {
	std::fstream fs;
	for (size_t i = 0; i < cheatList.size(); i++) {
		if (cheatList[i].substr(4) == activatedCheat) {
			cheatList[i] = "_C1 " + activatedCheat;
		}
	}

	File::OpenCPPFile(fs, activeCheatFile, std::ios::out);

	for (size_t j = 0; j < cheatList.size(); j++) {
		fs << cheatList[j];
		if (j < cheatList.size() - 1) {
			fs << "\n";
		}
	}
	fs.close();
}

void CwCheatScreen::processFileOff(std::string deactivatedCheat) {
	std::fstream fs;
	for (size_t i = 0; i < cheatList.size(); i++) {
		if (cheatList[i].substr(4) == deactivatedCheat) {
			cheatList[i] = "_C0 " + deactivatedCheat;
		}
	}

	File::OpenCPPFile(fs, activeCheatFile, std::ios::out);

	for (size_t j = 0; j < cheatList.size(); j++) {
		fs << cheatList[j];
		if (j < cheatList.size() - 1) {
			fs << "\n";
		}
	}
	fs.close();
}

void CheatCheckBox::Draw(UIContext &dc) {
	ClickableItem::Draw(dc);
	int paddingX = 16;
	int paddingY = 12;

	int image = *toggle_ ? dc.theme->checkOn : dc.theme->checkOff;

	UI::Style style = dc.theme->itemStyle;
	if (!IsEnabled())
		style = dc.theme->itemDisabledStyle;

	dc.SetFontStyle(dc.theme->uiFont);
	dc.DrawText(text_.c_str(), bounds_.x + paddingX, bounds_.centerY(), style.fgColor, ALIGN_VCENTER);
	dc.Draw()->DrawImage(image, bounds_.x2() - paddingX, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_RIGHT | ALIGN_VCENTER);
}


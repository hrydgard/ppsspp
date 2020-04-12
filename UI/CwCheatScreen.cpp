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

#include "ext/cityhash/city.h"
#include "i18n/i18n.h"
#include "ui/ui.h"
#include "util/text/utf8.h"

#include "Common/FileUtil.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CwCheat.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

#include "UI/GameInfoCache.h"
#include "UI/CwCheatScreen.h"

static const int FILE_CHECK_FRAME_INTERVAL = 53;

CwCheatScreen::CwCheatScreen(const std::string &gamePath)
	: UIDialogScreenWithBackground() {
	gamePath_ = gamePath;
}

CwCheatScreen::~CwCheatScreen() {
	delete engine_;
}

void CwCheatScreen::LoadCheatInfo() {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, 0);
	std::string gameID;
	if (info && info->paramSFOLoaded) {
		gameID = info->paramSFO.GetValueString("DISC_ID");
	}
	if ((info->id.empty() || !info->disc_total)
		&& gamePath_.find("/PSP/GAME/") != std::string::npos) {
		gameID = g_paramSFO.GenerateFakeID(gamePath_);
	}

	if (engine_ == nullptr || gameID != gameID_) {
		gameID_ = gameID;
		delete engine_;
		engine_ = new CWCheatEngine(gameID_);
		engine_->CreateCheatFile();
	}

	// We won't parse this, just using it to detect changes to the file.
	std::string str;
	if (readFileToString(true, engine_->CheatFilename().c_str(), str)) {
		fileCheckHash_ = CityHash64(str.c_str(), str.size());
	}
	fileCheckCounter_ = 0;

	fileInfo_ = engine_->FileInfo();

	// Let's also trigger a reload, in case it changed.
	g_Config.bReloadCheats = true;
}

void CwCheatScreen::CreateViews() {
	using namespace UI;
	auto cw = GetI18NCategory("CwCheats");
	auto di = GetI18NCategory("Dialog");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	LoadCheatInfo();
	Margins actionMenuMargins(50, -15, 15, 0);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(400, FILL_PARENT));
	leftColumn->Add(new ItemHeader(cw->T("Options")));
	//leftColumn->Add(new Choice(cw->T("Add Cheat")))->OnClick.Handle(this, &CwCheatScreen::OnAddCheat);
	leftColumn->Add(new Choice(cw->T("Import Cheats")))->OnClick.Handle(this, &CwCheatScreen::OnImportCheat);
#if !defined(MOBILE_DEVICE)
	leftColumn->Add(new Choice(cw->T("Edit Cheat File")))->OnClick.Handle(this, &CwCheatScreen::OnEditCheatFile);
#endif
	leftColumn->Add(new Choice(cw->T("Enable/Disable All")))->OnClick.Handle(this, &CwCheatScreen::OnEnableAll);
	leftColumn->Add(new PopupSliderChoice(&g_Config.iCwCheatRefreshRate, 1, 1000, cw->T("Refresh Rate"), 1, screenManager()));

	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 0.5f));
	rightScroll_->SetTag("CwCheats");
	rightScroll_->SetScrollToTop(false);
	rightScroll_->ScrollTo(g_Config.fCwCheatScrollPosition);
	LinearLayout *rightColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT, actionMenuMargins));
	rightScroll_->Add(rightColumn);

	rightColumn->Add(new ItemHeader(cw->T("Cheats")));
	for (size_t i = 0; i < fileInfo_.size(); ++i) {
		rightColumn->Add(new CheckBox(&fileInfo_[i].enabled, fileInfo_[i].name))->OnClick.Add([=](UI::EventParams &) {
			return OnCheckBox((int)i);
		});
	}

	LinearLayout *layout = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	layout->Add(leftColumn);
	layout->Add(rightScroll_);
	root_->Add(layout);

	AddStandardBack(root_);
}

void CwCheatScreen::update() {
	if (fileCheckCounter_++ >= FILE_CHECK_FRAME_INTERVAL && engine_) {
		// Check if the file has changed.  If it has, we'll reload.
		std::string str;
		if (readFileToString(true, engine_->CheatFilename().c_str(), str)) {
			uint64_t newHash = CityHash64(str.c_str(), str.size());
			if (newHash != fileCheckHash_) {
				// This will update the hash.
				RecreateViews();
			}
		}
		fileCheckCounter_ = 0;
	}

	UIDialogScreenWithBackground::update();
}

void CwCheatScreen::onFinish(DialogResult result) {
	if (result != DR_BACK) // This only works for BACK here.
		return;

	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
	g_Config.fCwCheatScrollPosition = rightScroll_->GetScrollPosition();
}

UI::EventReturn CwCheatScreen::OnEnableAll(UI::EventParams &params) {
	enableAllFlag_ = !enableAllFlag_;

	// Flip all the switches.
	for (auto &info : fileInfo_) {
		info.enabled = enableAllFlag_;
	}

	if (!RebuildCheatFile(INDEX_ALL)) {
		// Probably the file was modified outside PPSSPP, refresh.
		// TODO: Report error.
		RecreateViews();
		return UI::EVENT_SKIPPED;
	}

	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnAddCheat(UI::EventParams &params) {
	TriggerFinish(DR_OK);
	g_Config.bReloadCheats = true;
	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnEditCheatFile(UI::EventParams &params) {
	g_Config.bReloadCheats = true;
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
	if (engine_) {
#if PPSSPP_PLATFORM(UWP)
		LaunchBrowser(engine_->CheatFilename().c_str());
#else
		File::openIniFile(engine_->CheatFilename());
#endif
	}
	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnImportCheat(UI::EventParams &params) {
	if (gameID_.length() != 9 || !engine_) {
		WARN_LOG(COMMON, "CWCHEAT: Incorrect ID(%s) - can't import cheats.", gameID_.c_str());
		return UI::EVENT_DONE;
	}
	std::string line;
	std::vector<std::string> title;
	bool finished = false, skip = false;
	std::vector<std::string> newList;

	std::string cheatFile = GetSysDirectory(DIRECTORY_CHEATS) + "cheat.db";
	std::string gameID = StringFromFormat("_S %s-%s", gameID_.substr(0, 4).c_str(), gameID_.substr(4).c_str());

	std::fstream fs;
	File::OpenCPPFile(fs, cheatFile, std::ios::in);

	if (!fs.is_open()) {
		WARN_LOG(COMMON, "Unable to open %s\n", cheatFile.c_str());
	}

	while (fs.good()) {
		getline(fs, line); // get line from file
		if (line == gameID) {
			title.push_back(line);
			getline(fs, line);
			title.push_back(line);
			do {
				if (finished == false){
					getline(fs, line);
				}
				if (line[0] == '_' && line[1] == 'C') {
					// Test if cheat already exists.
					for (const auto &existing : fileInfo_) {
						if (line.substr(4) == existing.name) {
							finished = false;
							goto loop;
						}
					}

					newList.push_back(line);
					getline(fs, line);
					do {
						newList.push_back(line);
						getline(fs, line);
					} while ((line[0] == '_' && line[1] == 'L') || line[0] == '/' || line[0] == '#');
					finished = true;
				} else {
					continue;
				}
			loop:;
			} while (fs.good() && ((line[0] == '_' && line[1] != 'S') || line[0] == '/' || line[0] == '#'));
			finished = true;
		}
		if (finished == true)
			break;
	}
	fs.close();
	std::string title2;
	File::OpenCPPFile(fs, engine_->CheatFilename(), std::ios::in);
	getline(fs, title2);
	fs.close();
	File::OpenCPPFile(fs, engine_->CheatFilename(), std::ios::out | std::ios::app);

	auto it = title.begin();
	if (((title2[0] == '_' && title2[1] != 'S') || title2[0] == '/' || title2[0] == '#') && it != title.end() && (++it) != title.end()) {
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
	RecreateViews();
	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnCheckBox(int index) {
	if (!RebuildCheatFile(index)) {
		// TODO: Report error.  Let's reload the file, presumably it changed.
		RecreateViews();
		return UI::EVENT_SKIPPED;
	}

	return UI::EVENT_DONE;
}

bool CwCheatScreen::RebuildCheatFile(int index) {
	std::fstream fs;
	if (!engine_ || !File::OpenCPPFile(fs, engine_->CheatFilename(), std::ios::in)) {
		return false;
	}

	// In case lines were edited while we weren't looking, reload them.
	std::vector<std::string> lines;
	for (; fs && !fs.eof(); ) {
		std::string line;
		std::getline(fs, line, '\n');
		lines.push_back(line);
	}
	fs.close();

	auto updateLine = [&](const CheatFileInfo &info) {
		// Line numbers start with one, not zero.
		size_t lineIndex = info.lineNum - 1;
		if (lines.size() > lineIndex) {
			auto &line = lines[lineIndex];
			// This is the one to change.  Let's see if it matches - maybe the file changed.
			bool isCheatDef = line.find("_C") != line.npos;
			bool hasCheatName = !info.name.empty() && line.find(info.name) != line.npos;
			if (!isCheatDef || !hasCheatName) {
				return false;
			}

			line = (info.enabled ? "_C1 " : "_C0 ") + info.name;
			return true;
		}
		return false;
	};

	if (index == INDEX_ALL) {
		for (const auto &info : fileInfo_) {
			// Bail out if any don't match with no changes.
			if (!updateLine(info)) {
				return false;
			}
		}
	} else {
		if (!updateLine(fileInfo_[index])) {
			return false;
		}
	}


	if (!File::OpenCPPFile(fs, engine_->CheatFilename(), std::ios::out | std::ios::trunc)) {
		return false;
	}

	for (const auto &line : lines) {
		fs << line << '\n';
	}
	fs.close();

	// Cheats will need to be reparsed now.
	g_Config.bReloadCheats = true;
	return true;
}

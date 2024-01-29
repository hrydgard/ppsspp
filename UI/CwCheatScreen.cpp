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

#include "ppsspp_config.h"
#include "ext/xxhash.h"
#include "Common/UI/UI.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CwCheat.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

#include "UI/GameInfoCache.h"
#include "UI/CwCheatScreen.h"

static const int FILE_CHECK_FRAME_INTERVAL = 53;

static Path GetGlobalCheatFilePath() {
	return GetSysDirectory(DIRECTORY_CHEATS) / "cheat.db";
}

CwCheatScreen::CwCheatScreen(const Path &gamePath)
	: UIDialogScreenWithGameBackground(gamePath) {
}

CwCheatScreen::~CwCheatScreen() {
	delete engine_;
}

bool CwCheatScreen::TryLoadCheatInfo() {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
	std::string gameID;
	if (!info->Ready(GameInfoFlags::PARAM_SFO)) {
		return false;
	}
	gameID = info->paramSFO.GetValueString("DISC_ID");
	if ((info->id.empty() || !info->disc_total)
		&& gamePath_.FilePathContainsNoCase("PSP/GAME/")) {
		gameID = g_paramSFO.GenerateFakeID(gamePath_);
	}

	if (!engine_ || gameID != gameID_) {
		gameID_ = gameID;
		delete engine_;
		engine_ = new CWCheatEngine(gameID_);
		engine_->CreateCheatFile();
	}

	// We won't parse this, just using it to detect changes to the file.
	std::string str;
	if (File::ReadTextFileToString(engine_->CheatFilename(), &str)) {
		fileCheckHash_ = XXH3_64bits(str.c_str(), str.size());
	}
	fileCheckCounter_ = 0;

	fileInfo_ = engine_->FileInfo();

	// Let's also trigger a reload, in case it changed.
	g_Config.bReloadCheats = true;
	return true;
}

void CwCheatScreen::CreateViews() {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	TryLoadCheatInfo();  // in case the info is already in cache.
	Margins actionMenuMargins(50, -15, 15, 0);

	LinearLayout *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(400, FILL_PARENT));
	//leftColumn->Add(new Choice(cw->T("Add Cheat")))->OnClick.Handle(this, &CwCheatScreen::OnAddCheat);
	leftColumn->Add(new ItemHeader(cw->T("Import Cheats")));

	Path cheatPath = GetGlobalCheatFilePath();

	std::string root = GetSysDirectory(DIRECTORY_MEMSTICK_ROOT).ToString();

	std::string title = StringFromFormat(cw->T("Import from %s"), "PSP/Cheats/cheat.db");

	leftColumn->Add(new Choice(title.c_str()))->OnClick.Handle(this, &CwCheatScreen::OnImportCheat);
	leftColumn->Add(new Choice(mm->T("Browse"), ImageID("I_FOLDER_OPEN")))->OnClick.Handle(this, &CwCheatScreen::OnImportBrowse);
	errorMessageView_ = leftColumn->Add(new TextView(di->T("LoadingFailed")));
	errorMessageView_->SetVisibility(V_GONE);

	leftColumn->Add(new ItemHeader(di->T("Options")));
#if !defined(MOBILE_DEVICE)
	leftColumn->Add(new Choice(cw->T("Edit Cheat File")))->OnClick.Handle(this, &CwCheatScreen::OnEditCheatFile);
#endif
	leftColumn->Add(new Choice(di->T("Disable All")))->OnClick.Handle(this, &CwCheatScreen::OnDisableAll);
	leftColumn->Add(new PopupSliderChoice(&g_Config.iCwCheatRefreshIntervalMs, 1, 1000, 77, cw->T("Refresh interval"), 1, screenManager()))->SetFormat(di->T("%d ms"));


	rightScroll_ = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 0.5f));
	rightScroll_->SetTag("CwCheats");
	rightScroll_->RememberPosition(&g_Config.fCwCheatScrollPosition);
	LinearLayout *rightColumn = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT, actionMenuMargins));
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
	if (gameID_.empty()) {
		if (TryLoadCheatInfo()) {
			RecreateViews();
		}
	}

	if (fileCheckCounter_++ >= FILE_CHECK_FRAME_INTERVAL && engine_) {
		// Check if the file has changed.  If it has, we'll reload.
		std::string str;
		if (File::ReadTextFileToString(engine_->CheatFilename(), &str)) {
			uint64_t newHash = XXH3_64bits(str.c_str(), str.size());
			if (newHash != fileCheckHash_) {
				// This will update the hash.
				RecreateViews();
			}
		}
		fileCheckCounter_ = 0;
	}

	UIDialogScreenWithGameBackground::update();
}

void CwCheatScreen::onFinish(DialogResult result) {
	if (result != DR_BACK) // This only works for BACK here.
		return;

	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
}

UI::EventReturn CwCheatScreen::OnDisableAll(UI::EventParams &params) {
	// Disable all the switches.
	for (auto &info : fileInfo_) {
		info.enabled = false;
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
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
	if (engine_) {
		File::OpenFileInEditor(engine_->CheatFilename());
	}
	return UI::EVENT_DONE;
}

static char *GetLineNoNewline(char *temp, int sz, FILE *fp) {
	char *line = fgets(temp, sz, fp);
	if (!line)
		return nullptr;

	// If the last character is \n, just make it the terminator.
	char *end = line + strlen(line) - 1;
	if (*end == '\n')
		*end = '\0';
	return line;
}

UI::EventReturn CwCheatScreen::OnImportBrowse(UI::EventParams &params) {
	System_BrowseForFile(GetRequesterToken(), "Open cheat DB file", BrowseFileType::DB, [&](const std::string &value, int) {
		Path path(value);
		INFO_LOG(SYSTEM, "Attempting to load cheats from: '%s'", path.ToVisualString().c_str());
		if (ImportCheats(path)) {
			g_Config.bReloadCheats = true;
		} else {
			// Show an error message?
		}
		RecreateViews();
	});
	return UI::EVENT_DONE;
}

UI::EventReturn CwCheatScreen::OnImportCheat(UI::EventParams &params) {
	if (!ImportCheats(GetGlobalCheatFilePath())) {
		// Show an error message?
		errorMessageView_->SetVisibility(UI::V_VISIBLE);
		return UI::EVENT_DONE;
	}

	g_Config.bReloadCheats = true;
	RecreateViews();
	return UI::EVENT_DONE;
}

bool CwCheatScreen::ImportCheats(const Path & cheatFile) {
	if (gameID_.length() != 9 || !engine_) {
		WARN_LOG(COMMON, "CWCHEAT: Incorrect ID(%s) - can't import cheats.", gameID_.c_str());
		return false;
	}

	std::string gameID = StringFromFormat("_S %s-%s", gameID_.substr(0, 4).c_str(), gameID_.substr(4).c_str());

	FILE *in = File::OpenCFile(cheatFile, "rt");
	if (!in) {
		WARN_LOG(COMMON, "Unable to open %s\n", cheatFile.c_str());
		return false;
	}

	std::vector<std::string> title;
	std::vector<std::string> newList;

	char linebuf[2048]{};
	bool parseGameEntry = false;
	bool parseCheatEntry = false;

	while (in && !feof(in)) {
		char *line = GetLineNoNewline(linebuf, sizeof(linebuf), in);

		if (!line) {
			continue;
		}

		if (line[0] == '_' && line[1] == 'S') {
			parseGameEntry = gameID == line;
			parseCheatEntry = false;
		} else if (parseGameEntry && line[0] == '_' && line[1] == 'C') {
			// Test if cheat already exists.
			parseCheatEntry = !HasCheatWithName(std::string(line).substr(4));
		}

		if (!parseGameEntry) {
			if (newList.size() > 0) {
				// Only parse the first matching game entry.
				break;
			} else {
				// Haven't yet found a matching game entry, continue parsing.
				continue;
			}
		}

		if (line[0] == '_' && (line[1] == 'S' || line[1] == 'G') && title.size() < 2) {
			title.push_back(line);
		} else if (parseCheatEntry && ((line[0] == '_' && (line[1] == 'C' || line[1] == 'L')) || line[0] == '/' || line[0] == '#')) {
			newList.push_back(line);
		}
	}
	fclose(in);

	std::string title2;
	// Hmm, this probably gets confused about BOMs?
	FILE *inTitle2 = File::OpenCFile(engine_->CheatFilename(), "rt");
	if (inTitle2) {
		char temp[2048];
		char *line = GetLineNoNewline(temp, sizeof(temp), inTitle2);
		if (line)
			title2 = line;
		fclose(inTitle2);
	}

	FILE *append = File::OpenCFile(engine_->CheatFilename(), "at");
	if (!append)
		return UI::EVENT_SKIPPED;

	if (title2.size() == 0 || title2[0] != '_' || title2[1] != 'S') {
		for (int i = (int)title.size(); i > 0; i--) {
			newList.insert(newList.begin(), title[i - 1]);
		}
	}

	NOTICE_LOG(COMMON, "Imported %u lines from %s.\n", (int)newList.size(), cheatFile.c_str());
	if (newList.size() != 0) {
		fputc('\n', append);
	}

	for (int i = 0; i < (int)newList.size(); i++) {
		fprintf(append, "%s", newList[i].c_str());
		if (i < (int)newList.size() - 1) {
			fputc('\n', append);
		}
	}
	fclose(append);
	return true;
}

UI::EventReturn CwCheatScreen::OnCheckBox(int index) {
	if (!RebuildCheatFile(index)) {
		// TODO: Report error.  Let's reload the file, presumably it changed.
		RecreateViews();
		return UI::EVENT_SKIPPED;
	}

	return UI::EVENT_DONE;
}

bool CwCheatScreen::HasCheatWithName(const std::string &name) {
	for (const auto &existing : fileInfo_) {
		if (name == existing.name) {
			return true;
		}
	}

	return false;
}

bool CwCheatScreen::RebuildCheatFile(int index) {
	if (!engine_)
		return false;
	FILE *in = File::OpenCFile(engine_->CheatFilename(), "rt");
	if (!in)
		return false;

	// In case lines were edited while we weren't looking, reload them.
	std::vector<std::string> lines;
	for (; !feof(in); ) {
		char temp[2048];
		char *line = GetLineNoNewline(temp, sizeof(temp), in);
		if (!line)
			break;

		lines.push_back(line);
	}
	fclose(in);

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

	FILE *out = File::OpenCFile(engine_->CheatFilename(), "wt");
	if (!out) {
		return false;
	}

	for (size_t i = 0; i < lines.size(); ++i) {
		fprintf(out, "%s", lines[i].c_str());
		if (i != lines.size() - 1)
			fputc('\n', out);
	}
	fclose(out);

	// Cheats will need to be reparsed now.
	g_Config.bReloadCheats = true;
	return true;
}

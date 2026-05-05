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
#include "Common/UI/PopupScreens.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/CwCheat.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

#include "UI/GameInfoCache.h"
#include "UI/CwCheatScreen.h"
#include "UI/MiscViews.h"

static const int FILE_CHECK_FRAME_INTERVAL = 53;

static Path GetGlobalCheatFilePath() {
	return GetSysDirectory(DIRECTORY_CHEATS) / "cheat.db";
}

CwCheatScreen::CwCheatScreen(const Path &gamePath)
	: UITwoPaneBaseDialogScreen(gamePath, TwoPaneFlags::Default) {
}

CwCheatScreen::~CwCheatScreen() {
	delete engine_;
}

bool CwCheatScreen::WantsTextInput() const {
	// We don't want to pop a software keyboard on the cheat screen, just for type-to-search.
	return !System_GetPropertyBool(SYSPROP_KEYBOARD_IS_SOFT);
}

bool CwCheatScreen::TryLoadCheatInfo() {
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
	std::string gameID;
	if (!info->Ready(GameInfoFlags::PARAM_SFO)) {
		return false;
	}
	gameID = info->GetParamSFO().GetValueString("DISC_ID");
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

void CwCheatScreen::BeforeCreateViews() {
	TryLoadCheatInfo();  // in case the info is already in cache.
}

bool CwCheatScreen::key(const KeyInput &input) {
	if (search_.Key(cheatList_, input)) {
		// This will eat up the ESC key, which is used to cancel searches.
		return true;
	}

	return UITwoPaneBaseDialogScreen::key(input);
}

void CwCheatScreen::CreateSettingsViews(UI::ViewGroup *leftColumn) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	//leftColumn->Add(new Choice(cw->T("Add Cheat")))->OnClick.Handle(this, &CwCheatScreen::OnAddCheat);
	leftColumn->Add(new ItemHeader(cw->T("Import Cheats")));

	Path cheatPath = GetGlobalCheatFilePath();

	std::string root = GetSysDirectory(DIRECTORY_MEMSTICK_ROOT).ToString();

	std::string title = StringFromFormat(cw->T_cstr("Import from %s"), "PSP/Cheats/cheat.db");

	leftColumn->Add(new Choice(title))->OnClick.Handle(this, &CwCheatScreen::OnImportCheat);
	leftColumn->Add(new Choice(mm->T("Browse"), ImageID("I_FOLDER_OPEN")))->OnClick.Handle(this, &CwCheatScreen::OnImportBrowse);

	leftColumn->Add(new ItemHeader(di->T("Options")));
	Choice *searchChoice = leftColumn->Add(new Choice(di->T("Search"), ImageID("I_SEARCH")));
	searchChoice->OnClick.Add([this, searchChoice, screenManager = screenManager(), token = GetRequesterToken()](UI::EventParams &) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		AskForInput(screenManager, token, searchChoice, di->T("Search"), [this](const std::string &text, bool success) {
			if (!success) {
				return;
			}
			search_.searchFilter = text;
			search_.ApplySearchFilter(cheatList_, false);
		});
	});
#if !defined(MOBILE_DEVICE)
	leftColumn->Add(new Choice(cw->T("Edit Cheat File")))->OnClick.Handle(this, &CwCheatScreen::OnEditCheatFile);
#endif
	leftColumn->Add(new Choice(di->T("Disable All")))->OnClick.Handle(this, &CwCheatScreen::OnDisableAll);
	leftColumn->Add(new PopupSliderChoice(&g_Config.iCwCheatRefreshIntervalMs, 1, 1000, 77, cw->T("Refresh interval"), 1, screenManager()))->SetFormat(di->T("%d ms"));
}

void CwCheatScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	UI::LinearLayout *rightSide = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 0.5f)));
	search_.searchFilter.clear();
	search_.searchBar = rightSide->Add(new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	search_.searchBar->OnCancel.Add([this](UI::EventParams &) {
		search_.searchFilter.clear();
		search_.ApplySearchFilter(cheatList_, false);
	});

	UI::ScrollView *rightScroll = rightSide->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f)));
	rightScroll->SetTag("CwCheats");
	rightScroll->RememberPosition(&g_Config.fCwCheatScrollPosition);

	LinearLayout *rightColumn = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(200, FILL_PARENT));
	rightColumn->SetSpacing(0.0f);
	rightScroll->Add(rightColumn);

	cheatList_ = rightColumn;

	if (!errorMessage_.empty()) {
		rightColumn->Add(new NoticeView(errorLevel_, errorMessage_, errorDetails_));
		rightColumn->Add(new Spacer(8.0f));
	}

	rightColumn->Add(new ItemHeader(cw->T("Cheats")));

	bool prevIsTitle = false;
	View *prev = nullptr;
	for (size_t i = 0; i < fileInfo_.size(); ++i) {
		std::string_view text;
		if (fileInfo_[i].IsTitle(&text)) {
			// Title.
			TextView *titleView = rightColumn->Add(new TextView(text, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, UI::Margins(8, 8, 8, 0))));
			titleView->SetTextSize(UI::TextSize::Big);
			titleView->SetAlwaysVisibleInSearch(true);
			prevIsTitle = true;
			prev = nullptr;
		} else if (fileInfo_[i].IsPostComment(&text)) {
			rightColumn->Add(new SettingHint(text, prev));
			prevIsTitle = false;
		} else {
			// Regular cheat code.
			if (!prevIsTitle) {
				rightColumn->Add(new Spacer(8.0f));
			}
			CheckBox *checkBox = rightColumn->Add(new CheckBox(&fileInfo_[i].enabled, fileInfo_[i].name));
			checkBox->OnClick.Add([=](UI::EventParams &) {
				OnCheckBox((int)i);
			});
			prev = checkBox;
			prevIsTitle = false;
		}
	}
}

std::string_view CwCheatScreen::GetTitle() const {
	auto cw = GetI18NCategory(I18NCat::CWCHEATS);
	return cw->T("Cheats");
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

	UIBaseDialogScreen::update();
}

void CwCheatScreen::onFinish(DialogResult result) {
	if (result != DR_BACK) // This only works for BACK here.
		return;

	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
}

void CwCheatScreen::OnDisableAll(UI::EventParams &params) {
	// Disable all the switches.
	for (auto &info : fileInfo_) {
		info.enabled = false;
	}

	if (!RebuildCheatFile(INDEX_ALL)) {
		// Probably the file was modified outside PPSSPP, refresh.
		// TODO: Report error.
		RecreateViews();
	}
}

void CwCheatScreen::OnAddCheat(UI::EventParams &params) {
	TriggerFinish(DR_OK);
	g_Config.bReloadCheats = true;
}

void CwCheatScreen::OnEditCheatFile(UI::EventParams &params) {
	g_Config.bReloadCheats = true;
	std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
	if (MIPSComp::jit) {
		MIPSComp::jit->ClearCache();
	}
	if (engine_) {
		File::OpenFileInEditor(engine_->CheatFilename());
	}
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

void CwCheatScreen::ImportAndReport(const Path &cheatFile) {
	int cheatCount = 0;
	if (!ImportCheats(cheatFile, &cheatCount)) {
		// Show an error message?
		if (File::Exists(cheatFile)) {
			auto er = GetI18NCategory(I18NCat::ERRORS);
			errorMessage_ = ApplySafeSubstitutions(er->T("File not found: %1"), "");
			errorLevel_ = NoticeLevel::WARN;
		} else {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			errorMessage_ = di->T("LoadingFailed");
			errorLevel_ = NoticeLevel::ERROR;
		}
		errorDetails_ = GetFriendlyPath(cheatFile);
	} else if (cheatCount == 0) {
		auto cw = GetI18NCategory(I18NCat::CWCHEATS);
		// Show an error message?
		errorLevel_ = NoticeLevel::INFO;
		errorMessage_ = cw->T("No new cheats found for this game");
		errorDetails_.clear();
	} else {
		auto cw = GetI18NCategory(I18NCat::CWCHEATS);
		// Show a success message?
		errorLevel_ = NoticeLevel::SUCCESS;
		errorMessage_ = ApplySafeSubstitutions(cw->T("Added %1 cheats for this game"), cheatCount);
		errorDetails_ = GetFriendlyPath(cheatFile);
	}
	g_Config.bReloadCheats = true;
	RecreateViews();
}

void CwCheatScreen::OnImportBrowse(UI::EventParams &params) {
	System_BrowseForFile(GetRequesterToken(), "Open cheat DB file", BrowseFileType::DB, [&](const std::string &value, int) {
		if (value.empty()) {
			return;
		}
		Path path(value);
		INFO_LOG(Log::System, "Attempting to load cheats from: '%s'", path.ToVisualString().c_str());
		int cheatsFound = 0;
		ImportAndReport(path);
	});
}

void CwCheatScreen::OnImportCheat(UI::EventParams &params) {
	const Path importPath = GetGlobalCheatFilePath();
	ImportAndReport(importPath);
}

bool CwCheatScreen::ImportCheats(const Path &cheatFile, int *cheatsFound) {
	FILE *in = File::OpenCFile(cheatFile, "rt");
	if (!in) {
		WARN_LOG(Log::Common, "Unable to open %s\n", cheatFile.c_str());
		return false;
	}

	if (gameID_.length() != 9 || !engine_) {
		WARN_LOG(Log::Common, "CWCHEAT: Incorrect ID(%s) - can't import cheats.", gameID_.c_str());
		return false;
	}

	std::string gameID = StringFromFormat("_S %s-%s", gameID_.substr(0, 4).c_str(), gameID_.substr(4).c_str());

	std::vector<std::string> title;
	std::vector<std::string> newList;

	char linebuf[2048]{};
	bool parseGameEntry = false;
	bool parseCheatEntry = false;

	(*cheatsFound) = 0;
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
			(*cheatsFound)++;
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
	if (!append) {
		return false;
	}

	if (title2.size() == 0 || title2[0] != '_' || title2[1] != 'S') {
		for (int i = (int)title.size(); i > 0; i--) {
			newList.insert(newList.begin(), title[i - 1]);
		}
	}

	NOTICE_LOG(Log::Common, "Imported %u lines from %s.\n", (int)newList.size(), cheatFile.c_str());
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

void CwCheatScreen::OnCheckBox(int index) {
	if (!RebuildCheatFile(index)) {
		// TODO: Report error.  Let's reload the file, presumably it changed.
		RecreateViews();
	}
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

	// Don't force an auto-reload, though.
	std::string str;
	if (File::ReadTextFileToString(engine_->CheatFilename(), &str)) {
		uint64_t newHash = XXH3_64bits(str.c_str(), str.size());
		fileCheckHash_ = newHash;
	}

	// Cheats will need to be reparsed now.
	g_Config.bReloadCheats = true;
	return true;
}

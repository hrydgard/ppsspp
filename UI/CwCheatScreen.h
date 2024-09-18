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

#include <cstdint>
#include <functional>

#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/Context.h"
#include "UI/MiscScreens.h"

struct CheatFileInfo;
class CWCheatEngine;

class CwCheatScreen : public UIDialogScreenWithGameBackground {
public:
	CwCheatScreen(const Path &gamePath);
	~CwCheatScreen();

	bool TryLoadCheatInfo();

	UI::EventReturn OnAddCheat(UI::EventParams &params);
	UI::EventReturn OnImportCheat(UI::EventParams &params);
	UI::EventReturn OnImportBrowse(UI::EventParams &params);
	UI::EventReturn OnEditCheatFile(UI::EventParams &params);
	UI::EventReturn OnDisableAll(UI::EventParams &params);

	void update() override;
	void onFinish(DialogResult result) override;

	const char *tag() const override { return "CwCheat"; }

protected:
	void CreateViews() override;

private:
	UI::EventReturn OnCheckBox(int index);
	bool ImportCheats(const Path &cheatFile);

	enum { INDEX_ALL = -1 };
	bool HasCheatWithName(const std::string &name);
	bool RebuildCheatFile(int index);

	UI::ScrollView *rightScroll_ = nullptr;
	UI::TextView *errorMessageView_ = nullptr;

	CWCheatEngine *engine_ = nullptr;
	std::vector<CheatFileInfo> fileInfo_;
	std::string gameID_;
	int fileCheckCounter_ = 0;
	uint64_t fileCheckHash_ = 0;
	bool enableAllFlag_ = false;
};

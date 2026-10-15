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

#pragma once

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Notice.h"

#include "UI/SimpleDialogScreen.h"

// Compress ISOs to CSO, or decompress CSO/ZSO/DAX back to ISO, one game or a whole folder at a
// time, through the user-supplied maxcso library (see Core/Util/MaxCSO.h). Only reachable when
// the library was found at startup.
class MaxCSOScreen : public UITwoPaneBaseDialogScreen {
public:
	MaxCSOScreen(const Path &gamePath)
		: UITwoPaneBaseDialogScreen(gamePath, TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::ContentsCanScroll) {}

	const char *tag() const override { return "MaxCSO"; }
	void update() override;

protected:
	std::string_view GetTitle() const override;
	void CreateSettingsViews(UI::ViewGroup *parent) override;
	void CreateContentViews(UI::ViewGroup *parent) override;

private:
	void BrowseAndStart(bool compress, bool folder);
	void RefreshStatus();

	UI::TextView *statusText_ = nullptr;
	UI::ProgressBar *fileProgress_ = nullptr;
	UI::ProgressBar *totalProgress_ = nullptr;
	NoticeView *summary_ = nullptr;
	UI::LinearLayout *results_ = nullptr;
	UI::Choice *cancelChoice_ = nullptr;
	std::vector<UI::Choice *> startChoices_;
	size_t shownResults_ = 0;
};

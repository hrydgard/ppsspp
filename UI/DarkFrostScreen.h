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
#include "UI/MiscScreens.h"
#include "UI/GameSettingsScreen.h"

#ifndef _DF
#include "../Core/DarkFrost.h"
#endif

class DFExtMenu;

class DarkFrostScreen : public UIDialogScreenWithBackground {
public:
	DarkFrostScreen() {}
	void init();
	UI::EventReturn OnBack(UI::EventParams &params);

	//options
	UI::EventReturn OnRealAddressing(UI::EventParams &params);
	UI::EventReturn OnReloadCheats(UI::EventParams &params);
	UI::EventReturn OnSaveCheats(UI::EventParams &params);
	UI::EventReturn OnCheatsActivated(UI::EventParams &params);

	//searcher
	UI::EventReturn OnExactValue(UI::EventParams &params);
	UI::EventReturn OnUnknownValue8(UI::EventParams &params);
	UI::EventReturn OnUnknownValue16(UI::EventParams &params);
	UI::EventReturn OnUnknownValue32(UI::EventParams &params);
	UI::EventReturn OnFindText(UI::EventParams &params);
	UI::EventReturn OnSearchRange(UI::EventParams &params);
	UI::EventReturn OnDMASearcher(UI::EventParams &params);
	UI::EventReturn OnRemoveSearches(UI::EventParams &params);

protected:
	virtual void CreateViews();
};
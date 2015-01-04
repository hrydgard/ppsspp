// Copyright (c) 2014- PPSSPP Project.

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

#include "base/functional.h"
#include "ui/ui_screen.h"
#include "ui/viewgroup.h"
#include "UI/MiscScreens.h"

class GamePauseScreen : public UIDialogScreenWithGameBackground {
public:
	GamePauseScreen(const std::string &filename) : UIDialogScreenWithGameBackground(filename), finishNextFrame_(false) {}
	virtual ~GamePauseScreen();

	void onFinish(DialogResult result) override;
	virtual void dialogFinished(const Screen *dialog, DialogResult dr) override;

protected:
	virtual void CreateViews() override;
	virtual void update(InputState &input) override;
	virtual void sendMessage(const char *message, const char *value) override;
	void CallbackDeleteConfig(bool yes);

private:
	UI::EventReturn OnMainSettings(UI::EventParams &e);
	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnExitToMenu(UI::EventParams &e);
	UI::EventReturn OnReportFeedback(UI::EventParams &e);

	UI::EventReturn OnRewind(UI::EventParams &e);

	UI::EventReturn OnScreenshotClicked(UI::EventParams &e);
	UI::EventReturn OnCwCheat(UI::EventParams &e);

	UI::EventReturn OnCreateConfig(UI::EventParams &e);
	UI::EventReturn OnDeleteConfig(UI::EventParams &e);

	UI::EventReturn OnSwitchUMD(UI::EventParams &e);
	UI::EventReturn OnState(UI::EventParams &e);

	UI::Choice *saveStateButton_;
	UI::Choice *loadStateButton_;

	// hack
	bool finishNextFrame_;
};

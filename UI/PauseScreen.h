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

#include <functional>
#include <memory>

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/MiscScreens.h"
#include "UI/Screen.h"

enum class PauseScreenMode {
	MAIN,
	DISPLAY_SETTINGS,
};

class GamePauseScreen : public UIDialogScreenWithGameBackground {
public:
	GamePauseScreen(const Path &filename);
	~GamePauseScreen();

	void dialogFinished(const Screen *dialog, DialogResult dr) override;
	bool key(const KeyInput &key) override;

	const char *tag() const override { return "GamePause"; }

protected:
	void CreateViews() override;
	void update() override;
	void CallbackDeleteConfig(bool yes);

private:
	void CreateSavestateControls(UI::LinearLayout *viewGroup, bool vertical);

	UI::EventReturn OnGameSettings(UI::EventParams &e);
	UI::EventReturn OnExitToMenu(UI::EventParams &e);
	UI::EventReturn OnReportFeedback(UI::EventParams &e);

	UI::EventReturn OnRewind(UI::EventParams &e);
	UI::EventReturn OnLoadUndo(UI::EventParams &e);
	UI::EventReturn OnLastSaveUndo(UI::EventParams &e);

	UI::EventReturn OnScreenshotClicked(UI::EventParams &e);

	UI::EventReturn OnCreateConfig(UI::EventParams &e);
	UI::EventReturn OnDeleteConfig(UI::EventParams &e);

	UI::EventReturn OnState(UI::EventParams &e);

	// hack
	bool finishNextFrame_ = false;
	DialogResult finishNextFrameResult_ = DR_CANCEL;
	PauseScreenMode mode_ = PauseScreenMode::MAIN;

	UI::Button *playButton_ = nullptr;
};

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

#include <string>
#include <vector>
#include <map>

#include "ui/screen.h"
#include "ui/ui.h"
#include "ui/viewgroup.h"
#include "ui/ui_screen.h"
#include "file/file_util.h"
#include "net/http_client.h"

// Uses the new UI framework through UIScreen for simplicity.
// Looks disappointingly like the old UI, right? Don't worry, it's just the theme.
// A redesign is coming. And note that you can control this new UI with your
// keyboard or Xbox controller.

class PluginScreen : public UIScreen {
public:
	PluginScreen();

	virtual void update(InputState &input);
	
protected:
	virtual void CreateViews();
	virtual void DrawBackground(UIContext &dc);

private:
	// Event handlers
	UI::EventReturn OnDownload(UI::EventParams &e);
	UI::EventReturn OnInformation(UI::EventParams &e);

	// Keeping track of downloads
	http::Downloader downloader_;
	std::shared_ptr<http::Download> json_;
	std::shared_ptr<http::Download> at3plusdecoder_;

	// UI widgets that need updating
	UI::TextView *tvDescription_;
	UI::Button *buttonBack_;
	UI::Button *buttonDownload_;
	UI::ProgressBar *progress_;

	std::string at3plusdecoderUrl_;
};
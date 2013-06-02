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
#include "file/file_util.h"
#include "net/http_client.h"


class UIScreen : public Screen {
public:
	UIScreen() : Screen(), root_(0), orientationChanged_(false) {}
	~UIScreen() { delete root_; }

	virtual void update(InputState &input);
	virtual void render();
	virtual void touch(const TouchInput &touch);

protected:
	virtual void CreateViews() = 0;

	UI::ViewGroup *root_;
	bool orientationChanged_;
};

class PluginScreen : public UIScreen {
public:
	PluginScreen();

	virtual void update(InputState &input);
	
protected:
	virtual void CreateViews();

private:
	UI::EventReturn OnDownload(UI::EventParams &e);
	UI::EventReturn OnInformation(UI::EventParams &e);

	http::Downloader downloader_;
	std::shared_ptr<http::Download> json_;
	std::shared_ptr<http::Download> at3plusdecoder_;

	// UI widgets that need updating
	UI::TextView *tvDescription_;
	UI::Button *buttonDownload_;
	UI::ProgressBar *progress_;
};
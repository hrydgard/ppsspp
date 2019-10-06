// Copyright (c) 2016- PPSSPP Project.

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

#include <thread>
#include <mutex>

#include "ui/ui_screen.h"
#include "ui/viewgroup.h"
#include "UI/MiscScreens.h"
#include "UI/MainScreen.h"

class RemoteISOScreen : public UIScreenWithBackground {
public:
	RemoteISOScreen();

protected:
	void update() override;
	void CreateViews() override;

	UI::EventReturn HandleStartServer(UI::EventParams &e);
	UI::EventReturn HandleStopServer(UI::EventParams &e);
	UI::EventReturn HandleBrowse(UI::EventParams &e);
	UI::EventReturn HandleSettings(UI::EventParams &e);

	bool serverRunning_;
	bool serverStopping_;
};

enum class ScanStatus {
	SCANNING,
	RETRY_SCAN,
	FOUND,
	FAILED,
	LOADING,
	LOADED,
};

class RemoteISOConnectScreen : public UIScreenWithBackground {
public:
	RemoteISOConnectScreen();
	~RemoteISOConnectScreen() override;

protected:
	void update() override;
	void CreateViews() override;

	ScanStatus GetStatus();
	void ExecuteScan();
	void ExecuteLoad();

	UI::TextView *statusView_;

	ScanStatus status_;
	double nextRetry_;
	std::thread *scanThread_;
	std::mutex statusLock_;
	std::string host_;
	int port_;
	std::vector<std::string> games_;
};

class RemoteISOBrowseScreen : public MainScreen {
public:
	RemoteISOBrowseScreen(const std::vector<std::string> &games);

protected:
	void CreateViews() override;

	std::vector<std::string> games_;
};

class RemoteISOSettingsScreen : public UIDialogScreenWithBackground {
public:
	RemoteISOSettingsScreen();

	UI::EventReturn OnClickRemoteISOSubdir(UI::EventParams &e);
	UI::EventReturn OnClickRemoteServer(UI::EventParams &e);
protected:

	void update() override;
	void CreateViews() override;

	UI::EventReturn OnChangeRemoteISOSubdir(UI::EventParams &e);

	bool serverRunning_ = false;
};

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

#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"
#include "UI/MiscScreens.h"
#include "UI/MainScreen.h"
#include "UI/TabbedDialogScreen.h"

class RemoteISOScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	RemoteISOScreen(const Path &filename);

	const char *tag() const override { return "RemoteISO"; }
	void CreateTabs() override;

protected:
	UI::EventReturn OnChangeRemoteISOSubdir(UI::EventParams &e);

	void CreateConnectTab(UI::ViewGroup *viewGroup);
	void CreateSettingsTab(UI::ViewGroup *viewGroup);
	bool ShowSearchControls() const override { return false; }

	void update() override;

	UI::EventReturn HandleStartServer(UI::EventParams &e);
	UI::EventReturn HandleStopServer(UI::EventParams &e);
	UI::EventReturn HandleBrowse(UI::EventParams &e);

	UI::TextView *firewallWarning_ = nullptr;
	bool serverRunning_ = false;
	bool serverStopping_ = false;
};

enum class ScanStatus {
	SCANNING,
	RETRY_SCAN,
	FOUND,
	FAILED,
	LOADING,
	LOADED,
};

class RemoteISOConnectScreen : public UIDialogScreenWithBackground {
public:
	RemoteISOConnectScreen();
	~RemoteISOConnectScreen();

	const char *tag() const override { return "RemoteISOConnect"; }

protected:
	void update() override;
	void CreateViews() override;

	ScanStatus GetStatus();
	void ExecuteScan();
	void ExecuteLoad();
	bool FindServer(std::string &resultHost, int &resultPort);

	UI::TextView *statusView_ = nullptr;

	ScanStatus status_ = ScanStatus::SCANNING;
	std::string statusMessage_;
	double nextRetry_ = 0.0;
	std::thread *scanThread_;
	std::mutex statusLock_;
	std::string host_;
	int port_ = -1;
	std::string url_;
	std::vector<Path> games_;
};

class RemoteISOBrowseScreen : public MainScreen {
public:
	RemoteISOBrowseScreen(const std::string &url, const std::vector<Path> &games)
		: url_(url), games_(games) {}

	const char *tag() const override { return "RemoteISOBrowse"; }

protected:
	void CreateViews() override;

	std::string url_;
	std::vector<Path> games_;
};

std::string RemoteSubdir();
std::string FormatRemoteISOUrl(const char *host, int port, const char *subdir);

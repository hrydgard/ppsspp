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

#include "i18n/i18n.h"
#include "net/http_server.h"
#include "net/resolve.h"
#include "net/sinks.h"
#include "thread/thread.h"
#include "thread/threadutil.h"
#include "Common/Common.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "UI/RemoteISOScreen.h"

using namespace UI;

enum class ServerStatus {
	STOPPED,
	STARTING,
	RUNNING,
	STOPPING,
};

static std::thread *serverThread = nullptr;
static ServerStatus serverStatus;
static recursive_mutex serverStatusLock;
static condition_variable serverStatusCond;

static void UpdateStatus(ServerStatus s) {
	lock_guard guard(serverStatusLock);
	serverStatus = s;
	serverStatusCond.notify_one();
}

static ServerStatus RetrieveStatus() {
	lock_guard guard(serverStatusLock);
	return serverStatus;
}

static void ExecuteServer() {
	setCurrentThreadName("HTTPServer");

	net::Init();
	auto http = new http::Server(new threading::SameThreadExecutor());

	http->Listen(0);
	// TODO: Report local IP and port.
	UpdateStatus(ServerStatus::RUNNING);

	while (RetrieveStatus() == ServerStatus::RUNNING) {
        http->RunSlice(5.0);
	}

	net::Shutdown();

	UpdateStatus(ServerStatus::STOPPED);
}

RemoteISOScreen::RemoteISOScreen() {
}

void RemoteISOScreen::CreateViews() {
	I18NCategory *rp = GetI18NCategory("Reporting");
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *sy = GetI18NCategory("System");

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	leftColumnItems->Add(new TextView(sy->T("RemoteISOWifi", "Note: Connect both devices to the same wifi"), new LinearLayoutParams(Margins(12, 5, 0, 5))));

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(rp->T("Browse Games")));
	if (serverStatus != ServerStatus::STOPPED) {
		rightColumnItems->Add(new Choice(rp->T("Stop Sharing")))->OnClick.Handle(this, &RemoteISOScreen::HandleStopServer);
	} else {
		rightColumnItems->Add(new Choice(rp->T("Share Games (Server)")))->OnClick.Handle(this, &RemoteISOScreen::HandleStartServer);
	}

	rightColumnItems->Add(new Spacer(25.0));
	rightColumnItems->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

UI::EventReturn RemoteISOScreen::HandleStartServer(UI::EventParams &e) {
	lock_guard guard(serverStatusLock);

	if (serverStatus != ServerStatus::STOPPED) {
		return EVENT_SKIPPED;
	}

	serverStatus = ServerStatus::STARTING;
	serverThread = new std::thread(&ExecuteServer);
	serverThread->detach();

	serverStatusCond.wait(serverStatusLock);
	RecreateViews();

	return EVENT_DONE;
}

UI::EventReturn RemoteISOScreen::HandleStopServer(UI::EventParams &e) {
	lock_guard guard(serverStatusLock);

	if (serverStatus != ServerStatus::RUNNING) {
		return EVENT_SKIPPED;
	}

	serverStatus = ServerStatus::STOPPING;
	serverStatusCond.wait(serverStatusLock);
	delete serverThread;
	serverThread = nullptr;

	RecreateViews();

	return EVENT_DONE;
}

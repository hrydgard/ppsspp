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

	std::map<std::string, std::string> paths;
	for (std::string filename : g_Config.recentIsos) {
#ifdef _WIN32
		static const std::string sep = "\\/";
#else
		static const std::string sep = "/";
#endif
		size_t basepos = filename.find_last_of(sep);
		std::string basename = "/" + (basepos == filename.npos ? filename : filename.substr(basepos + 1));

		// Let's not serve directories, since they won't work.  Only single files.
		// Maybe can do PBPs and other files later.  Would be neat to stream virtual disc filesystems.
		if (endsWithNoCase(basename, ".cso") || endsWithNoCase(basename, ".iso")) {
			paths[basename] = filename;
		}
	}

	auto handler = [&](const http::Request &request) {
		std::string filename = paths[request.resource()];
		s64 sz = File::GetFileSize(filename);

		std::string range;
		if (request.Method() == http::RequestHeader::HEAD) {
			request.WriteHttpResponseHeader(200, sz, "application/octet-stream", "Accept-Ranges: bytes\r\n");
		} else if (request.GetHeader("range", &range)) {
			s64 begin = 0, last = 0;
			if (sscanf(range.c_str(), "bytes=%lld-%lld", &begin, &last) != 2) {
				request.WriteHttpResponseHeader(400, -1, "text/plain");
				request.Out()->Push("Could not understand range request.");
				return;
			}

			if (begin < 0 || begin > last || last >= sz) {
				request.WriteHttpResponseHeader(416, -1, "text/plain");
				request.Out()->Push("Range goes outside of file.");
				return;
			}

			FILE *fp = File::OpenCFile(filename, "rb");
			if (!fp || fseek(fp, begin, SEEK_SET) != 0) {
				request.WriteHttpResponseHeader(500, -1, "text/plain");
				request.Out()->Push("File access failed.");
				if (fp) {
					fclose(fp);
				}
				return;
			}

			s64 len = last - begin + 1;
			char contentRange[1024];
			sprintf(contentRange, "Content-Range: bytes %lld-%lld/%lld\r\n", begin, last, sz);
			request.WriteHttpResponseHeader(206, len, "application/octet-stream", contentRange);

			const size_t CHUNK_SIZE = 16 * 1024;
			char *buf = new char[CHUNK_SIZE];
			for (s64 pos = 0; pos < len; pos += CHUNK_SIZE) {
				s64 chunklen = std::min(len - pos, (s64)CHUNK_SIZE);
				fread(buf, chunklen, 1, fp);
				request.Out()->Push(buf, chunklen);
			}
			fclose(fp);
			delete [] buf;
			request.Out()->Flush();
		} else {
			request.WriteHttpResponseHeader(418, -1, "text/plain");
			request.Out()->Push("This server only supports range requests.");
		}
	};

	for (auto pair : paths) {
		http->RegisterHandler(pair.first.c_str(), handler);
	}

	http->Listen(0);
	// TODO: Report local IP and port.
	UpdateStatus(ServerStatus::RUNNING);

	while (RetrieveStatus() == ServerStatus::RUNNING) {
		http->RunSlice(5.0);
	}

	net::Shutdown();

	UpdateStatus(ServerStatus::STOPPED);
}

RemoteISOScreen::RemoteISOScreen() : serverRunning_(false), serverStopping_(false) {
}

void RemoteISOScreen::update(InputState &input) {
	UIScreenWithBackground::update(input);

	bool nowRunning = RetrieveStatus() != ServerStatus::STOPPED;
	if (serverStopping_ && !nowRunning) {
		// Server stopped, delete the thread.
		delete serverThread;
		serverThread = nullptr;
		serverStopping_ = false;
	}

	if (serverRunning_ != nowRunning) {
		RecreateViews();
	}
	serverRunning_ = nowRunning;
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

	leftColumnItems->Add(new TextView(sy->T("RemoteISODesc", "Games in your recent list will be shared"), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	leftColumnItems->Add(new TextView(sy->T("RemoteISOWifi", "Note: Connect both devices to the same wifi"), new LinearLayoutParams(Margins(12, 5, 0, 5))));

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(rp->T("Browse Games")));
	ServerStatus status = RetrieveStatus();
	if (status == ServerStatus::STOPPING) {
		rightColumnItems->Add(new Choice(rp->T("Stopping..")))->SetDisabledPtr(&serverStopping_);
	} else if (status != ServerStatus::STOPPED) {
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

	return EVENT_DONE;
}

UI::EventReturn RemoteISOScreen::HandleStopServer(UI::EventParams &e) {
	lock_guard guard(serverStatusLock);

	if (serverStatus != ServerStatus::RUNNING) {
		return EVENT_SKIPPED;
	}

	serverStatus = ServerStatus::STOPPING;
	serverStopping_ = true;
	RecreateViews();

	return EVENT_DONE;
}

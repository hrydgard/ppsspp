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

#include "base/timeutil.h"
#include "ext/vjson/json.h"
#include "file/fd_util.h"
#include "i18n/i18n.h"
#include "net/http_client.h"
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

static const char *REPORT_HOSTNAME = "report.ppsspp.org";
static const int REPORT_PORT = 80;

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

static bool scanCancelled = false;

static void UpdateStatus(ServerStatus s) {
	lock_guard guard(serverStatusLock);
	serverStatus = s;
	serverStatusCond.notify_one();
}

static ServerStatus RetrieveStatus() {
	lock_guard guard(serverStatusLock);
	return serverStatus;
}

// This reports the local IP address to report.ppsspp.org, which can then
// relay that address to a mobile device searching for the server.
static void RegisterServer(int port) {
	http::Client http;
	Buffer theVoid;

	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT)) {
		if (http.Connect()) {
			char resource[1024] = {};
			std::string ip = fd_util::GetLocalIP(http.sock());
			snprintf(resource, sizeof(resource) - 1, "/match/update?local=%s&port=%d", ip.c_str(), port);

			http.GET(resource, &theVoid);
			http.Disconnect();
		}
	}
}

static void ExecuteServer() {
	setCurrentThreadName("HTTPServer");

	net::AutoInit netInit;
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
			paths[ReplaceAll(basename, " ", "%20")] = filename;
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

	if (!http->Listen(g_Config.iRemoteISOPort)) {
		if (!http->Listen(0)) {
			ERROR_LOG(COMMON, "Unable to listen on any port");
			UpdateStatus(ServerStatus::STOPPED);
			return;
		}
	}
	UpdateStatus(ServerStatus::RUNNING);

	g_Config.iRemoteISOPort = http->Port();
	RegisterServer(http->Port());
	double lastRegister = real_time_now();
	while (RetrieveStatus() == ServerStatus::RUNNING) {
		http->RunSlice(5.0);

		double now = real_time_now();
		if (now > lastRegister + 540.0) {
			RegisterServer(http->Port());
			lastRegister = now;
		}
	}

	http->Stop();

	UpdateStatus(ServerStatus::STOPPED);
}

static bool FindServer(std::string &resultHost, int &resultPort) {
	http::Client http;
	Buffer result;
	int code = 500;

	// Start by requesting a list of recent local ips for this network.
	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT)) {
		if (http.Connect()) {
			code = http.GET("/match/list", &result);
			http.Disconnect();
		}
	}

	if (code != 200 || scanCancelled) {
		return false;
	}

	std::string json;
	result.TakeAll(&json);

	JsonReader reader(json.c_str(), json.size());
	if (!reader.ok()) {
		return false;
	}

	const json_value *entries = reader.root();
	if (!entries) {
		return false;
	}

	std::vector<std::string> servers;
	const json_value *entry = entries->first_child;
	while (entry) {
		const char *host = entry->getString("ip", "");
		int port = entry->getInt("p", 0);

		char url[1024] = {};
		snprintf(url, sizeof(url), "http://%s:%d", host, port);
		servers.push_back(url);

		if (http.Resolve(host, port) && http.Connect()) {
			http.Disconnect();
			resultHost = host;
			resultPort = port;
			return true;
		}

		entry = entry->next_sibling;
	}

	// None of the local IPs were reachable.
	return false;
}

static bool LoadGameList(const std::string &host, int port, std::vector<std::string> &games) {
	http::Client http;
	Buffer result;
	int code = 500;

	// Start by requesting a list of recent local ips for this network.
	if (http.Resolve(host.c_str(), port)) {
		if (http.Connect()) {
			code = http.GET("/", &result);
			http.Disconnect();
		}
	}

	if (code != 200 || scanCancelled) {
		return false;
	}

	std::string listing;
	std::vector<std::string> items;
	result.TakeAll(&listing);

	SplitString(listing, '\n', items);
	for (const std::string &item : items) {
		if (!endsWithNoCase(item, ".cso") && !endsWithNoCase(item, ".iso") && !endsWithNoCase(item, ".pbp")) {
			continue;
		}

		char temp[1024] = {};
		snprintf(temp, sizeof(temp) - 1, "http://%s:%d%s", host.c_str(), port, item.c_str());
		games.push_back(temp);
	}

	return !games.empty();
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

	// TODO: Could display server address for manual entry.

	rightColumnItems->SetSpacing(0.0f);
	Choice *browseChoice = new Choice(sy->T("Browse Games"));
	rightColumnItems->Add(browseChoice)->OnClick.Handle(this, &RemoteISOScreen::HandleBrowse);
	ServerStatus status = RetrieveStatus();
	if (status == ServerStatus::STOPPING) {
		rightColumnItems->Add(new Choice(sy->T("Stopping..")))->SetDisabledPtr(&serverStopping_);
		browseChoice->SetEnabled(false);
	} else if (status != ServerStatus::STOPPED) {
		rightColumnItems->Add(new Choice(sy->T("Stop Sharing")))->OnClick.Handle(this, &RemoteISOScreen::HandleStopServer);
		browseChoice->SetEnabled(false);
	} else {
		rightColumnItems->Add(new Choice(sy->T("Share Games (Server)")))->OnClick.Handle(this, &RemoteISOScreen::HandleStartServer);
		browseChoice->SetEnabled(true);
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

UI::EventReturn RemoteISOScreen::HandleBrowse(UI::EventParams &e) {
	screenManager()->push(new RemoteISOConnectScreen());
	return EVENT_DONE;
}

RemoteISOConnectScreen::RemoteISOConnectScreen() : status_(ScanStatus::SCANNING), nextRetry_(0.0) {
	scanCancelled = false;
	statusLock_ = new recursive_mutex();

	scanThread_ = new std::thread([](RemoteISOConnectScreen *thiz) {
		thiz->ExecuteScan();
	}, this);
	scanThread_->detach();
}

RemoteISOConnectScreen::~RemoteISOConnectScreen() {
	int maxWait = 5000;
	scanCancelled = true;
	while (GetStatus() == ScanStatus::SCANNING || GetStatus() == ScanStatus::LOADING) {
		sleep_ms(1);
		if (--maxWait < 0) {
			// If it does ever wake up, it may crash... but better than hanging?
			break;
		}
	}
	delete scanThread_;
	delete statusLock_;
}

void RemoteISOConnectScreen::CreateViews() {
	I18NCategory *sy = GetI18NCategory("System");

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	statusView_ = leftColumnItems->Add(new TextView(sy->T("RemoteISOScanning", "Scanning... click Share Games on your desktop"), new LinearLayoutParams(Margins(12, 5, 0, 5))));

	// TODO: Here would be a good place for manual entry.

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(sy->T("Cancel"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

void RemoteISOConnectScreen::update(InputState &input) {
	I18NCategory *sy = GetI18NCategory("System");

	UIScreenWithBackground::update(input);

	ScanStatus s = GetStatus();
	switch (s) {
	case ScanStatus::SCANNING:
	case ScanStatus::LOADING:
		break;

	case ScanStatus::FOUND:
		statusView_->SetText(sy->T("RemoteISOLoading", "Connected - loading game list"));
		status_ = ScanStatus::LOADING;

		// Let's reuse scanThread_.
		delete scanThread_;
		scanThread_ = new std::thread([](RemoteISOConnectScreen *thiz) {
			thiz->ExecuteLoad();
		}, this);
		scanThread_->detach();
		break;

	case ScanStatus::FAILED:
		nextRetry_ = real_time_now() + 30.0;
		status_ = ScanStatus::RETRY_SCAN;
		break;

	case ScanStatus::RETRY_SCAN:
		if (nextRetry_ < real_time_now()) {
			status_ = ScanStatus::SCANNING;
			nextRetry_ = 0.0;

			delete scanThread_;
			scanThread_ = new std::thread([](RemoteISOConnectScreen *thiz) {
				thiz->ExecuteScan();
			}, this);
			scanThread_->detach();
		}
		break;

	case ScanStatus::LOADED:
		screenManager()->finishDialog(this, DR_OK);
		screenManager()->push(new RemoteISOBrowseScreen(games_));
		break;
	}
}

void RemoteISOConnectScreen::ExecuteScan() {
	FindServer(host_, port_);
	if (scanCancelled) {
		return;
	}

	lock_guard guard(*statusLock_);
	status_ = host_.empty() ? ScanStatus::FAILED : ScanStatus::FOUND;
}

ScanStatus RemoteISOConnectScreen::GetStatus() {
	lock_guard guard(*statusLock_);
	return status_;
}

void RemoteISOConnectScreen::ExecuteLoad() {
	bool result = LoadGameList(host_, port_, games_);
	if (scanCancelled) {
		return;
	}

	lock_guard guard(*statusLock_);
	status_ = result ? ScanStatus::LOADED : ScanStatus::FAILED;
}

class RemoteGameBrowser : public GameBrowser {
public:
	RemoteGameBrowser(const std::vector<std::string> &games, bool allowBrowsing, bool *gridStyle_, std::string lastText, std::string lastLink, int flags = 0, UI::LayoutParams *layoutParams = 0)
	: GameBrowser("!REMOTE", allowBrowsing, gridStyle_, lastText, lastLink, flags, layoutParams) {
		games_ = games;
		Refresh();
	}

protected:
	bool DisplayTopBar() override {
		return false;
	}

	bool HasSpecialFiles(std::vector<std::string> &filenames) override;

	std::vector<std::string> games_;
};

bool RemoteGameBrowser::HasSpecialFiles(std::vector<std::string> &filenames) {
	filenames = games_;
	return true;
}

RemoteISOBrowseScreen::RemoteISOBrowseScreen(const std::vector<std::string> &games) : games_(games) {
}

void RemoteISOBrowseScreen::CreateViews() {
	bool vertical = UseVerticalLayout();

	I18NCategory *mm = GetI18NCategory("MainMenu");
	I18NCategory *di = GetI18NCategory("Dialog");

	Margins actionMenuMargins(0, 10, 10, 0);

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	tabHolder_ = leftColumn;
	tabHolder_->SetTag("RemoteGames");
	gameBrowsers_.clear();

	leftColumn->SetClip(true);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollRecentGames->SetTag("RemoteGamesTab");
	RemoteGameBrowser *tabRemoteGames = new RemoteGameBrowser(
		games_, false, &g_Config.bGridView1, "", "", 0,
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	scrollRecentGames->Add(tabRemoteGames);
	gameBrowsers_.push_back(tabRemoteGames);

	leftColumn->AddTab(mm->T("Remote Server"), scrollRecentGames);
	tabRemoteGames->OnChoice.Handle<MainScreen>(this, &MainScreen::OnGameSelectedInstant);
	tabRemoteGames->OnHoldChoice.Handle<MainScreen>(this, &MainScreen::OnGameSelected);
	tabRemoteGames->OnHighlight.Handle<MainScreen>(this, &MainScreen::OnGameHighlight);

	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL);
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	rightColumnItems->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	if (vertical) {
		root_ = new LinearLayout(ORIENT_VERTICAL);
		rightColumn->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
		leftColumn->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0));
		root_->Add(rightColumn);
		root_->Add(leftColumn);
	} else {
		root_ = new LinearLayout(ORIENT_HORIZONTAL);
		leftColumn->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0));
		rightColumn->ReplaceLayoutParams(new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
		root_->Add(leftColumn);
		root_->Add(rightColumn);
	}

	root_->SetDefaultFocusView(tabHolder_);

	upgradeBar_ = 0;
}

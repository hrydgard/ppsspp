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

#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "base/timeutil.h"
#include "file/fd_util.h"
#include "i18n/i18n.h"
#include "json/json_reader.h"
#include "net/http_client.h"
#include "net/http_server.h"
#include "net/resolve.h"
#include "net/sinks.h"
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
static std::mutex serverStatusLock;
static std::condition_variable serverStatusCond;

static bool scanCancelled = false;
static bool scanAborted = false;

static void UpdateStatus(ServerStatus s) {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	serverStatus = s;
	serverStatusCond.notify_one();
}

static ServerStatus RetrieveStatus() {
	std::lock_guard<std::mutex> guard(serverStatusLock);
	return serverStatus;
}

// This reports the local IP address to report.ppsspp.org, which can then
// relay that address to a mobile device searching for the server.
static void RegisterServer(int port) {
	http::Client http;
	Buffer theVoid;

	char resource4[1024] = {};
	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT, net::DNSType::IPV4)) {
		if (http.Connect(2, 20.0, &scanCancelled)) {
			std::string ip = fd_util::GetLocalIP(http.sock());
			snprintf(resource4, sizeof(resource4) - 1, "/match/update?local=%s&port=%d", ip.c_str(), port);

			http.GET(resource4, &theVoid);
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}
	}

	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT, net::DNSType::IPV6)) {
		// We register both IPv4 and IPv6 in case the other client is using a different one.
		if (resource4[0] != 0 && http.Connect()) {
			http.GET(resource4, &theVoid);
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}

		// Currently, we're not using keepalive, so gotta reconnect...
		if (http.Connect()) {
			char resource6[1024] = {};
			std::string ip = fd_util::GetLocalIP(http.sock());
			snprintf(resource6, sizeof(resource6) - 1, "/match/update?local=%s&port=%d", ip.c_str(), port);

			http.GET(resource6, &theVoid);
			theVoid.Skip(theVoid.size());
			http.Disconnect();
		}
	}
}

static void ExecuteServer() {
	setCurrentThreadName("HTTPServer");

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
			ERROR_LOG(FILESYS, "Unable to listen on any port");
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

bool StartRemoteISOSharing() {
	std::lock_guard<std::mutex> guard(serverStatusLock);

	if (serverStatus != ServerStatus::STOPPED) {
		return false;
	}

	serverStatus = ServerStatus::STARTING;
	serverThread = new std::thread(&ExecuteServer);
	serverThread->detach();

	return true;
}

static bool FindServer(std::string &resultHost, int &resultPort) {
	http::Client http;
	Buffer result;
	int code = 500;

	auto TryServer = [&](const std::string &host, int port) {
		// Don't wait as long for a connect - we need a good connection for smooth streaming anyway.
		// This way if it's down, we'll find the right one faster.
		if (http.Resolve(host.c_str(), port) && http.Connect(1, 10.0, &scanCancelled)) {
			http.Disconnect();
			resultHost = host;
			resultPort = port;
			return true;
		}

		return false;
	};

	// Try last server first, if it is set
	if (g_Config.iLastRemoteISOPort && g_Config.sLastRemoteISOServer != "") {
		if (TryServer(g_Config.sLastRemoteISOServer.c_str(), g_Config.iLastRemoteISOPort)) {
			return true;
		}
	}

	// Don't scan if in manual mode.
	if (g_Config.bRemoteISOManual || scanCancelled) {
		return false;
	}

	// Start by requesting a list of recent local ips for this network.
	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT)) {
		if (http.Connect(2, 20.0, &scanCancelled)) {
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

	const JsonValue entries = reader.rootArray();
	if (entries.getTag() != JSON_ARRAY) {
		return false;
	}

	std::vector<std::string> servers;
	for (const auto pentry : entries) {
		JsonGet entry = pentry->value;
		if (scanCancelled)
			return false;

		const char *host = entry.getString("ip", "");
		int port = entry.getInt("p", 0);

		char url[1024] = {};
		snprintf(url, sizeof(url), "http://%s:%d", host, port);
		servers.push_back(url);

		if (TryServer(host, port)) {
			return true;
		}
	}

	// None of the local IPs were reachable.
	return false;
}

static bool LoadGameList(const std::string &host, int port, std::vector<std::string> &games) {
	http::Client http;
	Buffer result;
	int code = 500;
	std::vector<std::string> responseHeaders;
	std::string subdir ="/";
	size_t offset;

	if (g_Config.bRemoteISOManual) {
		subdir = g_Config.sRemoteISOSubdir;
		offset=subdir.find_last_of("/");
		if (offset != subdir.length() - 1 && offset != subdir.npos) {
			//truncate everything after last / 
			subdir.erase(offset + 1);
		}
	}

	// Start by requesting the list of games from the server.
	if (http.Resolve(host.c_str(), port)) {
		if (http.Connect(2, 20.0, &scanCancelled)) {
			code = http.GET(subdir.c_str(), &result,responseHeaders);
			http.Disconnect();
		}
	}

	if (code != 200 || scanCancelled) {
		return false;
	}

	std::string listing;
	std::vector<std::string> items;
	result.TakeAll(&listing);

	std::string contentType;
	for (const std::string &header : responseHeaders) {
		if (startsWithNoCase(header, "Content-Type:")) {
			contentType = header.substr(strlen("Content-Type:"));
			// Strip any whitespace (TODO: maybe move this to stringutil?)
			contentType.erase(0, contentType.find_first_not_of(" \t\r\n"));
			contentType.erase(contentType.find_last_not_of(" \t\r\n") + 1);
		}
	}

	// TODO: Technically, "TExt/hTml    ; chaRSet    =    Utf8" should pass, but "text/htmlese" should not.
	// But unlikely that'll be an issue.
	bool parseHtml = startsWithNoCase(contentType, "text/html");
	bool parseText = startsWithNoCase(contentType, "text/plain");

	if (parseText) {
		//ppsspp server
		SplitString(listing, '\n', items);
		for (const std::string &item : items) {
			if (!endsWithNoCase(item, ".cso") && !endsWithNoCase(item, ".iso") && !endsWithNoCase(item, ".pbp")) {
				continue;
			}

			char temp[1024] = {};
			snprintf(temp, sizeof(temp) - 1, "http://%s:%d%s", host.c_str(), port, item.c_str());
			games.push_back(temp);
		}
	} else if (parseHtml) {
		//other webserver
		GetQuotedStrings(listing, items);
		for (const std::string &item : items) {
			
			if (!endsWithNoCase(item, ".cso") && !endsWithNoCase(item, ".iso") && !endsWithNoCase(item, ".pbp")) {
				continue;
			}

			char temp[1024] = {};
			snprintf(temp, sizeof(temp) - 1, "http://%s:%d%s%s", host.c_str(), port, subdir.c_str(),item.c_str());
			games.push_back(temp);
		}
	} else {
		ERROR_LOG(FILESYS, "Unsupported Content-Type: %s", contentType.c_str());
		return false;
	}

	//save for next time unless manual is true
	if (!games.empty() && !g_Config.bRemoteISOManual){
		g_Config.sLastRemoteISOServer = host;
		g_Config.iLastRemoteISOPort = port;
	}

	return !games.empty();
}

RemoteISOScreen::RemoteISOScreen() : serverRunning_(false), serverStopping_(false) {
}

void RemoteISOScreen::update() {
	UIScreenWithBackground::update();

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
	I18NCategory *ri = GetI18NCategory("RemoteISO");

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	leftColumnItems->Add(new TextView(ri->T("RemoteISODesc", "Games in your recent list will be shared"), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	leftColumnItems->Add(new TextView(ri->T("RemoteISOWifi", "Note: Connect both devices to the same wifi"), new LinearLayoutParams(Margins(12, 5, 0, 5))));

	rightColumnItems->SetSpacing(0.0f);
	Choice *browseChoice = new Choice(ri->T("Browse Games"));
	rightColumnItems->Add(browseChoice)->OnClick.Handle(this, &RemoteISOScreen::HandleBrowse);
	ServerStatus status = RetrieveStatus();
	if (status == ServerStatus::STOPPING) {
		rightColumnItems->Add(new Choice(ri->T("Stopping..")))->SetDisabledPtr(&serverStopping_);
		browseChoice->SetEnabled(false);
	} else if (status != ServerStatus::STOPPED) {
		rightColumnItems->Add(new Choice(ri->T("Stop Sharing")))->OnClick.Handle(this, &RemoteISOScreen::HandleStopServer);
		browseChoice->SetEnabled(false);
	} else {
		rightColumnItems->Add(new Choice(ri->T("Share Games (Server)")))->OnClick.Handle(this, &RemoteISOScreen::HandleStartServer);
		browseChoice->SetEnabled(true);
	}
	Choice *settingsChoice = new Choice(ri->T("Settings"));
	rightColumnItems->Add(settingsChoice)->OnClick.Handle(this, &RemoteISOScreen::HandleSettings);

	LinearLayout *beforeBack = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT));
	beforeBack->Add(leftColumn);
	beforeBack->Add(rightColumn);
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(beforeBack);
	root_->Add(new Choice(di->T("Back"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

UI::EventReturn RemoteISOScreen::HandleStartServer(UI::EventParams &e) {
	if (!StartRemoteISOSharing()) {
		return EVENT_SKIPPED;
	}

	return EVENT_DONE;
}

UI::EventReturn RemoteISOScreen::HandleStopServer(UI::EventParams &e) {
	std::lock_guard<std::mutex> guard(serverStatusLock);

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

UI::EventReturn RemoteISOScreen::HandleSettings(UI::EventParams &e) {
	screenManager()->push(new RemoteISOSettingsScreen());
	return EVENT_DONE;
}

RemoteISOConnectScreen::RemoteISOConnectScreen() : status_(ScanStatus::SCANNING), nextRetry_(0.0) {
	scanCancelled = false;
	scanAborted = false;

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
			scanAborted = true;
			break;
		}
	}
	delete scanThread_;
}

void RemoteISOConnectScreen::CreateViews() {
	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ri = GetI18NCategory("RemoteISO");

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	statusView_ = leftColumnItems->Add(new TextView(ri->T("RemoteISOScanning", "Scanning... click Share Games on your desktop"), new LinearLayoutParams(Margins(12, 5, 0, 5))));

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(di->T("Cancel"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

void RemoteISOConnectScreen::update() {
	I18NCategory *ri = GetI18NCategory("RemoteISO");

	UIScreenWithBackground::update();

	ScanStatus s = GetStatus();
	switch (s) {
	case ScanStatus::SCANNING:
	case ScanStatus::LOADING:
		break;

	case ScanStatus::FOUND:
		statusView_->SetText(ri->T("RemoteISOLoading", "Connected - loading game list"));
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
		TriggerFinish(DR_OK);
		screenManager()->push(new RemoteISOBrowseScreen(games_));
		break;
	}
}

void RemoteISOConnectScreen::ExecuteScan() {
	FindServer(host_, port_);
	if (scanAborted) {
		return;
	}

	std::lock_guard<std::mutex> guard(statusLock_);
	status_ = host_.empty() ? ScanStatus::FAILED : ScanStatus::FOUND;
}

ScanStatus RemoteISOConnectScreen::GetStatus() {
	std::lock_guard<std::mutex> guard(statusLock_);
	return status_;
}

void RemoteISOConnectScreen::ExecuteLoad() {
	bool result = LoadGameList(host_, port_, games_);
	if (scanAborted) {
		return;
	}

	std::lock_guard<std::mutex> guard(statusLock_);
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

	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *ri = GetI18NCategory("RemoteISO");

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

	leftColumn->AddTab(ri->T("Remote Server"), scrollRecentGames);
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

RemoteISOSettingsScreen::RemoteISOSettingsScreen() {
	serverRunning_ = RetrieveStatus() != ServerStatus::STOPPED;
}

void RemoteISOSettingsScreen::update() {
	UIDialogScreenWithBackground::update();

	bool nowRunning = RetrieveStatus() != ServerStatus::STOPPED;
	if (serverRunning_ != nowRunning) {
		RecreateViews();
	}
	serverRunning_ = nowRunning;
}

void RemoteISOSettingsScreen::CreateViews() {
	I18NCategory *ri = GetI18NCategory("RemoteISO");
	
	ViewGroup *remoteisoSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	remoteisoSettingsScroll->SetTag("RemoteISOSettings");
	LinearLayout *remoteisoSettings = new LinearLayout(ORIENT_VERTICAL);
	remoteisoSettings->SetSpacing(0);
	remoteisoSettingsScroll->Add(remoteisoSettings);

	remoteisoSettings->Add(new ItemHeader(ri->T("Remote disc streaming")));
	remoteisoSettings->Add(new CheckBox(&g_Config.bRemoteShareOnStartup, ri->T("Share on PPSSPP startup")));
	remoteisoSettings->Add(new CheckBox(&g_Config.bRemoteISOManual, ri->T("Manual Mode Client", "Manually configure client")));
#if !defined(MOBILE_DEVICE)
	PopupTextInputChoice *remoteServer = remoteisoSettings->Add(new PopupTextInputChoice(&g_Config.sLastRemoteISOServer, ri->T("Remote Server"), "", 255, screenManager()));
#else
	ChoiceWithValueDisplay *remoteServer = new ChoiceWithValueDisplay(&g_Config.sLastRemoteISOServer, ri->T("Remote Server"), (const char *)nullptr);
	remoteisoSettings->Add(remoteServer);
	remoteServer->OnClick.Handle(this, &RemoteISOSettingsScreen::OnClickRemoteServer);
#endif
	remoteServer->SetEnabledPtr(&g_Config.bRemoteISOManual);
	PopupSliderChoice *remotePort = remoteisoSettings->Add(new PopupSliderChoice(&g_Config.iLastRemoteISOPort, 0, 65535, ri->T("Remote Port", "Remote Port"), 100, screenManager()));
	remotePort->SetEnabledPtr(&g_Config.bRemoteISOManual);
#if !defined(MOBILE_DEVICE)
	PopupTextInputChoice *remoteSubdir = remoteisoSettings->Add(new PopupTextInputChoice(&g_Config.sRemoteISOSubdir, ri->T("Remote Subdirectory"), "", 255, screenManager()));
	remoteSubdir->OnChange.Handle(this, &RemoteISOSettingsScreen::OnChangeRemoteISOSubdir);
#else
	ChoiceWithValueDisplay *remoteSubdir = remoteisoSettings->Add(
			new ChoiceWithValueDisplay(&g_Config.sRemoteISOSubdir, ri->T("Remote Subdirectory"), (const char *)nullptr));
	remoteSubdir->OnClick.Handle(this, &RemoteISOSettingsScreen::OnClickRemoteISOSubdir);
#endif
	remoteSubdir->SetEnabledPtr(&g_Config.bRemoteISOManual);

	PopupSliderChoice *portChoice = new PopupSliderChoice(&g_Config.iRemoteISOPort, 0, 65535, ri->T("Local Server Port", "Local Server Port"), 100, screenManager());
	remoteisoSettings->Add(portChoice);
	portChoice->SetDisabledPtr(&serverRunning_);
	remoteisoSettings->Add(new Spacer(25.0));
	
	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(remoteisoSettingsScroll);
	AddStandardBack(root_);
}

UI::EventReturn RemoteISOSettingsScreen::OnClickRemoteServer(UI::EventParams &e) {
	System_SendMessage("inputbox", ("remoteiso_server:" + g_Config.sLastRemoteISOServer).c_str());
	return UI::EVENT_DONE;
}

UI::EventReturn RemoteISOSettingsScreen::OnClickRemoteISOSubdir(UI::EventParams &e) {
	System_SendMessage("inputbox", ("remoteiso_subdir:" + g_Config.sRemoteISOSubdir).c_str());
	return UI::EVENT_DONE;
}

UI::EventReturn RemoteISOSettingsScreen::OnChangeRemoteISOSubdir(UI::EventParams &e) {
	//Conform to HTTP standards
	ReplaceAll(g_Config.sRemoteISOSubdir, " ", "%20");
	ReplaceAll(g_Config.sRemoteISOSubdir, "\\", "/");
	//Make sure it begins with /
	if (g_Config.sRemoteISOSubdir[0] != '/')
		g_Config.sRemoteISOSubdir = "/" + g_Config.sRemoteISOSubdir;
	
	return UI::EVENT_DONE;
}

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

#include "ppsspp_config.h"
#include <algorithm>
#include <thread>
#include <mutex>

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
#include "Common/CommonWindows.h"
#include <netfw.h>
#endif

// TODO: For text align flags, probably shouldn't be in gfx_es2/...
#include "Common/Render/DrawBuffer.h"
#include "Common/Net/HTTPClient.h"
#include "Common/Net/Resolve.h"
#include "Common/Net/URL.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/System/Request.h"

#include "Common/File/PathBrowser.h"
#include "Common/Data/Format/JSONReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/WebServer.h"
#include "UI/RemoteISOScreen.h"
#include "UI/OnScreenDisplay.h"

using namespace UI;

static const char *REPORT_HOSTNAME = "report.ppsspp.org";
static const int REPORT_PORT = 80;

static bool scanCancelled = false;
static bool scanAborted = false;

enum class ServerAllowStatus {
	NO,
	YES,
	UNKNOWN,
};

static ServerAllowStatus IsServerAllowed(int port) {
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	INetFwMgr *fwMgr = nullptr;
	HRESULT hr = CoCreateInstance(__uuidof(NetFwMgr), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwMgr), (void **)&fwMgr);
	if (FAILED(hr)) {
		return ServerAllowStatus::UNKNOWN;
	}

	std::wstring app;
	size_t sz;
	do {
		app.resize(app.size() + MAX_PATH);
		// On failure, this will return the same value as passed in, but success will always be one lower.
		sz = GetModuleFileName(nullptr, &app[0], (DWORD)app.size());
	} while (sz >= app.size());

	VARIANT allowedV, restrictedV;
	VariantInit(&allowedV);
	VariantInit(&restrictedV);
	hr = fwMgr->IsPortAllowed(&app[0], NET_FW_IP_VERSION_ANY, port, nullptr, NET_FW_IP_PROTOCOL_TCP, &allowedV, &restrictedV);
	fwMgr->Release();

	if (FAILED(hr)) {
		return ServerAllowStatus::UNKNOWN;
	}

	bool allowed = allowedV.vt == VT_BOOL && allowedV.boolVal != VARIANT_FALSE;
	bool restricted = restrictedV.vt == VT_BOOL && restrictedV.boolVal != VARIANT_FALSE;
	if (!allowed || restricted) {
		return ServerAllowStatus::NO;
	}
	return ServerAllowStatus::YES;
#else
	return ServerAllowStatus::UNKNOWN;
#endif
}

std::string RemoteSubdir() {
	if (g_Config.bRemoteISOManual) {
		return g_Config.sRemoteISOSubdir;
	}

	return "/";
}

bool RemoteISOConnectScreen::FindServer(std::string &resultHost, int &resultPort) {
	http::Client http;
	Buffer result;
	int code = 500;
	bool hadTimeouts = false;

	std::string subdir = RemoteSubdir();

	auto ri = GetI18NCategory(I18NCat::REMOTEISO);
	auto SetStatus = [&](const std::string &key, const std::string &host, int port) {
		std::string formatted = ReplaceAll(ri->T(key), "[URL]", StringFromFormat("http://%s:%d/", host.c_str(), port));

		std::lock_guard<std::mutex> guard(statusLock_);
		statusMessage_ = formatted;
		INFO_LOG(Log::System, "Remote: %s", formatted.c_str());
	};

	http.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));

	auto TryServer = [&](const std::string &host, int port) {
		SetStatus("Resolving [URL]...", host, port);
		if (!http.Resolve(host.c_str(), port)) {
			SetStatus("Could not resolve [URL]", host, port);
			return false;
		}

		SetStatus("Connecting to [URL]...", host, port);
		// Don't wait as long for a connect - we need a good connection for smooth streaming anyway.
		// This way if it's down, we'll find the right one faster.
		if (!http.Connect(1, 10.0, &scanCancelled)) {
			hadTimeouts = true;
			SetStatus("Could not connect to [URL]", host, port);
			return false;
		}

		SetStatus("Loading game list from [URL]...", host, port);
		net::RequestProgress progress(&scanCancelled);
		code = http.GET(http::RequestParams(subdir.c_str()), &result, &progress);
		http.Disconnect();

		if (code != 200) {
			if (code < 0) {
				hadTimeouts = true;
			}
			SetStatus("Game list failed from [URL]", host, port);
			return false;
		}

		// Make sure this isn't just the debugger.  If so, move on.
		std::string listing;
		std::vector<std::string> items;
		result.TakeAll(&listing);
		SplitString(listing, '\n', items);

		bool supported = false;
		for (const std::string &item : items) {
			if (item.empty())
				continue;
			if (!RemoteISOFileSupported(item)) {
				if (item.back() != '/') {
					// We accept lists of just directories - we kinda have to.
					continue;
				}
			}
			supported = true;
			break;
		}

		if (supported) {
			resultHost = host;
			resultPort = port;
			SetStatus("Connected to [URL]", host, port);
			NOTICE_LOG(Log::System, "RemoteISO found: %s : %d", host.c_str(), port);
			return true;
		}

		return false;
	};

	// Try last server first, if it is set
	if (g_Config.iLastRemoteISOPort && !g_Config.sLastRemoteISOServer.empty()) {
		if (TryServer(g_Config.sLastRemoteISOServer.c_str(), g_Config.iLastRemoteISOPort)) {
			return true;
		}
	}

	// Don't scan if in manual mode.
	if (g_Config.bRemoteISOManual || scanCancelled) {
		return false;
	}

	// Start by requesting a list of recent local ips for this network.
	SetStatus("Looking for peers...", "", 0);
	if (http.Resolve(REPORT_HOSTNAME, REPORT_PORT)) {
		if (http.Connect(2, 20.0, &scanCancelled)) {
			net::RequestProgress progress(&scanCancelled);
			code = http.GET(http::RequestParams("/match/list"), &result, &progress);
			http.Disconnect();
		}
	}

	if (code != 200 || scanCancelled) {
		if (!scanCancelled) {
			SetStatus("Could not load peers, retrying soon...", "", 0);
		}
		return false;
	}

	std::string json;
	result.TakeAll(&json);

	using namespace json;

	JsonReader reader(json.c_str(), json.size());
	if (!reader.ok()) {
		SetStatus("Could not load peers, retrying soon...", "", 0);
		return false;
	}

	const JsonValue entries = reader.rootArray();
	if (entries.getTag() != JSON_ARRAY) {
		SetStatus("Could not load peers, retrying soon...", "", 0);
		return false;
	}

	for (const auto pentry : entries) {
		JsonGet entry = pentry->value;
		if (scanCancelled)
			return false;

		const char *host = entry.getStringOr("ip", "");
		int port = entry.getInt("p", 0);

		if (TryServer(host, port)) {
			return true;
		}
	}

	// None of the local IPs were reachable.  We'll retry again.
	std::lock_guard<std::mutex> guard(statusLock_);
	if (hadTimeouts) {
		statusMessage_ = ri->T("RemoteISOScanningTimeout", "Scanning... check your desktop's firewall settings");
	} else {
		statusMessage_ = ri->T("RemoteISOScanning", "Scanning... click Share Games on your desktop");
	}
	return false;
}

static bool LoadGameList(const Path &url, std::vector<Path> &games) {
	PathBrowser browser;
	browser.SetPath(url);
	std::vector<File::FileInfo> files;
	browser.SetUserAgent(StringFromFormat("PPSSPP/%s", PPSSPP_GIT_VERSION));
	browser.SetRootAlias("ms:", GetSysDirectory(DIRECTORY_MEMSTICK_ROOT));
	browser.GetListing(files, "iso:cso:chd:pbp:elf:prx:ppdmp:", &scanCancelled);
	if (scanCancelled) {
		return false;
	}
	for (auto &file : files) {
		if (file.isDirectory || RemoteISOFileSupported(file.name)) {
			games.push_back(file.fullName);
		}
	}

	return !games.empty();
}

RemoteISOScreen::RemoteISOScreen(const Path &filename) : TabbedUIDialogScreenWithGameBackground(filename) {}


void RemoteISOScreen::CreateTabs() {
	auto ri = GetI18NCategory(I18NCat::REMOTEISO);

	UI::LinearLayout *connect = AddTab("Connect", ri->T("Connect"));
	connect->SetSpacing(5.0f);
	CreateConnectTab(connect);

	UI::LinearLayout *settings = AddTab("Settings", ri->T("Settings"));
	CreateSettingsTab(settings);
}

void RemoteISOScreen::update() {
	TabbedUIDialogScreenWithGameBackground::update();

	if (!WebServerStopped(WebServerFlags::DISCS)) {
		auto result = IsServerAllowed(g_Config.iRemoteISOPort);
		if (result == ServerAllowStatus::NO) {
			firewallWarning_->SetVisibility(V_VISIBLE);
		} else if (result == ServerAllowStatus::YES) {
			firewallWarning_->SetVisibility(V_GONE);
		}
	}

	bool nowRunning = !WebServerStopped(WebServerFlags::DISCS);
	if (serverStopping_ && !nowRunning) {
		serverStopping_ = false;
	}

	if (serverRunning_ != nowRunning) {
		RecreateViews();
	}
	serverRunning_ = nowRunning;
}

void RemoteISOScreen::CreateConnectTab(UI::ViewGroup *tab) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ri = GetI18NCategory(I18NCat::REMOTEISO);

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);

	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	if (serverRunning_) {
		leftColumnItems->Add(new NoticeView(NoticeLevel::SUCCESS, ri->T("Currently sharing"), "", new LinearLayoutParams(Margins(12, 5, 0, 5))));
	} else {
		leftColumnItems->Add(new NoticeView(NoticeLevel::INFO, ri->T("Not currently sharing"), "", new LinearLayoutParams(Margins(12, 5, 0, 5))));
	}

	if ((RemoteISOShareType)g_Config.iRemoteISOShareType == RemoteISOShareType::RECENT) {
		leftColumnItems->Add(new TextView(ri->T("RemoteISODesc", "Games in your recent list will be shared"), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	} else {
		leftColumnItems->Add(new TextView(std::string(ri->T("Share Games (Server)")) + ": " + Path(g_Config.sRemoteISOSharedDir).ToVisualString(), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	}
	leftColumnItems->Add(new TextView(ri->T("RemoteISOWifi", "Note: Connect both devices to the same wifi"), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	firewallWarning_ = leftColumnItems->Add(new TextView(ri->T("RemoteISOWinFirewall", "WARNING: Windows Firewall is blocking sharing"), new LinearLayoutParams(Margins(12, 5, 0, 5))));
	firewallWarning_->SetTextColor(0xFF0000FF);
	firewallWarning_->SetVisibility(V_GONE);

	rightColumnItems->SetSpacing(0.0f);
	Choice *browseChoice = new Choice(ri->T("Browse Games"));
	rightColumnItems->Add(browseChoice)->OnClick.Handle(this, &RemoteISOScreen::HandleBrowse);
	if (WebServerStopping(WebServerFlags::DISCS)) {
		rightColumnItems->Add(new Choice(ri->T("Stopping..")))->SetDisabledPtr(&serverStopping_);
		browseChoice->SetEnabled(false);
	} else if (!WebServerStopped(WebServerFlags::DISCS)) {
		rightColumnItems->Add(new Choice(ri->T("Stop Sharing")))->OnClick.Handle(this, &RemoteISOScreen::HandleStopServer);
		browseChoice->SetEnabled(false);
	} else {
		rightColumnItems->Add(new Choice(ri->T("Share Games (Server)")))->OnClick.Handle(this, &RemoteISOScreen::HandleStartServer);
		browseChoice->SetEnabled(true);
	}

	LinearLayout *beforeBack = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	beforeBack->Add(leftColumn);
	beforeBack->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
	tab->Add(beforeBack);
}

void RemoteISOScreen::CreateSettingsTab(UI::ViewGroup *remoteisoSettings) {
	serverRunning_ = !WebServerStopped(WebServerFlags::DISCS);

	auto ri = GetI18NCategory(I18NCat::REMOTEISO);

	remoteisoSettings->Add(new ItemHeader(ri->T("Remote disc streaming")));
	remoteisoSettings->Add(new CheckBox(&g_Config.bRemoteShareOnStartup, ri->T("Share on PPSSPP startup")));
	remoteisoSettings->Add(new CheckBox(&g_Config.bRemoteISOManual, ri->T("Manual Mode Client", "Manually configure client")));
	remoteisoSettings->Add(new CheckBox(&g_Config.bRemoteTab, ri->T("Show Remote tab on main screen")));

	if (System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER)) {
		static const char *shareTypes[] = { "Recent files", "Choose directory" };
		remoteisoSettings->Add(new PopupMultiChoice(&g_Config.iRemoteISOShareType, ri->T("Files to share"), shareTypes, 0, ARRAY_SIZE(shareTypes), I18NCat::REMOTEISO, screenManager()));
		FolderChooserChoice *folderChooser = remoteisoSettings->Add(new FolderChooserChoice(GetRequesterToken(), &g_Config.sRemoteISOSharedDir, ri->T("Files to share")));
		folderChooser->SetEnabledFunc([=]() {
			return g_Config.iRemoteISOShareType == (int)RemoteISOShareType::LOCAL_FOLDER;
		});
	} else {
		// Can't pick a folder, only allow sharing recent stuff.
		g_Config.iRemoteISOShareType = (int)RemoteISOShareType::RECENT;
	}

	UI::Choice *remoteServer = new PopupTextInputChoice(GetRequesterToken(), &g_Config.sLastRemoteISOServer, ri->T("Remote Server"), "", 255, screenManager());
	remoteisoSettings->Add(remoteServer);
	remoteServer->SetEnabledPtr(&g_Config.bRemoteISOManual);

	PopupSliderChoice *remotePort = remoteisoSettings->Add(new PopupSliderChoice(&g_Config.iLastRemoteISOPort, 0, 65535, 0, ri->T("Remote Port"), 100, screenManager()));
	remotePort->SetEnabledPtr(&g_Config.bRemoteISOManual);

	UI::Choice *remoteSubdir;
	{
		PopupTextInputChoice *remoteSubdirInput = new PopupTextInputChoice(GetRequesterToken(), &g_Config.sRemoteISOSubdir, ri->T("Remote Subdirectory"), "", 255, screenManager());
		remoteSubdirInput->OnChange.Handle(this, &RemoteISOScreen::OnChangeRemoteISOSubdir);
		remoteSubdir = remoteSubdirInput;
	}
	remoteisoSettings->Add(remoteSubdir);
	remoteSubdir->SetEnabledPtr(&g_Config.bRemoteISOManual);

	PopupSliderChoice *portChoice = new PopupSliderChoice(&g_Config.iRemoteISOPort, 0, 65535, 0, ri->T("Local Server Port", "Local Server Port"), 100, screenManager());
	remoteisoSettings->Add(portChoice);
	portChoice->SetDisabledPtr(&serverRunning_);
}

static void CleanupRemoteISOSubdir() {
	// Replace spaces and force forward slashes.
	// TODO: Maybe we should uri escape this after?
	ReplaceAll(g_Config.sRemoteISOSubdir, " ", "%20");
	ReplaceAll(g_Config.sRemoteISOSubdir, "\\", "/");
	// Make sure it begins with /.
	if (g_Config.sRemoteISOSubdir.empty() || g_Config.sRemoteISOSubdir[0] != '/')
		g_Config.sRemoteISOSubdir = "/" + g_Config.sRemoteISOSubdir;
}


UI::EventReturn RemoteISOScreen::OnChangeRemoteISOSubdir(UI::EventParams &e) {
	CleanupRemoteISOSubdir();
	return UI::EVENT_DONE;
}

UI::EventReturn RemoteISOScreen::HandleStartServer(UI::EventParams &e) {
	if (!StartWebServer(WebServerFlags::DISCS)) {
		return EVENT_SKIPPED;
	}

	return EVENT_DONE;
}

UI::EventReturn RemoteISOScreen::HandleStopServer(UI::EventParams &e) {
	if (!StopWebServer(WebServerFlags::DISCS)) {
		return EVENT_SKIPPED;
	}

	serverStopping_ = true;
	RecreateViews();

	return EVENT_DONE;
}

UI::EventReturn RemoteISOScreen::HandleBrowse(UI::EventParams &e) {
	screenManager()->push(new RemoteISOConnectScreen());
	return EVENT_DONE;
}

RemoteISOConnectScreen::RemoteISOConnectScreen() {
	scanCancelled = false;
	scanAborted = false;

	scanThread_ = new std::thread([](RemoteISOConnectScreen *thiz) {
		SetCurrentThreadName("RemoteISOScan");
		thiz->ExecuteScan();
	}, this);
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
	if (scanThread_->joinable())
		scanThread_->join();
	delete scanThread_;
}

void RemoteISOConnectScreen::CreateViews() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ri = GetI18NCategory(I18NCat::REMOTEISO);

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	statusView_ = leftColumnItems->Add(new TextView(ri->T("RemoteISOScanning", "Scanning... click Share Games on your desktop"), FLAG_WRAP_TEXT, false, new LinearLayoutParams(Margins(12, 5, 0, 5))));

	rightColumnItems->SetSpacing(0.0f);
	rightColumnItems->Add(new Choice(di->T("Cancel"), "", false, new AnchorLayoutParams(150, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	root_ = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(leftColumn);
	root_->Add(rightColumn);

	leftColumn->Add(leftColumnItems);
	rightColumn->Add(rightColumnItems);
}

void RemoteISOConnectScreen::update() {
	auto ri = GetI18NCategory(I18NCat::REMOTEISO);

	UIDialogScreenWithBackground::update();

	ScanStatus s = GetStatus();
	switch (s) {
	case ScanStatus::SCANNING:
	case ScanStatus::LOADING:
		break;

	case ScanStatus::FOUND:
		statusView_->SetText(ri->T("RemoteISOLoading", "Connected - loading game list"));
		status_ = ScanStatus::LOADING;

		// Let's reuse scanThread_.
		if (scanThread_->joinable())
			scanThread_->join();
		delete scanThread_;
		statusMessage_.clear();
		scanThread_ = new std::thread([](RemoteISOConnectScreen *thiz) {
			thiz->ExecuteLoad();
		}, this);
		break;

	case ScanStatus::FAILED:
		nextRetry_ = time_now_d() + 15.0;
		status_ = ScanStatus::RETRY_SCAN;
		break;

	case ScanStatus::RETRY_SCAN:
		if (nextRetry_ < time_now_d()) {
			status_ = ScanStatus::SCANNING;
			nextRetry_ = 0.0;

			if (scanThread_->joinable())
				scanThread_->join();
			delete scanThread_;
			statusMessage_.clear();
			scanThread_ = new std::thread([](RemoteISOConnectScreen *thiz) {
				thiz->ExecuteScan();
			}, this);
		}
		break;

	case ScanStatus::LOADED:
		TriggerFinish(DR_OK);
		screenManager()->push(new RemoteISOBrowseScreen(url_, games_));
		break;
	}

	std::lock_guard<std::mutex> guard(statusLock_);
	if (!statusMessage_.empty()) {
		statusView_->SetText(statusMessage_);
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

std::string FormatRemoteISOUrl(const char *host, int port, const char *subdir) {
	return StringFromFormat("http://%s:%d%s", host, port, subdir);
}

void RemoteISOConnectScreen::ExecuteLoad() {
	std::string subdir = RemoteSubdir();
	url_ = FormatRemoteISOUrl(host_.c_str(), port_, subdir.c_str());
	bool result = LoadGameList(Path(url_), games_);
	if (scanAborted) {
		return;
	}

	if (result && !games_.empty() && !g_Config.bRemoteISOManual) {
		g_Config.sLastRemoteISOServer = host_;
		g_Config.iLastRemoteISOPort = port_;
	}

	std::lock_guard<std::mutex> guard(statusLock_);
	status_ = result ? ScanStatus::LOADED : ScanStatus::FAILED;
}

void RemoteISOBrowseScreen::CreateViews() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ri = GetI18NCategory(I18NCat::REMOTEISO);

	bool vertical = UseVerticalLayout();

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	tabHolder_ = leftColumn;
	tabHolder_->SetTag("RemoteGames");
	gameBrowsers_.clear();

	leftColumn->SetClip(true);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollRecentGames->SetTag("RemoteGamesTab");
	GameBrowser *tabRemoteGames = new GameBrowser(GetRequesterToken(),
		Path(url_), BrowseFlags::NAVIGATE, &g_Config.bGridView1, screenManager(), "", "",
		new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	tabRemoteGames->SetHomePath(Path(url_));

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
		Margins actionMenuMargins(0, 10, 10, 0);
		root_ = new LinearLayout(ORIENT_HORIZONTAL);
		leftColumn->ReplaceLayoutParams(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 1.0));
		rightColumn->ReplaceLayoutParams(new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
		root_->Add(leftColumn);
		root_->Add(rightColumn);
	}

	root_->SetDefaultFocusView(tabHolder_);

	upgradeBar_ = 0;
}

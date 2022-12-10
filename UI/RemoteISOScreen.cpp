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

#include "Common/File/PathBrowser.h"
#include "Common/Data/Format/JSONReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/WebServer.h"
#include "UI/RemoteISOScreen.h"

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

static std::string RemoteSubdir() {
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

	auto ri = GetI18NCategory("RemoteISO");
	auto SetStatus = [&](const std::string &key, const std::string &host, int port) {
		std::string formatted = ReplaceAll(ri->T(key), "[URL]", StringFromFormat("http://%s:%d/", host.c_str(), port));

		std::lock_guard<std::mutex> guard(statusLock_);
		statusMessage_ = formatted;
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
		http::RequestProgress progress(&scanCancelled);
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
			if (!RemoteISOFileSupported(item)) {
				continue;
			}
			supported = true;
			break;
		}

		if (supported) {
			resultHost = host;
			resultPort = port;
			SetStatus("Connected to [URL]", host, port);
			NOTICE_LOG(SYSTEM, "RemoteISO found: %s : %d", host.c_str(), port);
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
			http::RequestProgress progress(&scanCancelled);
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

		const char *host = entry.getString("ip", "");
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
	PathBrowser browser(url);
	std::vector<File::FileInfo> files;
	browser.GetListing(files, "iso:cso:pbp:elf:prx:ppdmp:", &scanCancelled);
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

RemoteISOScreen::RemoteISOScreen() {
}

void RemoteISOScreen::update() {
	UIScreenWithBackground::update();

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

void RemoteISOScreen::CreateViews() {
	auto di = GetI18NCategory("Dialog");
	auto ri = GetI18NCategory("RemoteISO");

	Margins actionMenuMargins(0, 20, 15, 0);
	Margins contentMargins(0, 20, 5, 5);
	ViewGroup *leftColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 0.4f, contentMargins));
	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(WRAP_CONTENT, FILL_PARENT));
	ViewGroup *rightColumn = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(300, FILL_PARENT, actionMenuMargins));
	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);

	leftColumnItems->Add(new TextView(ri->T("RemoteISODesc", "Games in your recent list will be shared"), new LinearLayoutParams(Margins(12, 5, 0, 5))));
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

UI::EventReturn RemoteISOScreen::HandleSettings(UI::EventParams &e) {
	screenManager()->push(new RemoteISOSettingsScreen());
	return EVENT_DONE;
}

RemoteISOConnectScreen::RemoteISOConnectScreen() {
	scanCancelled = false;
	scanAborted = false;

	scanThread_ = new std::thread([](RemoteISOConnectScreen *thiz) {
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
	auto di = GetI18NCategory("Dialog");
	auto ri = GetI18NCategory("RemoteISO");

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
	auto ri = GetI18NCategory("RemoteISO");

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

void RemoteISOConnectScreen::ExecuteLoad() {
	std::string subdir = RemoteSubdir();
	url_ = StringFromFormat("http://%s:%d%s", host_.c_str(), port_, subdir.c_str());
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

class RemoteGameBrowser : public GameBrowser {
public:
	RemoteGameBrowser(const Path &url, BrowseFlags browseFlags, bool *gridStyle_, ScreenManager *screenManager, std::string lastText, std::string lastLink, UI::LayoutParams *layoutParams = nullptr)
		: GameBrowser(url, browseFlags, gridStyle_, screenManager, lastText, lastLink, layoutParams) {
		initialPath_ = url;
	}

protected:
	Path HomePath() override {
		return initialPath_;
	}

	Path initialPath_;
};

RemoteISOBrowseScreen::RemoteISOBrowseScreen(const std::string &url, const std::vector<Path> &games)
	: url_(url), games_(games) {
}

void RemoteISOBrowseScreen::CreateViews() {
	auto di = GetI18NCategory("Dialog");
	auto ri = GetI18NCategory("RemoteISO");

	bool vertical = UseVerticalLayout();

	Margins actionMenuMargins(0, 10, 10, 0);

	TabHolder *leftColumn = new TabHolder(ORIENT_HORIZONTAL, 64, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	tabHolder_ = leftColumn;
	tabHolder_->SetTag("RemoteGames");
	gameBrowsers_.clear();

	leftColumn->SetClip(true);

	ScrollView *scrollRecentGames = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	scrollRecentGames->SetTag("RemoteGamesTab");
	GameBrowser *tabRemoteGames = new RemoteGameBrowser(
		Path(url_), BrowseFlags::PIN | BrowseFlags::NAVIGATE, &g_Config.bGridView1, screenManager(), "", "",
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
	serverRunning_ = !WebServerStopped(WebServerFlags::DISCS);
}

void RemoteISOSettingsScreen::update() {
	UIDialogScreenWithBackground::update();

	bool nowRunning = !WebServerStopped(WebServerFlags::DISCS);
	if (serverRunning_ != nowRunning) {
		RecreateViews();
	}
	serverRunning_ = nowRunning;
}

void RemoteISOSettingsScreen::CreateViews() {
	auto ri = GetI18NCategory("RemoteISO");

	ViewGroup *remoteisoSettingsScroll = new ScrollView(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	remoteisoSettingsScroll->SetTag("RemoteISOSettings");
	LinearLayout *remoteisoSettings = new LinearLayoutList(ORIENT_VERTICAL);
	remoteisoSettings->SetSpacing(0);
	remoteisoSettingsScroll->Add(remoteisoSettings);

	remoteisoSettings->Add(new ItemHeader(ri->T("Remote disc streaming")));
	remoteisoSettings->Add(new CheckBox(&g_Config.bRemoteShareOnStartup, ri->T("Share on PPSSPP startup")));
	remoteisoSettings->Add(new CheckBox(&g_Config.bRemoteISOManual, ri->T("Manual Mode Client", "Manually configure client")));

	UI::Choice *remoteServer;
#if defined(MOBILE_DEVICE)
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD)) {
		remoteServer = new ChoiceWithValueDisplay(&g_Config.sLastRemoteISOServer, ri->T("Remote Server"), (const char *)nullptr);
		remoteServer->OnClick.Handle(this, &RemoteISOSettingsScreen::OnClickRemoteServer);
	} else
#endif
	remoteServer = new PopupTextInputChoice(&g_Config.sLastRemoteISOServer, ri->T("Remote Server"), "", 255, screenManager());
	remoteisoSettings->Add(remoteServer);
	remoteServer->SetEnabledPtr(&g_Config.bRemoteISOManual);

	PopupSliderChoice *remotePort = remoteisoSettings->Add(new PopupSliderChoice(&g_Config.iLastRemoteISOPort, 0, 65535, ri->T("Remote Port", "Remote Port"), 100, screenManager()));
	remotePort->SetEnabledPtr(&g_Config.bRemoteISOManual);

	UI::Choice *remoteSubdir;
#if defined(MOBILE_DEVICE)
	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD)) {
		remoteSubdir = new ChoiceWithValueDisplay(&g_Config.sRemoteISOSubdir, ri->T("Remote Subdirectory"), (const char *)nullptr);
		remoteSubdir->OnClick.Handle(this, &RemoteISOSettingsScreen::OnClickRemoteISOSubdir);
	} else
#endif
	{
		PopupTextInputChoice *remoteSubdirInput = new PopupTextInputChoice(&g_Config.sRemoteISOSubdir, ri->T("Remote Subdirectory"), "", 255, screenManager());
		remoteSubdirInput->OnChange.Handle(this, &RemoteISOSettingsScreen::OnChangeRemoteISOSubdir);
		remoteSubdir = remoteSubdirInput;
	}
	remoteisoSettings->Add(remoteSubdir);
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
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(__ANDROID__)
	auto ri = GetI18NCategory("RemoteISO");
	System_InputBoxGetString(ri->T("Remote Server"), g_Config.sLastRemoteISOServer, [](bool result, const std::string &value) {
		g_Config.sLastRemoteISOServer = value;
	});
#endif
	return UI::EVENT_DONE;
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

UI::EventReturn RemoteISOSettingsScreen::OnClickRemoteISOSubdir(UI::EventParams &e) {
#if PPSSPP_PLATFORM(WINDOWS) || defined(USING_QT_UI) || defined(__ANDROID__)
	auto ri = GetI18NCategory("RemoteISO");
	System_InputBoxGetString(ri->T("Remote Subdirectory"), g_Config.sRemoteISOSubdir, [](bool result, const std::string &value) {
		g_Config.sRemoteISOSubdir = value;
		// Apply the cleanup logic, too.
		CleanupRemoteISOSubdir();
	});
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn RemoteISOSettingsScreen::OnChangeRemoteISOSubdir(UI::EventParams &e) {
	CleanupRemoteISOSubdir();
	return UI::EVENT_DONE;
}

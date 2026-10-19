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

#include <memory>

#include "Common/Data/Format/JSONReader.h"
#include "Common/File/FileDescriptor.h"
#include "Common/File/FileUtil.h"
#include "Core/Debugger/WebSocket/GameSubscriber.h"
// Keep the debugger header before winsock's OPTIONAL macro.
#include "Common/Net/Sinks.h"
#include "Common/Net/SocketCompat.h"
#include "Common/Net/WebsocketServer.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNetApctl.h"
#include "Core/RetroAchievements.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "ext/rcheevos/src/rc_client_internal.h"
#include "unittest/UnitTest.h"

namespace {

// Exercise the production response writer and WebSocket framing over a local socket.
// The registered handler is called directly, so this does not test the HTTP upgrade or dispatcher.
class TestWebSocket : public net::WebSocketServer {
public:
	TestWebSocket(SOCKET fd, net::OutputSink *output) : WebSocketServer(fd, nullptr, output) {}
};

class ResponseConnection {
public:
	ResponseConnection() {
#if PPSSPP_PLATFORM(WINDOWS)
		WSADATA data{};
		winsockStarted_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
	}

	~ResponseConnection() {
		subscriber_.reset();
		if (server_ != INVALID_SOCKET) closesocket(server_);
		if (client_ != INVALID_SOCKET) closesocket(client_);
		if (listener_ != INVALID_SOCKET) closesocket(listener_);
#if PPSSPP_PLATFORM(WINDOWS)
		if (winsockStarted_) WSACleanup();
#endif
	}

	bool Open() {
#if PPSSPP_PLATFORM(WINDOWS)
		EXPECT_TRUE(winsockStarted_);
#endif
		listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		EXPECT_TRUE(listener_ != INVALID_SOCKET);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		EXPECT_EQ_INT(bind(listener_, (sockaddr *)&address, sizeof(address)), 0);
		EXPECT_EQ_INT(listen(listener_, 1), 0);
#if PPSSPP_PLATFORM(WINDOWS)
		int addressSize = sizeof(address);
#else
		socklen_t addressSize = sizeof(address);
#endif
		EXPECT_EQ_INT(getsockname(listener_, (sockaddr *)&address, &addressSize), 0);
		client_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		EXPECT_TRUE(client_ != INVALID_SOCKET);
		fd_util::SetNonBlocking((int)client_, true);
		int connected = connect(client_, (sockaddr *)&address, sizeof(address));
		EXPECT_TRUE(connected == 0 || connectInProgress(socket_errno));
		EXPECT_TRUE(fd_util::WaitUntilReady((int)listener_, 5.0));
		fd_util::SetNonBlocking((int)listener_, true);
		server_ = accept(listener_, nullptr, nullptr);
		EXPECT_TRUE(server_ != INVALID_SOCKET);
		output_ = std::make_unique<net::OutputSink>(server_);
		input_ = std::make_unique<net::InputSink>(client_);
		websocket_ = std::make_unique<TestWebSocket>(server_, output_.get());
		subscriber_.reset(WebSocketGameInit(handlers_));
		return true;
	}

	bool Request(bool save, const char *expectedEvent, const char *expectedMessage = nullptr) {
		RET(Send(save, Path("unused-admission-test.ppst"), nullptr, R"({"request":17})", true));
		return Expect(expectedEvent, R"({"request":17})", nullptr, nullptr, expectedMessage);
	}

	bool Send(bool save, const Path &path, const char *wait, const char *ticket, bool expectedFinished) {
		JsonWriter json;
		json.begin();
		json.writeString("event", save ? "game.state.save" : "game.state.load");
		json.writeString("path", path.ToString());
		if (wait) json.writeRaw("wait", wait);
		if (ticket) json.writeRaw("ticket", ticket);
		json.end();
		{
			std::string data = json.str();
			json::JsonReader reader(data.data(), data.size());
			EXPECT_TRUE(reader.ok());
			const char *event = reader.root().getStringOr("event", "");
			DebuggerRequest req(event, websocket_.get(), reader.root(), &clientInfo_);
			handlers_.at(event)(req);
			EXPECT_EQ_INT(req.Finish(), expectedFinished);
		}
		// The request reader and its event/ticket storage are gone before Broadcast or Process.
		return output_->Flush(false);
	}

	bool Expect(const char *event, const char *ticket, const char *status = nullptr, const char *forEvent = nullptr, const char *message = nullptr) {
		// Read one bounded, unmasked, non-fragmented text frame from the production writer.
		unsigned char header[2];
		EXPECT_TRUE(input_->TakeExact((char *)header, sizeof(header)));
		EXPECT_EQ_INT(header[0], 0x81);
		EXPECT_TRUE(header[1] <= 126);
		size_t size = header[1];
		if (size == 126) {
			EXPECT_TRUE(input_->TakeExact((char *)header, sizeof(header)));
			size = (header[0] << 8) | header[1];
		}
		EXPECT_TRUE(size <= 4096);
		std::string payload(size, '\0');
		EXPECT_TRUE(input_->TakeExact(payload.data(), payload.size()));
		json::JsonReader response(payload.data(), payload.size());
		EXPECT_TRUE(response.ok());
		printf("  %s\n", payload.c_str());
		const JsonGet &root = response.root();
		EXPECT_EQ_STR(std::string(root.getStringOr("event", "")), std::string(event));
		if (ticket) {
			EXPECT_TRUE(root.get("ticket") != nullptr);
			EXPECT_EQ_STR(json_stringify(root.get("ticket")), std::string(ticket));
		} else {
			EXPECT_TRUE(root.get("ticket") == nullptr);
		}
		if (status) {
			EXPECT_EQ_STR(std::string(root.getStringOr("status", "")), std::string(status));
			EXPECT_TRUE(strlen(root.getStringOr("message", "")) > 0);
		} else {
			EXPECT_TRUE(root.get("status") == nullptr);
		}
		if (forEvent) {
			EXPECT_EQ_STR(std::string(root.getStringOr("for", "")), std::string(forEvent));
			EXPECT_EQ_INT(root.getInt("level"), (int)LogLevel::LERROR);
		}
		if (message) {
			EXPECT_EQ_STR(std::string(root.getStringOr("message", "")), std::string(message));
		} else if (!status) {
			// Legacy acceptance has only event and ticket, and says nothing about completion.
			EXPECT_EQ_INT(root.numChildren(), ticket ? 2 : 1);
		}
		EXPECT_EQ_INT(input_->ValidAmount(), 0);
		return true;
	}

	bool Broadcast() {
		if (subscriber_) subscriber_->Broadcast(websocket_.get());
		return output_->Flush(false);
	}

	bool ExpectQuiet() {
		RET(Broadcast());
		EXPECT_EQ_INT(input_->ValidAmount(), 0);
		EXPECT_FALSE(fd_util::WaitUntilReady((int)client_, 0.01));
		return true;
	}

private:
#if PPSSPP_PLATFORM(WINDOWS)
	bool winsockStarted_ = false;
#endif
	SOCKET listener_ = INVALID_SOCKET;
	SOCKET server_ = INVALID_SOCKET;
	SOCKET client_ = INVALID_SOCKET;
	std::unique_ptr<net::OutputSink> output_;
	std::unique_ptr<net::InputSink> input_;
	std::unique_ptr<TestWebSocket> websocket_;
	WebSocketClientInfo clientInfo_;
	DebuggerEventHandlerMap handlers_;
	std::unique_ptr<DebuggerSubscriber> subscriber_;
};

class AdmissionEnvironment {
public:
	explicit AdmissionEnvironment(const char *directory = "ppsspp-savestate-admission")
		: config_(g_Config), parameter_(PSP_CoreParameter()), state_(coreState), pending_(coreStatePending), apctlState_(netApctlState) {
		g_Config.RestoreDefaults(RestoreSettingsBits::SETTINGS, false);
		g_Config.bAchievementsEnable = false;
		g_Config.bAutoSaveSymbolMap = false;
		g_Config.bAutoSaveLoadSymbols = false;
		g_Config.bFuncHashMap = false;
		g_Config.iDumpFileTypes = 0;
		g_Config.sReportHost.clear();
		g_Config.memStickDirectory = UnitTestTempPath(directory);
		g_Config.nandRootDirectory = g_Config.memStickDirectory / "nand";
		File::CreateDir(g_Config.memStickDirectory, true);
		g_threadManager.Init(1, 1);
		Core_ProcessCPUQueue();
	}

	~AdmissionEnvironment() {
		SaveState::Shutdown();
		Achievements::Shutdown();
		if (PSP_GetBootState() == BootState::Complete) PSP_Shutdown(true);
		g_threadManager.Teardown();
		g_Config = config_;
		PSP_CoreParameter() = parameter_;
		Core_UpdateState(state_);
		coreStatePending = pending_;
		netApctlState = apctlState_;
	}

	bool Boot() {
		// CI runs from the repository root; local builds may run from build/ or elsewhere.
		const char *relative = "pspautotests/tests/cpu/cpu_alu/cpu_alu.prx";
		Path fixture(relative);
		if (!File::Exists(fixture)) fixture = Path("..") / relative;
		if (!File::Exists(fixture)) fixture = Path(Path(__FILE__).GetDirectory()) / ".." / relative;
		if (!File::Exists(fixture)) printf("Missing cpu_alu.prx; initialize the pspautotests submodule.\n");
		EXPECT_TRUE(File::Exists(fixture));
		CoreParameter parameter{};
		parameter.cpuCore = CPUCore::INTERPRETER;
		parameter.gpuCore = GPUCORE_NULL;
		parameter.enableSound = false;
		parameter.headLess = true;
		parameter.startBreak = true;
		parameter.loadGameConfigs = false;
		parameter.updateRecent = false;
		parameter.fileToStart = fixture;
		std::string error;
		BootState state = PSP_Init(parameter, &error);
		if (state != BootState::Complete) printf("Admission fixture boot failed: %s\n", error.c_str());
		EXPECT_TRUE(state == BootState::Complete);
		return true;
	}

private:
	Config config_;
	CoreParameter parameter_;
	CoreState state_;
	bool pending_;
	u32 apctlState_;
};

// This is the same offline transport technique used by rcheevos' own client tests.
// Login and game processing state are established by its real public APIs and JSON parsers.
// No RetroAchievements request leaves the process, and no account or credentials are used.
static void RC_CCONV AchievementResponse(const rc_api_request_t *request, rc_client_server_callback_t callback, void *data, rc_client_t *) {
	const char *body = R"({"Success":false,"Error":"Unexpected test request"})";
	const char *post = request->post_data ? request->post_data : "";
	if (strstr(post, "r=login2") != nullptr) {
		body = R"({"Success":true,"User":"AdmissionTest","Token":"LocalTestToken","Score":0,"Messages":0})";
	} else if (strstr(post, "r=achievementsets") != nullptr) {
		body = R"({"Success":true,"GameId":1234,"Title":"Admission fixture","ConsoleId":41,
			"ImageIconUrl":"http://localhost/icon.png","RichPresenceGameId":1234,"RichPresencePatch":"",
			"Sets":[{"AchievementSetId":1111,"GameId":1234,"Title":null,"Type":"core",
			"ImageIconUrl":"http://localhost/icon.png","Leaderboards":[],"Achievements":[{
			"ID":5501,"Title":"Test","Description":"Test","Flags":3,"Points":1,"MemAddr":"0xH0001=3",
			"Author":"Test","BadgeName":"00000","Created":1,"Modified":1}]}]})";
	} else if (strstr(post, "r=startsession") != nullptr) {
		body = R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[],"ServerNow":1700000000})";
	}
	rc_api_server_response_t response{};
	response.body = body;
	response.body_length = strlen(body);
	response.http_status_code = 200;
	callback(&response, data);
}

static void RC_CCONV AchievementComplete(int result, const char *, rc_client_t *, void *data) {
	*(int *)data = result;
}

static bool EnableHardcoreFixture() {
	g_Config.bAchievementsEnable = true;
	g_Config.bAchievementsHardcoreMode = true;
	g_Config.bAchievementsSaveStateInHardcoreMode = false;
	g_Config.sAchievementsUserName.clear();
	Achievements::Initialize();
	rc_client_t *client = Achievements::GetClient();
	EXPECT_TRUE(client != nullptr);
	// The only private rcheevos dependency is replacing this client's transport callback.
	// Its user/game/runtime state and the PPSSPP admission predicates remain production code.
	client->callbacks.server_call = AchievementResponse;
	int result = -1;
	rc_client_begin_login_with_token(client, "AdmissionTest", "LocalTestToken", AchievementComplete, &result);
	EXPECT_EQ_INT(result, RC_OK);
	result = -1;
	rc_client_begin_load_game(client, "0123456789ABCDEF", AchievementComplete, &result);
	EXPECT_EQ_INT(result, RC_OK);
	EXPECT_TRUE(Achievements::HardcoreModeActive());
	return true;
}

static bool CheckProducer(bool save, bool expectedAdmission) {
	// The only surviving owner is the actual pending Operation's callback. This observes
	// enqueue, without reading queue internals or requiring the return type added by the fix.
	bool called = false;
	auto capture = std::make_shared<int>(0);
	std::weak_ptr<int> pending = capture;
	SaveState::Callback callback = [capture, &called](SaveState::Status, std::string_view, std::string_view) { called = true; };
	if (save) SaveState::Save(Path("unused-admission-test.ppst"), -1, callback);
	else SaveState::Load(Path("unused-admission-test.ppst"), -1, callback);
	callback = {};
	capture.reset();
	bool admitted = !pending.expired();
	SaveState::Shutdown();
	EXPECT_EQ_INT(admitted, expectedAdmission);
	EXPECT_TRUE(pending.expired());
	EXPECT_FALSE(called);
	return true;
}

static bool CheckAdmission(ResponseConnection &connection, const char *name, bool save, bool expectedAdmission) {
	printf("Admission case: %s\n", name);
	bool producer = CheckProducer(save, expectedAdmission);
	const char *event = expectedAdmission ? (save ? "game.state.save" : "game.state.load") : "error";
	const char *message = expectedAdmission ? nullptr : (save ? "Could not queue savestate save" : "Could not queue savestate load");
	bool handler = connection.Request(save, event, message);
	// Admission only: no frame is executed and no save/load completion is claimed.
	SaveState::Shutdown();
	return producer && handler;
}

}  // namespace

bool TestSaveStateAdmission() {
	AdmissionEnvironment environment;
	ResponseConnection connection;
	RET(connection.Open());
	EXPECT_TRUE(PSP_GetBootState() == BootState::Off);
	bool passed = connection.Request(false, "error", "Game not running");
	passed &= connection.Request(true, "error", "Game not running");
	RET(environment.Boot());

	// Control the real connectivity predicate; do not open a PSP network connection.
	netApctlState = PSP_NET_APCTL_STATE_GOT_IP;
	g_Config.bAllowSavestateWhileConnected = false;
	EXPECT_FALSE(NetworkAllowSaveState());
	passed &= CheckAdmission(connection, "online load refused", false, false);
	passed &= CheckAdmission(connection, "online save refused", true, false);
	g_Config.bAllowSavestateWhileConnected = true;
	EXPECT_TRUE(NetworkAllowSaveState());
	passed &= CheckAdmission(connection, "online load allowed by setting", false, true);
	passed &= CheckAdmission(connection, "online save allowed by setting", true, true);
	netApctlState = PSP_NET_APCTL_STATE_DISCONNECTED;
	g_Config.bAllowSavestateWhileConnected = false;
	EXPECT_TRUE(NetworkAllowSaveState());
	passed &= CheckAdmission(connection, "offline load accepted", false, true);
	passed &= CheckAdmission(connection, "offline save accepted", true, true);

	RET(EnableHardcoreFixture());
	passed &= CheckAdmission(connection, "hardcore load refused", false, false);
	passed &= CheckAdmission(connection, "hardcore save refused", true, false);
	g_Config.bAchievementsSaveStateInHardcoreMode = true;
	passed &= CheckAdmission(connection, "hardcore load still refused with save setting", false, false);
	passed &= CheckAdmission(connection, "hardcore save allowed by setting", true, true);
	return passed;
}

bool TestSaveStateCompletion() {
	AdmissionEnvironment environment("ppsspp-savestate-completion");
	ResponseConnection connection;
	RET(connection.Open());
	RET(environment.Boot());
	g_Config.bHideStateWarnings = false;
	const Path saved = g_Config.memStickDirectory / "completion.ppst";
	const Path busy = g_Config.memStickDirectory / "busy.ppst";
	const Path legacy = g_Config.memStickDirectory / "legacy.ppst";
	const Path missing = g_Config.memStickDirectory / "missing.ppst";
	const Path disconnected = g_Config.memStickDirectory / "disconnected.ppst";
	const Path abandoned = g_Config.memStickDirectory / "abandoned.ppst";
	const Path paths[] = {saved, busy, legacy, missing, disconnected, abandoned};
	for (const Path &path : paths) {
		if (File::Exists(path)) EXPECT_TRUE(File::Delete(path));
	}

	printf("Completion case: invalid wait does not queue or occupy the result slot\n");
	RET(connection.Send(true, busy, "[]", R"("invalid-wait")", true));
	RET(connection.Expect("error", R"("invalid-wait")", nullptr, nullptr, "Invalid 'wait' parameter type"));
	SaveState::Process();
	RET(connection.ExpectQuiet());
	EXPECT_FALSE(File::Exists(busy));

	printf("Completion case: real save, bounded wait slot, independent legacy request\n");
	SaveState::NotifySaveData();
	RET(connection.Send(true, saved, "true", R"({"request":[17,null,"save"]})", false));
	RET(connection.ExpectQuiet());
	EXPECT_FALSE(File::Exists(saved));
	RET(connection.Send(true, busy, "true", "18", true));
	RET(connection.Expect("error", "18", nullptr, nullptr, "A savestate completion is already pending on this connection"));
	RET(connection.Send(true, legacy, "false", "19", true));
	RET(connection.Expect("game.state.save", "19"));
	SaveState::Process();
	EXPECT_TRUE(File::GetFileSize(saved) > 0);
	EXPECT_TRUE(File::GetFileSize(legacy) > 0);
	EXPECT_FALSE(File::Exists(busy));
	// Process has released its callback copies. Recorded completion must win over token expiry.
	RET(connection.Broadcast());
	RET(connection.Expect("game.state.save", R"({"request":[17,null,"save"]})", "success"));
	RET(connection.ExpectQuiet());

	printf("Completion case: successful load and real savedata-generation warning\n");
	RET(connection.Send(false, saved, "true", R"("load")", false));
	RET(connection.ExpectQuiet());
	SaveState::Process();
	RET(connection.Broadcast());
	RET(connection.Expect("game.state.load", R"("load")", "success"));
	RET(connection.ExpectQuiet());
	SaveState::NotifySaveData();
	RET(connection.Send(false, saved, "true", R"(["warning",20])", false));
	SaveState::Process();
	RET(connection.Broadcast());
	RET(connection.Expect("game.state.load", R"(["warning",20])", "warning"));
	RET(connection.ExpectQuiet());

	printf("Completion case: failed file operations and all JSON ticket types\n");
	const char *tickets[] = {nullptr, "null", "true", "23", R"("quoted\"ticket")", R"([1,"two"])", R"({"nested":{"value":3}})"};
	for (const char *ticket : tickets) {
		RET(connection.Send(false, missing, "true", ticket, false));
		RET(connection.ExpectQuiet());
		SaveState::Process();
		RET(connection.Broadcast());
		RET(connection.Expect("error", ticket, "failed", "game.state.load"));
		RET(connection.ExpectQuiet());
	}
	// Saving to an existing directory fails in the real file writer.
	RET(connection.Send(true, g_Config.memStickDirectory, "true", "24", false));
	SaveState::Process();
	RET(connection.Broadcast());
	RET(connection.Expect("error", "24", "failed", "game.state.save"));
	RET(connection.ExpectQuiet());

	printf("Completion case: shutdown abandonment and pending-slot recovery\n");
	RET(connection.Send(true, abandoned, "true", "25", false));
	RET(connection.ExpectQuiet());
	SaveState::Shutdown();
	RET(connection.Broadcast());
	RET(connection.Expect("error", "25", "abandoned", "game.state.save", "Savestate callback was discarded without a completion result"));
	RET(connection.ExpectQuiet());
	EXPECT_FALSE(File::Exists(abandoned));
	RET(connection.Send(false, missing, "true", "26", false));
	SaveState::Process();
	RET(connection.Broadcast());
	RET(connection.Expect("error", "26", "failed", "game.state.load"));
	RET(connection.ExpectQuiet());

	printf("Completion case: connection lifetime and independent per-connection slots\n");
	RET(connection.Send(false, missing, "true", "27", false));
	{
		ResponseConnection other;
		RET(other.Open());
		RET(other.Send(true, disconnected, "true", "28", false));
		RET(other.ExpectQuiet());
		// Destroy the subscriber, request map, writer and socket before its callback runs.
	}
	SaveState::Process();
	EXPECT_TRUE(File::GetFileSize(disconnected) > 0);
	RET(connection.Broadcast());
	RET(connection.Expect("error", "27", "failed", "game.state.load"));
	RET(connection.ExpectQuiet());

	printf("Completion case: late hardcore refusal abandons the queued callback\n");
	RET(connection.Send(true, abandoned, "true", "29", false));
	RET(EnableHardcoreFixture());
	SaveState::Process();
	RET(connection.Broadcast());
	RET(connection.Expect("error", "29", "abandoned", "game.state.save"));
	RET(connection.ExpectQuiet());
	EXPECT_FALSE(File::Exists(abandoned));
	// An immediate refusal must not leave a pending result or produce an abandoned terminal.
	RET(connection.Send(false, saved, "true", "30", true));
	RET(connection.Expect("error", "30", nullptr, nullptr, "Could not queue savestate load"));
	RET(connection.ExpectQuiet());
	// Default requests retain their old response shape even when their callback is discarded.
	g_Config.bAchievementsSaveStateInHardcoreMode = true;
	RET(connection.Send(true, abandoned, nullptr, "31", true));
	RET(connection.Expect("game.state.save", "31"));
	SaveState::Shutdown();
	RET(connection.ExpectQuiet());

	for (const Path &path : paths) {
		if (File::Exists(path)) EXPECT_TRUE(File::Delete(path));
	}
	return true;
}

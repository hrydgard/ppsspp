#pragma once

#include <vector>
#include <mutex>
#include <map>
#include <functional>

#include "Common/System/System.h"

class Path;

typedef std::function<void(const char *responseString, int responseValue)> RequestCallback;
typedef std::function<void()> RequestFailedCallback;

typedef int RequesterToken;
#define NO_REQUESTER_TOKEN -1
#define NON_EPHEMERAL_TOKEN -2

// Platforms often have to process requests asynchronously, on wildly different threads,
// and then somehow pass a response back to the main thread (especially Android...)
// This acts as bridge and buffer.
// However - the actual request is performed on the current thread, it's the callbacks that are "made threadsafe"
// by running them on the main thread. So beware in your implementations!
class RequestManager {
public:
	// These requests are to be handled by platform implementations.
	// The callback you pass in will be called on the main thread later.
	// Params are at the end since it's the part most likely to recieve additions in the future,
	// now that we have both callbacks.
	bool MakeSystemRequest(SystemRequestType type, RequesterToken token, RequestCallback callback, RequestFailedCallback failedCallback, const std::string &param1, const std::string &param2, int param3);

	// Called by the platform implementation, when it's finished with a request.
	void PostSystemSuccess(int requestId, const char *responseString, int responseValue = 0);
	void PostSystemFailure(int requestId);

	// This must be called every frame from the beginning of NativeFrame().
	// This will call the callback of any finished requests.
	void ProcessRequests();

	RequesterToken GenerateRequesterToken() {
		int token = tokenGen_++;
		return token;
	}

	void ForgetRequestsWithToken(RequesterToken token);

	// Unclear if we need this...
	void Clear();

private:
	struct CallbackPair {
		RequestCallback callback;
		RequestFailedCallback failedCallback;
		RequesterToken token;
	};

	std::map<int, CallbackPair> callbackMap_;
	std::mutex callbackMutex_;

	struct PendingSuccess {
		std::string responseString;
		int responseValue;
		RequestCallback callback;
	};

	struct PendingFailure {
		RequestFailedCallback failedCallback;
	};

	// Let's start at 10 to get a recognizably valid ID in logs.
	int idCounter_ = 10;
	std::vector<PendingSuccess> pendingSuccesses_;
	std::vector<PendingFailure> pendingFailures_;
	std::mutex responseMutex_;

	RequesterToken tokenGen_ = 20000;
};

const char *RequestTypeAsString(SystemRequestType type);

extern RequestManager g_requestManager;

// Wrappers for easy requests.
// NOTE: Semantics have changed - this no longer calls the callback on cancellation, instead you
// can specify a different callback for that.
inline void System_InputBoxGetString(RequesterToken token, const std::string &title, const std::string &defaultValue, RequestCallback callback, RequestFailedCallback failedCallback = nullptr) {
	g_requestManager.MakeSystemRequest(SystemRequestType::INPUT_TEXT_MODAL, token, callback, failedCallback, title, defaultValue, 0);
}

// This one will pop up a special image browser if available. You can also pick
// images with the file browser below.
inline void System_BrowseForImage(RequesterToken token, const std::string &title, RequestCallback callback, RequestFailedCallback failedCallback = nullptr) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_IMAGE, token, callback, failedCallback, title, "", 0);
}

enum class BrowseFileType {
	BOOTABLE,
	IMAGE,
	INI,
	DB,
	SOUND_EFFECT,
	ZIP,
	ANY,
};

inline void System_BrowseForFile(RequesterToken token, const std::string &title, BrowseFileType type, RequestCallback callback, RequestFailedCallback failedCallback = nullptr) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_FILE, token, callback, failedCallback, title, "", (int)type);
}

void System_BrowseForFolder(RequesterToken token, const std::string &title, const Path &initialPath, RequestCallback callback, RequestFailedCallback failedCallback = nullptr);

// The returned string is username + '\n' + password.
inline void System_AskUsernamePassword(RequesterToken token, const std::string &title, RequestCallback callback, RequestFailedCallback failedCallback = nullptr) {
	g_requestManager.MakeSystemRequest(SystemRequestType::ASK_USERNAME_PASSWORD, token, callback, failedCallback, title, "", 0);
}

inline void System_CopyStringToClipboard(const std::string &string) {
	g_requestManager.MakeSystemRequest(SystemRequestType::COPY_TO_CLIPBOARD, NO_REQUESTER_TOKEN, nullptr, nullptr, string, "", 0);
}

inline void System_ExitApp() {
	g_requestManager.MakeSystemRequest(SystemRequestType::EXIT_APP, NO_REQUESTER_TOKEN, nullptr, nullptr, "", "", 0);
}

inline void System_RestartApp(const std::string &params) {
	g_requestManager.MakeSystemRequest(SystemRequestType::RESTART_APP, NO_REQUESTER_TOKEN, nullptr, nullptr, params, "", 0);
}

inline void System_RecreateActivity() {
	g_requestManager.MakeSystemRequest(SystemRequestType::RECREATE_ACTIVITY, NO_REQUESTER_TOKEN, nullptr, nullptr, "", "", 0);
}

// The design is a little weird, just a holdover from the old message. Can either toggle or set to on or off.
inline void System_ToggleFullscreenState(const std::string &param) {
	g_requestManager.MakeSystemRequest(SystemRequestType::TOGGLE_FULLSCREEN_STATE, NO_REQUESTER_TOKEN, nullptr, nullptr, param, "", 0);
}

inline void System_GraphicsBackendFailedAlert(const std::string &param) {
	g_requestManager.MakeSystemRequest(SystemRequestType::GRAPHICS_BACKEND_FAILED_ALERT, NO_REQUESTER_TOKEN, nullptr, nullptr, param, "", 0);
}

inline void System_CameraCommand(const std::string &command) {
	g_requestManager.MakeSystemRequest(SystemRequestType::CAMERA_COMMAND, NO_REQUESTER_TOKEN, nullptr, nullptr, command, "", 0);
}

inline void System_GPSCommand(const std::string &command) {
	g_requestManager.MakeSystemRequest(SystemRequestType::GPS_COMMAND, NO_REQUESTER_TOKEN, nullptr, nullptr, command, "", 0);
}

inline void System_InfraredCommand(const std::string &command) {
	g_requestManager.MakeSystemRequest(SystemRequestType::INFRARED_COMMAND, NO_REQUESTER_TOKEN, nullptr, nullptr, command, "", 0);
}

inline void System_MicrophoneCommand(const std::string &command) {
	g_requestManager.MakeSystemRequest(SystemRequestType::MICROPHONE_COMMAND, NO_REQUESTER_TOKEN, nullptr, nullptr, command, "", 0);
}

inline void System_ShareText(const std::string &text) {
	g_requestManager.MakeSystemRequest(SystemRequestType::SHARE_TEXT, NO_REQUESTER_TOKEN, nullptr, nullptr, text, "", 0);
}

inline void System_NotifyUIState(const std::string &state) {
	g_requestManager.MakeSystemRequest(SystemRequestType::NOTIFY_UI_STATE, NO_REQUESTER_TOKEN, nullptr, nullptr, state, "", 0);
}

inline void System_SetWindowTitle(const std::string &param) {
	g_requestManager.MakeSystemRequest(SystemRequestType::SET_WINDOW_TITLE, NO_REQUESTER_TOKEN, nullptr, nullptr, param, "", 0);
}

inline bool System_SendDebugOutput(const std::string &string) {
	return g_requestManager.MakeSystemRequest(SystemRequestType::SEND_DEBUG_OUTPUT, NO_REQUESTER_TOKEN, nullptr, nullptr, string, "", 0);
}

inline void System_SendDebugScreenshot(const std::string &data, int height) {
	g_requestManager.MakeSystemRequest(SystemRequestType::SEND_DEBUG_SCREENSHOT, NO_REQUESTER_TOKEN, nullptr, nullptr, data, "", height);
}

// Non-inline to avoid including Path.h
void System_CreateGameShortcut(const Path &path, const std::string &title);
void System_ShowFileInFolder(const Path &path);

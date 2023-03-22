#pragma once

#include <vector>
#include <mutex>
#include <map>
#include <functional>

#include "Common/System/System.h"

typedef std::function<void(const char *responseString, int responseValue)> RequestCallback;

// Platforms often have to process requests asynchronously, on wildly different threads.
// (Especially Android...)
// This acts as bridge and buffer.
class RequestManager {
public:
	// These requests are to be handled by platform implementations.
	// The callback you pass in will be called on the main thread later.
	bool MakeSystemRequest(SystemRequestType type, RequestCallback callback, const std::string &param1, const std::string &param2, int param3);

	// Called by the platform implementation, when it's finished with a request.
	void PostSystemSuccess(int requestId, const char *responseString, int responseValue = 0);
	void PostSystemFailure(int requestId);

	// This must be called every frame from the beginning of NativeUpdate().
	// This will call the callback of any finished requests.
	void ProcessRequests();

	// Unclear if we need this...
	void Clear();

private:
	struct PendingRequest {
		SystemRequestType type;
		RequestCallback callback;
	};

	std::map<int, RequestCallback> callbackMap_;
	std::mutex callbackMutex_;

	struct PendingResponse {
		std::string responseString;
		int responseValue;
		RequestCallback callback;
	};

	int idCounter_ = 0;
	std::vector<PendingResponse> pendingResponses_;
	std::mutex responseMutex_;
};

const char *RequestTypeAsString(SystemRequestType type);

extern RequestManager g_requestManager;

// Wrappers for easy requests.
// NOTE: Semantics have changed - this no longer calls the callback on cancellation.
inline void System_InputBoxGetString(const std::string &title, const std::string &defaultValue, RequestCallback callback) {
	g_requestManager.MakeSystemRequest(SystemRequestType::INPUT_TEXT_MODAL, callback, title, defaultValue, 0);
}

// This one will pop up a special image brwoser if available. You can also pick
// images with the file browser below.
inline void System_BrowseForImage(const std::string &title, RequestCallback callback) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_IMAGE, callback, title, "", 0);
}

enum class BrowseFileType {
	BOOTABLE,
	IMAGE,
	INI,
	ANY,
};

inline void System_BrowseForFile(const std::string &title, BrowseFileType type, RequestCallback callback) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_FILE, callback, title, "", (int)type);
}

inline void System_BrowseForFolder(const std::string &title, RequestCallback callback) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_FOLDER, callback, title, "", 0);
}

inline void System_CopyStringToClipboard(const std::string &string) {
	g_requestManager.MakeSystemRequest(SystemRequestType::COPY_TO_CLIPBOARD, nullptr, string, "", 0);
}

inline void System_ExitApp() {
	g_requestManager.MakeSystemRequest(SystemRequestType::EXIT_APP, nullptr, "", "", 0);
}

inline void System_RestartApp(const std::string &params) {
	g_requestManager.MakeSystemRequest(SystemRequestType::RESTART_APP, nullptr, params, "", 0);
}

// The design is a little weird, just a holdover from the old message. Can either toggle or set to on or off.
inline void System_ToggleFullscreenState(const std::string &param) {
	g_requestManager.MakeSystemRequest(SystemRequestType::TOGGLE_FULLSCREEN_STATE, nullptr, param, "", 0);
}

inline void System_GraphicsBackendFailedAlert(const std::string &param) {
	g_requestManager.MakeSystemRequest(SystemRequestType::GRAPHICS_BACKEND_FAILED_ALERT, nullptr, param, "", 0);
}

inline void System_CameraCommand(const std::string &command) {
	g_requestManager.MakeSystemRequest(SystemRequestType::CAMERA_COMMAND, nullptr, command, "", 0);
}

inline void System_GPSCommand(const std::string &command) {
	g_requestManager.MakeSystemRequest(SystemRequestType::GPS_COMMAND, nullptr, command, "", 0);
}

inline void System_MicrophoneCommand(const std::string &command) {
	g_requestManager.MakeSystemRequest(SystemRequestType::MICROPHONE_COMMAND, nullptr, command, "", 0);
}

inline void System_ShareText(const std::string &text) {
	g_requestManager.MakeSystemRequest(SystemRequestType::SHARE_TEXT, nullptr, text, "", 0);
}

inline void System_NotifyUIState(const std::string &state) {
	g_requestManager.MakeSystemRequest(SystemRequestType::NOTIFY_UI_STATE, nullptr, state, "", 0);
}

#include "ppsspp_config.h"

#include <cstring>

#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/TimeUtil.h"

#if PPSSPP_PLATFORM(ANDROID)

// Maybe not the most natural place for this, but not sure what would be. It needs to be in the Common project
// unless we want to make another System_ function to retrieve it.

#include <jni.h>

JavaVM *gJvm = nullptr;

#endif

RequestManager g_requestManager;

const char *RequestTypeAsString(SystemRequestType type) {
	switch (type) {
	case SystemRequestType::BROWSE_FOR_IMAGE: return "BROWSE_FOR_IMAGE";
	case SystemRequestType::BROWSE_FOR_FILE: return "BROWSE_FOR_FILE";
	case SystemRequestType::BROWSE_FOR_FOLDER: return "BROWSE_FOR_FOLDER";
	case SystemRequestType::BROWSE_FOR_FILE_SAVE: return "BROWSE_FOR_FILE_SAVE";
	case SystemRequestType::INPUT_TEXT_MODAL: return "INPUT_TEXT_MODAL";
	case SystemRequestType::ASK_USERNAME_PASSWORD: return "ASK_USERNAME_PASSWORD";
	case SystemRequestType::EXIT_APP: return "EXIT_APP";
	case SystemRequestType::RESTART_APP: return "RESTART_APP";
	case SystemRequestType::RECREATE_ACTIVITY: return "RECREATE_ACTIVITY";
	case SystemRequestType::COPY_TO_CLIPBOARD: return "COPY_TO_CLIPBOARD";
	case SystemRequestType::SHARE_TEXT: return "SHARE_TEXT";
	case SystemRequestType::SHARE_FILE: return "SHARE_FILE";
	case SystemRequestType::SET_WINDOW_TITLE: return "SET_WINDOW_TITLE";
	case SystemRequestType::APPLY_FULLSCREEN_STATE: return "SET_FULLSCREEN_STATE";
	case SystemRequestType::GRAPHICS_BACKEND_FAILED_ALERT: return "GRAPHICS_BACKEND_FAILED_ALERT";
	case SystemRequestType::CREATE_GAME_SHORTCUT: return "CREATE_GAME_SHORTCUT";
	case SystemRequestType::SHOW_FILE_IN_FOLDER: return "SHOW_FILE_IN_FOLDER";
	case SystemRequestType::NOTIFY_UI_EVENT: return "NOTIFY_UI_EVENT";
	case SystemRequestType::SET_KEEP_SCREEN_BRIGHT: return "SET_KEEP_SCREEN_BRIGHT";
	case SystemRequestType::CAMERA_COMMAND: return "CAMERA_COMMAND";
	case SystemRequestType::GPS_COMMAND: return "GPS_COMMAND";
	case SystemRequestType::INFRARED_COMMAND: return "INFRARED_COMMAND";
	case SystemRequestType::MICROPHONE_COMMAND: return "MICROPHONE_COMMAND";
	case SystemRequestType::RUN_CALLBACK_IN_WNDPROC: return "RUN_CALLBACK_IN_WNDPROC";
	case SystemRequestType::MOVE_TO_TRASH: return "MOVE_TO_TRASH";
	case SystemRequestType::IAP_RESTORE_PURCHASES: return "IAP_RESTORE_PURCHASES";
	case SystemRequestType::IAP_MAKE_PURCHASE: return "IAP_MAKE_PURCHASE";
	default: return "N/A";
	}
}

bool RequestManager::MakeSystemRequest(SystemRequestType type, RequesterToken token, RequestCallback callback, RequestFailedCallback failedCallback, std::string_view param1, std::string_view param2, int64_t param3, int64_t param4) {
	if (token == NO_REQUESTER_TOKEN) {
		_dbg_assert_(!callback);
		_dbg_assert_(!failedCallback);
	}
	if (callback || failedCallback) {
		_dbg_assert_(token != NO_REQUESTER_TOKEN);
	}

	int requestId = idCounter_++;

	// NOTE: We need to register immediately, in order to support synchronous implementations.
	if (callback || failedCallback) {
		std::lock_guard<std::mutex> guard(callbackMutex_);
		callbackMap_[requestId] = { callback, failedCallback, token };
	}

	VERBOSE_LOG(Log::System, "Making system request %s: id %d", RequestTypeAsString(type), requestId);
	std::string p1(param1);
	std::string p2(param2);
	// TODO: Convert to string_view
	if (!System_MakeRequest(type, requestId, p1, p2, param3, param4)) {
		if (callback || failedCallback) {
			std::lock_guard<std::mutex> guard(callbackMutex_);
			callbackMap_.erase(requestId);
		}
		return false;
	}
	return true;
}

void RequestManager::ForgetRequestsWithToken(RequesterToken token) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	for (auto &iter : callbackMap_) {
		if (iter.second.token == token) {
			INFO_LOG(Log::System, "Forgetting about requester with token %d", token);
			iter.second.callback = nullptr;
			iter.second.failedCallback = nullptr;
		}
	}
	// Also the ones that finished but haven't been processed yet.
	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	for (auto &response : pendingResponses_) {
		if (response.token == token) {
			response.callback = nullptr;
			response.failedCallback = nullptr;
		}
	}
}

// Responses can contain passwords, so we don't log them.
void RequestManager::PostSystemSuccess(int requestId, std::string_view responseString, int responseValue) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		ERROR_LOG(Log::System, "PostSystemSuccess: Unexpected request ID %d", requestId);
		return;
	}

	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	pendingResponses_.push_back({ true, std::string(responseString), responseValue, iter->second.callback, nullptr, iter->second.token });
	DEBUG_LOG(Log::System, "PostSystemSuccess: Request %d (%d chars, %d)", requestId, (int)responseString.size(), responseValue);
	callbackMap_.erase(iter);
}

void RequestManager::PostSystemFailure(int requestId, int responseValue) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		ERROR_LOG(Log::System, "PostSystemFailure: Unexpected request ID %d", requestId);
		return;
	}

	WARN_LOG(Log::System, "PostSystemFailure: Request %d failed", requestId);

	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	pendingResponses_.push_back({ false, std::string(), responseValue, nullptr, iter->second.failedCallback, iter->second.token });
	callbackMap_.erase(iter);
}

void RequestManager::ProcessRequests() {
	// One at a time, without holding the lock while calling. A callback can make new requests, or close a screen,
	// which forgets the callbacks of the responses still in the queue.
	while (true) {
		PendingResponse response;
		{
			std::lock_guard<std::mutex> guard(responseMutex_);
			if (pendingResponses_.empty()) {
				break;
			}
			response = std::move(pendingResponses_.front());
			pendingResponses_.pop_front();
		}
		if (response.success) {
			if (response.callback) {
				response.callback(response.responseString, response.responseValue);
			}
		} else if (response.failedCallback) {
			response.failedCallback(response.responseValue);
		}
	}
}

void RequestManager::Clear() {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	std::lock_guard<std::mutex> responseGuard(responseMutex_);

	pendingResponses_.clear();
	callbackMap_.clear();
}

static std::mutex g_textInputLock;
static bool g_textInputWanted[(size_t)TextInputUser::COUNT];

static void SendUIEvent(UIEventNotification notification) {
	g_requestManager.MakeSystemRequest(SystemRequestType::NOTIFY_UI_EVENT, NO_REQUESTER_TOKEN, nullptr, nullptr, "", "", (int64_t)notification, 0);
}

void System_SetTextInputWanted(TextInputUser user, bool wanted) {
	bool anyWanted = false;
	{
		std::lock_guard<std::mutex> guard(g_textInputLock);
		g_textInputWanted[(size_t)user] = wanted;
		for (bool w : g_textInputWanted) {
			anyWanted = anyWanted || w;
		}
	}
	// GOTFOCUS always goes through, as platforms may need it to show a keyboard again.
	if (wanted) {
		SendUIEvent(UIEventNotification::TEXT_GOTFOCUS);
	} else if (!anyWanted) {
		SendUIEvent(UIEventNotification::TEXT_LOSTFOCUS);
	}
}

void System_NotifyUIEvent(UIEventNotification notification) {
	switch (notification) {
	case UIEventNotification::TEXT_GOTFOCUS:
		System_SetTextInputWanted(TextInputUser::UI, true);
		break;
	case UIEventNotification::TEXT_LOSTFOCUS:
		System_SetTextInputWanted(TextInputUser::UI, false);
		break;
	case UIEventNotification::POPUP_CLOSED:
		// Platforms may hide an on-screen keyboard on this, but text input itself goes through the above.
		System_SetTextInputWanted(TextInputUser::UI, false);
		SendUIEvent(notification);
		break;
	default:
		SendUIEvent(notification);
		break;
	}
}

void System_CreateGameShortcut(const Path &path, std::string_view title) {
	g_requestManager.MakeSystemRequest(SystemRequestType::CREATE_GAME_SHORTCUT, NO_REQUESTER_TOKEN, nullptr, nullptr, path.ToString(), title, 0);
}

// Also acts as just show folder, if you pass in a folder.
void System_ShowFileInFolder(const Path &path) {
	g_requestManager.MakeSystemRequest(SystemRequestType::SHOW_FILE_IN_FOLDER, NO_REQUESTER_TOKEN, nullptr, nullptr, path.ToString(), "", 0);
}

void System_BrowseForFolder(RequesterToken token, std::string_view title, const Path &initialPath, RequestCallback callback, RequestFailedCallback failedCallback) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_FOLDER, token, callback, failedCallback, title, initialPath.ToCString(), 0);
}

void System_RunCallbackInWndProc(void (*callback)(void *, void *), void *userdata) {
	int64_t castPtr = (int64_t)callback;
	int64_t castUserData = (int64_t)userdata;
	g_requestManager.MakeSystemRequest(SystemRequestType::RUN_CALLBACK_IN_WNDPROC, NO_REQUESTER_TOKEN, nullptr, nullptr, "", "", castPtr, castUserData);
}

void System_MoveToTrash(const Path &path) {
	g_requestManager.MakeSystemRequest(SystemRequestType::MOVE_TO_TRASH, NO_REQUESTER_TOKEN, nullptr, nullptr, path.ToString(), "", 0);
}

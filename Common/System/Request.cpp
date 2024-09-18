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
	case SystemRequestType::INPUT_TEXT_MODAL: return "INPUT_TEXT_MODAL";
	case SystemRequestType::BROWSE_FOR_IMAGE: return "BROWSE_FOR_IMAGE";
	case SystemRequestType::BROWSE_FOR_FILE: return "BROWSE_FOR_FILE";
	case SystemRequestType::BROWSE_FOR_FOLDER: return "BROWSE_FOR_FOLDER";
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
	for (auto &iter : callbackMap_) {
		if (iter.second.token == token) {
			INFO_LOG(Log::System, "Forgetting about requester with token %d", token);
			iter.second.callback = nullptr;
			iter.second.failedCallback = nullptr;
		}
	}
}

void RequestManager::PostSystemSuccess(int requestId, const char *responseString, int responseValue) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		ERROR_LOG(Log::System, "PostSystemSuccess: Unexpected request ID %d (responseString=%s)", requestId, responseString);
		return;
	}

	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	PendingSuccess response;
	response.callback = iter->second.callback;
	response.responseString = responseString;
	response.responseValue = responseValue;
	pendingSuccesses_.push_back(response);
	DEBUG_LOG(Log::System, "PostSystemSuccess: Request %d (%s, %d)", requestId, responseString, responseValue);
	callbackMap_.erase(iter);
}

void RequestManager::PostSystemFailure(int requestId) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		ERROR_LOG(Log::System, "PostSystemFailure: Unexpected request ID %d", requestId);
		return;
	}

	WARN_LOG(Log::System, "PostSystemFailure: Request %d failed", requestId);

	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	PendingFailure response;
	response.failedCallback = iter->second.failedCallback;
	pendingFailures_.push_back(response);
	callbackMap_.erase(iter);
}

void RequestManager::ProcessRequests() {
	std::lock_guard<std::mutex> guard(responseMutex_);
	for (auto &iter : pendingSuccesses_) {
		if (iter.callback) {
			iter.callback(iter.responseString.c_str(), iter.responseValue);
		}
	}
	pendingSuccesses_.clear();
	for (auto &iter : pendingFailures_) {
		if (iter.failedCallback) {
			iter.failedCallback();
		}
	}
	pendingFailures_.clear();
}

void RequestManager::Clear() {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	std::lock_guard<std::mutex> responseGuard(responseMutex_);

	pendingSuccesses_.clear();
	pendingFailures_.clear();
	callbackMap_.clear();
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

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

bool RequestManager::MakeSystemRequest(SystemRequestType type, RequestCallback callback, RequestFailedCallback failedCallback, const std::string &param1, const std::string &param2, int param3) {
	int requestId = idCounter_++;

	// NOTE: We need to register immediately, in order to support synchronous implementations.
	if (callback || failedCallback) {
		std::lock_guard<std::mutex> guard(callbackMutex_);
		callbackMap_[requestId] = { callback, failedCallback };
	}

	DEBUG_LOG(SYSTEM, "Making system request %s: id %d", RequestTypeAsString(type), requestId);
	if (!System_MakeRequest(type, requestId, param1, param2, param3)) {
		if (callback || failedCallback) {
			std::lock_guard<std::mutex> guard(callbackMutex_);
			callbackMap_.erase(requestId);
		}
		return false;
	}

	return true;
}

void RequestManager::PostSystemSuccess(int requestId, const char *responseString, int responseValue) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		ERROR_LOG(SYSTEM, "PostSystemSuccess: Unexpected request ID %d (responseString=%s)", requestId, responseString);
		return;
	}

	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	PendingSuccess response;
	response.callback = iter->second.callback;
	response.responseString = responseString;
	response.responseValue = responseValue;
	pendingSuccesses_.push_back(response);
	DEBUG_LOG(SYSTEM, "PostSystemSuccess: Request %d (%s, %d)", requestId, responseString, responseValue);
	callbackMap_.erase(iter);
}

void RequestManager::PostSystemFailure(int requestId) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		ERROR_LOG(SYSTEM, "PostSystemFailure: Unexpected request ID %d", requestId);
		return;
	}

	WARN_LOG(SYSTEM, "PostSystemFailure: Request %d failed", requestId);

	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	PendingFailure response;
	response.callback = iter->second.failedCallback;
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
		if (iter.callback) {
			iter.callback();
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

void System_CreateGameShortcut(const Path &path, const std::string &title) {
	g_requestManager.MakeSystemRequest(SystemRequestType::CREATE_GAME_SHORTCUT, nullptr, nullptr, path.ToString(), title, 0);
}

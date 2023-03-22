#include "Common/System/Message.h"
#include "Common/System/System.h"
#include "Common/Log.h"

RequestManager g_RequestManager;

const char *RequestTypeAsString(SystemRequestType type) {
	switch (type) {
	case SystemRequestType::INPUT_TEXT_MODAL: return "INPUT_TEXT_MODAL";
	default: return "N/A";
	}
}

bool RequestManager::MakeSystemRequest(SystemRequestType type, RequestCallback callback, const char *param1, const char *param2) {
	int requestId = idCounter_++;
	if (!System_MakeRequest(type, requestId, param1, param2)) {
		return false;
	}

	if (!callback) {
		// We don't expect a response, this is a one-directional request. We're thus done.
		return true;
	}

	std::lock_guard<std::mutex> guard(callbackMutex_);
	callbackMap_[requestId] = callback;
	return true;
}

void RequestManager::PostSystemResponse(int requestId, const char *responseString, int responseValue) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		// Unexpected!
		ERROR_LOG(SYSTEM, "PostSystemResponse: Unexpected request ID %d for %s (responseString=%s)", requestId, responseString);
		return;
	}

	std::lock_guard<std::mutex> responseGuard(responseMutex_);
	PendingResponse response;
	response.callback = iter->second;
	response.responseString = responseString;
	response.responseValue = responseValue;
	pendingResponses_.push_back(response);
}

void RequestManager::ProcessRequests() {
	std::lock_guard<std::mutex> guard(responseMutex_);
	for (auto &iter : pendingResponses_) {
		if (iter.callback) {
			iter.callback(iter.responseString.c_str(), iter.responseValue);
		}
	}
	pendingResponses_.clear();
}

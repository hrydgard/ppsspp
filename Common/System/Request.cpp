#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/Log.h"

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

bool RequestManager::MakeSystemRequest(SystemRequestType type, RequestCallback callback, const std::string &param1, const std::string &param2, int param3) {
	int requestId = idCounter_++;

	// NOTE: We need to register immediately, in order to support synchronous implementations.
	{
		std::lock_guard<std::mutex> guard(callbackMutex_);
		callbackMap_[requestId] = callback;
	}

	INFO_LOG(SYSTEM, "Making system request %s: id %d, callback_valid %d", RequestTypeAsString(type), requestId, callback != nullptr);
	if (!System_MakeRequest(type, requestId, param1, param2, param3)) {
		{
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
	PendingResponse response;
	response.callback = iter->second;
	response.responseString = responseString;
	response.responseValue = responseValue;
	pendingResponses_.push_back(response);
	INFO_LOG(SYSTEM, "PostSystemSuccess: Request %d (%s, %d)", requestId, responseString, responseValue);
}

void RequestManager::PostSystemFailure(int requestId) {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	auto iter = callbackMap_.find(requestId);
	if (iter == callbackMap_.end()) {
		ERROR_LOG(SYSTEM, "PostSystemFailure: Unexpected request ID %d", requestId);
		return;
	}
	INFO_LOG(SYSTEM, "PostSystemFailure: Request %d failed", requestId);
	callbackMap_.erase(iter);
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

void RequestManager::Clear() {
	std::lock_guard<std::mutex> guard(callbackMutex_);
	std::lock_guard<std::mutex> responseGuard(responseMutex_);

	pendingResponses_.clear();
	callbackMap_.clear();
}

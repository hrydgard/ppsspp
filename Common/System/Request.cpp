#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/TimeUtil.h"

RequestManager g_requestManager;
OnScreenDisplay g_OSD;

void OnScreenDisplay::Update() {
	std::lock_guard<std::mutex> guard(mutex_);

	double now = time_now_d();
	for (auto iter = entries_.begin(); iter != entries_.end(); ) {
		if (now >= iter->endTime) {
			iter = entries_.erase(iter);
		} else {
			iter++;
		}
	}
}

std::vector<OnScreenDisplay::Entry> OnScreenDisplay::Entries() {
	std::lock_guard<std::mutex> guard(mutex_);
	return entries_;  // makes a copy.
}

void OnScreenDisplay::Show(OSDType type, const std::string &text, float duration_s, const char *id) {
	// Automatic duration based on type.
	if (duration_s <= 0.0f) {
		switch (type) {
		case OSDType::MESSAGE_ERROR:
		case OSDType::MESSAGE_WARNING:
			duration_s = 3.0f;
			break;
		case OSDType::MESSAGE_FILE_LINK:
			duration_s = 5.0f;
			break;
		case OSDType::MESSAGE_SUCCESS:
			duration_s = 2.0f;
			break;
		default:
			duration_s = 1.5f;
			break;
		}
	}

	double now = time_now_d();
	std::lock_guard<std::mutex> guard(mutex_);
	if (id) {
		for (auto iter = entries_.begin(); iter != entries_.end(); ++iter) {
			if (iter->id && !strcmp(iter->id, id)) {
				Entry msg = *iter;
				msg.endTime = now + duration_s;
				msg.text = text;
				entries_.erase(iter);
				entries_.insert(entries_.begin(), msg);
				return;
			}
		}
	}

	Entry msg;
	msg.text = text;
	msg.endTime = now + duration_s;
	msg.id = id;
	entries_.insert(entries_.begin(), msg);
}

void OnScreenDisplay::ShowOnOff(const std::string &message, bool on, float duration_s) {
	// TODO: translate "on" and "off"? Or just get rid of this whole thing?
	Show(OSDType::MESSAGE_INFO, message + ": " + (on ? "on" : "off"), duration_s);
}

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

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

inline void System_BrowseForImage(const std::string &title, RequestCallback callback) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_IMAGE, callback, title, "", 0);
}

enum class BrowseFileType {
	BOOTABLE,
	INI,
	ANY,
};

inline void System_BrowseForFile(const std::string &title, BrowseFileType type, RequestCallback callback) {
	g_requestManager.MakeSystemRequest(SystemRequestType::BROWSE_FOR_FILE, callback, title, "", (int)type);
}

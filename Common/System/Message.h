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
	bool MakeSystemRequest(SystemRequestType type, RequestCallback callback, const char *param1, const char *param2);

	// Called by the platform implementation, when it's finished with a request.
	void PostSystemResponse(int requestId, const char *responseString, int responseValue);

	// This must be called every frame from the beginning of NativeUpdate().
	// This will call the callback of any finished requests.
	void ProcessRequests();

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

extern RequestManager g_RequestManager;

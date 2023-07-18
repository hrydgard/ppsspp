#pragma once

#include <cstdint>
#include <functional>

#include "Common/Buffer.h"

namespace net {

class RequestProgress {
public:
	RequestProgress() {}
	explicit RequestProgress(bool *c) : cancelled(c) {}

	void Update(float newProgress) {
		progress = newProgress;
		if (callback) {
			callback(newProgress);
		}
	}

	float progress = 0.0f;
	float kBps = 0.0f;
	bool *cancelled = nullptr;
	std::function<void(float)> callback;
};

class Buffer : public ::Buffer {
public:
	bool FlushSocket(uintptr_t sock, double timeout, bool *cancelled = nullptr);

	bool ReadAllWithProgress(int fd, int knownSize, RequestProgress *progress);

	// < 0: error
	// >= 0: number of bytes read
	int Read(int fd, size_t sz);
};

}

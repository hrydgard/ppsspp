#pragma once

#include "Common/Buffer.h"

namespace net {

class Buffer : public ::Buffer {
public:
	bool FlushSocket(uintptr_t sock, double timeout, bool *cancelled = nullptr);

	bool ReadAllWithProgress(int fd, int knownSize, float *progress, float *kBps, bool *cancelled);

	// < 0: error
	// >= 0: number of bytes read
	int Read(int fd, size_t sz);
};

}

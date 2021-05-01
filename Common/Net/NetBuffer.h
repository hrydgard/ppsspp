#pragma once

#include "Common/Buffer.h"

namespace net {

class Buffer : public ::Buffer {
public:
	bool FlushSocket(uintptr_t sock, double timeout = -1.0, bool *cancelled = nullptr);

	bool ReadAll(int fd, int hintSize = 0);
	bool ReadAllWithProgress(int fd, int knownSize, float *progress, bool *cancelled);

	// < 0: error
	// >= 0: number of bytes read
	int Read(int fd, size_t sz);
};

}

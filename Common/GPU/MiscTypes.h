#pragma once

#include "Common/Common.h"

// Flags and structs shared between backends that haven't found a good home.

enum class InvalidationFlags {
	CACHED_RENDER_STATE = 1,
};
ENUM_CLASS_BITOPS(InvalidationFlags);

enum class InvalidationCallbackFlags {
	RENDER_PASS_STATE = 1,
	COMMAND_BUFFER_STATE = 2,
};
ENUM_CLASS_BITOPS(InvalidationCallbackFlags);

typedef std::function<void(InvalidationCallbackFlags)> InvalidationCallback;

// These are separate from FrameData because we store some history of these.
// Also, this might be joined with more non-GPU timing information later.
struct FrameTimeData {
	uint64_t frameId;

	int waitCount;

	double frameBegin;
	double afterFenceWait;
	double firstSubmit;
	double queuePresent;

	double actualPresent;
	double desiredPresentTime;
	double earliestPresentTime;
	double presentMargin;
};
constexpr size_t FRAME_TIME_HISTORY_LENGTH = 32;

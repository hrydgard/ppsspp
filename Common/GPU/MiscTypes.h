#pragma once

#include "Common/Common.h"

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

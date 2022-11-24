#pragma once

#include "Common/Common.h"

enum class InvalidationFlags {
	RENDER_PASS_STATE = 1,
	COMMAND_BUFFER_STATE = 2,
};
ENUM_CLASS_BITOPS(InvalidationFlags);

typedef std::function<void(InvalidationFlags)> InvalidationCallback;

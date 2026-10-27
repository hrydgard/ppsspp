#pragma once

#include <string_view>
#include "Common/Common.h"

enum class AdrenoEnvVar : int {
	enable_tp_ubwc_flag_hint = (1 << 0),
	storage_8bit = (1 << 1),
};
ENUM_CLASS_BITOPS(AdrenoEnvVar);

struct AdrenoEnvVarDef {
	std::string_view name;
	AdrenoEnvVar var;
};

const AdrenoEnvVarDef *GetAdrenoEnvVarDefs(int *count);

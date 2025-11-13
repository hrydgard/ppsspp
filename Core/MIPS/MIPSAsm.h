#pragma once

#include <string_view>
#include "Core/Debugger/DebugInterface.h"

bool MipsAssembleOpcode(std::string_view line, DebugInterface* cpu, u32 address, std::string *error);

#pragma once

#include "Core/Debugger/DebugInterface.h"

namespace MIPSAsm {
	bool MipsAssembleOpcode(const char* line, DebugInterface* cpu, u32 address);
	std::string GetAssembleError();
}

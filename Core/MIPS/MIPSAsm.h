#pragma once

#include "Core/Debugger/DebugInterface.h"

namespace MIPSAsm {
	// NOTE: This can support assembling multiple opcodes.
	bool MipsAssembleOpcode(const char* line, DebugInterface* cpu, u32 address);
	std::string GetAssembleError();
}

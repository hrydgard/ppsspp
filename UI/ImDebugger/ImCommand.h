#pragma once

#include <cstdint>

enum class ImCmd {
	NONE = 0,
	TRIGGER_FIND_POPUP,
	SHOW_IN_CPU_DISASM,
	SHOW_IN_GE_DISASM,
	SHOW_IN_MEMORY_VIEWER,  // param is address, param2 is viewer index
	SHOW_IN_PIXEL_VIEWER,  // param is address, param2 is stride, |0x80000000 if depth, param3 is w/h
	SHOW_IN_MEMORY_DUMPER, // param is address, param2 is size, param3 is mode
};

struct ImCommand {
	ImCmd cmd;
	uint32_t param;
	uint32_t param2;
	uint32_t param3;
};

struct ImControl {
	ImCommand command;
};

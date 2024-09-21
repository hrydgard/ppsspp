// Copyright (c) 2024- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <unordered_map>
#include <vector>
#include <string>

#include "Common/CommonTypes.h"
#include "Core/Opcode.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/Data/Collections/CyclicBuffer.h"


struct TraceBlockInfo {
	u32 virt_address;
	u32 storage_index;
};

struct TraceBlockStorage {
	std::vector<u32> raw_instructions;
	u32 cur_index;
	u32* cur_data_ptr;

	TraceBlockStorage(u32 capacity):
		raw_instructions(capacity, 0),
		cur_index(0),
		cur_data_ptr(raw_instructions.data())
		{}

	TraceBlockStorage(): raw_instructions(), cur_index(0), cur_data_ptr(nullptr) {}

	bool save_block(const u32* instructions, u32 size);

	void initialize(u32 capacity);
	void clear();

	u32 operator[](u32 index) {
		return raw_instructions[index];
	}
	Memory::Opcode read_asm(u32 index) {
		return Memory::Opcode(raw_instructions[index]);
	}
};



// This system is meant for trace recording.
// A trace here stands for a sequence of instructions and their respective addresses in RAM.
// The register/memory changes (or thread switches) are not included!
// Note: the tracer stores the basic blocks inside, which causes the last block to be dumped as a whole,
// despite the fact that it may not have executed to its end by the time the tracer is stopped.
struct MIPSTracer {
	std::vector<TraceBlockInfo> trace_info;

	// The trace might be very big, in that case I don't mind losing the oldest entries.
	CyclicBuffer<u32> executed_blocks;

	std::unordered_map<u64, u32> hash_to_storage_index;

	TraceBlockStorage storage;

	Path logging_path;
	FILE* output;
	bool tracing_enabled = false;

	int in_storage_capacity = 0x10'0000;
	int in_max_trace_size = 0x10'0000;

	void start_tracing();
	void stop_tracing();

	void prepare_block(const MIPSComp::IRBlock* block, MIPSComp::IRBlockCache& blocks);
	void set_logging_path(std::string path) {
		logging_path = Path(path);
	}
	std::string get_logging_path() const {
		return logging_path.ToString();
	}

	bool flush_to_file();
	void flush_block_to_file(const TraceBlockInfo& block);

	void initialize(u32 storage_capacity, u32 max_trace_size);
	void clear();

	inline void print_stats() const;

	MIPSTracer(): trace_info(), executed_blocks(), hash_to_storage_index(), storage(), logging_path() {}
};

extern MIPSTracer mipsTracer;

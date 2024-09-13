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
#include <iterator>
#include <fstream>

#include "Common/CommonTypes.h"
#include "Core/Opcode.h"
#include "Core/MIPS/IR/IRJit.h"
#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"


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


template <typename T>
struct CyclicBuffer {
	std::vector<T> buffer;
	u32 current_index;
	bool overflow;

	explicit CyclicBuffer(u32 capacity) : buffer(capacity, T()), current_index(0), overflow(false) {}

	CyclicBuffer(): buffer(), current_index(0), overflow(false) {}

	void push_back(const T& value);
	void push_back(T&& value);

	void clear();
	void resize(u32 new_capacity);

	std::vector<T> get_content() const;
};

template<typename T>
std::vector<T> CyclicBuffer<T>::get_content() const {
	if (!overflow) {
		return std::vector<T>(buffer.begin(), buffer.begin() + current_index);
	}

	std::vector<T> ans;
	ans.reserve(buffer.size());
	std::copy(buffer.begin() + current_index, buffer.end(), std::back_inserter(ans));
	std::copy(buffer.begin(), buffer.begin() + current_index, std::back_inserter(ans));
	return ans;
}

template <typename T>
void CyclicBuffer<T>::push_back(const T& value) {
	buffer[current_index] = value;
	++current_index;
	if (current_index == buffer.size()) {
		current_index = 0;
		overflow = true;
	}
}

template <typename T>
void CyclicBuffer<T>::push_back(T&& value) {
	buffer[current_index] = std::move(value);
	++current_index;
	if (current_index == buffer.size()) {
		current_index = 0;
		overflow = true;
	}
}

template <typename T>
void CyclicBuffer<T>::clear() {
	buffer.clear();
	current_index = 0;
	overflow = false;
}

template <typename T>
void CyclicBuffer<T>::resize(u32 new_capacity) {
	buffer.resize(new_capacity);
}

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

	void prepare_block(MIPSComp::IRBlock* block, MIPSComp::IRBlockCache& blocks);
	void setLoggingPath(std::string path) {
		logging_path = Path(path);
	}
	std::string getLoggingPath() const {
		return logging_path.ToString();
	}

	bool flush_to_file();
	void flush_block_to_file(TraceBlockInfo& block);

	void initialize(u32 storage_capacity, u32 max_trace_size);
	void clear();

	inline void print_stats() const;

	MIPSTracer(): trace_info(), executed_blocks(), hash_to_storage_index(), storage(), logging_path() {}
};

extern MIPSTracer mipsTracer;

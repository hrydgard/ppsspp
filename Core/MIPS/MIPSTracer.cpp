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

#include "Core/MIPS/MIPSTracer.h"

#include <cstring> // for std::memcpy
#include "Core/MIPS/MIPSTables.h" // for MIPSDisAsm
#include "Core/MemMap.h" // for Memory::GetPointerUnchecked
#include "Common/File/FileUtil.h" // for the File::OpenCFile


bool TraceBlockStorage::save_block(const u32* instructions, u32 size) {
	// 'size' is measured in bytes
	const auto indexes_count = size / 4;

	if (cur_index + 1 + indexes_count >= raw_instructions.size()) {
		return false;
	}

	// Save the size first
	*cur_data_ptr = size;
	++cur_data_ptr;

	// Now save the MIPS instructions
	std::memcpy(cur_data_ptr, instructions, size);
	cur_data_ptr += indexes_count;

	cur_index += 1 + indexes_count;
	return true;
}

void TraceBlockStorage::initialize(u32 capacity) {
	raw_instructions.resize(capacity);
	cur_index = 0;
	cur_data_ptr = raw_instructions.data();
	INFO_LOG(Log::JIT, "TraceBlockStorage initialized: capacity=0x%x", capacity);
}

void TraceBlockStorage::clear() {
	raw_instructions.clear();
	cur_index = 0;
	cur_data_ptr = nullptr;
	INFO_LOG(Log::JIT, "TraceBlockStorage cleared");
}

void MIPSTracer::prepare_block(const MIPSComp::IRBlock* block, MIPSComp::IRBlockCache& blocks) {
	u32 virt_addr, size;
	block->GetRange(&virt_addr, &size);

	u64 hash = block->GetHash();
	auto it = hash_to_storage_index.find(hash);

	u32 storage_index;
	if (it != hash_to_storage_index.end()) {
		// We've seen this one before => it's saved in our storage
		storage_index = it->second;
	}
	else {
		// We haven't seen a block like that before, let's save it
		auto mips_instructions_ptr = (const u32*)Memory::GetPointerUnchecked(virt_addr);

		storage_index = storage.cur_index;
		if (!storage.save_block(mips_instructions_ptr, size)) {
			// We ran out of storage!
			WARN_LOG(Log::JIT, "The MIPSTracer ran out of storage for the blocks, cannot proceed!");
			stop_tracing();
			return;
		}
		// Successfully inserted the block at index 'storage_index'!

		hash_to_storage_index.emplace(hash, storage_index);
	}

	// NB!
	// If for some reason the blocks get invalidated while tracing, PPSSPP will be forced to recompile
	// the same code again => the 'trace_info' will be filled with duplicates, because we can't detect that...
	// If we store the TraceBlockInfo instances in an unordered_map, we won't be able to reference the entries
	// by using the 4 byte IRInst field 'constant' (the iterators won't fit there).
	// And, of course, doing a linear search in the vector is not worth the conserved space.
	trace_info.push_back({ virt_addr, storage_index });


	u32 index = (u32)(trace_info.size() - 1);
	auto ir_ptr = (IRInst*)blocks.GetBlockInstructionPtr(*block);
	ir_ptr[1].constant = index;
}

bool MIPSTracer::flush_to_file() {
	if (logging_path.empty()) {
		WARN_LOG(Log::JIT, "The path is empty, cannot flush the trace!");
		return false;
	}

	INFO_LOG(Log::JIT, "Flushing the trace to a file...");
	output = File::OpenCFile(logging_path, "w");
	
	if (!output) {
		WARN_LOG(Log::JIT, "MIPSTracer failed to open the file '%s'", logging_path.c_str());
		return false;
	}
	auto trace = executed_blocks.get_content();
	for (auto index : trace) {
		auto& block_info = trace_info[index];
		flush_block_to_file(block_info);
	}

	INFO_LOG(Log::JIT, "Trace flushed, closing the file...");
	std::fclose(output);

	clear();
	return true;
}

void MIPSTracer::flush_block_to_file(const TraceBlockInfo& block_info) {
	char buffer[512];

	// The log format is '{prefix}{disassembled line}', where 'prefix' is '0x{8 hex digits of the address}: '
	const auto prefix_size = 2 + 8 + 2;

	u32 addr = block_info.virt_address;
	u32 index = block_info.storage_index;

	u32 size = storage[index];
	++index;

	u32 end_addr = addr + size;
	

	for (; addr < end_addr; addr += 4, ++index) {
		snprintf(buffer, sizeof(buffer), "0x%08x: ", addr);
		MIPSDisAsm(storage.read_asm(index), addr, buffer + prefix_size, sizeof(buffer) - prefix_size, true);

		std::fprintf(output, "%s\n", buffer);
	}
}

void MIPSTracer::start_tracing() {
	if (!tracing_enabled) {
		INFO_LOG(Log::JIT, "MIPSTracer enabled");
		tracing_enabled = true;
	}
}

void MIPSTracer::stop_tracing() {
	if (tracing_enabled) {
		INFO_LOG(Log::JIT, "MIPSTracer disabled");
		tracing_enabled = false;

#ifdef _DEBUG
		print_stats();
#endif
	}
}

inline void MIPSTracer::print_stats() const {
	// First, the storage
	INFO_LOG(Log::JIT, "=============== MIPSTracer storage ===============");
	INFO_LOG(Log::JIT, "Current index = %d, storage size = %d", storage.cur_index, (int)storage.raw_instructions.size());

	// Then the cyclic buffer
	if (executed_blocks.overflow) {
		INFO_LOG(Log::JIT, "=============== MIPSTracer cyclic buffer (overflow) ===============");
		INFO_LOG(Log::JIT, "Trace size = %d, starts from index %d", (int)executed_blocks.buffer.size(), executed_blocks.current_index);
	}
	else {
		INFO_LOG(Log::JIT, "=============== MIPSTracer cyclic buffer (no overflow) ===============");
		INFO_LOG(Log::JIT, "Trace size = %d, starts from index 0", executed_blocks.current_index);
	}
	// Next, the hash-to-index mapping
	INFO_LOG(Log::JIT, "=============== MIPSTracer hashes ===============");
	INFO_LOG(Log::JIT, "Number of unique hashes = %d", (int)hash_to_storage_index.size());

	// Finally, the basic block list
	INFO_LOG(Log::JIT, "=============== MIPSTracer basic block list ===============");
	INFO_LOG(Log::JIT, "Number of processed basic blocks = %d", (int)trace_info.size());

	INFO_LOG(Log::JIT, "=============== MIPSTracer stats end ===============");
}

void MIPSTracer::initialize(u32 storage_capacity, u32 max_trace_size) {
	executed_blocks.resize(max_trace_size);
	hash_to_storage_index.reserve(max_trace_size);
	storage.initialize(storage_capacity);
	trace_info.reserve(max_trace_size);
	INFO_LOG(Log::JIT, "MIPSTracer initialized: storage_capacity=0x%x, max_trace_size=%d", storage_capacity, max_trace_size);
}

void MIPSTracer::clear() {
	executed_blocks.clear();
	hash_to_storage_index.clear();
	storage.clear();
	trace_info.clear();
	INFO_LOG(Log::JIT, "MIPSTracer cleared");
}

MIPSTracer mipsTracer;



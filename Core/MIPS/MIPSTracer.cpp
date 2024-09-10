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


bool TraceBlockStorage::push_block(const u32* instructions, u32 size) {
	if (cur_offset + size >= raw_instructions.size()) {
		return false;
	}
	std::memcpy(cur_data_ptr, instructions, size);
	cur_offset += size;
	cur_data_ptr += size;
	return true;
}

void TraceBlockStorage::initialize(u32 capacity) {
	raw_instructions.resize(capacity);
	cur_offset = 0;
	cur_data_ptr = raw_instructions.data();
	INFO_LOG(Log::JIT, "TraceBlockStorage initialized: capacity=0x%x", capacity);
}

void TraceBlockStorage::clear() {
	raw_instructions.clear();
	cur_offset = 0;
	cur_data_ptr = nullptr;
	INFO_LOG(Log::JIT, "TraceBlockStorage cleared");
}

void MIPSTracer::prepare_block(MIPSComp::IRBlock* block, MIPSComp::IRBlockCache& blocks) {
	auto hash = block->GetHash();
	auto it = mipsTracer.hash_to_index.find(hash);

	if (it != mipsTracer.hash_to_index.end()) {
		u32 index = it->second;
		auto ir_ptr = (IRInst*)blocks.GetBlockInstructionPtr(*block);

		ir_ptr[1].constant = index;
		return;
	}

	// 1) Copy the block instructions into our storage
	u32 virt_addr, size;
	block->GetRange(virt_addr, size);
	u32 storage_index = mipsTracer.storage.cur_offset;

	auto mips_instructions_ptr = (const u32*)Memory::GetPointerUnchecked(virt_addr);

	if (!mipsTracer.storage.push_block(mips_instructions_ptr, size)) {
		// We ran out of storage!
		WARN_LOG(Log::JIT, "The MIPSTracer ran out of storage for the blocks, cannot proceed!");
		stop_tracing();
		return;
	}
	// Successfully inserted the block at index 'possible_index'!

	mipsTracer.trace_info.push_back({ virt_addr, size, storage_index });

	// 2) Save the hash and the index

	u32 index = mipsTracer.trace_info.size() - 1;
	hash_to_index.emplace(hash, index);

	auto ir_ptr = (IRInst*)blocks.GetBlockInstructionPtr(*block);
	ir_ptr[1].constant = index;
}

bool MIPSTracer::flush_to_file() {
	INFO_LOG(Log::JIT, "MIPSTracer ordered to flush the trace to a file...");

	output.open(logging_path, std::ios::out);
	if (!output) {
		WARN_LOG(Log::JIT, "MIPSTracer failed to open the file '%s'", logging_path);
		return false;
	}
	auto trace = executed_blocks.get_content();
	for (auto index : trace) {
		auto& block_info = trace_info[index];
		flush_block_to_file(block_info);
	}

	INFO_LOG(Log::JIT, "Trace flushed, closing the file...");
	output.close();
	clear();
	return true;
}

void MIPSTracer::flush_block_to_file(TraceBlockInfo& block_info) {
	static char buffer[1024];

	// The log format is '0x{8 hex digits of the address}: {disassembled line}'
	const auto prefix_size = 2 + 8 + 2;

	u32 addr = block_info.virt_address;
	u32 index = block_info.storage_index;
	u32 end_addr = addr + block_info.size;

	for (; addr < end_addr; addr += 4, ++index) {
		snprintf(buffer, sizeof(buffer), "0x%08x: ", addr);
		MIPSDisAsm(storage[index], addr, buffer + prefix_size, sizeof(buffer) - prefix_size, true);

		// TODO: check if removing the std::string makes this faster
		output << std::string(buffer) << "\n";
	}
}

void MIPSTracer::start_tracing() {
	if (!tracing_enabled) {
		INFO_LOG(Log::JIT, "MIPSTracer enabled");
	}
	tracing_enabled = true;
}

void MIPSTracer::stop_tracing() {
	if (tracing_enabled) {
		INFO_LOG(Log::JIT, "MIPSTracer disabled");
	}
	tracing_enabled = false;
}

void MIPSTracer::initialize(u32 storage_capacity, u32 max_trace_size) {
	executed_blocks.resize(max_trace_size);
	hash_to_index.reserve(max_trace_size);
	storage.initialize(storage_capacity);
	trace_info.reserve(max_trace_size);
	INFO_LOG(Log::JIT, "MIPSTracer initialized: storage_capacity=0x%x, max_trace_size=%d", storage_capacity, max_trace_size);
}

void MIPSTracer::clear() {
	executed_blocks.clear();
	hash_to_index.clear();
	storage.clear();
	trace_info.clear();
	INFO_LOG(Log::JIT, "MIPSTracer cleared");
}

MIPSTracer mipsTracer;



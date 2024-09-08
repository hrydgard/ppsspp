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
}

void MIPSTracer::prepare_block(MIPSComp::IRBlock* block, MIPSComp::IRBlockCache& blocks) {
	auto hash = block->GetHash();
	auto it = mipsTracer.hash_to_index.find(hash);

	if (it != mipsTracer.hash_to_index.end()) {
		u32 index = it->second;
		auto ir_ptr = (IRInst*)blocks.GetBlockInstructionPtr(*block);
		ir_ptr->constant = index;
		return;
	}

	// 1) Copy the block instructions into our storage
	u32 virt_addr, size;
	block->GetRange(virt_addr, size);
	u32 storage_index = mipsTracer.storage.cur_offset;

	auto mips_instructions_ptr = (const u32*)Memory::GetPointerUnchecked(virt_addr);

	if (!mipsTracer.storage.push_block(mips_instructions_ptr, size)) {
		// We ran out of storage! TODO: report that to the user
		mipsTracer.tracing_enabled = false;
		return;
	}
	// Successfully inserted the block at index 'possible_index'!

	mipsTracer.trace_info.push_back({ virt_addr, size, storage_index });

	// 2) Save the hash and the index

	u32 index = mipsTracer.trace_info.size() - 1;
	hash_to_index.emplace(hash, index);

	auto ir_ptr = (IRInst*)blocks.GetBlockInstructionPtr(*block);
	ir_ptr->constant = index;
}

bool MIPSTracer::flush_to_file() {
	// Do nothing for now
	return true;
}

MIPSTracer mipsTracer;



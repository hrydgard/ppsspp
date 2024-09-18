// Copyright (c) 2012- PPSSPP Project.

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

#include <cstring>

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Util/BlockAllocator.h"
#include "Core/Reporting.h"

// Slow freaking thing but works (eventually) :)

BlockAllocator::BlockAllocator(int grain) : bottom_(NULL), top_(NULL), grain_(grain)
{
}

BlockAllocator::~BlockAllocator()
{
	Shutdown();
}

void BlockAllocator::Init(u32 rangeStart, u32 rangeSize, bool suballoc) {
	Shutdown();
	rangeStart_ = rangeStart;
	rangeSize_ = rangeSize;
	//Initial block, covering everything
	top_ = new Block(rangeStart_, rangeSize_, false, NULL, NULL);
	bottom_ = top_;
	suballoc_ = suballoc;
}

void BlockAllocator::Shutdown()
{
	while (bottom_ != NULL)
	{
		Block *next = bottom_->next;
		delete bottom_;
		bottom_ = next;
	}
	top_ = NULL;
}

u32 BlockAllocator::AllocAligned(u32 &size, u32 sizeGrain, u32 grain, bool fromTop, const char *tag)
{
	// Sanity check
	if (size == 0 || size > rangeSize_) {
		ERROR_LOG(Log::sceKernel, "Clearly bogus size: %08x - failing allocation", size);
		return -1;
	}

	// It could be off step, but the grain should generally be a power of 2.
	if (grain < grain_)
		grain = grain_;
	if (sizeGrain < grain_)
		sizeGrain = grain_;

	// upalign size to grain
	size = (size + sizeGrain - 1) & ~(sizeGrain - 1);

	if (!fromTop)
	{
		//Allocate from bottom of mem
		for (Block *bp = bottom_; bp != NULL; bp = bp->next)
		{
			Block &b = *bp;
			u32 offset = b.start % grain;
			if (offset != 0)
				offset = grain - offset;
			u32 needed = offset + size;
			if (b.taken == false && b.size >= needed)
			{
				if (b.size == needed)
				{
					if (offset >= grain_)
						InsertFreeBefore(&b, offset);
					b.taken = true;
					b.SetAllocated(tag, suballoc_);
					return b.start;
				}
				else
				{
					InsertFreeAfter(&b, b.size - needed);
					if (offset >= grain_)
						InsertFreeBefore(&b, offset);
					b.taken = true;
					b.SetAllocated(tag, suballoc_);
					return b.start;
				}
			}
		}
	}
	else
	{
		// Allocate from top of mem.
		for (Block *bp = top_; bp != NULL; bp = bp->prev)
		{
			Block &b = *bp;
			u32 offset = (b.start + b.size - size) % grain;
			u32 needed = offset + size;
			if (b.taken == false && b.size >= needed)
			{
				if (b.size == needed)
				{
					if (offset >= grain_)
						InsertFreeAfter(&b, offset);
					b.taken = true;
					b.SetAllocated(tag, suballoc_);
					return b.start;
				}
				else
				{
					InsertFreeBefore(&b, b.size - needed);
					if (offset >= grain_)
						InsertFreeAfter(&b, offset);
					b.taken = true;
					b.SetAllocated(tag, suballoc_);
					return b.start;
				}
			}
		}
	}

	//Out of memory :(
	ListBlocks();
	ERROR_LOG(Log::sceKernel, "Block Allocator (%08x-%08x) failed to allocate %i (%08x) bytes of contiguous memory", rangeStart_, rangeStart_ + rangeSize_, size, size);
	return -1;
}

u32 BlockAllocator::Alloc(u32 &size, bool fromTop, const char *tag)
{
	// We want to make sure it's aligned in case AllocAt() was used.
	return AllocAligned(size, grain_, grain_, fromTop, tag);
}

u32 BlockAllocator::AllocAt(u32 position, u32 size, const char *tag)
{
	CheckBlocks();
	if (size > rangeSize_) {
		ERROR_LOG(Log::sceKernel, "Clearly bogus size: %08x - failing allocation", size);
		return -1;
	}

	// Downalign the position so we're allocating full blocks.
	u32 alignedPosition = position;
	u32 alignedSize = size;
	if (position & (grain_ - 1)) {
		DEBUG_LOG(Log::sceKernel, "Position %08x does not align to grain.", position);
		alignedPosition &= ~(grain_ - 1);

		// Since the position was decreased, size must increase.
		alignedSize += position - alignedPosition;
	}

	// Upalign size to grain.
	alignedSize = (alignedSize + grain_ - 1) & ~(grain_ - 1);
	// Tell the caller the allocated size from their requested starting position.
	size = alignedSize - (position - alignedPosition);

	Block *bp = GetBlockFromAddress(alignedPosition);
	if (bp != NULL)
	{
		Block &b = *bp;
		if (b.taken)
		{
			ERROR_LOG(Log::sceKernel, "Block allocator AllocAt failed, block taken! %08x, %i", position, size);
			return -1;
		}
		else
		{
			// Make sure the block is big enough to split.
			if (b.start + b.size < alignedPosition + alignedSize)
			{
				ERROR_LOG(Log::sceKernel, "Block allocator AllocAt failed, not enough contiguous space %08x, %i", position, size);
				return -1;
			}
			//good to go
			else if (b.start == alignedPosition)
			{
				if (b.size != alignedSize)
					InsertFreeAfter(&b, b.size - alignedSize);
				b.taken = true;
				b.SetAllocated(tag, suballoc_);
				CheckBlocks();
				return position;
			}
			else
			{
				InsertFreeBefore(&b, alignedPosition - b.start);
				if (b.size > alignedSize)
					InsertFreeAfter(&b, b.size - alignedSize);
				b.taken = true;
				b.SetAllocated(tag, suballoc_);

				return position;
			}
		}
	}
	else
	{
		ERROR_LOG(Log::sceKernel, "Block allocator AllocAt failed :( %08x, %i", position, size);
	}


	//Out of memory :(
	ListBlocks();
	ERROR_LOG(Log::sceKernel, "Block Allocator (%08x-%08x) failed to allocate %i (%08x) bytes of contiguous memory", rangeStart_, rangeStart_ + rangeSize_, alignedSize, alignedSize);
	return -1;
}

void BlockAllocator::MergeFreeBlocks(Block *fromBlock)
{
	DEBUG_LOG(Log::sceKernel, "Merging Blocks");

	Block *prev = fromBlock->prev;
	while (prev != NULL && prev->taken == false)
	{
		DEBUG_LOG(Log::sceKernel, "Block Alloc found adjacent free blocks - merging");
		prev->size += fromBlock->size;
		if (fromBlock->next == NULL)
			top_ = prev;
		else
			fromBlock->next->prev = prev;
		prev->next = fromBlock->next;
		delete fromBlock;
		fromBlock = prev;
		prev = fromBlock->prev;
	}

	if (prev == NULL)
		bottom_ = fromBlock;
	else
		prev->next = fromBlock;

	Block *next = fromBlock->next;
	while (next != NULL && next->taken == false)
	{
		DEBUG_LOG(Log::sceKernel, "Block Alloc found adjacent free blocks - merging");
		fromBlock->size += next->size;
		fromBlock->next = next->next;
		delete next;
		next = fromBlock->next;
	}

	if (next == NULL)
		top_ = fromBlock;
	else
		next->prev = fromBlock;
}

bool BlockAllocator::Free(u32 position)
{
	Block *b = GetBlockFromAddress(position);
	if (b && b->taken)
	{
		NotifyMemInfo(suballoc_ ? MemBlockFlags::SUB_FREE : MemBlockFlags::FREE, b->start, b->size, "");
		b->taken = false;
		MergeFreeBlocks(b);
		return true;
	}
	else
	{
		ERROR_LOG(Log::sceKernel, "BlockAllocator : invalid free %08x", position);
		return false;
	}
}

bool BlockAllocator::FreeExact(u32 position)
{
	Block *b = GetBlockFromAddress(position);
	if (b && b->taken && b->start == position)
	{
		NotifyMemInfo(suballoc_ ? MemBlockFlags::SUB_FREE : MemBlockFlags::FREE, b->start, b->size, "");
		b->taken = false;
		MergeFreeBlocks(b);
		return true;
	}
	else
	{
		ERROR_LOG(Log::sceKernel, "BlockAllocator : invalid free %08x", position);
		return false;
	}
}

BlockAllocator::Block *BlockAllocator::InsertFreeBefore(Block *b, u32 size)
{
	Block *inserted = new Block(b->start, size, false, b->prev, b);
	b->prev = inserted;
	if (inserted->prev == NULL)
		bottom_ = inserted;
	else
		inserted->prev->next = inserted;

	b->start += size;
	b->size -= size;
	return inserted;
}

BlockAllocator::Block *BlockAllocator::InsertFreeAfter(Block *b, u32 size)
{
	Block *inserted = new Block(b->start + b->size - size, size, false, b, b->next);
	b->next = inserted;
	if (inserted->next == NULL)
		top_ = inserted;
	else
		inserted->next->prev = inserted;

	b->size -= size;
	return inserted;
}

void BlockAllocator::CheckBlocks() const
{
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (b.start > 0xc0000000) {  // probably free'd debug values
			ERROR_LOG_REPORT(Log::HLE, "Bogus block in allocator");
		}
		// Outside the valid range, probably logic bug in allocation.
		if (b.start + b.size > rangeStart_ + rangeSize_ || b.start < rangeStart_) {
			ERROR_LOG_REPORT(Log::HLE, "Bogus block in allocator");
		}
	}
}

const char *BlockAllocator::GetBlockTag(u32 addr) const {
	const Block *b = GetBlockFromAddress(addr);
	return b->tag;
}

inline BlockAllocator::Block *BlockAllocator::GetBlockFromAddress(u32 addr)
{
	for (Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		Block &b = *bp;
		if (b.start <= addr && b.start + b.size > addr)
		{
			// Got one!
			return bp;
		}
	}
	return NULL;
}

const BlockAllocator::Block *BlockAllocator::GetBlockFromAddress(u32 addr) const
{
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (b.start <= addr && b.start + b.size > addr)
		{
			// Got one!
			return bp;
		}
	}
	return NULL;
}

u32 BlockAllocator::GetBlockStartFromAddress(u32 addr) const
{
	const Block *b = GetBlockFromAddress(addr);
	if (b)
		return b->start;
	else
		return -1;
}

u32 BlockAllocator::GetBlockSizeFromAddress(u32 addr) const
{
	const Block *b = GetBlockFromAddress(addr);
	if (b)
		return b->size;
	else
		return -1;
}

void BlockAllocator::ListBlocks() const
{
	DEBUG_LOG(Log::sceKernel,"-----------");
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		DEBUG_LOG(Log::sceKernel, "Block: %08x - %08x size %08x taken=%i tag=%s", b.start, b.start+b.size, b.size, b.taken ? 1:0, b.tag);
	}
	DEBUG_LOG(Log::sceKernel,"-----------");
}

u32 BlockAllocator::GetLargestFreeBlockSize() const
{
	u32 maxFreeBlock = 0;
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (!b.taken)
		{
			if (b.size > maxFreeBlock)
				maxFreeBlock = b.size;
		}
	}
	if (maxFreeBlock & (grain_ - 1))
		WARN_LOG_REPORT(Log::HLE, "GetLargestFreeBlockSize: free size %08x does not align to grain %08x.", maxFreeBlock, grain_);
	return maxFreeBlock;
}

u32 BlockAllocator::GetTotalFreeBytes() const
{
	u32 sum = 0;
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (!b.taken)
		{
			sum += b.size;
		}
	}
	if (sum & (grain_ - 1))
		WARN_LOG_REPORT(Log::HLE, "GetTotalFreeBytes: free size %08x does not align to grain %08x.", sum, grain_);
	return sum;
}

void BlockAllocator::DoState(PointerWrap &p)
{
	auto s = p.Section("BlockAllocator", 1);
	if (!s)
		return;

	int count = 0;

	if (p.mode == p.MODE_READ)
	{
		Shutdown();
		Do(p, count);

		bottom_ = new Block(0, 0, false, NULL, NULL);
		bottom_->DoState(p);
		--count;

		top_ = bottom_;
		for (int i = 0; i < count; ++i)
		{
			top_->next = new Block(0, 0, false, top_, NULL);
			top_->next->DoState(p);
			top_ = top_->next;
		}
	}
	else
	{
		_assert_(bottom_ != nullptr);
		for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
			++count;
		Do(p, count);

		bottom_->DoState(p);
		--count;

		Block *last = bottom_;
		for (int i = 0; i < count; ++i)
		{
			last->next->DoState(p);
			last = last->next;
		}
	}

	Do(p, rangeStart_);
	Do(p, rangeSize_);
	Do(p, grain_);
}

BlockAllocator::Block::Block(u32 _start, u32 _size, bool _taken, Block *_prev, Block *_next)
: start(_start), size(_size), taken(_taken), prev(_prev), next(_next)
{
	truncate_cpy(tag, "(untitled)");
}

void BlockAllocator::Block::SetAllocated(const char *_tag, bool suballoc) {
	NotifyMemInfo(suballoc ? MemBlockFlags::SUB_ALLOC : MemBlockFlags::ALLOC, start, size, _tag ? _tag : "");
	if (_tag)
		truncate_cpy(tag, _tag);
	else
		truncate_cpy(tag, "---");
}

void BlockAllocator::Block::DoState(PointerWrap &p)
{
	auto s = p.Section("Block", 1);
	if (!s)
		return;

	Do(p, start);
	Do(p, size);
	Do(p, taken);
	// Since we use truncate_cpy, the empty space is not zeroed.  Zero it now.
	// This avoids saving uninitialized memory.
	size_t tagLen = strlen(tag);
	if (tagLen != sizeof(tag))
		memset(tag + tagLen, 0, sizeof(tag) - tagLen);
	DoArray(p, tag, sizeof(tag));
}

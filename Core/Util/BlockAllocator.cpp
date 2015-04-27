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
#include "Common/ChunkFile.h"
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

void BlockAllocator::Init(u32 rangeStart, u32 rangeSize)
{
	Shutdown();
	rangeStart_ = rangeStart;
	rangeSize_ = rangeSize;
	//Initial block, covering everything
	top_ = new Block(rangeStart_, rangeSize_, false, NULL, NULL);
	bottom_ = top_;
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
		ERROR_LOG(HLE, "Clearly bogus size: %08x - failing allocation", size);
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
					b.taken = true;
					b.SetTag(tag);
					return b.start + offset;
				}
				else
				{
					InsertFreeAfter(&b, b.start + needed, b.size - needed);
					b.taken = true;
					b.size = needed;
					b.SetTag(tag);
					return b.start + offset;
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
					b.taken = true;
					b.SetTag(tag);
					return b.start;
				}
				else
				{
					InsertFreeBefore(&b, b.start, b.size - needed);
					b.taken = true;
					b.start += b.size - needed;
					b.size = needed;
					b.SetTag(tag);
					return b.start;
				}
			}
		}
	}

	//Out of memory :(
	ListBlocks();
	ERROR_LOG(HLE, "Block Allocator failed to allocate %i (%08x) bytes of contiguous memory", size, size);
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
		ERROR_LOG(HLE, "Clearly bogus size: %08x - failing allocation", size);
		return -1;
	}
	
	// Downalign the position so we're allocating full blocks.
	u32 alignedPosition = position;
	u32 alignedSize = size;
	if (position & (grain_ - 1)) {
		DEBUG_LOG(HLE, "Position %08x does not align to grain.", position);
		alignedPosition &= ~(grain_ - 1);

		// Since the position was decreased, size must increase.
		alignedSize += alignedPosition - position;
	}

	// Upalign size to grain.
	alignedSize = (alignedSize + grain_ - 1) & ~(grain_ - 1);
	// Tell the caller the allocated size from their requested starting position.
	size = alignedSize - (alignedPosition - position);

	Block *bp = GetBlockFromAddress(alignedPosition);
	if (bp != NULL)
	{
		Block &b = *bp;
		if (b.taken)
		{
			ERROR_LOG(HLE, "Block allocator AllocAt failed, block taken! %08x, %i", position, size);
			return -1;
		}
		else
		{
			// Make sure the block is big enough to split.
			if (b.start + b.size < alignedPosition + alignedSize)
			{
				ERROR_LOG(HLE, "Block allocator AllocAt failed, not enough contiguous space %08x, %i", position, size);
				return -1;
			}
			//good to go
			else if (b.start == alignedPosition)
			{
				InsertFreeAfter(&b, b.start + alignedSize, b.size - alignedSize);
				b.taken = true;
				b.size = alignedSize;
				b.SetTag(tag);
				CheckBlocks();
				return position;
			}
			else
			{
				int size1 = alignedPosition - b.start;
				InsertFreeBefore(&b, b.start, size1);
				if (b.start + b.size > alignedPosition + alignedSize)
					InsertFreeAfter(&b, alignedPosition + alignedSize, b.size - (alignedSize + size1));
				b.taken = true;
				b.start = alignedPosition;
				b.size = alignedSize;
				b.SetTag(tag);

				return position;
			}
		}
	}
	else
	{
		ERROR_LOG(HLE, "Block allocator AllocAt failed :( %08x, %i", position, size);
	}

	
	//Out of memory :(
	ListBlocks();
	ERROR_LOG(HLE, "Block Allocator failed to allocate %i bytes of contiguous memory", alignedSize);
	return -1;
}

void BlockAllocator::MergeFreeBlocks(Block *fromBlock)
{
	DEBUG_LOG(HLE, "Merging Blocks");

	Block *prev = fromBlock->prev;
	while (prev != NULL && prev->taken == false)
	{
		DEBUG_LOG(HLE, "Block Alloc found adjacent free blocks - merging");
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
		DEBUG_LOG(HLE, "Block Alloc found adjacent free blocks - merging");
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
		b->taken = false;
		MergeFreeBlocks(b);
		return true;
	}
	else
	{
		ERROR_LOG(HLE, "BlockAllocator : invalid free %08x", position);
		return false;
	}
}

bool BlockAllocator::FreeExact(u32 position)
{
	Block *b = GetBlockFromAddress(position);
	if (b && b->taken && b->start == position)
	{
		b->taken = false;
		MergeFreeBlocks(b);
		return true;
	}
	else
	{
		ERROR_LOG(HLE, "BlockAllocator : invalid free %08x", position);
		return false;
	}
}

BlockAllocator::Block *BlockAllocator::InsertFreeBefore(Block *b, u32 start, u32 size)
{
	Block *inserted = new Block(start, size, false, b->prev, b);
	b->prev = inserted;
	if (inserted->prev == NULL)
		bottom_ = inserted;
	else
		inserted->prev->next = inserted;

	return inserted;
}

BlockAllocator::Block *BlockAllocator::InsertFreeAfter(Block *b, u32 start, u32 size)
{
	Block *inserted = new Block(start, size, false, b, b->next);
	b->next = inserted;
	if (inserted->next == NULL)
		top_ = inserted;
	else
		inserted->next->prev = inserted;

	return inserted;
}

void BlockAllocator::CheckBlocks() const
{
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		if (b.start > 0xc0000000) {  // probably free'd debug values
			ERROR_LOG_REPORT(HLE, "Bogus block in allocator");
		}
		// Outside the valid range, probably logic bug in allocation.
		if (b.start + b.size > rangeStart_ + rangeSize_ || b.start < rangeStart_) {
			ERROR_LOG_REPORT(HLE, "Bogus block in allocator");
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
	INFO_LOG(HLE,"-----------");
	for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
	{
		const Block &b = *bp;
		INFO_LOG(HLE, "Block: %08x - %08x size %08x taken=%i tag=%s", b.start, b.start+b.size, b.size, b.taken ? 1:0, b.tag);
	}
	INFO_LOG(HLE,"-----------");
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
		WARN_LOG_REPORT(HLE, "GetLargestFreeBlockSize: free size %08x does not align to grain %08x.", maxFreeBlock, grain_);
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
		WARN_LOG_REPORT(HLE, "GetTotalFreeBytes: free size %08x does not align to grain %08x.", sum, grain_);
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
		p.Do(count);

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
		for (const Block *bp = bottom_; bp != NULL; bp = bp->next)
			++count;
		p.Do(count);

		bottom_->DoState(p);
		--count;

		Block *last = bottom_;
		for (int i = 0; i < count; ++i)
		{
			last->next->DoState(p);
			last = last->next;
		}
	}

	p.Do(rangeStart_);
	p.Do(rangeSize_);
	p.Do(grain_);
}

BlockAllocator::Block::Block(u32 _start, u32 _size, bool _taken, Block *_prev, Block *_next)
: start(_start), size(_size), taken(_taken), prev(_prev), next(_next)
{
	strcpy(tag, "(untitled)");
}

void BlockAllocator::Block::SetTag(const char *_tag)
{
	if (_tag)
		strncpy(tag, _tag, 32);
	else
		strncpy(tag, "---", 32);
	tag[31] = 0;
}

void BlockAllocator::Block::DoState(PointerWrap &p)
{
	auto s = p.Section("Block", 1);
	if (!s)
		return;

	p.Do(start);
	p.Do(size);
	p.Do(taken);
	p.DoArray(tag, sizeof(tag));
}

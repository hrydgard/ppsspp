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

#include "Log.h"
#include "BlockAllocator.h"
#include "ChunkFile.h"

// Slow freaking thing but works (eventually) :)

BlockAllocator::BlockAllocator(int grain) : grain_(grain)
{
	blocks.clear();
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
	blocks.push_back(Block(rangeStart_, rangeSize_, false));
}

void BlockAllocator::Shutdown()
{
	blocks.clear();
}

u32 BlockAllocator::AllocAligned(u32 &size, u32 grain, bool fromTop, const char *tag)
{
	// Sanity check
	if (size == 0 || size > rangeSize_) {
		ERROR_LOG(HLE, "Clearly bogus size: %08x - failing allocation", size);
		return -1;
	}

	// It could be off step, but the grain should generally be a power of 2.
	if (grain < grain_)
		grain = grain_;

	// upalign size to grain
	size = (size + grain - 1) & ~(grain - 1);

	if (!fromTop)
	{
		//Allocate from bottom of mem
		for (std::list<Block>::iterator iter = blocks.begin(); iter != blocks.end(); iter++)
		{
			BlockAllocator::Block &b = *iter;
			u32 offset = b.start % grain;
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
					blocks.insert(++iter, Block(b.start + needed, b.size - needed, false));
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
		for (std::list<Block>::reverse_iterator iter = blocks.rbegin(); iter != blocks.rend(); ++iter)
		{
			std::list<Block>::reverse_iterator hey = iter;
			BlockAllocator::Block &b = *((++hey).base()); //yes, confusing syntax. reverse_iterators are confusing
			u32 offset = b.start % grain;
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
					blocks.insert(hey.base(), Block(b.start, b.size - needed, false));
					b.taken = true;
					b.start += b.size - needed;
					b.size = needed;
					b.SetTag(tag);
					return b.start + offset;
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
	return AllocAligned(size, grain_, fromTop, tag);
}

u32 BlockAllocator::AllocAt(u32 position, u32 size, const char *tag)
{
	CheckBlocks();
	if (size > rangeSize_) {
		ERROR_LOG(HLE, "Clearly bogus size: %08x - failing allocation", size);
		return -1;
	}

	// upalign size to grain
	size = (size + grain_ - 1) & ~(grain_ - 1);
	
	// check that position is aligned
	if (position & (grain_ - 1)) {
		ERROR_LOG(HLE, "Position %08x does not align to grain. Grain will be off.", position);
	}

	std::list<Block>::iterator iter = GetBlockIterFromAddress(position);
	if (iter != blocks.end())
	{
		Block &b = *iter;
		if (b.taken)
		{
			ERROR_LOG(HLE, "Block allocator AllocAt failed, block taken! %08x, %i", position, size);
			return -1;
		}
		else
		{
			//good to go
			if (b.start == position)
			{
				blocks.insert(++iter, Block(b.start + size, b.size - size, false));
				b.taken = true;
				b.size = size;
				b.SetTag(tag);
				CheckBlocks();
				return position;
			}
			else
			{
				int size1 = position - b.start;
				blocks.insert(iter, Block(b.start, size1, false));
				if (b.start + b.size > position + size)
				{
					iter++;
					blocks.insert(iter, Block(position + size, b.size - (size + size1), false));
				}
				b.taken = true;
				b.start = position;
				b.size = size;
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
	ERROR_LOG(HLE, "Block Allocator failed to allocate %i bytes of contiguous memory", size);
	return -1;
}

void BlockAllocator::MergeFreeBlocks()
{
restart:
	DEBUG_LOG(HLE, "Merging Blocks");
	std::list<Block>::iterator iter1, iter2;
	iter1 = blocks.begin();
	iter2 = blocks.begin();
	iter2++;
	while (iter2 != blocks.end())
	{
		BlockAllocator::Block &b1 = *iter1;
		BlockAllocator::Block &b2 = *iter2;
			
		if (b1.taken == false && b2.taken == false)
		{
			DEBUG_LOG(HLE, "Block Alloc found adjacent free blocks - merging");
			b1.size += b2.size;
			blocks.erase(iter2);
			CheckBlocks();
			goto restart; //iterators now invalid - we have to restart our search
		}
		iter1++;
		iter2++;
	}
}

bool BlockAllocator::Free(u32 position)
{
	BlockAllocator::Block *b = GetBlockFromAddress(position);
	if (b && b->taken)
	{
		b->taken = false;
		MergeFreeBlocks();
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
	BlockAllocator::Block *b = GetBlockFromAddress(position);
	if (b && b->taken && b->start == position)
		return Free(position);
	else
	{
		ERROR_LOG(HLE, "BlockAllocator : invalid free %08x", position);
		return false;
	}
}

void BlockAllocator::CheckBlocks()
{
	for (std::list<Block>::iterator iter = blocks.begin(); iter != blocks.end(); iter++)
	{
		BlockAllocator::Block &b = *iter;
		if (b.start > 0xc0000000) {  // probably free'd debug values
			ERROR_LOG(HLE, "Bogus block in allocator");
		}
	}
}

std::list<BlockAllocator::Block>::iterator BlockAllocator::GetBlockIterFromAddress(u32 addr)
{
	for (std::list<Block>::iterator iter = blocks.begin(); iter != blocks.end(); iter++)
	{
		BlockAllocator::Block &b = *iter;
		if (b.start <= addr && b.start+b.size > addr)
		{
			// Got one!
			return iter;
		}
	}
	return blocks.end();
}

BlockAllocator::Block *BlockAllocator::GetBlockFromAddress(u32 addr) 
{
	std::list<Block>::iterator iter = GetBlockIterFromAddress(addr);
	if (iter == blocks.end())
		return 0;
	else
		return &(*iter);
}

u32 BlockAllocator::GetBlockStartFromAddress(u32 addr) 
{
	Block *b = GetBlockFromAddress(addr);
	if (b)
		return b->start;
	else
		return -1;
}

u32 BlockAllocator::GetBlockSizeFromAddress(u32 addr) 
{
	Block *b = GetBlockFromAddress(addr);
	if (b)
		return b->size;
	else
		return -1;
}

void BlockAllocator::ListBlocks()
{
	INFO_LOG(HLE,"-----------");
	for (std::list<Block>::const_iterator iter = blocks.begin(); iter != blocks.end(); iter++)
	{
		const Block &b = *iter;
		INFO_LOG(HLE, "Block: %08x - %08x	size %08x	taken=%i	tag=%s", b.start, b.start+b.size, b.size, b.taken ? 1:0, b.tag);
	}
}

u32 BlockAllocator::GetLargestFreeBlockSize()
{
	u32 maxFreeBlock = 0;
	for (std::list<Block>::const_iterator iter = blocks.begin(); iter != blocks.end(); iter++)
	{
		const Block &b = *iter;
		if (!b.taken)
		{
			if (b.size > maxFreeBlock)
				maxFreeBlock = b.size;
		}
	}
	return maxFreeBlock;
}

u32 BlockAllocator::GetTotalFreeBytes()
{
	u32 sum = 0;
	for (std::list<Block>::const_iterator iter = blocks.begin(); iter != blocks.end(); iter++)
	{
		const Block &b = *iter;
		if (!b.taken)
		{
			sum += b.size;
		}
	}
	return sum;
}

void BlockAllocator::DoState(PointerWrap &p)
{
	Block b(0, 0, false);
	p.Do(blocks, b);
	p.Do(rangeStart_);
	p.Do(rangeSize_);
	p.Do(grain_);
	p.DoMarker("BlockAllocator");
}

void BlockAllocator::Block::DoState(PointerWrap &p)
{
	p.Do(start);
	p.Do(size);
	p.Do(taken);
	p.DoArray(tag, sizeof(tag));
	p.DoMarker("Block");
}

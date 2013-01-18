
#pragma once

#include "../../Globals.h"
#include "../../Common/ChunkFile.h"

#include <vector>
#include <list>
#include <cstring>


// Generic allocator thingy
// Allocates blocks from a range

class BlockAllocator
{
public:
	BlockAllocator(int grain = 16);  // 16 byte granularity by default.
	~BlockAllocator();

	void Init(u32 _rangeStart, u32 _rangeSize);
	void Shutdown();

	void ListBlocks();

	// WARNING: size can be modified upwards!
	u32 Alloc(u32 &size, bool fromTop = false, const char *tag = 0);
	u32 AllocAligned(u32 &size, u32 grain, bool fromTop = false, const char *tag = 0);
	u32 AllocAt(u32 position, u32 size, const char *tag = 0);

	bool Free(u32 position);
	bool FreeExact(u32 position);
	bool IsBlockFree(u32 position) {
		Block *b = GetBlockFromAddress(position);
		if (b)
			return !b->taken;
		else
			return false;
	}

	void MergeFreeBlocks();

	u32 GetBlockStartFromAddress(u32 addr);
	u32 GetBlockSizeFromAddress(u32 addr);
	u32 GetLargestFreeBlockSize();
	u32 GetTotalFreeBytes();

	void DoState(PointerWrap &p);

private:
	void CheckBlocks();

	struct Block
	{
		Block(u32 _start, u32 _size, bool _taken) : start(_start), size(_size), taken(_taken)
		{
			strcpy(tag, "(untitled)");
		}
		void SetTag(const char *_tag) {
			if (_tag)
				strncpy(tag, _tag, 32);
			else
				strncpy(tag, "---", 32);
			tag[31] = 0;
		}
		u32 start;
		u32 size;
		bool taken;
		char tag[32];
	};

	std::list<Block> blocks;
	u32 rangeStart_;
	u32 rangeSize_;

	u32 grain_;

	Block *GetBlockFromAddress(u32 addr);
	std::list<Block>::iterator GetBlockIterFromAddress(u32 addr);
};

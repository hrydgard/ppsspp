// Copyright (c) 2021- PPSSPP Project.

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

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MIPS/MIPS.h"

class MemSlabMap {
public:
	MemSlabMap();
	~MemSlabMap();

	bool Mark(uint32_t addr, uint32_t size, uint32_t pc, bool allocated, const std::string &tag);
	bool Find(MemBlockFlags flags, uint32_t addr, uint32_t size, std::vector<MemBlockInfo> &results);
	void Reset();
	void DoState(PointerWrap &p);

private:
	struct Slab {
		uint32_t start = 0;
		uint32_t size = 0;
		uint64_t ticks = 0;
		uint32_t pc = 0;
		bool allocated = false;
		std::string tag;
		Slab *prev = nullptr;
		Slab *next = nullptr;

		void DoState(PointerWrap &p);
	};

	Slab *FindSlab(uint32_t addr);
	void Clear();
	// Returns the new slab after size.
	Slab *Split(Slab *slab, uint32_t size);
	void MergeAdjacent(Slab *slab);
	bool Same(const Slab *a, const Slab *b) const;
	void Merge(Slab *a, Slab *b);

	Slab *first_ = nullptr;
};

static MemSlabMap allocMap;
static MemSlabMap suballocMap;
static MemSlabMap writeMap;
static MemSlabMap textureMap;

MemSlabMap::MemSlabMap() {
	Reset();
}

MemSlabMap::~MemSlabMap() {
	Clear();
}

bool MemSlabMap::Mark(uint32_t addr, uint32_t size, uint32_t pc, bool allocated, const std::string &tag) {
	uint32_t end = addr + size;
	Slab *slab = FindSlab(addr);
	Slab *firstMatch = nullptr;
	while (slab != nullptr && slab->start < end) {
		if (slab->start < addr)
			slab = Split(slab, addr - slab->start);
		// Don't replace slab, the return is the after part.
		if (slab->start + slab->size > end) {
			Split(slab, end - slab->start);
		}

		slab->allocated = allocated;
		if (pc != 0) {
			slab->ticks = CoreTiming::GetTicks();
			slab->pc = pc;
		}
		if (!tag.empty())
			slab->tag = tag;

		// Move on to the next one.
		if (firstMatch != nullptr)
			firstMatch = slab;
		slab = slab->next;
	}

	if (firstMatch != nullptr) {
		// This will merge all those blocks to one.
		MergeAdjacent(firstMatch);
		return true;
	}
	return false;
}

bool MemSlabMap::Find(MemBlockFlags flags, uint32_t addr, uint32_t size, std::vector<MemBlockInfo> &results) {
	uint32_t end = addr + size;
	Slab *slab = FindSlab(addr);
	bool found = false;
	while (slab != nullptr && slab->start < end) {
		results.push_back({ flags, slab->start, slab->size, slab->pc, slab->tag, slab->allocated });
		found = true;
		slab = slab->next;
	}
	return found;
}

void MemSlabMap::Reset() {
	Clear();

	first_ = new Slab();
	first_->size = UINT_MAX;
}

void MemSlabMap::DoState(PointerWrap &p) {
	auto s = p.Section("MemSlabMap", 1);
	if (!s)
		return;

	int count = 0;
	if (p.mode == p.MODE_READ) {
		Clear();
		Do(p, count);

		first_ = new Slab();
		first_->DoState(p);
		first_->prev = nullptr;
		first_->next = nullptr;
		--count;

		Slab *slab = first_;
		for (int i = 0; i < count; ++i) {
			slab->next = new Slab();
			slab->DoState(p);

			slab->next->prev = slab;
			slab = slab->next;
		}
	} else {
		for (Slab *slab = first_; slab != nullptr; slab = slab->next)
			++count;
		Do(p, count);

		first_->DoState(p);
		--count;

		Slab *slab = first_;
		for (int i = 0; i < count; ++i) {
			slab->next->DoState(p);
			slab = slab->next;
		}
	}
}

void MemSlabMap::Slab::DoState(PointerWrap &p) {
	auto s = p.Section("MemSlabMapSlab", 1);
	if (!s)
		return;

	Do(p, start);
	Do(p, size);
	Do(p, ticks);
	Do(p, pc);
	Do(p, allocated);
	Do(p, tag);
}

void MemSlabMap::Clear() {
	Slab *s = first_;
	while (s != nullptr) {
		Slab *next = s->next;
		delete s;
		s = next;
	}
	first_ = nullptr;
}

MemSlabMap::Slab *MemSlabMap::FindSlab(uint32_t addr) {
	Slab *slab = first_;
	while (slab != nullptr && slab->start <= addr) {
		if (slab->start + slab->size > addr)
			return slab;
		slab = slab->next;
	}
	return nullptr;
}

MemSlabMap::Slab *MemSlabMap::Split(Slab *slab, uint32_t size) {
	Slab *next = new Slab();
	next->start = slab->start + size;
	next->size = slab->size - size;
	next->ticks = slab->ticks;
	next->pc = slab->pc;
	next->allocated = slab->allocated;
	next->tag = slab->tag;
	next->prev = slab;
	next->next = slab->next;

	slab->next = next;
	if (next->next)
		next->next->prev = next;

	slab->size = size;
	return next;
}

void MemSlabMap::MergeAdjacent(Slab *slab) {
	while (slab->next != nullptr && Same(slab, slab->next)) {
		Merge(slab, slab->next);
	}
	while (slab->prev != nullptr && Same(slab, slab->prev)) {
		Merge(slab, slab->prev);
	}
}

bool MemSlabMap::Same(const Slab *a, const Slab *b) const {
	if (a->allocated != b->allocated)
		return false;
	if (a->pc != b->pc)
		return false;
	if (a->tag != b->tag)
		return false;
	return true;
}

void MemSlabMap::Merge(Slab *a, Slab *b) {
	if (a->next == b) {
		_assert_(a->start + a->size == b->start);
		a->next = b->next;

		if (a->next)
			a->next->prev = a;
	} else if (a->prev == b) {
		_assert_(b->start + b->size == a->start);
		a->start = b->start;
		a->prev = b->prev;

		if (a->prev)
			a->prev->next = a;
		else if (first_ == b)
			first_ = a;
	} else {
		_assert_(false);
	}
	a->size += b->size;
	delete b;
}

void NotifyMemInfo(MemBlockFlags flags, uint32_t start, uint32_t size, const std::string &tag) {
	NotifyMemInfoPC(flags, start, size, currentMIPS->pc, tag);
}

void NotifyMemInfoPC(MemBlockFlags flags, uint32_t start, uint32_t size, uint32_t pc, const std::string &tag) {
	if (size == 0) {
		return;
	}
	// Clear the uncached and kernel bits.
	start &= ~0xC0000000;

	if (flags & MemBlockFlags::ALLOC) {
		allocMap.Mark(start, size, pc, true, tag);
	} else if (flags & MemBlockFlags::FREE) {
		// Maintain the previous allocation tag for debugging.
		allocMap.Mark(start, size, 0, false, "");
		suballocMap.Mark(start, size, 0, false, "");
	}
	if (flags & MemBlockFlags::SUB_ALLOC) {
		suballocMap.Mark(start, size, pc, true, tag);
	} else if (flags & MemBlockFlags::SUB_FREE) {
		// Maintain the previous allocation tag for debugging.
		suballocMap.Mark(start, size, 0, false, "");
	}
	if (flags & MemBlockFlags::TEXTURE) {
		textureMap.Mark(start, size, pc, true, tag);
	}
	if (flags & MemBlockFlags::WRITE) {
		CBreakPoints::ExecMemCheck(start, true, size, pc, tag);
		writeMap.Mark(start, size, pc, true, tag);
	} else if (flags & MemBlockFlags::READ) {
		CBreakPoints::ExecMemCheck(start, false, size, pc, tag);
	}
}

std::vector<MemBlockInfo> FindMemInfo(uint32_t start, uint32_t size) {
	std::vector<MemBlockInfo> results;
	allocMap.Find(MemBlockFlags::ALLOC, start, size, results);
	suballocMap.Find(MemBlockFlags::SUB_ALLOC, start, size, results);
	writeMap.Find(MemBlockFlags::WRITE, start, size, results);
	textureMap.Find(MemBlockFlags::TEXTURE, start, size, results);
	return results;
}

void MemBlockInfoInit() {
}

void MemBlockInfoShutdown() {
	allocMap.Reset();
	suballocMap.Reset();
	writeMap.Reset();
	textureMap.Reset();
}

void MemBlockInfoDoState(PointerWrap &p) {
	auto s = p.Section("MemBlockInfo", 0, 1);
	if (!s)
		return;

	allocMap.DoState(p);
	suballocMap.DoState(p);
	writeMap.DoState(p);
	textureMap.DoState(p);
}

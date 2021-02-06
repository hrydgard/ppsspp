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

	bool Mark(uint32_t addr, uint32_t size, uint64_t ticks, uint32_t pc, bool allocated, const std::string &tag);
	bool Find(MemBlockFlags flags, uint32_t addr, uint32_t size, std::vector<MemBlockInfo> &results);
	void Reset();
	void DoState(PointerWrap &p);

private:
	struct Slab {
		uint32_t start = 0;
		uint32_t end = 0;
		uint64_t ticks = 0;
		uint32_t pc = 0;
		bool allocated = false;
		std::string tag;
		Slab *prev = nullptr;
		Slab *next = nullptr;

		void DoState(PointerWrap &p);
	};

	static constexpr uint32_t MAX_SIZE = 0x40000000;
	static constexpr uint32_t SLICES = 16384;
	static constexpr uint32_t SLICE_SIZE = MAX_SIZE / SLICES;

	Slab *FindSlab(uint32_t addr);
	void Clear();
	// Returns the new slab after size.
	Slab *Split(Slab *slab, uint32_t size);
	void MergeAdjacent(Slab *slab);
	bool Same(const Slab *a, const Slab *b) const;
	void Merge(Slab *a, Slab *b);
	void FillHeads(Slab *slab);

	Slab *first_ = nullptr;
	std::vector<Slab *> heads_;
};

struct PendingNotifyMem {
	MemBlockFlags flags;
	uint32_t start;
	uint32_t size;
	uint64_t ticks;
	uint32_t pc;
	std::string tag;
};

static constexpr size_t MAX_PENDING_NOTIFIES = 4096;
static MemSlabMap allocMap;
static MemSlabMap suballocMap;
static MemSlabMap writeMap;
static MemSlabMap textureMap;
static std::vector<PendingNotifyMem> pendingNotifies;

MemSlabMap::MemSlabMap() {
	Reset();
}

MemSlabMap::~MemSlabMap() {
	Clear();
}

bool MemSlabMap::Mark(uint32_t addr, uint32_t size, uint64_t ticks, uint32_t pc, bool allocated, const std::string &tag) {
	uint32_t end = addr + size;
	Slab *slab = FindSlab(addr);
	Slab *firstMatch = nullptr;
	while (slab != nullptr && slab->start < end) {
		if (slab->start < addr)
			slab = Split(slab, addr - slab->start);
		// Don't replace slab, the return is the after part.
		if (slab->end > end) {
			Split(slab, end - slab->start);
		}

		slab->allocated = allocated;
		if (pc != 0) {
			slab->ticks = ticks;
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
		results.push_back({ flags, slab->start, slab->end - slab->start, slab->pc, slab->tag, slab->allocated });
		found = true;
		slab = slab->next;
	}
	return found;
}

void MemSlabMap::Reset() {
	Clear();

	first_ = new Slab();
	first_->end = MAX_SIZE;

	heads_.resize(SLICES, first_);
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
		--count;

		heads_.resize(SLICES, nullptr);
		FillHeads(first_);

		Slab *slab = first_;
		for (int i = 0; i < count; ++i) {
			slab->next = new Slab();
			slab->next->DoState(p);

			slab->next->prev = slab;
			slab = slab->next;

			FillHeads(slab);
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
	Do(p, end);
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
	heads_.clear();
}

MemSlabMap::Slab *MemSlabMap::FindSlab(uint32_t addr) {
	// Jump ahead using our index.
	Slab *slab = heads_[addr / SLICE_SIZE];
	while (slab != nullptr && slab->start <= addr) {
		if (slab->end > addr)
			return slab;
		slab = slab->next;
	}
	return nullptr;
}

MemSlabMap::Slab *MemSlabMap::Split(Slab *slab, uint32_t size) {
	Slab *next = new Slab();
	next->start = slab->start + size;
	next->end = slab->end;
	next->ticks = slab->ticks;
	next->pc = slab->pc;
	next->allocated = slab->allocated;
	next->tag = slab->tag;
	next->prev = slab;
	next->next = slab->next;

	slab->next = next;
	if (next->next)
		next->next->prev = next;

	// If the split is big, we might have to update our index.
	FillHeads(next);

	slab->end = slab->start + size;
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
		_assert_(a->end == b->start);
		a->end = b->end;
		a->next = b->next;

		if (a->next)
			a->next->prev = a;
	} else if (a->prev == b) {
		_assert_(b->end == a->start);
		a->start = b->start;
		a->prev = b->prev;

		if (a->prev)
			a->prev->next = a;
		else if (first_ == b)
			first_ = a;
	} else {
		_assert_(false);
	}
	// Take over index entries b had.
	FillHeads(a);
	delete b;
}

void MemSlabMap::FillHeads(Slab *slab) {
	uint32_t slice = slab->start / SLICE_SIZE;
	uint32_t endSlice = (slab->end - 1) / SLICE_SIZE;

	// For the first slice, only replace if it's the one we're removing.
	if (slab->start == slice * SLICE_SIZE) {
		heads_[slice] = slab;
	}

	// Now replace all the rest - we definitely cover the start of them.
	for (uint32_t i = slice + 1; i <= endSlice; ++i) {
		heads_[i] = slab;
	}
}

void FlushPendingMemInfo() {
	for (auto info : pendingNotifies) {
		if (info.flags & MemBlockFlags::ALLOC) {
			allocMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		} else if (info.flags & MemBlockFlags::FREE) {
			// Maintain the previous allocation tag for debugging.
			allocMap.Mark(info.start, info.size, info.ticks, 0, false, "");
			suballocMap.Mark(info.start, info.size, info.ticks, 0, false, "");
		}
		if (info.flags & MemBlockFlags::SUB_ALLOC) {
			suballocMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		} else if (info.flags & MemBlockFlags::SUB_FREE) {
			// Maintain the previous allocation tag for debugging.
			suballocMap.Mark(info.start, info.size, info.ticks, 0, false, "");
		}
		if (info.flags & MemBlockFlags::TEXTURE) {
			textureMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		}
		if (info.flags & MemBlockFlags::WRITE) {
			writeMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		}
	}
	pendingNotifies.clear();
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

	PendingNotifyMem info{ flags, start, size };
	info.ticks = CoreTiming::GetTicks();
	info.pc = pc;
	info.tag = tag;
	pendingNotifies.push_back(info);

	if (pendingNotifies.size() > MAX_PENDING_NOTIFIES) {
		FlushPendingMemInfo();
	}

	if (flags & MemBlockFlags::WRITE) {
		CBreakPoints::ExecMemCheck(start, true, size, pc, tag);
	} else if (flags & MemBlockFlags::READ) {
		CBreakPoints::ExecMemCheck(start, false, size, pc, tag);
	}
}

std::vector<MemBlockInfo> FindMemInfo(uint32_t start, uint32_t size) {
	FlushPendingMemInfo();

	std::vector<MemBlockInfo> results;
	allocMap.Find(MemBlockFlags::ALLOC, start, size, results);
	suballocMap.Find(MemBlockFlags::SUB_ALLOC, start, size, results);
	writeMap.Find(MemBlockFlags::WRITE, start, size, results);
	textureMap.Find(MemBlockFlags::TEXTURE, start, size, results);
	return results;
}

void MemBlockInfoInit() {
	pendingNotifies.reserve(MAX_PENDING_NOTIFIES);
}

void MemBlockInfoShutdown() {
	allocMap.Reset();
	suballocMap.Reset();
	writeMap.Reset();
	textureMap.Reset();
	pendingNotifies.clear();
}

void MemBlockInfoDoState(PointerWrap &p) {
	auto s = p.Section("MemBlockInfo", 0, 1);
	if (!s)
		return;

	FlushPendingMemInfo();
	allocMap.DoState(p);
	suballocMap.DoState(p);
	writeMap.DoState(p);
	textureMap.DoState(p);
}

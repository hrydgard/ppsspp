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

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MIPS/MIPS.h"
#include "Common/StringUtils.h"

class MemSlabMap {
public:
	MemSlabMap();
	~MemSlabMap();

	bool Mark(uint32_t addr, uint32_t size, uint64_t ticks, uint32_t pc, bool allocated, const char *tag);
	bool Find(MemBlockFlags flags, uint32_t addr, uint32_t size, std::vector<MemBlockInfo> &results);
	// Note that the returned pointer gets invalidated as soon as Mark is called.
	const char *FastFindWriteTag(MemBlockFlags flags, uint32_t addr, uint32_t size);
	void Reset();
	void DoState(PointerWrap &p);

private:
	struct Slab {
		uint32_t start = 0;
		uint32_t end = 0;
		uint64_t ticks = 0;
		uint32_t pc = 0;
		bool allocated = false;
		// Intentionally not save stated.
		bool bulkStorage = false;
		char tag[128]{};
		Slab *prev = nullptr;
		Slab *next = nullptr;

		void DoState(PointerWrap &p);
	};

	static constexpr uint32_t MAX_SIZE = 0x40000000;
	static constexpr uint32_t SLICES = 65536;
	static constexpr uint32_t SLICE_SIZE = MAX_SIZE / SLICES;

	Slab *FindSlab(uint32_t addr);
	void Clear();
	// Returns the new slab after size.
	Slab *Split(Slab *slab, uint32_t size);
	void MergeAdjacent(Slab *slab);
	static inline bool Same(const Slab *a, const Slab *b);
	void Merge(Slab *a, Slab *b);
	void FillHeads(Slab *slab);

	Slab *first_ = nullptr;
	Slab *lastFind_ = nullptr;
	std::vector<Slab *> heads_;
	Slab *bulkStorage_ = nullptr;
};

struct PendingNotifyMem {
	MemBlockFlags flags;
	uint32_t start;
	uint32_t size;
	uint32_t copySrc;
	uint64_t ticks;
	uint32_t pc;
	char tag[128];
};

// 160 KB.
static constexpr size_t MAX_PENDING_NOTIFIES = 1024;
static constexpr size_t MAX_PENDING_NOTIFIES_THREAD = 1000;
static MemSlabMap allocMap;
static MemSlabMap suballocMap;
static MemSlabMap writeMap;
static MemSlabMap textureMap;
static std::vector<PendingNotifyMem> pendingNotifies;
static std::atomic<uint32_t> pendingNotifyMinAddr1;
static std::atomic<uint32_t> pendingNotifyMaxAddr1;
static std::atomic<uint32_t> pendingNotifyMinAddr2;
static std::atomic<uint32_t> pendingNotifyMaxAddr2;
// To prevent deadlocks, acquire Read before Write if you're going to acquire both.
static std::mutex pendingWriteMutex;
static std::mutex pendingReadMutex;
static int detailedOverride;

static std::thread flushThread;
static std::atomic<bool> flushThreadRunning;
static std::atomic<bool> flushThreadPending;
static std::mutex flushLock;
static std::condition_variable flushCond;

MemSlabMap::MemSlabMap() {
	Reset();
}

MemSlabMap::~MemSlabMap() {
	Clear();
}

bool MemSlabMap::Mark(uint32_t addr, uint32_t size, uint64_t ticks, uint32_t pc, bool allocated, const char *tag) {
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
		if (tag)
			truncate_cpy(slab->tag, tag);

		// Move on to the next one.
		if (firstMatch == nullptr)
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
		if (slab->pc != 0 || slab->tag[0] != '\0') {
			results.push_back({ flags, slab->start, slab->end - slab->start, slab->ticks, slab->pc, slab->tag, slab->allocated });
			found = true;
		}
		slab = slab->next;
	}
	return found;
}

const char *MemSlabMap::FastFindWriteTag(MemBlockFlags flags, uint32_t addr, uint32_t size) {
	uint32_t end = addr + size;
	Slab *slab = FindSlab(addr);
	while (slab != nullptr && slab->start < end) {
		if (slab->pc != 0 || slab->tag[0] != '\0') {
			return slab->tag;
		}
		slab = slab->next;
	}
	return nullptr;
}

void MemSlabMap::Reset() {
	Clear();

	first_ = new Slab();
	first_->end = MAX_SIZE;
	lastFind_ = first_;

	heads_.resize(SLICES, first_);
}

void MemSlabMap::DoState(PointerWrap &p) {
	auto s = p.Section("MemSlabMap", 1);
	if (!s)
		return;

	int count = 0;
	if (p.mode == p.MODE_READ) {
		// Since heads_ is a static size, let's avoid clearing it.
		// This helps in case a debugger call happens concurrently.
		Slab *old = first_;
		Slab *oldBulk = bulkStorage_;
		Do(p, count);

		first_ = new Slab();
		first_->DoState(p);
		lastFind_ = first_;
		--count;

		FillHeads(first_);

		bulkStorage_ = new Slab[count];

		Slab *slab = first_;
		for (int i = 0; i < count; ++i) {
			slab->next = &bulkStorage_[i];
			slab->next->bulkStorage = true;
			slab->next->DoState(p);

			slab->next->prev = slab;
			slab = slab->next;

			FillHeads(slab);
		}

		// Now that it's entirely disconnected, delete the old slabs.
		while (old != nullptr) {
			Slab *next = old->next;
			if (!old->bulkStorage)
				delete old;
			old = next;
		}
		delete [] oldBulk;
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
	auto s = p.Section("MemSlabMapSlab", 1, 3);
	if (!s)
		return;

	Do(p, start);
	Do(p, end);
	Do(p, ticks);
	Do(p, pc);
	Do(p, allocated);
	if (s >= 3) {
		Do(p, tag);
	} else if (s >= 2) {
		char shortTag[32];
		Do(p, shortTag);
		memcpy(tag, shortTag, sizeof(shortTag));
	} else {
		std::string stringTag;
		Do(p, stringTag);
		truncate_cpy(tag, stringTag.c_str());
	}
}

void MemSlabMap::Clear() {
	Slab *s = first_;
	while (s != nullptr) {
		Slab *next = s->next;
		if (!s->bulkStorage)
			delete s;
		s = next;
	}
	delete bulkStorage_;
	bulkStorage_ = nullptr;
	first_ = nullptr;
	lastFind_ = nullptr;
	heads_.clear();
}

MemSlabMap::Slab *MemSlabMap::FindSlab(uint32_t addr) {
	// Jump ahead using our index.
	Slab *slab = heads_[addr / SLICE_SIZE];
	// We often move forward, so check the last find.
	if (lastFind_->start > slab->start && lastFind_->start <= addr)
		slab = lastFind_;

	while (slab != nullptr && slab->start <= addr) {
		if (slab->end > addr) {
			lastFind_ = slab;
			return slab;
		}
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
	truncate_cpy(next->tag, slab->tag);
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

bool MemSlabMap::Same(const Slab *a, const Slab *b) {
	if (a->allocated != b->allocated)
		return false;
	if (a->pc != b->pc)
		return false;
	if (strcmp(a->tag, b->tag))
		return false;
	return true;
}

void MemSlabMap::MergeAdjacent(Slab *slab) {
	while (slab->next != nullptr && Same(slab, slab->next)) {
		Merge(slab, slab->next);
	}
	while (slab->prev != nullptr && Same(slab, slab->prev)) {
		Merge(slab, slab->prev);
	}
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
	if (b->ticks > a->ticks) {
		a->ticks = b->ticks;
		// In case we ignore PC for same.
		a->pc = b->pc;
	}
	if (lastFind_ == b)
		lastFind_ = a;
	if (!b->bulkStorage)
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
	Slab **next = &heads_[slice + 1];
	// We want to set slice + 1 through endSlice, inclusive.
	size_t c = endSlice - slice;
	for (size_t i = 0; i < c; ++i) {
		next[i] = slab;
	}
}

size_t FormatMemWriteTagAtNoFlush(char *buf, size_t sz, const char *prefix, uint32_t start, uint32_t size);

void FlushPendingMemInfo() {
	// This lock prevents us from another thread reading while we're busy flushing.
	std::lock_guard<std::mutex> guard(pendingReadMutex);
	std::vector<PendingNotifyMem> thisBatch;
	{
		std::lock_guard<std::mutex> guard(pendingWriteMutex);
		thisBatch = std::move(pendingNotifies);
		pendingNotifies.clear();
		pendingNotifies.reserve(MAX_PENDING_NOTIFIES);

		pendingNotifyMinAddr1 = 0xFFFFFFFF;
		pendingNotifyMaxAddr1 = 0;
		pendingNotifyMinAddr2 = 0xFFFFFFFF;
		pendingNotifyMaxAddr2 = 0;
	}

	for (const auto &info : thisBatch) {
		if (info.copySrc != 0) {
			char tagData[128];
			size_t tagSize = FormatMemWriteTagAtNoFlush(tagData, sizeof(tagData), info.tag, info.copySrc, info.size);
			writeMap.Mark(info.start, info.size, info.ticks, info.pc, true, tagData);
			continue;
		}

		if (info.flags & MemBlockFlags::ALLOC) {
			allocMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		} else if (info.flags & MemBlockFlags::FREE) {
			// Maintain the previous allocation tag for debugging.
			allocMap.Mark(info.start, info.size, info.ticks, 0, false, nullptr);
			suballocMap.Mark(info.start, info.size, info.ticks, 0, false, nullptr);
		}
		if (info.flags & MemBlockFlags::SUB_ALLOC) {
			suballocMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		} else if (info.flags & MemBlockFlags::SUB_FREE) {
			// Maintain the previous allocation tag for debugging.
			suballocMap.Mark(info.start, info.size, info.ticks, 0, false, nullptr);
		}
		if (info.flags & MemBlockFlags::TEXTURE) {
			textureMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		}
		if (info.flags & MemBlockFlags::WRITE) {
			writeMap.Mark(info.start, info.size, info.ticks, info.pc, true, info.tag);
		}
	}
}

static inline uint32_t NormalizeAddress(uint32_t addr) {
	if ((addr & 0x3F000000) == 0x04000000)
		return addr & 0x041FFFFF;
	return addr & 0x3FFFFFFF;
}

static inline bool MergeRecentMemInfo(const PendingNotifyMem &info, size_t copyLength) {
	if (pendingNotifies.size() < 4)
		return false;

	for (size_t i = 1; i <= 4; ++i) {
		auto &prev = pendingNotifies[pendingNotifies.size() - i];
		if (prev.copySrc != 0)
			return false;

		if (prev.flags != info.flags)
			continue;

		if (prev.start >= info.start + info.size || prev.start + prev.size <= info.start)
			continue;

		// This means there's overlap, but not a match, so we can't combine any.
		if (prev.start != info.start || prev.size > info.size)
			return false;

		memcpy(prev.tag, info.tag, copyLength + 1);
		prev.size = info.size;
		prev.ticks = info.ticks;
		prev.pc = info.pc;
		return true;
	}

	return false;
}

void NotifyMemInfoPC(MemBlockFlags flags, uint32_t start, uint32_t size, uint32_t pc, const char *tagStr, size_t strLength) {
	if (size == 0) {
		return;
	}
	// Clear the uncached and kernel bits.
	start = NormalizeAddress(start);

	bool needFlush = false;
	// When the setting is off, we skip smaller info to keep things fast.
	if (MemBlockInfoDetailed(size) && flags != MemBlockFlags::READ) {
		PendingNotifyMem info{ flags, start, size };
		info.ticks = CoreTiming::GetTicks();
		info.pc = pc;

		size_t copyLength = strLength;
		if (copyLength >= sizeof(info.tag)) {
			copyLength = sizeof(info.tag) - 1;
		}
		memcpy(info.tag, tagStr, copyLength);
		info.tag[copyLength] = 0;

		std::lock_guard<std::mutex> guard(pendingWriteMutex);
		// Sometimes we get duplicates, quickly check.
		if (!MergeRecentMemInfo(info, copyLength)) {
			if (start < 0x08000000) {
				pendingNotifyMinAddr1 = std::min(pendingNotifyMinAddr1.load(), start);
				pendingNotifyMaxAddr1 = std::max(pendingNotifyMaxAddr1.load(), start + size);
			} else {
				pendingNotifyMinAddr2 = std::min(pendingNotifyMinAddr2.load(), start);
				pendingNotifyMaxAddr2 = std::max(pendingNotifyMaxAddr2.load(), start + size);
			}
			pendingNotifies.push_back(info);
		}
		needFlush = pendingNotifies.size() > MAX_PENDING_NOTIFIES_THREAD;
	}

	if (needFlush) {
		{
			std::lock_guard<std::mutex> guard(flushLock);
			flushThreadPending = true;
		}
		flushCond.notify_one();
	}

	if (!(flags & MemBlockFlags::SKIP_MEMCHECK)) {
		if (flags & MemBlockFlags::WRITE) {
			CBreakPoints::ExecMemCheck(start, true, size, pc, tagStr);
		} else if (flags & MemBlockFlags::READ) {
			CBreakPoints::ExecMemCheck(start, false, size, pc, tagStr);
		}
	}
}

void NotifyMemInfo(MemBlockFlags flags, uint32_t start, uint32_t size, const char *str, size_t strLength) {
	NotifyMemInfoPC(flags, start, size, currentMIPS->pc, str, strLength);
}

void NotifyMemInfoCopy(uint32_t destPtr, uint32_t srcPtr, uint32_t size, const char *prefix) {
	if (size == 0)
		return;

	bool needsFlush = false;
	if (CBreakPoints::HasMemChecks()) {
		// This will cause a flush, but it's needed to trigger memchecks with proper data.
		char tagData[128];
		size_t tagSize = FormatMemWriteTagAt(tagData, sizeof(tagData), prefix, srcPtr, size);
		NotifyMemInfo(MemBlockFlags::READ, srcPtr, size, tagData, tagSize);
		NotifyMemInfo(MemBlockFlags::WRITE, destPtr, size, tagData, tagSize);
	} else if (MemBlockInfoDetailed(size)) {
		srcPtr = NormalizeAddress(srcPtr);
		destPtr = NormalizeAddress(destPtr);

		PendingNotifyMem info{ MemBlockFlags::WRITE, destPtr, size };
		info.copySrc = srcPtr;
		info.ticks = CoreTiming::GetTicks();
		info.pc = currentMIPS->pc;

		// Store the prefix for now.  The correct tag will be calculated on flush.
		truncate_cpy(info.tag, prefix);

		std::lock_guard<std::mutex> guard(pendingWriteMutex);
		if (destPtr < 0x08000000) {
			pendingNotifyMinAddr1 = std::min(pendingNotifyMinAddr1.load(), destPtr);
			pendingNotifyMaxAddr1 = std::max(pendingNotifyMaxAddr1.load(), destPtr + size);
		} else {
			pendingNotifyMinAddr2 = std::min(pendingNotifyMinAddr2.load(), destPtr);
			pendingNotifyMaxAddr2 = std::max(pendingNotifyMaxAddr2.load(), destPtr + size);
		}
		pendingNotifies.push_back(info);
		needsFlush = pendingNotifies.size() > MAX_PENDING_NOTIFIES_THREAD;
	}

	if (needsFlush) {
		{
			std::lock_guard<std::mutex> guard(flushLock);
			flushThreadPending = true;
		}
		flushCond.notify_one();
	}
}

std::vector<MemBlockInfo> FindMemInfo(uint32_t start, uint32_t size) {
	start = NormalizeAddress(start);

	if (pendingNotifyMinAddr1 < start + size && pendingNotifyMaxAddr1 >= start)
		FlushPendingMemInfo();
	if (pendingNotifyMinAddr2 < start + size && pendingNotifyMaxAddr2 >= start)
		FlushPendingMemInfo();

	std::vector<MemBlockInfo> results;
	allocMap.Find(MemBlockFlags::ALLOC, start, size, results);
	suballocMap.Find(MemBlockFlags::SUB_ALLOC, start, size, results);
	writeMap.Find(MemBlockFlags::WRITE, start, size, results);
	textureMap.Find(MemBlockFlags::TEXTURE, start, size, results);
	return results;
}

std::vector<MemBlockInfo> FindMemInfoByFlag(MemBlockFlags flags, uint32_t start, uint32_t size) {
	start = NormalizeAddress(start);

	if (pendingNotifyMinAddr1 < start + size && pendingNotifyMaxAddr1 >= start)
		FlushPendingMemInfo();
	if (pendingNotifyMinAddr2 < start + size && pendingNotifyMaxAddr2 >= start)
		FlushPendingMemInfo();

	std::vector<MemBlockInfo> results;
	if (flags & MemBlockFlags::ALLOC)
		allocMap.Find(MemBlockFlags::ALLOC, start, size, results);
	if (flags & MemBlockFlags::SUB_ALLOC)
		suballocMap.Find(MemBlockFlags::SUB_ALLOC, start, size, results);
	if (flags & MemBlockFlags::WRITE)
		writeMap.Find(MemBlockFlags::WRITE, start, size, results);
	if (flags & MemBlockFlags::TEXTURE)
		textureMap.Find(MemBlockFlags::TEXTURE, start, size, results);
	return results;
}

static const char *FindWriteTagByFlag(MemBlockFlags flags, uint32_t start, uint32_t size, bool flush = true) {
	start = NormalizeAddress(start);

	if (flush) {
		if (pendingNotifyMinAddr1 < start + size && pendingNotifyMaxAddr1 >= start)
			FlushPendingMemInfo();
		if (pendingNotifyMinAddr2 < start + size && pendingNotifyMaxAddr2 >= start)
			FlushPendingMemInfo();
	}

	if (flags & MemBlockFlags::ALLOC) {
		const char *tag = allocMap.FastFindWriteTag(MemBlockFlags::ALLOC, start, size);
		if (tag)
			return tag;
	}
	if (flags & MemBlockFlags::SUB_ALLOC) {
		const char *tag = suballocMap.FastFindWriteTag(MemBlockFlags::SUB_ALLOC, start, size);
		if (tag)
			return tag;
	}
	if (flags & MemBlockFlags::WRITE) {
		const char *tag = writeMap.FastFindWriteTag(MemBlockFlags::WRITE, start, size);
		if (tag)
			return tag;
	}
	if (flags & MemBlockFlags::TEXTURE) {
		const char *tag = textureMap.FastFindWriteTag(MemBlockFlags::TEXTURE, start, size);
		if (tag)
			return tag;
	}
	return nullptr;
}

size_t FormatMemWriteTagAt(char *buf, size_t sz, const char *prefix, uint32_t start, uint32_t size) {
	const char *tag = FindWriteTagByFlag(MemBlockFlags::WRITE, start, size);
	if (tag && strcmp(tag, "MemInit") != 0) {
		return snprintf(buf, sz, "%s%s", prefix, tag);
	}
	// Fall back to alloc and texture, especially for VRAM.  We prefer write above.
	tag = FindWriteTagByFlag(MemBlockFlags::ALLOC | MemBlockFlags::TEXTURE, start, size);
	if (tag) {
		return snprintf(buf, sz, "%s%s", prefix, tag);
	}
	return snprintf(buf, sz, "%s%08x_size_%08x", prefix, start, size);
}

size_t FormatMemWriteTagAtNoFlush(char *buf, size_t sz, const char *prefix, uint32_t start, uint32_t size) {
	const char *tag = FindWriteTagByFlag(MemBlockFlags::WRITE, start, size, false);
	if (tag && strcmp(tag, "MemInit") != 0) {
		return snprintf(buf, sz, "%s%s", prefix, tag);
	}
	// Fall back to alloc and texture, especially for VRAM.  We prefer write above.
	tag = FindWriteTagByFlag(MemBlockFlags::ALLOC | MemBlockFlags::TEXTURE, start, size, false);
	if (tag) {
		return snprintf(buf, sz, "%s%s", prefix, tag);
	}
	return snprintf(buf, sz, "%s%08x_size_%08x", prefix, start, size);
}

static void FlushMemInfoThread() {
	SetCurrentThreadName("FlushMemInfo");

	while (flushThreadRunning.load()) {
		flushThreadPending = false;
		FlushPendingMemInfo();

		std::unique_lock<std::mutex> guard(flushLock);
		flushCond.wait(guard, [] {
			return flushThreadPending.load();
		});
	}
}

void MemBlockInfoInit() {
	std::lock_guard<std::mutex> guard(pendingReadMutex);
	std::lock_guard<std::mutex> guardW(pendingWriteMutex);
	pendingNotifies.reserve(MAX_PENDING_NOTIFIES);
	pendingNotifyMinAddr1 = 0xFFFFFFFF;
	pendingNotifyMaxAddr1 = 0;
	pendingNotifyMinAddr2 = 0xFFFFFFFF;
	pendingNotifyMaxAddr2 = 0;

	flushThreadRunning = true;
	flushThreadPending = false;
	flushThread = std::thread(&FlushMemInfoThread);
}

void MemBlockInfoShutdown() {
	{
		std::lock_guard<std::mutex> guard(pendingReadMutex);
		std::lock_guard<std::mutex> guardW(pendingWriteMutex);
		allocMap.Reset();
		suballocMap.Reset();
		writeMap.Reset();
		textureMap.Reset();
		pendingNotifies.clear();
	}

	if (flushThreadRunning.load()) {
		std::lock_guard<std::mutex> guard(flushLock);
		flushThreadRunning = false;
		flushThreadPending = true;
	}
	flushCond.notify_one();
	flushThread.join();
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

// Used by the debugger.
void MemBlockOverrideDetailed() {
	detailedOverride++;
}

void MemBlockReleaseDetailed() {
	detailedOverride--;
}

bool MemBlockInfoDetailed() {
	return g_Config.bDebugMemInfoDetailed || detailedOverride != 0;
}

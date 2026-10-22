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

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "Common/BitSet.h"
#include "Common/StringUtils.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MemoryScanner.h"
#include "Core/HLE/sceKernelModule.h"

static constexpr u32 CWCHEAT_BASE = 0x08800000;

// Common/BitSet.h only has the 64-bit version on some platforms (not MSVC ARM64/x86).
static inline int LowestSetBit64(u64 v) {
	const u32 lo = (u32)v;
	return lo ? LeastSignificantSetBit(lo) : 32 + LeastSignificantSetBit((u32)(v >> 32));
}

MemoryScanner &GetMemoryScanner() {
	static MemoryScanner scanner;
	return scanner;
}

u32 MemoryScanner::TypeSize(ScanValueType type) {
	switch (type) {
	case ScanValueType::U8: return 1;
	case ScanValueType::U16: return 2;
	case ScanValueType::U32:
	case ScanValueType::Float:
	default:
		return 4;
	}
}

bool MemoryScanner::CompareNeedsValue(ScanCompare compare) {
	switch (compare) {
	case ScanCompare::Equal:
	case ScanCompare::NotEqual:
	case ScanCompare::Greater:
	case ScanCompare::Less:
	case ScanCompare::Between:
	case ScanCompare::IncreasedBy:
	case ScanCompare::DecreasedBy:
		return true;
	default:
		return false;
	}
}

bool MemoryScanner::CompareNeedsPrevious(ScanCompare compare) {
	switch (compare) {
	case ScanCompare::Increased:
	case ScanCompare::Decreased:
	case ScanCompare::IncreasedBy:
	case ScanCompare::DecreasedBy:
	case ScanCompare::Changed:
	case ScanCompare::Unchanged:
		return true;
	default:
		return false;
	}
}

static u32 TypeMask(ScanValueType type) {
	switch (type) {
	case ScanValueType::U8: return 0xFF;
	case ScanValueType::U16: return 0xFFFF;
	default: return 0xFFFFFFFF;
	}
}

bool MemoryScanner::ParseValue(ScanValueType type, std::string_view text, ScanValue *value) {
	std::string str(StripSpaces(text));
	if (str.empty()) {
		return false;
	}

	bool hex = false;
	bool negative = false;
	size_t pos = 0;
	if (str[0] == '-' || str[0] == '+') {
		negative = str[0] == '-';
		pos = 1;
	}
	if (str.compare(pos, 2, "0x") == 0 || str.compare(pos, 2, "0X") == 0) {
		hex = true;
		pos += 2;
	} else if (str.compare(pos, 1, "$") == 0) {
		hex = true;
		pos += 1;
	}
	if (pos >= str.size()) {
		return false;
	}

	if (type == ScanValueType::Float && !hex) {
		char *end = nullptr;
		double d = strtod(str.c_str(), &end);
		if (!end || *end != '\0' || !std::isfinite(d)) {
			return false;
		}
		// Tolerance from the number of decimals typed, like Cheat Engine's "rounded" float search.
		size_t dot = str.find('.');
		size_t exp = str.find_first_of("eE");
		int decimals = 0;
		if (dot != std::string::npos) {
			size_t decEnd = exp != std::string::npos ? exp : str.size();
			decimals = decEnd > dot ? (int)(decEnd - dot - 1) : 0;
		}
		double tolerance = 0.5 * pow(10.0, -decimals);
		if (exp != std::string::npos) {
			// Scientific notation, just allow for float rounding.
			tolerance = std::max(fabs(d) * 1e-6, 1e-30);
		}
		float f = (float)d;
		value->f = d;
		memcpy(&value->bits, &f, sizeof(f));
		value->tolerance = tolerance;
		return true;
	}

	const std::string digits = str.substr(pos);
	for (char c : digits) {
		bool ok = hex ? ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) : (c >= '0' && c <= '9');
		if (!ok) {
			return false;
		}
	}
	if (digits.size() > 20) {
		return false;
	}
	unsigned long long magnitude = strtoull(digits.c_str(), nullptr, hex ? 16 : 10);

	const u32 mask = TypeMask(type);
	if (type == ScanValueType::Float) {
		// Hex for a float means its raw bits, matched exactly.
		if (negative || magnitude > 0xFFFFFFFFULL) {
			return false;
		}
		u32 bits = (u32)magnitude;
		float f;
		memcpy(&f, &bits, sizeof(f));
		value->bits = bits;
		value->f = f;
		value->tolerance = 0.0;
		return std::isfinite(f);
	}

	// Allow both the unsigned and the signed range of the type, so "-1" works for any size.
	if (negative) {
		unsigned long long limit = (unsigned long long)(mask >> 1) + 1;
		if (magnitude > limit) {
			return false;
		}
		value->bits = (u32)(0 - (u32)magnitude) & mask;
	} else {
		if (magnitude > mask) {
			return false;
		}
		value->bits = (u32)magnitude;
	}
	value->f = (double)value->bits;
	value->tolerance = 0.0;
	return true;
}

std::string MemoryScanner::FormatValue(ScanValueType type, u32 bits) {
	switch (type) {
	case ScanValueType::U8:
		return (bits & 0x80) ? StringFromFormat("%u (%d)", bits & 0xFF, (int)(s8)bits) : StringFromFormat("%u", bits & 0xFF);
	case ScanValueType::U16:
		return (bits & 0x8000) ? StringFromFormat("%u (%d)", bits & 0xFFFF, (int)(s16)bits) : StringFromFormat("%u", bits & 0xFFFF);
	case ScanValueType::U32:
		return (bits & 0x80000000) ? StringFromFormat("%u (%d)", bits, (int)bits) : StringFromFormat("%u", bits);
	case ScanValueType::Float:
	default:
	{
		float f;
		memcpy(&f, &bits, sizeof(f));
		return StringFromFormat("%.7g", f);
	}
	}
}

std::string MemoryScanner::FormatHex(ScanValueType type, u32 bits) {
	switch (type) {
	case ScanValueType::U8: return StringFromFormat("0x%02X", bits & 0xFF);
	case ScanValueType::U16: return StringFromFormat("0x%04X", bits & 0xFFFF);
	default: return StringFromFormat("0x%08X", bits);
	}
}

std::string MemoryScanner::MakeCwCheatLine(ScanValueType type, u32 addr, u32 bits) {
	const u32 offset = (addr - CWCHEAT_BASE) & 0x0FFFFFFF;
	switch (type) {
	case ScanValueType::U8:
		return StringFromFormat("_L 0x0%07X 0x%08X", offset, bits & 0xFF);
	case ScanValueType::U16:
		return StringFromFormat("_L 0x1%07X 0x%08X", offset, bits & 0xFFFF);
	case ScanValueType::U32:
	case ScanValueType::Float:
	default:
		return StringFromFormat("_L 0x2%07X 0x%08X", offset, bits);
	}
}

void MemoryScanner::GetPSPScanRange(u32 *start, u32 *size) {
	*start = PSP_GetUserMemoryBase();
	const u32 end = PSP_GetUserMemoryEnd();
	*size = end > *start ? end - *start : 0;
}

bool MemoryScanner::ReadPSP(ScanValueType type, u32 addr, u32 *bits) {
	const u32 size = TypeSize(type);
	if (!Memory::IsValidRange(addr, size)) {
		return false;
	}
	switch (size) {
	case 1: *bits = Memory::ReadUnchecked_U8(addr); break;
	case 2: *bits = Memory::ReadUnchecked_U16(addr); break;
	default: *bits = Memory::ReadUnchecked_U32(addr); break;
	}
	return true;
}

bool MemoryScanner::WritePSP(ScanValueType type, u32 addr, u32 bits) {
	const u32 size = TypeSize(type);
	if (!Memory::IsValidRange(addr, size)) {
		return false;
	}
	switch (size) {
	case 1: Memory::WriteUnchecked_U8((u8)bits, addr); break;
	case 2: Memory::WriteUnchecked_U16((u16)bits, addr); break;
	default: Memory::WriteUnchecked_U32(bits, addr); break;
	}
	// Same as cheats do, in case the value was in code.
	if (currentMIPS) {
		currentMIPS->InvalidateICacheRangeDeferred(addr & ~3, 4);
	}
	return true;
}

// The value types as they're compared. Integers are unsigned.
template <typename T>
static inline T LoadValue(const u8 *p) {
	T v;
	memcpy(&v, p, sizeof(T));
	return v;
}

template <typename T>
static inline bool IsUsable(T) { return true; }
template <>
inline bool IsUsable<float>(float v) { return std::isfinite(v); }

template <typename T>
static inline T ValueAs(const ScanValue &v) { return (T)v.bits; }
template <>
inline float ValueAs<float>(const ScanValue &v) { return (float)v.f; }

template <typename T>
static inline bool NearlyEqual(T a, T b, double) { return a == b; }
template <>
inline bool NearlyEqual<float>(float a, float b, double tolerance) {
	// Compare in double, so "95" matches 95.4999 but not 95.5001.
	return fabs((double)a - (double)b) <= tolerance;
}

template <typename T, ScanCompare C>
static inline bool Matches(T cur, T prev, const ScanValue &v, const ScanValue &v2) {
	if (!IsUsable(cur)) {
		return false;
	}
	switch (C) {
	case ScanCompare::Equal: return NearlyEqual<T>(cur, ValueAs<T>(v), v.tolerance);
	case ScanCompare::NotEqual: return !NearlyEqual<T>(cur, ValueAs<T>(v), v.tolerance);
	case ScanCompare::Greater: return cur > ValueAs<T>(v);
	case ScanCompare::Less: return cur < ValueAs<T>(v);
	case ScanCompare::Between:
	{
		T lo = ValueAs<T>(v);
		T hi = ValueAs<T>(v2);
		if (hi < lo) {
			std::swap(lo, hi);
		}
		return cur >= lo && cur <= hi;
	}
	case ScanCompare::Unknown: return true;
	case ScanCompare::Increased: return IsUsable(prev) && cur > prev;
	case ScanCompare::Decreased: return IsUsable(prev) && cur < prev;
	case ScanCompare::IncreasedBy: return IsUsable(prev) && NearlyEqual<T>((T)(cur - prev), ValueAs<T>(v), v.tolerance);
	case ScanCompare::DecreasedBy: return IsUsable(prev) && NearlyEqual<T>((T)(prev - cur), ValueAs<T>(v), v.tolerance);
	// Bitwise, so these work for floats with any bit pattern.
	case ScanCompare::Changed: return memcmp(&cur, &prev, sizeof(T)) != 0;
	case ScanCompare::Unchanged: return memcmp(&cur, &prev, sizeof(T)) == 0;
	default: return false;
	}
}

template <typename T, ScanCompare C>
static void FirstLoop(const u8 *mem, u32 size, std::vector<u64> &bits, const ScanValue &v, const ScanValue &v2) {
	const u32 align = (u32)sizeof(T);
	if (size < sizeof(T)) {
		return;
	}
	const u32 last = size - (u32)sizeof(T);
	for (u32 off = 0; off <= last; off += align) {
		T cur = LoadValue<T>(mem + off);
		if (Matches<T, C>(cur, cur, v, v2)) {
			bits[off >> 6] |= 1ULL << (off & 63);
		}
	}
}

template <typename T, ScanCompare C>
static void NextLoop(const u8 *mem, const u8 *prevMem, u32 size, std::vector<u64> &bits, const ScanValue &v, const ScanValue &v2) {
	for (size_t i = 0; i < bits.size(); i++) {
		u64 w = bits[i];
		if (!w) {
			continue;
		}
		u64 keep = w;
		while (w) {
			int b = LowestSetBit64(w);
			w &= w - 1;
			u32 off = (u32)(i * 64 + b);
			if (off + sizeof(T) > size) {
				keep &= ~(1ULL << b);
				continue;
			}
			T cur = LoadValue<T>(mem + off);
			T prev = LoadValue<T>(prevMem + off);
			if (!Matches<T, C>(cur, prev, v, v2)) {
				keep &= ~(1ULL << b);
			}
		}
		bits[i] = keep;
	}
}

#define SCAN_DISPATCH(LOOP, ...) \
	switch (compare) { \
	case ScanCompare::Equal: LOOP<T, ScanCompare::Equal>(__VA_ARGS__); break; \
	case ScanCompare::NotEqual: LOOP<T, ScanCompare::NotEqual>(__VA_ARGS__); break; \
	case ScanCompare::Greater: LOOP<T, ScanCompare::Greater>(__VA_ARGS__); break; \
	case ScanCompare::Less: LOOP<T, ScanCompare::Less>(__VA_ARGS__); break; \
	case ScanCompare::Between: LOOP<T, ScanCompare::Between>(__VA_ARGS__); break; \
	case ScanCompare::Unknown: LOOP<T, ScanCompare::Unknown>(__VA_ARGS__); break; \
	case ScanCompare::Increased: LOOP<T, ScanCompare::Increased>(__VA_ARGS__); break; \
	case ScanCompare::Decreased: LOOP<T, ScanCompare::Decreased>(__VA_ARGS__); break; \
	case ScanCompare::IncreasedBy: LOOP<T, ScanCompare::IncreasedBy>(__VA_ARGS__); break; \
	case ScanCompare::DecreasedBy: LOOP<T, ScanCompare::DecreasedBy>(__VA_ARGS__); break; \
	case ScanCompare::Changed: LOOP<T, ScanCompare::Changed>(__VA_ARGS__); break; \
	case ScanCompare::Unchanged: LOOP<T, ScanCompare::Unchanged>(__VA_ARGS__); break; \
	default: break; \
	}

template <typename T>
void MemoryScanner::ScanFirst(const u8 *mem, ScanCompare compare, const ScanValue &value, const ScanValue &value2) {
	if (compare == ScanCompare::Unknown) {
		// Every aligned address. Fill whole words with the alignment pattern.
		u64 pattern = sizeof(T) == 1 ? 0xFFFFFFFFFFFFFFFFULL : (sizeof(T) == 2 ? 0x5555555555555555ULL : 0x1111111111111111ULL);
		std::fill(bits_.begin(), bits_.end(), pattern);
		// Drop anything past the end of the range.
		for (u32 off = size_ >= sizeof(T) ? size_ - (u32)sizeof(T) + 1 : 0; off < (u32)bits_.size() * 64; off++) {
			bits_[off >> 6] &= ~(1ULL << (off & 63));
		}
		return;
	}
	SCAN_DISPATCH(FirstLoop, mem, size_, bits_, value, value2);
}

template <typename T>
void MemoryScanner::ScanNext(const u8 *mem, ScanCompare compare, const ScanValue &value, const ScanValue &value2) {
	SCAN_DISPATCH(NextLoop, mem, snapshot_.data(), size_, bits_, value, value2);
}

#undef SCAN_DISPATCH

void MemoryScanner::TakeSnapshot(const u8 *mem) {
	snapshot_.resize(size_);
	memcpy(snapshot_.data(), mem, size_);
}

void MemoryScanner::CountResults() {
	size_t count = 0;
	for (u64 w : bits_) {
		count += (size_t)CountSetBits((u32)w) + (size_t)CountSetBits((u32)(w >> 32));
	}
	resultCount_ = count;
}

bool MemoryScanner::NewScanOnBuffer(const u8 *mem, u32 base, u32 size, ScanValueType type, ScanCompare compare, const ScanValue &value, const ScanValue &value2) {
	if (!mem || size == 0 || !CompareAllowedOnFirstScan(compare)) {
		return false;
	}
	std::string gameID = gameID_;
	Reset();
	gameID_ = gameID;

	type_ = type;
	base_ = base;
	size_ = size;
	bits_.assign((size + 63) / 64, 0);

	switch (type) {
	case ScanValueType::U8: ScanFirst<u8>(mem, compare, value, value2); break;
	case ScanValueType::U16: ScanFirst<u16>(mem, compare, value, value2); break;
	case ScanValueType::U32: ScanFirst<u32>(mem, compare, value, value2); break;
	case ScanValueType::Float: ScanFirst<float>(mem, compare, value, value2); break;
	default: return false;
	}

	TakeSnapshot(mem);
	CountResults();
	scanCount_ = 1;
	return true;
}

bool MemoryScanner::NextScanOnBuffer(const u8 *mem, u32 base, u32 size, ScanCompare compare, const ScanValue &value, const ScanValue &value2) {
	if (!mem || scanCount_ == 0 || base != base_ || size != size_ || compare == ScanCompare::Unknown) {
		return false;
	}

	undoBits_ = bits_;
	undoResultCount_ = resultCount_;

	switch (type_) {
	case ScanValueType::U8: ScanNext<u8>(mem, compare, value, value2); break;
	case ScanValueType::U16: ScanNext<u16>(mem, compare, value, value2); break;
	case ScanValueType::U32: ScanNext<u32>(mem, compare, value, value2); break;
	case ScanValueType::Float: ScanNext<float>(mem, compare, value, value2); break;
	default: return false;
	}

	TakeSnapshot(mem);
	CountResults();
	scanCount_++;
	return true;
}

bool MemoryScanner::GetPSPScanArea(SearchIn searchIn, const std::string &moduleName, Area *area, Error *error) {
	*area = Area();
	area->searchIn = searchIn;
	*error = Error::None;
	if (searchIn == SearchIn::ModuleGlobals) {
		for (const ModuleRange &r : __KernelModuleRanges()) {
			if (r.kernel || r.dataSize == 0) {
				continue;
			}
			if (moduleName.empty() ? r.isMain : r.name == moduleName) {
				area->start = r.dataStart;
				area->size = r.dataSize;
				area->moduleName = r.name;
				// Code can sit after the data too.
				area->excluded = r.code;
				break;
			}
		}
		if (area->size == 0) {
			*error = Error::NoModule;
			return false;
		}
	} else {
		GetPSPScanRange(&area->start, &area->size);
	}
	if (area->size == 0 || !Memory::IsValidRange(area->start, area->size)) {
		*error = Error::NoMemory;
		return false;
	}
	if (searchIn == SearchIn::UserMemoryNoCode) {
		for (const ModuleRange &r : __KernelModuleRanges()) {
			area->excluded.insert(area->excluded.end(), r.code.begin(), r.code.end());
		}
	}
	return true;
}

void MemoryScanner::ExcludeRanges(const std::vector<std::pair<u32, u32>> &ranges) {
	for (const auto &r : ranges) {
		const u64 rangeStart = r.first;
		const u64 rangeEnd = (u64)r.first + r.second;
		const u64 start = std::max<u64>(rangeStart, base_);
		const u64 end = std::min<u64>(rangeEnd, (u64)base_ + size_);
		for (u64 a = start; a < end; a++) {
			const u32 off = (u32)(a - base_);
			if ((off & 63) == 0 && a + 64 <= end) {
				// A whole word at a time.
				bits_[off >> 6] = 0;
				a += 63;
				continue;
			}
			bits_[off >> 6] &= ~(1ULL << (off & 63));
		}
	}
	CountResults();
}

bool MemoryScanner::NewScan(ScanValueType type, ScanCompare compare, const ScanValue &value, const ScanValue &value2, SearchIn searchIn, const std::string &moduleName, Error *error) {
	Area area;
	if (!GetPSPScanArea(searchIn, moduleName, &area, error)) {
		return false;
	}
	if (!NewScanOnBuffer(Memory::GetPointerUnchecked(area.start), area.start, area.size, type, compare, value, value2)) {
		*error = Error::NoMemory;
		return false;
	}
	area_ = area;
	ExcludeRanges(area_.excluded);
	return true;
}

bool MemoryScanner::NextScan(ScanCompare compare, const ScanValue &value, const ScanValue &value2, Error *error) {
	Area area;
	if (!GetPSPScanArea(area_.searchIn, area_.moduleName, &area, error)) {
		if (*error == Error::NoModule) {
			// The module the search was in is gone.
			*error = Error::AreaChanged;
		}
		return false;
	}
	if (area.start != base_ || area.size != size_) {
		// The module moved, or RAM size changed (different game?): the results are
		// meaningless now.
		Reset();
		*error = Error::AreaChanged;
		return false;
	}
	if (!NextScanOnBuffer(Memory::GetPointerUnchecked(area.start), area.start, area.size, compare, value, value2)) {
		*error = Error::NoMemory;
		return false;
	}
	// Modules loaded since the last scan may have brought new code.
	area_.excluded = area.excluded;
	ExcludeRanges(area_.excluded);
	return true;
}

void MemoryScanner::Undo() {
	if (undoBits_.empty()) {
		return;
	}
	bits_.swap(undoBits_);
	resultCount_ = undoResultCount_;
	std::vector<u64>().swap(undoBits_);
	if (scanCount_ > 1) {
		scanCount_--;
	}
}

void MemoryScanner::Reset() {
	std::vector<u64>().swap(bits_);
	std::vector<u64>().swap(undoBits_);
	std::vector<u8>().swap(snapshot_);
	resultCount_ = 0;
	undoResultCount_ = 0;
	scanCount_ = 0;
	base_ = 0;
	size_ = 0;
	area_ = Area();
	gameID_.clear();
}

std::vector<u32> MemoryScanner::GetResults(size_t max) const {
	std::vector<u32> results;
	for (size_t i = 0; i < bits_.size() && results.size() < max; i++) {
		u64 w = bits_[i];
		while (w && results.size() < max) {
			int b = LowestSetBit64(w);
			w &= w - 1;
			results.push_back(base_ + (u32)(i * 64 + b));
		}
	}
	return results;
}

u32 MemoryScanner::PreviousValue(u32 addr) const {
	const u32 size = TypeSize(type_);
	if (addr < base_ || addr - base_ + size > snapshot_.size()) {
		return 0;
	}
	const u8 *p = snapshot_.data() + (addr - base_);
	switch (size) {
	case 1: return p[0];
	case 2: return LoadValue<u16>(p);
	default: return LoadValue<u32>(p);
	}
}

// Pointer chains

template <typename Read32>
static bool FollowChain(const PointerChain &c, Read32 read32, u32 *addr) {
	if (c.depth <= 0) {
		*addr = c.base;
		return true;
	}
	u32 p;
	if (!read32(c.base, &p)) {
		return false;
	}
	for (int i = 0; i < c.depth - 1; i++) {
		if (!read32(p + c.offsets[i], &p)) {
			return false;
		}
	}
	// The same memory through a mirror (uncached 0x4xxxxxxx and so on) is the same address.
	*addr = (p + c.offsets[c.depth - 1]) & 0x3FFFFFFF;
	return true;
}

bool PointerChain::Resolve(u32 *addr) const {
	return FollowChain(*this, [](u32 a, u32 *v) {
		if (!Memory::IsValidRange(a, 4)) {
			return false;
		}
		*v = Memory::ReadUnchecked_U32(a);
		return true;
	}, addr);
}

bool PointerChain::ResolveInBuffer(const u8 *mem, u32 memBase, u32 memSize, u32 *addr) const {
	return FollowChain(*this, [=](u32 a, u32 *v) {
		// Mirrors (uncached 0x4xxxxxxx and so on) are the same memory.
		a &= 0x3FFFFFFF;
		if (a < memBase || a - memBase + 4 > memSize) {
			return false;
		}
		memcpy(v, mem + (a - memBase), 4);
		return true;
	}, addr);
}

std::string PointerChain::ToString() const {
	if (depth <= 0) {
		return StringFromFormat("0x%08X", base);
	}
	std::string s(depth, '[');
	s += StringFromFormat("0x%08X]", base);
	for (int i = 0; i < depth; i++) {
		s += StringFromFormat(" + 0x%X", offsets[i]);
		if (i < depth - 1) {
			s += "]";
		}
	}
	return s;
}

bool PointerChain::operator==(const PointerChain &other) const {
	if (base != other.base || depth != other.depth) {
		return false;
	}
	for (int i = 0; i < depth; i++) {
		if (offsets[i] != other.offsets[i]) {
			return false;
		}
	}
	return true;
}

std::vector<std::string> MemoryScanner::MakeCwCheatLines(ScanValueType type, const PointerChain &chain, u32 bits) {
	if (!chain.IsPointer()) {
		return { MakeCwCheatLine(type, chain.base, bits) };
	}
	// _L 0x6AAAAAAA 0xVVVVVVVV   A = first pointer, relative to 0x08800000; V = value
	// _L 0x000T000N 0xOOOOOOOO   T = 0/1/2 for 8/16/32-bit, N = this line + the walk lines, O = last offset
	// _L 0x2WWWWWWW 0x00000000   one per pointer after the first: p = [p + W]
	u32 typeCode;
	switch (type) {
	case ScanValueType::U8: typeCode = 0; bits &= 0xFF; break;
	case ScanValueType::U16: typeCode = 1; bits &= 0xFFFF; break;
	default: typeCode = 2; break;
	}
	std::vector<std::string> lines;
	lines.push_back(StringFromFormat("_L 0x6%07X 0x%08X", (chain.base - CWCHEAT_BASE) & 0x0FFFFFFF, bits));
	lines.push_back(StringFromFormat("_L 0x%08X 0x%08X", (typeCode << 16) | (u32)chain.depth, chain.offsets[chain.depth - 1]));
	for (int i = 0; i < chain.depth - 1; i++) {
		lines.push_back(StringFromFormat("_L 0x2%07X 0x00000000", chain.offsets[i] & 0x0FFFFFFF));
	}
	return lines;
}

// Freezing

void MemoryScanner::SetFrozen(const PointerChain &where, ScanValueType type, u32 bits, bool frozen) {
	auto it = std::find_if(freezes_.begin(), freezes_.end(), [&](const Freeze &f) { return f.where == where && f.type == type; });
	if (frozen) {
		if (it != freezes_.end()) {
			it->bits = bits;
		} else {
			freezes_.push_back(Freeze{ where, type, bits });
		}
		// Hold it right away, not only at the next cheat run.
		u32 addr;
		if (where.Resolve(&addr)) {
			WritePSP(type, addr, bits);
		}
	} else if (it != freezes_.end()) {
		freezes_.erase(it);
	}
}

bool MemoryScanner::IsFrozen(const PointerChain &where, ScanValueType type) const {
	for (const Freeze &f : freezes_) {
		if (f.where == where && f.type == type) {
			return true;
		}
	}
	return false;
}

void MemoryScanner::UpdateFrozenValue(const PointerChain &where, ScanValueType type, u32 bits) {
	for (Freeze &f : freezes_) {
		if (f.where == where && f.type == type) {
			f.bits = bits;
		}
	}
}

void MemoryScanner::ApplyFreezes() const {
	for (const Freeze &f : freezes_) {
		u32 addr, cur = 0;
		// A pointer that's invalid right now (loading, menus) just isn't written.
		if (!f.where.Resolve(&addr)) {
			continue;
		}
		// Only write (and invalidate) when the game changed it.
		if (ReadPSP(f.type, addr, &cur) && cur != f.bits) {
			WritePSP(f.type, addr, f.bits);
		}
	}
}

// Pointer scan

std::vector<std::pair<u32, u32>> SubtractRanges(std::pair<u32, u32> range, std::vector<std::pair<u32, u32>> holes) {
	std::sort(holes.begin(), holes.end());
	std::vector<std::pair<u32, u32>> pieces;
	u64 pos = range.first;
	const u64 end = (u64)range.first + range.second;
	for (const auto &hole : holes) {
		const u64 holeStart = hole.first;
		const u64 holeEnd = (u64)hole.first + hole.second;
		if (holeEnd <= pos || holeStart >= end) {
			continue;
		}
		if (holeStart > pos) {
			pieces.emplace_back((u32)pos, (u32)(holeStart - pos));
		}
		pos = std::max(pos, holeEnd);
	}
	if (pos < end) {
		pieces.emplace_back((u32)pos, (u32)(end - pos));
	}
	return pieces;
}

PointerScanner &GetPointerScanner() {
	static PointerScanner scanner;
	return scanner;
}

std::vector<std::pair<u32, u32>> PointerScanner::GetPSPStaticRanges(const std::string &moduleName) {
	std::vector<std::pair<u32, u32>> ranges;
	for (const ModuleRange &r : __KernelModuleRanges()) {
		if (r.kernel || r.dataSize == 0) {
			continue;
		}
		if (moduleName.empty() || r.name == moduleName) {
			// Without the code in it: jump instructions look just like pointers.
			for (const auto &piece : SubtractRanges({ r.dataStart, r.dataSize }, r.code)) {
				ranges.push_back(piece);
			}
		}
	}
	return ranges;
}

void PointerScanner::Reset() {
	std::vector<PointerChain>().swap(results_);
	target_ = 0;
	scans_ = 0;
	truncated_ = false;
	gameID_.clear();
}

bool PointerScanner::ScanBuffer(const u8 *mem, u32 memBase, u32 memSize, const std::vector<std::pair<u32, u32>> &staticRangesIn,
	u32 target, ScanValueType type, const Settings &settingsIn) {
	if (!mem || memSize < 4) {
		return false;
	}
	std::string gameID = gameID_;
	Reset();
	gameID_ = gameID;

	Settings settings = settingsIn;
	settings.maxDepth = std::max(1, std::min(settings.maxDepth, MAX_POINTER_DEPTH));
	target_ = target;
	type_ = type;
	settings_ = settings;
	const u32 memEnd = memBase + memSize;

	// Bases must be in static data, and in user memory so CwCheat can address them.
	std::vector<std::pair<u32, u32>> staticRanges;
	for (const auto &r : staticRangesIn) {
		u32 start = std::max(r.first, CWCHEAT_BASE);
		u32 end = r.first + r.second;
		if (end > start) {
			staticRanges.emplace_back(start, end);
		}
	}
	std::sort(staticRanges.begin(), staticRanges.end());
	auto isStatic = [&](u32 addr) {
		auto it = std::upper_bound(staticRanges.begin(), staticRanges.end(), std::make_pair(addr, 0xFFFFFFFFu));
		if (it == staticRanges.begin()) {
			return false;
		}
		--it;
		return addr >= it->first && addr + 4 <= it->second;
	};

	// Every aligned word that looks like a pointer into this memory, sorted by where it points:
	// (value << 32) | address of the word.
	std::vector<u64> index;
	index.reserve(memSize / 64);
	for (u32 off = 0; off + 4 <= memSize; off += 4) {
		u32 v;
		memcpy(&v, mem + off, 4);
		v &= 0x3FFFFFFF;
		if ((v & 3) == 0 && v >= memBase && v < memEnd) {
			index.push_back(((u64)v << 32) | (memBase + off));
		}
	}
	std::sort(index.begin(), index.end());

	// Back from the target, one pointer at a time: what points at (or a bit before) it, then what
	// points at those, and so on. A pointer in static data ends a chain.
	struct Node {
		u32 addr;
		u32 offsets[MAX_POINTER_DEPTH];  // from this node to the target; level of them are used
	};
	std::vector<Node> frontier(1);
	frontier[0].addr = target;
	for (int level = 1; level <= settings.maxDepth && !frontier.empty(); level++) {
		std::vector<Node> next;
		for (const Node &node : frontier) {
			const u32 lo = node.addr >= settings.maxOffset ? node.addr - settings.maxOffset : 0;
			auto it = std::lower_bound(index.begin(), index.end(), (u64)lo << 32);
			for (; it != index.end() && (u32)(*it >> 32) <= node.addr; ++it) {
				const u32 value = (u32)(*it >> 32);
				const u32 addr = (u32)*it;
				if (isStatic(addr)) {
					if (results_.size() >= MAX_RESULTS) {
						truncated_ = true;
						continue;
					}
					PointerChain c;
					c.base = addr;
					c.depth = level;
					c.offsets[0] = node.addr - value;
					for (int i = 1; i < level; i++) {
						c.offsets[i] = node.offsets[i - 1];
					}
					results_.push_back(c);
				} else if (level < settings.maxDepth) {
					if (next.size() >= MAX_NODES_PER_LEVEL) {
						truncated_ = true;
						continue;
					}
					Node n;
					n.addr = addr;
					n.offsets[0] = node.addr - value;
					for (int i = 1; i < level; i++) {
						n.offsets[i] = node.offsets[i - 1];
					}
					next.push_back(n);
				}
			}
		}
		frontier.swap(next);
	}

	// Shortest and simplest first: fewer pointers, then smaller offsets.
	std::sort(results_.begin(), results_.end(), [](const PointerChain &a, const PointerChain &b) {
		if (a.depth != b.depth) {
			return a.depth < b.depth;
		}
		u64 sa = 0, sb = 0;
		for (int i = 0; i < a.depth; i++) {
			sa += a.offsets[i];
			sb += b.offsets[i];
		}
		if (sa != sb) {
			return sa < sb;
		}
		return a.base < b.base;
	});
	scans_ = 1;
	return true;
}

size_t PointerScanner::FilterBuffer(const u8 *mem, u32 memBase, u32 memSize, u32 newTarget) {
	if (scans_ == 0) {
		return 0;
	}
	results_.erase(std::remove_if(results_.begin(), results_.end(), [&](const PointerChain &c) {
		u32 addr;
		return !c.ResolveInBuffer(mem, memBase, memSize, &addr) || addr != newTarget;
	}), results_.end());
	target_ = newTarget;
	scans_++;
	return results_.size();
}

bool PointerScanner::Scan(u32 target, ScanValueType type, const Settings &settings) {
	u32 start, size;
	MemoryScanner::GetPSPScanRange(&start, &size);
	if (size == 0 || !Memory::IsValidRange(start, size)) {
		return false;
	}
	return ScanBuffer(Memory::GetPointerUnchecked(start), start, size, GetPSPStaticRanges(settings.moduleName), target, type, settings);
}

size_t PointerScanner::Filter(u32 newTarget) {
	u32 start, size;
	MemoryScanner::GetPSPScanRange(&start, &size);
	if (size == 0 || !Memory::IsValidRange(start, size)) {
		return results_.size();
	}
	return FilterBuffer(Memory::GetPointerUnchecked(start), start, size, newTarget);
}

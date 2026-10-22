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

// Memory Scanner: a small "Cheat Engine"-like value search over PSP user memory.
// Results are kept as a bitmap (one bit per byte of the scanned range), so even a first
// scan that matches tens of millions of addresses costs only range/8 bytes. A copy of the
// scanned range is kept after every scan, which is what "increased"/"decreased"/"changed"
// compare against.
//
// Everything here runs on the emulator thread (the UI thread), so emulation isn't running
// while a scan is in progress.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Common/CommonTypes.h"

enum class ScanValueType {
	U8 = 0,
	U16 = 1,
	U32 = 2,
	Float = 3,
	COUNT,
};

enum class ScanCompare {
	Equal = 0,      // value == x (floats: rounded to the digits typed)
	NotEqual,       // value != x
	Greater,        // value > x
	Less,           // value < x
	Between,        // x <= value <= y
	Unknown,        // first scan only: take every address, compare later with the ones below
	Increased,      // value > previous
	Decreased,      // value < previous
	IncreasedBy,    // value == previous + x
	DecreasedBy,    // value == previous - x
	Changed,        // value != previous
	Unchanged,      // value == previous
	COUNT,
};

// Where a value is: a plain address (depth 0), or a chain of pointers starting at a static
// address, like CwCheat's pointer code type (0x6) follows:
//   p = [base]; p = [p + offsets[0]]; ... ; address = p + offsets[depth - 1]
// so depth is the number of pointers read.
static constexpr int MAX_POINTER_DEPTH = 4;
struct PointerChain {
	u32 base = 0;
	int depth = 0;
	u32 offsets[MAX_POINTER_DEPTH]{};

	static PointerChain Direct(u32 addr) {
		PointerChain c;
		c.base = addr;
		return c;
	}
	bool IsPointer() const { return depth > 0; }
	// Follows the chain in PSP memory. False if a pointer on the way isn't a valid address.
	bool Resolve(u32 *addr) const;
	// The same, in a buffer holding memory from memBase.
	bool ResolveInBuffer(const u8 *mem, u32 memBase, u32 memSize, u32 *addr) const;
	// "[[0x08912340] + 0x10] + 0x1C", or "0x08912340" for a plain address.
	std::string ToString() const;
	bool operator==(const PointerChain &other) const;
};

// A parsed search value. For integers, bits holds the value truncated to the type's size.
struct ScanValue {
	u32 bits = 0;
	double f = 0.0;
	// For floats, the allowed difference for Equal/IncreasedBy/DecreasedBy, from the number of
	// decimals typed: "95" matches 94.5..95.5, "95.25" matches 95.245..95.255.
	double tolerance = 0.5;
};

class MemoryScanner {
public:
	// Size in bytes of a value of this type.
	static u32 TypeSize(ScanValueType type);
	// Addresses are aligned to this when scanning. Matches how games store values.
	static u32 TypeAlign(ScanValueType type) { return TypeSize(type); }

	static bool CompareNeedsValue(ScanCompare compare);
	static bool CompareNeedsSecondValue(ScanCompare compare) { return compare == ScanCompare::Between; }
	static bool CompareNeedsPrevious(ScanCompare compare);
	static bool CompareAllowedOnFirstScan(ScanCompare compare) { return !CompareNeedsPrevious(compare); }

	// Accepts decimal ("100", "-1"), hex ("0x64", "$64") and for floats "1.5", "-2e3".
	// Returns false if the text isn't a valid value for the type (or out of its range).
	static bool ParseValue(ScanValueType type, std::string_view text, ScanValue *value);
	// Decimal value for display. Integers are shown unsigned, with the signed value too when negative.
	static std::string FormatValue(ScanValueType type, u32 bits);
	static std::string FormatHex(ScanValueType type, u32 bits);

	// CwCheat line(s) writing bits to addr: "_L 0x2XXXXXXX 0xYYYYYYYY". The address must be
	// in user memory (at or above 0x08800000).
	static std::string MakeCwCheatLine(ScanValueType type, u32 addr, u32 bits);
	// The lines of a cheat writing bits through a chain: a plain write for a plain address, or a
	// pointer code (0x6) with one walk line (0x2) per pointer after the first.
	static std::vector<std::string> MakeCwCheatLines(ScanValueType type, const PointerChain &chain, u32 bits);

	// All of user memory, from 0x08800000 to the end of RAM. This includes the extra 32MB of a
	// PSP-2000+ when the game (or the HD remaster settings) enabled it, since g_MemorySize says
	// how much RAM is mapped.
	static void GetPSPScanRange(u32 *start, u32 *size);

	// Where a search looks (the Search in scan option).
	enum class SearchIn {
		UserMemoryNoCode = 0,  // All of user memory, minus the modules' code.
		ModuleGlobals = 1,     // One module's globals (.data and .bss), which never move.
		AllUserMemory = 2,
		COUNT,
	};
	// What a search covers.
	struct Area {
		SearchIn searchIn = SearchIn::UserMemoryNoCode;
		u32 start = 0;
		u32 size = 0;
		// For ModuleGlobals.
		std::string moduleName;
		// Ranges left out, as (start, size): the modules' code.
		std::vector<std::pair<u32, u32>> excluded;
	};
	enum class Error {
		None,
		NoMemory,       // Game memory isn't available.
		NoModule,       // The module isn't loaded (or has no globals).
		AreaChanged,    // The module moved or was unloaded since the search started.
	};
	// moduleName is for ModuleGlobals; empty means the main module.
	static bool GetPSPScanArea(SearchIn searchIn, const std::string &moduleName, Area *area, Error *error);

	// Scanning the emulated PSP's memory. The area is picked by a new scan and kept by next scans.
	bool NewScan(ScanValueType type, ScanCompare compare, const ScanValue &value, const ScanValue &value2, SearchIn searchIn, const std::string &moduleName, Error *error);
	bool NextScan(ScanCompare compare, const ScanValue &value, const ScanValue &value2, Error *error);
	// The current search's area.
	const Area &SearchArea() const { return area_; }

	// Drops results in these ranges, as (start, size). Used for skipping code.
	void ExcludeRanges(const std::vector<std::pair<u32, u32>> &ranges);

	// The same, on any buffer. Used by the above, and by tests.
	bool NewScanOnBuffer(const u8 *mem, u32 base, u32 size, ScanValueType type, ScanCompare compare, const ScanValue &value, const ScanValue &value2);
	bool NextScanOnBuffer(const u8 *mem, u32 base, u32 size, ScanCompare compare, const ScanValue &value, const ScanValue &value2);

	// Go back to the results before the last Next Scan. Only one level.
	bool CanUndo() const { return !undoBits_.empty(); }
	void Undo();

	// Clears the search. Frozen values stay.
	void Reset();

	bool HasResults() const { return scanCount_ > 0; }
	int ScanCount() const { return scanCount_; }
	size_t ResultCount() const { return resultCount_; }
	ScanValueType Type() const { return type_; }
	u32 Base() const { return base_; }
	u32 Size() const { return size_; }
	const std::string &GameID() const { return gameID_; }
	void SetGameID(std::string_view id) { gameID_ = id; }

	// Up to max result addresses, lowest first.
	std::vector<u32> GetResults(size_t max) const;
	// The value at addr when the last scan ran.
	u32 PreviousValue(u32 addr) const;

	// Frozen values are written back to memory every time cheats run (the cheat refresh
	// interval), until unfrozen. They're independent of the search, so a new search keeps them.
	// A frozen pointer chain is followed again every time, like a pointer cheat.
	struct Freeze {
		PointerChain where;
		ScanValueType type;
		u32 bits;
	};
	void SetFrozen(const PointerChain &where, ScanValueType type, u32 bits, bool frozen);
	bool IsFrozen(const PointerChain &where, ScanValueType type) const;
	// Changes the value a frozen address is held at (if it's frozen).
	void UpdateFrozenValue(const PointerChain &where, ScanValueType type, u32 bits);
	void SetFrozen(u32 addr, ScanValueType type, u32 bits, bool frozen) { SetFrozen(PointerChain::Direct(addr), type, bits, frozen); }
	bool IsFrozen(u32 addr, ScanValueType type) const { return IsFrozen(PointerChain::Direct(addr), type); }
	void UpdateFrozenValue(u32 addr, ScanValueType type, u32 bits) { UpdateFrozenValue(PointerChain::Direct(addr), type, bits); }
	const std::vector<Freeze> &Freezes() const { return freezes_; }
	void ClearFreezes() { freezes_.clear(); }
	void ApplyFreezes() const;

	// Reading and writing a value of the scan's type in PSP memory.
	static bool ReadPSP(ScanValueType type, u32 addr, u32 *bits);
	static bool WritePSP(ScanValueType type, u32 addr, u32 bits);

private:
	template <typename T>
	void ScanFirst(const u8 *mem, ScanCompare compare, const ScanValue &value, const ScanValue &value2);
	template <typename T>
	void ScanNext(const u8 *mem, ScanCompare compare, const ScanValue &value, const ScanValue &value2);

	void TakeSnapshot(const u8 *mem);
	void CountResults();

	ScanValueType type_ = ScanValueType::U32;
	u32 base_ = 0;
	u32 size_ = 0;
	int scanCount_ = 0;
	size_t resultCount_ = 0;
	std::string gameID_;

	std::vector<u64> bits_;
	std::vector<u8> snapshot_;
	std::vector<u64> undoBits_;
	size_t undoResultCount_ = 0;
	Area area_;

	std::vector<Freeze> freezes_;
};

// The one scanner, which keeps its results while the game runs.
MemoryScanner &GetMemoryScanner();

// Finds pointer chains leading to an address, starting from static data (the .data/.bss of the
// game's modules), which stays at the same place when the game reloads a level or restarts.
// A heap object can be somewhere else every time, but the chain to it still finds it.
//
// The first scan usually finds many chains, most of them only right by chance. Filtering keeps
// the ones that lead to the value's new address after it moved (new level, restarted game).
class PointerScanner {
public:
	struct Settings {
		int maxDepth = 2;          // 1 to MAX_POINTER_DEPTH pointers.
		u32 maxOffset = 0x1000;    // Largest offset from a pointer to what's used after it.
		std::string moduleName;    // Chains only start in this module's globals, if set.
	};
	// Upper bounds, to keep a deep scan from using all the RAM. Truncated() says if hit.
	static constexpr size_t MAX_RESULTS = 200000;
	static constexpr size_t MAX_NODES_PER_LEVEL = 1000000;

	// Static data ranges of the game, as (start, size).
	// The globals of the game's modules, or of the named one.
	static std::vector<std::pair<u32, u32>> GetPSPStaticRanges(const std::string &moduleName);

	bool Scan(u32 target, ScanValueType type, const Settings &settings);
	// Keeps the chains that now lead to newTarget. Returns how many are left.
	size_t Filter(u32 newTarget);

	// The same, on a buffer. Used by the above, and by tests.
	bool ScanBuffer(const u8 *mem, u32 memBase, u32 memSize, const std::vector<std::pair<u32, u32>> &staticRanges,
		u32 target, ScanValueType type, const Settings &settings);
	size_t FilterBuffer(const u8 *mem, u32 memBase, u32 memSize, u32 newTarget);

	void Reset();

	bool HasResults() const { return scans_ > 0; }
	// 1 after a scan, +1 for each filter.
	int Scans() const { return scans_; }
	bool Truncated() const { return truncated_; }
	u32 Target() const { return target_; }
	ScanValueType Type() const { return type_; }
	const Settings &LastSettings() const { return settings_; }
	// Shortest chains first.
	const std::vector<PointerChain> &Results() const { return results_; }
	const std::string &GameID() const { return gameID_; }
	void SetGameID(std::string_view id) { gameID_ = id; }

private:
	std::vector<PointerChain> results_;
	u32 target_ = 0;
	ScanValueType type_ = ScanValueType::U32;
	Settings settings_;
	int scans_ = 0;
	bool truncated_ = false;
	std::string gameID_;
};

PointerScanner &GetPointerScanner();

// Copyright (c) 2026- PPSSPP Project.

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

	// The range of PSP RAM that's scanned: user memory, from 0x08800000 to the end of RAM.
	// This includes the extra 32MB of a PSP-2000+ when the game (or the HD remaster
	// settings) enabled it, since g_MemorySize says how much RAM is mapped.
	static void GetPSPScanRange(u32 *start, u32 *size);

	// Scanning the emulated PSP's user memory. Return false if the memory isn't available.
	bool NewScan(ScanValueType type, ScanCompare compare, const ScanValue &value, const ScanValue &value2);
	bool NextScan(ScanCompare compare, const ScanValue &value, const ScanValue &value2);

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
	struct Freeze {
		u32 addr;
		ScanValueType type;
		u32 bits;
	};
	void SetFrozen(u32 addr, ScanValueType type, u32 bits, bool frozen);
	bool IsFrozen(u32 addr, ScanValueType type) const;
	// Changes the value a frozen address is held at (if it's frozen).
	void UpdateFrozenValue(u32 addr, ScanValueType type, u32 bits);
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

	std::vector<Freeze> freezes_;
};

// The one scanner, which keeps its results while the game runs.
MemoryScanner &GetMemoryScanner();

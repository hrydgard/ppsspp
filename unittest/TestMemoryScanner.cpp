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

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Core/CwCheat.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/MemoryScanner.h"
#include "Core/MIPS/MIPS.h"
#include "unittest/UnitTest.h"

static bool Parse(ScanValueType type, const char *text, ScanValue *v) {
	return MemoryScanner::ParseValue(type, text, v);
}

static void Put32(std::vector<u8> &mem, u32 off, u32 v) { memcpy(&mem[off], &v, 4); }
static void PutF(std::vector<u8> &mem, u32 off, float v) { memcpy(&mem[off], &v, 4); }

static bool TestValueScans() {
	const u32 base = 0x08800000;
	ScanValue v, v2;

	// Parsing.
	EXPECT_TRUE(Parse(ScanValueType::U8, "255", &v));
	EXPECT_EQ_INT(v.bits, 255);
	EXPECT_TRUE(Parse(ScanValueType::U8, "-1", &v));
	EXPECT_EQ_INT(v.bits, 0xFF);
	EXPECT_FALSE(Parse(ScanValueType::U8, "256", &v));
	EXPECT_TRUE(Parse(ScanValueType::U16, "0x1234", &v));
	EXPECT_EQ_INT(v.bits, 0x1234);
	EXPECT_TRUE(Parse(ScanValueType::U32, "$FFFFFFFF", &v));
	EXPECT_EQ_INT(v.bits, 0xFFFFFFFF);
	EXPECT_FALSE(Parse(ScanValueType::U32, "12a", &v));
	EXPECT_FALSE(Parse(ScanValueType::U32, "", &v));
	EXPECT_TRUE(Parse(ScanValueType::Float, "95", &v));
	EXPECT_TRUE(v.tolerance == 0.5);
	EXPECT_TRUE(Parse(ScanValueType::Float, "1.25", &v));
	EXPECT_TRUE(v.tolerance < 0.0051 && v.tolerance > 0.0049);

	// CwCheat lines.
	EXPECT_EQ_STR(MemoryScanner::MakeCwCheatLine(ScanValueType::U32, 0x08912340, 100), std::string("_L 0x20112340 0x00000064"));
	EXPECT_EQ_STR(MemoryScanner::MakeCwCheatLine(ScanValueType::U16, 0x08800002, 0xBEEF), std::string("_L 0x10000002 0x0000BEEF"));
	EXPECT_EQ_STR(MemoryScanner::MakeCwCheatLine(ScanValueType::U8, 0x0A000001, 0x7F), std::string("_L 0x01800001 0x0000007F"));

	std::vector<u8> mem(4096, 0);
	Put32(mem, 0x100, 100);
	Put32(mem, 0x200, 100);
	Put32(mem, 0x300, 100);
	Put32(mem, 0x301 & ~3, 100);  // same as 0x300, unaligned spots aren't scanned for 32-bit.

	MemoryScanner scanner;
	EXPECT_TRUE(Parse(ScanValueType::U32, "100", &v));
	EXPECT_TRUE(scanner.NewScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanValueType::U32, ScanCompare::Equal, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 3);

	// Next scan can't be Unknown, and a relative compare isn't allowed on the first one.
	EXPECT_FALSE(scanner.NextScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanCompare::Unknown, v, v2));
	MemoryScanner other;
	EXPECT_FALSE(other.NewScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanValueType::U32, ScanCompare::Increased, v, v2));

	// One went down, one went up.
	Put32(mem, 0x100, 90);
	Put32(mem, 0x200, 110);
	EXPECT_TRUE(scanner.NextScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanCompare::Decreased, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 1);
	std::vector<u32> results = scanner.GetResults(20);
	EXPECT_EQ_INT((int)results.size(), 1);
	EXPECT_EQ_INT(results[0], base + 0x100);

	// Undo brings back all three.
	EXPECT_TRUE(scanner.CanUndo());
	scanner.Undo();
	EXPECT_EQ_INT((int)scanner.ResultCount(), 3);

	// Unknown initial value, then unchanged / changed / decreased by.
	EXPECT_TRUE(scanner.NewScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanValueType::U32, ScanCompare::Unknown, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 4096 / 4);
	Put32(mem, 0x200, 105);
	EXPECT_TRUE(scanner.NextScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanCompare::Changed, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 1);
	Put32(mem, 0x200, 100);
	EXPECT_TRUE(Parse(ScanValueType::U32, "5", &v));
	EXPECT_TRUE(scanner.NextScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanCompare::DecreasedBy, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 1);
	EXPECT_TRUE(scanner.NextScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanCompare::Unchanged, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 1);

	// Skipping code: results inside excluded ranges are dropped, including the ends of a range
	// that doesn't start or end on a 64-byte boundary.
	EXPECT_TRUE(scanner.NewScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanValueType::U32, ScanCompare::Unknown, v, v2));
	scanner.ExcludeRanges({ { base + 0x100, 0x100 }, { base + 0x224, 0x88 } });
	EXPECT_EQ_INT((int)scanner.ResultCount(), 4096 / 4 - 0x100 / 4 - 0x88 / 4);
	EXPECT_TRUE(scanner.NextScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanCompare::Unchanged, v, v2));
	for (u32 addr : scanner.GetResults(4096)) {
		EXPECT_FALSE(addr >= base + 0x100 && addr < base + 0x200);
		EXPECT_FALSE(addr >= base + 0x224 && addr < base + 0x2AC);
	}
	// Ranges partly or wholly outside the scanned memory are fine.
	scanner.ExcludeRanges({ { base - 0x10, 0x14 }, { base + 0x2000, 0x10000 } });
	EXPECT_EQ_INT((int)scanner.ResultCount(), 4096 / 4 - 0x100 / 4 - 0x88 / 4 - 1);

	// Static memory minus code: holes in any order, overlapping, or reaching past either end.
	{
		auto pieces = SubtractRanges({ 0x1000, 0x1000 }, { { 0x1800, 0x100 }, { 0x0F00, 0x200 }, { 0x1850, 0x100 }, { 0x1F00, 0x400 } });
		EXPECT_EQ_INT((int)pieces.size(), 2);
		EXPECT_EQ_HEX(pieces[0].first, 0x1100u);
		EXPECT_EQ_HEX(pieces[0].second, 0x700u);
		EXPECT_EQ_HEX(pieces[1].first, 0x1950u);
		EXPECT_EQ_HEX(pieces[1].second, 0x5B0u);
		EXPECT_EQ_INT((int)SubtractRanges({ 0x1000, 0x100 }, {}).size(), 1);
		EXPECT_EQ_INT((int)SubtractRanges({ 0x1000, 0x100 }, { { 0x0, 0x2000 } }).size(), 0);
	}

	// 8-bit scans every byte.
	EXPECT_TRUE(scanner.NewScanOnBuffer(mem.data(), base, (u32)mem.size(), ScanValueType::U8, ScanCompare::Unknown, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 4096);

	// Floats: "95" matches 95.3 (rounded), not 95.6. Between works too.
	std::vector<u8> fmem(256, 0);
	PutF(fmem, 0x10, 95.3f);
	PutF(fmem, 0x20, 95.6f);
	PutF(fmem, 0x30, -3.0f);
	EXPECT_TRUE(Parse(ScanValueType::Float, "95", &v));
	EXPECT_TRUE(scanner.NewScanOnBuffer(fmem.data(), base, (u32)fmem.size(), ScanValueType::Float, ScanCompare::Equal, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 1);
	EXPECT_TRUE(Parse(ScanValueType::Float, "-5", &v));
	EXPECT_TRUE(Parse(ScanValueType::Float, "-1", &v2));
	EXPECT_TRUE(scanner.NewScanOnBuffer(fmem.data(), base, (u32)fmem.size(), ScanValueType::Float, ScanCompare::Between, v, v2));
	EXPECT_EQ_INT((int)scanner.ResultCount(), 1);
	EXPECT_EQ_INT(scanner.GetResults(1)[0], base + 0x30);

	scanner.Reset();
	EXPECT_EQ_INT((int)scanner.ResultCount(), 0);
	EXPECT_FALSE(scanner.HasResults());
	return true;
}

// Turns generated "_L 0xAAAAAAAA 0xBBBBBBBB" lines into a cheat for the engine.
static bool LinesToCheat(const std::vector<std::string> &lines, CheatCode *cheat) {
	cheat->name = "Pointer test";
	cheat->format = CheatCodeFormat::CwCheat;
	cheat->lines.clear();
	for (const std::string &line : lines) {
		CheatLine l;
		if (sscanf(line.c_str(), "_L 0x%x 0x%x", &l.part1, &l.part2) != 2) {
			printf("Bad generated line: %s\n", line.c_str());
			return false;
		}
		cheat->lines.push_back(l);
	}
	return true;
}

static bool RunGeneratedCheat(CWCheatEngine &engine, ScanValueType type, const PointerChain &chain, u32 bits) {
	CheatCode cheat;
	if (!LinesToCheat(MemoryScanner::MakeCwCheatLines(type, chain, bits), &cheat)) {
		return false;
	}
	engine.SetCheats({ cheat });
	engine.Run();
	return true;
}

static bool HasChain(const std::vector<PointerChain> &chains, const PointerChain &wanted) {
	for (const PointerChain &c : chains) {
		if (c == wanted) {
			return true;
		}
	}
	return false;
}

// Pointer chains in emulated RAM: the scan finds them, the generated pointer cheat writes
// through them in the real cheat engine, and filtering keeps only the ones that follow a move.
static bool TestPointerScan() {
	// "Static data" of a pretend game module, and two heap objects.
	const u32 staticStart = 0x08900000;
	const u32 staticSize = 0x1000;
	const u32 player = 0x09A00000;     // the struct the value is in
	const u32 world = 0x09B00000;      // a struct pointing to player
	const u32 target = player + 0x1C;  // the value

	Memory::Memset(staticStart, 0, staticSize, "PointerTest");
	Memory::Memset(player, 0, 0x100, "PointerTest");
	Memory::Memset(world, 0, 0x100, "PointerTest");

	// Depth 1: static -> player.  Depth 2: static -> world -> player.
	Memory::WriteUnchecked_U32(player, staticStart + 0x40);
	Memory::WriteUnchecked_U32(world, staticStart + 0x80);
	Memory::WriteUnchecked_U32(player, world + 0x10);
	Memory::WriteUnchecked_U32(100, target);

	u32 memStart, memSize;
	MemoryScanner::GetPSPScanRange(&memStart, &memSize);
	const u8 *mem = Memory::GetPointerUnchecked(memStart);
	const std::vector<std::pair<u32, u32>> statics = { { staticStart, staticSize } };

	PointerChain one;
	one.base = staticStart + 0x40;
	one.depth = 1;
	one.offsets[0] = 0x1C;
	PointerChain two;
	two.base = staticStart + 0x80;
	two.depth = 2;
	two.offsets[0] = 0x10;
	two.offsets[1] = 0x1C;

	u32 resolved = 0;
	EXPECT_TRUE(two.Resolve(&resolved));
	EXPECT_EQ_HEX(resolved, target);
	EXPECT_EQ_STR(two.ToString(), std::string("[[0x08900080] + 0x10] + 0x1C"));

	PointerScanner scanner;
	PointerScanner::Settings settings;
	settings.maxDepth = 2;
	settings.maxOffset = 0x100;
	EXPECT_TRUE(scanner.ScanBuffer(mem, memStart, memSize, statics, target, ScanValueType::U32, settings));
	EXPECT_TRUE(HasChain(scanner.Results(), one));
	EXPECT_TRUE(HasChain(scanner.Results(), two));
	// Shortest first.
	EXPECT_EQ_INT(scanner.Results()[0].depth, 1);

	// Depth 1 only finds the direct one.
	settings.maxDepth = 1;
	EXPECT_TRUE(scanner.ScanBuffer(mem, memStart, memSize, statics, target, ScanValueType::U32, settings));
	EXPECT_TRUE(HasChain(scanner.Results(), one));
	EXPECT_FALSE(HasChain(scanner.Results(), two));

	// Generated pointer cheats, run by the engine.
	CWCheatEngine engine("TEST00000");
	EXPECT_TRUE(RunGeneratedCheat(engine, ScanValueType::U32, two, 999));
	EXPECT_EQ_INT(Memory::ReadUnchecked_U32(target), 999);
	EXPECT_TRUE(RunGeneratedCheat(engine, ScanValueType::U16, one, 0xBEEF));
	EXPECT_EQ_HEX((u32)Memory::ReadUnchecked_U16(target), 0xBEEF);
	EXPECT_TRUE(RunGeneratedCheat(engine, ScanValueType::U8, two, 0x7F));
	EXPECT_EQ_HEX((u32)Memory::ReadUnchecked_U8(target), 0x7F);
	// A plain address still makes a plain write.
	EXPECT_EQ_INT((int)MemoryScanner::MakeCwCheatLines(ScanValueType::U32, PointerChain::Direct(target), 1).size(), 1);

	// The player moves (new level): world now points to the new one, and the depth 1 pointer is
	// gone. Filtering with the new address keeps only the depth 2 chain.
	settings.maxDepth = 2;
	EXPECT_TRUE(scanner.ScanBuffer(mem, memStart, memSize, statics, target, ScanValueType::U32, settings));
	const u32 player2 = 0x09C00000;
	Memory::Memset(player2, 0, 0x100, "PointerTest");
	Memory::WriteUnchecked_U32(0, staticStart + 0x40);
	Memory::WriteUnchecked_U32(player2, world + 0x10);
	EXPECT_TRUE(scanner.FilterBuffer(mem, memStart, memSize, player2 + 0x1C) >= 1);
	EXPECT_FALSE(HasChain(scanner.Results(), one));
	EXPECT_TRUE(HasChain(scanner.Results(), two));
	EXPECT_EQ_INT(scanner.Scans(), 2);

	// The same cheat now writes to the moved value.
	EXPECT_TRUE(RunGeneratedCheat(engine, ScanValueType::U32, two, 1234));
	EXPECT_EQ_INT(Memory::ReadUnchecked_U32(player2 + 0x1C), 1234);

	// A pointer through the uncached mirror is the same address.
	Memory::WriteUnchecked_U32(player2 | 0x40000000, world + 0x10);
	EXPECT_TRUE(two.Resolve(&resolved));
	EXPECT_EQ_HEX(resolved, player2 + 0x1C);

	// A step of the chain leads outside memory (a stale pointer): the cheat must not write at all.
	// Skipping just that step would write at the pointer before it + the last offset, which here
	// is valid memory (0x09FFFF1C, near the end of RAM).
	PointerChain stale;
	stale.base = staticStart + 0xC0;
	stale.depth = 2;
	stale.offsets[0] = 0x200;
	stale.offsets[1] = 0x1C;
	Memory::WriteUnchecked_U32(0x09FFFF00, stale.base);
	Memory::WriteUnchecked_U32(0x5555, 0x09FFFF1C);
	EXPECT_FALSE(stale.Resolve(&resolved));
	EXPECT_TRUE(RunGeneratedCheat(engine, ScanValueType::U32, stale, 0xDEAD));
	EXPECT_EQ_HEX(Memory::ReadUnchecked_U32(0x09FFFF1C), 0x5555);
	return true;
}

bool TestMemoryScanner() {
	if (!TestValueScans()) {
		return false;
	}
	currentMIPS = &mipsr4k;
	Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE;
	if (!Memory::Init(Memory::MemMapSetupFlags::Default)) {
		printf("Memory::Init failed\n");
		return false;
	}
	const bool success = TestPointerScan();
	Memory::Shutdown();
	return success;
}

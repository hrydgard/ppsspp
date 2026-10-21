#include <cstring>
#include <vector>

#include "Core/MemoryScanner.h"
#include "unittest/UnitTest.h"

static bool Parse(ScanValueType type, const char *text, ScanValue *v) {
	return MemoryScanner::ParseValue(type, text, v);
}

static void Put32(std::vector<u8> &mem, u32 off, u32 v) { memcpy(&mem[off], &v, 4); }
static void PutF(std::vector<u8> &mem, u32 off, float v) { memcpy(&mem[off], &v, 4); }

bool TestMemoryScanner() {
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

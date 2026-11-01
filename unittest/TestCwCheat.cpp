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
#include <string>
#include <vector>

#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/StringUtils.h"
#include "Core/CwCheat.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/MIPS/MIPS.h"
#include "UnitTest.h"

// Most of these are the PSPAR tests from TempAR's docs/tests/tests.db, with their expected results.
static const char *const g_cheatFile = R"(_S TEST00000
_G Cheat tests
_C1 Loop
_M 0xD5000000 0x11223344
_M 0xC0000000 0x00000002
_M 0xD6000000 0x08802000
_M 0xD4000000 0x44444444
_M 0xD2000000 0x00000000
_C1 Loop without terminator
_M 0xD5000000 0x11223344
_M 0xC0000000 0x00000002
_M 0xD6000000 0x08802000
_M 0xD4000000 0x44444444
_C1 Patch
_M 0xE8802000 0x0000000C
_M 0x11223344 0x55667788
_M 0x99AABBCC 0x00000000
_C1 Patch with extra lines
_M 0xE8802000 0x0000000C # Expected Result:
_M 0x11223344 0x55667788 #   0x08802000 = 0x11223344; 0x08802004 = 0x55667788
_M 0x99AABBCC 0x00000000 #   0x08802008 = 0x99AABBCC; 0x0880200C = 0xDDEEFF00
_M 0x0880200C 0xDDEEFF00 #
_C1 Memory copy
_M 0xD3000000 0x08802000
_M 0x00000000 0x11223344
_M 0xF8802008 0x00000004
_C1 Write offset
_M 0x08802000 0x08802004
_M 0x08802004 0x08802020
_M 0x68802000 0x00000000
_M 0xB8802000 0x00000000
_M 0xB0000000 0x00000000
_M 0xC6000000 0x08802008
_M 0xD2000000 0x00000000
_C1 Multi-level pointer
_M 0x08802000 0x08802004
_M 0x08802004 0x08802020
_M 0x68802000 0x00000000
_M 0xB8802000 0x00000000
_M 0xB0000000 0x00000000
_M 0x00000000 0x12345678
_M 0xD2000000 0x00000000
_C1 Data register add
_M 0x08802000 0x11111111
_M 0xD9000000 0x08802000
_M 0xD4000000 0x11111111
_M 0xD6000000 0x08802004
_M 0xD2000000 0x00000000
_C1 Data register sub
_M 0x08802000 0x22222222
_M 0xD9000000 0x08802000
_M 0xD4000000 0xEEEEEEEF
_M 0xD6000000 0x08802004
_M 0xD2000000 0x00000000
_C1 Safe data store
_M 0xC4000000 0x00000000
_M 0x00000020 0x08802000
_M 0x00000024 0x12345678
_M 0xD2000000 0x00000000
_M 0x00000000 0x00000000
_C1 Conditions and terminator
_M 0x08802000 0x00001234
_M 0x58802000 0x00001234
_M 0x08802004 0x11111111
_M 0x58802000 0x00005678
_M 0x08802008 0x22222222
_M 0xD0000000 0x00000000
_M 0x0880200C 0x33333333
_M 0xD0000000 0x00000000
_M 0x88802000 0xFF0F0005
_M 0x08802010 0x44444444
_M 0xD2000000 0x00000000
_M 0x28802014 0x00000055
_C1 Pad (PSPAR address)
_M 0x99F00000 0x00000200
_M 0x08802000 0x11111111
_M 0xD0000000 0x00000000
_M 0xA9F00000 0x00000200
_M 0x08802004 0x22222222
_C1 Pad (TempAR address)
_M 0x9A000000 0xFDFF0200
_M 0x08802000 0x33333333
_C1 CwCheat with a comment
_L 0x20002000 0x11111111 # Comment
)";

struct ExpectedWord {
	u32 addr;
	u32 value;
};

static bool RunCheatTest(CWCheatEngine &engine, const CheatCode &cheat, const std::vector<ExpectedWord> &expected) {
	Memory::Memset(0x08801F00, 0, 0x400, "CheatTest");
	engine.SetCheats({ cheat });
	engine.Run();
	for (const ExpectedWord &e : expected) {
		u32 actual = Memory::ReadUnchecked_U32(e.addr);
		if (actual != e.value) {
			printf("Cheat '%s': %08x = %08x, expected %08x\n", cheat.name.c_str(), e.addr, actual, e.value);
			return false;
		}
	}
	return true;
}

static bool RunCheatTests() {
	const Path filename = UnitTestTempPath("unittest_cheats.ini");
	if (!File::WriteStringToFile(true, g_cheatFile, filename)) {
		printf("Can't write %s\n", filename.c_str());
		return false;
	}
	bool parsed;
	std::vector<CheatCode> cheats;
	{
		// The parser holds the file open, which keeps it from being deleted on Windows.
		CheatFileParser parser(filename, "TEST00000");
		parsed = parser.Parse();
		for (const std::string &error : parser.GetErrors()) {
			printf("%s\n", error.c_str());
		}
		cheats = parser.GetCheats();
	}
	File::Delete(filename);
	EXPECT_TRUE(parsed);
	EXPECT_EQ_INT((int)cheats.size(), 14);

	const std::vector<std::vector<ExpectedWord>> expected = {
		{ { 0x08802000, 0x11223344 }, { 0x08802004, 0x55667788 }, { 0x08802008, 0x99AABBCC } },
		{ { 0x08802000, 0x11223344 }, { 0x08802004, 0x55667788 }, { 0x08802008, 0x99AABBCC } },
		{ { 0x08802000, 0x11223344 }, { 0x08802004, 0x55667788 }, { 0x08802008, 0x99AABBCC }, { 0x0880200C, 0 } },
		{ { 0x08802000, 0x11223344 }, { 0x08802004, 0x55667788 }, { 0x08802008, 0x99AABBCC }, { 0x0880200C, 0xDDEEFF00 } },
		{ { 0x08802000, 0x11223344 }, { 0x08802008, 0x11223344 } },
		{ { 0x08802000, 0x08802004 }, { 0x08802004, 0x08802020 }, { 0x08802008, 0x08802020 } },
		{ { 0x08802000, 0x08802004 }, { 0x08802004, 0x08802020 }, { 0x08802020, 0x12345678 } },
		{ { 0x08802004, 0x22222222 } },
		{ { 0x08802004, 0x11111111 } },
		{ { 0x08802000, 0x12345678 } },
		{ { 0x08802004, 0x11111111 }, { 0x08802008, 0 }, { 0x0880200C, 0x33333333 }, { 0x08802010, 0x44444444 }, { 0x08802014, 0x55 } },
	};

	CWCheatEngine engine("TEST00000");
	bool success = cheats.size() == 14;
	for (size_t i = 0; success && i < expected.size(); i++) {
		success = RunCheatTest(engine, cheats[i], expected[i]);
	}

	if (success) {
		__CtrlUpdateButtons(0, 0xFFFFFFFF);
		success = RunCheatTest(engine, cheats[11], { { 0x08802000, 0 }, { 0x08802004, 0x22222222 } });
		success = success && RunCheatTest(engine, cheats[12], { { 0x08802000, 0 } });
		__CtrlUpdateButtons(0x00000200 | 0x00004000, 0);
		success = success && RunCheatTest(engine, cheats[11], { { 0x08802000, 0x11111111 }, { 0x08802004, 0 } });
		success = success && RunCheatTest(engine, cheats[12], { { 0x08802000, 0x33333333 } });
		__CtrlUpdateButtons(0, 0xFFFFFFFF);
		success = success && RunCheatTest(engine, cheats[13], { { 0x08802000, 0x11111111 } });
	}

	return success;
}

static bool TestCheatParseErrors() {
	const Path filename = UnitTestTempPath("unittest_cheats_bad.ini");
	const char *content = "_S TEST00000\n_C1 Mixed\n_L 0x20002000 0x00000001\n_M 0x08802004 0x00000002\n_C1 Extended\n_N 0xD4000001 0x00000001\n";
	if (!File::WriteStringToFile(true, content, filename)) {
		return false;
	}
	bool parsed;
	size_t errorCount;
	std::vector<CheatCode> cheats;
	{
		CheatFileParser parser(filename, "TEST00000");
		parsed = parser.Parse();
		errorCount = parser.GetErrors().size();
		cheats = parser.GetCheats();
	}
	File::Delete(filename);
	EXPECT_FALSE(parsed);
	EXPECT_EQ_INT((int)errorCount, 2);
	EXPECT_EQ_INT((int)cheats.size(), 1);
	EXPECT_EQ_INT((int)cheats[0].lines.size(), 1);
	EXPECT_TRUE(cheats[0].format == CheatCodeFormat::CwCheat);
	return true;
}

// Cheats pasted from websites, with a BOM, no _S line and a U+202C (pop directional formatting)
// after every address (#14955).
static bool TestCheatInvisibleCharacters() {
	const Path filename = UnitTestTempPath("unittest_cheats_invisible.ini");
	const char *content = "\xEF\xBB\xBF\n_C1 Blade Change\xE2\x80\xAC\n_L 0x002F614A\xE2\x80\xAC 0x0000003D\n_L\xC2\xA0" "0x002F614B 0x00000018\n";
	if (!File::WriteStringToFile(true, content, filename)) {
		return false;
	}
	bool parsed;
	std::vector<CheatCode> cheats;
	std::vector<CheatFileInfo> info;
	{
		CheatFileParser parser(filename, "TEST00000");
		parsed = parser.Parse();
		cheats = parser.GetCheats();
		info = parser.GetFileInfo();
	}
	File::Delete(filename);
	EXPECT_TRUE(parsed);
	EXPECT_EQ_INT((int)cheats.size(), 1);
	EXPECT_EQ_INT((int)cheats[0].lines.size(), 2);
	EXPECT_EQ_HEX(cheats[0].lines[0].part1, 0x002F614A);
	EXPECT_EQ_HEX(cheats[0].lines[1].part2, 0x00000018);
	EXPECT_EQ_INT((int)info.size(), 1);
	EXPECT_EQ_STR(info[0].name, std::string("Blade Change"));

	// The length limits still apply, after the characters are dropped.
	EXPECT_EQ_STR(SanitizeString("a\xE2\x80\x8B" "bcd", StringRestriction::NoInvisibleFormatting, 0, 3), std::string("abc"));
	EXPECT_EQ_STR(SanitizeString("a\xE2\x80\x8B" "b", StringRestriction::NoInvisibleFormatting, 3), std::string());
	return true;
}

bool TestCwCheat() {
	// Other tests may leave currentMIPS pointing elsewhere; the cheat engine needs the real one.
	currentMIPS = &mipsr4k;
	Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE;
	if (!Memory::Init(Memory::MemMapSetupFlags::Default)) {
		printf("Memory::Init failed\n");
		return false;
	}
	const bool success = RunCheatTests() && TestCheatParseErrors() && TestCheatInvisibleCharacters();
	Memory::Shutdown();
	return success;
}

// Rough and ready CwCheats implementation, disabled by default.

#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#include "Common/File/Path.h"
#include "Core/MemMap.h"

class PointerWrap;

void __CheatInit();
void __CheatShutdown();
void __CheatDoState(PointerWrap &p);

// Return whether cheats are enabled and in effect.
bool CheatsInEffect();

struct CheatLine {
	uint32_t part1;
	uint32_t part2;
};

enum class CheatCodeFormat {
	UNDEFINED,
	CWCHEAT,
	TEMPAR,
};

struct CheatCode {
	CheatCodeFormat fmt;
	std::vector<CheatLine> lines;
};

struct CheatFileInfo {
	int lineNum;
	std::string name;
	bool enabled;
};

struct CheatOperation;

class CWCheatEngine {
public:
	CWCheatEngine(const std::string &gameID);
	std::vector<CheatFileInfo> FileInfo();
	void ParseCheats();
	void CreateCheatFile();
	Path CheatFilename();
	void Run();
	bool HasCheats();
	void InvalidateICache(u32 addr, int size);
private:
	u32 GetAddress(u32 value);

	CheatOperation InterpretNextOp(const CheatCode &cheat, size_t &i);
	CheatOperation InterpretNextCwCheat(const CheatCode &cheat, size_t &i);
	CheatOperation InterpretNextTempAR(const CheatCode &cheat, size_t &i);

	void ExecuteOp(const CheatOperation &op, const CheatCode &cheat, size_t &i);
	void ApplyMemoryOperator(const CheatOperation &op, uint32_t(*oper)(uint32_t, uint32_t));
	bool TestIf(const CheatOperation &op, bool(*oper)(int a, int b));
	bool TestIfAddr(const CheatOperation &op, bool(*oper)(int a, int b));

	std::vector<CheatCode> cheats_;
	std::string gameID_;
	Path filename_;
};

// Rough and ready CwCheats implementation, disabled by default.

#pragma once

#include <string>
#include <string_view>
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

struct CheatCode {
	std::string name;
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
	CWCheatEngine(std::string_view gameID);
	std::vector<CheatFileInfo> FileInfo() const;
	void ParseCheats();
	void CreateCheatFile();
	const Path &CheatFilename() const {
		return filename_;
	}
	void Run();
	bool HasCheats();
	static u32 GetAddress(u32 value) {
		// TODO: This comment is weird:
		// Returns static address used by ppsspp. Some games may not like this, and causes cheats to not work without offset
		u32 address = (value + 0x08800000) & 0x3FFFFFFF;
		return address;
	}

private:
	void InvalidateICache(u32 addr, int size) const;

	CheatOperation InterpretNextCwCheat(const CheatCode &cheat, size_t &i);

	void ExecuteOp(const CheatOperation &op, const CheatCode &cheat, size_t &i);
	inline void ApplyMemoryOperator(const CheatOperation &op, uint32_t(*oper)(uint32_t, uint32_t));
	inline bool TestIf(const CheatOperation &op, bool(*oper)(int a, int b)) const;
	inline bool TestIfAddr(const CheatOperation &op, bool(*oper)(int a, int b)) const;

	std::vector<CheatCode> cheats_;
	std::string gameID_;
	Path filename_;
};

class CheatFileParser {
public:
	CheatFileParser(const Path &filename, std::string_view gameID = "");
	~CheatFileParser();

	bool Parse();

	const std::vector<std::string> &GetErrors() const { return errors_; }
	const std::vector<CheatCode> &GetCheats() const { return cheats_; }
	const std::vector<CheatFileInfo> &GetFileInfo() const { return cheatInfo_; }

protected:
	void Flush();
	void FlushCheatInfo();
	void AddError(const std::string &msg, int lineNumber);
	void ParseLine(const std::string &line, int lineNumber);
	void ParseDataLine(const std::string &line, int lineNumber);
	bool ValidateGameID(std::string_view gameID);

	FILE *fp_ = nullptr;
	std::string validGameID_;

	int games_ = 0;
	std::vector<std::string> errors_;
	std::vector<CheatFileInfo> cheatInfo_;
	std::vector<CheatCode> cheats_;
	std::vector<CheatLine> pendingLines_;
	CheatFileInfo lastCheatInfo_;
	bool gameEnabled_ = true;
	bool gameRiskyEnabled_ = false;
	bool cheatEnabled_ = false;
};

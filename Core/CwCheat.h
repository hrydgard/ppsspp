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

bool DetectCheatTitle(std::string_view name, std::string_view *title);
bool DetectCheatPostComment(std::string_view name, std::string_view *title);

enum class CheatCodeFormat {
	CwCheat,  // _L lines
	PSPAR,    // _M lines (Datel's PSP Action Replay code types, as run by TempAR)
};

struct CheatCode {
	std::string name;
	std::vector<CheatLine> lines;
	CheatCodeFormat format = CheatCodeFormat::CwCheat;
};

struct CheatFileInfo {
	int lineNum;
	std::string name;
	bool enabled;

	bool IsTitle(std::string_view *title) const {
		return DetectCheatTitle(name, title);
	}
	bool IsPostComment(std::string_view *comment) const {
		return DetectCheatPostComment(name, comment);
	}
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
	// Replaces the parsed cheats, for tests.
	void SetCheats(const std::vector<CheatCode> &cheats) {
		cheats_ = cheats;
	}
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

	// Takes the cheat non-const: PSPAR's safe data store (0xC4) lets a code write over its own lines.
	void RunPSPAR(CheatCode &cheat);
	u32 PSPARRead(const CheatCode &cheat, u32 addr, int size) const;
	void PSPARWrite(CheatCode &cheat, u32 addr, u32 value, int size) const;

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
	void ParseDataLine(const std::string &line, int lineNumber, CheatCodeFormat format);
	bool ValidateGameID(std::string_view gameID);

	FILE *fp_ = nullptr;
	std::string validGameID_;

	int games_ = 0;
	std::vector<std::string> errors_;
	std::vector<CheatFileInfo> cheatInfo_;
	std::vector<CheatCode> cheats_;
	std::vector<CheatLine> pendingLines_;
	CheatCodeFormat pendingFormat_ = CheatCodeFormat::CwCheat;
	CheatFileInfo lastCheatInfo_;
	bool gameEnabled_ = true;
	bool gameRiskyEnabled_ = false;
	bool cheatEnabled_ = false;
};

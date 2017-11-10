// Rough and ready CwCheats implementation, disabled by default.
// Will not enable by default until the TOOD:s have been addressed.

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#include "base/basictypes.h"
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

struct CheatOperation;

class CWCheatEngine {
public:
	CWCheatEngine();
	std::vector<std::string> GetCodesList();
	void ParseCheats();
	void CreateCheatFile();
	void Run();
	bool HasCheats();

private:
	void InvalidateICache(u32 addr, int size);
	u32 GetAddress(u32 value);

	CheatOperation InterpretNextOp(const CheatCode &cheat, size_t &i);
	CheatOperation InterpretNextCwCheat(const CheatCode &cheat, size_t &i);
	CheatOperation InterpretNextTempAR(const CheatCode &cheat, size_t &i);

	void ExecuteOp(const CheatOperation &op, const CheatCode &cheat, size_t &i);
	void ApplyMemoryOperator(const CheatOperation &op, uint32_t(*oper)(uint32_t, uint32_t));
	bool TestIf(const CheatOperation &op, bool(*oper)(int a, int b));
	bool TestIfAddr(const CheatOperation &op, bool(*oper)(int a, int b));

	std::vector<CheatCode> cheats_;
};

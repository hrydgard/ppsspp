// Rough and ready CwCheats implementation, disabled by default.
// Will not enable by default until the TOOD:s have been addressed.

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#include "base/basictypes.h"
#include "Core/MemMap.h"

void __CheatInit();
void __CheatShutdown();

std::vector<std::string> makeCodeParts();

class CWCheatEngine {
public:
	CWCheatEngine();
	std::string String();
	void AddCheatLine(std::string& line);
	std::vector<std::string> GetCodesList();
	void CreateCodeList();
	void Exit();
	void Run();
	std::vector<std::string> GetNextCode();

private:
	void SkipCodes(int count);
	void SkipAllCodes();
	
	int GetAddress(int value);

	static uint64_t const serialVersionUID = 6791588139795694296ULL;
	static const int cheatsThreadSleepMillis = 5;

	bool cheatsOn;
	std::vector<std::string> codes;
	int currentCode;
	bool exit2;
	std::vector<std::string> parts;
};

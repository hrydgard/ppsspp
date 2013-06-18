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

std::vector<std::string> makeCodeParts(std::vector<std::string> CodesList);

class CWCheatEngine {
public:
	CWCheatEngine();
	std::string String();
	void AddCheatLine(std::string& line);
	std::vector<std::string> GetCodesList();
	void CreateCodeList();
	void Exit();
	void Run();
	std::vector<int> GetNextCode();
	

private:
	void SkipCodes(int count);
	void SkipAllCodes();
	bool cheatsOn, exit2, cheatEnabled;
	int GetAddress(int value);
	std::vector<std::string> codeNameList;

	std::vector<std::string> codes, initialCodesList, parts;
	size_t currentCode;
};

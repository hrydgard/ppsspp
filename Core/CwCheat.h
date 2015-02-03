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

class CWCheatEngine {
public:
	CWCheatEngine();
	std::vector<std::string> GetCodesList();
	void CreateCodeList();
	void Exit();
	void Run();
	std::vector<int> GetNextCode();
	bool HasCheats();

private:
	void InvalidateICache(u32 addr, int size);
	void SkipCodes(int count);
	void SkipAllCodes();
	bool cheatsOn, exit2, cheatEnabled;
	int GetAddress(int value);
	std::vector<std::string> codeNameList;

	std::vector<std::string> codes, initialCodesList, parts;
	size_t currentCode;
};


#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include "Core\MemMap.h"

void __CheatInit();
void __CheatShutdown();
//void Register_CwCheat();
using namespace std;



vector<string> makeCodeParts(string l);
void trim2(string& str);
static long parseLong(string s);
static long parseHexLong(string s);


class CheatsGUI {
private:
	static long const serialVersionUID = 6791588139795694296L;
	static long const int cheatsThreadSleepMillis = 5;
	bool cheatsOn;
	std::vector<std::string> codes;
	int currentCode;
	bool exit2;
	void skipCodes(int count);
	void skipAllCodes();
	int getAddress(int value);
	bool cheatsThread;

public:
	CheatsGUI();
	string String();
	void AddCheatLine(string& line);
	void OnCheatsThreadEnded();
	void Dispose();
	vector<string> GetCodesList();

	void Exit();
	void Run();
	string GetNextCode();
};





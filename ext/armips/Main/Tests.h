#pragma once
#include "Util/Util.h"

#ifdef _WIN32
#include <windows.h>
#endif

class TestRunner
{
public:
	bool runTests(const std::wstring& dir);
private:
	enum class ConsoleColors { White, Red, Green };
	
	StringList getTestsList(const std::wstring& dir, const std::wstring& prefix = L"/");
	bool executeTest(const std::wstring& dir, const std::wstring& testName, std::wstring& errorString);
	StringList listSubfolders(const std::wstring& dir);
	void initConsole();
	void changeConsoleColor(ConsoleColors color);
	void restoreConsole();

#ifdef _WIN32
	HANDLE hstdin;
	HANDLE hstdout;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
#endif

};

bool runTests(const std::wstring& dir);

#include "stdafx.h"
#include "Tests.h"
#include "Util/Util.h"
#include "Core/Common.h"
#include "Core/Assembler.h"

#ifndef _WIN32
#include <dirent.h>
#endif

StringList TestRunner::listSubfolders(const std::wstring& dir)
{
	StringList result;
	
#ifdef _WIN32
	WIN32_FIND_DATAW findFileData;
	HANDLE hFind;

	std::wstring m = dir + L"*";
	hFind = FindFirstFileW(m.c_str(),&findFileData);
	
	if (hFind != INVALID_HANDLE_VALUE) 
	{
		do
		{
			if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				std::wstring dirName = findFileData.cFileName;
				if (dirName != L"." && dirName != L"..")
					result.push_back(dirName);
			}
			
		} while (FindNextFileW(hFind,&findFileData));
	}
#else
	std::string utf8 = convertWStringToUtf8(dir);
	auto directory = opendir(utf8.c_str());

	if (directory != NULL)
	{
		auto elem = readdir(directory);
		while (elem != NULL)
		{
			if(elem->d_type == DT_DIR)
			{
				std::wstring dirName = convertUtf8ToWString(elem->d_name);
				if (dirName != L"." && dirName != L"..")
					result.push_back(dirName);
			}

			elem = readdir(directory);
		}
	}
#endif

	return result;
}

void TestRunner::initConsole()
{
#ifdef _WIN32
	// initialize console
	hstdin = GetStdHandle(STD_INPUT_HANDLE);
	hstdout = GetStdHandle(STD_OUTPUT_HANDLE);

	// Remember how things were when we started
	GetConsoleScreenBufferInfo(hstdout,&csbi);
#endif
}

void TestRunner::changeConsoleColor(ConsoleColors color)
{
#ifdef _WIN32
	switch (color)
	{
	case ConsoleColors::White:
		SetConsoleTextAttribute(hstdout,0x7);
		break;
	case ConsoleColors::Red:
		SetConsoleTextAttribute(hstdout,(1 << 2) | (1 << 3));
		break;
	case ConsoleColors::Green:
		SetConsoleTextAttribute(hstdout,(1 << 1) | (1 << 3));
		break;
	}
#else
	switch (color)
	{
	case ConsoleColors::White:
		Logger::print(L"\033[1;0m");
		break;
	case ConsoleColors::Red:
		Logger::print(L"\033[1;31m");
		break;
	case ConsoleColors::Green:
		Logger::print(L"\033[1;32m");
		break;
	}
#endif
}

void TestRunner::restoreConsole()
{
#ifdef _WIN32
	FlushConsoleInputBuffer(hstdin);
	SetConsoleTextAttribute(hstdout,csbi.wAttributes);
#endif
}

StringList TestRunner::getTestsList(const std::wstring& dir, const std::wstring& prefix)
{
	StringList tests;

	StringList dirs = listSubfolders(dir+prefix);
	for (std::wstring& dirName: dirs)
	{
		std::wstring testName = prefix + dirName;
		std::wstring fileName = dir + testName + L"/" + dirName + L".asm";

		if (fileExists(fileName))
		{
			if (testName[0] == L'/')
				testName.erase(0,1);
			tests.push_back(testName);
		} else {
			StringList subTests = getTestsList(dir,testName+L"/");
			tests.insert(tests.end(),subTests.begin(),subTests.end());
		}
	}

	return tests;
}

bool TestRunner::executeTest(const std::wstring& dir, const std::wstring& testName, std::wstring& errorString)
{
	std::wstring oldDir = getCurrentDirectory();
	changeDirectory(dir);

	ArmipsArguments args;
	StringList errors;

	args.inputFileName = testName + L".asm";
	args.tempFileName = testName + L".temp.txt";
	args.errorsResult = &errors;
	args.silent = true;

	// may or may not be supposed to cause errors
	runArmips(args);

	// check errors
	bool result = true;
	if (fileExists(L"expected.txt"))
	{
		TextFile f;
		f.open(L"expected.txt",TextFile::Read);
		StringList expectedErrors = f.readAll();

		if (errors.size() == expectedErrors.size())
		{
			for (size_t i = 0; i < errors.size(); i++)
			{
				if (errors[i] != expectedErrors[i])
				{
					errorString += formatString(L"Unexpected error: %S\n",errors[i]);
					result = false;
				}
			}
		} else {
			result = false;
		}
	} else {
		// if no errors are expected, there should be none
		for (size_t i = 0; i < errors.size(); i++)
		{
			errorString += formatString(L"Unexpected error: %S\n",errors[i]);
			result = false;
		}
	}

	// write errors to file
	TextFile output;
	output.open(L"output.txt",TextFile::Write);
	output.writeLines(errors);
	output.close();

	if (fileExists(L"expected.bin"))
	{
		ByteArray expected = ByteArray::fromFile(L"expected.bin");
		ByteArray actual = ByteArray::fromFile(L"output.bin");

		if (expected.size() == actual.size())
		{
			if (memcmp(expected.data(),actual.data(),actual.size()) != 0)
			{
				errorString += formatString(L"Output data does not match\n");
				result = false;
			}
		} else {
			errorString += formatString(L"Output data size does not match\n");
			result = false;
		}
	}

	changeDirectory(oldDir);
	return result;
}

bool TestRunner::runTests(const std::wstring& dir)
{
	StringList tests = getTestsList(dir);
	if (tests.empty())
	{
		Logger::printLine(L"No tests to run");
		return true;
	}

	initConsole();

	unsigned int successCount = 0;
	for (size_t i = 0; i < tests.size(); i++)
	{
		changeConsoleColor(ConsoleColors::White);

		std::wstring line = formatString(L"Test %d of %d, %s:",i+1,tests.size(),tests[i]);
		Logger::print(L"%-50s",line);

		std::wstring path = dir + L"/" + tests[i];
		std::wstring errors;

		size_t n = tests[i].find_last_of('/');
		std::wstring testName = n == tests[i].npos ? tests[i] : tests[i].substr(n+1);
		if (executeTest(path,testName,errors) == false)
		{
			changeConsoleColor(ConsoleColors::Red);
			Logger::printLine(L"FAILED");
			Logger::print(L"%s",errors);
		} else {
			changeConsoleColor(ConsoleColors::Green);
			Logger::printLine(L"PASSED");
			successCount++;
		}
	}
	
	changeConsoleColor(ConsoleColors::White);
	Logger::printLine(L"\n%d out of %d tests passed.",successCount,tests.size());
	
	restoreConsole();
	return successCount == tests.size();
}

bool runTests(const std::wstring& dir)
{
	TestRunner runner;
	return runner.runTests(dir);
}

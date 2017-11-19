#include "stdafx.h"
#include "Core/Common.h"
#include "Core/Assembler.h"
#include "Archs/MIPS/Mips.h"
#include "Commands/CDirectiveFile.h"
#include "Tests.h"

#if defined(_WIN64) || defined(__x86_64__) || defined(__amd64__)
#define ARMIPSNAME "ARMIPS64"
#else
#define ARMIPSNAME "ARMIPS"
#endif

void printUsage(std::wstring executableName)
{
	Logger::printLine(L"%s Assembler v%d.%d.%d (%s %s) by Kingcom",
		ARMIPSNAME, ARMIPS_VERSION_MAJOR, ARMIPS_VERSION_MINOR, ARMIPS_VERSION_REVISION, __DATE__, __TIME__);
	Logger::printLine(L"Usage: %s [optional parameters] <FILE>", executableName);
	Logger::printLine(L"");
	Logger::printLine(L"Optional parameters:");
	Logger::printLine(L" -temp <TEMP>         Output temporary assembly data to <TEMP> file");
	Logger::printLine(L" -sym  <SYM>          Output symbol data in the sym format to <SYM> file");
	Logger::printLine(L" -sym2 <SYM2>         Output symbol data in the sym2 format to <SYM2> file");
	Logger::printLine(L" -root <ROOT>         Use <ROOT> as working directory during execution");
	Logger::printLine(L" -equ  <NAME> <VAL>   Equivalent to \'<NAME> equ <VAL>\' in code");
	Logger::printLine(L" -strequ <NAME> <VAL> Equivalent to \'<NAME> equ \"<VAL>\"\' in code");
	Logger::printLine(L" -erroronwarning      Treat all warnings like errors");
	Logger::printLine(L"");
	Logger::printLine(L"File arguments:");
	Logger::printLine(L" <FILE>               Main assembly code file");
}

int wmain(int argc, wchar_t* argv[])
{
	std::setlocale(LC_CTYPE,"");

	ArmipsArguments parameters;

#ifdef ARMIPS_TESTS
	std::wstring name;

	if (argc < 2)
		return !runTests(L"Tests");
	else
		return !runTests(argv[1]);
#endif

	StringList arguments = getStringListFromArray(argv,argc);
	size_t argpos = 1;
	bool readflags = true;
	while (argpos < arguments.size())
	{
		if (readflags && arguments[argpos][0] == L'-')
		{
			if (arguments[argpos] == L"--")
			{
				readflags = false;
				argpos += 1;
			} else if (arguments[argpos] == L"-temp" && argpos + 1 < arguments.size())
			{
				parameters.tempFileName = arguments[argpos + 1];
				argpos += 2;
			} else if (arguments[argpos] == L"-sym" && argpos + 1 < arguments.size())
			{
				parameters.symFileName = arguments[argpos + 1];
				parameters.symFileVersion = 1;
				argpos += 2;
			} else if (arguments[argpos] == L"-sym2" && argpos + 1 < arguments.size())
			{
				parameters.symFileName = arguments[argpos + 1];
				parameters.symFileVersion = 2;
				argpos += 2;
			} else if (arguments[argpos] == L"-erroronwarning")
			{
				parameters.errorOnWarning = true;
				argpos += 1;
			} else if (arguments[argpos] == L"-equ" && argpos + 2 < arguments.size())
			{
				EquationDefinition def;
				def.name = arguments[argpos + 1];
				def.value = arguments[argpos + 2];
				parameters.equList.push_back(def);
				argpos += 3;
			} else if (arguments[argpos] == L"-strequ" && argpos + 2 < arguments.size())
			{
				EquationDefinition def;
				def.name = arguments[argpos + 1];
				def.value = formatString(L"\"%s\"", arguments[argpos + 2]);
				parameters.equList.push_back(def);
				argpos += 3;
			} else if (arguments[argpos] == L"-time")
			{
				Logger::printError(Logger::Warning, L"-time flag is deprecated");
				argpos += 1;
			} else if (arguments[argpos] == L"-root" && argpos + 1 < arguments.size())
			{
				changeDirectory(arguments[argpos + 1]);
				argpos += 2;
			} else {
				Logger::printError(Logger::Error, L"Invalid command line argument '%s'\n", arguments[argpos]);
				printUsage(arguments[0]);
				return 1;
			}
		} else {
			// only allow one input filename
			if (parameters.inputFileName == L"")
			{
				parameters.inputFileName = arguments[argpos];
				argpos++;
			} else {
				Logger::printError(Logger::Error, L"Multiple input assembly files specified\n");
				printUsage(arguments[0]);
				return 1;
			}
		}
	}

	// ensure input file was specified
	if (parameters.inputFileName == L"")
	{
		if(arguments.size() > 1)
			Logger::printError(Logger::Error, L"Missing input assembly file\n");

		printUsage(arguments[0]);
		return 1;
	}

	// turn input filename into an absolute path
	if (isAbsolutePath(parameters.inputFileName) == false)
		parameters.inputFileName = formatString(L"%s/%s", getCurrentDirectory(), parameters.inputFileName);

	if (fileExists(parameters.inputFileName) == false)
	{
		Logger::printError(Logger::Error, L"File '%s' not found\n", parameters.inputFileName);
		return 1;
	}

	bool result = runArmips(parameters);

	if (result == false)
	{
		Logger::printLine(L"Aborting.");
		return 1;
	}

	return 0;
}

#ifndef _WIN32

int main(int argc, char* argv[])
{
	// convert input to wstring
	std::vector<std::wstring> wideStrings;
	for (int i = 0; i < argc; i++)
	{
		std::wstring str = convertUtf8ToWString(argv[i]);
		wideStrings.push_back(str);
	}

	// create argv replacement
	wchar_t** wargv = new wchar_t*[argc];
	for (int i = 0; i < argc; i++)
	{
		wargv[i] = (wchar_t*) wideStrings[i].c_str();
	}

	int result = wmain(argc,wargv);

	delete[] wargv;
	return result;
}

#endif

#pragma once

#include <vector>
#include "SymbolTable.h"
#include "Commands/CAssemblerCommand.h"
#include "Util/EncodingTable.h"
#include "Util/FileClasses.h"
#include "Core/Misc.h"
#include "Core/SymbolData.h"
#include "Archs/Architecture.h"

typedef struct {
	std::vector<std::wstring> FileList;
	int FileCount;
	int FileNum;
	int LineNumber;
	int TotalLineCount;
} tFileInfo;

typedef struct {
	tFileInfo FileInfo;
	SymbolTable symbolTable;
	EncodingTable Table;
	int Section;
	bool nocash;
	bool relativeInclude;
	int validationPasses;
	bool memoryMode;
	AssemblerFile* memoryFile;
	bool multiThreading;
} tGlobal;

extern tGlobal Global;
extern CArchitecture* Arch;

class FileManager;
extern FileManager* g_fileManager;

std::wstring getFolderNameFromPath(const std::wstring& src);
std::wstring getFullPathName(const std::wstring& path);

bool checkLabelDefined(const std::wstring& labelName, int section);
bool checkValidLabelName(const std::wstring& labelName);

bool isPowerOfTwo(int64_t n);

// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "Globals.h"
#include "native/base/mutex.h"
#include <vector>
#include <set>
#include <map>

enum SymbolType {
	ST_NONE     = 0,
	ST_FUNCTION = 1,
	ST_DATA     = 2,
	ST_ALL      = 3,
};

struct SymbolInfo {
	SymbolType type;
	u32 address;
	u32 size;
};

struct SymbolEntry {
	std::string name;
	u32 address;
	u32 size;
};

enum DataType {
	DATATYPE_NONE, DATATYPE_BYTE, DATATYPE_HALFWORD, DATATYPE_WORD, DATATYPE_ASCII
};

#ifdef _WIN32
struct HWND__;
typedef struct HWND__ *HWND;
#endif

class SymbolMap {
public:
	SymbolMap() {}
	void Clear();
	void SortSymbols();

	bool LoadSymbolMap(const char *filename);
	void SaveSymbolMap(const char *filename) const;
	bool LoadNocashSym(const char *ilename);

	SymbolType GetSymbolType(u32 address) const;
	bool GetSymbolInfo(SymbolInfo *info, u32 address, SymbolType symmask = ST_FUNCTION) const;
	u32 GetNextSymbolAddress(u32 address, SymbolType symmask);
	const char *GetDescription(unsigned int address) const;
	std::vector<SymbolEntry> GetAllSymbols(SymbolType symmask);

#ifdef _WIN32
	void FillSymbolListBox(HWND listbox, SymbolType symType) const;
#endif

	void AddFunction(const char* name, u32 address, u32 size);
	u32 GetFunctionStart(u32 address) const;
	int GetFunctionNum(u32 address) const;
	u32 GetFunctionSize(u32 startAddress) const;
	bool SetFunctionSize(u32 startAddress, u32 newSize);
	bool RemoveFunction(u32 startAddress, bool removeName);

	void AddLabel(const char* name, u32 address);
	void SetLabelName(const char* name, u32 address);
	const char* GetLabelName(u32 address) const;
	bool GetLabelValue(const char* name, u32& dest);

	void AddData(u32 address, u32 size, DataType type);
	u32 GetDataStart(u32 address) const;
	u32 GetDataSize(u32 startAddress) const;
	DataType GetDataType(u32 startAddress) const;

	static const u32 INVALID_ADDRESS = (u32)-1;
private:
	void AssignFunctionIndices();

	struct FunctionEntry {
		u32 size;
		int index;
	};

	struct LabelEntry {
		char name[128];
	};

	struct DataEntry {
		DataType type;
		u32 size;
	};

	std::map<u32, FunctionEntry> functions;
	std::map<u32, LabelEntry> labels;
	std::map<u32, DataEntry> data;

	mutable recursive_mutex lock_;
};

extern SymbolMap symbolMap;


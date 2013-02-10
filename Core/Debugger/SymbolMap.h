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

#include "../../Globals.h"
#include <vector>

enum SymbolType
{
	ST_FUNCTION=1,
	ST_DATA=2
};

#ifdef _WIN32
struct HWND__;
typedef struct HWND__ *HWND;
#endif

class SymbolMap
{
public:
	SymbolMap() {}
	bool LoadSymbolMap(const char *filename);
	void SaveSymbolMap(const char *filename);
	void AddSymbol(const char *symbolname, unsigned int vaddress, size_t size, SymbolType symbol);
	void ResetSymbolMap();
	void AnalyzeBackwards();
	int GetSymbolNum(unsigned int address, SymbolType symmask=ST_FUNCTION);
	char *GetDescription(unsigned int address);
#ifdef _WIN32
	void FillSymbolListBox(HWND listbox, SymbolType symmask=ST_FUNCTION);
	void FillSymbolComboBox(HWND listbox,SymbolType symmask=ST_FUNCTION);
	void FillListBoxBLinks(HWND listbox, int num);
#endif
	int GetNumSymbols();
	char *GetSymbolName(int i);
	void SetSymbolName(int i, const char *newname);
	u32 GetSymbolSize(int i);
	u32 GetSymbolAddr(int i);
	SymbolType GetSymbolType(int i);
	int FindSymbol(const char *name);
	u32	GetAddress(int num);
	void IncreaseRunCount(int num);
	unsigned int GetRunCount(int num);
	void SortSymbols();

	void UseFuncSignaturesFile(const char *filename, u32 maxAddress);
	void CompileFuncSignaturesFile(const char *filename);

private:
	struct MapEntry
	{
		u32 address;
		u32 vaddress;
		u32 size;
		u32 unknown;

		u32 runCount;

		SymbolType type;

#ifdef BWLINKS
		std::vector <u32> backwardLinks;
#endif

		char name[128];

		void UndecorateName()
		{
			// TODO
		}

		bool operator <(const MapEntry &other) const {
			return vaddress < other.vaddress;
		}
	};

	std::vector<MapEntry> entries;
};

extern SymbolMap symbolMap;

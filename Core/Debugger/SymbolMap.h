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
#include <set>
#include <map>

enum SymbolType
{
	ST_FUNCTION=1,
	ST_DATA=2
};

struct SymbolInfo
{
	u32 address;
	u32 size;
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
	void SaveSymbolMap(const char *filename) const;
	void AddSymbol(const char *symbolname, unsigned int vaddress, size_t size, SymbolType symbol);
	void ResetSymbolMap();
	void AnalyzeBackwards();
	int GetSymbolNum(unsigned int address, SymbolType symmask=ST_FUNCTION) const;
	bool GetSymbolInfo(SymbolInfo *info, u32 address, SymbolType symmask = ST_FUNCTION) const;
	const char *GetDescription(unsigned int address) const;
#ifdef _WIN32
	void FillSymbolListBox(HWND listbox, SymbolType symmask=ST_FUNCTION) const;
	void FillSymbolComboBox(HWND listbox,SymbolType symmask=ST_FUNCTION) const;
	void FillListBoxBLinks(HWND listbox, int num) const;
#endif
	int GetNumSymbols() const;
	const char *GetSymbolName(int i) const;
	void SetSymbolName(int i, const char *newname);
	u32 GetSymbolSize(int i) const;
	u32 GetSymbolAddr(int i) const;
	SymbolType GetSymbolType(int i) const;
	int FindSymbol(const char *name) const;
	u32	GetAddress(int num) const;
	void IncreaseRunCount(int num);
	unsigned int GetRunCount(int num) const;
	void SortSymbols();
	const char* getDirectSymbol(u32 address);
	bool getSymbolValue(char* symbol, u32& dest);

	void UseFuncSignaturesFile(const char *filename, u32 maxAddress);
	void CompileFuncSignaturesFile(const char *filename) const;

private:
	struct MapEntryUniqueInfo
	{
		u32 address;
		u32 vaddress;
		u32 size;
		SymbolType type;

		bool operator <(const MapEntryUniqueInfo &other) const {
			return vaddress < other.vaddress;
		}
	};

	struct MapEntry : public MapEntryUniqueInfo
	{
		char name[128];
		u32 unknown;
		u32 runCount;

#ifdef BWLINKS
		std::vector <u32> backwardLinks;
#endif

		void UndecorateName()
		{
			// TODO
		}
	};

	std::set<MapEntryUniqueInfo> uniqueEntries;
	std::vector<MapEntry> entries;
	std::map<u32, u32> entryRanges;
};

extern SymbolMap symbolMap;

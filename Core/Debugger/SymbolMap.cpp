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

#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <WindowsX.h>
#else
#include <unistd.h>
#endif

#include <algorithm>

#include "Common/FileUtil.h"
#include "Globals.h"
#include "Core/MemMap.h"
#include "Core/Debugger/SymbolMap.h"

SymbolMap symbolMap;

void SymbolMap::SortSymbols() {
	lock_guard guard(lock_);

	AssignFunctionIndices();
}

void SymbolMap::Clear() {
	lock_guard guard(lock_);
	functions.clear();
	labels.clear();
	data.clear();
}

bool SymbolMap::LoadSymbolMap(const char *filename) {
	lock_guard guard(lock_);
	Clear();
	FILE *f = File::OpenCFile(filename, "r");
	if (!f)
		return false;
	//char temp[256];
	//fgets(temp,255,f); //.text section layout
	//fgets(temp,255,f); //  Starting        Virtual
	//fgets(temp,255,f); //  address  Size   address
	//fgets(temp,255,f); //  -----------------------

	bool started=false;

	while (!feof(f)) {
		char line[512], temp[256] = {0};
		char *p = fgets(line, 512, f);
		if (p == NULL)
			break;

		// Chop any newlines off.
		for (size_t i = strlen(line) - 1; i > 0; i--) {
			if (line[i] == '\r' || line[i] == '\n') {
				line[i] = '\0';
			}
		}

		if (strlen(line) < 4 || sscanf(line, "%s", temp) != 1)
			continue;

		if (strcmp(temp,"UNUSED")==0) continue;
		if (strcmp(temp,".text")==0)  {started=true;continue;};
		if (strcmp(temp,".init")==0)  {started=true;continue;};
		if (strcmp(temp,"Starting")==0) continue;
		if (strcmp(temp,"extab")==0) continue;
		if (strcmp(temp,".ctors")==0) break;
		if (strcmp(temp,".dtors")==0) break;
		if (strcmp(temp,".rodata")==0) continue;
		if (strcmp(temp,".data")==0) continue;
		if (strcmp(temp,".sbss")==0) continue;
		if (strcmp(temp,".sdata")==0) continue;
		if (strcmp(temp,".sdata2")==0) continue;
		if (strcmp(temp,"address")==0)  continue;
		if (strcmp(temp,"-----------------------")==0)  continue;
		if (strcmp(temp,".sbss2")==0) break;
		if (temp[1]==']') continue;

		if (!started) continue;

		u32 address, size, vaddress;
		SymbolType type;
		char name[128] = {0};

		sscanf(line,"%08x %08x %08x %i %127c", &address, &size, &vaddress, (int*)&type, name);
		if (!Memory::IsValidAddress(vaddress)) {
			ERROR_LOG(LOADER, "Invalid address in symbol file: %08x (%s)", vaddress, name);
			continue;
		}
		if (type == ST_DATA && size == 0)
			size = 4;

		if (!strcmp(name, ".text") || !strcmp(name, ".init") || strlen(name) <= 1) {

		} else {
			switch (type)
			{
			case ST_FUNCTION:
				AddFunction(name, vaddress, size);
				break;
			case ST_DATA:
				AddData(vaddress,size,DATATYPE_BYTE);
				if (name[0] != 0)
					AddLabel(name, vaddress);
				break;
			case ST_NONE:
			case ST_ALL:
				// Shouldn't be possible.
				break;
			}
		}
	}
	fclose(f);
	SortSymbols();
	return true;
}


void SymbolMap::SaveSymbolMap(const char *filename) const
{
	lock_guard guard(lock_);
	FILE *f = File::OpenCFile(filename, "w");
	if (!f)
		return;
	fprintf(f,".text\n");
	for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
		const FunctionEntry& e = it->second;
		fprintf(f,"%08x %08x %08x %i %s\n",it->first,e.size,it->first,ST_FUNCTION,GetLabelName(it->first));
	}

	for (auto it = data.begin(), end = data.end(); it != end; ++it) {
		const DataEntry& e = it->second;
		fprintf(f,"%08x %08x %08x %i %s\n",it->first,e.size,it->first,ST_DATA,GetLabelName(it->first));
	}
	fclose(f);
}

bool SymbolMap::LoadNocashSym(const char *filename)
{
	lock_guard guard(lock_);
	FILE *f = File::OpenCFile(filename, "r");
	if (!f)
		return false;

	while (!feof(f)) {
		char line[256], value[256] = {0};
		char *p = fgets(line, 256, f);
		if (p == NULL)
			break;

		u32 address;
		if (sscanf(line, "%08X %s", &address, value) != 2)
			continue;
		if (address == 0 && strcmp(value, "0") == 0)
			continue;

		if (value[0] == '.') {
			// data directives
			char* s = strchr(value, ':');
			if (s != NULL) {
				*s = 0;

				u32 size = 0;
				if (sscanf(s + 1, "%04X", &size) != 1)
					continue;

				if (strcasecmp(value, ".byt") == 0) {
					AddData(address, size, DATATYPE_BYTE);
				} else if (strcasecmp(value, ".wrd") == 0) {
					AddData(address, size, DATATYPE_HALFWORD);
				} else if (strcasecmp(value, ".dbl") == 0) {
					AddData(address, size, DATATYPE_WORD);
				} else if (strcasecmp(value, ".asc") == 0) {
					AddData(address, size, DATATYPE_ASCII);
				}
			}
		} else {				// labels
			int size = 1;
			char* seperator = strchr(value,',');
			if (seperator != NULL) {
				*seperator = 0;
				sscanf(seperator+1,"%08X",&size);
			}

			if (size != 1) {
				AddFunction(value,address,size);
			} else {
				AddLabel(value,address);
			}
		}
	}

	fclose(f);
	return true;
}

SymbolType SymbolMap::GetSymbolType(u32 address) const {
	if (functions.find(address) != functions.end())
		return ST_FUNCTION;
	if (data.find(address) != data.end())
		return ST_DATA;
	return ST_NONE;
}

bool SymbolMap::GetSymbolInfo(SymbolInfo *info, u32 address, SymbolType symmask) const
{
	u32 functionAddress = INVALID_ADDRESS;
	u32 dataAddress = INVALID_ADDRESS;

	if (symmask & ST_FUNCTION)
		functionAddress = GetFunctionStart(address);

	if (symmask & ST_DATA)
		dataAddress = GetDataStart(address);

	if (functionAddress == INVALID_ADDRESS || dataAddress == INVALID_ADDRESS)
	{
		if (functionAddress != INVALID_ADDRESS)
		{
			if (info != NULL)
			{
				info->type = ST_FUNCTION;
				info->address = functionAddress;
				info->size = GetFunctionSize(functionAddress);
			}

			return true;
		}
		
		if (dataAddress != INVALID_ADDRESS)
		{
			if (info != NULL)
			{
				info->type = ST_DATA;
				info->address = dataAddress;
				info->size = GetDataSize(dataAddress);
			}

			return true;
		}

		return false;
	}

	// if both exist, return the function
	if (info != NULL) {
		info->type = ST_FUNCTION;
		info->address = functionAddress;
		info->size = GetFunctionSize(functionAddress);
	}

	return true;
}

u32 SymbolMap::GetNextSymbolAddress(u32 address, SymbolType symmask) {
	const auto functionEntry = symmask & ST_FUNCTION ? functions.upper_bound(address) : functions.end();
	const auto dataEntry = symmask & ST_DATA ? data.upper_bound(address) : data.end();

	if (functionEntry == functions.end() && dataEntry == data.end())
		return INVALID_ADDRESS;

	u32 funcAddress = (functionEntry != functions.end()) ? functionEntry->first : 0xFFFFFFFF;
	u32 dataAddress = (dataEntry != data.end()) ? dataEntry->first : 0xFFFFFFFF;

	if (funcAddress <= dataAddress)
		return funcAddress;
	else
		return dataAddress;
}

static char descriptionTemp[256];

const char *SymbolMap::GetDescription(unsigned int address) const {
	const char* labelName = NULL;

	u32 funcStart = GetFunctionStart(address);
	if (funcStart != INVALID_ADDRESS) {
		labelName = GetLabelName(funcStart);
	} else {
		u32 dataStart = GetDataStart(address);
		if (dataStart != INVALID_ADDRESS)
			labelName = GetLabelName(dataStart);
	}

	if (labelName != NULL)
		return labelName;

	sprintf(descriptionTemp, "(%08x)", address);
	return descriptionTemp;
}

std::vector<SymbolEntry> SymbolMap::GetAllSymbols(SymbolType symmask) {
	std::vector<SymbolEntry> result;

	if (symmask & ST_FUNCTION) {
		for (auto it = functions.begin(); it != functions.end(); it++) {
			SymbolEntry entry;
			entry.address = it->first;
			entry.size = GetFunctionSize(entry.address);
			const char* name = GetLabelName(entry.address);
			if (name != NULL)
				entry.name = name;
			result.push_back(entry);
		}
	}

	if (symmask & ST_DATA) {
		for (auto it = data.begin(); it != data.end(); it++) {
			SymbolEntry entry;
			entry.address = it->first;
			entry.size = GetDataSize(entry.address);
			const char* name = GetLabelName(entry.address);
			if (name != NULL)
				entry.name = name;
			result.push_back(entry);
		}
	}

	return result;
}

void SymbolMap::AddFunction(const char* name, u32 address, u32 size) {
	lock_guard guard(lock_);

	FunctionEntry func;
	func.size = size;
	func.index = (int)functions.size();
	functions[address] = func;

	if (GetLabelName(address) == NULL)
		AddLabel(name,address);
}

u32 SymbolMap::GetFunctionStart(u32 address) const {
	auto it = functions.upper_bound(address);
	if (it == functions.end()) {
		// check last element
		auto rit = functions.rbegin();
		if (rit != functions.rend()) {
			u32 start = rit->first;
			u32 size = rit->second.size;
			if (start <= address && start+size > address)
				return start;
		}
		// otherwise there's no function that contains this address
		return INVALID_ADDRESS;
	}

	if (it != functions.begin()) {
		it--;
		u32 start = it->first;
		u32 size = it->second.size;
		if (start <= address && start+size > address)
			return start;
	}

	return INVALID_ADDRESS;
}

u32 SymbolMap::GetFunctionSize(u32 startAddress) const {
	auto it = functions.find(startAddress);
	if (it == functions.end())
		return INVALID_ADDRESS;

	return it->second.size;
}

int SymbolMap::GetFunctionNum(u32 address) const {
	u32 start = GetFunctionStart(address);
	if (start == INVALID_ADDRESS)
		return INVALID_ADDRESS;

	auto it = functions.find(start);
	if (it == functions.end())
		return INVALID_ADDRESS;

	return it->second.index;
}

void SymbolMap::AssignFunctionIndices() {
	int index = 0;
	for (auto it = functions.begin(); it != functions.end(); it++) {
		it->second.index = index++;
	}
}

bool SymbolMap::SetFunctionSize(u32 startAddress, u32 newSize) {
	lock_guard guard(lock_);

	auto it = functions.find(startAddress);
	if (it == functions.end())
		return false;

	it->second.size = newSize;

	// TODO: check for overlaps
	return true;
}

bool SymbolMap::RemoveFunction(u32 startAddress, bool removeName) {
	lock_guard guard(lock_);

	auto it = functions.find(startAddress);
	if (it == functions.end())
		return false;

	functions.erase(it);
	if (removeName) {
		auto labelIt = labels.find(startAddress);
		if (labelIt != labels.end())
			labels.erase(labelIt);
	}

	return true;
}

void SymbolMap::AddLabel(const char* name, u32 address) {
	// keep a label if it already exists
	auto it = labels.find(address);
	if (it == labels.end()) {
		LabelEntry label;
		strncpy(label.name, name, 128);
		label.name[127] = 0;
		labels[address] = label;
	}
}

void SymbolMap::SetLabelName(const char* name, u32 address) {
	auto it = labels.find(address);
	if (it == labels.end()) {
		LabelEntry label;
		strcpy(label.name,name);
		label.name[127] = 0;

		labels[address] = label;
	} else {
		strcpy(it->second.name,name);
		it->second.name[127] = 0;
	}
}

const char* SymbolMap::GetLabelName(u32 address) const {
	auto it = labels.find(address);
	if (it == labels.end())
		return NULL;

	return it->second.name;
}

bool SymbolMap::GetLabelValue(const char* name, u32& dest) {
	for (auto it = labels.begin(); it != labels.end(); it++) {
		if (strcasecmp(name,it->second.name) == 0) {
			dest = it->first;
			return true;
		}
	}

	return false;
}

void SymbolMap::AddData(u32 address, u32 size, DataType type) {
	DataEntry entry;
	entry.size = size;
	entry.type = type;
	data[address] = entry;
}

u32 SymbolMap::GetDataStart(u32 address) const {
	auto it = data.upper_bound(address);
	if (it == data.end())
	{
		// check last element
		auto rit = data.rbegin();

		if (rit != data.rend())
		{
			u32 start = rit->first;
			u32 size = rit->second.size;
			if (start <= address && start+size > address)
				return start;
		}
		// otherwise there's no data that contains this address
		return INVALID_ADDRESS;
	}

	if (it != data.begin()) {
		it--;
		u32 start = it->first;
		u32 size = it->second.size;
		if (start <= address && start+size > address)
			return start;
	}

	return INVALID_ADDRESS;
}

u32 SymbolMap::GetDataSize(u32 startAddress) const {
	auto it = data.find(startAddress);
	if (it == data.end())
		return INVALID_ADDRESS;
	return it->second.size;
}

DataType SymbolMap::GetDataType(u32 startAddress) const {
	auto it = data.find(startAddress);
	if (it == data.end())
		return DATATYPE_NONE;
	return it->second.type;
}

#if defined(_WIN32) && !defined(_XBOX)

struct DefaultSymbol {
	u32 address;
	const char* name;
};

static const DefaultSymbol defaultSymbols[]= {
	{ 0x08800000,	"User memory" },
	{ 0x08804000,	"Default load address" },
	{ 0x04000000,	"VRAM" },
	{ 0x88000000,	"Kernel memory" },
	{ 0x00010000,	"Scratchpad" },
};

void SymbolMap::FillSymbolListBox(HWND listbox,SymbolType symType) const {
	wchar_t temp[256];
	lock_guard guard(lock_);

	SendMessage(listbox, WM_SETREDRAW, FALSE, 0);
	ListBox_ResetContent(listbox);

	switch (symType) {
	case ST_FUNCTION:
		{
			SendMessage(listbox, LB_INITSTORAGE, (WPARAM)functions.size(), (LPARAM)functions.size() * 30);

			for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
				const FunctionEntry& entry = it->second;
				const char* name = GetLabelName(it->first);
				if (name != NULL)
					wsprintf(temp, L"%S", name);
				else
					wsprintf(temp, L"0x%08X", it->first);
				int index = ListBox_AddString(listbox,temp);
				ListBox_SetItemData(listbox,index,it->first);
			}
		}
		break;

	case ST_DATA:
		{
			int count = ARRAYSIZE(defaultSymbols)+(int)data.size();
			SendMessage(listbox, LB_INITSTORAGE, (WPARAM)count, (LPARAM)count * 30);

			for (int i = 0; i < ARRAYSIZE(defaultSymbols); i++) {
				wsprintf(temp, L"0x%08X (%S)", defaultSymbols[i].address, defaultSymbols[i].name);
				int index = ListBox_AddString(listbox,temp);
				ListBox_SetItemData(listbox,index,defaultSymbols[i].address);
			}

			for (auto it = data.begin(), end = data.end(); it != end; ++it) {
				const DataEntry& entry = it->second;
				const char* name = GetLabelName(it->first);

				if (name != NULL)
					wsprintf(temp, L"%S", name);
				else
					wsprintf(temp, L"0x%08X", it->first);

				int index = ListBox_AddString(listbox,temp);
				ListBox_SetItemData(listbox,index,it->first);
			}
		}
		break;
	}

	SendMessage(listbox, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(listbox, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

#endif
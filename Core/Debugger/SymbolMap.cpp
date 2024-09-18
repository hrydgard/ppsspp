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

// These functions tends to be slow in debug mode.
// Comment this out if debugging the symbol map itself.
#if defined(_MSC_VER) && defined(_DEBUG)
#pragma optimize("gty", on)
#endif

#include "ppsspp_config.h"
#ifdef _WIN32
#include "Common/CommonWindows.h"
#include <WindowsX.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <memory>
#ifndef NO_ARMIPS
#include <string_view>
#endif
#include <unordered_map>

#include "zlib.h"

#include "Common/CommonTypes.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/MemMap.h"
#include "Core/Debugger/SymbolMap.h"

#ifndef NO_ARMIPS
#include "ext/armips/Core/Assembler.h"
#else
struct Identifier {
	explicit Identifier() {}
	explicit Identifier(const std::string &s) {}
};

struct LabelDefinition {
	Identifier name;
	int64_t value;
};
#endif

SymbolMap *g_symbolMap;

void SymbolMap::SortSymbols() {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	AssignFunctionIndices();
}

void SymbolMap::Clear() {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	functions.clear();
	labels.clear();
	data.clear();
	activeFunctions.clear();
	activeLabels.clear();
	activeData.clear();
	activeModuleEnds.clear();
	modules.clear();
	activeNeedUpdate_ = false;
}

bool SymbolMap::LoadSymbolMap(const Path &filename) {
	Clear();  // let's not recurse the lock

	std::lock_guard<std::recursive_mutex> guard(lock_);

	// TODO(scoped): Use gzdopen instead.

#if defined(_WIN32) && defined(UNICODE)
	gzFile f = gzopen_w(filename.ToWString().c_str(), "r");
#else
	gzFile f = gzopen(filename.c_str(), "r");
#endif

	if (f == Z_NULL)
		return false;

	//char temp[256];
	//fgets(temp,255,f); //.text section layout
	//fgets(temp,255,f); //  Starting        Virtual
	//fgets(temp,255,f); //  address  Size   address
	//fgets(temp,255,f); //  -----------------------

	bool started = false;
	bool hasModules = false;

	while (!gzeof(f)) {
		char line[512], temp[256] = {0};
		char *p = gzgets(f, line, 512);
		if (p == NULL)
			break;

		// Chop any newlines off.
		for (size_t i = strlen(line) - 1; i > 0; i--) {
			if (line[i] == '\r' || line[i] == '\n') {
				line[i] = '\0';
			}
		}

		if (strlen(line) < 4 || sscanf(line, "%255s", temp) != 1)
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

		u32 address = -1, size = 0, vaddress = -1;
		int moduleIndex = 0;
		int typeInt = ST_NONE;
		SymbolType type;
		char name[128] = {0};

		if (sscanf(line, ".module %x %08x %08x %127c", (unsigned int *)&moduleIndex, &address, &size, name) >= 3) {
			// Found a module definition.
			ModuleEntry mod;
			mod.index = moduleIndex;
			strcpy(mod.name, name);
			mod.start = address;
			mod.size = size;
			modules.push_back(mod);
			hasModules = true;
			continue;
		}

		int matched = sscanf(line, "%08x %08x %x %i %127c", &address, &size, &vaddress, &typeInt, name);
		if (matched < 1)
			continue;
		type = (SymbolType) typeInt;
		if (!hasModules) {
			if (!Memory::IsValidAddress(vaddress)) {
				ERROR_LOG(Log::Loader, "Invalid address in symbol file: %08x (%s)", vaddress, name);
				continue;
			}
		} else {
			// The 3rd field is now used for the module index.
			moduleIndex = vaddress;
			vaddress = GetModuleAbsoluteAddr(address, moduleIndex);
			if (!Memory::IsValidAddress(vaddress)) {
				ERROR_LOG(Log::Loader, "Invalid address in symbol file: %08x (%s)", vaddress, name);
				continue;
			}
		}

		if (type == ST_DATA && size == 0)
			size = 4;

		// Ignore syscalls, will be recognized from stubs.
		// Note: it's still useful to save these for grepping and importing into other tools.
		if (strncmp(name, "zz_sce", 6) == 0)
			continue;
		// Also ignore unresolved imports, which will similarly be replaced.
		if (strncmp(name, "zz_[UNK", 7) == 0)
			continue;

		if (!strcmp(name, ".text") || !strcmp(name, ".init") || strlen(name) <= 1) {

		} else {
			switch (type)
			{
			case ST_FUNCTION:
				AddFunction(name, vaddress, size, moduleIndex);
				break;
			case ST_DATA:
				AddData(vaddress,size,DATATYPE_BYTE, moduleIndex);
				if (name[0] != 0)
					AddLabel(name, vaddress, moduleIndex);
				break;
			case ST_NONE:
			case ST_ALL:
				// Shouldn't be possible.
				break;
			}
		}
	}
	gzclose(f);
	SortSymbols();
	return started;
}

bool SymbolMap::SaveSymbolMap(const Path &filename) const {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	// Don't bother writing a blank file.
	if (!File::Exists(filename) && functions.empty() && data.empty()) {
		return true;
	}

	// TODO(scoped): Use gzdopen
#if defined(_WIN32) && defined(UNICODE)
	gzFile f = gzopen_w(filename.ToWString().c_str(), "w9");
#else
	gzFile f = gzopen(filename.c_str(), "w9");
#endif

	if (f == Z_NULL)
		return false;

	gzprintf(f, ".text\n");

	for (auto it = modules.begin(), end = modules.end(); it != end; ++it) {
		const ModuleEntry &mod = *it;
		gzprintf(f, ".module %x %08x %08x %s\n", mod.index, mod.start, mod.size, mod.name);
	}

	for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
		const FunctionEntry& e = it->second;
		gzprintf(f, "%08x %08x %x %i %s\n", e.start, e.size, e.module, ST_FUNCTION, GetLabelNameRel(e.start, e.module));
	}

	for (auto it = data.begin(), end = data.end(); it != end; ++it) {
		const DataEntry& e = it->second;
		gzprintf(f, "%08x %08x %x %i %s\n", e.start, e.size, e.module, ST_DATA, GetLabelNameRel(e.start, e.module));
	}
	gzclose(f);
	return true;
}

bool SymbolMap::LoadNocashSym(const Path &filename) {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	FILE *f = File::OpenCFile(filename, "r");
	if (!f)
		return false;

	while (!feof(f)) {
		char line[256], value[256] = {0};
		char *p = fgets(line, 256, f);
		if (p == NULL)
			break;

		u32 address;
		if (sscanf(line, "%08X %255s", &address, value) != 2)
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
					AddData(address, size, DATATYPE_BYTE, 0);
				} else if (strcasecmp(value, ".wrd") == 0) {
					AddData(address, size, DATATYPE_HALFWORD, 0);
				} else if (strcasecmp(value, ".dbl") == 0) {
					AddData(address, size, DATATYPE_WORD, 0);
				} else if (strcasecmp(value, ".asc") == 0) {
					AddData(address, size, DATATYPE_ASCII, 0);
				}
			}
		} else {				// labels
			unsigned int size = 1;
			char* seperator = strchr(value, ',');
			if (seperator != NULL) {
				*seperator = 0;
				sscanf(seperator+1,"%08X",&size);
			}

			if (size != 1) {
				AddFunction(value, address,size, 0);
			} else {
				AddLabel(value, address, 0);
			}
		}
	}

	fclose(f);
	return true;
}

void SymbolMap::SaveNocashSym(const Path &filename) const {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	// Don't bother writing a blank file.
	if (!File::Exists(filename) && functions.empty() && data.empty()) {
		return;
	}

	FILE* f = File::OpenCFile(filename, "w");
	if (f == NULL)
		return;

	// only write functions, the rest isn't really interesting
	for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
		const FunctionEntry& e = it->second;
		fprintf(f, "%08X %s,%04X\n", GetModuleAbsoluteAddr(e.start,e.module),GetLabelNameRel(e.start, e.module), e.size);
	}
	
	fclose(f);
}

SymbolType SymbolMap::GetSymbolType(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	if (activeFunctions.find(address) != activeFunctions.end())
		return ST_FUNCTION;
	if (activeData.find(address) != activeData.end())
		return ST_DATA;
	return ST_NONE;
}

bool SymbolMap::GetSymbolInfo(SymbolInfo *info, u32 address, SymbolType symmask) {
	u32 functionAddress = INVALID_ADDRESS;
	u32 dataAddress = INVALID_ADDRESS;

	if (symmask & ST_FUNCTION) {
		functionAddress = GetFunctionStart(address);

		// If both are found, we always return the function, so just do that early.
		if (functionAddress != INVALID_ADDRESS) {
			if (info != NULL) {
				info->type = ST_FUNCTION;
				info->address = functionAddress;
				info->size = GetFunctionSize(functionAddress);
				info->moduleAddress = GetFunctionModuleAddress(functionAddress);
			}

			return true;
		}
	}

	if (symmask & ST_DATA) {
		dataAddress = GetDataStart(address);
		
		if (dataAddress != INVALID_ADDRESS) {
			if (info != NULL) {
				info->type = ST_DATA;
				info->address = dataAddress;
				info->size = GetDataSize(dataAddress);
				info->moduleAddress = GetDataModuleAddress(dataAddress);
			}

			return true;
		}
	}

	return false;
}

u32 SymbolMap::GetNextSymbolAddress(u32 address, SymbolType symmask) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	const auto functionEntry = symmask & ST_FUNCTION ? activeFunctions.upper_bound(address) : activeFunctions.end();
	const auto dataEntry = symmask & ST_DATA ? activeData.upper_bound(address) : activeData.end();

	if (functionEntry == activeFunctions.end() && dataEntry == activeData.end())
		return INVALID_ADDRESS;

	u32 funcAddress = (functionEntry != activeFunctions.end()) ? functionEntry->first : 0xFFFFFFFF;
	u32 dataAddress = (dataEntry != activeData.end()) ? dataEntry->first : 0xFFFFFFFF;

	if (funcAddress <= dataAddress)
		return funcAddress;
	else
		return dataAddress;
}

std::string SymbolMap::GetDescription(unsigned int address) {
	std::lock_guard<std::recursive_mutex> guard(lock_);
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

	char descriptionTemp[256];
	sprintf(descriptionTemp, "(%08x)", address);
	return descriptionTemp;
}

std::vector<SymbolEntry> SymbolMap::GetAllSymbols(SymbolType symmask) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::vector<SymbolEntry> result;

	if (symmask & ST_FUNCTION) {
		std::lock_guard<std::recursive_mutex> guard(lock_);
		for (auto it = activeFunctions.begin(); it != activeFunctions.end(); it++) {
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
		std::lock_guard<std::recursive_mutex> guard(lock_);
		for (auto it = activeData.begin(); it != activeData.end(); it++) {
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

void SymbolMap::AddModule(const char *name, u32 address, u32 size) {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	for (auto it = modules.begin(), end = modules.end(); it != end; ++it) {
		if (!strcmp(it->name, name)) {
			// Just reactivate that one.
			it->start = address;
			it->size = size;
			activeModuleEnds.emplace(it->start + it->size, *it);
			activeNeedUpdate_ = true;
			return;
		}
	}

	ModuleEntry mod;
	truncate_cpy(mod.name, name);
	mod.start = address;
	mod.size = size;
	mod.index = (int)modules.size() + 1;

	modules.push_back(mod);
	activeModuleEnds.emplace(mod.start + mod.size, mod);
	activeNeedUpdate_ = true;
}

void SymbolMap::UnloadModule(u32 address, u32 size) {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	activeModuleEnds.erase(address + size);
	activeNeedUpdate_ = true;
}

u32 SymbolMap::GetModuleRelativeAddr(u32 address, int moduleIndex) const {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	if (moduleIndex == -1) {
		moduleIndex = GetModuleIndex(address);
	}

	for (auto it = modules.begin(), end = modules.end(); it != end; ++it) {
		if (it->index == moduleIndex) {
			return address - it->start;
		}
	}
	return address;
}

u32 SymbolMap::GetModuleAbsoluteAddr(u32 relative, int moduleIndex) const {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	for (auto it = modules.begin(), end = modules.end(); it != end; ++it) {
		if (it->index == moduleIndex) {
			return it->start + relative;
		}
	}
	return relative;
}

int SymbolMap::GetModuleIndex(u32 address) const {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto iter = activeModuleEnds.upper_bound(address);
	if (iter == activeModuleEnds.end())
		return -1;
	return iter->second.index;
}

bool SymbolMap::IsModuleActive(int moduleIndex) {
	if (moduleIndex == 0) {
		return true;
	}

	std::lock_guard<std::recursive_mutex> guard(lock_);
	for (auto it = activeModuleEnds.begin(), end = activeModuleEnds.end(); it != end; ++it) {
		if (it->second.index == moduleIndex) {
			return true;
		}
	}
	return false;
}

std::vector<LoadedModuleInfo> SymbolMap::getAllModules() const {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	std::vector<LoadedModuleInfo> result;
	for (size_t i = 0; i < modules.size(); i++) {
		LoadedModuleInfo m;
		m.name = modules[i].name;
		m.address = modules[i].start;
		m.size = modules[i].size;

		u32 key = modules[i].start + modules[i].size;
		m.active = activeModuleEnds.find(key) != activeModuleEnds.end();

		result.push_back(m);
	}

	return result;
}

void SymbolMap::AddFunction(const char* name, u32 address, u32 size, int moduleIndex) {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	if (moduleIndex == -1) {
		moduleIndex = GetModuleIndex(address);
	} else if (moduleIndex == 0) {
		sawUnknownModule = true;
	}

	// Is there an existing one?
	u32 relAddress = GetModuleRelativeAddr(address, moduleIndex);
	auto symbolKey = std::make_pair(moduleIndex, relAddress);
	auto existing = functions.find(symbolKey);
	if (sawUnknownModule && existing == functions.end()) {
		// Fall back: maybe it's got moduleIndex = 0.
		existing = functions.find(std::make_pair(0, address));
	}

	if (existing != functions.end()) {
		existing->second.size = size;
		if (existing->second.module != moduleIndex) {
			FunctionEntry func = existing->second;
			func.start = relAddress;
			func.module = moduleIndex;
			functions.erase(existing);
			functions[symbolKey] = func;
		}

		// Refresh the active item if it exists.
		auto active = activeFunctions.find(address);
		if (active != activeFunctions.end() && active->second.module == moduleIndex) {
			activeFunctions.erase(active);
			activeFunctions.emplace(address, existing->second);
		}
	} else {
		FunctionEntry func;
		func.start = relAddress;
		func.size = size;
		func.index = (int)functions.size();
		func.module = moduleIndex;
		functions[symbolKey] = func;

		if (IsModuleActive(moduleIndex)) {
			activeFunctions.emplace(address, func);
		}
	}

	AddLabel(name, address, moduleIndex);
}

u32 SymbolMap::GetFunctionStart(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeFunctions.upper_bound(address);
	if (it == activeFunctions.end()) {
		// check last element
		auto rit = activeFunctions.rbegin();
		if (rit != activeFunctions.rend()) {
			u32 start = rit->first;
			u32 size = rit->second.size;
			if (start <= address && start+size > address)
				return start;
		}
		// otherwise there's no function that contains this address
		return INVALID_ADDRESS;
	}

	if (it != activeFunctions.begin()) {
		it--;
		u32 start = it->first;
		u32 size = it->second.size;
		if (start <= address && start+size > address)
			return start;
	}

	return INVALID_ADDRESS;
}

u32 SymbolMap::FindPossibleFunctionAtAfter(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeFunctions.lower_bound(address);
	if (it == activeFunctions.end()) {
		return (u32)-1;
	}
	return it->first;
}

u32 SymbolMap::GetFunctionSize(u32 startAddress) {
	if (activeNeedUpdate_) {
		std::lock_guard<std::recursive_mutex> guard(lock_);

		// This is common, from the jit.  Direct lookup is faster than updating active symbols.
		auto mod = activeModuleEnds.lower_bound(startAddress);
		std::pair<int, u32> funcKey;
		if (mod == activeModuleEnds.end()) {
			// Could still be mod 0, backwards compatibility.
			if (!sawUnknownModule)
				return INVALID_ADDRESS;
			funcKey.first = 0;
			funcKey.second = startAddress;
		} else {
			if (mod->second.start > startAddress)
				return INVALID_ADDRESS;
			funcKey.first = mod->second.index;
			funcKey.second = startAddress - mod->second.start;
		}

		auto func = functions.find(funcKey);
		if (func == functions.end())
			return INVALID_ADDRESS;

		return func->second.size;
	}

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeFunctions.find(startAddress);
	if (it == activeFunctions.end())
		return INVALID_ADDRESS;

	return it->second.size;
}

u32 SymbolMap::GetFunctionModuleAddress(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeFunctions.find(startAddress);
	if (it == activeFunctions.end())
		return INVALID_ADDRESS;

	return GetModuleAbsoluteAddr(0, it->second.module);
}

int SymbolMap::GetFunctionNum(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	u32 start = GetFunctionStart(address);
	if (start == INVALID_ADDRESS)
		return INVALID_ADDRESS;

	auto it = activeFunctions.find(start);
	if (it == activeFunctions.end())
		return INVALID_ADDRESS;

	return it->second.index;
}

void SymbolMap::AssignFunctionIndices() {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	int index = 0;
	for (auto mod = activeModuleEnds.begin(), modend = activeModuleEnds.end(); mod != modend; ++mod) {
		int moduleIndex = mod->second.index;
		auto begin = functions.lower_bound(std::make_pair(moduleIndex, 0));
		auto end = functions.upper_bound(std::make_pair(moduleIndex, 0xFFFFFFFF));
		for (auto it = begin; it != end; ++it) {
			it->second.index = index++;
		}
	}
}

void SymbolMap::UpdateActiveSymbols() {
	// return;   (slow in debug mode)
	std::lock_guard<std::recursive_mutex> guard(lock_);

	activeFunctions.clear();
	activeLabels.clear();
	activeData.clear();

	// On startup and shutdown, we can skip the rest.  Tiny optimization.
	if (activeModuleEnds.empty() || (functions.empty() && labels.empty() && data.empty())) {
		return;
	}

	std::unordered_map<int, u32> activeModuleIndexes;
	for (auto it = activeModuleEnds.begin(), end = activeModuleEnds.end(); it != end; ++it) {
		activeModuleIndexes[it->second.index] = it->second.start;
	}

	for (auto it = functions.begin(), end = functions.end(); it != end; ++it) {
		const auto mod = activeModuleIndexes.find(it->second.module);
		if (it->second.module == 0) {
			activeFunctions.emplace(it->second.start, it->second);
		} else if (mod != activeModuleIndexes.end()) {
			activeFunctions.emplace(mod->second + it->second.start, it->second);
		}
	}

	for (auto it = labels.begin(), end = labels.end(); it != end; ++it) {
		const auto mod = activeModuleIndexes.find(it->second.module);
		if (it->second.module == 0) {
			activeLabels.emplace(it->second.addr, it->second);
		} else if (mod != activeModuleIndexes.end()) {
			activeLabels.emplace(mod->second + it->second.addr, it->second);
		}
	}

	for (auto it = data.begin(), end = data.end(); it != end; ++it) {
		const auto mod = activeModuleIndexes.find(it->second.module);
		if (it->second.module == 0) {
			activeData.emplace(it->second.start, it->second);
		} else if (mod != activeModuleIndexes.end()) {
			activeData.emplace(mod->second + it->second.start, it->second);
		}
	}

	AssignFunctionIndices();
	activeNeedUpdate_ = false;
}

bool SymbolMap::SetFunctionSize(u32 startAddress, u32 newSize) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);

	auto funcInfo = activeFunctions.find(startAddress);
	if (funcInfo != activeFunctions.end()) {
		auto symbolKey = std::make_pair(funcInfo->second.module, funcInfo->second.start);
		auto func = functions.find(symbolKey);
		if (func != functions.end()) {
			func->second.size = newSize;
			activeFunctions.erase(funcInfo);
			activeFunctions.emplace(startAddress, func->second);
		}
	}

	// TODO: check for overlaps
	return true;
}

bool SymbolMap::RemoveFunction(u32 startAddress, bool removeName) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);

	auto it = activeFunctions.find(startAddress);
	if (it == activeFunctions.end())
		return false;

	auto symbolKey = std::make_pair(it->second.module, it->second.start);
	auto it2 = functions.find(symbolKey);
	if (it2 != functions.end()) {
		functions.erase(it2);
	}
	activeFunctions.erase(it);

	if (removeName) {
		auto labelIt = activeLabels.find(startAddress);
		if (labelIt != activeLabels.end()) {
			symbolKey = std::make_pair(labelIt->second.module, labelIt->second.addr);
			auto labelIt2 = labels.find(symbolKey);
			if (labelIt2 != labels.end()) {
				labels.erase(labelIt2);
			}
			activeLabels.erase(labelIt);
		}
	}

	return true;
}

void SymbolMap::AddLabel(const char* name, u32 address, int moduleIndex) {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	if (moduleIndex == -1) {
		moduleIndex = GetModuleIndex(address);
	} else if (moduleIndex == 0) {
		sawUnknownModule = true;
	}

	// Is there an existing one?
	u32 relAddress = GetModuleRelativeAddr(address, moduleIndex);
	auto symbolKey = std::make_pair(moduleIndex, relAddress);
	auto existing = labels.find(symbolKey);
	if (sawUnknownModule && existing == labels.end()) {
		// Fall back: maybe it's got moduleIndex = 0.
		existing = labels.find(std::make_pair(0, address));
	}

	if (existing != labels.end()) {
		// We leave an existing label alone, rather than overwriting.
		// But we'll still upgrade it to the correct module / relative address.
		if (existing->second.module != moduleIndex) {
			LabelEntry label = existing->second;
			label.addr = relAddress;
			label.module = moduleIndex;
			labels.erase(existing);
			labels[symbolKey] = label;

			// Refresh the active item if it exists.
			auto active = activeLabels.find(address);
			if (active != activeLabels.end() && active->second.module == moduleIndex) {
				activeLabels.erase(active);
				activeLabels.emplace(address, label);
			}
		}
	} else {
		LabelEntry label;
		label.addr = relAddress;
		label.module = moduleIndex;
		truncate_cpy(label.name, name);

		labels[symbolKey] = label;
		if (IsModuleActive(moduleIndex)) {
			activeLabels.emplace(address, label);
		}
	}
}

void SymbolMap::SetLabelName(const char* name, u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto labelInfo = activeLabels.find(address);
	if (labelInfo == activeLabels.end()) {
		AddLabel(name, address);
	} else {
		auto symbolKey = std::make_pair(labelInfo->second.module, labelInfo->second.addr);
		auto label = labels.find(symbolKey);
		if (label != labels.end()) {
			truncate_cpy(label->second.name, name);
			label->second.name[127] = 0;

			// Refresh the active item if it exists.
			auto active = activeLabels.find(address);
			if (active != activeLabels.end() && active->second.module == label->second.module) {
				activeLabels.erase(active);
				activeLabels.emplace(address, label->second);
			}
		}
	}
}

const char *SymbolMap::GetLabelName(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeLabels.find(address);
	if (it == activeLabels.end())
		return NULL;

	return it->second.name;
}

const char *SymbolMap::GetLabelNameRel(u32 relAddress, int moduleIndex) const {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = labels.find(std::make_pair(moduleIndex, relAddress));
	if (it == labels.end())
		return NULL;

	return it->second.name;
}

std::string SymbolMap::GetLabelString(u32 address) {
	std::lock_guard<std::recursive_mutex> guard(lock_);
	const char *label = GetLabelName(address);
	if (label == NULL)
		return "";
	return label;
}

bool SymbolMap::GetLabelValue(const char* name, u32& dest) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	for (auto it = activeLabels.begin(); it != activeLabels.end(); it++) {
		if (strcasecmp(name, it->second.name) == 0) {
			dest = it->first;
			return true;
		}
	}

	return false;
}

void SymbolMap::AddData(u32 address, u32 size, DataType type, int moduleIndex) {
	std::lock_guard<std::recursive_mutex> guard(lock_);

	if (moduleIndex == -1) {
		moduleIndex = GetModuleIndex(address);
	} else if (moduleIndex == 0) {
		sawUnknownModule = true;
	}

	// Is there an existing one?
	u32 relAddress = GetModuleRelativeAddr(address, moduleIndex);
	auto symbolKey = std::make_pair(moduleIndex, relAddress);
	auto existing = data.find(symbolKey);
	if (sawUnknownModule && existing == data.end()) {
		// Fall back: maybe it's got moduleIndex = 0.
		existing = data.find(std::make_pair(0, address));
	}

	if (existing != data.end()) {
		existing->second.size = size;
		existing->second.type = type;
		if (existing->second.module != moduleIndex) {
			DataEntry entry = existing->second;
			entry.module = moduleIndex;
			entry.start = relAddress;
			data.erase(existing);
			data[symbolKey] = entry;
		}

		// Refresh the active item if it exists.
		auto active = activeData.find(address);
		if (active != activeData.end() && active->second.module == moduleIndex) {
			activeData.erase(active);
			activeData.emplace(address, existing->second);
		}
	} else {
		DataEntry entry;
		entry.start = relAddress;
		entry.size = size;
		entry.type = type;
		entry.module = moduleIndex;

		data[symbolKey] = entry;
		if (IsModuleActive(moduleIndex)) {
			activeData.emplace(address, entry);
		}
	}
}

u32 SymbolMap::GetDataStart(u32 address) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeData.upper_bound(address);
	if (it == activeData.end())
	{
		// check last element
		auto rit = activeData.rbegin();

		if (rit != activeData.rend())
		{
			u32 start = rit->first;
			u32 size = rit->second.size;
			if (start <= address && start+size > address)
				return start;
		}
		// otherwise there's no data that contains this address
		return INVALID_ADDRESS;
	}

	if (it != activeData.begin()) {
		it--;
		u32 start = it->first;
		u32 size = it->second.size;
		if (start <= address && start+size > address)
			return start;
	}

	return INVALID_ADDRESS;
}

u32 SymbolMap::GetDataSize(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeData.find(startAddress);
	if (it == activeData.end())
		return INVALID_ADDRESS;
	return it->second.size;
}

u32 SymbolMap::GetDataModuleAddress(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeData.find(startAddress);
	if (it == activeData.end())
		return INVALID_ADDRESS;
	return GetModuleAbsoluteAddr(0, it->second.module);
}

DataType SymbolMap::GetDataType(u32 startAddress) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	auto it = activeData.find(startAddress);
	if (it == activeData.end())
		return DATATYPE_NONE;
	return it->second.type;
}

void SymbolMap::GetLabels(std::vector<LabelDefinition> &dest) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	std::lock_guard<std::recursive_mutex> guard(lock_);
	for (auto it = activeLabels.begin(); it != activeLabels.end(); it++) {
		LabelDefinition entry;
		entry.value = it->first;
		std::string name = it->second.name;
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		entry.name = Identifier(name);
		dest.push_back(entry);
	}
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)

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

void SymbolMap::FillSymbolListBox(HWND listbox,SymbolType symType) {
	if (activeNeedUpdate_)
		UpdateActiveSymbols();

	wchar_t temp[256];
	std::lock_guard<std::recursive_mutex> guard(lock_);

	SendMessage(listbox, WM_SETREDRAW, FALSE, 0);
	ListBox_ResetContent(listbox);

	switch (symType) {
	case ST_FUNCTION:
		{
			SendMessage(listbox, LB_INITSTORAGE, (WPARAM)activeFunctions.size(), (LPARAM)activeFunctions.size() * 30);

			for (auto it = activeFunctions.begin(), end = activeFunctions.end(); it != end; ++it) {
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
			size_t count = ARRAYSIZE(defaultSymbols)+activeData.size();
			SendMessage(listbox, LB_INITSTORAGE, (WPARAM)count, (LPARAM)count * 30);

			for (int i = 0; i < ARRAYSIZE(defaultSymbols); i++) {
				wsprintf(temp, L"0x%08X (%S)", defaultSymbols[i].address, defaultSymbols[i].name);
				int index = ListBox_AddString(listbox,temp);
				ListBox_SetItemData(listbox,index,defaultSymbols[i].address);
			}

			for (auto it = activeData.begin(), end = activeData.end(); it != end; ++it) {
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

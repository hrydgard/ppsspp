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

#include <vector>
#include <map>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"

// Next plans for the symbol map
//
// Symbol maps should auto-load and auto-save. They should be per module with section offsets, and the ID
// should be the module name plus the hash of the code and data sections. This way
// symbols will not misapply to the wrong modules.
// We already have some support for module-specific symbols, but it's just not set up right, as far as I can tell.

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
	u32 moduleAddress;
};

struct SymbolEntry {
	std::string name;
	u32 address;
	u32 size;
};

struct LoadedModuleInfo {
	std::string name;
	u32 address;
	u32 size;
	bool active;
};

enum DataType {
	DATATYPE_NONE, DATATYPE_BYTE, DATATYPE_HALFWORD, DATATYPE_WORD, DATATYPE_ASCII
};

struct LabelDefinition;

#ifdef _WIN32
struct HWND__;
typedef struct HWND__ *HWND;
#endif

// SymbolMap keeps two parallel sets of tables for functions/labels/data:
//  - The "master" tables (functions/labels/data below) are permanent: keyed by
//    (moduleIndex, addressRelativeToModuleStart), they retain every symbol ever seen for
//    every module ever loaded this session, even after a module unloads. This is what makes
//    it safe for two unrelated modules to occupy the same address range at different points
//    in a session (common - modules load/unload constantly) without losing or corrupting
//    each other's symbols.
//  - The "active" tables (activeFunctions/activeLabels/activeData) are a derived, read-only
//    cache: just the master entries belonging to currently-loaded modules, flattened to plain
//    absolute addresses. Nearly every query (GetFunctionStart, GetSymbolType, ...) reads only
//    from these, so lookups are a single cheap map access with no per-call module resolution,
//    and a stale/unloaded module's symbols can never shadow whatever's actually live right now.
// The active tables are rebuilt lazily by UpdateActiveSymbols() - AddModule/UnloadModule just
// set activeNeedUpdate_, and the rebuild happens on the next query.
//
// Module index 0 is reserved to mean "unknown module" and is always treated as active - it
// exists for backward compatibility with old flat (non-per-module) symbol files, and for
// symbols added without an explicit module.
class SymbolMap {
public:
	SymbolMap();

	// Changes every time anything in the map changes, so a UI that caches a flattened copy of
	// it (see GetAllActiveSymbols) can tell that its copy went stale without being explicitly
	// told. Values are unique across SymbolMap instances, not just within one - a new map is
	// created for every game boot (see PSP_Init), so a per-instance counter starting over at
	// zero would let a fresh map's version compare equal to a cached one from the last game.
	uint32_t Version() const { return version_; }

	void Clear();
	void SortSymbols();

	bool LoadSymbolMap(const Path &filename);
	bool SaveSymbolMap(const Path &filename) const;
	bool LoadNocashSym(const Path &filename);
	bool SaveNocashSym(const Path &filename) const;

	// Save/load just one module's symbols, e.g. for the WebSocket debugger's
	// hle.module.saveSymbols/loadSymbols, or the auto-save/load hooks around module load/unload
	// in Core/HLE/sceKernelModule.cpp (gated on g_Config.bAutoSaveLoadSymbols). Addresses inside
	// the file are relative to the module's load address (like the master tables above), so a
	// saved file stays valid however the module ends up positioned on a later run.
	// The file is keyed by module name + crc, not by game - see GetModuleSymbolsPath - so it's
	// deliberately shared by every game that happens to load the exact same module (common for
	// kernel/driver modules, or a homebrew's own libraries). gameID/gameTitle are only recorded
	// as an informational "last saved by" comment for a human reading the file; they don't
	// affect the file path or matching.
	// LoadModuleSymbols requires moduleIndex to currently be an active module (so relative
	// addresses can be resolved), and overwrites any existing names for symbols it touches -
	// the file is assumed to be the authoritative, possibly hand-edited version.
	// Pass moduleIndex 0 for the symbols that aren't inside any module - see GetGameSymbolsPath.
	// Only symbols a human chose are saved; if there are none, no file is written and any previous
	// one is removed, so an unnamed module doesn't leave a file behind.
	bool SaveModuleSymbols(int moduleIndex, const Path &filename, const std::string &gameID, const std::string &gameTitle) const;
	bool LoadModuleSymbols(int moduleIndex, const Path &filename);
	// Standard per-module symbol file path: <memstick>/PSP/SYSTEM/SYMBOLS/<moduleName>_<crc>.ppsym
	static Path GetModuleSymbolsPath(const std::string &moduleName, u32 crc);
	// Symbols that aren't inside any module (module index 0) are absolute addresses the user - or
	// a loaded map file - attached to RAM directly: heap, stack, scratchpad, hardware registers.
	// Those describe one game's own memory layout and are worthless to any other game, so unlike
	// module symbols they're keyed by game rather than shared:
	// <memstick>/PSP/SYSTEM/SYMBOLS/<gameID>_syms.ppsym
	static Path GetGameSymbolsPath(const std::string &gameID);
	// 0 if moduleIndex isn't known (never seen, not just inactive).
	u32 GetModuleCrc(int moduleIndex) const;

	SymbolType GetSymbolType(u32 address);
	bool GetSymbolInfo(SymbolInfo *info, u32 address, SymbolType symmask = ST_FUNCTION);
	u32 GetNextSymbolAddress(u32 address, SymbolType symmask);
	std::string GetDescription(u32 address);
	std::vector<SymbolEntry> GetAllActiveSymbols(SymbolType symmask);

#ifdef _WIN32
	void FillSymbolListBox(HWND listbox, SymbolType symType);
#endif
	void GetLabels(std::vector<LabelDefinition> &dest);

	// crc is optional (0 = unknown) and lets a reload of the same-named module be told apart
	// from a different binary that just happens to share a name (common with generic module
	// names) - see AddModule's implementation comment. Passing 0 falls back to matching by
	// name alone, same as before crc existed.
	void AddModule(const char *name, u32 address, u32 size, u32 crc = 0);
	void UnloadModule(u32 address, u32 size);
	u32 GetModuleRelativeAddr(u32 address, int moduleIndex = -1) const;
	u32 GetModuleAbsoluteAddr(u32 relative, int moduleIndex) const;
	// Index of the currently *active* module containing address, or -1 if none does (including
	// if address falls in a gap between active modules' ranges).
	int GetModuleIndex(u32 address) const;
	// Prefers a currently active module if the name is ambiguous; otherwise the most recently
	// added module entry with that name (which may be inactive). -1 if never seen.
	int GetModuleIndexByName(const std::string &name) const;
	// Turns the moduleIndex an AddFunction/AddData/AddLabel caller passed into one that's safe to
	// store: -1 means "work it out from the address", and an address in no module is module 0
	// ("absolute"), never -1. See the implementation for why that distinction matters.
	int ResolveModuleIndex(u32 address, int moduleIndex);
	bool IsModuleActive(int moduleIndex);
	std::vector<LoadedModuleInfo> getAllModules() const;

	// updateName controls whether an existing label at this address gets its name overwritten -
	// see AddLabel.
	void AddFunction(const char* name, u32 address, u32 size, int moduleIndex = -1, bool updateName = false);
	u32 GetFunctionStart(u32 address);
	int GetFunctionNum(u32 address);
	u32 GetFunctionSize(u32 startAddress);
	u32 GetFunctionModuleAddress(u32 startAddress);
	bool SetFunctionSize(u32 startAddress, u32 newSize);
	bool RemoveFunction(u32 startAddress, bool removeName);
	// Search for the first address their may be a function after address.
	// Only valid for currently loaded modules.  Not guaranteed there will be a function.
	u32 FindPossibleFunctionAtAfter(u32 address);

	// By default, an existing label's name is left alone (first writer wins) - this protects a
	// deliberately-assigned name from being clobbered by a later, lower-confidence automatic
	// detection pass. Pass updateName=true to override this for a source that should take
	// priority, e.g. restoring symbols explicitly saved for this module.
	void AddLabel(const char* name, u32 address, int moduleIndex = -1, bool updateName = false);
	std::string GetLabelString(u32 address);
	void SetLabelName(const char* name, u32 address);
	bool GetLabelValue(const char* name, u32& dest);

	void AddData(u32 address, u32 size, DataType type, int moduleIndex = -1);
	u32 GetDataStart(u32 address);
	u32 GetDataSize(u32 startAddress);
	u32 GetDataModuleAddress(u32 startAddress);
	DataType GetDataType(u32 startAddress);
	bool RemoveData(u32 startAddress, bool removeName);

	static const u32 INVALID_ADDRESS = (u32)-1;

	void UpdateActiveSymbols();

private:
	// Call from anything that adds, removes, renames or moves a symbol or module.
	void Bump();
	void AssignFunctionIndices();
	const char *GetLabelName(u32 address);
	const char *GetLabelNameRel(u32 relAddress, int moduleIndex) const;

	struct FunctionEntry {
		u32 start;
		u32 size;
		int index;
		int module;
	};

	struct LabelEntry {
		u32 addr;
		int module;
		char name[256];
	};

	struct DataEntry {
		DataType type;
		u32 start;
		u32 size;
		int module;
	};

	struct ModuleEntry {
		// Note: this index is +1, 0 matches any for backwards-compat.
		int index;
		u32 start;
		u32 size;
		char name[256];
		// 0 = unknown. See AddModule.
		u32 crc = 0;
	};

	// These are flattened, read-only copies of the actual data in active modules only.
	std::map<u32, const FunctionEntry> activeFunctions;
	std::map<u32, const LabelEntry> activeLabels;
	std::map<u32, const DataEntry> activeData;
	bool activeNeedUpdate_ = false;

	// This is indexed by the end address of the module.
	std::map<u32, const ModuleEntry> activeModuleEnds;

	// Module ID, index
	typedef std::pair<int, u32> SymbolKey;

	// These are indexed by the module id and relative address in the module.
	std::map<SymbolKey, FunctionEntry> functions;
	std::map<SymbolKey, LabelEntry> labels;
	std::map<SymbolKey, DataEntry> data;
	std::vector<ModuleEntry> modules;

	uint32_t version_ = 0;
	bool sawUnknownModule = false;
};

extern SymbolMap *g_symbolMap;


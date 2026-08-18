// Copyright (c) 2026- PPSSPP Project.

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

#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"

// Source line information decoded from an ELF's DWARF .debug_line section.
//
// Only available where an unstripped ELF is: PRX conversion drops every .debug section, so this
// never applies to a commercial game, and in practice it means homebrew that ships its app.elf
// next to the EBOOT (the same thing the companion symbol loader relies on).
//
// Addresses are stored absolute, relocated to wherever the module was loaded. Unlike SymbolMap,
// which keeps module-relative addresses so a saved .ppsym can be reloaded by a different game that
// pulls in the same module, none of this is ever written anywhere - it's regenerated from the ELF
// on every boot - so there'd be nothing for relative addresses to buy.
struct LineEntry {
	u32 address;
	// 0 marks the end of a sequence: the address is one past the last instruction the preceding
	// rows describe. Without these, a lookup for an address in a gap (a compilation unit built
	// without debug info, say) silently reports the last line of an unrelated source file - it
	// mis-attributed 70 of 349 functions in one test binary before they were recorded.
	u32 line;
	u32 fileIndex;
};

class LineInfoMap {
public:
	// Parses .debug_line out of an unstripped ELF image. Returns the number of rows kept, or 0 if
	// the ELF has nothing usable. Replaces whatever was held for the same module.
	//
	// addressDelta is added to every address in the table, and rows that don't then land inside
	// the module are dropped. That covers both shapes this arrives in: a companion ELF links at
	// zero, so the delta is the module's base; an ELF launched directly and loaded where it asked
	// to be already has final addresses, so the delta is zero.
	int AddModule(std::string_view elfData, u32 moduleStart, u32 moduleSize, u32 addressDelta);

	// Each module owns its rows and its file names outright, so dropping one can never disturb
	// another's. Note this isn't called when a module unloads: a savestate load destroys and
	// rebuilds every kernel object, and doing it there discarded the table every time - AddModule
	// replacing by key is what retires a range instead, when something else claims it. See
	// ~PSPModule.
	void RemoveModule(u32 moduleStart, u32 moduleSize);
	void Clear();

	bool IsEmpty() const { return modules_.empty(); }

	// The source location of the instruction at this address. False when no loaded module owns the
	// address, or when it falls in a gap between sequences.
	bool Lookup(u32 address, std::string *file, int *line) const;

	// "file.c:123", or empty if unknown. For status bars and log lines.
	std::string LookupString(u32 address) const;

private:
	struct ModuleLines {
		u32 start = 0;
		u32 size = 0;
		std::vector<std::string> files;
		std::vector<LineEntry> entries;  // Sorted by address.
	};

	const ModuleLines *FindModule(u32 address) const;

	std::vector<ModuleLines> modules_;
};

extern LineInfoMap g_lineInfo;

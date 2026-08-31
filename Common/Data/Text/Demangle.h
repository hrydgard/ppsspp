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

// Demangler for Itanium C++ ABI symbol names ("_Z..."), which is what GCC and Clang
// produce - and thus what the PSP toolchain produces. Handy for homebrew built from C++,
// where the ELF symbol table is otherwise unreadable.
//
// Handles the constructs that show up in practice, and simply fails on the rest (complex
// template expressions, mostly) rather than emitting something wrong.

// Returns false and leaves *out alone if this isn't a mangled name, or we can't parse it.
bool DemangleItanium(std::string_view mangled, std::string *out);

// The other two manglings that show up in PSP binaries, both from compilers older than the
// PSP SDK's GCC: Metrowerks CodeWarrior (a descendant of the AT&T cfront scheme) and SN
// Systems' SNC/ProDG. Both are much rougher than the Itanium demangler above - they aim to
// recover a readable, correctly qualified *name* and make a best effort at the parameter
// list, rather than to reproduce any particular tool's output byte for byte.

// A demangled symbol, kept split up so callers can use the parts. The Itanium demangler
// doesn't fill this in (it prints straight to a string); the two below do.
struct DemangledSymbol {
	std::string name;        // Qualified, no parameters: "ANIMEData::operator=".
	std::string parameters;  // What goes between the parens. Empty means "()".
	std::string returnType;  // Usually empty - only templates encode one. Also carries the
	                         // "static" of an SN Systems static member function.
	std::string qualifiers;  // "const" and friends, printed after the parameter list.
	bool isFunction = false; // False for data symbols, where there are no parens at all.

	std::string ToString() const;
};

// Metrowerks CodeWarrior: "getDistance__6KzUtilFP7st_unitP7st_unit".
// See docs/CodeWarriorMangling.md.
bool DemangleCodeWarrior(std::string_view mangled, DemangledSymbol *out);

// SN Systems (SNC/ProDG): "__0fLCHeapMemoryFAlloci".
// See docs/SNSystemsMangling.md.
bool DemangleSNSystems(std::string_view mangled, DemangledSymbol *out);

// Convenience wrapper: tries all three manglings and returns the demangled name, or a
// copy of the input if it isn't something we can demangle.
std::string DemangleSymbolName(std::string_view name);

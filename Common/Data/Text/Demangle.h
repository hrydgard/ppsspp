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

// Convenience wrapper: returns the demangled name, or a copy of the input if it isn't
// something we can demangle.
std::string DemangleSymbolName(std::string_view name);

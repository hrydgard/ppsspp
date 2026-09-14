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
#include "Common/File/Path.h"
#include <string>

// Root directory the importer extracts into and the library scans:
//   <memstick>/PSP/shaders/slang
Path GetSlangShaderDir();

// Given a destination root and a zip entry's internal name, compute the safe on-disk
// output path. Returns false (reject the entry) if the name is absolute, contains a
// ".." traversal component, is empty, or the resolved path escapes 'destRoot'.
// Also returns false for entry names whose extension is not an allowed shader asset
// (.slang, .slangp, .inc, .h, .png) unless 'isDirectory' is true.
bool ResolveSafeZipEntryPath(const Path &destRoot, const std::string &entryName,
                             bool isDirectory, Path *outPath);

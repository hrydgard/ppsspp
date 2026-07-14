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
#include "Common/File/Path.h"

// Extract a slang-shaders zip at 'zipPath' into 'destRoot' (default GetSlangShaderDir()),
// using a temp dir + atomic swap. Only shader-asset files that pass ResolveSafeZipEntryPath
// are written. On success, writes destRoot/manifest.json (sourceUrl, timestamp, fileCount).
// Returns false and leaves any prior install untouched on any failure; *error is set.
// 'sourceUrl' and 'unixTimestamp' are recorded in the manifest (pass "" / 0 if unknown).
// Synchronous; Task 7 calls this from a worker thread.
bool ExtractSlangPackage(const Path &zipPath, const Path &destRoot,
                         const std::string &sourceUrl, int64_t unixTimestamp,
                         std::string *error);

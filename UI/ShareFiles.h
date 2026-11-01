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

#include <string>
#include <string_view>
#include <vector>

#include "Common/File/Path.h"

struct ShareZipEntry {
	Path source;  // Any file we can read, content:// URIs included.
	std::string nameInZip;  // Empty for the source's own filename.
};

// Whether ShareFilesAsZip can work here (the platform can share files).
bool CanShareFiles();

// Zips the files up and offers the zip to other apps through the system share sheet (System_ShareFile),
// for example a savestate and its screenshot, to send to a friend. zipName is what the receiver sees, and is
// sanitized. False if the zip couldn't be made, or the platform can't share files.
//
// The zip is written to a folder of its own in the app's cache, since the receiving app reads it whenever
// it gets around to it, and deleted by a later call once it's an hour old. The files are read into memory
// and compressed on the calling thread, which is fine for savestates but not for whole ISOs.
bool ShareFilesAsZip(std::string_view zipName, const std::vector<ShareZipEntry> &files);

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

#include <functional>
#include <string>

#include "Common/File/Path.h"

struct StorageTestResult {
	std::string name;
	bool passed;
	// What failed, or for a pass, anything worth knowing (like which way DeleteDir went).
	std::string detail;
};

// Exercises the file functions and the memory stick move in a scratch folder inside root, deleted again
// afterwards. root can be a native path, or on Android a picked folder (a content URI), which is the case
// the unit tests can't reach: they have no Java VM. report is called after each test, on the calling thread.
// Returns the number of failures.
int RunStorageTests(const Path &root, const std::function<void(const StorageTestResult &)> &report);

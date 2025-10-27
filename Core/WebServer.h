// Copyright (c) 2014- PPSSPP Project.

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
#include "Common/Common.h"

class Path;

enum class WebServerFlags {
	NONE = 0,
	DISCS = 1,
	DEBUGGER = 2,
	FILE_UPLOAD = 4,

	ALL = 1 | 2 | 4,
};
ENUM_CLASS_BITOPS(WebServerFlags);

bool StartWebServer(WebServerFlags flags);
bool StopWebServer(WebServerFlags flags);
bool WebServerStopping(WebServerFlags flags);
bool WebServerStopped(WebServerFlags flags);
bool WebServerRunning(WebServerFlags flags);
void ShutdownWebServer();

bool RemoteISOFileSupported(const std::string &filename);
void WebServerSetUploadPath(const Path &path);
int WebServerPort();

struct UploadProgress {
	s64 totalBytes = 0;
	s64 uploadedBytes = 0;
	s64 currentFileSize = 0;
	std::string filename;
};

std::vector<UploadProgress> GetUploadsInProgress();

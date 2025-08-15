// Copyright (c) 2023- PPSSPP Project.

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

#include <list>
#include <set>
#include <string>

#include "Common/File/DirListing.h"
#include "StoragePickers.h"

// Locations
std::string GetPSPFolder(); // Where main data is, default is app data
std::string GetInstallationFolder();
std::string GetLocalFolder();
std::string GetTempFolder();
std::string GetTempFile(std::string name);
std::string GetPicturesFolder(); // Requires 'picturesLibrary' capability
std::string GetVideosFolder(); // Requires 'videosLibrary' capability
std::string GetDocumentsFolder(); // Requires 'documentsLibrary' capability
std::string GetMusicFolder(); // Requires 'musicLibrary' capability
std::string GetPreviewPath(std::string path);
bool isLocalState(std::string path);

// Management
// `GetFileStreamFromApp` Will use Windows UWP API, use it instead of fopen..etc
FILE* GetFileStreamFromApp(std::string path, const char* mode);

// 'driveName' like C:
bool CheckDriveAccess(std::string driveName);
bool GetFakeFolders(Path path, std::vector<File::FileInfo>* files, const char* filter, std::set<std::string> filters);
bool IsRootForAccessibleItems(std::string path);

// Helpers
bool OpenFile(std::string path);
bool OpenFolder(std::string path);
std::string ResolvePathUWP(std::string path);
bool GetDriveFreeSpace(Path path, int64_t& space);

// Log helpers
std::string GetLogFile();

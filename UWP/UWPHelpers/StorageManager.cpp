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

#include "pch.h"
#include <io.h>
#include <fcntl.h>
#include <collection.h>

#include "Common/Log.h"
#include "Core/Config.h"
#include "Common/File/Path.h"
#include "Common/StringUtils.h"
#include "UWPUtil.h"

#include "StorageManager.h"
#include "StorageAsync.h"
#include "StorageAccess.h"


using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Windows::ApplicationModel;


#pragma region Locations
std::string GetPSPFolder() {
	if (g_Config.memStickDirectory.empty()) {
		return g_Config.internalDataDirectory.ToString();
	}
	else {
		return g_Config.memStickDirectory.ToString();
	}
}
std::string GetInstallationFolder() {
	return FromPlatformString(Package::Current->InstalledLocation->Path);
}
StorageFolder^ GetLocalStorageFolder() {
	return ApplicationData::Current->LocalFolder;
}
std::string GetLocalFolder() {
	return FromPlatformString(GetLocalStorageFolder()->Path);
}
std::string GetTempFolder() {
	return FromPlatformString(ApplicationData::Current->TemporaryFolder->Path);
}
std::string GetTempFile(std::string name) {
	StorageFile^ tmpFile;
	ExecuteTask(tmpFile, ApplicationData::Current->TemporaryFolder->CreateFileAsync(ToPlatformString(name), CreationCollisionOption::GenerateUniqueName));
	if (tmpFile != nullptr) {
		return FromPlatformString(tmpFile->Path);
	}
	else {
		return "";
	}
}
std::string GetPicturesFolder() {
	// Requires 'picturesLibrary' capability
	return FromPlatformString(KnownFolders::PicturesLibrary->Path);
}
std::string GetVideosFolder() {
	// Requires 'videosLibrary' capability
	return FromPlatformString(KnownFolders::VideosLibrary->Path);
}
std::string GetDocumentsFolder() {
	// Requires 'documentsLibrary' capability
	return FromPlatformString(KnownFolders::DocumentsLibrary->Path);
}
std::string GetMusicFolder() {
	// Requires 'musicLibrary' capability
	return FromPlatformString(KnownFolders::MusicLibrary->Path);
}
std::string GetPreviewPath(std::string path) {
	std::string pathView = path;
	pathView = ReplaceAll(pathView, "/", "\\");
	std::string currentMemoryStick = GetPSPFolder();
	// Ensure memStick sub path replaced by 'ms:'
	pathView = ReplaceAll(pathView, currentMemoryStick + "\\", "ms:\\");
	auto appData = ReplaceAll(GetLocalFolder(), "\\LocalState", "");
	pathView = ReplaceAll(pathView, appData, "AppData");

	return pathView;
}
bool isLocalState(std::string path) {
	return !_stricmp(GetPreviewPath(path).c_str(), "LocalState");
}
#pragma endregion

#pragma region Internal
Path PathResolver(Path path) {
	auto root = path.GetDirectory();
	auto newPath = path.ToString();
	if (path.IsRoot() || !_stricmp(root.c_str(), "/") || !_stricmp(root.c_str(), "\\")) {
		// System requesting file from app data
		newPath = ReplaceAll(newPath, "/", (GetLocalFolder() + (path.size() > 1 ? "/" : "")));
	}
	return Path(newPath);
}
Path PathResolver(std::string path) {
	return PathResolver(Path(path));
}

std::string ResolvePathUWP(std::string path) {
	return PathResolver(path).ToString();
}
#pragma endregion

#pragma region Functions
std::map<std::string, bool> accessState;
bool CheckDriveAccess(std::string driveName) {
	bool state = false;

	HANDLE searchResults;
	WIN32_FIND_DATA findDataResult;
	auto keyIter = accessState.find(driveName);
	if (keyIter != accessState.end()) {
		state = keyIter->second;
	}
	else {
		try {
			wchar_t* filteredPath = _wcsdup(ConvertUTF8ToWString(driveName).c_str());
			wcscat_s(filteredPath, sizeof(L"\\*.*"), L"\\*.*");

			searchResults = FindFirstFileExFromAppW(
				filteredPath, FindExInfoBasic, &findDataResult,
				FindExSearchNameMatch, NULL, 0);

			state = searchResults != NULL && searchResults != INVALID_HANDLE_VALUE;
			if (state) {
				FindClose(searchResults);
			}
			// Cache the state
			accessState.insert(std::make_pair(driveName, state));
		}
		catch (...) {
		}
	}
	if (!state) {
		state = IsRootForAccessibleItems(driveName);
	}
	return state;
}

FILE* GetFileStreamFromApp(std::string path, const char* mode) {

	FILE* file{};

	auto pathResolved = Path(ResolvePathUWP(path));
	HANDLE handle;

	DWORD dwDesiredAccess = GENERIC_READ;
	DWORD dwShareMode = FILE_SHARE_READ;
	DWORD dwCreationDisposition = OPEN_EXISTING;
	int flags = 0;

	if (!strcmp(mode, "r") || !strcmp(mode, "rb") || !strcmp(mode, "rt"))
	{
		dwDesiredAccess = GENERIC_READ;
		dwShareMode = FILE_SHARE_READ;
		dwCreationDisposition = OPEN_EXISTING;
		flags = _O_RDONLY;
	}
	else if (!strcmp(mode, "r+") || !strcmp(mode, "rb+") || !strcmp(mode, "r+b") || !strcmp(mode, "rt+") || !strcmp(mode, "r+t"))
	{
		dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
		dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
		dwCreationDisposition = OPEN_EXISTING;
		flags = _O_RDWR;
	}
	else if (!strcmp(mode, "a") || !strcmp(mode, "ab") || !strcmp(mode, "at")) {
		dwDesiredAccess = GENERIC_WRITE;
		dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
		dwCreationDisposition = OPEN_ALWAYS;
		flags = _O_APPEND | _O_WRONLY | _O_CREAT;
	}
	else if (!strcmp(mode, "a+") || !strcmp(mode, "ab+") || !strcmp(mode, "a+b") || !strcmp(mode, "at+") || !strcmp(mode, "a+t")) {
		dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
		dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
		dwCreationDisposition = OPEN_ALWAYS;
		flags = _O_APPEND | _O_RDWR | _O_CREAT;
	}
	else if (!strcmp(mode, "w") || !strcmp(mode, "wb") || !strcmp(mode, "wt"))
	{
		dwDesiredAccess = GENERIC_WRITE;
		dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
		dwCreationDisposition = CREATE_ALWAYS;
		flags = _O_WRONLY | _O_CREAT | _O_TRUNC;
	}
	else if (!strcmp(mode, "w+") || !strcmp(mode, "wb+") || !strcmp(mode, "w+b") || !strcmp(mode, "wt+") || !strcmp(mode, "w+t"))
	{
		dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
		dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
		dwCreationDisposition = CREATE_ALWAYS;
		flags = _O_RDWR | _O_CREAT | _O_TRUNC;
	}

	if (strpbrk(mode, "t") != nullptr) {
		flags |= _O_TEXT;
	}

	handle = CreateFile2FromAppW(pathResolved.ToWString().c_str(), dwDesiredAccess, dwShareMode, dwCreationDisposition, nullptr);

	if (handle != INVALID_HANDLE_VALUE) {
		file = _fdopen(_open_osfhandle((intptr_t)handle, flags), mode);
	}

	return file;
}

#pragma endregion

#pragma region FakeFolders
// Parent and child full path
std::string getSubRoot(std::string parent, std::string child) {
	auto childCut = child;
	childCut = ReplaceAll(childCut, (parent + "/"), "");
	size_t len = childCut.find_first_of('/', 0);
	auto subRoot = childCut.substr(0, len);

	return parent + "/" + subRoot;
}

bool isChild(std::string parent, std::string child) {
	return child.find(parent) != std::string::npos;
}

// Parent full path, child full path, child name only
bool isParent(std::string parent, std::string child, std::string childName) {
	parent.append("/" + childName);
	return parent == child;
}

bool IsRootForAccessibleItems(Path path, std::list<std::string>& subRoot, bool breakOnFirstMatch = false) {
	path = PathResolver(path);
	auto FutureAccessItems = GetFutureAccessList();
	for (auto& fItem : FutureAccessItems) {
		if (isChild(path.ToString(), fItem)) {
			if (breakOnFirstMatch) {
				// Just checking, we don't need to loop for each item
				return true;
			}
			auto sub = getSubRoot(path.ToString(), fItem);

			// This check can be better, but that's how I can do it in C++
			if (!endsWith(sub, ":")) {
				bool alreadyAdded = false;
				for each (auto sItem in subRoot) {
					if (!strcmp(sItem.c_str(), sub.c_str())) {
						alreadyAdded = true;
						break;
					}
				}
				if (!alreadyAdded) {
					subRoot.push_back(sub);
				}
			}
		}
	}
	return !subRoot.empty();
}
bool IsRootForAccessibleItems(std::string path)
{
	std::list<std::string> tmp;
	return IsRootForAccessibleItems(Path(path), tmp, true);
}

bool GetFakeFolders(Path path, std::vector<File::FileInfo>* files, const char* filter, std::set<std::string> filters) {
	bool state = false;
	std::list<std::string> subRoot;
	if (IsRootForAccessibleItems(path, subRoot)) {
		if (!subRoot.empty()) {
			for each (auto sItem in subRoot) {
				auto folderPath = Path(sItem);
				File::FileInfo info;
				info.name = folderPath.GetFilename();
				info.fullName = folderPath;
				info.exists = true;
				info.size = 1;
				info.isDirectory = true;
				info.isWritable = true;
				info.atime = 0;
				info.mtime = 0;
				info.ctime = 0;
				info.access = 0111;

				files->push_back(info);
				state = true;
			}
		}
	}
	return state;
}

#pragma endregion

#pragma region Helpers
bool OpenFile(std::string path) {
	bool state = false;
	Platform::String^ wString = ref new Platform::String(Path(path).ToWString().c_str());

	StorageFile^ storageItem;
	ExecuteTask(storageItem, StorageFile::GetFileFromPathAsync(wString));
	if (storageItem != nullptr) {
		ExecuteTask(state, Windows::System::Launcher::LaunchFileAsync(storageItem), false);
	}
	else {
		auto uri = ref new Windows::Foundation::Uri(wString);
		ExecuteTask(state, Windows::System::Launcher::LaunchUriAsync(uri), false);
	}
	return state;
}

bool OpenFolder(std::string path) {
	bool state = false;
	Path itemPath(path);
	Platform::String^ wString = ref new Platform::String(itemPath.ToWString().c_str());
	StorageFolder^ storageItem;
	ExecuteTask(storageItem, StorageFolder::GetFolderFromPathAsync(wString));
	if (storageItem != nullptr) {
		ExecuteTask(state, Windows::System::Launcher::LaunchFolderAsync(storageItem), false);
	}
	else {
		// Try as it's file
		Path parent = Path(itemPath.GetDirectory());
		Platform::String^ wParentString = ref new Platform::String(parent.ToWString().c_str());

		ExecuteTask(storageItem, StorageFolder::GetFolderFromPathAsync(wParentString));
		if (storageItem != nullptr) {
			ExecuteTask(state, Windows::System::Launcher::LaunchFolderAsync(storageItem), false);
		}
	}
	return state;
}

bool GetDriveFreeSpace(Path path, int64_t& space) {

	bool state = false;
	if (path.empty()) {
		// This case happen on first start only
		path = Path(GetPSPFolder());
		if (g_Config.memStickDirectory.empty()) {
			g_Config.memStickDirectory = path;
		}
	}
	Platform::String^ wString = ref new Platform::String(path.ToWString().c_str());
	StorageFolder^ storageItem;
	ExecuteTask(storageItem, StorageFolder::GetFolderFromPathAsync(wString));
	if (storageItem != nullptr) {
		Platform::String^ freeSpaceKey = ref new Platform::String(L"System.FreeSpace");
		Platform::Collections::Vector<Platform::String^>^ propertiesToRetrieve = ref new Platform::Collections::Vector<Platform::String^>();
		propertiesToRetrieve->Append(freeSpaceKey);
		Windows::Foundation::Collections::IMap<Platform::String^, Platform::Object^>^ result;
		ExecuteTask(result, storageItem->Properties->RetrievePropertiesAsync(propertiesToRetrieve));
		if (result != nullptr && result->Size > 0) {
			try {
				auto value = result->Lookup(L"System.FreeSpace");
				space = (uint64_t)value;
				state = true;
			}
			catch (...) {

			}
		}
	}

	return state;
}
#pragma endregion

#pragma region Logs
std::string GetLogFile() {
	std::string logFile;
	Path logFilePath = Path(GetPSPFolder() + "\\PSP\\ppsspplog.txt");
	HANDLE h = CreateFile2FromAppW(logFilePath.ToWString().c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, CREATE_ALWAYS, nullptr);
	if (h != INVALID_HANDLE_VALUE) {
		logFile = logFilePath.ToString();
		CloseHandle(h);
	}
	return logFile;
}

#pragma endregion

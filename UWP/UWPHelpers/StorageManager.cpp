// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

#include "pch.h"
#include <io.h>
#include <fcntl.h>


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
std::string GetWorkingFolder() {
	if (g_Config.memStickDirectory.empty()) {
		return GetLocalFolder();
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
	pathView = ReplaceAll(pathView, GetLocalFolder(), "LocalState");
	pathView = ReplaceAll(pathView, GetTempFolder(), "TempState");
	pathView = ReplaceAll(pathView, GetInstallationFolder(), "Installation folder");
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

bool isWriteMode(const char* mode) {
	return (!strcmp(mode, "w") || !strcmp(mode, "wb") || !strcmp(mode, "wt") || !strcmp(mode, "at") || !strcmp(mode, "a"));
}
bool isAppendMode(const char* mode) {
	return (!strcmp(mode, "at") || !strcmp(mode, "a"));
}

FILE* GetFileStreamFromApp(std::string path, const char* mode) {

	FILE* file{};

	auto pathResolved = Path(ResolvePathUWP(path));
	HANDLE handle;
	auto access = GENERIC_READ;
	auto share = FILE_SHARE_READ;
	auto creation = OPEN_EXISTING;
	bool isWrite = isWriteMode(mode);
	bool isAppend = isAppendMode(mode);

	if (isWrite) {
		access = GENERIC_WRITE;
		share = FILE_SHARE_WRITE;
		creation = isAppend ? OPEN_ALWAYS : CREATE_ALWAYS;
	}
	handle = CreateFile2FromAppW(pathResolved.ToWString().c_str(), access, share, creation, nullptr);

	if (handle != INVALID_HANDLE_VALUE) {
		int flags = _O_RDONLY;
		if (isWrite) {
			flags = _O_RDWR;
		}
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
				auto attributes = FILE_ATTRIBUTE_DIRECTORY;
				File::FileInfo info;
				info.name = folderPath.GetFilename();
				info.fullName = folderPath;
				info.exists = true;
				info.size = 1;
				info.isDirectory = true;
				info.isWritable = (attributes & FILE_ATTRIBUTE_READONLY) == 0;
				info.atime = 1000;
				info.mtime = 1000;
				info.ctime = 1000;
				if (attributes & FILE_ATTRIBUTE_READONLY) {
					info.access = 0444;  // Read
				}
				else {
					info.access = 0666;  // Read/Write
				}
				if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
					info.access |= 0111;  // Execute
				}
				if (!info.isDirectory) {
					std::string ext = info.fullName.GetFileExtension();
					if (!ext.empty()) {
						ext = ext.substr(1);  // Remove the dot.
						if (filter && filters.find(ext) == filters.end()) {
							continue;
						}
					}
				}
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
	path = ReplaceAll(path, "/", "\\");

	StorageFile^ storageItem;
	ExecuteTask(storageItem, StorageFile::GetFileFromPathAsync(ToPlatformString(path)));
	if (storageItem != nullptr) {
		ExecuteTask(state, Windows::System::Launcher::LaunchFileAsync(storageItem), false);
	}
	else {
		auto uri = ref new Windows::Foundation::Uri(ToPlatformString(path));
		ExecuteTask(state, Windows::System::Launcher::LaunchUriAsync(uri), false);
	}
	return state;
}

bool OpenFolder(std::string path) {
	bool state = false;
	path = ReplaceAll(path, "/", "\\");

	StorageFolder^ storageItem;
	ExecuteTask(storageItem, StorageFolder::GetFolderFromPathAsync(ToPlatformString(path)));
	if (storageItem != nullptr) {
		ExecuteTask(state, Windows::System::Launcher::LaunchFolderAsync(storageItem), false);
	}
	return state;
}

bool IsFirstStart() {
	auto firstrun = GetDataFromLocalSettings("first_run");
	AddDataToLocalSettings("first_run", "done", true);
	return firstrun.empty();
}
#pragma endregion

#pragma region Logs
std::string GetLogFile() {
	Path logFilePath = Path(GetWorkingFolder() + "\\PSP\\ppsspp.txt");
	HANDLE h = CreateFile2FromAppW(logFilePath.ToWString().c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, CREATE_ALWAYS, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return std::string();
	}
	return logFilePath.ToString();
}

#pragma endregion

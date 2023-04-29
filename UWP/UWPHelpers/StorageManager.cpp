// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

// Functions:
// GetWorkingFolder()
// SetWorkingFolder(std::string location)
// GetInstallationFolder()
// GetLocalFolder()
// GetTempFolder()
// GetPicturesFolder()
// GetVideosFolder()
// GetDocumentsFolder()
// GetMusicFolder()
// GetPreviewPath(std::string path)
//
// CreateFileUWP(std::string path, int accessMode, int shareMode, int openMode)
// CreateFileUWP(std::wstring path, int accessMode, int shareMode, int openMode)
// GetFileStream(std::string path, const char* mode)
// IsValidUWP(std::string path)
// IsExistsUWP(std::string path)
// IsDirectoryUWP(std::string path)
// 
// GetFolderContents(std::string path, T& files)
// GetFolderContents(std::wstring path, T& files)
// GetFileInfoUWP(std::string path, T& info)
//
// GetSizeUWP(std::string path)
// DeleteUWP(std::string path)
// CreateDirectoryUWP(std::string path, bool replaceExisting)
// RenameUWP(std::string path, std::string name)
// CopyUWP(std::string path, std::string name)
// MoveUWP(std::string path, std::string name)
//
// OpenFile(std::string path)
// OpenFolder(std::string path)
// IsFirstStart()
//
// GetLogFile()
// SaveLogs()
// CleanupLogs()

#include "pch.h"

#include "Common/Log.h"

#include "StorageConfig.h"
#include "StorageManager.h"
#include "StorageExtensions.h"
#include "StorageHandler.h"
#include "StorageAsync.h"
#include "StorageAccess.h"
#include "StorageItemW.h"
#include "StorageLog.h"


using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Windows::ApplicationModel;

extern std::list<StorageItemW> FutureAccessItems;

#pragma region Locations
std::string GetWorkingFolder() {
	if (AppWorkingFolder.empty()) {
		return GetLocalFolder();
	}
	else {
		return AppWorkingFolder;
	}
}
void SetWorkingFolder(std::string location) {
	AppWorkingFolder = location;
}
std::string GetInstallationFolder() {
	return convert(Package::Current->InstalledLocation->Path);
}
StorageFolder^ GetLocalStorageFolder() {
	return ApplicationData::Current->LocalFolder;
}
std::string GetLocalFolder() {
	return convert(GetLocalStorageFolder()->Path);
}
std::string GetTempFolder() {
	return convert(ApplicationData::Current->TemporaryFolder->Path);
}
std::string GetTempFile(std::string name) {
	StorageFile^ tmpFile;
	ExecuteTask(tmpFile, ApplicationData::Current->TemporaryFolder->CreateFileAsync(convert(name), CreationCollisionOption::GenerateUniqueName));
	if (tmpFile != nullptr) {
		return convert(tmpFile->Path);
	}
	else {
		return "";
	}
}
std::string GetPicturesFolder() {
	// Requires 'picturesLibrary' capability
	return convert(KnownFolders::PicturesLibrary->Path);
}
std::string GetVideosFolder() {
	// Requires 'videosLibrary' capability
	return convert(KnownFolders::VideosLibrary->Path);
}
std::string GetDocumentsFolder() {
	// Requires 'documentsLibrary' capability
	return convert(KnownFolders::DocumentsLibrary->Path);
}
std::string GetMusicFolder() {
	// Requires 'musicLibrary' capability
	return convert(KnownFolders::MusicLibrary->Path);
}
std::string GetPreviewPath(std::string path) {
	std::string pathView = path;
	windowsPath(pathView);
	replace(pathView, GetLocalFolder(), "LocalState");
	replace(pathView, GetTempFolder(), "TempState");
	replace(pathView, GetInstallationFolder(), "Installation folder");
	return pathView;
}
bool isLocalState(std::string path) {
	return iequals(GetPreviewPath(path), "LocalState");
}
#pragma endregion

#pragma region Internal
PathUWP PathResolver(PathUWP path) {
	auto root = path.GetDirectory();
	auto newPath = path.ToString();
	if (path.IsRoot() || iequals(root, "/") || iequals(root, "\\")) {
		// System requesting file from app data
		replace(newPath, "/", (GetLocalFolder() + (path.size() > 1 ? "/": "")));
	}
	path = PathUWP(newPath);
	return path;
}
PathUWP PathResolver(std::string path) {
	return PathResolver(PathUWP(path));
}

std::string ResolvePathUWP(std::string path) {
	return PathResolver(path).ToString();
}

// Return closer parent
StorageItemW GetStorageItemParent(PathUWP path) {
	path = PathResolver(path);
	StorageItemW parent;

	for (auto& fItem : FutureAccessItems) {
		if (isChild(fItem.GetPath(), path.ToString())) {
			if (fItem.IsDirectory()) {
				parent = fItem;
				break;
			}
		}
	}

	return parent;
}

StorageItemW GetStorageItem(PathUWP path, bool createIfNotExists = false, bool forceFolderType = false) {
	// Fill call will be ignored internally after the first call
	FillLookupList();
	
	path = PathResolver(path);
	StorageItemW item;

	// Look for match in FutureAccessItems
	for (auto& fItem : FutureAccessItems) {
		if (fItem.Equal(path)) {
			item = fItem;
			break;
		}
	}

	if (!item.IsValid()) {
		// Look for match inside FutureAccessFolders
		for (auto& fItem : FutureAccessItems) {
			if (fItem.IsDirectory()) {
				IStorageItem^ storageItem;
				if (fItem.Contains(path, storageItem)) {
					item = StorageItemW(storageItem);
					break;
				}
			}
		}
	}

	if (!item.IsValid() && createIfNotExists) {
		// Create and return new folder
		auto parent = GetStorageItemParent(path);
		if (parent.IsValid()) {
			if (!forceFolderType) {
				// File creation must be called in this case
				// Create folder usually will be called from 'CreateDirectory'
				item = StorageItemW(parent.CreateFile(path));
			}
			else {
				item = StorageItemW(parent.CreateFolder(path));
			}
		}
	}
	return item;
}

StorageItemW GetStorageItem(std::string path, bool createIfNotExists = false, bool forceFolderType = false) {
	return GetStorageItem(PathUWP(path), createIfNotExists, forceFolderType);
}

std::list<StorageItemW> GetStorageItemsByParent(PathUWP path) {
	path = PathResolver(path);
	std::list<StorageItemW> items;

	// Look for match in FutureAccessItems
	for (auto& fItem : FutureAccessItems) {
		if (isParent(path.ToString(), fItem.GetPath(), fItem.GetName())) {
			items.push_back(fItem);
		}
	}

	return items;
}

std::list<StorageItemW> GetStorageItemsByParent(std::string path) {
	return GetStorageItemsByParent(PathUWP(path));
}

bool IsContainsAccessibleItems(PathUWP path) {
	path = PathResolver(path);

	for (auto& fItem : FutureAccessItems) {
		if (isParent(path.ToString(), fItem.GetPath(), fItem.GetName())) {
			return true;
		}
	}

	return false;
}

bool IsContainsAccessibleItems(std::string path) {
	return IsContainsAccessibleItems(PathUWP(path));
}

bool IsRootForAccessibleItems(PathUWP path, std::list<std::string>& subRoot, bool breakOnFirstMatch = false) {
	path = PathResolver(path);

	for (auto& fItem : FutureAccessItems) {
		if (isChild(path.ToString(), fItem.GetPath())) {
			if (breakOnFirstMatch) {
				// Just checking, we don't need to loop for each item
				return true;
			}
			auto sub = getSubRoot(path.ToString(), fItem.GetPath());

			// This check can be better, but that's how I can do it in C++
			if (!ends_with(sub, ":")) {
				bool alreadyAdded = false;
				for each (auto sItem in subRoot) {
					if (iequals(sItem, sub)) {
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

bool IsRootForAccessibleItems(std::string path, std::list<std::string>& subRoot, bool breakOnFirstMatch = false) {
	return IsRootForAccessibleItems(PathUWP(path), subRoot, breakOnFirstMatch);
}
bool IsRootForAccessibleItems(std::string path) {
	std::list<std::string> tmp;
	return IsRootForAccessibleItems(path, tmp, true);
}
#pragma endregion

#pragma region Functions
bool CreateIfNotExists(int openMode) {
	switch (openMode)
	{
	case OPEN_ALWAYS:
	case CREATE_NEW:
		return true;
		break;
	default:
		return false;
	}
}

HANDLE CreateFileUWP(std::string path, int accessMode, int shareMode, int openMode) {
	HANDLE handle = INVALID_HANDLE_VALUE;
	if (IsValidUWP(path)) {
		bool createIfNotExists = CreateIfNotExists(openMode);
		auto storageItem = GetStorageItem(path, createIfNotExists);
		
		if (storageItem.IsValid()) {
			DEBUG_LOG(FILESYS, "Getting handle (%s)", path.c_str());
		 	HRESULT hr = storageItem.GetHandle(&handle, accessMode, shareMode);
			if (hr == E_FAIL) {
				handle = INVALID_HANDLE_VALUE;
			}
		}
		else {
			handle = INVALID_HANDLE_VALUE;
			DEBUG_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
		}	
	}
    return handle;
}

HANDLE CreateFileUWP(std::wstring path, int accessMode, int shareMode, int openMode) {
	auto pathString = convert(path);
	return CreateFileUWP(pathString, accessMode, shareMode, openMode);
}

std::map<std::string, bool> accessState;
bool CheckDriveAccess(std::string driveName, bool checkIfContainsFutureAccessItems) {
	bool state = false;

	HANDLE searchResults;
	WIN32_FIND_DATA findDataResult;
	auto keyIter = accessState.find(driveName);
	if (keyIter != accessState.end()) {
		state = keyIter->second;
	}
	else {
		try {
			wchar_t* filteredPath = _wcsdup(convert(driveName)->Data());
			wcscat_s(filteredPath, sizeof(L"\\*.*"), L"\\*.*");
#if !defined(_M_ARM)
			searchResults = FindFirstFileExFromAppW(
				filteredPath, FindExInfoBasic, &findDataResult,
				FindExSearchNameMatch, NULL, 0);
#else
			searchResults = FindFirstFileEx(
				filteredPath, FindExInfoBasic, &findDataResult,
				FindExSearchNameMatch, NULL, 0);
#endif
			state = searchResults != NULL && searchResults != INVALID_HANDLE_VALUE;
			if (state) {
				FindClose(searchResults);
			}
			accessState.insert(std::make_pair(driveName, state));
		}
		catch (...) {
		}
	}

	if (!state && checkIfContainsFutureAccessItems) {
		// Consider the drive accessible in case it contain files/folder selected before to avoid empty results
		state = IsRootForAccessibleItems(driveName) || IsContainsAccessibleItems(driveName);
	}
	return state;
}
bool IsValidUWP(std::string path) {
	auto p = PathResolver(path);

	//Check valid path
	if (p.Type() == PathTypeUWP::UNDEFINED || !p.IsAbsolute()) {
		// Nothing to do here
		VERBOSE_LOG(FILESYS, "File is not valid (%s)", p.ToString().c_str());
		return false;
	}


	bool state = false;

	auto resolvedPathStr = p.ToString();
	if (ends_with(resolvedPathStr, "LocalState") || ends_with(resolvedPathStr, "TempState") || ends_with(resolvedPathStr, "LocalCache")) {
		state = true;
	}
	else
		if (isChild(GetLocalFolder(), resolvedPathStr)) {
			state = true;
		}
		else if (isChild(GetInstallationFolder(), resolvedPathStr)) {
			state = true;
		}
		else if (isChild(GetTempFolder(), resolvedPathStr)) {
			state = true;
		}

	if (!state)
	{
		auto p = PathUWP(path);
		std::string driveName = p.GetRootVolume().ToString();
		state = CheckDriveAccess(driveName, false);
	}

	return !state;
}

bool IsExistsUWP(std::string path) {
	if (IsValidUWP(path)) {
		auto storageItem = GetStorageItem(path);
		if (storageItem.IsValid()) {
			return true;
		}

		// If folder is not accessible but contains accessible items
		// consider it exists
		if (IsContainsAccessibleItems(path)) {
			return true;
		}

		// If folder is not accessible but is part of accessible items
		// consider it exists
		std::list<std::string> tmp;
		if (IsRootForAccessibleItems(path, tmp, true)) {
			return true;
		}
	}
	// ERROR_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
	return false;
}

bool IsDirectoryUWP(std::string path) {
	if (IsValidUWP(path)) {
		auto storageItem = GetStorageItem(path);
		if (storageItem.IsValid()) {
			if (storageItem.IsDirectory()) {
				return true;
			}
		}
	}
	return false;
}

FILE* GetFileStream(std::string path, const char* mode) {
	FILE* file{};
	if (IsValidUWP(path)) {
		auto storageItem = GetStorageItem(path);
		if (storageItem.IsValid()) {
			file = storageItem.GetStream(mode);
		}
		else {
			// Forward the request to parent folder
			auto p = PathUWP(path);
			auto itemName = p.GetFilename();
			auto rootPath = p.GetDirectory();
			if (IsValidUWP(rootPath)) {
				storageItem = GetStorageItem(rootPath);
				if (storageItem.IsValid()) {
					file = storageItem.GetFileStream(itemName, mode);
				}
				else {
					ERROR_LOG(FILESYS, "Couldn't find or access (%s)", rootPath.c_str());
					ERROR_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
				}
			}
		}
	}

	return file;
}

FILE* GetFileStreamFromApp(std::string path, const char* mode) {

	FILE* file{};

	auto pathResolved = PathUWP(ResolvePathUWP(path));
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
#if !defined(_M_ARM)
	handle = CreateFile2FromAppW(pathResolved.ToWString().c_str(), access, share, creation, nullptr);
#else
	handle = CreateFile2(pathResolved.ToWString().c_str(), access, share, creation, nullptr);
#endif
	if (handle != INVALID_HANDLE_VALUE) {
		int flags = _O_RDONLY;
		if (isWrite) {
			flags = _O_RDWR;
		}
		file = _fdopen(_open_osfhandle((intptr_t)handle, flags), mode);
	}

	return file;
}

#pragma region Content Helpers
ItemInfoUWP GetFakeFolderInfo(std::string folder) {
	ItemInfoUWP info;
	auto folderPath = PathUWP(folder);
	info.name = folderPath.GetFilename();
	info.fullName = folderPath.ToString();

	info.isDirectory = true;

	info.size = 1;
	info.lastAccessTime = 1000;
	info.lastWriteTime = 1000;
	info.changeTime = 1000;
	info.creationTime = 1000;

	info.attributes = FILE_ATTRIBUTE_DIRECTORY;

	return info;
}

#pragma endregion

std::list<ItemInfoUWP> GetFolderContents(std::string path, bool deepScan) {
	std::list<ItemInfoUWP> contents;

	if (IsValidUWP(path)) {
		auto storageItem = GetStorageItem(path);
		if (storageItem.IsValid()) {

			// Files
			// deepScan is slow, try to avoid it
			auto rfiles = deepScan ? storageItem.GetAllFiles() : storageItem.GetFiles();
			for each (auto file in rfiles) {
				contents.push_back(file.GetFileInfo());
			}

			// Folders
			// deepScan is slow, try to avoid it
			auto rfolders = deepScan ? storageItem.GetAllFolders() : storageItem.GetFolders();
			for each (auto folder in rfolders) {
				contents.push_back(folder.GetFolderInfo());
			}
		}
		else {
			DEBUG_LOG(FILESYS, "Cannot get contents!, checking for other options.. (%s)", path.c_str());
		}
;	}

	if (contents.size() == 0) {
		// Folder maybe not accessible or not exists
			// if not accessible, maybe some items inside it were selected before
			// and they already in our accessible list
		if (IsContainsAccessibleItems(path)) {
			DEBUG_LOG(FILESYS, "Folder contains accessible items (%s)", path.c_str());

			// Check contents
			auto cItems = GetStorageItemsByParent(path);
			if (!cItems.empty()) {
				for each (auto item in cItems) {
					VERBOSE_LOG(FILESYS, "Appending accessible item (%s)", item.GetPath().c_str());
					contents.push_back(item.GetItemInfo());
				}
			}
		}
		else
		{
			// Check if this folder is root for accessible item
			// then add fake folder as sub root to avoid empty results
			std::list<std::string> subRoot;
			if (IsRootForAccessibleItems(path, subRoot)) {
				DEBUG_LOG(FILESYS, "Folder is root for accessible items (%s)", path.c_str());

				if (!subRoot.empty()) {
					for each (auto sItem in subRoot) {
						VERBOSE_LOG(FILESYS, "Appending fake folder (%s)", sItem.c_str());
						contents.push_back(GetFakeFolderInfo(sItem));
					}
				}
			}
			else {
				VERBOSE_LOG(FILESYS, "Cannot get any content!.. (%s)", path.c_str());
			}
		}
	}
	return contents;
}
std::list<ItemInfoUWP> GetFolderContents(std::wstring path, bool deepScan) {
	return GetFolderContents(convert(path), deepScan);
}

ItemInfoUWP GetItemInfoUWP(std::string path) {
	ItemInfoUWP info;
	info.size = -1;
	info.attributes = INVALID_FILE_ATTRIBUTES;

	if (IsValidUWP(path)) {
		auto storageItem = GetStorageItem(path);
		if (storageItem.IsValid()) {
			info = storageItem.GetItemInfo();
		}
		else {
			ERROR_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
		}
	}
	return info;
}
#pragma endregion

#pragma region Basics
int64_t GetSizeUWP(std::string path) {
	int64_t size = 0;
	if (IsValidUWP(path)) {
		auto storageItem = GetStorageItem(path);
		if (storageItem.IsValid()) {
			size = storageItem.GetSize();
		}
		else {
			ERROR_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
		}
	}
	return size;
}

bool DeleteUWP(std::string path) {
	bool state = false;
	if (IsValidUWP(path)) {
		auto storageItem = GetStorageItem(path);
		if (storageItem.IsValid()) {
			DEBUG_LOG(FILESYS, "Delete (%s)", path.c_str());
			state = storageItem.Delete();
		}
		else {
			DEBUG_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
		}
	}

	return state;
}
  
bool CreateDirectoryUWP(std::string path, bool replaceExisting) {
	bool state = false;
	auto p = PathUWP(path);
	auto itemName = p.GetFilename();
	auto rootPath = p.GetDirectory();

	if (IsValidUWP(rootPath)) {
		auto storageItem = GetStorageItem(rootPath);
		if (storageItem.IsValid()) {
			DEBUG_LOG(FILESYS, "Create new folder (%s)", path.c_str());
			state = storageItem.CreateFolder(itemName, replaceExisting);
		}
		else {
			ERROR_LOG(FILESYS, "Couldn't find or access (%s)", rootPath.c_str());
		}
	}
	
	return state;
}

bool CopyUWP(std::string path, std::string dest) {
	bool state = false;

	if (IsValidUWP(path) && IsValidUWP(dest)) {
		auto srcStorageItem = GetStorageItem(path);
		if (srcStorageItem.IsValid()) {
			auto destDir = dest;
			auto srcName = srcStorageItem.GetName();
			auto dstPath = PathUWP(dest);
			auto dstName = dstPath.GetFilename();
			// Destination must be parent folder
			destDir = dstPath.GetDirectory();
			auto dstStorageItem = GetStorageItem(destDir, true, true);
			if (dstStorageItem.IsValid()) {
				DEBUG_LOG(FILESYS, "Copy (%s) to (%s)", path.c_str(), dest.c_str());
				state = srcStorageItem.Copy(dstStorageItem, dstName);
			}
			else {
				ERROR_LOG(FILESYS, "Couldn't find or access (%s)", dest.c_str());
			}
		}
		else {
			ERROR_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
		}
	}

	return state;
}

bool MoveUWP(std::string path, std::string dest) {
	bool state = false;

	if (IsValidUWP(path) && IsValidUWP(dest)) {
		auto srcStorageItem = GetStorageItem(path);
		
		if (srcStorageItem.IsValid()) {
			auto destDir = dest;
			auto srcName = srcStorageItem.GetName();
			auto dstPath = PathUWP(dest);
			auto dstName = dstPath.GetFilename();
			// Destination must be parent folder
			destDir = dstPath.GetDirectory();
			auto dstStorageItem = GetStorageItem(destDir, true, true);
			if (dstStorageItem.IsValid()) {
				DEBUG_LOG(FILESYS, "Move (%s) to (%s)", path.c_str(), dest.c_str());
				state = srcStorageItem.Move(dstStorageItem, dstName);
			}
			else {
				ERROR_LOG(FILESYS, "Couldn't find or access (%s)", dest.c_str());
			}
		}
		else {
			ERROR_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
		}
	}

	return state;
}

bool RenameUWP(std::string path, std::string name) {
	bool state = false;
	
	auto srcRoot = PathUWP(path).GetDirectory();
	auto dstRoot = PathUWP(name).GetDirectory();
	// Check if system using rename to move
	if (iequals(srcRoot, dstRoot)) {
		auto srcStorageItem = GetStorageItem(path);
		if (srcStorageItem.IsValid()) {
			DEBUG_LOG(FILESYS, "Rename (%s) to (%s)", path.c_str(), name.c_str());
			state = srcStorageItem.Rename(name);
		}
		else {
			DEBUG_LOG(FILESYS, "Couldn't find or access (%s)", path.c_str());
		}
	}
	else {
		DEBUG_LOG(FILESYS, " Rename used as move -> call move (%s) to (%s)", path.c_str(), name.c_str());
		state = MoveUWP(path, name);
	}

	return state;
}
#pragma endregion


#pragma region Helpers
bool OpenFile(std::string path) {
	bool state = false;

	auto storageItem = GetStorageItem(path);
	if (storageItem.IsValid()) {
		if (!storageItem.IsDirectory()) {
			ExecuteTask(state, Windows::System::Launcher::LaunchFileAsync(storageItem.GetStorageFile()), false);
		}
	}
	else {
		auto uri = ref new Windows::Foundation::Uri(convert(path));
		ExecuteTask(state, Windows::System::Launcher::LaunchUriAsync(uri), false);
	}
	return state;
}

bool OpenFolder(std::string path) {
	bool state = false;
	auto uri = ref new Windows::Foundation::Uri(convert(path));
	
	auto storageItem = GetStorageItem(path);
	if (storageItem.IsValid()) {
		if (storageItem.IsDirectory()) {
			ExecuteTask(state, Windows::System::Launcher::LaunchFolderAsync(storageItem.GetStorageFolder()), false);
		}
		else {
			OpenFile(storageItem.GetPath());
		}
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
// Get log file name
std::string currentLogFile;
std::string getLogFileName() {
	//Initial new name each session/launch
	if (currentLogFile.empty() || currentLogFile.size() == 0) {
		std::time_t now = std::time(0);
		char mbstr[100];
		std::strftime(mbstr, 100, "ppsspp %d-%m-%Y (%T).txt", std::localtime(&now));
		std::string formatedDate(mbstr);
		std::replace(formatedDate.begin(), formatedDate.end(), ':', '-');
		currentLogFile = formatedDate;
	}

	return currentLogFile;
}

// Get current log file location
StorageFolder^ GetLogsStorageFolder() {
	// Ensure 'LOGS' folder is created
	auto workingFolder = GetStorageItem(GetWorkingFolder());
	StorageFolder^ logsFolder;
	if (workingFolder.IsValid()) {
		auto workingStorageFolder = workingFolder.GetStorageFolder();
		ExecuteTask(logsFolder, workingStorageFolder->CreateFolderAsync("LOGS", CreationCollisionOption::OpenIfExists));
	}
	return logsFolder;
}
std::string GetLogFile() {
	std::string logFilePath = "";

	// Ensure 'LOGS' folder is created
	StorageFolder^ logsFolder = GetLogsStorageFolder();

	if (logsFolder != nullptr) {
		auto logFileName = convert(getLogFileName());
		StorageFile^ logFile;
		ExecuteTask(logFile, logsFolder->CreateFileAsync(logFileName, CreationCollisionOption::OpenIfExists));

		if (logFile != nullptr) {
			logFilePath = convert(logFile->Path);
		}
	}

	return logFilePath;
}

// Save logs to folder selected by the user
bool SaveLogs() {
	try {
		auto folderPicker = ref new Windows::Storage::Pickers::FolderPicker();
		folderPicker->SuggestedStartLocation = Windows::Storage::Pickers::PickerLocationId::Desktop;
		folderPicker->FileTypeFilter->Append("*");

		StorageFolder^ saveFolder;
		ExecuteTask(saveFolder, folderPicker->PickSingleFolderAsync());

		if (saveFolder != nullptr) {
			StorageFolder^ logsFolder = GetLogsStorageFolder();

			if (logsFolder != nullptr) {
				StorageFolderW logsCache(logsFolder);
				logsCache.Copy(saveFolder);
			}
		}
	}
	catch (...) {
		return false;
	}
	return true;
}

void CleanupLogs() {
	StorageFolder^ logsFolder = GetLogsStorageFolder();
	if (logsFolder != nullptr) {
		StorageFolderW logsCache(logsFolder);
		std::list<StorageFileW> files = logsCache.GetFiles();
		if (!files.empty()) {
			for each (auto fItem in files) {
				if (fItem.GetSize() == 0) {
					fItem.Delete();
				}
			}
		}
	}
}
#pragma endregion

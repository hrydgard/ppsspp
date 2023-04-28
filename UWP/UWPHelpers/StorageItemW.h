// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

#pragma once 

#include "pch.h"
#include <io.h>
#include <fcntl.h>

#include "StorageLog.h"
#include "StoragePath.h"
#include "StorageExtensions.h"
#include "StorageHandler.h"
#include "StorageAsync.h"
#include "StorageFolderW.h"

using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Windows::Storage::FileProperties;

class StorageItemW {
public:
	StorageItemW() {
	}

	StorageItemW(IStorageItem^ item) {
		storageItem = item;
		itemSize = 0;
		if (item != nullptr) {
			isDirectory = storageItem->IsOfType(StorageItemTypes::Folder);
			if (isDirectory) {
				storageFolderW = StorageFolderW(item);
			}
			else {
				storageFileW = StorageFileW(item);
			}
		}
	}
	StorageItemW(StorageFolderW folder) {
		StorageItemW((IStorageItem^)folder.GetStorageFolder());
	}
	StorageItemW(StorageFileW file) {
		StorageItemW((IStorageItem^)file.GetStorageFile());
	}
	~StorageItemW() {
		delete storageItem;
	}

	bool IsDirectory() {
		return isDirectory;
	}

	// Detect if main storage item is not null
	bool IsValid() {
		return (storageItem != nullptr);
	}

	// Delete item
	bool Delete() {
		bool state = ExecuteTask(storageItem->DeleteAsync());
		if (state) {
			storageItem = nullptr;
		}
		return state;
	}

	// Rename item
	bool Rename(std::string name) {
		auto path = PathUWP(name);
		if (path.IsAbsolute()) {
			name = path.GetFilename();
		}
		return ExecuteTask(storageItem->RenameAsync(convert(name)));
	}

	// Copy item
	bool Copy(StorageFolder^ folder) {
		bool state = false;
		if (IsDirectory()) {
			state = storageFolderW.Copy(folder);
		}
		else {
			state = storageFileW.Copy(folder, storageFileW.GetName());
		}
		return state;
	}


	// Copy item
	bool Copy(StorageItemW folder) {
		return Copy(folder.GetStorageFolder());
	}

	bool Copy(StorageFolder^ folder, std::string name) {
		bool state = false;
		if (IsDirectory()) {
			state = storageFolderW.Copy(folder);
		}
		else {
			state = storageFileW.Copy(folder, name);
		}
		return state;
	}

	bool Copy(StorageItemW folder, std::string name) {
		return Copy(folder.GetStorageFolder(), name);
	}

	// Move item
	bool Move(StorageFolder^ folder) {
		bool state = false;
		if (IsDirectory()) {
			state = storageFolderW.Copy(folder, true);
		}
		else {
			state = storageFileW.Move(folder, storageFileW.GetName());
		}
		return state;
	}

	bool Move(StorageItemW folder) {
		return Move(folder.GetStorageFolder());
	}

	// Move item
	bool Move(StorageFolder^ folder, std::string name) {
		bool state = false;
		if (IsDirectory()) {
			state = storageFolderW.Copy(folder, true);
		}
		else {
			state = storageFileW.Move(folder, name);
		}
		return state;
	}

	bool Move(StorageItemW folder, std::string name) {
		return Move(folder.GetStorageFolder(), name);
	}

	// Get item path
	std::string GetPath() {
		return convert(storageItem->Path);
	}

	// Get item name
	std::string GetName() {
		return convert(storageItem->Name);
	}

	// Compare item with std::string
	bool Equal(std::string path) {
		std::string itemPath = GetPath();

		// Fix slashs back from '/' to '\'
		windowsPath(path);
		return iequals(itemPath, path);
	}

	// Compare item with Platform::String
	bool Equal(Platform::String^ path) {
		return storageItem->Path->Equals(path);
	}

	// Compare item with Path
	bool Equal(PathUWP path) {
		return Equal(path.ToString());
	}
	
	// Compare item with StorageItem
	bool Equal(IStorageItem^ item) {
		return Equal(item->Path);
	}

	// Get item size
	__int64 GetSize(bool updateCache = false) {
		if (itemSize == 0 || updateCache) {
			if (IsDirectory()) {
				itemSize = storageFolderW.GetSize(updateCache);
			}
			else {
				itemSize = storageFileW.GetSize(updateCache);
			}
		}
		return itemSize;
	}

	// Get item handle
	HRESULT GetHandle(HANDLE* handle, int accessMode = GENERIC_READ, int shareMode = FILE_SHARE_READ) {
		HRESULT hr = E_FAIL;
		if(IsDirectory()){
			hr = GetFolderHandle(storageFolderW.GetStorageFolder(), handle, GetAccessMode(accessMode), GetShareMode(shareMode));
		}
		else {
			hr = GetFileHandle(storageFileW.GetStorageFile(), handle, GetAccessMode(accessMode), GetShareMode(shareMode));
		}
		return hr;
	}

	// Get file stream
	FILE* GetStream(const char* mode) {
		FILE* file{};
		if (!IsDirectory()) {
			file = storageFileW.GetStream(mode);
		}
		return file;
	}

	// Get files stream from folder
	FILE* GetFileStream(std::string name, const char* mode) {
		FILE* file{};
		if (IsDirectory()) {
			file = storageFolderW.GetFileStream(name, mode);
		}
		return file;
	}

	// Get item handle from stream
	HANDLE GetHandleFromStream(FILE* file) {
		return (HANDLE)_get_osfhandle(_fileno(file));
	}


	// Create or open folder if exists
	StorageFolderW CreateFolder(PathUWP path) {
		return StorageFolderW(storageFolderW.GetOrCreateFolder(path));
	}

	// Create or open file if exists
	StorageFileW CreateFile(PathUWP path) {
		return StorageFileW(storageFolderW.GetOrCreateFile(path));
	}

	// Create new folder
	bool CreateFolder(std::string name, bool replaceExisting = true) {
		return storageFolderW.CreateFolder(name, replaceExisting);
	}

	// Create new file
	bool CreateFile(std::string name, bool replaceExisting = true) {
		return storageFolderW.CreateFile(name, replaceExisting);
	}

	// Check if folder contains item by name or path
	bool Contains(PathUWP path, IStorageItem^& storageItem) {
		return storageFolderW.Contains(path, storageItem);
	}

	// Get all sub folders (deep scan)
	std::list<StorageFolderW> GetAllFolders(bool useWindowsIndexer = false) {
		return storageFolderW.GetAllFolders(useWindowsIndexer);
	}

	// Get all files including files in sub folders (deep scan)
	std::list<StorageFileW> GetAllFiles(bool useWindowsIndexer = false) {
		return storageFolderW.GetAllFiles(useWindowsIndexer);
	}

	std::list<StorageFolderW> GetFolders() {
		return storageFolderW.GetFolders();
	}

	std::list<StorageFileW> GetFiles() {
		return storageFolderW.GetFiles();
	}

	// Get item properties
	FILE_BASIC_INFO* GetProperties() {
		if (IsDirectory()) {
			return storageFolderW.GetProperties();
		}
		else {
			return storageFileW.GetProperties();
		}
	}

	// Get main storage item
	IStorageItem^ GetStorageItem() {
		return storageItem;
	}

	// Get Wrapped StorageFolder
	StorageFolderW GetStorageFolderW() {
		return storageFolderW;
	}
	
	// Get Wrapped StorageFolder
	StorageFolder^ GetStorageFolder() {
		return storageFolderW.GetStorageFolder();
	}

	// Get Wrapped StorageFile
	StorageFileW GetStorageFileW() {
		return storageFileW;
	}

	// Get StorageFile^
	StorageFile^ GetStorageFile() {
		return storageFileW.GetStorageFile();
	}

	ItemInfoUWP GetItemInfo() {
		ItemInfoUWP info;
		if (IsDirectory()) {
			info = storageFolderW.GetFolderInfo();
		}
		else {
			info = storageFileW.GetFileInfo();
		}
		return info;
	}

private:
	IStorageItem^ storageItem;
	StorageFileW storageFileW;
	StorageFolderW storageFolderW;

	__int64 itemSize = 0;
	bool isDirectory = false;
};

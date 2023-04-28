// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

// Functions:
// IsValid()
// Delete()
// Rename(std::string name)
// Copy(StorageFolder^ folder)
// Move(StorageFolder^ folder)
// GetPath()
// GetName()
// Equal(std::string path)
// Equal(Path path)
// Equal(Platform::String^ path)
// Equal(StorageFile^ file)
// GetProperties()
// GetSize(bool updateCache)
// GetHandle(HANDLE* handle, HANDLE_ACCESS_OPTIONS access)
// GetStream(const char* mode)
// GetHandle(FILE* file)
// GetStorageFile()

#pragma once 

#include "pch.h"
#include <io.h>
#include <fcntl.h>

#include "Common/Log.h"

#include "StorageLog.h"
#include "StoragePath.h"
#include "StorageExtensions.h"
#include "StorageHandler.h"
#include "StorageAsync.h"
#include "StorageInfo.h"

using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Windows::Storage::FileProperties;

class StorageFileW {
public:
	StorageFileW() {
	}

	StorageFileW(StorageFile^ file) {
		storageFile = file;
		fileSize = 0;
	}
	StorageFileW(IStorageItem^ file) {
		storageFile = (StorageFile^)file;
		fileSize = 0;
	}
	~StorageFileW() {
		delete storageFile;
		delete properties;
	}

	// Detect if main storage file is not null
	bool IsValid() {
		return (storageFile != nullptr);
	}

	// Delete file
	bool Delete() {
		bool state = ExecuteTask(storageFile->DeleteAsync());
		if (state) {
			storageFile = nullptr;
		}
		return state;
	}

	// Rename file
	bool Rename(std::string name) {
		auto path = PathUWP(name);
		if (path.IsAbsolute()) {
			name = path.GetFilename();
		}
		return ExecuteTask(storageFile->RenameAsync(convert(name)));
	}

	// Copy file
	bool Copy(StorageFolder^ folder, std::string name) {
		bool state = false;
		StorageFile^ newFile;
		ExecuteTask(newFile, storageFile->CopyAsync(folder, convert(name), NameCollisionOption::ReplaceExisting));
		if (newFile != nullptr) {
			state = true;
		}
		return state;
	}

	// Move file
	bool Move(StorageFolder^ folder, std::string name) {
		bool state = false;
		IStorageItem^ newFile;
		state = ExecuteTask(storageFile->MoveAsync(folder, convert(name), NameCollisionOption::GenerateUniqueName));
		if (state) {
			ExecuteTask(newFile, folder->TryGetItemAsync(storageFile->Name));
			if (newFile != nullptr) {
				storageFile = (StorageFile^)newFile;
			}
			else {
				state = false;
			}
		}
		return state;
	}

	// Get file path
	std::string GetPath() {
		return convert(storageFile->Path);
	}

	// Get file name
	std::string GetName() {
		return convert(storageFile->Name);
	}

	// Compare file with std::string
	bool Equal(std::string path) {
		std::string filePath = GetPath();

		// Fix slashs back from '/' to '\'
		windowsPath(path);
		return iequals(filePath, path);
	}

	// Compare file with Platform::String
	bool Equal(Platform::String^ path) {
		return storageFile->Path->Equals(path);
	}

	// Compare file with Path
	bool Equal(PathUWP path) {
		return Equal(path.ToString());
	}
	
	// Compare file with StorageFile
	bool Equal(StorageFile^ file) {
		return Equal(file->Path);
	}

	// Get file size
	__int64 GetSize(bool updateCache = false) {
		if (fileSize == 0 || updateCache) {
			// Let's try getting size by handle first
			HANDLE handle;
			HRESULT hr = GetHandle(&handle);
			if (handle == INVALID_HANDLE_VALUE || hr != S_OK) {
				// We have no other option, fallback to UWP
				fileSize = FetchProperties()->Size;
			}
			else {
				LARGE_INTEGER size{ 0 };
				if (FALSE == GetFileSizeEx(handle, &size)) {
					LARGE_INTEGER end_offset;
					const LARGE_INTEGER zero{};
					if (SetFilePointerEx(handle, zero, &end_offset, FILE_END) == 0) {
						CloseHandle(handle);
					}
					else {
						fileSize = end_offset.QuadPart;
						SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN);
						CloseHandle(handle);
					}
				}
				else {
					fileSize = size.QuadPart;
					CloseHandle(handle);
				}
			}
		}
		return fileSize;
	}

	// Get file handle
	HRESULT GetHandle(HANDLE* handle, int accessMode = GENERIC_READ, int shareMode = FILE_SHARE_READ) {
		return GetFileHandle(storageFile, handle, GetAccessMode(accessMode), GetShareMode(shareMode));
	}

	// Get file stream
	FILE* GetStream(const char* mode) {
		HANDLE handle;
		auto access = GENERIC_READ;
		auto share = FILE_SHARE_READ;
		bool isWrite = isWriteMode(mode);
		if (isWrite) {
			access = GENERIC_WRITE;
			share = FILE_SHARE_WRITE;
		}
		HRESULT hr = GetHandle(&handle, access, share);

		FILE* file{};
		if (hr == S_OK && handle != INVALID_HANDLE_VALUE) {
			int flags = _O_RDONLY;
			if (isWrite) {
				flags = _O_RDWR;
			}
			DEBUG_LOG(FILESYS, "Opening file (%s) with flag:%d mode:%s", GetPath().c_str(), flags, mode);
			file = _fdopen(_open_osfhandle((intptr_t)handle, flags), mode);
		}
		return file;
	}

	// Get file handle from stream
	HANDLE GetHandle(FILE* file) {
		return (HANDLE)_get_osfhandle(_fileno(file));
	}

	// Get file properties
	FILE_BASIC_INFO* GetProperties() {
		HANDLE handle;
		HRESULT hr = GetHandle(&handle);

		size_t size = sizeof(FILE_BASIC_INFO);
		FILE_BASIC_INFO* information = (FILE_BASIC_INFO*)(malloc(size));
		if(hr == S_OK && handle != INVALID_HANDLE_VALUE && information){
			information->FileAttributes = (DWORD)storageFile->Attributes;

			if (FALSE == GetFileInformationByHandleEx(handle, FileBasicInfo, information, (DWORD)size)) {
				// Fallback to UWP method (Slow)
				auto props = FetchProperties();
				information->ChangeTime.QuadPart = props->DateModified.UniversalTime;
				information->CreationTime.QuadPart = props->ItemDate.UniversalTime;
				information->LastAccessTime.QuadPart = props->DateModified.UniversalTime;
				information->LastWriteTime.QuadPart = props->DateModified.UniversalTime;
			}
			CloseHandle(handle);
		}

		return information;
	}

	// Get main storage file
	StorageFile^ GetStorageFile() {
		return storageFile;
	}

    time_t  filetime_to_timet(LARGE_INTEGER ull) const {
		return ull.QuadPart / 10000000ULL - 11644473600ULL;
	}
	ItemInfoUWP GetFileInfo() {
		ItemInfoUWP info;
		info.name = GetName();
		info.fullName = GetPath();
		info.isDirectory = false;

		auto sProperties = GetProperties();

		info.size = (uint64_t)GetSize();
		info.lastAccessTime = (uint64_t)filetime_to_timet(sProperties->LastAccessTime);
		info.lastWriteTime = (uint64_t)filetime_to_timet(sProperties->LastWriteTime);
		info.changeTime = (uint64_t)filetime_to_timet(sProperties->ChangeTime);
		info.creationTime = (uint64_t)filetime_to_timet(sProperties->CreationTime);


		info.attributes = sProperties->FileAttributes;

		return info;
	}

private:
	StorageFile^ storageFile;
	BasicProperties^ properties;
	__int64 fileSize = 0;

	BasicProperties^ FetchProperties() {
		if (properties == nullptr) {
			// Very bad and slow way in UWP to get size and other properties
			// not preferred to be used on big list of files
			ExecuteTask(properties, storageFile->GetBasicPropertiesAsync());
		}
		return properties;
	}
};

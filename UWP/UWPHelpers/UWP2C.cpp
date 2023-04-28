// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

// This is small bridge to invoke UWP Storage manager

// Functions:
// CreateFileUWP(const char* path, int accessMode, int shareMode, int openMode)
// GetFileAttributesUWP(const void* name, void* lpFileInformation)
// DeleteFileUWP(const void* name)

#include "UWP2C.h"
#include "StorageManager.h"
#include "StorageExtensions.h"

#ifdef __cplusplus
extern "C" {
#endif
void* CreateFileUWP(const char* path, int accessMode, int shareMode, int openMode) {
	std::string fn = convert(path);
	
	return (void*)CreateFileUWP(fn, accessMode, shareMode, openMode);
}

int GetFileAttributesUWP(const void* name, void* lpFileInformation) {
	size_t size = sizeof(WIN32_FILE_ATTRIBUTE_DATA);
	WIN32_FILE_ATTRIBUTE_DATA* file_attributes = (WIN32_FILE_ATTRIBUTE_DATA*)(malloc(size));
	
	std::string fn = convert((const char*)name);
	HANDLE handle = CreateFileUWP(fn);

	FILETIME createTime{};
	FILETIME changeTime{};
	
	DWORD fileSizeHigh = 0;
	DWORD fileSizeLow = 0;
	DWORD fileAttributes = 32;

	if (handle != INVALID_HANDLE_VALUE) {
		size_t size = sizeof(FILE_BASIC_INFO);
		FILE_BASIC_INFO* information = (FILE_BASIC_INFO*)(malloc(size));
		if (information) {
			if (FALSE != GetFileInformationByHandleEx(handle, FileBasicInfo, information, (DWORD)size)) {
				createTime.dwHighDateTime = information->CreationTime.HighPart;
				createTime.dwLowDateTime = information->CreationTime.LowPart;
				changeTime.dwHighDateTime = information->ChangeTime.HighPart;
				changeTime.dwLowDateTime = information->ChangeTime.LowPart;
				fileAttributes = information->FileAttributes;
			}
		}
		LARGE_INTEGER fsize{ 0 };
		if (FALSE == GetFileSizeEx(handle, &fsize)) {
			LARGE_INTEGER end_offset;
			const LARGE_INTEGER zero{};
			if (SetFilePointerEx(handle, zero, &end_offset, FILE_END) == 0) {
				CloseHandle(handle);
			}
			else {
				fileSizeHigh = (DWORD)end_offset.HighPart;
				fileSizeLow = (DWORD)end_offset.LowPart;
				SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN);
				CloseHandle(handle);
			}
		}
		else {
			fileSizeHigh = (DWORD)fsize.HighPart;
			fileSizeLow = (DWORD)fsize.LowPart;

			CloseHandle(handle);
		}
	}

	((WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation)->ftCreationTime = createTime;
	((WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation)->ftLastAccessTime = changeTime;
	((WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation)->ftLastWriteTime = changeTime;
	((WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation)->nFileSizeHigh = fileSizeHigh;
	((WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation)->nFileSizeLow = fileSizeLow;
	((WIN32_FILE_ATTRIBUTE_DATA*)lpFileInformation)->dwFileAttributes = fileAttributes;

	return 1;
}

int DeleteFileUWP(const void* name) {
	std::string fn = convert((const char*)name);
	bool state = DeleteUWP(fn);

	return state ? 1 : 0;
}
#ifdef __cplusplus
}
#endif

// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan

// Functions:
// GetFileHandle(StorageFile^ file, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode)
// GetFileHandleFromFolder(StorageFolder^ folder, std::string filename, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode, HANDLE_CREATION_OPTIONS openMode)
// GetFolderHandle(StorageFolder^ folder, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode)
// 
// GetAccessMode(int accessMode)
// GetShareMode(int shareMode)
// GetOpenMode(int openMode)

#include "StorageHandler.h"
#include "StorageExtensions.h"

using namespace Windows::Storage;

HRESULT GetFileHandle(StorageFile^ file, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode)
{
	if (file != nullptr) {
		Microsoft::WRL::ComPtr<IUnknown> abiPointer(reinterpret_cast<IUnknown*>(file));
		Microsoft::WRL::ComPtr<IStorageItemHandleAccess> handleAccess;
		if (SUCCEEDED(abiPointer.As(&handleAccess)))
		{
			HANDLE hFile = INVALID_HANDLE_VALUE;

			if (SUCCEEDED(handleAccess->Create(accessMode,
				shareMode,
				HO_NONE,
				nullptr,
				&hFile)))
			{
				*handle = hFile;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT GetFileHandleFromFolder(StorageFolder^ folder, std::string filename, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode, HANDLE_CREATION_OPTIONS openMode)
{
	if (folder != nullptr) {
		Microsoft::WRL::ComPtr<IUnknown> abiPointer(reinterpret_cast<IUnknown*>(folder));
		Microsoft::WRL::ComPtr<IStorageFolderHandleAccess> handleAccess;
		if (SUCCEEDED(abiPointer.As(&handleAccess)))
		{
			HANDLE hFolder = INVALID_HANDLE_VALUE;
			auto fn = convertToLPCWSTR(filename);
			if (SUCCEEDED(handleAccess->Create(fn,
				openMode,
				accessMode,
				shareMode,
				HO_NONE,
				nullptr,
				&hFolder)))
			{
				*handle = hFolder;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HRESULT GetFolderHandle(StorageFolder^ folder, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode)
{
	if (folder != nullptr) {
		Microsoft::WRL::ComPtr<IUnknown> abiPointer(reinterpret_cast<IUnknown*>(folder));
		Microsoft::WRL::ComPtr<IStorageItemHandleAccess> handleAccess;
		if (SUCCEEDED(abiPointer.As(&handleAccess)))
		{
			HANDLE hFolder = INVALID_HANDLE_VALUE;

			if (SUCCEEDED(handleAccess->Create(accessMode,
				shareMode,
				HO_NONE,
				nullptr,
				&hFolder)))
			{
				*handle = hFolder;
				return S_OK;
			}
		}
	}
	return E_FAIL;
}

HANDLE_ACCESS_OPTIONS GetAccessMode(int accessMode) {
	switch (accessMode) {
	case GENERIC_READ:
		return HAO_READ | HAO_READ_ATTRIBUTES;
	case GENERIC_WRITE:
		return HAO_WRITE | HAO_READ;
	case GENERIC_ALL:
		return HAO_READ | HAO_READ_ATTRIBUTES | HAO_WRITE | HAO_DELETE;
	default:
		return HAO_READ;
	}
}

HANDLE_SHARING_OPTIONS GetShareMode(int shareMode) {
	switch (shareMode)
	{
	case FILE_SHARE_READ:
		return HSO_SHARE_READ;
	case FILE_SHARE_WRITE:
		return HSO_SHARE_READ | HSO_SHARE_WRITE;
	case FILE_SHARE_DELETE:
		return HSO_SHARE_DELETE;
	default:
		return HSO_SHARE_READ;
	}
}

HANDLE_CREATION_OPTIONS GetOpenMode(int openMode) {
	switch (openMode)
	{
	case CREATE_NEW:
		return HCO_CREATE_NEW;
	case CREATE_ALWAYS:
		return HCO_CREATE_ALWAYS;
	case OPEN_ALWAYS:
		return HCO_OPEN_ALWAYS;
	case OPEN_EXISTING:
		return HCO_OPEN_EXISTING;
	default:
		return HCO_OPEN_EXISTING;
	}
}

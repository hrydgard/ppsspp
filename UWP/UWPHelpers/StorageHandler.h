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

#pragma once

#include "pch.h"
#include <ppl.h>
#include <ppltasks.h>
#include <wrl.h>
#include <wrl/implements.h>

using namespace Windows::Storage;

#pragma region WindowsStorageCOM
// These APIs have been accidentally placed inside the WINAPI_PARTITION_DESKTOP partition
// (they're not desktop-specific; they're available to UWPs).
// This will be addressed in future SDK updates.
// These are copied from WindowsStorageCOM.h
// You can remove this region once the real file has been updated
// to fix the WINAPI_PARTITION_DESKTOP block
// Source: https://stackoverflow.com/questions/42799235/how-can-i-get-a-win32-handle-for-a-storagefile-or-storagefolder-in-uwp

typedef interface IOplockBreakingHandler IOplockBreakingHandler;
typedef interface IStorageItemHandleAccess IStorageItemHandleAccess;
typedef interface IStorageFolderHandleAccess IStorageFolderHandleAccess;

#ifdef __cplusplus
extern "C" {
#endif 

	typedef
		enum HANDLE_OPTIONS
	{
		HO_NONE = 0,
		HO_OPEN_REQUIRING_OPLOCK = 0x40000,
		HO_DELETE_ON_CLOSE = 0x4000000,
		HO_SEQUENTIAL_SCAN = 0x8000000,
		HO_RANDOM_ACCESS = 0x10000000,
		HO_NO_BUFFERING = 0x20000000,
		HO_OVERLAPPED = 0x40000000,
		HO_WRITE_THROUGH = 0x80000000
	}     HANDLE_OPTIONS;

	DEFINE_ENUM_FLAG_OPERATORS(HANDLE_OPTIONS);
	typedef
		enum HANDLE_ACCESS_OPTIONS
	{
		HAO_NONE = 0,
		HAO_READ_ATTRIBUTES = 0x80,
		HAO_READ = 0x120089,
		HAO_WRITE = 0x120116,
		HAO_DELETE = 0x10000
	}     HANDLE_ACCESS_OPTIONS;

	DEFINE_ENUM_FLAG_OPERATORS(HANDLE_ACCESS_OPTIONS);
	typedef
		enum HANDLE_SHARING_OPTIONS
	{
		HSO_SHARE_NONE = 0,
		HSO_SHARE_READ = 0x1,
		HSO_SHARE_WRITE = 0x2,
		HSO_SHARE_DELETE = 0x4
	}     HANDLE_SHARING_OPTIONS;

	DEFINE_ENUM_FLAG_OPERATORS(HANDLE_SHARING_OPTIONS);
	typedef
		enum HANDLE_CREATION_OPTIONS
	{
		HCO_CREATE_NEW = 0x1,
		HCO_CREATE_ALWAYS = 0x2,
		HCO_OPEN_EXISTING = 0x3,
		HCO_OPEN_ALWAYS = 0x4,
		HCO_TRUNCATE_EXISTING = 0x5
	}     HANDLE_CREATION_OPTIONS;


	EXTERN_C const IID IID_IOplockBreakingHandler;
	MIDL_INTERFACE("826ABE3D-3ACD-47D3-84F2-88AAEDCF6304")
		IOplockBreakingHandler : public IUnknown
	{
	public:
		virtual HRESULT STDMETHODCALLTYPE OplockBreaking(void) = 0;

	};

	EXTERN_C const IID IID_IStorageItemHandleAccess;
	MIDL_INTERFACE("5CA296B2-2C25-4D22-B785-B885C8201E6A")
		IStorageItemHandleAccess : public IUnknown
	{
	public:
		virtual HRESULT STDMETHODCALLTYPE Create(
			HANDLE_ACCESS_OPTIONS accessOptions,
			HANDLE_SHARING_OPTIONS sharingOptions,
			HANDLE_OPTIONS options,
			__RPC__in_opt IOplockBreakingHandler * oplockBreakingHandler,
			__RPC__deref_out_opt HANDLE * interopHandle) = 0;

	};

	EXTERN_C const IID IID_IStorageFolderHandleAccess;
	MIDL_INTERFACE("DF19938F-5462-48A0-BE65-D2A3271A08D6")
		IStorageFolderHandleAccess : public IUnknown
	{
	public:
		virtual HRESULT STDMETHODCALLTYPE Create(
			__RPC__in_string LPCWSTR fileName,
			HANDLE_CREATION_OPTIONS creationOptions,
		    HANDLE_ACCESS_OPTIONS accessOptions,
			HANDLE_SHARING_OPTIONS sharingOptions,
			HANDLE_OPTIONS options,
			__RPC__in_opt IOplockBreakingHandler * oplockBreakingHandler,
			__RPC__deref_out_opt HANDLE * interopHandle) = 0;

	};
#ifdef __cplusplus
}
#endif
#pragma endregion

HRESULT GetFileHandle(StorageFile^ file, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode);
HRESULT GetFileHandleFromFolder(StorageFolder^ folder, std::string filename, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode, HANDLE_CREATION_OPTIONS openMode);
HRESULT GetFolderHandle(StorageFolder^ folder, HANDLE* handle, HANDLE_ACCESS_OPTIONS accessMode, HANDLE_SHARING_OPTIONS shareMode);

HANDLE_ACCESS_OPTIONS GetAccessMode(int accessMode);
HANDLE_SHARING_OPTIONS GetShareMode(int shareMode);
HANDLE_CREATION_OPTIONS GetOpenMode(int openMode);

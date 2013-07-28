// Copyright (c) 2012- PPSSPP Project.

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

// TODO: Remove the Windows-specific code, FILE is fine there too.

#include <map>

#include "Core/FileSystems/FileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"

class VirtualDiscFileSystem: public IFileSystem {
public:
	VirtualDiscFileSystem(IHandleAllocator *_hAlloc, std::string _basePath);
	~VirtualDiscFileSystem();

	void DoState(PointerWrap &p);
	u32      OpenFile(std::string filename, FileAccess access, const char *devicename=NULL);
	size_t   SeekFile(u32 handle, s32 position, FileMove type);
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size);
	void     CloseFile(u32 handle);
	PSPFileInfo GetFileInfo(std::string filename);
	bool     OwnsHandle(u32 handle);
	bool GetHostPath(const std::string &inpath, std::string &outpath);
	std::vector<PSPFileInfo> GetDirListing(std::string path);

	// unsupported operations
	size_t  WriteFile(u32 handle, const u8 *pointer, s64 size);
	bool MkDir(const std::string &dirname);
	bool RmDir(const std::string &dirname);
	int  RenameFile(const std::string &from, const std::string &to);
	bool RemoveFile(const std::string &filename);

private:
	void LoadFileListIndex();
	int getFileListIndex(std::string& fileName);
	int getFileListIndex(u32 accessBlock, u32 accessSize, bool blockMode = false);
	std::string GetLocalPath(std::string localpath);

	typedef void *HandlerLibrary;
	typedef int HandlerHandle;
	typedef s64 HandlerOffset;
	typedef void (*HandlerLogFunc)(HandlerHandle fd, int level, const char *msg);

	// The primary purpose of handlers is to make it easier to work with large archives.
	// However, they have other uses as well, such as patching individual files.
	struct Handler {
		Handler(const char *filename);
		~Handler();

		HandlerLibrary library;
		bool (*Init)(HandlerLogFunc logger);
		void (*Shutdown)();
		HandlerHandle (*Open)(const char *basePath, const char *filename);
		HandlerOffset (*Seek)(HandlerHandle handle, HandlerOffset offset, FileMove origin);
		HandlerOffset (*Read)(HandlerHandle handle, void *data, HandlerOffset size);
		void (*Close)(HandlerHandle handle);
	};

	struct HandlerFileHandle {
		Handler *handler;
		HandlerHandle handle;

		bool Open(std::string& basePath, std::string& fileName, FileAccess access) {
			// Ignore access, read only.
			handle = handler->Open(basePath.c_str(), fileName.c_str());
			return handle > 0;
		}
		size_t Read(u8 *data, s64 size) {
			return (size_t)handler->Read(handle, data, size);
		}
		size_t Seek(s32 position, FileMove type) {
			return (size_t)handler->Seek(handle, position, type);
		}
		void Close() {
			handler->Close(handle);
		}
	};

	typedef enum { VFILETYPE_NORMAL, VFILETYPE_LBN, VFILETYPE_ISO } VirtualFileType;

	struct OpenFileEntry {
		DirectoryFileHandle hFile;
		HandlerFileHandle handler;
		VirtualFileType type;
		u32 fileIndex;
		u32 curOffset;
		u32 startOffset;	// only used by lbn files
		u32 size;			// only used by lbn files
	};

	typedef std::map<u32, OpenFileEntry> EntryMap;
	EntryMap entries;
	IHandleAllocator *hAlloc;
	std::string basePath;

	typedef struct {
		std::string fileName;
		u32 firstBlock;
		u32 totalSize;
	} FileListEntry;

	std::vector<FileListEntry> fileList;
	u32 currentBlockIndex;

	std::map<std::string, Handler> handlers;
};

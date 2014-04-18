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
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec);
	int      DevType(u32 handle);
	bool GetHostPath(const std::string &inpath, std::string &outpath);
	std::vector<PSPFileInfo> GetDirListing(std::string path);
	int  Flags() { return 0; }

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
	typedef void (*HandlerLogFunc)(void *arg, HandlerHandle handle, LogTypes::LOG_LEVELS level, const char *msg);

	static void HandlerLogger(void *arg, HandlerHandle handle, LogTypes::LOG_LEVELS level, const char *msg);

	// The primary purpose of handlers is to make it easier to work with large archives.
	// However, they have other uses as well, such as patching individual files.
	struct Handler {
		Handler(const char *filename, VirtualDiscFileSystem *const sys);
		~Handler();

		typedef bool (*InitFunc)(HandlerLogFunc logger, void *loggerArg);
		typedef void (*ShutdownFunc)();
		typedef HandlerHandle (*OpenFunc)(const char *basePath, const char *filename);
		typedef HandlerOffset (*SeekFunc)(HandlerHandle handle, HandlerOffset offset, FileMove origin);
		typedef HandlerOffset (*ReadFunc)(HandlerHandle handle, void *data, HandlerOffset size);
		typedef void (*CloseFunc)(HandlerHandle handle);

		HandlerLibrary library;
		InitFunc Init;
		ShutdownFunc Shutdown;
		OpenFunc Open;
		SeekFunc Seek;
		ReadFunc Read;
		CloseFunc Close;

		bool IsValid() { return library != NULL; }
	};

	struct HandlerFileHandle {
		Handler *handler;
		HandlerHandle handle;

		HandlerFileHandle() : handler(NULL), handle(0) {
		}
		HandlerFileHandle(Handler *handler_) : handler(handler_), handle(-1) {
		}

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

		bool IsValid() {
			return handler != NULL && handler->IsValid();
		}

		HandlerFileHandle &operator =(Handler *_handler) {
			handler = _handler;
			return *this;
		}
	};

	typedef enum { VFILETYPE_NORMAL, VFILETYPE_LBN, VFILETYPE_ISO } VirtualFileType;

	struct OpenFileEntry {
		DirectoryFileHandle hFile;
		HandlerFileHandle handler;
		VirtualFileType type;
		u32 fileIndex;
		u64 curOffset;
		u64 startOffset;	// only used by lbn files
		u64 size;			// only used by lbn files

		bool Open(std::string& basePath, std::string& fileName, FileAccess access) {
			if (handler.IsValid()) {
				return handler.Open(basePath, fileName, access);
			} else {
				return hFile.Open(basePath, fileName, access);
			}
		}
		size_t Read(u8 *data, s64 size) {
			if (handler.IsValid()) {
				return handler.Read(data, size);
			} else {
				return hFile.Read(data, size);
			}
		}
		size_t Seek(s32 position, FileMove type) {
			if (handler.IsValid()) {
				return handler.Seek(position, type);
			} else {
				return hFile.Seek(position, type);
			}
		}
		void Close() {
			if (handler.IsValid()) {
				return handler.Close();
			} else {
				return hFile.Close();
			}
		}
	};

	typedef std::map<u32, OpenFileEntry> EntryMap;
	EntryMap entries;
	IHandleAllocator *hAlloc;
	std::string basePath;

	struct FileListEntry {
		std::string fileName;
		u32 firstBlock;
		u32 totalSize;
		Handler *handler;
	};

	std::vector<FileListEntry> fileList;
	u32 currentBlockIndex;

	std::map<std::string, Handler *> handlers;
};

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

#include "Common/File/Path.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"

extern const std::string INDEX_FILENAME;

class VirtualDiscFileSystem: public IFileSystem {
public:
	VirtualDiscFileSystem(IHandleAllocator *_hAlloc, const Path &_basePath);
	~VirtualDiscFileSystem();

	void DoState(PointerWrap &p) override;
	int      OpenFile(std::string filename, FileAccess access, const char *devicename = nullptr) override;
	size_t   SeekFile(u32 handle, s32 position, FileMove type) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size) override;
	size_t   ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) override;
	void     CloseFile(u32 handle) override;
	PSPFileInfo GetFileInfo(std::string filename) override;
	PSPFileInfo GetFileInfoByHandle(u32 handle) override;
	bool     OwnsHandle(u32 handle) override;
	int      Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) override;
	PSPDevType DevType(u32 handle) override;
	std::vector<PSPFileInfo> GetDirListing(const std::string &path, bool *exists = nullptr) override;
	FileSystemFlags Flags() const override { return FileSystemFlags::UMD; }
	u64  FreeDiskSpace(const std::string &path) override { return 0; }

	// unsupported operations
	size_t  WriteFile(u32 handle, const u8 *pointer, s64 size) override;
	size_t  WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) override;
	bool MkDir(const std::string &dirname) override;
	bool RmDir(const std::string &dirname) override;
	int  RenameFile(const std::string &from, const std::string &to) override;
	bool RemoveFile(const std::string &filename) override;

	bool ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) override { return false; }

	void Describe(char *buf, size_t size) const override { snprintf(buf, size, "VirtualDisc: %s", basePath.ToVisualString().c_str()); }  // TODO: Ask the fileLoader about the origins

private:
	void LoadFileListIndex();
	// Warning: modifies input string.
	int getFileListIndex(std::string &fileName);
	int getFileListIndex(u32 accessBlock, u32 accessSize, bool blockMode = false) const;
	Path GetLocalPath(std::string_view localpath) const;

	typedef void *HandlerLibrary;
	typedef int HandlerHandle;
	typedef s64 HandlerOffset;
	typedef void (*HandlerLogFunc)(void *arg, HandlerHandle handle, LogLevel level, const char *msg);

	static void HandlerLogger(void *arg, HandlerHandle handle, LogLevel level, const char *msg);

	// The primary purpose of handlers is to make it easier to work with large archives.
	// However, they have other uses as well, such as patching individual files.
	struct Handler {
		Handler(const char *filename, VirtualDiscFileSystem *const sys);
		~Handler();

		typedef bool (*InitFunc)(HandlerLogFunc logger, void *loggerArg);
		typedef void (*ShutdownFunc)();
		typedef void (*ShutdownV2Func)(void *loggerArg);
		typedef HandlerHandle (*OpenFunc)(const char *basePath, const char *filename);
		typedef HandlerOffset (*SeekFunc)(HandlerHandle handle, HandlerOffset offset, FileMove origin);
		typedef HandlerOffset (*ReadFunc)(HandlerHandle handle, void *data, HandlerOffset size);
		typedef void (*CloseFunc)(HandlerHandle handle);
		typedef int (*VersionFunc)();

		HandlerLibrary library;
		VirtualDiscFileSystem *const sys_;
		InitFunc Init;
		ShutdownFunc Shutdown;
		ShutdownV2Func ShutdownV2;
		OpenFunc Open;
		SeekFunc Seek;
		ReadFunc Read;
		CloseFunc Close;

		bool IsValid() const { return library != nullptr; }
	};

	struct HandlerFileHandle {
		Handler *handler;
		HandlerHandle handle;

		HandlerFileHandle() : handler(nullptr), handle(0) {}
		HandlerFileHandle(Handler *handler_) : handler(handler_), handle(-1) {}

		bool Open(const std::string& basePath, const std::string& fileName, FileAccess access) {
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
			return handler != nullptr && handler->IsValid();
		}

		HandlerFileHandle &operator =(Handler *_handler) {
			handler = _handler;
			return *this;
		}
	};

	typedef enum { VFILETYPE_NORMAL, VFILETYPE_LBN, VFILETYPE_ISO } VirtualFileType;

	struct OpenFileEntry {
		OpenFileEntry() {}
		OpenFileEntry(FileSystemFlags fileSystemFlags) {
			hFile = DirectoryFileHandle(DirectoryFileHandle::SKIP_REPLAY, fileSystemFlags);
		}

		DirectoryFileHandle hFile;
		HandlerFileHandle handler;
		VirtualFileType type = VFILETYPE_NORMAL;
		u32 fileIndex = 0;
		u64 curOffset = 0;
		u64 startOffset = 0;	// only used by lbn files
		u64 size = 0;			// only used by lbn files

		bool Open(const Path &basePath, std::string& fileName, FileAccess access) {
			// Ignored, we're read only.
			u32 err;
			if (handler.IsValid()) {
				return handler.Open(basePath.ToString(), fileName, access);
			} else {
				return hFile.Open(basePath, fileName, access, err);
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
	Path basePath;

	struct FileListEntry {
		std::string fileName;
		u32 firstBlock;
		u32 totalSize;
		Handler *handler;
	};

	std::vector<FileListEntry> fileList;
	u32 currentBlockIndex;
	u32 lastReadBlock_;

	std::map<std::string, Handler *> handlers;
};

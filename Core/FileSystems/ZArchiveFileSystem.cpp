// Copyright (c) 2026- PPSSPP Project.

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

#include <algorithm>
#include <cstring>

#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/FileSystems/ZArchiveFileSystem.h"
#include "Core/System.h"

// Splits a guest path into its components, skipping empty ones and ".".
static std::vector<std::string_view> PathComponents(std::string_view path) {
	std::vector<std::string_view> components;
	size_t pos = 0;
	while (pos < path.size()) {
		while (pos < path.size() && (path[pos] == '/' || path[pos] == '\\'))
			pos++;
		size_t end = pos;
		while (end < path.size() && path[end] != '/' && path[end] != '\\')
			end++;
		std::string_view component = path.substr(pos, end - pos);
		if (!component.empty() && component != ".")
			components.push_back(component);
		pos = end;
	}
	return components;
}

ZArchiveFileSystem::ZArchiveFileSystem(IHandleAllocator *hAlloc, std::shared_ptr<ZArchive> archive, FileSystemFlags flags)
	: hAlloc_(hAlloc), archive_(std::move(archive)), flags_(flags) {
	if (!archive_->GetPath().IsLocalType() || !File::GetModifTime(archive_->GetPath(), time_)) {
		// 2000-01-01, rather than 1900.
		time_ = tm{};
		time_.tm_year = 100;
		time_.tm_mday = 1;
	}
}

ZArchiveFileSystem::~ZArchiveFileSystem() {
	std::lock_guard<std::mutex> guard(lock_);
	for (auto &iter : entries_) {
		hAlloc_->FreeHandle(iter.first);
	}
}

PSPFileInfo ZArchiveFileSystem::MakeInfo(std::string_view name, ZArchive::NodeHandle node) const {
	PSPFileInfo info;
	info.name = std::string(name);
	if (node == ZArchive::INVALID_NODE)
		return info;
	info.exists = true;
	if (archive_->IsDirectory(node)) {
		info.type = FILETYPE_DIRECTORY;
		info.access = 0777;
	} else {
		info.type = FILETYPE_NORMAL;
		info.size = (s64)archive_->GetFileSize(node);
		// Like a read-only file on the memory stick.
		info.access = 0444;
	}
	info.atime = time_;
	info.ctime = time_;
	info.mtime = time_;
	return info;
}

std::vector<PSPFileInfo> ZArchiveFileSystem::GetDirListing(std::string_view path, bool *exists) {
	std::vector<PSPFileInfo> listing;
	const ZArchive::NodeHandle node = archive_->LookUp(path);
	if (!archive_->IsDirectory(node)) {
		if (exists)
			*exists = false;
		return listing;
	}
	if (exists)
		*exists = true;

	// FAT has these, and DirectoryFileSystem lists them too.
	PSPFileInfo dot = MakeInfo(".", node);
	PSPFileInfo dotdot = MakeInfo("..", node);
	dotdot.size = 4096;
	listing.push_back(dot);
	listing.push_back(dotdot);

	for (const ZArchive::DirEntry &entry : archive_->ListDirectory(node)) {
		PSPFileInfo info = MakeInfo(entry.name, entry.node);
		if (flags_ & FileSystemFlags::SIMULATE_FAT32) {
			// Like the memory stick, see DirectoryFileSystem::GetDirListing.
			info.name = SimulateVFATBug(info.name);
		}
		listing.push_back(info);
	}
	return listing;
}

int ZArchiveFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	if (access & (FILEACCESS_WRITE | FILEACCESS_APPEND | FILEACCESS_CREATE | FILEACCESS_TRUNCATE)) {
		return SCE_KERNEL_ERROR_ERRNO_READ_ONLY;
	}
	const ZArchive::NodeHandle node = archive_->LookUp(filename);
	if (!archive_->IsFile(node)) {
		if (!(access & FILEACCESS_PPSSPP_QUIET)) {
			ERROR_LOG(Log::FileSystem, "ZArchiveFileSystem::OpenFile: '%s' not found", filename.c_str());
		}
		return SCE_KERNEL_ERROR_ERRNO_FILE_NOT_FOUND;
	}

	OpenFileEntry entry;
	entry.filename = filename;
	entry.node = node;
	entry.size = archive_->GetFileSize(node);

	std::lock_guard<std::mutex> guard(lock_);
	const u32 handle = hAlloc_->GetNewHandle();
	entries_[handle] = entry;
	return (int)handle;
}

void ZArchiveFileSystem::CloseFile(u32 handle) {
	std::lock_guard<std::mutex> guard(lock_);
	auto iter = entries_.find(handle);
	if (iter != entries_.end()) {
		hAlloc_->FreeHandle(handle);
		entries_.erase(iter);
	} else {
		ERROR_LOG(Log::FileSystem, "ZArchiveFileSystem: Cannot close file that hasn't been opened: %08x", handle);
	}
}

size_t ZArchiveFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int ignored;
	return ReadFile(handle, pointer, size, ignored);
}

size_t ZArchiveFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	ZArchive::NodeHandle node;
	u64 pos;
	u64 fileSize;
	{
		std::lock_guard<std::mutex> guard(lock_);
		auto iter = entries_.find(handle);
		if (iter == entries_.end()) {
			ERROR_LOG(Log::FileSystem, "ZArchiveFileSystem: Cannot read file that hasn't been opened: %08x", handle);
			return 0;
		}
		node = iter->second.node;
		pos = iter->second.seekPos;
		fileSize = iter->second.size;
	}
	if (size <= 0 || pos >= fileSize)
		return 0;

	const size_t toRead = (size_t)std::min<u64>((u64)size, fileSize - pos);
	const size_t bytesRead = archive_->ReadFile(node, pos, toRead, pointer);

	std::lock_guard<std::mutex> guard(lock_);
	auto iter = entries_.find(handle);
	if (iter != entries_.end()) {
		iter->second.seekPos = pos + bytesRead;
	}
	return bytesRead;
}

size_t ZArchiveFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	std::lock_guard<std::mutex> guard(lock_);
	auto iter = entries_.find(handle);
	if (iter == entries_.end()) {
		ERROR_LOG(Log::FileSystem, "ZArchiveFileSystem: Cannot seek in file that hasn't been opened: %08x", handle);
		return 0;
	}
	OpenFileEntry &entry = iter->second;
	s64 newPos;
	switch (type) {
	case FILEMOVE_BEGIN: newPos = position; break;
	case FILEMOVE_CURRENT: newPos = (s64)entry.seekPos + position; break;
	case FILEMOVE_END: newPos = (s64)entry.size + position; break;
	default: return (size_t)entry.seekPos;
	}
	// Seeking past the end is fine (reads just return nothing), before the start isn't.
	if (newPos >= 0) {
		entry.seekPos = (u64)newPos;
	}
	return (size_t)entry.seekPos;
}

PSPFileInfo ZArchiveFileSystem::GetFileInfo(std::string filename) {
	// Named like the path asked for, as DirectoryFileSystem does.
	return MakeInfo(filename, archive_->LookUp(filename));
}

PSPFileInfo ZArchiveFileSystem::GetFileInfoByHandle(u32 handle) {
	std::string filename;
	{
		std::lock_guard<std::mutex> guard(lock_);
		auto iter = entries_.find(handle);
		if (iter == entries_.end())
			return PSPFileInfo();
		filename = iter->second.filename;
	}
	return GetFileInfo(filename);
}

bool ZArchiveFileSystem::OwnsHandle(u32 handle) {
	std::lock_guard<std::mutex> guard(lock_);
	return entries_.find(handle) != entries_.end();
}

bool ZArchiveFileSystem::HasOpenFiles() {
	std::lock_guard<std::mutex> guard(lock_);
	return !entries_.empty();
}

bool ZArchiveFileSystem::ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) {
	const ZArchive::NodeHandle node = archive_->LookUp(path);
	if (!archive_->IsDirectory(node))
		return false;
	// It's all in memory, so it is fast. Iterative, the archive is untrusted.
	int64_t total = 0;
	std::vector<ZArchive::NodeHandle> pending{ node };
	while (!pending.empty()) {
		const ZArchive::NodeHandle dir = pending.back();
		pending.pop_back();
		for (const ZArchive::DirEntry &entry : archive_->ListDirectory(dir)) {
			if (entry.isDirectory) {
				pending.push_back(entry.node);
			} else {
				total += (int64_t)entry.size;
			}
		}
	}
	*size = total;
	return true;
}

void ZArchiveFileSystem::Describe(char *buf, size_t size) const {
	snprintf(buf, size, "ZArchive: %s", archive_->GetPath().c_str());
}

void ZArchiveFileSystem::DoState(PointerWrap &p) {
	std::lock_guard<std::mutex> guard(lock_);

	// Only written when there are files open, which is rare, so that savestates are usually the same as
	// without an archive, and work both ways. When reading, a missing section means no files were open.
	if (p.mode != PointerWrap::MODE_READ && entries_.empty()) {
		return;
	}
	auto absent = [&]() {
		for (auto &iter : entries_) {
			hAlloc_->FreeHandle(iter.first);
		}
		entries_.clear();
	};
	// Peek for the section's marker rather than letting Section() probe, which can fail the load if
	// the state ends here, and misreads some states (it takes a following int 1 for an old marker).
	static const char SECTION_TITLE[] = "ZArchiveFileSystem";
	if (p.mode == PointerWrap::MODE_READ) {
		char marker[16]{};
		strncpy(marker, SECTION_TITLE, sizeof(marker));
		if (p.Remaining() < sizeof(marker) + sizeof(int) || memcmp(*p.ptr, marker, sizeof(marker)) != 0) {
			absent();
			return;
		}
	}
	auto s = p.Section(SECTION_TITLE, 1, 1);
	if (!s) {
		if (p.mode == PointerWrap::MODE_READ) {
			absent();
		}
		return;
	}

	u32 num = (u32)entries_.size();
	Do(p, num);
	if (p.mode == PointerWrap::MODE_READ) {
		entries_.clear();
		for (u32 i = 0; i < num; i++) {
			u32 handle = 0;
			OpenFileEntry entry;
			Do(p, handle);
			Do(p, entry.filename);
			Do(p, entry.seekPos);
			entry.node = archive_->LookUp(entry.filename);
			if (!archive_->IsFile(entry.node)) {
				// A different archive than when the state was saved. Reads will fail, like for a
				// file deleted from the memory stick.
				ERROR_LOG(Log::FileSystem, "ZArchiveFileSystem: '%s' was open in the savestate, but isn't in the archive", entry.filename.c_str());
				entry.node = ZArchive::INVALID_NODE;
			}
			entry.size = archive_->GetFileSize(entry.node);
			entries_[handle] = entry;
		}
	} else {
		for (auto &iter : entries_) {
			u32 handle = iter.first;
			Do(p, handle);
			Do(p, iter.second.filename);
			Do(p, iter.second.seekPos);
		}
	}
}

MemStickOverlayFileSystem::MemStickOverlayFileSystem(std::shared_ptr<IFileSystem> memstick, std::shared_ptr<ZArchiveFileSystem> archive)
	: memstick_(std::move(memstick)), archive_(std::move(archive)) {
}

bool MemStickOverlayFileSystem::InOverlay(std::string_view path) {
	std::vector<std::string_view> components = PathComponents(path);
	if (components.empty()) {
		// The root, for listing the PSP directory.
		return true;
	}
	if (!equalsNoCase(components[0], "PSP"))
		return false;
	if (components.size() >= 2 && equalsNoCase(components[1], "SAVEDATA"))
		return false;
	return true;
}

PSPFileInfo MemStickOverlayFileSystem::ArchiveOnlyInfo(const std::string &path) {
	if (!InOverlay(path) || memstick_->GetFileInfo(path).exists)
		return PSPFileInfo();
	return archive_->GetFileInfo(path);
}

void MemStickOverlayFileSystem::CreateParentDirs(const std::string &path) {
	std::vector<std::string_view> components = PathComponents(path);
	std::string dir;
	for (size_t i = 0; i + 1 < components.size(); i++) {
		dir += "/";
		dir += components[i];
		if (!memstick_->GetFileInfo(dir).exists && archive_->GetFileInfo(dir).exists) {
			INFO_LOG(Log::FileSystem, "MemStickOverlay: Creating %s on the memory stick, it's in the archive", dir.c_str());
			memstick_->MkDir(dir);
		}
	}
}

bool MemStickOverlayFileSystem::CopyUp(const std::string &path) {
	INFO_LOG(Log::FileSystem, "MemStickOverlay: Copying %s from the archive to the memory stick, to modify it", path.c_str());
	CreateParentDirs(path);
	const int in = archive_->OpenFile(path, FILEACCESS_READ);
	if (in < 0)
		return false;
	const int out = memstick_->OpenFile(path, (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
	if (out < 0) {
		archive_->CloseFile(in);
		return false;
	}
	bool success = true;
	std::vector<u8> buffer(256 * 1024);
	while (true) {
		const size_t bytes = archive_->ReadFile(in, buffer.data(), buffer.size());
		if (bytes == 0)
			break;
		if (memstick_->WriteFile(out, buffer.data(), bytes) != bytes) {
			success = false;
			break;
		}
	}
	archive_->CloseFile(in);
	memstick_->CloseFile(out);
	if (!success) {
		ERROR_LOG(Log::FileSystem, "MemStickOverlay: Failed copying %s to the memory stick", path.c_str());
		memstick_->RemoveFile(path);
	}
	return success;
}

std::vector<PSPFileInfo> MemStickOverlayFileSystem::GetDirListing(std::string_view path, bool *exists) {
	bool memstickExists = false;
	std::vector<PSPFileInfo> listing = memstick_->GetDirListing(path, &memstickExists);
	bool archiveExists = false;
	std::vector<PSPFileInfo> archiveListing;
	if (InOverlay(path)) {
		archiveListing = archive_->GetDirListing(path, &archiveExists);
	}
	if (exists)
		*exists = memstickExists || archiveExists;

	const std::vector<std::string_view> components = PathComponents(path);
	const bool isRoot = components.empty();
	const bool isPSPDir = components.size() == 1 && equalsNoCase(components[0], "PSP");
	const bool hideFolders = PSP_CoreParameter().compat.flags().HideISOFiles;
	for (PSPFileInfo &info : archiveListing) {
		// In the root, the PSP directory is all we show of the archive. In there, not SAVEDATA.
		if (isRoot && !equalsNoCase(info.name, "PSP"))
			continue;
		if (isPSPDir && equalsNoCase(info.name, "SAVEDATA"))
			continue;
		if (hideFolders && isPSPDir) {
			// See DirectoryFileSystem::GetDirListing, some games choke on scanning these.
			continue;
		}
		bool found = false;
		for (const PSPFileInfo &existing : listing) {
			if (equalsNoCase(existing.name, info.name)) {
				found = true;
				break;
			}
		}
		if (!found) {
			listing.push_back(info);
		}
	}
	return listing;
}

int MemStickOverlayFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	if (!InOverlay(filename) || memstick_->GetFileInfo(filename).exists) {
		return memstick_->OpenFile(filename, access, devicename);
	}

	const PSPFileInfo info = archive_->GetFileInfo(filename);
	const bool write = (access & (FILEACCESS_WRITE | FILEACCESS_APPEND | FILEACCESS_CREATE | FILEACCESS_TRUNCATE)) != 0;
	if (!info.exists) {
		if (write) {
			// A new file, perhaps in a directory that so far only exists in the archive.
			CreateParentDirs(filename);
		}
		return memstick_->OpenFile(filename, access, devicename);
	}
	if (info.type == FILETYPE_DIRECTORY) {
		// Let the memory stick produce the error for this.
		return memstick_->OpenFile(filename, access, devicename);
	}
	if (!write) {
		return archive_->OpenFile(filename, access, devicename);
	}

	// Writing to a file that's only in the archive.
	if ((access & FILEACCESS_CREATE) && (access & FILEACCESS_EXCL)) {
		return SCE_KERNEL_ERROR_ERRNO_FILE_ALREADY_EXISTS;
	}
	if (access & FILEACCESS_TRUNCATE) {
		CreateParentDirs(filename);
	} else if (!CopyUp(filename)) {
		return SCE_KERNEL_ERROR_ERRNO_READ_ONLY;
	}
	return memstick_->OpenFile(filename, access, devicename);
}

void MemStickOverlayFileSystem::CloseFile(u32 handle) {
	if (archive_->OwnsHandle(handle)) {
		archive_->CloseFile(handle);
	} else {
		memstick_->CloseFile(handle);
	}
}

size_t MemStickOverlayFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	if (archive_->OwnsHandle(handle))
		return archive_->ReadFile(handle, pointer, size);
	return memstick_->ReadFile(handle, pointer, size);
}

size_t MemStickOverlayFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	if (archive_->OwnsHandle(handle))
		return archive_->ReadFile(handle, pointer, size, usec);
	return memstick_->ReadFile(handle, pointer, size, usec);
}

size_t MemStickOverlayFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	// Files from the archive are only ever open for reading.
	return memstick_->WriteFile(handle, pointer, size);
}

size_t MemStickOverlayFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
	return memstick_->WriteFile(handle, pointer, size, usec);
}

size_t MemStickOverlayFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	if (archive_->OwnsHandle(handle))
		return archive_->SeekFile(handle, position, type);
	return memstick_->SeekFile(handle, position, type);
}

PSPFileInfo MemStickOverlayFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo info = memstick_->GetFileInfo(filename);
	if (!info.exists && InOverlay(filename)) {
		PSPFileInfo archiveInfo = archive_->GetFileInfo(filename);
		if (archiveInfo.exists) {
			// Keep the name the way the memory stick reports it.
			archiveInfo.name = info.name;
			return archiveInfo;
		}
	}
	return info;
}

PSPFileInfo MemStickOverlayFileSystem::GetFileInfoByHandle(u32 handle) {
	if (archive_->OwnsHandle(handle))
		return archive_->GetFileInfoByHandle(handle);
	return memstick_->GetFileInfoByHandle(handle);
}

bool MemStickOverlayFileSystem::OwnsHandle(u32 handle) {
	return archive_->OwnsHandle(handle) || memstick_->OwnsHandle(handle);
}

bool MemStickOverlayFileSystem::MkDir(const std::string &dirname) {
	if (ArchiveOnlyInfo(dirname).exists) {
		// It's there already, as far as the game can tell. Like on the memory stick, that fails.
		// Anything written into it later creates it for real, see CreateParentDirs.
		return false;
	}
	CreateParentDirs(dirname);
	return memstick_->MkDir(dirname);
}

bool MemStickOverlayFileSystem::RmDir(const std::string &dirname) {
	if (ArchiveOnlyInfo(dirname).exists) {
		WARN_LOG(Log::FileSystem, "MemStickOverlay: Can't remove %s, it's in the archive", dirname.c_str());
		return false;
	}
	return memstick_->RmDir(dirname);
}

int MemStickOverlayFileSystem::RenameFile(const std::string &from, const std::string &to) {
	const PSPFileInfo info = ArchiveOnlyInfo(from);
	if (info.exists) {
		if (info.type == FILETYPE_DIRECTORY) {
			WARN_LOG(Log::FileSystem, "MemStickOverlay: Can't rename %s, it's a directory in the archive", from.c_str());
			return SCE_KERNEL_ERROR_ERRNO_READ_ONLY;
		}
		if (!CopyUp(from)) {
			return SCE_KERNEL_ERROR_ERRNO_READ_ONLY;
		}
	}
	if (InOverlay(to) && !memstick_->GetFileInfo(to).exists) {
		CreateParentDirs(to);
	}
	return memstick_->RenameFile(from, to);
}

bool MemStickOverlayFileSystem::RemoveFile(const std::string &filename) {
	if (ArchiveOnlyInfo(filename).exists) {
		WARN_LOG(Log::FileSystem, "MemStickOverlay: Can't remove %s, it's in the archive", filename.c_str());
		return false;
	}
	return memstick_->RemoveFile(filename);
}

bool MemStickOverlayFileSystem::SetFileWritable(const std::string &filename, bool writable) {
	if (ArchiveOnlyInfo(filename).exists) {
		// Archive files are read-only, so that's already the case, otherwise it needs a copy.
		if (!writable)
			return true;
		if (!CopyUp(filename))
			return false;
	}
	return memstick_->SetFileWritable(filename, writable);
}

int MemStickOverlayFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	if (archive_->OwnsHandle(handle))
		return archive_->Ioctl(handle, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
	return memstick_->Ioctl(handle, cmd, indataPtr, inlen, outdataPtr, outlen, usec);
}

PSPDevType MemStickOverlayFileSystem::DevType(u32 handle) {
	if (archive_->OwnsHandle(handle))
		return archive_->DevType(handle);
	return memstick_->DevType(handle);
}

bool MemStickOverlayFileSystem::ComputeRecursiveDirSizeIfFast(const std::string &path, int64_t *size) {
	// Used for the size of save data and the like, so what's actually on the memory stick.
	return memstick_->ComputeRecursiveDirSizeIfFast(path, size);
}

void MemStickOverlayFileSystem::Describe(char *buf, size_t size) const {
	char memstick[256];
	char archive[256];
	memstick_->Describe(memstick, sizeof(memstick));
	archive_->Describe(archive, sizeof(archive));
	snprintf(buf, size, "%s + %s", memstick, archive);
}

void MemStickOverlayFileSystem::DoState(PointerWrap &p) {
	// The memory stick's state exactly as without the overlay, then the archive's, which is usually
	// nothing at all. See ZArchiveFileSystem::DoState.
	memstick_->DoState(p);
	archive_->DoState(p);
}

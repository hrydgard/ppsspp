// Copyright (c) 2017- PPSSPP Project.

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

#include <ctime>
#include "Core/FileSystems/BlobFileSystem.h"

BlobFileSystem::BlobFileSystem(IHandleAllocator *hAlloc, FileLoader *fileLoader, std::string alias)
: alloc_(hAlloc), fileLoader_(fileLoader), alias_(alias) {
}

BlobFileSystem::~BlobFileSystem() {
	// TODO: Who deletes fileLoader?
}

void BlobFileSystem::DoState(PointerWrap &p) {
	// Not used in real emulation.
}

std::vector<PSPFileInfo> BlobFileSystem::GetDirListing(const std::string &path, bool *exists) {
	std::vector<PSPFileInfo> listing;
	listing.push_back(GetFileInfo(alias_));
	if (exists)
		*exists = true;
	return listing;
}

int BlobFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	u32 newHandle = alloc_->GetNewHandle();
	entries_[newHandle] = 0;
	return newHandle;
}

void BlobFileSystem::CloseFile(u32 handle) {
	alloc_->FreeHandle(handle);
	entries_.erase(handle);
}

size_t BlobFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	auto entry = entries_.find(handle);
	if (entry != entries_.end()) {
		s64 readSize = (s64)fileLoader_->ReadAt(entry->second, (size_t)size, pointer);
		entry->second += readSize;
		return (size_t)readSize;
	}
	return 0;
}

size_t BlobFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	usec = 0;
	return ReadFile(handle, pointer, size);
}

size_t BlobFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	return 0;
}

size_t BlobFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
	return 0;
}

size_t BlobFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	auto entry = entries_.find(handle);
	if (entry != entries_.end()) {
		switch (type) {
		case FILEMOVE_BEGIN:
			entry->second = position;
			break;
		case FILEMOVE_CURRENT:
			entry->second += position;
			break;
		case FILEMOVE_END:
			entry->second = fileLoader_->FileSize() + position;
			break;
		}
		return (size_t)entry->second;
	}
	return 0;
}

PSPFileInfo BlobFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo info{};
	info.name = alias_;
	info.size = fileLoader_->FileSize();
	info.access = 0666;
	info.exists = true;
	info.type = FILETYPE_NORMAL;
	return info;
}

bool BlobFileSystem::OwnsHandle(u32 handle) {
	auto entry = entries_.find(handle);
	return entry != entries_.end();
}

int BlobFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	return -1;
}

PSPDevType BlobFileSystem::DevType(u32 handle) {
	return PSPDevType::FILE;
}

bool BlobFileSystem::MkDir(const std::string &dirname) {
	return false;
}

bool BlobFileSystem::RmDir(const std::string &dirname)  {
	return false;
}

int BlobFileSystem::RenameFile(const std::string &from, const std::string &to) {
	return -1;
}

bool BlobFileSystem::RemoveFile(const std::string &filename) {
	return false;
}

u64 BlobFileSystem::FreeSpace(const std::string &path) {
	return 0;
}

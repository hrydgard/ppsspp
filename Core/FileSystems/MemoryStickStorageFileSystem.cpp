// Copyright (c) 2012- PPSSPP Project.

#include "Core/FileSystems/MemoryStickStorageFileSystem.h"

#include <algorithm>
#include <cstring>

#include "Common/Log.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/HW/MemoryStick.h"
#include "Core/MemMap.h"
#include "Core/MemMapHelpers.h"

void MemoryStickStorageFileSystem::DoState(PointerWrap &p) {
	auto s = p.Section("MemoryStickStorageFileSystem", 1);
	if (!s) {
		return;
	}
	Do(p, positions_);
}

std::vector<PSPFileInfo> MemoryStickStorageFileSystem::GetDirListing(std::string_view path, bool *exists) {
	if (exists) {
		*exists = false;
	}
	return {};
}

int MemoryStickStorageFileSystem::OpenFile(std::string filename, FileAccess access, const char *devicename) {
	if (!allocator_) {
		return SCE_KERNEL_ERROR_NODEV;
	}
	const u32 handle = allocator_->GetNewHandle();
	positions_[handle] = 0;
	return (int)handle;
}

void MemoryStickStorageFileSystem::CloseFile(u32 handle) {
	if (positions_.erase(handle) != 0 && allocator_) {
		allocator_->FreeHandle(handle);
	}
}

size_t MemoryStickStorageFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size) {
	int usec = 0;
	return ReadFile(handle, pointer, size, usec);
}

size_t MemoryStickStorageFileSystem::ReadFile(u32 handle, u8 *pointer, s64 size, int &usec) {
	auto position = positions_.find(handle);
	if (position == positions_.end() || size <= 0) {
		return 0;
	}
	memset(pointer, 0, (size_t)size);
	position->second += (u64)size;
	return (size_t)size;
}

size_t MemoryStickStorageFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size) {
	int usec = 0;
	return WriteFile(handle, pointer, size, usec);
}

size_t MemoryStickStorageFileSystem::WriteFile(u32 handle, const u8 *pointer, s64 size, int &usec) {
	auto position = positions_.find(handle);
	if (position == positions_.end() || size <= 0) {
		return 0;
	}
	// Direct raw writes are deliberately not reflected into the host directory
	// tree. Filesystem writes continue through ms0:/fatms0:.
	position->second += (u64)size;
	MemoryStick_NotifyWrite();
	return (size_t)size;
}

size_t MemoryStickStorageFileSystem::SeekFile(u32 handle, s32 position, FileMove type) {
	auto current = positions_.find(handle);
	if (current == positions_.end()) {
		return 0;
	}
	s64 next = 0;
	switch (type) {
	case FILEMOVE_BEGIN: next = position; break;
	case FILEMOVE_CURRENT: next = (s64)current->second + position; break;
	case FILEMOVE_END: next = (s64)MemoryStick_FreeSpace("") + position; break;
	}
	current->second = (u64)std::max<s64>(0, next);
	return (size_t)current->second;
}

PSPFileInfo MemoryStickStorageFileSystem::GetFileInfo(std::string filename) {
	PSPFileInfo info;
	info.name = filename;
	info.exists = true;
	info.type = FILETYPE_NORMAL;
	info.access = 0666;
	info.size = (s64)MemoryStick_FreeSpace("");
	return info;
}

PSPFileInfo MemoryStickStorageFileSystem::GetFileInfoByHandle(u32 handle) {
	return OwnsHandle(handle) ? GetFileInfo("/") : PSPFileInfo{};
}

bool MemoryStickStorageFileSystem::OwnsHandle(u32 handle) {
	return positions_.find(handle) != positions_.end();
}

int MemoryStickStorageFileSystem::Ioctl(u32 handle, u32 cmd, u32 indataPtr, u32 inlen, u32 outdataPtr, u32 outlen, int &usec) {
	if (!OwnsHandle(handle)) {
		return SCE_KERNEL_ERROR_BADF;
	}
	switch (cmd) {
	case 0x02125001:  // Partition mounted.
	case 0x02125008:  // Medium inserted.
		if (!Memory::IsValid4AlignedAddress(outdataPtr) || outlen < sizeof(u32)) {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		Memory::WriteUnchecked_U32(1, outdataPtr);
		return 0;
	case 0x02125009:  // Write-protect switch.
		if (!Memory::IsValid4AlignedAddress(outdataPtr) || outlen < sizeof(u32)) {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		Memory::WriteUnchecked_U32(0, outdataPtr);
		return 0;
	case 0x02125803:  // Memory Stick identity used by the Camera/Savedata UI.
		if (!Memory::IsValidRange(outdataPtr, outlen)) {
			return SCE_KERNEL_ERROR_ERRNO_INVALID_ARGUMENT;
		}
		Memory::Memset(outdataPtr, 0, outlen, "MemoryStickIdentity");
		if (outlen > 0) {
			Memory::WriteUnchecked_U8(2, outdataPtr);
		}
		return 0;
	default:
		DEBUG_LOG(Log::sceIo, "MemoryStickStorage ioctl %08x in=%u out=%u", cmd, inlen, outlen);
		return 0;
	}
}

u64 MemoryStickStorageFileSystem::FreeDiskSpace(const std::string &path) {
	return MemoryStick_FreeSpace("");
}


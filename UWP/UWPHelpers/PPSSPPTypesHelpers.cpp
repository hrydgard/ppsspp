// UWP STORAGE MANAGER
// Copyright (c) 2023 Bashar Astifan.
// Email: bashar@astifan.online
// Telegram: @basharastifan
// GitHub: https://github.com/basharast/UWP2Win32

#include "PPSSPPTypesHelpers.h"
#include "StorageManager.h"
#include "Common/Log.h"

bool ItemsInfoUWPToFilesInfo(std::list<ItemInfoUWP> items, std::vector<File::FileInfo>* files, const char* filter, std::set<std::string> filters) {
	bool state = false;
	if (!items.empty()) {
		for each (auto item in items) {
			File::FileInfo info;
			info.name = item.name;
			info.fullName = Path(item.fullName);
			info.exists = true;
			info.size = item.size;
			info.isDirectory = item.isDirectory;
			info.isWritable = (item.attributes & FILE_ATTRIBUTE_READONLY) == 0;
			info.atime = item.lastAccessTime;
			info.mtime = item.lastWriteTime;
			info.ctime = item.creationTime;
			if (item.attributes & FILE_ATTRIBUTE_READONLY) {
				info.access = 0444;  // Read
			}
			else {
				info.access = 0666;  // Read/Write
			}
			if (item.attributes & FILE_ATTRIBUTE_DIRECTORY) {
				info.access |= 0111;  // Execute
			}
			if (!info.isDirectory) {
				std::string ext = info.fullName.GetFileExtension();
				if (!ext.empty()) {
					ext = ext.substr(1);  // Remove the dot.
					if (filter && filters.find(ext) == filters.end()) {
						continue;
					}
				}
			}
			files->push_back(info);
		}
		state = true;
	}

	return state;
}

void UpdateFileInfoByItemInfo(ItemInfoUWP item, File::FileInfo* info) {
	info->name = item.name;
	info->fullName = Path(item.fullName);
	info->exists = true;
	info->size = item.size;
	info->isDirectory = item.isDirectory;
	info->isWritable = (item.attributes & FILE_ATTRIBUTE_READONLY) == 0;
	info->atime = item.lastAccessTime;
	info->mtime = item.lastWriteTime;
	info->ctime = item.creationTime;
	if (item.attributes & FILE_ATTRIBUTE_READONLY) {
		info->access = 0444;  // Read
	}
	else {
		info->access = 0666;  // Read/Write
	}
	if (item.attributes & FILE_ATTRIBUTE_DIRECTORY) {
		info->access |= 0111;  // Execute
	}
}

bool GetFileInfoUWP(std::string path, File::FileInfo* info) {
	bool state = false;
	VERBOSE_LOG(FILESYS, "GetFileInfoUWP (%s)", path.c_str());
	auto itemInfo = GetItemInfoUWP(path);
	if (itemInfo.attributes != INVALID_FILE_ATTRIBUTES) {
		UpdateFileInfoByItemInfo(itemInfo, info);
		state = true;
	}
	else {
		VERBOSE_LOG(FILESYS, "GetFileInfoUWP failed! (%s)", path.c_str());
	}

	return state;
}

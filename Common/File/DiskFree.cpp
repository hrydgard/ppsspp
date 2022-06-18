#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#if defined(__ANDROID__)
#include <sys/types.h>
#include <sys/vfs.h>
#define statvfs statfs
#else
#include <sys/statvfs.h>
#endif
#include <ctype.h>
#include <fcntl.h>
#endif
#include <cinttypes>

#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/File/AndroidStorage.h"
#include "Common/Data/Encoding/Utf8.h"

bool free_disk_space(const Path &path, int64_t &space) {
#ifdef _WIN32
	ULARGE_INTEGER free;
	if (GetDiskFreeSpaceExW(path.ToWString().c_str(), &free, nullptr, nullptr)) {
		space = free.QuadPart;
		return true;
	}
#else
	if (path.Type() == PathType::CONTENT_URI) {
		space = Android_GetFreeSpaceByContentUri(path.ToString());
		INFO_LOG(COMMON, "Free space at '%s': %" PRIu64, path.c_str(), space);
		return space >= 0;
	}

	struct statvfs diskstat;
	int res = statvfs(path.c_str(), &diskstat);

	if (res == 0) {
		// Not sure why we're excluding Android here anyway...
#ifndef __ANDROID__
		if (diskstat.f_flag & ST_RDONLY) {
			// No space to write.
			space = 0;
			return true;
		}
#endif
		space = (uint64_t)diskstat.f_bavail * (uint64_t)diskstat.f_frsize;
		return true;
	}
#endif

	// We can't know how much is free.
	return false;
}

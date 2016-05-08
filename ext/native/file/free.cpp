#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sys/stat.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#if defined(ANDROID)
#include <sys/types.h>
#include <sys/vfs.h>
#define statvfs statfs
#elif defined(__SYMBIAN32__)
#include <mw/QSystemStorageInfo>
QTM_USE_NAMESPACE
#else
#include <sys/statvfs.h>
#endif
#include <ctype.h>
#include <fcntl.h>
#endif

#include "base/basictypes.h"
#include "util/text/utf8.h"

bool free_disk_space(const std::string &dir, uint64_t &space) {
#ifdef _WIN32
	const std::wstring w32path = ConvertUTF8ToWString(dir);
	ULARGE_INTEGER free;
	if (GetDiskFreeSpaceExW(w32path.c_str(), &free, nullptr, nullptr)) {
		space = free.QuadPart;
		return true;
	}
#elif defined(__SYMBIAN32__)
	QSystemStorageInfo storageInfo;
	space = (uint64_t)storageInfo.availableDiskSpace("E");
	return true;
#else
	struct statvfs diskstat;
	int res = statvfs(dir.c_str(), &diskstat);

	if (res == 0) {
#ifndef ANDROID
		if (diskstat.f_flag & ST_RDONLY) {
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
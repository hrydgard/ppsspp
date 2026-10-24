#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include "Common/CommonWindows.h"
#include <direct.h>
#if PPSSPP_PLATFORM(UWP)
#include <fileapifromapp.h>
#include <UWP/UWPHelpers/StorageManager.h>
#elif defined(_MSC_VER)
#include <lm.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include "Common/Thread/ThreadUtil.h"
#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#define HAVE_NETWORK_BROWSING
#endif
#else
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#endif

#include <cstring>
#include <string>
#include <set>
#include <cstdio>
#include <sys/stat.h>
#include <cctype>
#include <algorithm>  // remove_if

#include "Common/Data/Encoding/Utf8.h"
#include "Common/StringUtils.h"
#include "Common/Net/URL.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/File/AndroidStorage.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"

#if !defined(__linux__) && !defined(_WIN32) && !defined(__QNX__)
#define stat64 stat
#endif

#ifdef HAVE_LIBNX
// Far from optimal, but I guess it works...
#define fseeko fseek
#define ftello ftell
#define fileno
#endif // HAVE_LIBNX

// NOTE: There's another one in FileUtil.cpp.
#ifdef _WIN32
constexpr bool SIMULATE_SLOW_IO = false;
#else
constexpr bool SIMULATE_SLOW_IO = false;
#endif
constexpr bool LOG_IO = false;

namespace File {

#if PPSSPP_PLATFORM(WINDOWS)
static uint64_t FiletimeToStatTime(FILETIME ft, int* out_usec) {
	constexpr int64_t windowsTickResolution = 10000000;
	constexpr int64_t secToUnixEpoch = 11644473600LL;

	const int64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

	if (out_usec) {
		// The remainder will be between 0 and 9,999,999 ticks.
		// Dividing by 10 gives 0 to 999,999 microseconds.
		*out_usec = (int)((ticks % windowsTickResolution) / 10);
	}

	return (uint64_t)(ticks / windowsTickResolution - secToUnixEpoch);
}

// This is used for both WIN32_FILE_ATTRIBUTE_DATA and WIN32_FIND_DATA, which have the same time and size fields.
template<class T>
void ReadFileAttributes(FileInfo *fileInfo, const T &attrs) {
	fileInfo->atime = FiletimeToStatTime(attrs.ftLastAccessTime, &fileInfo->atimeUs);
	fileInfo->mtime = FiletimeToStatTime(attrs.ftLastWriteTime, &fileInfo->mtimeUs);
	fileInfo->ctime = FiletimeToStatTime(attrs.ftCreationTime, &fileInfo->ctimeUs);

	fileInfo->exists = true;
	fileInfo->size = ((uint64_t)attrs.nFileSizeHigh << 32) | attrs.nFileSizeLow;
	fileInfo->isDirectory = (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	fileInfo->isWritable = (attrs.dwFileAttributes & FILE_ATTRIBUTE_READONLY) == 0;

	if (attrs.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
		fileInfo->access = 0444;  // Read
	} else {
		fileInfo->access = 0666;  // Read/Write
	}
	if (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		fileInfo->access |= 0111;  // Execute
	}
}

#ifdef HAVE_NETWORK_BROWSING
// Returns the server name if the path is just "//server", which can't be listed like a directory.
static bool GetUNCServerName(const Path &path, std::wstring *server) {
	const std::string &str = path.ToString();
	if (str.size() < 3 || str[0] != '/' || str[1] != '/' || str[2] == '.' || str[2] == '?' || str.find('/', 2) != std::string::npos) {
		return false;
	}
	*server = ConvertUTF8ToWString(str.substr(2));
	return true;
}

static FileInfo NetworkDirInfo(const Path &fullName, const std::string &name) {
	FileInfo info;
	info.name = name;
	info.fullName = fullName;
	info.exists = true;
	info.isDirectory = true;
	info.isWritable = false;
	info.size = 0;
	return info;
}

struct NetListing {
	void Add(FileInfo &&info) {
		std::lock_guard<std::mutex> guard(lock);
		files.push_back(std::move(info));
		lastAdd = std::chrono::steady_clock::now();
		cond.notify_all();
	}

	std::mutex lock;
	std::condition_variable cond;
	std::vector<FileInfo> files;
	std::chrono::steady_clock::time_point lastAdd;
	bool done = false;
	bool success = false;
};

// Network calls can block for a long time (half a minute for the shell's network folder, even after it
// has found everything), and can't be cancelled. So run them on a thread of their own, and abandon it
// when the deadline passes, or when it has found something and then gone quiet for a while.
static bool ListNetworkWithTimeout(const Path &directory, std::function<bool(NetListing &)> func, std::vector<FileInfo> *files, const bool *cancel) {
	// Servers turn up in the network folder after 2 to 6 seconds.
	constexpr auto timeout = std::chrono::seconds(8);
	constexpr auto quietTime = std::chrono::seconds(1);
	constexpr auto cancelPollTime = std::chrono::milliseconds(20);
	auto listing = std::make_shared<NetListing>();
	std::thread([listing, func] {
		SetCurrentThreadName("NetListing");
		bool success = func(*listing);
		std::lock_guard<std::mutex> guard(listing->lock);
		listing->success = success;
		listing->done = true;
		listing->cond.notify_all();
	}).detach();

	const auto deadline = std::chrono::steady_clock::now() + timeout;
	std::unique_lock<std::mutex> guard(listing->lock);
	while (!listing->done) {
		auto wakeup = deadline;
		if (!listing->files.empty()) {
			wakeup = std::min(deadline, listing->lastAdd + quietTime);
		}
		if (cancel && *cancel) {
			return false;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now >= wakeup) {
			INFO_LOG(Log::IO, "Stopped waiting for the listing of %s, found %d entries", directory.c_str(), (int)listing->files.size());
			break;
		}
		listing->cond.wait_until(guard, cancel ? std::min(wakeup, now + cancelPollTime) : wakeup);
	}
	files->insert(files->end(), listing->files.begin(), listing->files.end());
	return listing->success || !listing->files.empty();
}

// Lists the disk shares of a server as directories, skipping the admin ones like C$ and IPC$.
static bool GetUNCServerShares(const Path &directory, const std::wstring &server, std::string_view prefix, NetListing &listing) {
	bool success = false;
	DWORD resumeHandle = 0;
	NET_API_STATUS status;
	do {
		SHARE_INFO_1 *shares = nullptr;
		DWORD count = 0;
		DWORD total = 0;
		status = NetShareEnum((LPWSTR)server.c_str(), 1, (LPBYTE *)&shares, MAX_PREFERRED_LENGTH, &count, &total, &resumeHandle);
		if (status != NERR_Success && status != ERROR_MORE_DATA) {
			WARN_LOG(Log::IO, "NetShareEnum failed for %s: %d", directory.c_str(), (int)status);
			break;
		}
		success = true;
		for (DWORD i = 0; i < count; i++) {
			if ((shares[i].shi1_type & STYPE_MASK) != STYPE_DISKTREE || (shares[i].shi1_type & STYPE_SPECIAL)) {
				continue;
			}
			std::string name = ConvertWStringToUTF8(shares[i].shi1_netname);
			if (startsWith(name, prefix)) {
				listing.Add(NetworkDirInfo(directory / name, name));
			}
		}
		if (shares) {
			NetApiBufferFree(shares);
		}
	} while (status == ERROR_MORE_DATA);
	return success;
}

// Lists the servers in the shell's network folder, the same ones Explorer shows. The old WNet
// enumeration finds nothing on a modern network, where servers are found through WSD.
static bool GetNetworkServers(std::string_view prefix, NetListing &listing) {
	HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	bool success = false;
	IShellItem *network = nullptr;
	IEnumShellItems *items = nullptr;
	if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_NetworkFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&network))) &&
		SUCCEEDED(network->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) {
		success = true;
		std::set<std::string> seen;
		IShellItem *item = nullptr;
		while (items->Next(1, &item, nullptr) == S_OK) {
			// Other devices (media players, printers) have parsing names that aren't UNC paths.
			LPWSTR parsingName = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsingName))) {
				if (wcsncmp(parsingName, L"\\\\", 2) == 0) {
					Path server{std::wstring(parsingName)};
					std::string name = server.GetFilename();
					if (startsWith(name, prefix) && seen.insert(name).second) {
						listing.Add(NetworkDirInfo(server, name));
					}
				}
				CoTaskMemFree(parsingName);
			}
			item->Release();
		}
		items->Release();
	} else {
		WARN_LOG(Log::IO, "Couldn't enumerate the network folder");
	}
	if (network) {
		network->Release();
	}
	if (SUCCEEDED(initResult)) {
		CoUninitialize();
	}
	return success;
}
#endif

#endif

bool GetFileInfo(const Path &path, FileInfo * fileInfo) {
	if (LOG_IO) {
		INFO_LOG(Log::IO, "GetFileInfo %s", path.ToVisualString().c_str());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(300, "slow-io-sim");
	}

	switch (path.Type()) {
	case PathType::NATIVE:
		break;  // OK
	case PathType::CONTENT_URI:
		return Android_GetFileInfo(path.ToString(), fileInfo);
	default:
		return false;
	}

	// TODO: Expand relative paths?
	fileInfo->fullName = path;

#if PPSSPP_PLATFORM(WINDOWS)
	WIN32_FILE_ATTRIBUTE_DATA attrs;
#if PPSSPP_PLATFORM(UWP)
	if (!GetFileAttributesExFromAppW(path.ToWString().c_str(), GetFileExInfoStandard, &attrs)) {
#else
	if (!GetFileAttributesExW(path.ToWString().c_str(), GetFileExInfoStandard, &attrs)) {
#endif
		fileInfo->size = 0;
		fileInfo->isDirectory = false;
		fileInfo->exists = false;
		return false;
	}

	ReadFileAttributes(fileInfo, attrs);

#else

#if (defined __ANDROID__) && (__ANDROID_API__ < 21)
	struct stat file_info;
	int result = stat(path.c_str(), &file_info);
#else
	struct stat64 file_info;
	int result = stat64(path.c_str(), &file_info);
#endif
	if (result < 0) {
		fileInfo->exists = false;
		return false;
	}

	fileInfo->isDirectory = S_ISDIR(file_info.st_mode);
	fileInfo->isWritable = false;
	fileInfo->size = file_info.st_size;
	fileInfo->exists = true;
	fileInfo->atime = file_info.st_atime;
	fileInfo->mtime = file_info.st_mtime;
	fileInfo->ctime = file_info.st_ctime;

	// TODO: Use statx to provide higher precision timestamps on Linux.
	fileInfo->atimeUs = 0;
	fileInfo->mtimeUs = 0;
	fileInfo->ctimeUs = 0;

	fileInfo->access = file_info.st_mode & 0x1ff;
	// HACK: approximation
	if (file_info.st_mode & 0200) {
		fileInfo->isWritable = true;
	}
#endif
	return true;
}

bool GetModifTimeT(const Path &filename, time_t *return_time) {
	FileInfo info;
	if (GetFileInfo(filename, &info)) {
		*return_time = info.mtime;
		return true;
	} else {
		*return_time = 0;
		return false;
	}
}

bool GetModifTime(const Path &filename, tm & return_time) {
	memset(&return_time, 0, sizeof(return_time));
	FileInfo info;
	if (GetFileInfo(filename, &info)) {
		time_t t = info.mtime;
		localtime_r((time_t*)&t, &return_time);
		return true;
	} else {
		return false;
	}
}

bool FileInfo::operator <(const FileInfo & other) const {
	if (isDirectory && !other.isDirectory)
		return true;
	else if (!isDirectory && other.isDirectory)
		return false;
	if (strcasecmp(name.c_str(), other.name.c_str()) < 0)
		return true;
	else
		return false;
}

std::vector<File::FileInfo> ApplyFilter(std::vector<File::FileInfo> files, const char *extensionFilter, std::string_view prefix) {
	std::set<std::string> filters;
	if (extensionFilter) {
		std::string tmp;
		while (*extensionFilter) {
			if (*extensionFilter == ':') {
				filters.emplace("." + tmp);
				tmp.clear();
			} else {
				tmp.push_back(*extensionFilter);
			}
			extensionFilter++;
		}
		if (!tmp.empty())
			filters.emplace("." + tmp);
	}

	auto pred = [&](const File::FileInfo &info) {
		// WARNING: Keep in mind that if we return true here, the files is REMOVED from the list.
		// It's not retain_if.
		if (!startsWith(info.name, prefix)) {
			return true;
		}
		if (info.isDirectory || !extensionFilter)
			return false;
		std::string ext = info.fullName.GetFileExtension();
		return filters.find(ext) == filters.end();
	};
	files.erase(std::remove_if(files.begin(), files.end(), pred), files.end());
	return files;
}

bool GetFilesInDir(const Path &directory, std::vector<FileInfo> *files, const char *filter, int flags, std::string_view prefix, const bool *cancel) {
	if (LOG_IO) {
		INFO_LOG(Log::IO, "GetFilesInDir '%s' (ext %s, prefix %.*s)", directory.ToVisualString().c_str(), filter, (int)prefix.size(), prefix.data());
	}
	if (SIMULATE_SLOW_IO) {
		sleep_ms(300, "slow-io-sim");
	}

	if (directory.Type() == PathType::CONTENT_URI) {
		bool exists = false;
		// TODO: Move prefix filtering over to the Java side for more speed.
		std::vector<File::FileInfo> fileList = Android_ListContentUri(directory.ToString(), std::string(prefix), &exists);
		int beforeFilter = (int)fileList.size();
		*files = ApplyFilter(fileList, filter, prefix);
		std::sort(files->begin(), files->end());
		DEBUG_LOG(Log::IO, "GetFilesInDir: Found %d entries (%d before filter). Path: %s", (int)files->size(), beforeFilter, directory.ToVisualString().c_str());
		return exists;
	}

	std::set<std::string> filters;
	if (filter) {
		std::string tmp;
		while (*filter) {
			if (*filter == ':') {
				filters.insert(tmp);
				tmp.clear();
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
		if (!tmp.empty())
			filters.insert(tmp);
	}

#if PPSSPP_PLATFORM(WINDOWS)
	if (directory.IsRoot()) {
		// Special path that means root of file system.
		// This does not respect prefix filtering.
		std::vector<std::string> drives = File::GetWindowsDrives();
		for (auto drive = drives.begin(); drive != drives.end(); ++drive) {
			if (*drive == "A:/" || *drive == "B:/")
				continue;
			File::FileInfo fake;
			fake.fullName = Path(*drive);
			fake.name = *drive;
			fake.isDirectory = true;
			fake.exists = true;
			fake.size = 0;
			fake.isWritable = false;
			files->push_back(fake);
		}
#ifdef HAVE_NETWORK_BROWSING
		files->push_back(NetworkDirInfo(Path("//"), "Network"));
#endif
		return files->size();
	}
#ifdef HAVE_NETWORK_BROWSING
	std::wstring server;
	if (directory.IsNetworkRoot() || GetUNCServerName(directory, &server)) {
		std::string prefixCopy(prefix);
		bool success;
		if (server.empty()) {
			success = ListNetworkWithTimeout(directory, [prefixCopy](NetListing &listing) {
				return GetNetworkServers(prefixCopy, listing);
			}, files, cancel);
		} else {
			success = ListNetworkWithTimeout(directory, [directory, server, prefixCopy](NetListing &listing) {
				return GetUNCServerShares(directory, server, prefixCopy, listing);
			}, files, cancel);
		}
		std::sort(files->begin(), files->end());
		return success;
	}
#endif
	// Find the first file in the directory.
	WIN32_FIND_DATA ffd;
	std::wstring wpath = directory.ToWString();
	wpath += L"\\*";
#if PPSSPP_PLATFORM(UWP)
	HANDLE hFind = FindFirstFileExFromAppW(wpath.c_str(), FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
#else
	HANDLE hFind = FindFirstFileEx(wpath.c_str(), FindExInfoStandard, &ffd, FindExSearchNameMatch, NULL, 0);
#endif
	if (hFind == INVALID_HANDLE_VALUE) {
#if PPSSPP_PLATFORM(UWP)
		// This step just to avoid empty results by adding fake folders
		// it will help also to navigate back between selected folder
		// we must ignore this function for any request other than UI navigation
		if (GetFakeFolders(directory, files, filter, filters))
			return true;
#endif
		return false;
	}
	do {
		const std::string virtualName = ConvertWStringToUTF8(ffd.cFileName);
		// check for "." and ".."
		if (!(flags & GETFILES_GET_NAVIGATION_ENTRIES) && (virtualName == "." || virtualName == ".."))
			continue;
		// Remove dotfiles (optional with flag.)
		if (!(flags & GETFILES_GETHIDDEN)) {
			if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
				continue;
		}

		if (!startsWith(virtualName, prefix)) {
			continue;
		}

		if (LOG_IO) {
			// INFO_LOG(Log::IO, "GetFilesInDir item %s", virtualName.c_str());
		}

		FileInfo info;
		info.name = virtualName;
		info.fullName = directory / virtualName;

		ReadFileAttributes(&info, ffd);

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
	} while (FindNextFile(hFind, &ffd) != 0);
	FindClose(hFind);
#else
	struct dirent *result = NULL;
	DIR *dirp = opendir(directory.c_str());
	if (!dirp)
		return false;
	while ((result = readdir(dirp))) {
		const std::string virtualName(result->d_name);
		// check for "." and ".."
		if (!(flags & GETFILES_GET_NAVIGATION_ENTRIES) && (virtualName == "." || virtualName == ".."))
			continue;

		// Remove dotfiles (optional with flag.)
		if (!(flags & GETFILES_GETHIDDEN)) {
			if (virtualName[0] == '.')
				continue;
		}

		if (!startsWith(virtualName, prefix)) {
			continue;
		}

		// Let's just reuse GetFileInfo. We're calling stat anyway to get isDirectory information.
		Path fullName = directory / virtualName;

		FileInfo info;
		info.name = virtualName;
		if (!GetFileInfo(fullName, &info)) {
			continue;
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
	closedir(dirp);
#endif
	std::sort(files->begin(), files->end());
	if (LOG_IO) {
		INFO_LOG(Log::IO, "GetFilesInDir: Found %d files", (int)files->size());
	}
	return true;
}

#if PPSSPP_PLATFORM(WINDOWS)
// Returns a vector with the device names
std::vector<std::string> GetWindowsDrives() {
	std::vector<std::string> drives;

#if PPSSPP_PLATFORM(UWP)
	DWORD logicaldrives = GetLogicalDrives();
	for (int i = 0; i < 26; i++)
	{
		if (logicaldrives & (1 << i))
		{
			CHAR driveName[] = { (CHAR)(TEXT('A') + i), TEXT(':'), TEXT('\\'), TEXT('\0') };
			std::string str(driveName);
			drives.push_back(driveName);
		}
	}
	return drives;
#else
	const DWORD buffsize = GetLogicalDriveStrings(0, NULL);
	std::vector<wchar_t> buff(buffsize);
	if (GetLogicalDriveStrings(buffsize, buff.data()) == buffsize - 1) {
		auto drive = buff.data();
		while (*drive) {
			std::string str(ConvertWStringToUTF8(drive));
			str.pop_back();	// we don't want the final backslash
			str += "/";
			drives.push_back(str);
			// advance to next drive
			while (*drive++) {}
		}
	}
	return drives;
#endif  // PPSSPP_PLATFORM(UWP)
}
#endif  // PPSSPP_PLATFORM(WINDOWS)

}  // namespace File

#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <string.h>
#include <thread>
#include <vector>
#include <stdlib.h>

#include "file/file_util.h"

// Abstraction above path that lets you navigate easily.
// "/" is a special path that means the root of the file system. On Windows,
// listing this will yield drives.
class PathBrowser {
public:
	PathBrowser() {}
	PathBrowser(std::string path) { SetPath(path); }
	~PathBrowser();

	void SetPath(const std::string &path);
	bool IsListingReady();
	bool GetListing(std::vector<FileInfo> &fileInfo, const char *filter = nullptr, bool *cancel = nullptr);
	void Navigate(const std::string &path);

	std::string GetPath() const {
		if (path_ != "/")
			return path_;
		else
			return "";
	}
	std::string GetFriendlyPath() const {
		std::string str = GetPath();
#if defined(__ANDROID__)
		// Do nothing
#elif defined(__linux)
		char *home = getenv("HOME");
		if (home != NULL && !strncmp(str.c_str(), home, strlen(home))) {
			str = str.substr(strlen(home));
			str.insert(0, 1, '~');
		}
#endif
		return str;
	}

private:
	void HandlePath();

	std::string path_;
	std::string pendingPath_;
	std::vector<FileInfo> pendingFiles_;
	std::condition_variable pendingCond_;
	std::mutex pendingLock_;
	std::thread pendingThread_;
	bool pendingCancel_ = false;
	bool pendingStop_ = false;
	bool ready_ = false;
};


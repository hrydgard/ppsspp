#pragma once

#include <string>
#include <string.h>
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

	void SetPath(const std::string &path);
	void GetListing(std::vector<FileInfo> &fileInfo, const char *filter = 0);
	void Navigate(const std::string &path);

	std::string GetPath() const {
		if (path_ != "/")
			return path_;
		else
			return "";
	}
	std::string GetFriendlyPath() {
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

	std::string path_;
};


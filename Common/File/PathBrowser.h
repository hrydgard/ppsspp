#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <cstdlib>

#include "Common/File/DirListing.h"

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
	bool GetListing(std::vector<File::FileInfo> &fileInfo, const char *filter = nullptr, bool *cancel = nullptr);

	bool CanNavigateUp();
	void NavigateUp();
	void Navigate(const std::string &path);

	std::string GetPath() const {
		if (path_ != "/")
			return path_;
		else
			return "";
	}
	std::string GetFriendlyPath() const;

private:
	void HandlePath();
	void ResetPending();

	std::string path_;
	std::string pendingPath_;
	std::vector<File::FileInfo> pendingFiles_;
	std::condition_variable pendingCond_;
	std::mutex pendingLock_;
	std::thread pendingThread_;
	bool pendingActive_ = false;
	bool pendingCancel_ = false;
	bool pendingStop_ = false;
	bool ready_ = false;
};


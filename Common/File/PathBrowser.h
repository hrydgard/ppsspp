#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <cstdlib>


#include "Common/File/DirListing.h"
#include "Common/File/Path.h"

// Abstraction above path that lets you navigate easily.
// "/" is a special path that means the root of the file system. On Windows,
// listing this will yield drives.
class PathBrowser {
public:
	PathBrowser() {}
	PathBrowser(const Path &path) { SetPath(path); }
	~PathBrowser();

	void SetPath(const Path &path);
	void Refresh() {
		HandlePath();
	}
	bool IsListingReady();
	bool GetListing(std::vector<File::FileInfo> &fileInfo, const char *filter = nullptr, bool *cancel = nullptr);

	bool CanNavigateUp();
	void NavigateUp();

	void Navigate(const std::string &subdir);

	const Path &GetPath() const {
		return path_;
	}
	std::string GetFriendlyPath() const;

	void SetUserAgent(const std::string &s) {
		userAgent_ = s;
	}
	void SetRootAlias(const std::string &alias, const std::string &longValue) {
		aliasDisplay_ = alias;
		aliasMatch_ = longValue;
	}

	bool empty() const {
		return path_.empty();
	}

private:
	void HandlePath();
	void ResetPending();

	Path path_;
	Path pendingPath_;
	std::string userAgent_;
	std::string aliasDisplay_;
	std::string aliasMatch_;
	std::vector<File::FileInfo> pendingFiles_;
	std::condition_variable pendingCond_;
	std::mutex pendingLock_;
	std::thread pendingThread_;
	bool pendingActive_ = false;
	bool pendingCancel_ = false;
	bool pendingStop_ = false;
	bool ready_ = false;
};


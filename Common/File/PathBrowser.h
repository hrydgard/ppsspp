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
	~PathBrowser();

	void SetPath(const Path &path);
	void Refresh() {
		HandlePath();
	}
	bool IsListingReady() const {
		return ready_;
	}
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
	void SetRootAlias(const std::string &alias, const Path &rootPath) {
		aliasDisplay_ = alias;
		aliasMatch_ = rootPath;
	}
	void RestrictToRoot(const Path &root);
	bool empty() const {
		return path_.empty();
	}
	bool Success() const {
		return success_;
	}

private:
	void HandlePath();
	void ResetPending();
	void ApplyRestriction();

	Path path_;
	Path pendingPath_;
	Path restrictedRoot_;
	std::string userAgent_;
	std::string aliasDisplay_;
	Path aliasMatch_;
	std::vector<File::FileInfo> pendingFiles_;
	std::condition_variable pendingCond_;
	std::mutex pendingLock_;
	std::thread pendingThread_;
	bool pendingActive_ = false;
	bool pendingCancel_ = false;
	bool pendingStop_ = false;
	bool ready_ = false;
	bool success_ = true;
};


#pragma once

#include <condition_variable>
#include <mutex>
#include <string_view>
#include <cstring>
#include <thread>
#include <vector>
#include <cstdlib>

#include "Common/File/DirListing.h"
#include "Common/File/Path.h"

// Abstraction above path that lets you navigate easily and get listings off-thread.
// "/" is a special path that means the root of the file system. On Windows,
// listing "/" will yield drives.
class PathBrowser {
public:
	PathBrowser() {}
	~PathBrowser();

	void SetPath(const Path &path);
	void Refresh() {
		HandlePath();
	}
	bool IsListingReady() const {
		std::lock_guard<std::mutex> guard(pendingLock_);
		return ready_;
	}

	// If called before IsListingReady() returns true, will block (becomes synchronous). Don't do that.
	bool GetListing(std::vector<File::FileInfo> &fileInfo, const char *filter = nullptr, bool *cancel = nullptr);

	bool CanNavigateUp() const;
	void NavigateUp();

	void Navigate(std::string_view subdir);

	const Path &GetPath() const {
		return path_;
	}

	void SetUserAgent(std::string_view s) {
		std::lock_guard<std::mutex> guard(pendingLock_);
		userAgent_ = s;
	}
	void RestrictToRoot(const Path &root);
	bool empty() const {
		return path_.empty();
	}
	bool Success() const {
		std::lock_guard<std::mutex> guard(pendingLock_);
		return success_;
	}

private:
	void HandlePath();
	void ApplyRestriction();

	Path path_;
	Path pendingPath_;
	Path restrictedRoot_;
	std::string userAgent_;
	std::vector<File::FileInfo> pendingFiles_;
	std::condition_variable pendingCond_;
	mutable std::mutex pendingLock_;
	std::thread pendingThread_;
	bool pendingActive_ = false;
	bool pendingCancel_ = false;
	bool pendingStop_ = false;
	bool ready_ = false;
	bool success_ = true;
};

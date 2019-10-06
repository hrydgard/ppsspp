#include <algorithm>
#include <set>
#include "base/stringutil.h"
#include "base/timeutil.h"
#include "file/path.h"
#include "net/http_client.h"
#include "net/url.h"
#include "thread/threadutil.h"

bool LoadRemoteFileList(const std::string &url, bool *cancel, std::vector<FileInfo> &files) {
	http::Client http;
	Buffer result;
	int code = 500;
	std::vector<std::string> responseHeaders;

	Url baseURL(url);
	if (!baseURL.Valid()) {
		return false;
	}

	// Start by requesting the list of files from the server.
	if (http.Resolve(baseURL.Host().c_str(), baseURL.Port())) {
		if (http.Connect(2, 20.0, cancel)) {
			code = http.GET(baseURL.Resource().c_str(), &result, responseHeaders);
			http.Disconnect();
		}
	}

	if (code != 200 || (cancel && *cancel)) {
		return false;
	}

	std::string listing;
	std::vector<std::string> items;
	result.TakeAll(&listing);

	std::string contentType;
	for (const std::string &header : responseHeaders) {
		if (startsWithNoCase(header, "Content-Type:")) {
			contentType = header.substr(strlen("Content-Type:"));
			// Strip any whitespace (TODO: maybe move this to stringutil?)
			contentType.erase(0, contentType.find_first_not_of(" \t\r\n"));
			contentType.erase(contentType.find_last_not_of(" \t\r\n") + 1);
		}
	}

	// TODO: Technically, "TExt/hTml    ; chaRSet    =    Utf8" should pass, but "text/htmlese" should not.
	// But unlikely that'll be an issue.
	bool parseHtml = startsWithNoCase(contentType, "text/html");
	bool parseText = startsWithNoCase(contentType, "text/plain");

	if (parseText) {
		// Plain text format - easy.
		SplitString(listing, '\n', items);
	} else if (parseHtml) {
		// Try to extract from an automatic webserver directory listing...
		GetQuotedStrings(listing, items);
	} else {
		ELOG("Unsupported Content-Type: %s", contentType.c_str());
		return false;
	}

	for (std::string item : items) {
		// Apply some workarounds.
		if (item.empty())
			continue;
		if (item.back() == '\r')
			item.pop_back();
		if (item == baseURL.Resource())
			continue;

		FileInfo info;
		info.name = item;
		info.fullName = baseURL.Relative(item).ToString();
		info.isDirectory = endsWith(item, "/");
		info.exists = true;
		info.size = 0;
		info.isWritable = false;

		files.push_back(info);
	}

	return !files.empty();
}

std::vector<FileInfo> ApplyFilter(std::vector<FileInfo> files, const char *filter) {
	std::set<std::string> filters;
	if (filter) {
		std::string tmp;
		while (*filter) {
			if (*filter == ':') {
				filters.insert(std::move(tmp));
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
		if (!tmp.empty())
			filters.insert(std::move(tmp));
	}

	auto pred = [&](const FileInfo &info) {
		if (info.isDirectory || !filter)
			return false;
		std::string ext = getFileExtension(info.fullName);
		return filters.find(ext) == filters.end();
	};
	files.erase(std::remove_if(files.begin(), files.end(), pred), files.end());
	return files;
}

PathBrowser::~PathBrowser() {
	std::unique_lock<std::mutex> guard(pendingLock_);
	pendingCancel_ = true;
	pendingStop_ = true;
	pendingCond_.notify_all();
	guard.unlock();

	if (pendingThread_.joinable()) {
		pendingThread_.join();
	}
}

// Normalize slashes.
void PathBrowser::SetPath(const std::string &path) {
	if (path[0] == '!') {
		path_ = path;
		HandlePath();
		return;
	}
	path_ = path;
	for (size_t i = 0; i < path_.size(); i++) {
		if (path_[i] == '\\') path_[i] = '/';
	}
	if (!path_.size() || (path_[path_.size() - 1] != '/'))
		path_ += "/";
	HandlePath();
}

void PathBrowser::HandlePath() {
	std::lock_guard<std::mutex> guard(pendingLock_);

	if (!path_.empty() && path_[0] == '!') {
		ready_ = true;
		pendingCancel_ = true;
		pendingPath_.clear();
		return;
	}
	if (!startsWith(path_, "http://") && !startsWith(path_, "https://")) {
		ready_ = true;
		pendingCancel_ = true;
		pendingPath_.clear();
		return;
	}

	ready_ = false;
	pendingCancel_ = false;
	pendingFiles_.clear();
	pendingPath_ = path_;
	pendingCond_.notify_all();

	if (pendingThread_.joinable())
		return;

	pendingThread_ = std::thread([&] {
		setCurrentThreadName("PathBrowser");

		std::unique_lock<std::mutex> guard(pendingLock_);
		std::vector<FileInfo> results;
		std::string lastPath;
		while (!pendingStop_) {
			while (lastPath == pendingPath_ && !pendingCancel_) {
				pendingCond_.wait(guard);
			}
			lastPath = pendingPath_;
			bool success = false;
			if (!lastPath.empty()) {
				guard.unlock();
				results.clear();
				success = LoadRemoteFileList(lastPath, &pendingCancel_, results);
				guard.lock();
			}

			if (pendingPath_ == lastPath) {
				if (success && !pendingCancel_) {
					pendingFiles_ = results;
				}
				pendingPath_.clear();
				lastPath.clear();
				ready_ = true;
			}
		}
	});
}

bool PathBrowser::IsListingReady() {
	return ready_;
}

bool PathBrowser::GetListing(std::vector<FileInfo> &fileInfo, const char *filter, bool *cancel) {
	std::unique_lock<std::mutex> guard(pendingLock_);
	while (!IsListingReady() && (!cancel || !*cancel)) {
		// In case cancel changes, just sleep.
		guard.unlock();
		sleep_ms(100);
		guard.lock();
	}

#ifdef _WIN32
	if (path_ == "/") {
		// Special path that means root of file system.
		std::vector<std::string> drives = getWindowsDrives();
		for (auto drive = drives.begin(); drive != drives.end(); ++drive) {
			if (*drive == "A:/" || *drive == "B:/")
				continue;
			FileInfo fake;
			fake.fullName = *drive;
			fake.name = *drive;
			fake.isDirectory = true;
			fake.exists = true;
			fake.size = 0;
			fake.isWritable = false;
			fileInfo.push_back(fake);
		}
	}
#endif

	if (startsWith(path_, "http://") || startsWith(path_, "https://")) {
		fileInfo = ApplyFilter(pendingFiles_, filter);
		return true;
	} else {
		getFilesInDir(path_.c_str(), &fileInfo, filter);
		return true;
	}
}

// TODO: Support paths like "../../hello"
void PathBrowser::Navigate(const std::string &path) {
	if (path == ".")
		return;
	if (path == "..") {
		// Upwards.
		// Check for windows drives.
		if (path_.size() == 3 && path_[1] == ':') {
			path_ = "/";
		} else {
			size_t slash = path_.rfind('/', path_.size() - 2);
			if (slash != std::string::npos)
				path_ = path_.substr(0, slash + 1);
		}
	} else {
		if (path.size() > 2 && path[1] == ':' && path_ == "/")
			path_ = path;
		else
			path_ = path_ + path;
		if (path_[path_.size() - 1] != '/')
			path_ += "/";
	}
	HandlePath();
}

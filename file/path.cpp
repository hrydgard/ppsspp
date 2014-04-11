#include "file/path.h"

// Normalize slashes.
void PathBrowser::SetPath(const std::string &path) {
	if (path[0] == '!') {
		path_ = path;
		return;
	}
	path_ = path;
	for (size_t i = 0; i < path_.size(); i++) {
		if (path_[i] == '\\') path_[i] = '/';
	}
	if (!path_.size() || (path_[path_.size() - 1] != '/'))
		path_ += "/";
}

void PathBrowser::GetListing(std::vector<FileInfo> &fileInfo, const char *filter) {
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

	getFilesInDir(path_.c_str(), &fileInfo, filter);
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
		if (path[1] == ':' && path_ == "/")
			path_ = path;
		else
			path_ = path_ + path;
		if (path_[path_.size() - 1] != '/')
			path_ += "/";
	}
}

// UWP STORAGE MANAGER
// Based on 'Path' from PPSSPP
// For updates check: https://github.com/basharast/UWP2Win32

#include <algorithm>
#include <cctype>
#include <cstring>

#include "StoragePath.h"
#include "StorageLog.h"
#include "StorageExtensions.h"

PathUWP::PathUWP(const std::string &str) {
	Init(str);
}

PathUWP::PathUWP(const std::wstring &str) {
	type_ = PathTypeUWP::NATIVE;
	Init(convert(str));
}

void PathUWP::Init(const std::string &str) {
	if (str.empty()) {
		type_ = PathTypeUWP::UNDEFINED;
		path_.clear();
	} else if (starts_with(str, "http://") || starts_with(str, "https://")) {
		type_ = PathTypeUWP::HTTP;
		path_ = str;
	} else {
		type_ = PathTypeUWP::NATIVE;
		path_ = str;
	}

	// Flip all the slashes around. We flip them back on ToWString().
	for (size_t i = 0; i < path_.size(); i++) {
		if (path_[i] == '\\') {
			path_[i] = '/';
		}
	}

	// Don't pop_back if it's just "/".
	if (type_ == PathTypeUWP::NATIVE && path_.size() > 1 && path_.back() == '/') {
		path_.pop_back();
	}
}

// We always use forward slashes internally, we convert to backslash only when
// converted to a wstring.
PathUWP PathUWP::operator /(const std::string &subdir) const {
	// Direct string manipulation.

	if (subdir.empty()) {
		return PathUWP(path_);
	}
	std::string fullPath = path_;
	if (subdir.front() != '/' && (fullPath.empty() || fullPath.back() != '/')) {
		fullPath += "/";
	}
	fullPath += subdir;
	// Prevent adding extra slashes.
	if (fullPath.back() == '/') {
		fullPath.pop_back();
	}
	return PathUWP(fullPath);
}

void PathUWP::operator /=(const std::string &subdir) {
	*this = *this / subdir;
}

PathUWP PathUWP::WithExtraExtension(const std::string &ext) const {
	return PathUWP(path_ + ext);
}

PathUWP PathUWP::WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const {
	if (ends_with(path_, oldExtension)) {
		std::string newPath = path_.substr(0, path_.size() - oldExtension.size());
		return PathUWP(newPath + newExtension);
	} else {
		return PathUWP(*this);
	}
}

PathUWP PathUWP::WithReplacedExtension(const std::string &newExtension) const {
	if (path_.empty()) {
		return PathUWP(*this);
	}
	std::string extension = GetFileExtension();
	std::string newPath = path_.substr(0, path_.size() - extension.size()) + newExtension;
	return PathUWP(newPath);
}

std::string PathUWP::GetFilename() const {
	size_t pos = path_.rfind('/');
	if (pos != std::string::npos) {
		return path_.substr(pos + 1);
	}
	return path_;
}

static std::string GetExtFromString(const std::string &str) {
	size_t pos = str.rfind(".");
	if (pos == std::string::npos) {
		return "";
	}
	size_t slash_pos = str.rfind("/");
	if (slash_pos != std::string::npos && slash_pos > pos) {
		// Don't want to detect "df/file" from "/as.df/file"
		return "";
	}
	std::string ext = str.substr(pos);
	for (size_t i = 0; i < ext.size(); i++) {
		ext[i] = tolower(ext[i]);
	}
	return ext;
}

std::string PathUWP::GetFileExtension() const {
	return GetExtFromString(path_);
}

std::string PathUWP::GetDirectory() const {
	size_t pos = path_.rfind('/');
	if (type_ == PathTypeUWP::HTTP) {
		// Things are a bit different for HTTP, because we probably ended with /.
		if (pos + 1 == path_.size()) {
			pos = path_.rfind('/', pos - 1);
			if (pos != path_.npos && pos > 8) {
				return path_.substr(0, pos + 1);
			}
		}
	}

	if (pos != std::string::npos) {
		if (pos == 0) {
			return "/";  // We're at the root.
		}
		return path_.substr(0, pos);
	} else if (path_.size() == 2 && path_[1] == ':') {
		// Windows fake-root.
		return "/";
	} else {
		// There could be a ':', too. Unlike the slash, let's include that
		// in the returned directory.
		size_t c_pos = path_.rfind(':');
		if (c_pos != std::string::npos) {
			return path_.substr(0, c_pos + 1);
		}
	}
	// No directory components, we're a relative path.
	return path_;
}

bool PathUWP::FilePathContainsNoCase(const std::string &needle) const {
	std::string haystack;
	
	haystack = path_;
	auto pred = [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); };
	auto found = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), pred);
	return found != haystack.end();
}

bool PathUWP::StartsWith(const PathUWP &other) const {
	if (type_ != other.type_) {
		// Bad
		return false;
	}
	return starts_with(path_, other.path_);
}

const std::string &PathUWP::ToString() const {
	return path_;
}

std::wstring PathUWP::ToWString() const {
	std::wstring w = convertToWString(path_);
	for (size_t i = 0; i < w.size(); i++) {
		if (w[i] == '/') {
			w[i] = '\\';
		}
	}
	return w;
}

std::string PathUWP::ToVisualString() const {
	if (type_ == PathTypeUWP::NATIVE) {
		return replace2(path_, "/", "\\");
	} else {
		return path_;
	}
}

bool PathUWP::CanNavigateUp() const {
	if (path_ == "/" || path_.empty()) {
		return false;
	}
	if (type_ == PathTypeUWP::HTTP) {
		size_t rootSlash = path_.find_first_of('/', strlen("https://"));
		if (rootSlash == path_.npos || path_.size() < rootSlash + 1) {
			// This means, "http://server" or "http://server/".  Can't go up.
			return false;
		}
	}
	return true;
}

PathUWP PathUWP::NavigateUp() const {
	std::string dir = GetDirectory();
	return PathUWP(dir);
}

PathUWP PathUWP::GetRootVolume() const {
	if (!IsAbsolute()) {
		// Can't do anything
		return PathUWP(path_);
	}

	if (path_[1] == ':') {
		// Windows path with drive letter
		std::string path = path_.substr(0, 2);
		return PathUWP(path);
	}
	// Support UNC and device paths.
	if (path_[0] == '/' && path_[1] == '/') {
		size_t next = 2;
		if ((path_[2] == '.' || path_[2] == '?') && path_[3] == '/') {
			// Device path, or "\\.\UNC" path, skip the dot and consider the device the root.
			next = 4;
		}

		size_t len = path_.find_first_of('/', next);
		return PathUWP(path_.substr(0, len));
	}
	return PathUWP("/");
}

bool PathUWP::IsAbsolute() const {
	if (path_.empty())
		return true;
	else if (path_.front() == '/')
		return true;
	else if (path_.size() > 3 && path_[1] == ':')
		return true; // Windows path with drive letter
	else
		return false;
}

bool PathUWP::ComputePathTo(const PathUWP &other, std::string &path) const {
	if (other == *this) {
		path.clear();
		return true;
	}

	if (!other.StartsWith(*this)) {
		// Can't do this. Should return an error.
		return false;
	}

	if (*this == other) {
		// Equal, the path is empty.
		path.clear();
		return true;
	}

	if (path_ == "/") {
		path = other.path_.substr(1);
		return true;
	} else {
		path = other.path_.substr(path_.size() + 1);
		return true;
	}
}

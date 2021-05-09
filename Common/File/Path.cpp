#include "Common/File/Path.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/Data/Encoding/Utf8.h"

Path::Path(const std::string &str) {
	if (str.empty()) {
		type_ = PathType::UNDEFINED;
	} else if (startsWith(str, "http://") || startsWith(str, "https://")) {
		type_ = PathType::HTTP;
	} else {
		type_ = PathType::NATIVE;
	}

	Init(str);
}

#if PPSSPP_PLATFORM(WINDOWS)
Path::Path(const std::wstring &str) {
	type_ = PathType::NATIVE;
	Init(ConvertWStringToUTF8(str));
}
#endif

void Path::Init(const std::string &str) {
	path_ = str;

#if PPSSPP_PLATFORM(WINDOWS)
	// Flip all the slashes around. We flip them back on ToWString().
	for (size_t i = 0; i < path_.size(); i++) {
		if (path_[i] == '\\') {
			path_[i] = '/';
		}
	}
#endif

	if (type_ == PathType::NATIVE && !path_.empty() && path_.back() == '/') {
		path_.pop_back();
	}
}

// We always use forward slashes internally, we convert to backslash only when
// converted to a wstring.
Path Path::operator /(const std::string &subdir) const {
	if (subdir.empty()) {
		return Path(path_);
	}
	std::string fullPath = path_;
	if (subdir.front() != '/') {
		fullPath += "/";
	}
	fullPath += subdir;
	// Prevent adding extra slashes.
	if (fullPath.back() == '/') {
		fullPath.pop_back();
	}
	return Path(fullPath);
}

void Path::operator /=(const std::string &subdir) {
	path_ += path_ + "/" + subdir;
}

Path Path::WithExtraExtension(const std::string &ext) const {
	_dbg_assert_(!ext.empty() && ext[0] == '.');
	return Path(path_ + ext);
}

Path Path::WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const {
	_dbg_assert_(!oldExtension.empty() && oldExtension[0] == '.');
	_dbg_assert_(!newExtension.empty() && newExtension[0] == '.');
	if (endsWithNoCase(path_, oldExtension)) {
		std::string newPath = path_.substr(0, path_.size() - oldExtension.size());
		return Path(newPath + newExtension);
	} else {
		return Path(*this);
	}
}

Path Path::WithReplacedExtension(const std::string &newExtension) const {
	_dbg_assert_(!newExtension.empty() && newExtension[0] == '.');
	if (path_.empty()) {
		return Path(*this);
	}
	std::string extension = GetFileExtension();
	std::string newPath = path_.substr(0, path_.size() - extension.size()) + newExtension;
	return Path(newPath);
}

std::string Path::GetFilename() const {
	size_t pos = path_.rfind('/');
	if (pos != std::string::npos) {
		return path_.substr(pos + 1);
	}
	// No directory components, just return the full path.
	return path_;
}

std::string Path::GetFileExtension() const {
	size_t pos = path_.rfind(".");
	if (pos == std::string::npos) {
		return "";
	}
	size_t slash_pos = path_.rfind("/");
	if (slash_pos != std::string::npos && slash_pos > pos) {
		// Don't want to detect "df/file" from "/as.df/file"
		return "";
	}
	std::string ext = path_.substr(pos);
	for (size_t i = 0; i < ext.size(); i++) {
		ext[i] = tolower(ext[i]);
	}
	return ext;
}

std::string Path::GetDirectory() const {
	size_t pos = path_.rfind('/');
	if (pos != std::string::npos) {
		return path_.substr(0, pos);
	} else {
		// There could be a ':', too. Unlike the slash, let's include that
		// in the returned directory.
		size_t c_pos = path_.rfind(':');
		if (c_pos != std::string::npos) {
			return path_.substr(0, c_pos + 1);
		}
	}
	// No directory components, just return the full path.
	return path_;
}

bool Path::FilePathContains(const std::string &needle) const {
	const std::string &haystack = path_;
	return haystack.find(needle) != std::string::npos;
}

bool Path::StartsWith(const Path &other) const {
	if (type_ != other.type_) {
		// Bad
		return false;
	}
	return startsWith(path_, other.path_);
}

const std::string &Path::ToString() const {
	return path_;
}

#if PPSSPP_PLATFORM(WINDOWS)
std::wstring Path::ToWString() const {
	std::wstring w = ConvertUTF8ToWString(path_);
	for (size_t i = 0; i < w.size(); i++) {
		if (w[i] == '/') {
			w[i] = '\\';
		}
	}
	return w;
}
#endif

std::string Path::ToVisualString() const {
	return path_;
}

bool Path::CanNavigateUp() const {
	if (path_ == "/" || path_ == "") {
		return false;
	}
	return true;
}

Path Path::NavigateUp() const {
	std::string dir = GetDirectory();
	return Path(dir);
}

bool Path::IsAbsolute() const {
	if (path_.empty())
		return true;
	else if (path_.front() == '/')
		return true;
	else if (path_.size() > 3 && path_[1] == ':')
		return true; // Windows path with drive letter
	else
		return false;
}

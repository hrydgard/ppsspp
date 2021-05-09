#include "Common/File/Path.h"
#include "Common/StringUtils.h"

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

Path Path::operator /(const std::string &subdir) const {
	// We always use forward slashes internally, we convert to backslash only when
	// converted to a wstring.
	return Path(path_ + "/" + subdir);
}

void Path::operator /=(const std::string &subdir) {
	path_ += path_ + "/" + subdir;
}

Path Path::WithExtraExtension(const std::string &ext) const {
	return Path(path_ + "." + ext);
}

Path Path::WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const {
	if (endsWithNoCase(path_, "." + oldExtension)) {
		std::string newPath = path_.substr(path_.size() - oldExtension.size() - 1);
		return Path(newPath + "." + newExtension);
	} else {
		return Path(*this);
	}
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
	std::string ext = path_.substr(pos + 1);
	for (size_t i = 0; i < ext.size(); i++) {
		ext[i] = tolower(ext[i]);
	}
	return ext;
}

Path Path::Directory() const {
	std::string directory;
	SplitPath(path_, &directory, nullptr, nullptr);
	return Path(directory);
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

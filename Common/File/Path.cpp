#include "ppsspp_config.h"
#include <cstring>

#include "Common/File/Path.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/Data/Encoding/Utf8.h"

#include "android/jni/app-android.h"
#include "android/jni/AndroidContentURI.h"

Path::Path(const std::string &str) {
	if (str.empty()) {
		type_ = PathType::UNDEFINED;
	} else if (startsWith(str, "http://") || startsWith(str, "https://")) {
		type_ = PathType::HTTP;
	} else if (Android_IsContentUri(str)) {
		type_ = PathType::CONTENT_URI;
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

	// Don't pop_back if it's just "/".
	if (type_ == PathType::NATIVE && path_.size() > 1 && path_.back() == '/') {
		path_.pop_back();
	}
}

// We always use forward slashes internally, we convert to backslash only when
// converted to a wstring.
Path Path::operator /(const std::string &subdir) const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return Path(uri.WithComponent(subdir).ToString());
	}

	// Direct string manipulation.

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
	*this = *this / subdir;
}

Path Path::WithExtraExtension(const std::string &ext) const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return Path(uri.WithExtraExtension(ext).ToString());
	}

	_dbg_assert_(!ext.empty() && ext[0] == '.');
	return Path(path_ + ext);
}

Path Path::WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return Path(uri.WithReplacedExtension(oldExtension, newExtension).ToString());
	}

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
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return uri.GetLastPart();
	}
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

std::string Path::GetFileExtension() const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return GetExtFromString(uri.FilePath());
	}
	return GetExtFromString(path_);
}

std::string Path::GetDirectory() const {
	if (type_ == PathType::CONTENT_URI) {
		// Unclear how meaningful this is.
		AndroidContentURI uri(path_);
		uri.NavigateUp();
		return uri.ToString();
	}

	size_t pos = path_.rfind('/');
	if (type_ == PathType::HTTP) {
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
#if PPSSPP_PLATFORM(WINDOWS)
	} else if (path_.size() == 2 && path_[1] == ':') {
		// Windows fake-root.
		return "/";
#endif
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

bool Path::FilePathContains(const std::string &needle) const {
	std::string haystack;
	if (type_ == PathType::CONTENT_URI) {
		haystack = AndroidContentURI(path_).FilePath();
	} else {
		haystack = path_;
	}
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
	if (type_ == PathType::CONTENT_URI) {
		return AndroidContentURI(path_).ToVisualString();
	}
	return path_;
}

bool Path::CanNavigateUp() const {
	if (type_ == PathType::CONTENT_URI) {
		return AndroidContentURI(path_).CanNavigateUp();
	}

	if (path_ == "/" || path_ == "") {
		return false;
	}
	if (type_ == PathType::HTTP) {
		size_t rootSlash = path_.find_first_of('/', strlen("https://"));
		if (rootSlash == path_.npos || path_.size() < rootSlash + 1) {
			// This means, "http://server" or "http://server/".  Can't go up.
			return false;
		}
	}
	return true;
}

Path Path::NavigateUp() const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		uri.NavigateUp();
		return Path(uri.ToString());
	}
	std::string dir = GetDirectory();
	return Path(dir);
}

Path Path::GetRootVolume() const {
	if (!IsAbsolute()) {
		// Can't do anything
		return Path(path_);
	}

	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		AndroidContentURI rootPath = uri.WithRootFilePath("");
		return Path(rootPath.ToString());
	}

#if PPSSPP_PLATFORM(WINDOWS)
	if (path_[1] == ':') {
		// Windows path with drive letter
		std::string path = path_.substr(0, 2);
		return Path(path);
	}
#endif

	return Path("/");
}

bool Path::IsAbsolute() const {
	if (type_ == PathType::CONTENT_URI) {
		// These don't exist in relative form.
		return true;
	}

	if (path_.empty())
		return true;
	else if (path_.front() == '/')
		return true;
#if PPSSPP_PLATFORM(WINDOWS)
	else if (path_.size() > 3 && path_[1] == ':')
		return true; // Windows path with drive letter
#endif
	else
		return false;
}

std::string Path::PathTo(const Path &other) {
	if (!other.StartsWith(*this)) {
		// Can't do this. Should return an error.
		return std::string();
	}

	std::string diff;

	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI a(path_);
		AndroidContentURI b(other.path_);
		if (a.RootPath() != b.RootPath()) {
			// No common root, can't do anything
			return std::string();
		}
		diff = a.PathTo(b);
	} else if (path_ == "/") {
		diff = other.path_.substr(1);
	} else {
		diff = other.path_.substr(path_.size() + 1);
	}

	return diff;
}

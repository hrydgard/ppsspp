#include "ppsspp_config.h"

#include <algorithm>  // for std::search
#include <cctype>
#include <cstring>

#include "Common/File/Path.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"
#include "Common/Data/Encoding/Utf8.h"

#include "android/jni/app-android.h"

#if PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
#include "UWP/UWPHelpers/StorageManager.h"
#endif

#if HOST_IS_CASE_SENSITIVE
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

Path::Path(std::string_view str) {
	Init(str);
}

#if PPSSPP_PLATFORM(WINDOWS)
Path::Path(const std::wstring &str) {
	type_ = PathType::NATIVE;
	Init(ConvertWStringToUTF8(str));
}
#endif

void Path::Init(std::string_view str) {
	if (str.empty()) {
		type_ = PathType::UNDEFINED;
		path_.clear();
	} else if (startsWith(str, "http://") || startsWith(str, "https://")) {
		type_ = PathType::HTTP;
		path_ = str;
	} else if (Android_IsContentUri(str)) {
		// Content URIs on non scoped-storage (and possibly other cases?) can contain
		// "raw:/" URIs inside. This happens when picking the Download folder using the folder browser
		// on Android 9.
		// Here's an example:
		// content://com.android.providers.downloads.documents/tree/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2Fp/document/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2Fp
		//
		// Since this is a legacy use case, I think it's safe enough to just detect this
		// and flip it to a NATIVE url and hope for the best.
		AndroidContentURI uri(str);
		if (startsWith(uri.FilePath(), "raw:/")) {
			INFO_LOG(Log::System, "Raw path detected: %s", uri.FilePath().c_str());
			path_ = uri.FilePath().substr(4);
			type_ = PathType::NATIVE;
		} else {
			// A normal content URI path.
			type_ = PathType::CONTENT_URI;
			path_ = str;
		}
	} else {
		type_ = PathType::NATIVE;
		path_ = str;
	}

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
Path Path::operator /(std::string_view subdir) const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return Path(uri.WithComponent(subdir).ToString());
	}

	// Direct string manipulation.

	if (subdir.empty()) {
		return Path(path_);
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
	return Path(fullPath);
}

void Path::operator /=(std::string_view subdir) {
	*this = *this / subdir;
}

Path Path::WithExtraExtension(std::string_view ext) const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return Path(uri.WithExtraExtension(ext).ToString());
	}

	_dbg_assert_(!ext.empty() && ext[0] == '.');
	return Path(path_ + std::string(ext));
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
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return Path(uri.WithReplacedExtension(newExtension).ToString());
	}

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

static std::string GetExtFromString(std::string_view str) {
	size_t pos = str.rfind(".");
	if (pos == std::string::npos) {
		return "";
	}
	size_t slash_pos = str.rfind("/");
	if (slash_pos != std::string::npos && slash_pos > pos) {
		// Don't want to detect "df/file" from "/as.df/file"
		return "";
	}
	std::string ext(str.substr(pos));
	for (size_t i = 0; i < ext.size(); i++) {
		ext[i] = tolower(ext[i]);
	}
	return ext;
}

std::string Path::GetFileExtension() const {
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI uri(path_);
		return uri.GetFileExtension();
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

bool Path::FilePathContainsNoCase(std::string_view needle) const {
	std::string haystack;
	if (type_ == PathType::CONTENT_URI) {
		haystack = AndroidContentURI(path_).FilePath();
	} else {
		haystack = path_;
	}
	auto pred = [](char ch1, char ch2) { return std::toupper(ch1) == std::toupper(ch2); };
	auto found = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), pred);
	return found != haystack.end();
}

bool Path::StartsWith(const Path &other) const {
	if (other.empty()) {
		return true;
	}
	if (type_ != other.type_) {
		// Bad
		return false;
	}
	return startsWith(path_, other.path_);
}

bool Path::StartsWithGlobalAndNotEqual(const Path &other) const {
	if (other.empty()) {
		return true;
	}
	if (type_ != other.type_) {
		// Bad
		return false;
	}
	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI a(path_);
		AndroidContentURI b(other.path_);
		std::string aLast = a.GetLastPart();
		std::string bLast = b.GetLastPart();
		if (aLast == bLast) {
			return false;
		}
		return CountChar(aLast, '/') != CountChar(bLast, '/') && startsWith(aLast, bLast);
	}
	return *this != other && StartsWith(other);
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
std::string Path::ToCString() const {
	std::string w = path_;
	for (size_t i = 0; i < w.size(); i++) {
		if (w[i] == '/') {
			w[i] = '\\';
		}
	}
	return w;
}
#endif

std::string Path::ToVisualString(const char *relativeRoot) const {
	if (type_ == PathType::CONTENT_URI) {
		return AndroidContentURI(path_).ToVisualString();
#if PPSSPP_PLATFORM(WINDOWS)
	} else if (type_ == PathType::NATIVE) {
#if PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
		return GetPreviewPath(path_);
#else
		// It can be useful to show the path as relative to the memstick
		if (relativeRoot) {
			std::string root = ReplaceAll(relativeRoot, "/", "\\");
			std::string path = ReplaceAll(path_, "/", "\\");
			if (startsWithNoCase(path, root)) {
				return path.substr(root.size());
			} else {
				return path;
			}
		} else {
			return ReplaceAll(path_, "/", "\\");
		}
#endif
#else
		if (relativeRoot) {
			std::string root = relativeRoot;
			if (startsWithNoCase(path_, root)) {
				return path_.substr(root.size());
			} else {
				return path_;
			}
		} else {
			return path_;
		}
#endif
	} else {
		return path_;
	}
}

bool Path::CanNavigateUp() const {
	if (type_ == PathType::CONTENT_URI) {
		return AndroidContentURI(path_).CanNavigateUp();
	} else if (type_ == PathType::HTTP) {
		size_t rootSlash = path_.find_first_of('/', strlen("https://"));
		if (rootSlash == path_.npos || path_.size() == rootSlash + 1) {
			// This means, "http://server" or "http://server/".  Can't go up.
			return false;
		}
	}
	if (path_ == "/" || path_.empty()) {
		return false;
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
	// Support UNC and device paths.
	if (path_[0] == '/' && path_[1] == '/') {
		size_t next = 2;
		if ((path_[2] == '.' || path_[2] == '?') && path_[3] == '/') {
			// Device path, or "\\.\UNC" path, skip the dot and consider the device the root.
			next = 4;
		}

		size_t len = path_.find_first_of('/', next);
		return Path(path_.substr(0, len));
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

bool Path::ComputePathTo(const Path &other, std::string &path) const {
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

	if (type_ == PathType::CONTENT_URI) {
		AndroidContentURI a(path_);
		AndroidContentURI b(other.path_);
		if (a.RootPath() != b.RootPath()) {
			// No common root, can't do anything
			return false;
		}
		return a.ComputePathTo(b, path);
	} else if (path_ == "/") {
		path = other.path_.substr(1);
		return true;
	} else {
		path = other.path_.substr(path_.size() + 1);
		return true;
	}
}

static bool FixFilenameCase(const std::string &path, std::string &filename) {
#if _WIN32
	// We don't support case-insensitive file systems on Windows.
	return true;
#else

	// Are we lucky?
	if (File::Exists(Path(path + filename)))
		return true;

	size_t filenameSize = filename.size();  // size in bytes, not characters
	for (size_t i = 0; i < filenameSize; i++)
	{
		filename[i] = tolower(filename[i]);
	}

	//TODO: lookup filename in cache for "path"
	struct dirent *result = NULL;

	DIR *dirp = opendir(path.c_str());
	if (!dirp)
		return false;

	bool retValue = false;

	while ((result = readdir(dirp)))
	{
		if (strlen(result->d_name) != filenameSize)
			continue;

		size_t i;
		for (i = 0; i < filenameSize; i++)
		{
			if (filename[i] != tolower(result->d_name[i]))
				break;
		}

		if (i < filenameSize)
			continue;

		filename = result->d_name;
		retValue = true;
	}

	closedir(dirp);

	return retValue;
#endif
}

bool FixPathCase(const Path &realBasePath, std::string &path, FixPathCaseBehavior behavior) {
#if _WIN32
	return true;
#else

	if (realBasePath.Type() == PathType::CONTENT_URI) {
		// Nothing to do. These are already case insensitive, I think.
		return true;
	}

	std::string basePath = realBasePath.ToString();

	size_t len = path.size();

	if (len == 0)
		return true;

	if (path[len - 1] == '/') {
		len--;

		if (len == 0)
			return true;
	}

	std::string fullPath;
	fullPath.reserve(basePath.size() + len + 1);
	fullPath.append(basePath);

	size_t start = 0;
	while (start < len) {
		size_t i = path.find('/', start);
		if (i == std::string::npos)
			i = len;

		if (i > start) {
			std::string component = path.substr(start, i - start);

			// Fix case and stop on nonexistant path component
			if (FixFilenameCase(fullPath, component) == false) {
				// Still counts as success if partial matches allowed or if this
				// is the last component and only the ones before it are required
				return (behavior == FPC_PARTIAL_ALLOWED || (behavior == FPC_PATH_MUST_EXIST && i >= len));
			}

			path.replace(start, i - start, component);

			fullPath.append(1, '/');
			fullPath.append(component);
		}

		start = i + 1;
	}

	return true;
#endif
}

#pragma once

#include "ppsspp_config.h"

#include <string>
#include <string_view>

#if defined(__APPLE__)

#if TARGET_OS_IPHONE
#define HOST_IS_CASE_SENSITIVE 1
#elif TARGET_IPHONE_SIMULATOR
#define HOST_IS_CASE_SENSITIVE 0
#else
// Mac OSX case sensitivity defaults off, but is user configurable (when
// creating a filesytem), so assume the worst:
#define HOST_IS_CASE_SENSITIVE 1
#endif

#elif defined(_WIN32)
#define HOST_IS_CASE_SENSITIVE 0

#else  // Android, Linux, BSD (and the rest?)
#define HOST_IS_CASE_SENSITIVE 1

#endif

enum class PathType {
	UNDEFINED = 0,
	NATIVE = 1,  // Can be relative.
	CONTENT_URI = 2,  // Android only. Can only be absolute!
	HTTP = 3,  // http://, https://
};

// Windows paths are always stored with '/' slashes in a Path.
// On .ToWString(), they are flipped back to '\'.

class Path {
private:
	void Init(std::string_view str);

public:
	Path() : type_(PathType::UNDEFINED) {}
	explicit Path(std::string_view str);

#if PPSSPP_PLATFORM(WINDOWS)
	explicit Path(const std::wstring &str);
#endif

	PathType Type() const {
		return type_;
	}
	bool IsLocalType() const {
		return type_ == PathType::NATIVE || type_ == PathType::CONTENT_URI;
	}

	bool Valid() const { return !path_.empty(); }
	bool IsRoot() const { return path_ == "/"; }  // Special value - only path that can end in a slash.

	// Some std::string emulation for simplicity.
	bool empty() const { return !Valid(); }
	void clear() {
		type_ = PathType::UNDEFINED;
		path_.clear();
	}
	size_t size() const {
		return path_.size();
	}

	// WARNING: Potentially unsafe usage, if it's not NATIVE.
	const char *c_str() const {
		return path_.c_str();
	}

	bool IsAbsolute() const;

	// Returns a path extended with a subdirectory.
	Path operator /(std::string_view subdir) const;

	// Navigates down into a subdir.
	void operator /=(std::string_view subdir);

	// File extension manipulation.
	Path WithExtraExtension(std::string_view ext) const;
	Path WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const;
	Path WithReplacedExtension(const std::string &newExtension) const;

	std::string GetFilename() const;  // Really, GetLastComponent. Could be a file or directory. Includes the extension.
	std::string GetFileExtension() const;  // Always lowercase return. Includes the dot.
	// Removes the last component.
	std::string GetDirectory() const;

	const std::string &ToString() const;

#if PPSSPP_PLATFORM(WINDOWS)
	std::wstring ToWString() const;
	std::string ToCString() const;  // Flips the slashes back to Windows standard, but string still UTF-8.
#else
	std::string ToCString() const {
		return ToString();
	}
#endif

	// Pass in a relative root to turn the path into a relative path - if it is one!
	std::string ToVisualString(const char *relativeRoot = nullptr) const;

	bool CanNavigateUp() const;
	Path NavigateUp() const;

	// Navigates as far up as possible from this path. If not possible to navigate upwards, returns the same path.
	// Not actually always the root of the volume, especially on systems like Mac and Linux where things are often mounted.
	// For Android directory trees, navigates to the root of the tree.
	Path GetRootVolume() const;

	bool ComputePathTo(const Path &other, std::string &path) const;

	bool operator ==(const Path &other) const {
		return path_ == other.path_ && type_ == other.type_;
	}
	bool operator !=(const Path &other) const {
		return path_ != other.path_ || type_ != other.type_;
	}

	bool FilePathContainsNoCase(std::string_view needle) const;

	bool StartsWith(const Path &other) const;

	bool operator <(const Path &other) const {
		return path_ < other.path_;
	}
	bool operator >(const Path &other) const {
		return path_ > other.path_;
	}

private:
	// The internal representation is currently always the plain string.
	// For CPU efficiency we could keep an AndroidStorageContentURI too,
	// but I don't think the encode/decode cost is significant. We simply create
	// those for processing instead.
	std::string path_;

	PathType type_;
};

// Utility function for parsing out file extensions.
std::string GetExtFromString(std::string_view str);

// Utility function for fixing the case of paths. Only present on Unix-like systems.

#if HOST_IS_CASE_SENSITIVE

enum FixPathCaseBehavior {
	FPC_FILE_MUST_EXIST,  // all path components must exist (rmdir, move from)
	FPC_PATH_MUST_EXIST,  // all except the last one must exist - still tries to fix last one (fopen, move to)
	FPC_PARTIAL_ALLOWED,  // don't care how many exist (mkdir recursive)
};

bool FixPathCase(const Path &basePath, std::string &path, FixPathCaseBehavior behavior);

#endif

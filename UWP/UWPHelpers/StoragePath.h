// UWP STORAGE MANAGER
// Based on 'Path' from PPSSPP
// For updates check: https://github.com/basharast/UWP2Win32

#pragma once

#include <string>

#define HOST_IS_CASE_SENSITIVE 0

enum class PathTypeUWP {
	UNDEFINED = 0,
	NATIVE = 1,  // Can be relative.
	CONTENT_URI = 2,  // Android only. Can only be absolute!
	HTTP = 3,  // http://, https://
};

// Windows paths are always stored with '/' slashes in a Path.
// On .ToWString(), they are flipped back to '\'.

class PathUWP {
private:
	void Init(const std::string &str);

public:
	PathUWP() : type_(PathTypeUWP::UNDEFINED) {}
	explicit PathUWP(const std::string &str);

	explicit PathUWP(const std::wstring &str);

	PathTypeUWP Type() const {
		return type_;
	}

	bool Valid() const { return !path_.empty(); }
	bool IsRoot() const { return path_ == "/"; }  // Special value - only path that can end in a slash.

	// Some std::string emulation for simplicity.
	bool empty() const { return !Valid(); }
	void clear() {
		type_ = PathTypeUWP::UNDEFINED;
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
	PathUWP operator /(const std::string &subdir) const;

	// Navigates down into a subdir.
	void operator /=(const std::string &subdir);

	// File extension manipulation.
	PathUWP WithExtraExtension(const std::string &ext) const;
	PathUWP WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const;
	PathUWP WithReplacedExtension(const std::string &newExtension) const;

	// Removes the last component.
	std::string GetFilename() const;  // Really, GetLastComponent. Could be a file or directory. Includes the extension.
	std::string GetFileExtension() const;  // Always lowercase return. Includes the dot.
	std::string GetDirectory() const;

	const std::string &ToString() const;

	std::wstring ToWString() const;

	std::string ToVisualString() const;

	bool CanNavigateUp() const;
	PathUWP NavigateUp() const;

	// Navigates as far up as possible from this path. If not possible to navigate upwards, returns the same path.
	// Not actually always the root of the volume, especially on systems like Mac and Linux where things are often mounted.
	// For Android directory trees, navigates to the root of the tree.
	PathUWP GetRootVolume() const;

	bool ComputePathTo(const PathUWP&other, std::string &path) const;

	bool operator ==(const PathUWP&other) const {
		return path_ == other.path_ && type_ == other.type_;
	}
	bool operator !=(const PathUWP&other) const {
		return path_ != other.path_ || type_ != other.type_;
	}

	bool FilePathContainsNoCase(const std::string &needle) const;

	bool StartsWith(const PathUWP&other) const;

	bool operator <(const PathUWP&other) const {
		return path_ < other.path_;
	}
	bool operator >(const PathUWP&other) const {
		return path_ > other.path_;
	}

private:
	// The internal representation is currently always the plain string.
	// For CPU efficiency we could keep an AndroidStorageContentURI too,
	// but I don't think the encode/decode cost is significant. We simply create
	// those for processing instead.
	std::string path_;

	PathTypeUWP type_;
};

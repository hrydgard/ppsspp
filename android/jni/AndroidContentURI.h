#pragma once

#include <string>

#include "Common/StringUtils.h"
#include "Common/Net/URL.h"
#include "Common/Log.h"

// Utility to deal with Android storage URIs of the forms:
// content://com.android.externalstorage.documents/tree/primary%3APSP%20ISO
// content://com.android.externalstorage.documents/tree/primary%3APSP%20ISO/document/primary%3APSP%20ISO

// This file compiles on all platforms, to reduce the need for ifdefs.

// I am not 100% sure it's OK to rely on the internal format of file content URIs.
// On the other hand, I'm sure tons of apps would break if these changed, so I think we can
// consider them pretty stable. Additionally, the official Document library just manipulates the URIs
// in similar ways...
class AndroidContentURI {
private:
	std::string provider;
	std::string root;
	std::string file;
public:
	AndroidContentURI() {}
	explicit AndroidContentURI(const std::string &path) {
		Parse(path);
	}

	bool Parse(const std::string &path) {
		const char *prefix = "content://";
		if (!startsWith(path, prefix)) {
			return false;
		}

		std::string components = path.substr(strlen(prefix));

		std::vector<std::string> parts;
		SplitString(components, '/', parts);
		if (parts.size() == 3) {
			// Single file URI.
			provider = parts[0];
			if (parts[1] == "tree") {
				// Single directory URI.
				// Not sure when we encounter these?
				// file empty signals this type.
				root = UriDecode(parts[2]);
				return true;
			} else if (parts[1] == "document") {
				// root empty signals this type.
				file = UriDecode(parts[2]);
				return true;
			} else {
				// What's this?
				return false;
			}
		} else if (parts.size() == 5) {
			// Tree URI
			provider = parts[0];
			if (parts[1] != "tree") {
				return false;
			}
			root = UriDecode(parts[2]);
			if (parts[3] != "document") {
				return false;
			}
			file = UriDecode(parts[4]);
			// Sanity check file path.
			return startsWith(file, root);
		} else {
			// Invalid Content URI
			return false;
		}
	}

	AndroidContentURI WithRootFilePath(const std::string &filePath) {
		if (root.empty()) {
			ERROR_LOG(SYSTEM, "WithRootFilePath cannot be used with single file URIs.");
			return *this;
		}

		AndroidContentURI uri = *this;
		uri.file = uri.root;
		if (!filePath.empty()) {
			uri.file += "/" + filePath;
		}
		return uri;
	}

	AndroidContentURI WithComponent(const std::string &filePath) {
		AndroidContentURI uri = *this;
		uri.file = uri.file + "/" + filePath;
		return uri;
	}

	AndroidContentURI WithExtraExtension(const std::string &extension) {
		AndroidContentURI uri = *this;
		uri.file = uri.file + extension;
		return uri;
	}

	AndroidContentURI WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const {
		_dbg_assert_(!oldExtension.empty() && oldExtension[0] == '.');
		_dbg_assert_(!newExtension.empty() && newExtension[0] == '.');
		AndroidContentURI uri = *this;
		if (endsWithNoCase(file, oldExtension)) {
			uri.file = file.substr(0, file.size() - oldExtension.size()) + newExtension;
		}
		return uri;
	}

	AndroidContentURI WithReplacedExtension(const std::string &newExtension) const {
		_dbg_assert_(!newExtension.empty() && newExtension[0] == '.');
		AndroidContentURI uri = *this;
		if (file.empty()) {
			return uri;
		}
		std::string extension = GetFileExtension();
		uri.file = file.substr(0, file.size() - extension.size()) + newExtension;
		return uri;
	}

	bool CanNavigateUp() const {
		if (root.empty()) {
			return false;
		}
		return file.size() > root.size();
	}

	// Only goes downwards in hierarchies. No ".." will ever be generated.
	bool ComputePathTo(const AndroidContentURI &other, std::string &path) const {
		size_t offset = FilePath().size() + 1;
		std::string otherFilePath = other.FilePath();
		if (offset >= otherFilePath.size()) {
			ERROR_LOG(SYSTEM, "Bad call to PathTo. '%s' -> '%s'", FilePath().c_str(), other.FilePath().c_str());
			return false;
		}

		path = other.FilePath().substr(FilePath().size() + 1);
		return true;
	}

	std::string GetFileExtension() const {
		size_t pos = file.rfind(".");
		if (pos == std::string::npos) {
			return "";
		}
		size_t slash_pos = file.rfind("/");
		if (slash_pos != std::string::npos && slash_pos > pos) {
			// Don't want to detect "df/file" from "/as.df/file"
			return "";
		}
		std::string ext = file.substr(pos);
		for (size_t i = 0; i < ext.size(); i++) {
			ext[i] = tolower(ext[i]);
		}
		return ext;
	}

	std::string GetLastPart() const {
		if (file.empty()) {
			// Can't do anything anyway.
			return std::string();
		}

		if (!CanNavigateUp()) {
			size_t colon = file.rfind(':');
			if (colon == std::string::npos) {
				return std::string();
			}
			return file.substr(colon + 1);
		}

		size_t slash = file.rfind('/');
		if (slash == std::string::npos) {
			return std::string();
		}

		std::string part = file.substr(slash + 1);
		return part;
	}

	bool NavigateUp() {
		if (!CanNavigateUp()) {
			return false;
		}

		size_t slash = file.rfind('/');
		if (slash == std::string::npos) {
			return false;
		}

		file = file.substr(0, slash);
		return true;
	}

	bool TreeContains(const AndroidContentURI &fileURI) {
		if (root.empty()) {
			return false;
		}
		return startsWith(fileURI.file, root);
	}

	std::string ToString() const {
		if (file.empty()) {
			// Tree URI
			return StringFromFormat("content://%s/tree/%s", provider.c_str(), UriEncode(root).c_str());
		} else if (root.empty()) {
			// Single file URI
			return StringFromFormat("content://%s/document/%s", provider.c_str(), UriEncode(file).c_str());
		} else {
			// File URI from Tree
			return StringFromFormat("content://%s/tree/%s/document/%s", provider.c_str(), UriEncode(root).c_str(), UriEncode(file).c_str());
		}
	}

	// Never store the output of this, only show it to the user.
	std::string ToVisualString() const {
		return file;
	}

	const std::string &FilePath() const {
		return file;
	}

	const std::string &RootPath() const {
		return root.empty() ? file : root;
	}
};

#pragma once

#include <string>

#include "Common/StringUtils.h"
#include "Common/Net/URL.h"

// Utility to deal with Android storage URIs of the forms:
// content://com.android.externalstorage.documents/tree/primary%3APSP%20ISO
// content://com.android.externalstorage.documents/tree/primary%3APSP%20ISO/document/primary%3APSP%20ISO

// This file compiles on all platforms, to reduce the need for ifdefs.

// I am not 100% sure it's OK to rely on the internal format of file content URIs.
// On the other hand, I'm sure tons of apps would break if these changed, so I think we can
// consider them pretty stable. Additionally, the official Document library just manipulates the URIs
// in similar ways...
class AndroidStorageContentURI {
private:
	std::string provider;
	std::string root;
	std::string file;
public:
	AndroidStorageContentURI() {}
	explicit AndroidStorageContentURI(const std::string &path) {
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
			provider = parts[0];
			if (parts[1] != "tree") {
				return false;
			}
			root = UriDecode(parts[2]);
			return true;
		} else if (parts.size() == 5) {
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

	AndroidStorageContentURI WithFilePath(const std::string &filePath) {
		AndroidStorageContentURI uri = *this;
		uri.file = uri.root + "/" + filePath;
		return uri;
	}

	bool IsTreeURI() const {
		return file.empty();
	}

	bool CanNavigateUp() const {
		return file.size() > root.size();
	}

	std::string GetLastPart() const {
		if (IsTreeURI()) {
			// Can't do anything anyway.
			return std::string();
		}

		if (!CanNavigateUp()) {
			// Kinda useless to get the "primary:" volume.
			return std::string();
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

	bool TreeContains(const AndroidStorageContentURI &fileURI) {
		if (!IsTreeURI()) {
			return false;
		}
		return startsWith(fileURI.file, root);
	}

	std::string ToString() const {
		if (file.empty()) {
			// Tree URI
			return StringFromFormat("content://%s/tree/%s", provider.c_str(), UriEncode(root).c_str());
		} else {
			// File URI
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
		return root;
	}
};

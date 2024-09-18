#include "Common/File/AndroidContentURI.h"

bool AndroidContentURI::Parse(std::string_view path) {
	const char *prefix = "content://";
	if (!startsWith(path, prefix)) {
		return false;
	}

	std::string_view components = path.substr(strlen(prefix));

	std::vector<std::string_view> parts;
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

AndroidContentURI AndroidContentURI::WithRootFilePath(const std::string &filePath) {
	if (root.empty()) {
		ERROR_LOG(Log::System, "WithRootFilePath cannot be used with single file URIs.");
		return *this;
	}

	AndroidContentURI uri = *this;
	uri.file = uri.root;
	if (!filePath.empty()) {
		uri.file += "/" + filePath;
	}
	return uri;
}

AndroidContentURI AndroidContentURI::WithComponent(std::string_view filePath) {
	AndroidContentURI uri = *this;
	if (uri.file.empty()) {
		// Not sure what to do.
		return uri;
	}
	if (uri.file.back() == ':') {
		// Special case handling for Document URIs: Treat the ':' as a directory separator too (but preserved in the filename).
		uri.file.append(filePath);
	} else {
		uri.file.push_back('/');
		uri.file.append(filePath);
	}
	return uri;
}

AndroidContentURI AndroidContentURI::WithExtraExtension(std::string_view extension) {
	AndroidContentURI uri = *this;
	uri.file.append(extension);
	return uri;
}

AndroidContentURI AndroidContentURI::WithReplacedExtension(const std::string &oldExtension, const std::string &newExtension) const {
	_dbg_assert_(!oldExtension.empty() && oldExtension[0] == '.');
	_dbg_assert_(!newExtension.empty() && newExtension[0] == '.');
	AndroidContentURI uri = *this;
	if (endsWithNoCase(file, oldExtension)) {
		uri.file = file.substr(0, file.size() - oldExtension.size()) + newExtension;
	}
	return uri;
}

AndroidContentURI AndroidContentURI::WithReplacedExtension(const std::string &newExtension) const {
	_dbg_assert_(!newExtension.empty() && newExtension[0] == '.');
	AndroidContentURI uri = *this;
	if (file.empty()) {
		return uri;
	}
	std::string extension = GetFileExtension();
	uri.file = file.substr(0, file.size() - extension.size()) + newExtension;
	return uri;
}

bool AndroidContentURI::CanNavigateUp() const {
	if (IsTreeURI()) {
		return file.size() > root.size();
	} else {
		return file.find(':') != std::string::npos && file.back() != ':';
	}
}

// Only goes downwards in hierarchies. No ".." will ever be generated.
bool AndroidContentURI::ComputePathTo(const AndroidContentURI &other, std::string &path) const {
	size_t offset = FilePath().size() + 1;
	const auto &otherFilePath = other.FilePath();
	if (offset >= otherFilePath.size()) {
		ERROR_LOG(Log::System, "Bad call to PathTo. '%s' -> '%s'", FilePath().c_str(), other.FilePath().c_str());
		return false;
	}

	path = other.FilePath().substr(FilePath().size() + 1);
	return true;
}

std::string AndroidContentURI::GetFileExtension() const {
	size_t pos = file.rfind('.');
	if (pos == std::string::npos) {
		return "";
	}
	size_t slash_pos = file.rfind('/');
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

std::string AndroidContentURI::GetLastPart() const {
	if (file.empty()) {
		// Can't do anything anyway.
		return std::string();
	}

	if (!CanNavigateUp()) {
		size_t colon = file.rfind(':');
		if (colon == std::string::npos) {
			return std::string();
		}
		if (file.back() == ':') {
			return file;
		}
		return file.substr(colon + 1);
	}

	size_t slash = file.rfind('/');
	if (slash == std::string::npos) {
		// ok, look for the final colon. If it's the last char, we would have been caught above in !CanNavigateUp.
		size_t colon = file.rfind(':');
		if (colon == std::string::npos) {
			return std::string();
		}
		return file.substr(colon + 1);
	}

	std::string part = file.substr(slash + 1);
	return part;
}

bool AndroidContentURI::NavigateUp() {
	if (!CanNavigateUp()) {
		return false;
	}

	size_t slash = file.rfind('/');
	if (slash == std::string::npos) {
		// ok, look for the final colon.
		size_t colon = file.rfind(':');
		if (colon == std::string::npos) {
			return false;
		}
		file = file.substr(0, colon + 1);  // Note: we include the colon in these paths.
		return true;
	}

	file = file.substr(0, slash);
	return true;
}


std::string AndroidContentURI::ToString() const {
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

#include "ppsspp_config.h"

#include <cctype>
#include <cstdio>

#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/File/DirListing.h"
#include "Common/File/VFS/DirectoryReader.h"

DirectoryReader::DirectoryReader(const Path &path, bool ignoreCase) {
	path_ = path;
#if !PPSSPP_PLATFORM(WINDOWS)
	// Windows file systems already ignore case.
	ignoreCase_ = ignoreCase;
#endif
}

static std::string ToLowerASCII(std::string_view str) {
	std::string lower(str);
	for (char &c : lower) {
		c = (char)tolower((unsigned char)c);
	}
	return lower;
}

void DirectoryReader::IndexDirectory(const Path &dir, const std::string &prefix) {
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(dir, &files);
	for (const File::FileInfo &file : files) {
		const std::string relative = prefix + file.name;
		if (file.isDirectory) {
			IndexDirectory(file.fullName, relative + "/");
		} else {
			lowerIndex_[ToLowerASCII(relative)] = relative;
		}
	}
}

bool DirectoryReader::FindIgnoringCase(std::string_view path, std::string *actualPath) {
	if (!ignoreCase_) {
		return false;
	}
	std::lock_guard<std::mutex> guard(indexLock_);
	if (!indexBuilt_) {
		// Only done on the first miss, so a directory where every name matches never pays for it.
		IndexDirectory(path_, "");
		indexBuilt_ = true;
		INFO_LOG(Log::IO, "Indexed %d files in %s to find names that differ in case", (int)lowerIndex_.size(), path_.c_str());
	}
	auto it = lowerIndex_.find(ToLowerASCII(path));
	if (it == lowerIndex_.end()) {
		return false;
	}
	*actualPath = it->second;
	if (caseFixCount_++ < 5) {
		WARN_LOG(Log::IO, "'%.*s' differs in case from the file '%s' in %s, which only works on some file systems", STR_VIEW(path), actualPath->c_str(), path_.c_str());
	}
	return true;
}

uint8_t *DirectoryReader::ReadFile(std::string_view path, size_t *size) {
	const bool absolute = Path(path).StartsWith(path_);
	Path new_path = absolute ? Path(path) : path_ / path;
	uint8_t *data = File::ReadLocalFile(new_path, size);
	std::string actualPath;
	if (!data && !absolute && FindIgnoringCase(path, &actualPath)) {
		data = File::ReadLocalFile(path_ / actualPath, size);
	}
	return data;
}

bool DirectoryReader::GetFileListing(std::string_view path, std::vector<File::FileInfo> *listing, const char *filter = nullptr) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;

	File::FileInfo info;
	if (!File::GetFileInfo(new_path, &info))
		return false;

	if (info.isDirectory) {
		File::GetFilesInDir(new_path, listing, filter);
		return true;
	}
	return false;
}

bool DirectoryReader::GetFileInfo(std::string_view path, File::FileInfo *info) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::GetFileInfo(new_path, info);
}

bool DirectoryReader::Exists(std::string_view path) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::Exists(new_path);
}

class DirectoryReaderFileReference : public VFSFileReference {
public:
	Path path;
};

class DirectoryReaderOpenFile : public VFSOpenFile {
public:
	~DirectoryReaderOpenFile() {
		_dbg_assert_(file == nullptr);
	}
	FILE *file = nullptr;
};

VFSFileReference *DirectoryReader::GetFile(std::string_view path) {
	Path filePath = path_ / path;
	if (!File::Exists(filePath)) {
		std::string actualPath;
		if (!FindIgnoringCase(path, &actualPath)) {
			return nullptr;
		}
		filePath = path_ / actualPath;
	}

	DirectoryReaderFileReference *reference = new DirectoryReaderFileReference();
	reference->path = filePath;
	return reference;
}

bool DirectoryReader::GetFileInfo(VFSFileReference *vfsReference, File::FileInfo *fileInfo) {
	DirectoryReaderFileReference *reference = (DirectoryReaderFileReference *)vfsReference;
	return File::GetFileInfo(reference->path, fileInfo);
}

void DirectoryReader::ReleaseFile(VFSFileReference *vfsReference) {
	DirectoryReaderFileReference *reference = (DirectoryReaderFileReference *)vfsReference;
	delete reference;
}

VFSOpenFile *DirectoryReader::OpenFileForRead(VFSFileReference *vfsReference, size_t *size) {
	DirectoryReaderFileReference *reference = (DirectoryReaderFileReference *)vfsReference;
	FILE *file = File::OpenCFile(reference->path, "rb");
	if (!file) {
		return nullptr;
	}
	File::Fseek(file, 0, SEEK_END);
	*size = File::Ftell(file);
	File::Fseek(file, 0, SEEK_SET);
	DirectoryReaderOpenFile *openFile = new DirectoryReaderOpenFile();
	openFile->file = file;
	return openFile;
}

void DirectoryReader::Rewind(VFSOpenFile *vfsOpenFile) {
	DirectoryReaderOpenFile *openFile = (DirectoryReaderOpenFile *)vfsOpenFile;
	File::Fseek(openFile->file, 0, SEEK_SET);
}

size_t DirectoryReader::Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) {
	DirectoryReaderOpenFile *openFile = (DirectoryReaderOpenFile *)vfsOpenFile;
	return fread(buffer, 1, length, openFile->file);
}

void DirectoryReader::CloseFile(VFSOpenFile *vfsOpenFile) {
	DirectoryReaderOpenFile *openFile = (DirectoryReaderOpenFile *)vfsOpenFile;
	_dbg_assert_(openFile);
	if (!openFile) {
		return;
	}
	_dbg_assert_(openFile->file != nullptr);
	if (openFile->file) {
		fclose(openFile->file);
		openFile->file = nullptr;
	}
	delete openFile;
}

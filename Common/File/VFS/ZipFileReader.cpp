#include <algorithm>
#include <ctype.h>
#include <set>
#include <cstdio>
#include <cstring>

#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif

#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/StringUtils.h"

ZipFileReader *ZipFileReader::Create(const Path &zipFile, const char *inZipPath, bool logErrors) {
	int error = 0;
	zip *zip_file;
	if (zipFile.Type() == PathType::CONTENT_URI) {
		int fd = File::OpenFD(zipFile, File::OPEN_READ);
		if (!fd) {
			if (logErrors) {
				ERROR_LOG(IO, "Failed to open FD for '%s' as zip file", zipFile.c_str());
			}
			return nullptr;
		}
		zip_file = zip_fdopen(fd, 0, &error);
	} else {
		zip_file = zip_open(zipFile.c_str(), 0, &error);
	}

	if (!zip_file) {
		if (logErrors) {
			ERROR_LOG(IO, "Failed to open %s as a zip file", zipFile.c_str());
		}
		return nullptr;
	}

	ZipFileReader *reader = new ZipFileReader();
	reader->zip_file_ = zip_file;
	truncate_cpy(reader->inZipPath_, inZipPath);
	return reader;
}

ZipFileReader::~ZipFileReader() {
	std::lock_guard<std::mutex> guard(lock_);
	zip_close(zip_file_);
}

uint8_t *ZipFileReader::ReadFile(const char *path, size_t *size) {
	char temp_path[2048];
	snprintf(temp_path, sizeof(temp_path), "%s%s", inZipPath_, path);

	std::lock_guard<std::mutex> guard(lock_);
	// Figure out the file size first.
	struct zip_stat zstat;
	zip_stat(zip_file_, temp_path, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED, &zstat);
	zip_file *file = zip_fopen(zip_file_, temp_path, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED);
	if (!file) {
		ERROR_LOG(IO, "Error opening %s from ZIP", temp_path);
		return 0;
	}
	uint8_t *contents = new uint8_t[zstat.size + 1];
	zip_fread(file, contents, zstat.size);
	zip_fclose(file);
	contents[zstat.size] = 0;

	*size = zstat.size;
	return contents;
}

bool ZipFileReader::GetFileListing(const char *orig_path, std::vector<File::FileInfo> *listing, const char *filter = 0) {
	char path[2048];
	snprintf(path, sizeof(path), "%s%s", inZipPath_, orig_path);

	std::set<std::string> filters;
	std::string tmp;
	if (filter) {
		while (*filter) {
			if (*filter == ':') {
				filters.insert("." + tmp);
				tmp.clear();
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
	}

	if (tmp.size())
		filters.insert("." + tmp);

	// We just loop through the whole ZIP file and deduce what files are in this directory, and what subdirectories there are.
	std::set<std::string> files;
	std::set<std::string> directories;
	GetZipListings(path, files, directories);

	for (auto diter = directories.begin(); diter != directories.end(); ++diter) {
		File::FileInfo info;
		info.name = *diter;

		// Remove the "inzip" part of the fullname.
		info.fullName = Path(std::string(path).substr(strlen(inZipPath_))) / *diter;
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = true;
		listing->push_back(info);
	}

	for (auto fiter = files.begin(); fiter != files.end(); ++fiter) {
		std::string fpath = path;
		File::FileInfo info;
		info.name = *fiter;
		info.fullName = Path(std::string(path).substr(strlen(inZipPath_))) / *fiter;
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = false;
		std::string ext = info.fullName.GetFileExtension();
		if (filter) {
			if (filters.find(ext) == filters.end()) {
				continue;
			}
		}
		listing->push_back(info);
	}

	std::sort(listing->begin(), listing->end());
	return true;
}

void ZipFileReader::GetZipListings(const char *path, std::set<std::string> &files, std::set<std::string> &directories) {
	size_t pathlen = strlen(path);
	if (path[pathlen - 1] == '/')
		pathlen--;

	std::lock_guard<std::mutex> guard(lock_);
	int numFiles = zip_get_num_files(zip_file_);
	for (int i = 0; i < numFiles; i++) {
		const char* name = zip_get_name(zip_file_, i, 0);
		if (!name)
			continue;
		if (!memcmp(name, path, pathlen)) {
			// The prefix is right. Let's see if this is a file or path.
			const char *slashPos = strchr(name + pathlen + 1, '/');
			if (slashPos != 0) {
				// A directory.
				std::string dirName = std::string(name + pathlen + 1, slashPos - (name + pathlen + 1));
				directories.insert(dirName);
			} else if (name[pathlen] == '/') {
				const char *fn = name + pathlen + 1;
				files.insert(std::string(fn));
			}  // else, it was a file with the same prefix as the path. like langregion.ini next to lang/.
		}
	}
}

bool ZipFileReader::GetFileInfo(const char *path, File::FileInfo *info) {
	struct zip_stat zstat;
	char temp_path[1024];
	snprintf(temp_path, sizeof(temp_path), "%s%s", inZipPath_, path);

	// Clear some things to start.
	info->isDirectory = false;
	info->isWritable = false;
	info->size = 0;

	{
		std::lock_guard<std::mutex> guard(lock_);
		if (0 != zip_stat(zip_file_, temp_path, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED, &zstat)) {
			// ZIP files do not have real directories, so we'll end up here if we
			// try to stat one. For now that's fine.
			info->exists = false;
			return false;
		}
	}

	// Zips usually don't contain directory entries, but they may.
	if ((zstat.valid & ZIP_STAT_NAME) != 0 && zstat.name) {
		info->isDirectory = zstat.name[strlen(zstat.name) - 1] == '/';
	}
	if ((zstat.valid & ZIP_STAT_SIZE) != 0) {
		info->size = zstat.size;
	}

	info->fullName = Path(path);
	info->exists = true;
	return true;
}

class ZipFileReaderFileReference : public VFSFileReference {
public:
	int zi;
};

class ZipFileReaderOpenFile : public VFSOpenFile {
public:
	~ZipFileReaderOpenFile() {
		// Needs to be closed properly and unlocked.
		_dbg_assert_(zf == nullptr);
	}
	ZipFileReaderFileReference *reference;
	zip_file_t *zf = nullptr;
};

VFSFileReference *ZipFileReader::GetFile(const char *path) {
	std::lock_guard<std::mutex> guard(lock_);
	int zi = zip_name_locate(zip_file_, path, ZIP_FL_NOCASE);
	if (zi < 0) {
		// Not found.
		return nullptr;
	}
	ZipFileReaderFileReference *ref = new ZipFileReaderFileReference();
	ref->zi = zi;
	return ref;
}

bool ZipFileReader::GetFileInfo(VFSFileReference *vfsReference, File::FileInfo *fileInfo) {
	ZipFileReaderFileReference *reference = (ZipFileReaderFileReference *)vfsReference;
	// If you crash here, you called this while having the lock held by having the file open.
	// Don't do that, check the info before you open the file.
	std::lock_guard<std::mutex> guard(lock_);
	zip_stat_t zstat;
	if (zip_stat_index(zip_file_, reference->zi, 0, &zstat) != 0)
		return false;
	*fileInfo = File::FileInfo{};
	fileInfo->size = 0;
	if (zstat.valid & ZIP_STAT_SIZE)
		fileInfo->size = zstat.size;
	return zstat.size;
}

void ZipFileReader::ReleaseFile(VFSFileReference *vfsReference) {
	ZipFileReaderFileReference *reference = (ZipFileReaderFileReference *)vfsReference;
	// Don't do anything other than deleting it.
	delete reference;
}

VFSOpenFile *ZipFileReader::OpenFileForRead(VFSFileReference *vfsReference, size_t *size) {
	ZipFileReaderFileReference *reference = (ZipFileReaderFileReference *)vfsReference;
	ZipFileReaderOpenFile *openFile = new ZipFileReaderOpenFile();
	openFile->reference = reference;
	*size = 0;
	// We only allow one file to be open for read concurrently. It's possible that this can be improved,
	// especially if we only access by index like this.
	lock_.lock();
	zip_stat_t zstat;
	if (zip_stat_index(zip_file_, reference->zi, 0, &zstat) != 0) {
		lock_.unlock();
		return nullptr;
	}

	openFile->zf = zip_fopen_index(zip_file_, reference->zi, 0);
	if (!openFile->zf) {
		WARN_LOG(G3D, "File with index %d not found in zip", reference->zi);
		lock_.unlock();
		return nullptr;
	}

	*size = zstat.size;
	// Intentionally leaving the mutex locked, will be closed in CloseFile.
	return openFile;
}

void ZipFileReader::Rewind(VFSOpenFile *vfsOpenFile) {
	ZipFileReaderOpenFile *openFile = (ZipFileReaderOpenFile *)vfsOpenFile;
	// Close and re-open.
	zip_fclose(openFile->zf);
	openFile->zf = zip_fopen_index(zip_file_, openFile->reference->zi, 0);
}

size_t ZipFileReader::Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) {
	ZipFileReaderOpenFile *file = (ZipFileReaderOpenFile *)vfsOpenFile;
	return zip_fread(file->zf, buffer, length);
}

void ZipFileReader::CloseFile(VFSOpenFile *vfsOpenFile) {
	ZipFileReaderOpenFile *file = (ZipFileReaderOpenFile *)vfsOpenFile;
	_dbg_assert_(file->zf != nullptr);
	zip_fclose(file->zf);
	file->zf = nullptr;
	lock_.unlock();
	delete file;
}

#include <cctype>
#include <set>
#include <algorithm>  // for sort
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

ZipContainer::ZipContainer() noexcept : sourceData_(nullptr), zip_(nullptr) {}

ZipContainer::ZipContainer(const Path &path) : sourceData_(new SourceData {path, nullptr}), zip_(nullptr) {
	zip_source_t *source = zip_source_function_create(SourceCallback, sourceData_, nullptr);
	if (source != nullptr && (zip_ = zip_open_from_source(source, ZIP_RDONLY, nullptr)) == nullptr) {
		zip_source_free(source);
	}
}

ZipContainer::ZipContainer(ZipContainer &&other) noexcept {
	*this = std::move(other);
}

ZipContainer &ZipContainer::operator=(ZipContainer &&other) noexcept {
	sourceData_ = other.sourceData_;
	zip_ = other.zip_;
	other.sourceData_ = nullptr;
	other.zip_ = nullptr;
	return *this;
}

ZipContainer::~ZipContainer() {
	close();
}

void ZipContainer::close() noexcept {
	if (zip_ != nullptr) {
		zip_close(zip_);
		zip_ = nullptr;
	}
	if (sourceData_ != nullptr) {
		delete sourceData_;
		sourceData_ = nullptr;
	}
}

ZipContainer::operator zip_t *() const noexcept {
	return zip_;
}

zip_int64_t ZipContainer::SourceCallback(void *userdata, void *data, zip_uint64_t len, zip_source_cmd_t cmd) {
	SourceData &sourceData = *(SourceData *)userdata;

	switch (cmd) {
		case ZIP_SOURCE_SUPPORTS:
			return zip_source_make_command_bitmap(
				ZIP_SOURCE_SUPPORTS,
				ZIP_SOURCE_OPEN,
				ZIP_SOURCE_READ,
				ZIP_SOURCE_CLOSE,
				ZIP_SOURCE_STAT,
				ZIP_SOURCE_ERROR,
				ZIP_SOURCE_SEEK,
				ZIP_SOURCE_TELL,
				-1);

		case ZIP_SOURCE_OPEN:
			{
				FILE *newFile = File::OpenCFile(sourceData.path, "rb");
				if (newFile == nullptr) {
					return -1;
				}
				sourceData.file = newFile;
				return 0;
			}

		case ZIP_SOURCE_READ:
			{
				size_t n = fread(data, 1, len, sourceData.file);
				return ferror(sourceData.file) ? -1 : n;
			}

		case ZIP_SOURCE_CLOSE:
			fclose(sourceData.file);
			sourceData.file = nullptr;
			return 0;

		case ZIP_SOURCE_STAT:
			if (sourceData.file == nullptr) {
				zip_stat_t *stat = (zip_stat_t *)data;
				zip_stat_init(stat);
				stat->valid = 0;
			} else {
				int64_t pos = File::Ftell(sourceData.file);
				if (pos == -1) {
					return -1;
				}
				if (File::Fseek(sourceData.file, 0, SEEK_END) != 0) {
					return -1;
				}
				int64_t size = File::Ftell(sourceData.file);
				if (size != pos && File::Fseek(sourceData.file, pos, SEEK_SET) != 0) {
					return -1;
				}
				zip_stat_t *stat = (zip_stat_t *)data;
				zip_stat_init(stat);
				stat->valid = ZIP_STAT_SIZE;
				stat->size = size;
			}
			return 0;

		case ZIP_SOURCE_ERROR:
			return ZIP_ER_INTERNAL;

		case ZIP_SOURCE_SEEK:
			{
				zip_source_args_seek_t *args = (zip_source_args_seek_t *)data;
				if (args == nullptr) {
					return -1;
				}
				return File::Fseek(sourceData.file, args->offset, args->whence) ? -1 : 0;
			}

		case ZIP_SOURCE_TELL:
			return File::Ftell(sourceData.file);

		default:
			return -1;
	}
}

ZipFileReader *ZipFileReader::Create(const Path &zipFile, std::string_view inZipPath, bool logErrors) {
	// The inZipPath is supposed to be a folder, and internally in this class, we suffix
	// folder paths with '/', matching how the zip library works.
	std::string path(inZipPath);
	if (!path.empty() && path.back() != '/') {
		path.push_back('/');
	}

	ZipContainer zip(zipFile);
	if (zip == nullptr) {
		if (logErrors) {
			ERROR_LOG(Log::IO, "Failed to open %s as a zip file", zipFile.c_str());
		}
		return nullptr;
	}

	return new ZipFileReader(std::move(zip), zipFile, path);
}

ZipFileReader::~ZipFileReader() {
	std::lock_guard<std::mutex> guard(lock_);
	zip_file_.close();
}

uint8_t *ZipFileReader::ReadFile(std::string_view path, size_t *size) {
	std::string temp_path = join(inZipPath_, path);

	std::lock_guard<std::mutex> guard(lock_);
	// Figure out the file size first. TODO: Can this part be done without locking the mutex?
	struct zip_stat zstat;
	int retval = zip_stat(zip_file_, temp_path.c_str(), ZIP_FL_NOCASE | ZIP_FL_UNCHANGED, &zstat);
	if (retval != 0) {
		ERROR_LOG(Log::IO, "Error opening %s from ZIP", temp_path.c_str());
		return 0;
	}
	zip_file *file = zip_fopen_index(zip_file_, zstat.index, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED);
	if (!file) {
		ERROR_LOG(Log::IO, "Error opening %s from ZIP", temp_path.c_str());
		return 0;
	}
	uint8_t *contents = new uint8_t[zstat.size + 1];
	zip_fread(file, contents, zstat.size);
	zip_fclose(file);
	contents[zstat.size] = 0;

	*size = zstat.size;
	return contents;
}

bool ZipFileReader::GetFileListing(std::string_view orig_path, std::vector<File::FileInfo> *listing, const char *filter = 0) {
	std::string path = join(inZipPath_, orig_path);
	if (!path.empty() && path.back() != '/') {
		path.push_back('/');
	}

	std::set<std::string> filters;
	std::string tmp;
	if (filter) {
		while (*filter) {
			if (*filter == ':') {
				filters.emplace("." + tmp);
				tmp.clear();
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
	}

	if (tmp.size())
		filters.emplace("." + tmp);

	// We just loop through the whole ZIP file and deduce what files are in this directory, and what subdirectories there are.
	std::set<std::string> files;
	std::set<std::string> directories;
	bool success = GetZipListings(path, files, directories);
	if (!success) {
		// This means that no file prefix matched the path.
		return false;
	}

	listing->clear();

	// INFO_LOG(Log::IO, "Zip: Listing '%s'", orig_path);

	const std::string relativePath = path.substr(inZipPath_.size());

	listing->reserve(directories.size() + files.size());
	for (const auto &dir : directories) {
		File::FileInfo info;
		info.name = dir;

		// Remove the "inzip" part of the fullname.
		info.fullName = Path(relativePath + dir);
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = true;
		// INFO_LOG(Log::IO, "Found file: %s (%s)", info.name.c_str(), info.fullName.c_str());
		listing->push_back(info);
	}

	for (const auto &fiter : files) {
		File::FileInfo info;
		info.name = fiter;
		info.fullName = Path(relativePath + fiter);
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = false;
		std::string ext = info.fullName.GetFileExtension();
		if (filter) {
			if (filters.find(ext) == filters.end()) {
				continue;
			}
		}
		// INFO_LOG(Log::IO, "Found dir: %s (%s)", info.name.c_str(), info.fullName.c_str());
		listing->push_back(info);
	}

	std::sort(listing->begin(), listing->end());
	return true;
}

// path here is from the root, so inZipPath needs to already be added.
bool ZipFileReader::GetZipListings(const std::string &path, std::set<std::string> &files, std::set<std::string> &directories) {
	_dbg_assert_(path.empty() || path.back() == '/');

	std::lock_guard<std::mutex> guard(lock_);
	int numFiles = zip_get_num_files(zip_file_);
	bool anyPrefixMatched = false;
	for (int i = 0; i < numFiles; i++) {
		const char* name = zip_get_name(zip_file_, i, 0);
		if (!name)
			continue;  // shouldn't happen, I think
		if (startsWith(name, path)) {
			if (strlen(name) == path.size()) {
				// Don't want to return the same folder.
				continue;
			}
			const char *slashPos = strchr(name + path.size(), '/');
			if (slashPos != 0) {
				anyPrefixMatched = true;
				// A directory. Let's pick off the only part we care about.
				size_t offset = path.size();
				std::string dirName = std::string(name + offset, slashPos - (name + offset));
				// We might get a lot of these if the tree is deep. The std::set deduplicates.
				directories.insert(dirName);
			} else {
				anyPrefixMatched = true;
				// It's a file.
				const char *fn = name + path.size();
				files.emplace(fn);
			}
		}
	}
	return anyPrefixMatched;
}

bool ZipFileReader::GetFileInfo(std::string_view path, File::FileInfo *info) {
	struct zip_stat zstat;
	std::string temp_path = join(inZipPath_, path);

	// Clear some things to start.
	info->isDirectory = false;
	info->isWritable = false;
	info->size = 0;

	{
		std::lock_guard<std::mutex> guard(lock_);
		if (0 != zip_stat(zip_file_, temp_path.c_str(), ZIP_FL_NOCASE | ZIP_FL_UNCHANGED, &zstat)) {
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

VFSFileReference *ZipFileReader::GetFile(std::string_view path) {
	std::string p(path);
	int zi = zip_name_locate(zip_file_, p.c_str(), ZIP_FL_NOCASE);  // this is EXPENSIVE
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
		delete openFile;
		return nullptr;
	}

	openFile->zf = zip_fopen_index(zip_file_, reference->zi, 0);
	if (!openFile->zf) {
		WARN_LOG(Log::G3D, "File with index %d not found in zip", reference->zi);
		lock_.unlock();
		delete openFile;
		return nullptr;
	}

	*size = zstat.size;
	// Intentionally leaving the mutex locked, will be closed in CloseFile.
	return openFile;
}

void ZipFileReader::Rewind(VFSOpenFile *vfsOpenFile) {
	ZipFileReaderOpenFile *file = (ZipFileReaderOpenFile *)vfsOpenFile;
	_assert_(file);
	// Unless the zip file is compressed, can't seek directly, so we re-open.
	// This version of libzip doesn't even have zip_file_is_seekable(), should probably upgrade.
	zip_fclose(file->zf);
	file->zf = zip_fopen_index(zip_file_, file->reference->zi, 0);
	_dbg_assert_(file->zf != nullptr);
}

size_t ZipFileReader::Read(VFSOpenFile *vfsOpenFile, void *buffer, size_t length) {
	ZipFileReaderOpenFile *file = (ZipFileReaderOpenFile *)vfsOpenFile;
	_assert_(file);
	_dbg_assert_(file->zf != nullptr);
	return zip_fread(file->zf, buffer, length);
}

void ZipFileReader::CloseFile(VFSOpenFile *vfsOpenFile) {
	ZipFileReaderOpenFile *file = (ZipFileReaderOpenFile *)vfsOpenFile;
	_assert_(file);
	_dbg_assert_(file->zf != nullptr);
	zip_fclose(file->zf);
	file->zf = nullptr;
	vfsOpenFile = nullptr;
	lock_.unlock();
	delete file;
}

bool ReadSingleFileFromZip(Path zipFile, const char *path, std::string *data, std::mutex *mutex) {
	ZipContainer zip(zipFile);
	if (zip == nullptr) {
		return false;
	}

	struct zip_stat zstat;
	if (zip_stat(zip, path, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED, &zstat) != 0) {
		return false;
	}
	zip_file *file = zip_fopen_index(zip, zstat.index, ZIP_FL_UNCHANGED);
	if (!file) {
		return false;
	}
	if (mutex) {
		mutex->lock();
	}
	data->resize(zstat.size);
	if (zip_fread(file, data->data(), zstat.size) != zstat.size) {
		if (mutex) {
			mutex->unlock();
		}
		data->resize(0);
		zip_fclose(file);
		return false;
	}
	if (mutex) {
		mutex->unlock();
	}
	zip_fclose(file);
	return true;
}

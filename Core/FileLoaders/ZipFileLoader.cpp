#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif

#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/FileLoaders/ZipFileLoader.h"

ZipFileLoader::ZipFileLoader(FileLoader *sourceLoader)
	: ProxiedFileLoader(sourceLoader), zipArchive_(nullptr) {
	if (!backend_ || !backend_->Exists() || backend_->IsDirectory()) {
		return;
	}

	zip_error_t error{};
	zip_source_t* zipSource = zip_source_function_create([](void* userdata, void* data, zip_uint64_t len, zip_source_cmd_t cmd) -> zip_int64_t {
		ZipFileLoader *loader = (ZipFileLoader *)userdata;
		return loader->ZipSourceCallback(data, len, cmd);
	}, this, &error);
	if (!zipSource) {
		ERROR_LOG(Log::IO, "Failed to create ZIP source: %s", zip_error_strerror(&error));
		return;
	}

	zipArchive_ = zip_open_from_source(zipSource, ZIP_RDONLY, &error);
	if (!zipArchive_) {
		ERROR_LOG(Log::IO, "Failed to open ZIP archive: %s", zip_error_strerror(&error));
		zip_source_free(zipSource);
		// zipArchive_ is already nullptr here.
	}
}

ZipFileLoader::~ZipFileLoader() {
	if (dataFile_) {
		zip_fclose(dataFile_);
	}
	if (zipArchive_) {
		zip_discard(zipArchive_);
	}
	if (data_) {
		free(data_);
	}
}

bool ZipFileLoader::Initialize(int fileIndex) {
	_dbg_assert_(!data_);

	if (!zipArchive_) {
		ERROR_LOG(Log::IO, "Cannot initialize: ZIP archive is null");
		return false;
	}

	struct zip_stat zstat;
	int retval = zip_stat_index(zipArchive_, fileIndex, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED, &zstat);
	if (retval < 0) {
		return false;
	}
	const char *name = zip_get_name(zipArchive_, fileIndex, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED);
	if (!name) {
		ERROR_LOG(Log::IO, "Failed to get name for file index %d", fileIndex);
		return false;
	}
	fileExtension_ = KeepIncludingLast(name, '.');

	_dbg_assert_(zstat.index == fileIndex);
	dataFileSize_ = zstat.size;
	dataFile_ = zip_fopen_index(zipArchive_, zstat.index, ZIP_FL_UNCHANGED);
	if (!dataFile_) {
		ERROR_LOG(Log::IO, "Failed to open file index %d from ZIP", fileIndex);
		return false;
	}

	// Handle zero-size files
	if (dataFileSize_ == 0) {
		data_ = nullptr;
		return true;
	}

	data_ = (u8 *)malloc(dataFileSize_);
	if (!data_) {
		ERROR_LOG(Log::IO, "Failed to allocate %lld bytes for ZIP file data", dataFileSize_);
		zip_fclose(dataFile_);
		dataFile_ = nullptr;
		return false;
	}
	return true;
}

size_t ZipFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	if (!dataFile_ || absolutePos < 0 || absolutePos >= dataFileSize_) {
		return 0;
	}

	if (absolutePos + (s64)bytes > dataFileSize_) {
		bytes = dataFileSize_ - absolutePos;
	}

	// Decompress until the requested point, filling up data_ as we go. TODO: Do on thread.
	while (dataReadPos_ < absolutePos + (s64)bytes) {
		int remaining = BLOCK_SIZE;
		if (dataReadPos_ + remaining > dataFileSize_) {
			remaining = (int)(dataFileSize_ - dataReadPos_);
		}
		zip_int64_t retval = zip_fread(dataFile_, data_ + dataReadPos_, remaining);
		if (retval < 0) {
			ERROR_LOG(Log::IO, "zip_fread failed with error code %lld", retval);
			return 0;
		}
		if (retval != remaining) {
			ERROR_LOG(Log::IO, "zip_fread: expected %d bytes, got %lld", remaining, retval);
			return 0;
		}
		dataReadPos_ += retval;
	}

	// Perform the read.
	memcpy(data, data_ + absolutePos, bytes);
	return bytes;
}

zip_int64_t ZipFileLoader::ZipSourceCallback(void *data, zip_uint64_t len, zip_source_cmd_t cmd) {
	switch (cmd) {
	case ZIP_SOURCE_OPEN:
	{
		zipReadPos_ = 0;
		return 0;
	}
	case ZIP_SOURCE_READ:
	{
		size_t readBytes = backend_->ReadAt(zipReadPos_, len, data);
		zipReadPos_ += readBytes;
		return static_cast<zip_int64_t>(readBytes);
	}
	case ZIP_SOURCE_SEEK:
	{
		if (len < sizeof(zip_source_args_seek_t)) {
			return -1;
		}
		const zip_source_args_seek_t *seekArgs = (const zip_source_args_seek_t *)data;
		zip_int64_t new_offset;
		switch (seekArgs->whence) {
		case SEEK_SET:
			new_offset = seekArgs->offset;
			break;
		case SEEK_CUR:
			new_offset = zipReadPos_ + seekArgs->offset;
			break;
		case SEEK_END:
			new_offset = backend_->FileSize() + seekArgs->offset;
			break;
		default:
			return -1;
		}
		if (new_offset < 0 || new_offset > backend_->FileSize()) {
			return -1;
		}
		zipReadPos_ = new_offset;
		return 0;
	}
	case ZIP_SOURCE_TELL:
		return zipReadPos_;
	case ZIP_SOURCE_CLOSE:
		return 0;
	case ZIP_SOURCE_STAT:
	{
		if (len < sizeof(zip_stat_t)) {
			return -1;
		}
		zip_stat_t* st = static_cast<zip_stat_t*>(data);
		zip_stat_init(st);
		st->valid = ZIP_STAT_SIZE;
		st->size = static_cast<zip_uint64_t>(backend_->FileSize());
		return sizeof(zip_stat_t);
	}
	case ZIP_SOURCE_ERROR:
		return -1;
	case ZIP_SOURCE_FREE:
		return 0;
	case ZIP_SOURCE_SUPPORTS:
		return zip_source_make_command_bitmap(ZIP_SOURCE_READ, ZIP_SOURCE_SEEK, ZIP_SOURCE_TELL, ZIP_SOURCE_OPEN, ZIP_SOURCE_CLOSE, ZIP_SOURCE_STAT, -1);
	default:
		return -1;
	}
}

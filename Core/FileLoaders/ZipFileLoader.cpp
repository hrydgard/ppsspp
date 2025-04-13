#include <ext/libzip/zip.h>

#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/FileLoaders/ZipFileLoader.h"

ZipFileLoader::ZipFileLoader(FileLoader *sourceLoader)
	: ProxiedFileLoader(sourceLoader), zipArchive_(nullptr) {
	if (!backend_ || !backend_->Exists() || backend_->IsDirectory()) {
		// bad
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

	struct zip_stat zstat;
	int retval = zip_stat_index(zipArchive_, fileIndex, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED, &zstat);
	if (retval < 0) {
		return false;
	}
	const char *name = zip_get_name(zipArchive_, fileIndex, ZIP_FL_NOCASE | ZIP_FL_UNCHANGED);
	fileExtension_ = KeepIncludingLast(name, '.');

	_dbg_assert_(zstat.index == fileIndex);
	dataFileSize_ = zstat.size;
	dataFile_ = zip_fopen_index(zipArchive_, zstat.index, ZIP_FL_UNCHANGED);
	data_ = (u8 *)malloc(dataFileSize_);
	return data_ != nullptr;
}

size_t ZipFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	if (!dataFile_ || absolutePos < 0 || absolutePos >= dataFileSize_) {
		return 0;
	}

	if (absolutePos + bytes > dataFileSize_) {
		// TODO: This could go negative..
		bytes = dataFileSize_ - absolutePos;
	}

	// Decompress until the requested point, filling up data_ as we go. TODO: Do on thread.
	while (dataReadPos_ < absolutePos + bytes) {
		int remaining = BLOCK_SIZE;
		if (dataReadPos_ + remaining > dataFileSize_) {
			remaining = dataFileSize_ - dataReadPos_;
		}
		zip_int64_t retval = zip_fread(dataFile_, data_ + dataReadPos_, remaining);
		_dbg_assert_(retval == remaining);
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
		size_t readBytes = static_cast<zip_int64_t>(backend_->ReadAt(zipReadPos_, len, data));
		zipReadPos_ += readBytes;
		return readBytes;
	}
	case ZIP_SOURCE_SEEK:
	{
		struct SeekData {
			zip_int64_t offset;
			int whence;
		};
		if (len < sizeof(SeekData)) {
			return -1; // Invalid argument size
		}
		const SeekData *seekData = static_cast<const SeekData*>(data);
		zip_int64_t new_offset;
		switch (seekData->whence) {
		case SEEK_SET:
			new_offset = seekData->offset;
			break;
		case SEEK_CUR:
			new_offset = zipReadPos_ + seekData->offset;
			break;
		case SEEK_END:
			new_offset = backend_->FileSize() + seekData->offset;
			break;
		default:
			return -1; // Invalid 'whence' value
		}
		if (new_offset < 0 || new_offset > backend_->FileSize()) {
			return -1; // Offset out of bounds
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
		return zip_source_make_command_bitmap(ZIP_SOURCE_READ, ZIP_SOURCE_SEEK, ZIP_SOURCE_TELL, ZIP_SOURCE_OPEN, ZIP_SOURCE_CLOSE, ZIP_SOURCE_STAT);
	default:
		return -1;
	}
}

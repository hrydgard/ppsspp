#pragma once

#include <mutex>

#ifdef SHARED_LIBZIP
#include <zip.h>
#else
#include "ext/libzip/zip.h"
#endif

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/StringUtils.h"
#include "Core/Loaders.h"

// Exposes a single (chosen) file from a zip file as another file loader.
// Useful in a bunch of possible chains.
class ZipFileLoader : public ProxiedFileLoader {
public:
	ZipFileLoader(FileLoader *sourceLoader);
	~ZipFileLoader() override;

	zip_t *GetZip() const {
		return zipArchive_;
	}

	bool Initialize(int fileIndex);

	bool Exists() override {
		return dataFile_ != nullptr;
	}

	bool IsDirectory() override {
		return false;
	}

	s64 FileSize() override {
		return dataFileSize_;
	}

	Path GetPath() const override {
		return backend_->GetPath();
	}

	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override;

	std::string GetFileExtension() const override {
		return fileExtension_;
	}

private:
	zip_int64_t ZipSourceCallback(void* data, zip_uint64_t len, zip_source_cmd_t cmd);

	enum {
		BLOCK_SIZE = 65536,
	};
	zip_t *zipArchive_ = nullptr;
	s64 zipReadPos_ = 0;

	zip_file_t *dataFile_ = nullptr;
	uint8_t *data_ = nullptr;  // malloc/free
	s64 dataReadPos_ = 0;
	s64 dataFileSize_ = 0;
	std::string fileExtension_;
};

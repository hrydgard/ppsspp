#pragma once

#include "pch.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>

#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

// This thing is a terrible abomination that wraps asynchronous file access behind a synchronous interface,
// completely defeating MS' design goals for StorageFile. But hey, you gotta do what you gotta do.
// This opens a stream attached to the passed-in file. Multiple of these can be created against one StorageFile.
class StorageFileLoader : public FileLoader {
public:
	StorageFileLoader(Windows::Storage::StorageFile ^file);
	~StorageFileLoader();

	bool Exists() override;
	bool ExistsFast() override;

	bool IsDirectory() override;
	s64 FileSize() override;
	std::string Path() const override;
	std::string Extension() override;

	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override;

private:
	void threadfunc();
	void EnsureOpen();

	enum class OpType {
		NONE,
		READ_AT,
	};

	struct Operation {
		OpType type;
		int64_t offset;
		int64_t size;
	};

	struct Response {
		Windows::Storage::Streams::IBuffer ^buffer;
	};

	bool active_ = false;
	int64_t size_ = -1;
	std::unique_ptr<std::thread> thread_;

	Windows::Storage::StorageFile ^file_;
	Windows::Storage::Streams::IRandomAccessStreamWithContentType ^stream_;
	std::string path_;

	bool operationRequested_ = false;
	Operation operation_{ OpType::NONE, 0, 0 };
	std::condition_variable cond_;
	std::mutex mutex_;

	bool operationFailed_ = false;

	bool responseAvailable_ = false;
	Response response_;
	std::condition_variable condResponse_;
	std::mutex mutexResponse_;

	int64_t seekPos_ = 0;
};

class StorageFileLoaderFactory : public FileLoaderFactory {
public:
	StorageFileLoaderFactory(Windows::Storage::StorageFile ^file, IdentifiedFileType fileType) : file_(file), fileType_(fileType) { }
	FileLoader *ConstructFileLoader(const std::string &filename) override;

private:
	Windows::Storage::StorageFile ^file_;
	IdentifiedFileType fileType_;
};

// Similar to StorageFileLoader but for directory browsing.
class StorageDirectoryWrapper {
private:
	std::thread thread_;
};

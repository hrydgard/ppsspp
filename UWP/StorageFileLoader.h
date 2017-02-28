#pragma once

#include "pch.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

// This thing is a terrible abomination that wraps asynchronous file access behind a synchronous interface,
// completely defeating MS' design goals for StorageFile. But hey, you gotta do what you gotta do.

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
	void Seek(s64 absolutePos) override;
	size_t Read(size_t bytes, size_t count, void *data, Flags flags = Flags::NONE);
	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE);

private:
	void threadfunc();
	void EnsureOpen();

	enum class OpType {
		READ,
	};

	struct Operation {
		OpType type;
		int64_t offset;
		int64_t size;
		Windows::Storage::Streams::Buffer ^buffer;
	};

	bool active_ = false;
	int64_t size_ = -1;
	std::thread thread_;
	Windows::Storage::StorageFile ^file_;
	Windows::Storage::Streams::IRandomAccessStreamWithContentType ^stream_;
	std::condition_variable cond_;
	std::mutex mutex_;

	std::condition_variable condResponse_;
	std::mutex mutexResponse_;

	std::queue<Operation> operations_;
	int64_t seekPos_ = 0;

	std::queue<Operation> responses_;
};
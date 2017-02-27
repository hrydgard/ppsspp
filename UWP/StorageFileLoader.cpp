#include "pch.h"
#include "ppltasks.h"
#include "StorageFileLoader.h"


using namespace Concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

StorageFileLoader::StorageFileLoader(Windows::Storage::StorageFile ^file) {
	create_task(file->OpenReadAsync()).then([this](IRandomAccessStreamWithContentType ^stream) {
		stream_ = stream;
		active_ = true;
		thread_ = std::thread([this]() { this->threadfunc(); });
	});
}

StorageFileLoader::~StorageFileLoader() {
	active_ = false;
	thread_.join();
}

void StorageFileLoader::threadfunc() {
	std::unique_lock<std::mutex> lock(mutex_);
	while (active_) {
		cond_.wait(lock);
		std::unique_lock<std::mutex> lock(mutexResponse_);
		while (operations_.size()) {
			Operation op = operations_.front();
			operations_.pop();

			switch (op.type) {
			case OpType::READ: {
				op.buffer = ref new Streams::Buffer(op.size);
				auto task = create_task(stream_->ReadAsync(op.buffer, op.size, Streams::InputStreamOptions::None));
				task.wait();
				break;
				responses_.push(op);
			}
			default:
				break;
			}
		}
		// OK, done with all operations.
		condResponse_.notify_one();
	}
}

bool StorageFileLoader::Exists() {
	return true;
}
bool StorageFileLoader::ExistsFast() {
	return true;
}

bool StorageFileLoader::IsDirectory() {
	return false;
}

s64 StorageFileLoader::FileSize() {
	return 0;
}

std::string StorageFileLoader::Path() const { return ""; }

std::string StorageFileLoader::Extension() { return ""; }

void StorageFileLoader::Seek(s64 absolutePos) {
	seekPos_ = absolutePos;
}

size_t StorageFileLoader::Read(size_t bytes, size_t count, void *data, Flags flags) {
	{
		std::unique_lock<std::mutex> lock(mutex_);
		operations_.push(Operation{ OpType::READ, seekPos_, (int64_t)(bytes * count) });
		cond_.notify_one();
	}
	// OK, now wait for response...
	{
		std::unique_lock<std::mutex> responseLock(mutexResponse_);
		condResponse_.wait(responseLock);
		Operation resp = responses_.front();
		responses_.pop();
		// memcpy(data,  bytes * count, )
		return 0;
	}
}

size_t StorageFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	return 0;
}

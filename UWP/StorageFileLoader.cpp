#include "pch.h"
#include "ppltasks.h"
#include "StorageFileLoader.h"


using namespace Concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

StorageFileLoader::StorageFileLoader(Windows::Storage::StorageFile ^file) {
	file_ = file;
	auto opentask = create_task(file->OpenReadAsync());
	opentask.then([this](IRandomAccessStreamWithContentType ^stream) {
		stream_ = stream;
		active_ = true;
		thread_ = std::thread([this]() { this->threadfunc(); });
	});
	auto attrtask = create_task(file->GetBasicPropertiesAsync());
	attrtask.then([this](Windows::Storage::FileProperties::BasicProperties ^props) {
		size_ = props->Size;
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
	return (file_->Attributes & Windows::Storage::FileAttributes::Directory) != Windows::Storage::FileAttributes::Normal;
}

s64 StorageFileLoader::FileSize() {
	if (size_ == -1)
		__debugbreak();  // crude race condition detection
	return size_;
}

std::string StorageFileLoader::Path() const {
	return "";
}

std::string StorageFileLoader::Extension() {
	return "";
}

void StorageFileLoader::EnsureOpen() {
	while (size_ == -1)
		Sleep(100);
}

// Note that multithreaded use could wreak havoc with this...
void StorageFileLoader::Seek(s64 absolutePos) {
	EnsureOpen();
	seekPos_ = absolutePos;
}

size_t StorageFileLoader::Read(size_t bytes, size_t count, void *data, Flags flags) {
	EnsureOpen();
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
		DataReader ^rd = DataReader::FromBuffer(resp.buffer);
		Platform::Array<uint8_t> ^bytearray = ref new Platform::Array<uint8_t>(resp.buffer->Length);
		rd->ReadBytes(bytearray);
		memcpy(data, bytearray->Data, bytes * count);
		return 0;
	}
}

size_t StorageFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	EnsureOpen();
	{
		std::unique_lock<std::mutex> lock(mutex_);
		operations_.push(Operation{ OpType::READ, absolutePos, (int64_t)(bytes * count) });
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

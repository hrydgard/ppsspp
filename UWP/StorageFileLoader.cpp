#include "pch.h"
#include "ppltasks.h"

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DirListing.h"
#include "Common/Thread/ThreadUtil.h"
#include "StorageFileLoader.h"
#include "Common/Log.h"
#include "UWPUtil.h"

using namespace Concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

// Not sure how necessary this one is.
static std::mutex initMutex;

StorageFileLoader::StorageFileLoader(Windows::Storage::StorageFile ^file) {
	active_ = false;
	file_ = file;
	path_ = Path(FromPlatformString(file_->Path));
	thread_.reset(new std::thread([this]() { this->threadfunc(); }));

	// Before we proceed, we need to block until the thread has found the size.
	// Hacky way:
	while (size_ < 0) {
		Sleep(10);
	}
}

StorageFileLoader::~StorageFileLoader() {
	{
		std::unique_lock<std::mutex> lock(mutex_);
		active_ = false;
		operationRequested_ = false;
		cond_.notify_one();
	}
	thread_->join();
}

void StorageFileLoader::threadfunc() {
	SetCurrentThreadName("StorageFileLoader");

	{
		std::unique_lock<std::mutex> lock(initMutex);
		_assert_(!active_);
		auto opentask = create_task(file_->OpenReadAsync()).then([this](IRandomAccessStreamWithContentType ^stream) {
			stream_ = stream;
			active_ = true;
		});

		try {
			opentask.wait();
		} catch (const std::exception& e) {
			operationFailed_ = true;
			// TODO: What do we do?
			const char *what = e.what();
			INFO_LOG(SYSTEM, "%s", what);
		} catch (Platform::COMException ^e) {

		}

		auto sizetask = create_task(file_->GetBasicPropertiesAsync()).then([this](Windows::Storage::FileProperties::BasicProperties ^props) {
			size_ = props->Size;
		});
		try {
			sizetask.wait();
		} catch (const std::exception& e) {
			const char *what = e.what();
			INFO_LOG(SYSTEM, "%s", what);
		} catch (Platform::COMException ^e) {
			std::string what = FromPlatformString(e->ToString());
			INFO_LOG(SYSTEM, "%s", what.c_str());
		}
	}

	std::unique_lock<std::mutex> lock(mutex_);
	while (active_) {
		if (!operationRequested_) {
			cond_.wait(lock);
		}
		if (operationRequested_) {
			switch (operation_.type) {
			case OpType::READ_AT: {
				Streams::Buffer ^buf = ref new Streams::Buffer((unsigned int)operation_.size);
				operationFailed_ = false;
				stream_->Seek(operation_.offset);
				auto task = create_task(stream_->ReadAsync(buf, (unsigned int)operation_.size, Streams::InputStreamOptions::None));
				Streams::IBuffer ^output = nullptr;
				try {
					task.wait();
					output = task.get();
				} catch (const std::exception& e) {
					operationFailed_ = true;
					const char *what = e.what();
					INFO_LOG(SYSTEM, "%s", what);
				}
				std::unique_lock<std::mutex> lock(mutexResponse_);
				operationRequested_ = false;
				response_.buffer = output;
				responseAvailable_ = true;
				condResponse_.notify_one();
				break;
			}
			default:
				operationRequested_ = false;
				break;
			}
		}
	}
}

bool StorageFileLoader::Exists() {
	return file_ != nullptr;
}

bool StorageFileLoader::ExistsFast() {
	return file_ != nullptr;
}

bool StorageFileLoader::IsDirectory() {
	return (file_->Attributes & Windows::Storage::FileAttributes::Directory) != Windows::Storage::FileAttributes::Normal;
}

s64 StorageFileLoader::FileSize() {
	EnsureOpen();
	if (size_ == -1)
		__debugbreak();  // crude race condition detection
	return size_;
}

Path StorageFileLoader::GetPath() const {
	return path_;
}

void StorageFileLoader::EnsureOpen() {
	while (size_ == -1)
		Sleep(50);
}

size_t StorageFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	// We can't handle multiple of these at a time, so serialize the easy way.
	std::unique_lock<std::mutex> lock(operationMutex_);

	EnsureOpen();

	_assert_(!operationRequested_);
	_assert_(!responseAvailable_)

	{
		std::unique_lock<std::mutex> lock(mutex_);
		operation_.type = OpType::READ_AT;
		operation_.offset = absolutePos;
		operation_.size = (int64_t)(bytes * count);
		operationRequested_ = true;
		cond_.notify_one();
	}

	// OK, now wait for response...
	{
		std::unique_lock<std::mutex> responseLock(mutexResponse_);
		while (!responseAvailable_) {
			condResponse_.wait(responseLock);
		}
		// still under mutexResponse_ lock here.
		responseAvailable_ = false;
		if (operationFailed_) {
			return 0;
		}

		DataReader ^rd = DataReader::FromBuffer(response_.buffer);
		size_t len = response_.buffer->Length;
		Platform::Array<uint8_t> ^bytearray = ref new Platform::Array<uint8_t>((unsigned int)len);
		rd->ReadBytes(bytearray);
		memcpy(data, bytearray->Data, len);
		response_.buffer = nullptr;
		return len / bytes;
	}
}

FileLoader *StorageFileLoaderFactory::ConstructFileLoader(const Path &filename) {
	return file_ ? new StorageFileLoader(file_) : nullptr;
}

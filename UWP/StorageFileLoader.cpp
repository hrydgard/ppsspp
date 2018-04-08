#include "pch.h"
#include "ppltasks.h"
#include "base/logging.h"
#include "file/file_util.h"
#include "thread/threadutil.h"
#include "StorageFileLoader.h"
#include "UWPUtil.h"

using namespace Concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

static std::mutex initMutex;

StorageFileLoader::StorageFileLoader(Windows::Storage::StorageFile ^file) {
	file_ = file;
	path_ = FromPlatformString(file_->Path);
	thread_.reset(new std::thread([this]() { this->threadfunc(); }));
}

StorageFileLoader::~StorageFileLoader() {
	initMutex.lock();
	active_ = false;
	operationRequested_ = false;
	cond_.notify_all();
	thread_->join();
	initMutex.unlock();
}

void StorageFileLoader::threadfunc() {
	setCurrentThreadName("StorageFileLoader");

	initMutex.lock();

	auto opentask = create_task(file_->OpenReadAsync()).then([this](IRandomAccessStreamWithContentType ^stream) {
		stream_ = stream;
		active_ = true;
	});

	try {
		opentask.wait();
	}
	catch (const std::exception& e) {
		operationFailed_ = true;
		// TODO: What do we do?
		const char *what = e.what();
		ILOG("%s", what);
	}
	catch (Platform::COMException ^e) {

	}

	auto sizetask = create_task(file_->GetBasicPropertiesAsync()).then([this](Windows::Storage::FileProperties::BasicProperties ^props) {
		size_ = props->Size;
	});
	try {
		sizetask.wait();
	}
	catch (const std::exception& e) {
		const char *what = e.what();
		ILOG("%s", what);
	}
	catch (Platform::COMException ^e) {
		std::string what = FromPlatformString(e->ToString());
		ILOG("%s", what.c_str());
	}

	initMutex.unlock();

	std::unique_lock<std::mutex> lock(mutex_);
	while (active_) {
		if (!operationRequested_) {
			cond_.wait(lock);
		}
		if (operationRequested_) {
			switch (operation_.type) {
			case OpType::READ_AT: {
				Streams::Buffer ^buf = ref new Streams::Buffer(operation_.size);
				operationFailed_ = false;
				stream_->Seek(operation_.offset);
				auto task = create_task(stream_->ReadAsync(buf, operation_.size, Streams::InputStreamOptions::None));
				Streams::IBuffer ^output = nullptr;
				try {
					task.wait();
					output = task.get();
				} catch (const std::exception& e) {
					operationFailed_ = true;
					const char *what = e.what();
					ILOG("%s", what);
				}
				operationRequested_ = false;
				std::unique_lock<std::mutex> lock(mutexResponse_);
				response_.buffer = output;
				responseAvailable_ = true;
				condResponse_.notify_one();
				break;
			}
			default:
				ELOG("Unknown operation");
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

std::string StorageFileLoader::Path() const {
	return path_;
}

std::string StorageFileLoader::Extension() {
	return "." + getFileExtension(path_);
}

void StorageFileLoader::EnsureOpen() {
	// UGLY!
	while (!thread_)
		Sleep(100);
	while (size_ == -1)
		Sleep(100);
}

size_t StorageFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	EnsureOpen();
	if (operationRequested_ || responseAvailable_)
		Crash();
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
		if (operationFailed_) {
			return 0;
		}
		DataReader ^rd = DataReader::FromBuffer(response_.buffer);
		size_t len = response_.buffer->Length;
		Platform::Array<uint8_t> ^bytearray = ref new Platform::Array<uint8_t>((unsigned int)len);
		rd->ReadBytes(bytearray);
		memcpy(data, bytearray->Data, len);
		responseAvailable_ = false;
		response_.buffer = nullptr;
		return len / bytes;
	}
}

FileLoader *StorageFileLoaderFactory::ConstructFileLoader(const std::string &filename) {
	return file_ ? new StorageFileLoader(file_) : nullptr;
}

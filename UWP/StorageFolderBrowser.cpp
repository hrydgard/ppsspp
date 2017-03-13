#include "pch.h"
#include "ppltasks.h"

#include "base/logging.h"
#include "thread/threadutil.h"

#include "StorageFolderBrowser.h"
#include "UWPUtil.h"

using namespace Concurrency;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

static std::mutex initMutex;

StorageFolderBrowser::StorageFolderBrowser(Windows::Storage::StorageFolder ^folder) : folder_(folder) {
	thread_.reset(new std::thread([this]() { this->threadfunc(); }));

	path_ = FromPlatformString(folder->Path);
	displayName_ = FromPlatformString(folder->DisplayName);
}

void StorageFolderBrowser::threadfunc() {
	setCurrentThreadName("StorageFileLoader");

	initMutex.lock();

	/*
	auto opentask = create_task(folder_->GetItemsAsync()->OpenReadAsync()).then([this](IRandomAccessStreamWithContentType ^stream) {
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
	*/
	initMutex.unlock();

	std::unique_lock<std::mutex> lock(mutex_);
	while (active_) {
		if (!operationRequested_) {
			cond_.wait(lock);
		}
		if (operationRequested_) {
			switch (operation_.type) {
			case OpType::LIST_DIRECTORY: {

				/*
				Streams::Buffer ^buf = ref new Streams::Buffer(operation_.size);
				operationFailed_ = false;
				stream_->Seek(operation_.offset);
				auto task = create_task(stream_->ReadAsync(buf, operation_.size, Streams::InputStreamOptions::None));
				Streams::IBuffer ^output = nullptr;
				try {
					task.wait();
					output = task.get();
				}
				catch (const std::exception& e) {
					operationFailed_ = true;
					const char *what = e.what();
					ILOG("%s", what);
				}
				operationRequested_ = false;
				std::unique_lock<std::mutex> lock(mutexResponse_);
				response_.buffer = output;
				responseAvailable_ = true;
				condResponse_.notify_one();
				break;*/
			}
			default:
				operationRequested_ = false;
				break;
			}
		}
	}
}

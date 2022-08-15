#include "pch.h"
#include "ppltasks.h"

#include "Common/Thread/ThreadUtil.h"

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
	SetCurrentThreadName("StorageFileLoader");

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
				operationRequested_ = false;
		}
	}
}

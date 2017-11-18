#pragma once
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

class StorageFolderBrowser {
public:
	StorageFolderBrowser(Windows::Storage::StorageFolder ^folder);
	~StorageFolderBrowser();

	std::string Path() const {
		return path_;
	}

	std::string DisplayName() const {
		return displayName_;
	}


private:
	void threadfunc();

	enum class OpType {
		NONE,
		LIST_DIRECTORY,
		CHANGE_FOLDER,
	};

	struct Operation {
		OpType type;
	};

	struct Response {
	};

	std::string path_;
	std::string displayName_;

	bool active_ = false;
	std::unique_ptr<std::thread> thread_;

	Windows::Storage::StorageFolder ^folder_;

	bool operationRequested_ = false;
	Operation operation_{ OpType::NONE };
	std::condition_variable cond_;
	std::mutex mutex_;

	bool operationFailed_ = false;

	bool responseAvailable_ = false;
	Response response_;
	std::condition_variable condResponse_;
	std::mutex mutexResponse_;

	int64_t seekPos_ = 0;
};

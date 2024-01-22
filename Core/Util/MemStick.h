#pragma once

#include "Common/File/Path.h"

#include <mutex>

// Utility functions moved out from MemstickScreen.

class MoveProgressReporter {
public:
	void Set(const std::string &value) {
		std::lock_guard<std::mutex> guard(mutex_);
		progress_ = value;
	}

	std::string Get() {
		std::lock_guard<std::mutex> guard(mutex_);
		return progress_;
	}

private:
	std::string progress_;
	std::mutex mutex_;
};

struct MoveResult {
	bool success;  // Got through the whole move.
	std::string errorMessage;
	size_t failedFiles;
	size_t skippedFiles;
};

bool FolderSeemsToBeUsed(const Path &newMemstickFolder);
bool SwitchMemstickFolderTo(Path newMemstickFolder);
MoveResult *MoveDirectoryContentsSafe(Path moveSrc, Path moveDest, MoveProgressReporter &progressReporter);

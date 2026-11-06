#pragma once

#include "Common/File/Path.h"

#include <mutex>
#include <string_view>

// Utility functions moved out from MemstickScreen.

class MoveProgressReporter {
public:
	void SetProgress(std::string_view value, size_t count = 0, size_t maxVal = 0) {
		std::lock_guard<std::mutex> guard(mutex_);
		progress_ = value;
		count_ = (int)count;
		max_ = (int)maxVal;
	}

	std::string Format();

private:
	std::string progress_;
	std::mutex mutex_;
	int count_;
	int max_;
};

struct MoveResult {
	bool success;  // Got through the whole move.
	std::string errorMessage;
	size_t failedFiles;
	size_t skippedFiles;
};

enum class MemstickFolderCheck {
	OK,
	// A different folder with the same PSP folder as the current one (a PSP folder and its parent). Nothing to move.
	SAME_PSP_FOLDER,
	// The current memory stick is inside the new one's PSP folder, so its data can't be moved there.
	CONTAINS_CURRENT,
	// Not allowed: inside the current memory stick's PSP folder.
	INSIDE_CURRENT,
	// Not allowed: inside the PSP folder of some other memory stick.
	INSIDE_OTHER,
};

// Checks a folder the user picked as the new memory stick. currentFolder may be empty.
MemstickFolderCheck CheckNewMemstickFolder(const Path &newFolder, const Path &currentFolder);

bool FolderSeemsToBeUsed(const Path &newMemstickFolder);
// The checks SwitchMemstickFolderTo() starts with, to run before moving any data.
bool CanSwitchMemstickFolderTo(const Path &newMemstickFolder);
bool SwitchMemstickFolderTo(Path newMemstickFolder);
MoveResult *MoveDirectoryContentsSafe(Path moveSrc, Path moveDest, MoveProgressReporter &progressReporter);

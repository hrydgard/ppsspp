#pragma once

#include "Common/File/Path.h"

#include <functional>
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
	// What the move is doing (like copying or checking), shown above the item it's on.
	void SetPhase(std::string_view phase) {
		std::lock_guard<std::mutex> guard(mutex_);
		phase_ = phase;
		progress_.clear();
		count_ = 0;
		max_ = 0;
	}

	std::string Format();

	// How far the current phase is, from 0 to 1. Negative when there's nothing to count.
	float Fraction() {
		std::lock_guard<std::mutex> guard(mutex_);
		if (max_ <= 0) {
			return -1.0f;
		}
		return (float)count_ / (float)max_;
	}

private:
	std::string phase_;
	std::string progress_;
	std::mutex mutex_;
	int count_ = 0;
	int max_ = 0;
};

struct MoveResult {
	bool success;  // Got through the whole move.
	std::string errorMessage;
	size_t failedFiles;
	// A failed move couldn't put everything back, so the data is partly in each folder.
	bool partlyMoved = false;
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
	// Not allowed: has PSP data directly in it, but wouldn't be used as the PSP folder (it's named something
	// else, or it's a PSP folder deeper in a picked tree), so the data would go into a new one inside.
	LOOKS_LIKE_PSP_FOLDER,
};

// Checks a folder the user picked as the new memory stick. currentFolder may be empty.
MemstickFolderCheck CheckNewMemstickFolder(const Path &newFolder, const Path &currentFolder);

bool FolderSeemsToBeUsed(const Path &newMemstickFolder);

// For showing a location to the user: splits Android shared storage, as a content URI or a native path, into
// its volume ("primary" for internal storage, otherwise an ID like "1234-ABCD") and the path inside it.
// False for anything else.
bool SplitStorageLocation(const Path &path, std::string *volume, std::string *pathInVolume);
// The checks SwitchMemstickFolderTo() starts with, to run before moving any data.
bool CanSwitchMemstickFolderTo(const Path &newMemstickFolder);
// Makes the next start use newMemstickFolder. SwitchMemstickFolderTo() does this and more.
bool WriteMemstickDirFile(const Path &newMemstickFolder);
bool SwitchMemstickFolderTo(Path newMemstickFolder);
// Moves the PSP folder's contents. beforeDeletingOriginals runs once everything is in the new folder (and
// for a copy, before the originals are deleted). The move is undone if it returns false. A failed move puts
// back what it can.
MoveResult *MoveDirectoryContentsSafe(Path moveSrc, Path moveDest, MoveProgressReporter &progressReporter, const std::function<bool()> &beforeDeletingOriginals = nullptr);

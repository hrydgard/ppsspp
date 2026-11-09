#include <algorithm>
#include <cstring>
#include <vector>

#include "Common/File/AndroidContentURI.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/System/System.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"

#include "Core/Util/MemStick.h"
#include "Core/Util/PathUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"

bool FolderSeemsToBeUsed(const Path &newMemstickFolder) {
	// Inspect the potential new folder, quickly. We could add more criteria like checking for actual savegames.
	const Path pspFolder = GetPSPFolderOf(newMemstickFolder);
	return File::Exists(pspFolder / "SAVEDATA") || File::Exists(pspFolder / "SYSTEM") || File::Exists(pspFolder / "PPSSPP_STATE");
}

namespace {

// Where a folder is, the same for a content URI and a native path to the same folder (which Path can't
// compare): on Android shared storage, the volume ID and the path in it, otherwise just the path.
// Compared case-insensitively, like Android's shared storage.
struct StorageLocation {
	// "primary" for internal storage, otherwise an ID like "1234-ABCD". Empty for other paths.
	std::string volume;
	// No trailing slash. Empty for the root of a volume.
	std::string path;

	bool operator==(const StorageLocation &other) const {
		return equalsNoCase(volume, other.volume) && equalsNoCase(path, other.path);
	}

	// Strictly inside folder, at any depth.
	bool IsInside(const StorageLocation &folder) const {
		if (!equalsNoCase(volume, folder.volume) || path.size() <= folder.path.size() || !startsWithNoCase(path, folder.path)) {
			return false;
		}
		// The root of a volume, or "/", contains everything below it.
		return folder.path.empty() || folder.path.back() == '/' || path[folder.path.size()] == '/';
	}

	StorageLocation WithComponent(std::string_view name) const {
		if (path.empty() || path.back() == '/') {
			return { volume, path + std::string(name) };
		}
		return { volume, path + "/" + std::string(name) };
	}
};

}  // namespace

// False if the location can't be told (another storage provider, with opaque document IDs).
static bool GetStorageLocation(const Path &path, StorageLocation *loc) {
	if (path.Type() == PathType::CONTENT_URI) {
		AndroidContentURI uri(path.ToString());
		if (uri.Provider() != "com.android.externalstorage.documents") {
			return false;
		}
		// The document ID, like "primary:Games/PSP".
		const std::string &id = uri.FilePath().empty() ? uri.RootPath() : uri.FilePath();
		const size_t colon = id.find(':');
		if (colon == std::string::npos || colon == 0) {
			return false;
		}
		loc->volume = id.substr(0, colon);
		loc->path = id.substr(colon + 1);
	} else if (path.Type() == PathType::NATIVE) {
		loc->volume.clear();
		loc->path = path.ToString();
		// Map Android storage paths to the volume IDs that content URIs use.
		static const std::string_view primaryPrefixes[] = { "/storage/emulated/0", "/sdcard", "/storage/self/primary", "/mnt/sdcard" };
		for (std::string_view prefix : primaryPrefixes) {
			if (loc->path == prefix || startsWith(loc->path, std::string(prefix) + "/")) {
				loc->volume = "primary";
				loc->path = loc->path.substr(std::min(loc->path.size(), prefix.size() + 1));
				break;
			}
		}
		if (loc->volume.empty() && startsWith(loc->path, "/storage/")) {
			// SD cards and USB drives: /storage/1234-ABCD/...
			const size_t volStart = strlen("/storage/");
			const size_t slash = loc->path.find('/', volStart);
			std::string volume = loc->path.substr(volStart, slash == std::string::npos ? std::string::npos : slash - volStart);
			if (!volume.empty() && volume != "emulated" && volume != "self") {
				loc->path = slash == std::string::npos ? std::string() : loc->path.substr(slash + 1);
				loc->volume = std::move(volume);
			}
		}
	} else {
		return false;
	}
	while (!loc->path.empty() && loc->path.back() == '/' && (loc->path.size() > 1 || !loc->volume.empty())) {
		loc->path.pop_back();
	}
	return !loc->volume.empty() || !loc->path.empty();
}

bool SplitStorageLocation(const Path &path, std::string *volume, std::string *pathInVolume) {
	StorageLocation loc;
	if (!GetStorageLocation(path, &loc) || loc.volume.empty()) {
		return false;
	}
	*volume = loc.volume;
	*pathInVolume = loc.path;
	return true;
}

// The location of GetPSPFolderOf(folder), by the same rule.
static bool GetPSPFolderLocation(const Path &folder, StorageLocation *loc) {
	if (!GetStorageLocation(folder, loc)) {
		return false;
	}
	if (!equalsNoCase(folder.GetFilename(), "PSP")) {
		*loc = loc->WithComponent("PSP");
	}
	return true;
}

static bool IsPSPSubfolderName(std::string_view name) {
	static const std::string_view names[] = {
		"SAVEDATA", "GAME", "SYSTEM", "PPSSPP_STATE", "TEXTURES", "PLUGINS", "CHEATS", "SCREENSHOT",
		"MUSIC", "PHOTO", "VIDEO", "COMMON",
	};
	for (std::string_view n : names) {
		if (equalsNoCase(name, n)) {
			return true;
		}
	}
	return false;
}

// Works from the name alone, for folders whose parents we can't look at (a picked content URI tree):
// a PSP folder with one of its usual subfolders below it, or a folder only PSP data is called, whatever
// its parent is called (a PSP folder may have been renamed).
static bool LooksInsidePSPFolder(const StorageLocation &loc) {
	std::vector<std::string_view> parts;
	SplitString(loc.path, '/', parts);
	parts.erase(std::remove(parts.begin(), parts.end(), std::string_view()), parts.end());
	for (size_t i = 0; i < parts.size(); i++) {
		if (equalsNoCase(parts[i], "SAVEDATA") || equalsNoCase(parts[i], "PPSSPP_STATE")) {
			return true;
		}
		if (i + 1 < parts.size() && equalsNoCase(parts[i], "PSP") && IsPSPSubfolderName(parts[i + 1])) {
			return true;
		}
	}
	return false;
}

// A PSP folder by its contents, whatever it's called. Two of the folders PPSSPP creates, so that some
// other folder that happens to have a SAVEDATA doesn't count.
static bool LooksLikePSPFolder(const Path &dir) {
	if (File::Exists(dir / "PPSSPP_STATE")) {
		return true;
	}
	return File::Exists(dir / "SAVEDATA") && (File::Exists(dir / "SYSTEM") || File::Exists(dir / "GAME"));
}

static bool IsInsideOtherMemstick(const Path &folder, const StorageLocation &loc) {
	if (LooksInsidePSPFolder(loc)) {
		return true;
	}
	if (folder.Type() != PathType::NATIVE) {
		return false;
	}
	// Native paths: look for a memory stick in use among the parents, or a PSP folder by another name.
	Path dir = folder;
	for (int depth = 0; depth < 64 && dir.CanNavigateUp(); depth++) {
		const Path up = dir.NavigateUp();
		if (up == dir || up.empty()) {
			break;
		}
		dir = up;
		StorageLocation dirPSP;
		if ((GetPSPFolderLocation(dir, &dirPSP) && loc.IsInside(dirPSP) && FolderSeemsToBeUsed(dir)) || LooksLikePSPFolder(dir)) {
			return true;
		}
	}
	return false;
}

MemstickFolderCheck CheckNewMemstickFolder(const Path &newFolder, const Path &currentFolder) {
	StorageLocation newLoc;
	StorageLocation newPSP;
	if (!GetStorageLocation(newFolder, &newLoc) || !GetPSPFolderLocation(newFolder, &newPSP)) {
		return MemstickFolderCheck::OK;
	}
	StorageLocation curPSP;
	const bool haveCurrent = !currentFolder.empty() && GetPSPFolderLocation(currentFolder, &curPSP);
	if (haveCurrent && newPSP.IsInside(curPSP)) {
		return MemstickFolderCheck::INSIDE_CURRENT;
	}
	if (IsInsideOtherMemstick(newFolder, newLoc)) {
		return MemstickFolderCheck::INSIDE_OTHER;
	}
	// A PSP folder that isn't used as one: we'd make a new PSP folder inside it.
	if (!(newPSP == newLoc) && !FolderSeemsToBeUsed(newFolder) && LooksLikePSPFolder(newFolder)) {
		return MemstickFolderCheck::LOOKS_LIKE_PSP_FOLDER;
	}
	if (haveCurrent) {
		if (newPSP == curPSP) {
			return MemstickFolderCheck::SAME_PSP_FOLDER;
		}
		if (curPSP.IsInside(newPSP)) {
			return MemstickFolderCheck::CONTAINS_CURRENT;
		}
	}
	return MemstickFolderCheck::OK;
}

static bool CanWriteTo(const Path &folder) {
	const Path testWriteFile = folder / ".write_verify_file";
	if (!File::WriteDataToFile(true, "1", 1, testWriteFile)) {
		return false;
	}
	File::Delete(testWriteFile);
	return true;
}

bool CanSwitchMemstickFolderTo(const Path &newMemstickFolder) {
	// Doesn't already exist, create. A picked folder (content URI) exists already.
	if (newMemstickFolder.Type() == PathType::NATIVE && !File::Exists(newMemstickFolder)) {
		File::CreateFullPath(newMemstickFolder);
	}
	// A picked folder can still be read-only (a write-protected SD card), or gone (deleted over USB).
	if (!CanWriteTo(newMemstickFolder)) {
		ERROR_LOG(Log::System, "Can't write to '%s'", newMemstickFolder.c_str());
		return false;
	}
	// Where memstick_dir.txt goes. Not set on desktops, which only get here through a debug start screen.
	if (!g_Config.internalDataDirectory.empty() && !CanWriteTo(g_Config.internalDataDirectory)) {
		ERROR_LOG(Log::System, "Can't write to '%s'", g_Config.internalDataDirectory.c_str());
		return false;
	}
	return true;
}

bool WriteMemstickDirFile(const Path &newMemstickFolder) {
	const Path memStickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
#if PPSSPP_PLATFORM(UWP)
	// No file means the default folder.
	if (newMemstickFolder == g_Config.internalDataDirectory) {
		return !File::Exists(memStickDirFile) || File::Delete(memStickDirFile);
	}
#endif
	// Written beside it, checked, and renamed over it, so that a failed write (a full disk) leaves the old
	// one in place rather than an empty one.
	const Path tempFile = g_Config.internalDataDirectory / "memstick_dir.txt.tmp";
	const std::string str = newMemstickFolder.ToString();
	std::string readBack;
	if (!File::WriteDataToFile(true, str.c_str(), str.size(), tempFile) || !File::ReadTextFileToString(tempFile, &readBack) || readBack != str) {
		ERROR_LOG(Log::System, "Failed to write memstick path '%s' to '%s'", newMemstickFolder.c_str(), tempFile.c_str());
		File::Delete(tempFile);
		return false;
	}
#if PPSSPP_PLATFORM(WINDOWS)
	// Renaming doesn't replace an existing file here.
	File::Delete(memStickDirFile);
#endif
	if (!File::Rename(tempFile, memStickDirFile)) {
		ERROR_LOG(Log::System, "Failed to rename '%s' to '%s'", tempFile.c_str(), memStickDirFile.c_str());
		File::Delete(tempFile);
		return false;
	}
	return true;
}

bool SwitchMemstickFolderTo(Path newMemstickFolder) {
	if (!CanSwitchMemstickFolderTo(newMemstickFolder) || !WriteMemstickDirFile(newMemstickFolder)) {
		return false;
	}

	// Save so the settings, at least, are transferred.
	g_Config.memStickDirectory = newMemstickFolder;
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.UpdateIniLocation();
	return true;
}

// Keep the size with the file, to verify the copy.
struct FileSuffix {
	std::string suffix;
	u64 fileSize;
};

// Fails if any part of the tree can't be listed. The move deletes the source folders afterwards, and on
// content URIs that deletes whatever is still in them, so anything left out here would be lost.
static bool ListFileSuffixesRecursively(const Path &root, const Path &folder, std::vector<std::string> &dirSuffixes, std::vector<FileSuffix> &fileSuffixes, MoveProgressReporter &progressReporter) {
	std::vector<File::FileInfo> files;
	if (!File::GetFilesInDir(folder, &files)) {
		ERROR_LOG(Log::System, "Failed to list '%s'", folder.c_str());
		return false;
	}

	for (auto &file : files) {
		std::string suffix;
		if (!root.ComputePathTo(file.fullName, suffix) || suffix.empty()) {
			ERROR_LOG_REPORT(Log::System, "Failed to compute PathTo from '%s' to '%s'", root.c_str(), file.fullName.c_str());
			return false;
		}
		if (file.isDirectory) {
			dirSuffixes.push_back(suffix);
			if (!ListFileSuffixesRecursively(root, folder / file.name, dirSuffixes, fileSuffixes, progressReporter)) {
				return false;
			}
			progressReporter.SetProgress(file.name, fileSuffixes.size(), (size_t)-1);
		} else {
			fileSuffixes.push_back(FileSuffix{ suffix, file.size });
		}
	}

	return true;
}

std::string MoveProgressReporter::Format() {
	std::string str;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (!phase_.empty()) {
			str = phase_;
			if (max_ > 0) {
				str += StringFromFormat(" (%d/%d)", count_ + 1, max_);
			} else if (max_ < 0) {
				str += StringFromFormat(" (%d/?)", count_ + 1);
			}
			if (!progress_.empty()) {
				str += "\n";
			}
		} else if (max_ > 0) {
			str = StringFromFormat("(%d/%d) ", count_ + 1, max_);
		} else if (max_ < 0) {
			str = StringFromFormat("(%d/?) ", count_ + 1);
		}
		str += progress_;
	}
	return str;
}

// What a move has changed so far, so that a failed one can put everything back.
struct MoveUndo {
	// Children of the source PSP folder that were renamed into the destination.
	std::vector<std::string> renamed;
	// Created by the copy, in creation order. A file that was already there and got overwritten isn't here.
	std::vector<Path> createdDirs;
	std::vector<Path> createdFiles;
	bool createdDest = false;
};

static bool MoveChildrenFast(const Path &moveSrc, const Path &moveDest, MoveProgressReporter &progressReporter, MoveUndo &undo) {
	std::vector<File::FileInfo> files;
	if (!File::GetFilesInDir(moveSrc, &files)) {
		return false;
	}

	for (size_t i = 0; i < files.size(); i++) {
		auto &file = files[i];
		// Construct destination path
		Path fileSrc = file.fullName;
		Path fileDest = moveDest / file.name;
		progressReporter.SetProgress(file.name, i, files.size());
		INFO_LOG(Log::System, "Fast-moving PSP data from '%s' to '%s' (%d/%d)", fileSrc.c_str(), fileDest.c_str(), (int)i + 1, (int)files.size());
		if (!File::MoveIfFast(fileSrc, fileDest)) {
			INFO_LOG(Log::System, "Failed to fast-move '%s' to '%s'", fileSrc.c_str(), fileDest.c_str());
			return false;
		}
		undo.renamed.push_back(file.name);
	}
	INFO_LOG(Log::System, "Done with fast-move.");
	return true;
}

// Puts back what a failed move changed. False if a renamed item couldn't be moved back, which leaves the
// data partly in each folder.
static bool UndoMove(const Path &moveSrc, const Path &moveDest, const MoveUndo &undo) {
	// The renamed items first: on content URIs, deleting a folder below takes everything in it along.
	bool restored = true;
	for (auto it = undo.renamed.rbegin(); it != undo.renamed.rend(); ++it) {
		if (!File::MoveIfFast(moveDest / *it, moveSrc / *it)) {
			ERROR_LOG(Log::System, "Failed to move '%s' back to '%s'", (moveDest / *it).c_str(), moveSrc.c_str());
			restored = false;
		}
	}
	// The copies. Their originals are all still in place.
	for (auto it = undo.createdFiles.rbegin(); it != undo.createdFiles.rend(); ++it) {
		File::Delete(*it);
	}
	for (auto it = undo.createdDirs.rbegin(); it != undo.createdDirs.rend(); ++it) {
		File::DeleteDir(*it);
	}
	// Unless something that couldn't be moved back is still in it.
	if (undo.createdDest && restored) {
		File::DeleteDir(moveDest);
	}
	return restored;
}

MoveResult *MoveDirectoryContentsSafe(Path moveSrc, Path moveDest, MoveProgressReporter &progressReporter, const std::function<bool()> &beforeDeletingOriginals) {
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);
	moveSrc = GetPSPFolderOf(moveSrc);
	moveDest = GetPSPFolderOf(moveDest);

	// The UI doesn't offer these (see CheckNewMemstickFolder), but moving a folder into itself would be a mess.
	StorageLocation srcLoc;
	StorageLocation destLoc;
	if (GetStorageLocation(moveSrc, &srcLoc) && GetStorageLocation(moveDest, &destLoc) && (srcLoc == destLoc || srcLoc.IsInside(destLoc) || destLoc.IsInside(srcLoc))) {
		ERROR_LOG(Log::System, "Refusing to move PSP data from '%s' to '%s'", moveSrc.c_str(), moveDest.c_str());
		return new MoveResult{ false, "Source and destination overlap" };
	}

	MoveUndo undo;
	if (!File::Exists(moveDest)) {
		undo.createdDest = File::CreateDir(moveDest);
	}

	// Anything that fails from here on puts back what was changed.
	auto fail = [&](const char *why, size_t failedFiles) {
		ERROR_LOG(Log::System, "Move failed: %s. Putting things back.", why);
		progressReporter.SetPhase(ms->T("Putting things back..."));
		MoveResult *result = new MoveResult{ false, why, failedFiles };
		result->partlyMoved = !UndoMove(moveSrc, moveDest, undo);
		return result;
	};

	INFO_LOG(Log::System, "About to move PSP data from '%s' to '%s'", moveSrc.c_str(), moveDest.c_str());

	Instant moveStart = Instant::Now();

	// First, we try the cheapest and safest way to move: Can we move files directly within the same device?
	// We loop through the files/dirs in the source directory and just try to move them, it should work.
	progressReporter.SetPhase(ms->T("Moving..."));
	if (MoveChildrenFast(moveSrc, moveDest, progressReporter, undo)) {
		INFO_LOG(Log::System, "Quick-move succeeded after %0.1f ms", moveStart.ElapsedMs());
		if (beforeDeletingOriginals && !beforeDeletingOriginals()) {
			return fail("Couldn't switch to the new folder", 0);
		}
		progressReporter.SetPhase(T(I18NCat::DIALOG, "Done!"));
		return new MoveResult{ true, "" };
	} else {
		INFO_LOG(Log::System, "Quick move denied after %0.1f ms, falling back to slow move.", moveStart.ElapsedMs());
	}

	// If this doesn't work, we'll fall back on a recursive *copy* (disk space is less of a concern when
	// moving from device to device, other than that everything fits on the destination).
	// Then we verify the results before we delete the originals.

	// Search through recursively, listing the files to move and also summing their sizes.
	std::vector<FileSuffix> fileSuffixesToMove;
	std::vector<std::string> directorySuffixesToCreate;

	progressReporter.SetPhase(ms->T("Finding files..."));
	// NOTE: It's correct to pass moveSrc twice here, it's to keep the root in the recursion.
	if (!ListFileSuffixesRecursively(moveSrc, moveSrc, directorySuffixesToCreate, fileSuffixesToMove, progressReporter)) {
		return fail("Failed to read old directory", 0);
	}

	// Better not interrupt the app while this is happening!

	progressReporter.SetPhase(ms->T("Copying..."));
	// Create all the necessary directories.
	for (size_t i = 0; i < directorySuffixesToCreate.size(); i++) {
		const auto &dirSuffix = directorySuffixesToCreate[i];
		Path dir = moveDest / dirSuffix;
		INFO_LOG(Log::System, "Creating dir '%s'", dir.c_str());
		progressReporter.SetProgress(dirSuffix);
		// Already there is fine (merging into existing data).
		if (!File::Exists(dir) && File::CreateDir(dir)) {
			undo.createdDirs.push_back(dir);
		}
	}

	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const auto &fileSuffix = fileSuffixesToMove[i];
		progressReporter.SetProgress(StringFromFormat("%s (%s)", fileSuffix.suffix.c_str(), NiceSizeFormat(fileSuffix.fileSize).c_str()),
			i, fileSuffixesToMove.size());

		Path from = moveSrc / fileSuffix.suffix;
		Path to = moveDest / fileSuffix.suffix;

		const bool existed = File::Exists(to);
		if (existed) {
			WARN_LOG(Log::System, "Target file '%s' already exists. Will be overwritten", to.c_str());
		}
		// Before copying, so that a partly written file is removed too.
		if (!existed) {
			undo.createdFiles.push_back(to);
		}
		if (!File::Copy(from, to)) {
			ERROR_LOG(Log::System, "Failed to copy file '%s' to '%s'", from.c_str(), to.c_str());
			return fail("Copy failed", 1);
		}
		INFO_LOG(Log::System, "Copied file '%s' to '%s' (size: %d)", from.c_str(), to.c_str(), (int)fileSuffix.fileSize);
	}

	INFO_LOG(Log::System, "Done copying. Moving to verification.");

	// After the whole move, verify that all the files arrived correctly.
	// If there's a single error, we do not delete the source data.
	progressReporter.SetPhase(ms->T("Checking..."));
	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const auto &fileSuffix = fileSuffixesToMove[i];
		progressReporter.SetProgress(fileSuffix.suffix, i, fileSuffixesToMove.size());

		Path to = moveDest / fileSuffix.suffix;

		File::FileInfo info;
		if (!File::GetFileInfo(to, &info) || !info.exists) {
			ERROR_LOG(Log::System, "File '%s' missing at target location '%s'.", fileSuffix.suffix.c_str(), to.c_str());
			return fail("Verification failed", 1);
		}

		// More lenient handling for .nomedia files.
		if (fileSuffix.fileSize != info.size && !endsWithNoCase(fileSuffix.suffix, ".nomedia")) {
			ERROR_LOG(Log::System, "Mismatched size in target file %s (expected %d, was %d bytes). Verification failed.", fileSuffix.suffix.c_str(), (int)fileSuffix.fileSize, (int)info.size);
			return fail("Verification failed", 1);
		}
	}

	INFO_LOG(Log::System, "Verification complete");

	// From here on the new folder has everything, so it has to be the one used next time before any
	// original is deleted. If we're interrupted while deleting, what's left in the old folder are copies.
	if (beforeDeletingOriginals && !beforeDeletingOriginals()) {
		return fail("Couldn't switch to the new folder", 0);
	}

	progressReporter.SetPhase(ms->T("Deleting from old folder..."));
	if (moveSrc.Type() == PathType::CONTENT_URI) {
		// DeleteDir takes a folder's contents along here, so only the top level needs deleting, one call
		// each instead of one per file. All of it is safely copied: the listing fails if any part of the
		// tree can't be read.
		std::vector<std::string> topLevel;
		for (const std::string &dirSuffix : directorySuffixesToCreate) {
			if (dirSuffix.find('/') == std::string::npos) {
				topLevel.push_back(dirSuffix);
			}
		}
		const size_t dirCount = topLevel.size();
		for (const FileSuffix &fileSuffix : fileSuffixesToMove) {
			if (fileSuffix.suffix.find('/') == std::string::npos) {
				topLevel.push_back(fileSuffix.suffix);
			}
		}
		for (size_t i = 0; i < topLevel.size(); i++) {
			const Path item = moveSrc / topLevel[i];
			progressReporter.SetProgress(topLevel[i], i, topLevel.size());
			if (!(i < dirCount ? File::DeleteDir(item) : File::Delete(item))) {
				// It's safely copied, so this only leaves a duplicate behind.
				WARN_LOG(Log::System, "Failed to delete '%s' after copying it", item.c_str());
			}
		}
		return new MoveResult{ true, "", 0 };
	}

	// Native paths: the files, then the folders, since DeleteDir only removes empty ones.
	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const Path from = moveSrc / fileSuffixesToMove[i].suffix;
		progressReporter.SetProgress(fileSuffixesToMove[i].suffix, i, fileSuffixesToMove.size());
		if (!File::Delete(from)) {
			// It's safely copied, so this only leaves a duplicate behind.
			WARN_LOG(Log::System, "Failed to delete '%s' after copying it", from.c_str());
		}
	}

	// Reverse the order to delete subfolders before parents.
	std::reverse(directorySuffixesToCreate.begin(), directorySuffixesToCreate.end());
	for (size_t i = 0; i < directorySuffixesToCreate.size(); i++) {
		const auto &dirSuffix = directorySuffixesToCreate[i];
		Path dir = moveSrc / dirSuffix;
		INFO_LOG(Log::System, "Deleting dir '%s'", dir.c_str());
		progressReporter.SetProgress(dirSuffix, i, directorySuffixesToCreate.size());
		if (File::Exists(dir)) {
			File::DeleteDir(dir);
		}
	}
	return new MoveResult{ true, "", 0 };
}

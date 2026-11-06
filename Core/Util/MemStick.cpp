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
	return File::Exists(pspFolder / "SAVEDATA") || File::Exists(pspFolder / "SYSTEM");
}

// Checked by string rather than Type(), which only says CONTENT_URI on Android, so the checks below can
// be unit tested anywhere.
static bool IsContentURI(const Path &path) {
	return startsWith(path.ToString(), "content://");
}

// A form of a folder's location that compares between content URIs and native paths: "volume:path" for
// Android shared storage, the plain path otherwise. Empty if the location can't be told.
static std::string LocationKey(const Path &path) {
	std::string key;
	if (IsContentURI(path)) {
		AndroidContentURI uri(path.ToString());
		// Other providers (like Downloads) use opaque document IDs.
		if (uri.Provider() != "com.android.externalstorage.documents") {
			return std::string();
		}
		key = uri.FilePath().empty() ? uri.RootPath() : uri.FilePath();
	} else if (path.Type() == PathType::NATIVE) {
		key = path.ToString();
		// Map Android storage paths to the volume IDs that content URIs use.
		static const std::string_view primaryPrefixes[] = { "/storage/emulated/0", "/sdcard", "/storage/self/primary", "/mnt/sdcard" };
		bool mapped = false;
		for (std::string_view prefix : primaryPrefixes) {
			if (key == prefix || startsWith(key, std::string(prefix) + "/")) {
				key = "primary:" + key.substr(std::min(key.size(), prefix.size() + 1));
				mapped = true;
				break;
			}
		}
		if (!mapped && startsWith(key, "/storage/")) {
			// SD cards and USB drives: /storage/1234-ABCD/...
			const size_t volStart = strlen("/storage/");
			const size_t slash = key.find('/', volStart);
			const std::string volume = key.substr(volStart, slash == std::string::npos ? std::string::npos : slash - volStart);
			if (!volume.empty() && volume != "emulated" && volume != "self") {
				key = volume + ":" + (slash == std::string::npos ? std::string() : key.substr(slash + 1));
			}
		}
	} else {
		return std::string();
	}
	while (key.size() > 1 && key.back() == '/') {
		key.pop_back();
	}
	return key;
}

// True if child is strictly inside parent. Case-insensitive, like Android's shared storage.
static bool KeyIsInside(std::string_view child, std::string_view parent) {
	if (parent.empty() || child.size() <= parent.size() || !startsWithNoCase(child, parent)) {
		return false;
	}
	const char last = parent.back();
	return last == '/' || last == ':' || child[parent.size()] == '/';
}

// LocationKey() of GetPSPFolderOf(folder), by the same rule.
static std::string PSPFolderKey(const Path &folder) {
	const std::string key = LocationKey(folder);
	if (key.empty()) {
		return key;
	}
	// Path::GetFilename() is GetLastPart() for content URIs.
	const std::string name = IsContentURI(folder) ? AndroidContentURI(folder.ToString()).GetLastPart() : folder.GetFilename();
	if (equalsNoCase(name, "PSP")) {
		return key;
	}
	return key + (key.back() == ':' || key.back() == '/' ? "PSP" : "/PSP");
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
// a PSP folder with one of its usual subfolders below it.
static bool KeyLooksInsidePSPFolder(std::string_view key) {
	const size_t colon = key.find(':');
	std::vector<std::string_view> parts;
	SplitString(key.substr(colon == std::string_view::npos ? 0 : colon + 1), '/', parts);
	parts.erase(std::remove(parts.begin(), parts.end(), std::string_view()), parts.end());
	for (size_t i = 0; i + 1 < parts.size(); i++) {
		if (equalsNoCase(parts[i], "PSP") && IsPSPSubfolderName(parts[i + 1])) {
			return true;
		}
	}
	return false;
}

static bool IsInsideOtherMemstick(const Path &folder, const std::string &key) {
	if (KeyLooksInsidePSPFolder(key)) {
		return true;
	}
	if (folder.Type() != PathType::NATIVE || IsContentURI(folder)) {
		return false;
	}
	// Native paths: look for a memory stick in use among the parents.
	Path dir = folder;
	for (int depth = 0; depth < 64 && dir.CanNavigateUp(); depth++) {
		const Path up = dir.NavigateUp();
		if (up == dir || up.empty()) {
			break;
		}
		dir = up;
		if (KeyIsInside(key, PSPFolderKey(dir)) && FolderSeemsToBeUsed(dir)) {
			return true;
		}
	}
	return false;
}

MemstickFolderCheck CheckNewMemstickFolder(const Path &newFolder, const Path &currentFolder) {
	const std::string newKey = LocationKey(newFolder);
	if (newKey.empty()) {
		return MemstickFolderCheck::OK;
	}
	const std::string newPSPKey = PSPFolderKey(newFolder);
	std::string curPSPKey;
	if (!currentFolder.empty()) {
		curPSPKey = PSPFolderKey(currentFolder);
		if (KeyIsInside(newPSPKey, curPSPKey)) {
			return MemstickFolderCheck::INSIDE_CURRENT;
		}
	}
	if (IsInsideOtherMemstick(newFolder, newKey)) {
		return MemstickFolderCheck::INSIDE_OTHER;
	}
	if (!curPSPKey.empty()) {
		if (equalsNoCase(newPSPKey, curPSPKey)) {
			return MemstickFolderCheck::SAME_PSP_FOLDER;
		}
		if (KeyIsInside(curPSPKey, newPSPKey)) {
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
	// Doesn't already exist, create.
	// Should this ever happen?
	if (newMemstickFolder.Type() == PathType::NATIVE) {
		if (!File::Exists(newMemstickFolder)) {
			File::CreateFullPath(newMemstickFolder);
		}
		if (!CanWriteTo(newMemstickFolder)) {
			ERROR_LOG(Log::System, "Can't write to '%s'", newMemstickFolder.c_str());
			return false;
		}
	} else {
		// TODO: Do the same but with scoped storage? Not really necessary, right? If it came from a browse
		// for folder, we can assume it exists and is writable, barring wacky race conditions like the user
		// being connected by USB and deleting it.
	}
	// Where memstick_dir.txt goes. Not set on desktops, which only get here through a debug start screen.
	if (!g_Config.internalDataDirectory.empty() && !CanWriteTo(g_Config.internalDataDirectory)) {
		ERROR_LOG(Log::System, "Can't write to '%s'", g_Config.internalDataDirectory.c_str());
		return false;
	}
	return true;
}

bool SwitchMemstickFolderTo(Path newMemstickFolder) {
	if (!CanSwitchMemstickFolderTo(newMemstickFolder)) {
		return false;
	}

	Path memStickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
#if PPSSPP_PLATFORM(UWP)
	File::Delete(memStickDirFile);
	if (newMemstickFolder != g_Config.internalDataDirectory) {
#endif

		std::string str = newMemstickFolder.ToString();
		if (!File::WriteDataToFile(true, str.c_str(), str.size(), memStickDirFile)) {
			// Disk full? Without it, the next start would be back on the old folder.
			ERROR_LOG(Log::System, "Failed to write memstick path '%s' to '%s'", newMemstickFolder.c_str(), memStickDirFile.c_str());
			return false;
		}

#if PPSSPP_PLATFORM(UWP)
	}
#endif

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

bool MoveChildrenFast(const Path &moveSrc, const Path &moveDest, MoveProgressReporter &progressReporter) {
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
		bool result = File::MoveIfFast(fileSrc, fileDest);
		if (!result) {
			// TODO: Should we try to move back anything that succeeded before this one?
			INFO_LOG(Log::System, "Failed to fast-move '%s' to '%s'", fileSrc.c_str(), fileDest.c_str());
			return false;
		}
	}
	INFO_LOG(Log::System, "Done with fast-move.");
	return true;
}

std::string MoveProgressReporter::Format() {
	std::string str;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (max_ > 0) {
			str = StringFromFormat("(%d/%d) ", count_ + 1, max_);
		} else if (max_ < 0) {
			str = StringFromFormat("(%d/?) ", count_ + 1);
		}
		str += progress_;
	}
	return str;
}

MoveResult *MoveDirectoryContentsSafe(Path moveSrc, Path moveDest, MoveProgressReporter &progressReporter) {
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);
	moveSrc = GetPSPFolderOf(moveSrc);
	moveDest = GetPSPFolderOf(moveDest);

	// The UI doesn't offer these (see CheckNewMemstickFolder), but moving a folder into itself would be a mess.
	const std::string srcKey = LocationKey(moveSrc);
	const std::string destKey = LocationKey(moveDest);
	if (!srcKey.empty() && !destKey.empty() && (equalsNoCase(srcKey, destKey) || KeyIsInside(srcKey, destKey) || KeyIsInside(destKey, srcKey))) {
		ERROR_LOG(Log::System, "Refusing to move PSP data from '%s' to '%s'", moveSrc.c_str(), moveDest.c_str());
		return new MoveResult{ false, "Source and destination overlap" };
	}
	File::CreateDir(moveDest);

	INFO_LOG(Log::System, "About to move PSP data from '%s' to '%s'", moveSrc.c_str(), moveDest.c_str());

	Instant moveStart = Instant::Now();

	// First, we try the cheapest and safest way to move: Can we move files directly within the same device?
	// We loop through the files/dirs in the source directory and just try to move them, it should work.
	if (MoveChildrenFast(moveSrc, moveDest, progressReporter)) {
		INFO_LOG(Log::System, "Quick-move succeeded after %0.1f ms", moveStart.ElapsedMs());
		progressReporter.SetProgress(StringFromFormat("%s (%0.3f)", T_cstr(I18NCat::DIALOG, "Done!"), moveStart.ElapsedSeconds()));
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

	// NOTE: It's correct to pass moveSrc twice here, it's to keep the root in the recursion.
	if (!ListFileSuffixesRecursively(moveSrc, moveSrc, directorySuffixesToCreate, fileSuffixesToMove, progressReporter)) {
		INFO_LOG(Log::System, "Failed to read old directory");
		progressReporter.SetProgress(ms->T("Failed to read old directory"));
		return new MoveResult{ false, "Failed to read old directory" };
	}

	bool dryRun = false;  // Useful for debugging. Probably want to remove the MoveChildrenFast path above to test it.

	size_t failedFileCount = 0;

	if (moveSrc.empty()) {
		// Shouldn't happen.
		return new MoveResult{ true, "", };
	}

	// Better not interrupt the app while this is happening!

	// Create all the necessary directories.
	for (size_t i = 0; i < directorySuffixesToCreate.size(); i++) {
		const auto &dirSuffix = directorySuffixesToCreate[i];
		Path dir = moveDest / dirSuffix;
		if (dryRun) {
			INFO_LOG(Log::System, "dry run: Would have created dir '%s'", dir.c_str());
		} else {
			INFO_LOG(Log::System, "Creating dir '%s'", dir.c_str());
			progressReporter.SetProgress(dirSuffix);
			// Just ignore already-exists errors.
			File::CreateDir(dir);
		}
	}

	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const auto &fileSuffix = fileSuffixesToMove[i];
		progressReporter.SetProgress(StringFromFormat("%s (%s)", fileSuffix.suffix.c_str(), NiceSizeFormat(fileSuffix.fileSize).c_str()),
			i, fileSuffixesToMove.size());

		Path from = moveSrc / fileSuffix.suffix;
		Path to = moveDest / fileSuffix.suffix;

		if (dryRun) {
			INFO_LOG(Log::System, "dry run: Would have moved '%s' to '%s' (%d bytes)", from.c_str(), to.c_str(), (int)fileSuffix.fileSize);
		} else {
			// Remove the "from" prefix from the path.
			// We have to drop down to string operations for this.
			if (File::Exists(to)) {
				WARN_LOG(Log::System, "Target file '%s' already exists. Will be overwritten", to.c_str());
			}

			if (!File::Copy(from, to)) {
				ERROR_LOG(Log::System, "Failed to copy file '%s' to '%s'", from.c_str(), to.c_str());
				failedFileCount++;
				// Should probably just bail?
			} else {
				INFO_LOG(Log::System, "Copied file '%s' to '%s' (size: %d)", from.c_str(), to.c_str(), (int)fileSuffix.fileSize);
			}
		}
	}

	if (failedFileCount) {
		ERROR_LOG(Log::System, "Copy failed (%d files failed)", (int)failedFileCount);
		return new MoveResult{ false, "", failedFileCount };
	}

	INFO_LOG(Log::System, "Done copying. Moving to verification.");

	// After the whole move, verify that all the files arrived correctly.
	// If there's a single error, we do not delete the source data.
	bool ok = true;
	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const auto &fileSuffix = fileSuffixesToMove[i];
		progressReporter.SetProgress(ms->T("Checking..."), i, fileSuffixesToMove.size());

		Path to = moveDest / fileSuffix.suffix;

		File::FileInfo info;
		if (!File::GetFileInfo(to, &info)) {
			ok = false;
			break;
		}

		if (!info.exists) {
			ERROR_LOG(Log::System, "File '%s' missing at target location '%s'.", fileSuffix.suffix.c_str(), to.c_str());
			ok = false;
			failedFileCount++;
			break;
		}

		// More lenient handling for .nomedia files.
		if (fileSuffix.fileSize != info.size && !endsWithNoCase(fileSuffix.suffix, ".nomedia")) {
			ERROR_LOG(Log::System, "Mismatched size in target file %s (expected %d, was %d bytes). Verification failed.", fileSuffix.suffix.c_str(), (int)fileSuffix.fileSize, (int)info.size);
			ok = false;
			failedFileCount++;
			break;
		}
	}

	if (!ok) {
		return new MoveResult{ false, "", failedFileCount };
	}

	INFO_LOG(Log::System, "Verification complete");

	// Everything arrived, so delete the originals: the files, then the folders. On content URIs DeleteDir
	// would take the files along anyway, but on native paths it only removes empty folders.
	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const Path from = moveSrc / fileSuffixesToMove[i].suffix;
		if (dryRun) {
			INFO_LOG(Log::System, "dry run: Would have deleted '%s'", from.c_str());
			continue;
		}
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
		if (dryRun) {
			INFO_LOG(Log::System, "dry run: Would have deleted dir '%s'", dir.c_str());
		} else {
			INFO_LOG(Log::System, "Deleting dir '%s'", dir.c_str());
			progressReporter.SetProgress(dirSuffix, i, directorySuffixesToCreate.size());
			if (File::Exists(dir)) {
				File::DeleteDir(dir);
			}
		}
	}
	return new MoveResult{ true, "", 0 };
}

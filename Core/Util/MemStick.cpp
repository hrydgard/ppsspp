#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"

#include "Core/Util/MemStick.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"

bool FolderSeemsToBeUsed(const Path &newMemstickFolder) {
	// Inspect the potential new folder, quickly.
	if (File::Exists(newMemstickFolder / "PSP/SAVEDATA") || File::Exists(newMemstickFolder / "SAVEDATA")) {
		// Does seem likely. We could add more criteria like checking for actual savegames or something.
		return true;
	} else {
		return false;
	}
}

bool SwitchMemstickFolderTo(Path newMemstickFolder) {
	// Doesn't already exist, create.
	// Should this ever happen?
	if (newMemstickFolder.Type() == PathType::NATIVE) {
		if (!File::Exists(newMemstickFolder)) {
			File::CreateFullPath(newMemstickFolder);
		}
		Path testWriteFile = newMemstickFolder / ".write_verify_file";
		if (!File::WriteDataToFile(true, "1", 1, testWriteFile)) {
			return false;
		}
		File::Delete(testWriteFile);
	} else {
		// TODO: Do the same but with scoped storage? Not really necessary, right? If it came from a browse
		// for folder, we can assume it exists and is writable, barring wacky race conditions like the user
		// being connected by USB and deleting it.
	}

	Path memStickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
#if PPSSPP_PLATFORM(UWP)
	File::Delete(memStickDirFile);
	if (newMemstickFolder != g_Config.internalDataDirectory) {
#endif

		std::string str = newMemstickFolder.ToString();
		if (!File::WriteDataToFile(true, str.c_str(), (unsigned int)str.size(), memStickDirFile)) {
			ERROR_LOG(SYSTEM, "Failed to write memstick path '%s' to '%s'", newMemstickFolder.c_str(), memStickDirFile.c_str());
			// Not sure what to do if this file can't be written.  Disk full?
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

// Keep the size with the file, so we can skip overly large ones in the move.
// The user will have to take care of them afterwards, it'll just take too long probably.
struct FileSuffix {
	std::string suffix;
	u64 fileSize;
};

static bool ListFileSuffixesRecursively(const Path &root, const Path &folder, std::vector<std::string> &dirSuffixes, std::vector<FileSuffix> &fileSuffixes, MoveProgressReporter &progressReporter) {
	std::vector<File::FileInfo> files;
	if (!File::GetFilesInDir(folder, &files)) {
		return false;
	}

	for (auto &file : files) {
		if (file.isDirectory) {
			std::string dirSuffix;
			if (root.ComputePathTo(file.fullName, dirSuffix)) {
				if (!dirSuffix.empty()) {
					dirSuffixes.push_back(dirSuffix);
					ListFileSuffixesRecursively(root, folder / file.name, dirSuffixes, fileSuffixes, progressReporter);
					progressReporter.SetProgress(file.name, fileSuffixes.size(), (size_t)-1);
				}
			} else {
				ERROR_LOG_REPORT(SYSTEM, "Failed to compute PathTo from '%s' to '%s'", root.c_str(), folder.c_str());
			}
		} else {
			std::string fileSuffix;
			if (root.ComputePathTo(file.fullName, fileSuffix)) {
				if (!fileSuffix.empty()) {
					fileSuffixes.push_back(FileSuffix{ fileSuffix, file.size });
				}
			}
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
		INFO_LOG(SYSTEM, "About to move PSP data from '%s' to '%s'", fileSrc.c_str(), fileDest.c_str());
		bool result = File::MoveIfFast(fileSrc, fileDest);
		if (!result) {
			// TODO: Should we try to move back anything that succeeded before this one?
			return false;
		}
	}
	return true;
}

std::string MoveProgressReporter::Format() {
	std::string str;
	{
		std::lock_guard<std::mutex> guard(mutex_);
		if (max_ > 0) {
			str = StringFromFormat("(%d/%d) ", count_, max_);
		} else if (max_ < 0) {
			str = StringFromFormat("(%d) ", count_);
		}
		str += progress_;
	}
	return str;
}


MoveResult *MoveDirectoryContentsSafe(Path moveSrc, Path moveDest, MoveProgressReporter &progressReporter) {
	auto ms = GetI18NCategory(I18NCat::MEMSTICK);
	if (moveSrc.GetFilename() != "PSP") {
		moveSrc = moveSrc / "PSP";
	}
	if (moveDest.GetFilename() != "PSP") {
		moveDest = moveDest / "PSP";
		File::CreateDir(moveDest);
	}

	INFO_LOG(SYSTEM, "About to move PSP data from '%s' to '%s'", moveSrc.c_str(), moveDest.c_str());

	// First, we try the cheapest and safest way to move: Can we move files directly within the same device?
	// We loop through the files/dirs in the source directory and just try to move them, it should work.
	if (MoveChildrenFast(moveSrc, moveDest, progressReporter)) {
		INFO_LOG(SYSTEM, "Quick-move succeeded");
		progressReporter.SetProgress(ms->T("Done!"));
		return new MoveResult{
			true, ""
		};
	}

	// If this doesn't work, we'll fall back on a recursive *copy* (disk space is less of a concern when
	// moving from device to device, other than that everything fits on the destination).
	// Then we verify the results before we delete the originals.

	// Search through recursively, listing the files to move and also summing their sizes.
	std::vector<FileSuffix> fileSuffixesToMove;
	std::vector<std::string> directorySuffixesToCreate;

	// NOTE: It's correct to pass moveSrc twice here, it's to keep the root in the recursion.
	if (!ListFileSuffixesRecursively(moveSrc, moveSrc, directorySuffixesToCreate, fileSuffixesToMove, progressReporter)) {
		// TODO: Handle failure listing files.
		std::string error = "Failed to read old directory";
		INFO_LOG(SYSTEM, "%s", error.c_str());
		progressReporter.SetProgress(ms->T(error.c_str()));
		return new MoveResult{ false, error };
	}

	bool dryRun = false;  // Useful for debugging.

	size_t failedFiles = 0;

	// We're not moving huge files like ISOs during this process, unless
	// they can be directly moved, without rewriting the file.
	const uint64_t BIG_FILE_THRESHOLD = 24 * 1024 * 1024;

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
			INFO_LOG(SYSTEM, "dry run: Would have created dir '%s'", dir.c_str());
		} else {
			INFO_LOG(SYSTEM, "Creating dir '%s'", dir.c_str());
			progressReporter.SetProgress(dirSuffix);
			// Just ignore already-exists errors.
			File::CreateDir(dir);
		}
	}

	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const auto &fileSuffix = fileSuffixesToMove[i];
		progressReporter.SetProgress(StringFromFormat("%s (%s)", fileSuffix.suffix.c_str(), NiceSizeFormat(fileSuffix.fileSize).c_str()),
			(int)i, (int)fileSuffixesToMove.size());

		Path from = moveSrc / fileSuffix.suffix;
		Path to = moveDest / fileSuffix.suffix;

		if (dryRun) {
			INFO_LOG(SYSTEM, "dry run: Would have moved '%s' to '%s' (%d bytes)", from.c_str(), to.c_str(), (int)fileSuffix.fileSize);
		} else {
			// Remove the "from" prefix from the path.
			// We have to drop down to string operations for this.
			if (!File::Copy(from, to)) {
				ERROR_LOG(SYSTEM, "Failed to copy file '%s' to '%s'", from.c_str(), to.c_str());
				failedFiles++;
				// Should probably just bail?
			} else {
				INFO_LOG(SYSTEM, "Copied file '%s' to '%s'", from.c_str(), to.c_str());
			}
		}
	}

	if (failedFiles) {
		return new MoveResult{ false, "", failedFiles };
	}

	// After the whole move, verify that all the files arrived correctly.
	// If there's a single error, we do not delete the source data.
	bool ok = true;
	for (size_t i = 0; i < fileSuffixesToMove.size(); i++) {
		const auto &fileSuffix = fileSuffixesToMove[i];
		progressReporter.SetProgress(ms->T("Checking..."), (int)i, (int)fileSuffixesToMove.size());

		Path to = moveDest / fileSuffix.suffix;

		File::FileInfo info;
		if (!File::GetFileInfo(to, &info)) {
			ok = false;
			break;
		}

		if (fileSuffix.fileSize != info.size) {
			ERROR_LOG(SYSTEM, "Mismatched size in target file %s. Verification failed.", fileSuffix.suffix.c_str());
			ok = false;
			failedFiles++;
			break;
		}
	}

	if (!ok) {
		return new MoveResult{ false, "", failedFiles };
	}

	INFO_LOG(SYSTEM, "Verification complete");

	// Delete all the old, now hopefully empty, directories.
	// Hopefully DeleteDir actually fails if it contains a file...
	for (size_t i = 0; i < directorySuffixesToCreate.size(); i++) {
		const auto &dirSuffix = directorySuffixesToCreate[i];
		Path dir = moveSrc / dirSuffix;
		if (dryRun) {
			INFO_LOG(SYSTEM, "dry run: Would have deleted dir '%s'", dir.c_str());
		} else {
			INFO_LOG(SYSTEM, "Deleting dir '%s'", dir.c_str());
			progressReporter.SetProgress(dirSuffix, i, directorySuffixesToCreate.size());
			if (File::Exists(dir)) {
				File::DeleteDir(dir);
			}
		}
	}
	return new MoveResult{ true, "", 0 };
}

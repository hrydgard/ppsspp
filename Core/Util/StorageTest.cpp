// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <vector>

#include "Common/Data/Text/Parsers.h"
#include "Common/File/AndroidStorage.h"
#include "Common/File/DirListing.h"
#include "Common/File/DiskFree.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/Util/MemStick.h"
#include "Core/Util/StorageTest.h"

// A test returns an empty string if it passed, and what went wrong if it didn't.
#define CHECK(cond) do { if (!(cond)) { return StringFromFormat("line %d: %s", __LINE__, #cond); } } while (false)

static std::string ReadAll(const Path &path) {
	std::string str;
	if (!File::ReadBinaryFileToString(path, &str)) {
		return "<unreadable>";
	}
	return str;
}

static bool Write(const Path &path, std::string_view str) {
	return File::WriteStringToFile(false, str, path);
}

static bool HasEntry(const std::vector<File::FileInfo> &files, std::string_view name, bool isDirectory) {
	return std::any_of(files.begin(), files.end(), [&](const File::FileInfo &f) {
		return f.name == name && f.isDirectory == isDirectory;
	});
}

int RunStorageTests(const Path &root, const std::function<void(const StorageTestResult &)> &report) {
	const Path scratch = root / "ppsspp_storage_test";
	// On content URIs DeleteDir takes everything in the folder along, on native paths it only removes empty
	// ones. The memory stick move depends on knowing which.
	const bool contentURI = root.Type() == PathType::CONTENT_URI;
	int failures = 0;
	std::string note;

	auto run = [&](const char *name, const std::function<std::string()> &test) {
		note.clear();
		std::string error = test();
		StorageTestResult result{ name, error.empty(), error.empty() ? note : error };
		if (!result.passed) {
			failures++;
		}
		INFO_LOG(Log::System, "StorageTest: %s %s%s%s", result.passed ? "PASS" : "FAIL", name, result.detail.empty() ? "" : ": ", result.detail.c_str());
		report(result);
	};

	run("CreateDir", [&]() -> std::string {
		if (File::Exists(scratch)) {
			// Left over from a run that didn't finish.
			CHECK(File::DeleteDirRecursively(scratch));
		}
		CHECK(!File::Exists(scratch));
		CHECK(File::CreateDir(scratch));
		CHECK(File::Exists(scratch));
		CHECK(File::IsDirectory(scratch));
		return "";
	});
	if (failures) {
		// Nowhere to run the rest.
		return failures;
	}

	run("WriteRead", [&]() -> std::string {
		const Path file = scratch / "a.txt";
		CHECK(Write(file, "hello"));
		CHECK(File::Exists(file));
		CHECK(!File::IsDirectory(file));
		CHECK(ReadAll(file) == "hello");
		File::FileInfo info;
		CHECK(File::GetFileInfo(file, &info));
		CHECK(info.exists && !info.isDirectory && info.size == 5);
		return "";
	});

	run("Overwrite", [&]() -> std::string {
		// Writing a shorter file over a longer one has to truncate it.
		const Path file = scratch / "b.txt";
		CHECK(Write(file, "a longer string"));
		CHECK(Write(file, "short"));
		CHECK(ReadAll(file) == "short");
		return "";
	});

	run("ListDir", [&]() -> std::string {
		CHECK(File::CreateDir(scratch / "sub"));
		std::vector<File::FileInfo> files;
		CHECK(File::GetFilesInDir(scratch, &files));
		CHECK(files.size() == 3);
		CHECK(HasEntry(files, "a.txt", false));
		CHECK(HasEntry(files, "b.txt", false));
		CHECK(HasEntry(files, "sub", true));
		return "";
	});

	run("CreateFullPath", [&]() -> std::string {
		CHECK(File::CreateFullPath(scratch / "x/y/z"));
		CHECK(File::IsDirectory(scratch / "x/y/z"));
		// Already there is fine.
		CHECK(File::CreateFullPath(scratch / "x/y"));
		return "";
	});

	run("Rename", [&]() -> std::string {
		CHECK(File::Rename(scratch / "a.txt", scratch / "renamed.txt"));
		CHECK(!File::Exists(scratch / "a.txt"));
		CHECK(ReadAll(scratch / "renamed.txt") == "hello");
		return "";
	});

	run("MoveIfFast", [&]() -> std::string {
		CHECK(File::MoveIfFast(scratch / "renamed.txt", scratch / "sub/renamed.txt"));
		CHECK(!File::Exists(scratch / "renamed.txt"));
		CHECK(ReadAll(scratch / "sub/renamed.txt") == "hello");
		// Folders too, which is what the memory stick move does.
		CHECK(File::MoveIfFast(scratch / "x", scratch / "sub/x"));
		CHECK(File::IsDirectory(scratch / "sub/x/y/z"));
		return "";
	});

	run("Copy", [&]() -> std::string {
		CHECK(File::Copy(scratch / "b.txt", scratch / "sub/b.txt"));
		CHECK(ReadAll(scratch / "sub/b.txt") == "short");
		CHECK(ReadAll(scratch / "b.txt") == "short");
		return "";
	});

	run("Delete", [&]() -> std::string {
		CHECK(File::Delete(scratch / "b.txt"));
		CHECK(!File::Exists(scratch / "b.txt"));
		return "";
	});

	run("DeleteDir", [&]() -> std::string {
		const Path dir = scratch / "full";
		CHECK(File::CreateDir(dir));
		CHECK(Write(dir / "f.txt", "f"));
		const bool deleted = File::DeleteDir(dir);
		if (contentURI) {
			CHECK(deleted && !File::Exists(dir));
			note = "takes the contents along";
		} else {
			CHECK(!deleted && File::Exists(dir / "f.txt"));
			note = "only empty folders";
		}
		CHECK(File::DeleteDirRecursively(dir) || !File::Exists(dir));
		CHECK(!File::Exists(dir));
		return "";
	});

	run("NoDuplicates", [&]() -> std::string {
		// Android's storage provider makes "name (1)" rather than fail, when asked to create something
		// whose name is taken. Nothing may ever do that to the user's files.
		const Path dir = scratch / "dup";
		CHECK(File::CreateFullPath(dir / "sub"));
		CHECK(Write(dir / "a.txt", "a"));
		CHECK(Write(dir / "b.txt", "b"));
		CHECK(Write(dir / "sub/a.txt", "old"));
		auto duplicates = [](const Path &folder) {
			std::vector<File::FileInfo> files;
			File::GetFilesInDir(folder, &files);
			std::string found;
			for (const File::FileInfo &f : files) {
				if (f.name.find(" (") != std::string::npos) {
					found += f.name + " ";
				}
			}
			return found;
		};
		std::vector<std::string> outcomes;

		// The memory stick move merges into existing data with this, so it has to overwrite.
		const bool copied = File::Copy(dir / "a.txt", dir / "sub/a.txt");
		CHECK(duplicates(dir / "sub").empty());
		CHECK(copied && ReadAll(dir / "sub/a.txt") == "a");

		// Rename and move onto a taken name differ between platforms (POSIX overwrites, Windows refuses), so
		// only duplicates are wrong. Note what happened.
		const bool renamed = File::Rename(dir / "b.txt", dir / "a.txt");
		CHECK(duplicates(dir).empty());
		outcomes.push_back(std::string("rename ") + (renamed ? (ReadAll(dir / "a.txt") == "b" ? "overwrote" : "?") : "refused"));
		if (!renamed) {
			CHECK(ReadAll(dir / "a.txt") == "a" && ReadAll(dir / "b.txt") == "b");
		}

		CHECK(Write(dir / "c.txt", "c"));
		const bool moved = File::MoveIfFast(dir / "c.txt", dir / "sub/a.txt");
		CHECK(duplicates(dir / "sub").empty());
		outcomes.push_back(std::string("move ") + (moved ? (ReadAll(dir / "sub/a.txt") == "c" ? "overwrote" : "?") : "refused"));

		// Creating what's there already, below the File functions (which check first).
		if (contentURI) {
			const StorageError file = Android_CreateFile(dir.ToString(), "sub");
			const StorageError folder = Android_CreateDirectory(dir.ToString(), "sub");
			CHECK(duplicates(dir).empty());
			outcomes.push_back(StringFromFormat("create file %s, folder %s", Android_ErrorToString(file), Android_ErrorToString(folder)));
		}
		note = "";
		for (const std::string &o : outcomes) {
			note += (note.empty() ? "" : ", ") + o;
		}
		CHECK(File::DeleteDirRecursively(dir));
		return "";
	});

	run("Names", [&]() -> std::string {
		// Characters that need escaping in a content URI, and outside ASCII.
		const char *name = "100% #1 & \xC3\xA4.txt";
		CHECK(Write(scratch / name, "odd"));
		CHECK(ReadAll(scratch / name) == "odd");
		std::vector<File::FileInfo> files;
		CHECK(File::GetFilesInDir(scratch, &files));
		CHECK(HasEntry(files, name, false));
		return "";
	});

	run("FreeSpace", [&]() -> std::string {
		int64_t space = 0;
		CHECK(free_disk_space(root, space));
		CHECK(space > 0);
		note = NiceSizeFormat(space);
		return "";
	});

	// The memory stick move, the way the memory stick screen does it.
	auto makeMemstick = [&](const Path &ms) {
		return File::CreateFullPath(ms / "PSP/SAVEDATA/GAME1") && File::CreateFullPath(ms / "PSP/SYSTEM") &&
			Write(ms / "PSP/SAVEDATA/GAME1/DATA.BIN", "save") && Write(ms / "PSP/SYSTEM/ppsspp.ini", "ini");
	};

	run("MoveRename", [&]() -> std::string {
		const Path src = scratch / "move1/old";
		const Path dest = scratch / "move1/new";
		CHECK(makeMemstick(src));
		CHECK(File::CreateFullPath(dest));
		bool committed = false;
		MoveProgressReporter progress;
		MoveResult *result = MoveDirectoryContentsSafe(src, dest, progress, [&]() { committed = true; return true; });
		const bool success = result && result->success;
		delete result;
		CHECK(success);
		CHECK(committed);
		CHECK(ReadAll(dest / "PSP/SAVEDATA/GAME1/DATA.BIN") == "save");
		CHECK(ReadAll(dest / "PSP/SYSTEM/ppsspp.ini") == "ini");
		CHECK(!File::Exists(src / "PSP/SAVEDATA"));
		CHECK(!File::Exists(src / "PSP/SYSTEM"));
		return "";
	});

	run("MoveCopy", [&]() -> std::string {
		// A non-empty SAVEDATA already there can't be renamed over, which forces the copy.
		const Path src = scratch / "move2/old";
		const Path dest = scratch / "move2/new";
		CHECK(makeMemstick(src));
		CHECK(File::CreateFullPath(dest / "PSP/SAVEDATA/GAME2"));
		CHECK(Write(dest / "PSP/SAVEDATA/GAME2/DATA.BIN", "other"));
		bool originalsThereAtCommit = false;
		MoveProgressReporter progress;
		MoveResult *result = MoveDirectoryContentsSafe(src, dest, progress, [&]() {
			originalsThereAtCommit = File::Exists(src / "PSP/SAVEDATA/GAME1/DATA.BIN");
			return true;
		});
		const bool success = result && result->success;
		delete result;
		CHECK(success);
		CHECK(originalsThereAtCommit);
		CHECK(ReadAll(dest / "PSP/SAVEDATA/GAME1/DATA.BIN") == "save");
		CHECK(ReadAll(dest / "PSP/SAVEDATA/GAME2/DATA.BIN") == "other");
		CHECK(ReadAll(dest / "PSP/SYSTEM/ppsspp.ini") == "ini");
		CHECK(!File::Exists(src / "PSP/SAVEDATA/GAME1/DATA.BIN"));
		// The old folders go too.
		CHECK(!File::Exists(src / "PSP/SAVEDATA"));
		CHECK(!File::Exists(src / "PSP/SYSTEM"));
		return "";
	});

	run("MoveUndo", [&]() -> std::string {
		// Fails at the end, after part was renamed and part copied, so everything has to be put back.
		const Path src = scratch / "move3/old";
		const Path dest = scratch / "move3/new";
		CHECK(makeMemstick(src));
		CHECK(File::CreateFullPath(src / "PSP/AAA"));
		CHECK(Write(src / "PSP/AAA/A.BIN", "a"));
		CHECK(File::CreateFullPath(dest / "PSP/SAVEDATA/GAME2"));
		CHECK(Write(dest / "PSP/SAVEDATA/GAME2/DATA.BIN", "other"));
		MoveProgressReporter progress;
		MoveResult *result = MoveDirectoryContentsSafe(src, dest, progress, []() { return false; });
		const bool undone = result && !result->success && !result->partlyMoved;
		delete result;
		CHECK(undone);
		CHECK(ReadAll(src / "PSP/AAA/A.BIN") == "a");
		CHECK(ReadAll(src / "PSP/SAVEDATA/GAME1/DATA.BIN") == "save");
		CHECK(ReadAll(src / "PSP/SYSTEM/ppsspp.ini") == "ini");
		CHECK(!File::Exists(dest / "PSP/AAA"));
		CHECK(!File::Exists(dest / "PSP/SAVEDATA/GAME1"));
		CHECK(ReadAll(dest / "PSP/SAVEDATA/GAME2/DATA.BIN") == "other");
		return "";
	});

	run("FolderChecks", [&]() -> std::string {
		const Path ms = scratch / "check/ms";
		CHECK(makeMemstick(ms));
		CHECK(FolderSeemsToBeUsed(ms));
		CHECK(CheckNewMemstickFolder(ms, Path()) == MemstickFolderCheck::OK);
		CHECK(CheckNewMemstickFolder(ms / "PSP/SAVEDATA", ms) == MemstickFolderCheck::INSIDE_CURRENT);
		// A PSP folder under another name.
		CHECK(File::Rename(ms / "PSP", ms / "Renamed"));
		CHECK(CheckNewMemstickFolder(ms / "Renamed", Path()) == MemstickFolderCheck::LOOKS_LIKE_PSP_FOLDER);
		CHECK(CanSwitchMemstickFolderTo(ms));
		return "";
	});

	// Not a pass/fail test: the cost of the calls the emulator makes most, for comparing implementations.
	// On content URIs each is a call into Java and a binder transaction to the provider.
	run("Timing", [&]() -> std::string {
		const Path dir = scratch / "timing";
		CHECK(File::CreateDir(dir));
		const int count = 20;
		std::string report;
		auto time = [&](const char *what, int n, const std::function<bool(int)> &op) -> bool {
			const Instant start = Instant::Now();
			for (int i = 0; i < n; i++) {
				if (!op(i)) {
					return false;
				}
			}
			report += StringFromFormat("%s%s %.2f", report.empty() ? "" : ", ", what, start.ElapsedMs() / n);
			return true;
		};
		auto name = [&](int i) { return dir / StringFromFormat("file%02d.bin", i); };
		CHECK(time("create+write", count, [&](int i) { return Write(name(i), "0123456789"); }));
		CHECK(time("overwrite", count, [&](int i) { return Write(name(i), "9876543210"); }));
		CHECK(time("read", count, [&](int i) { return ReadAll(name(i)) == "9876543210"; }));
		CHECK(time("exists", count, [&](int i) { return File::Exists(name(i)); }));
		CHECK(time("missing", count, [&](int i) { return !File::Exists(dir / StringFromFormat("none%02d", i)); }));
		CHECK(time("info", count, [&](int i) { File::FileInfo info; return File::GetFileInfo(name(i), &info) && info.size == 10; }));
		CHECK(time("list", 5, [&](int) { std::vector<File::FileInfo> files; return File::GetFilesInDir(dir, &files) && files.size() == count; }));
		CHECK(time("delete", count, [&](int i) { return File::Delete(name(i)); }));
		note = "ms per call: " + report;
		return "";
	});

	run("Cleanup", [&]() -> std::string {
		CHECK(File::DeleteDirRecursively(scratch));
		CHECK(!File::Exists(scratch));
		return "";
	});

	INFO_LOG(Log::System, "StorageTest: DONE, %d failed", failures);
	return failures;
}

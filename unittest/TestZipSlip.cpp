#include "ext/libzip/zip.h"

#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Core/Loaders.h"
#include "Core/Util/GameManager.h"
#include "Core/Util/PathUtil.h"

#include "UnitTest.h"

static bool TestHasParentDirComponent() {
	EXPECT_TRUE(HasParentDirComponent("../../evil.txt"));
	EXPECT_TRUE(HasParentDirComponent("game/../../evil.txt"));
	EXPECT_TRUE(HasParentDirComponent(".."));
	EXPECT_TRUE(HasParentDirComponent("sub/.."));
	EXPECT_TRUE(HasParentDirComponent("..\\evil.txt"));
	EXPECT_TRUE(HasParentDirComponent("a/b/../.."));
	EXPECT_FALSE(HasParentDirComponent("normal.txt"));
	EXPECT_FALSE(HasParentDirComponent("game/evil.txt"));
	EXPECT_FALSE(HasParentDirComponent("a.b/c.d"));
	EXPECT_FALSE(HasParentDirComponent(""));
	EXPECT_FALSE(HasParentDirComponent("/absolute/path.txt"));
	return true;
}

// Creates a zip archive at the given path with one entry of the given name.
static bool CreateZipWithEntry(const Path &zipPath, const std::string &entryName, const std::string &contents) {
	int errorp = 0;
	zip_t *z = zip_open(zipPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorp);
	if (!z)
		return false;
	zip_source_t *source = zip_source_buffer(z, contents.data(), contents.size(), 0);
	if (!source) {
		zip_close(z);
		return false;
	}
	if (zip_file_add(z, entryName.c_str(), source, ZIP_FL_ENC_UTF_8) < 0) {
		zip_source_free(source);
		zip_close(z);
		return false;
	}
	return zip_close(z) == 0;
}

// Crafts a zip with a parent-directory entry and verifies ExtractZipContents
// refuses to write outside the destination directory (Zip Slip).
static bool TestZipSlipExtraction() {
	Path tempRoot = Path("unittest_zip_slip_test");
	File::DeleteDirRecursively(tempRoot);
	EXPECT_TRUE(File::CreateDir(tempRoot));

	Path destDir = tempRoot / "dest";
	EXPECT_TRUE(File::CreateDir(destDir));

	Path zipPath = tempRoot / "bad.zip";
	EXPECT_TRUE(CreateZipWithEntry(zipPath, "../evil.txt", "should not escape"));

	int errorp = 0;
	zip_t *z = zip_open(zipPath.c_str(), 0, &errorp);
	EXPECT_TRUE(z != nullptr);

	ZipFileInfo info;
	info.numFiles = 1;
	info.stripChars = 0;
	info.ignoreMetaFiles = false;

	GameManager manager;
	EXPECT_TRUE(manager.ExtractZipContents(z, destDir, info, true));
	zip_close(z);

	// The malicious file must not have been written outside destDir.
	// A naive "dest / ../evil.txt" would land here.
	EXPECT_FALSE(File::Exists(tempRoot / "evil.txt"));
	// And the actual file should not exist inside destDir either.
	EXPECT_FALSE(File::Exists(destDir / "evil.txt"));

	// Clean up.
	File::Delete(zipPath);
	File::DeleteDirRecursively(tempRoot);
	return true;
}

// Save state zips are detected with stripChars past the last slash, since the whole
// directory is stripped. The files must land directly in the destination.
static bool TestSaveStateZipExtraction() {
	// Absolute, since CreateFullPath does nothing with relative paths.
	Path tempRoot = File::GetCurDirectory() / "unittest_zip_savestate_test";
	File::DeleteDirRecursively(tempRoot);
	EXPECT_TRUE(File::CreateDir(tempRoot));

	Path destDir = tempRoot / "dest";
	EXPECT_TRUE(File::CreateDir(destDir));

	Path zipPath = tempRoot / "states.zip";
	int errorp = 0;
	zip_t *z = zip_open(zipPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorp);
	EXPECT_TRUE(z != nullptr);
	static const char *const contents = "data";
	EXPECT_TRUE(zip_dir_add(z, "SAVE/", ZIP_FL_ENC_UTF_8) >= 0);
	EXPECT_TRUE(zip_dir_add(z, "SAVE/PPSSPP_STATE/", ZIP_FL_ENC_UTF_8) >= 0);
	for (const char *name : { "SAVE/PPSSPP_STATE/UCES01401_1.00_1.jpg", "SAVE/PPSSPP_STATE/UCES01401_1.00_1.ppst" }) {
		zip_source_t *source = zip_source_buffer(z, contents, strlen(contents), 0);
		EXPECT_TRUE(source != nullptr);
		EXPECT_TRUE(zip_file_add(z, name, source, ZIP_FL_ENC_UTF_8) >= 0);
	}
	EXPECT_TRUE(zip_close(z) == 0);

	z = zip_open(zipPath.c_str(), 0, &errorp);
	EXPECT_TRUE(z != nullptr);

	ZipFileInfo info;
	DetectZipFileContents(z, &info);
	EXPECT_EQ_INT((int)info.contents, (int)ZipFileContents::SAVE_STATES);

	GameManager manager;
	EXPECT_TRUE(manager.ExtractZipContents(z, destDir, info, true));
	zip_close(z);

	EXPECT_TRUE(File::Exists(destDir / "UCES01401_1.00_1.jpg") && !File::IsDirectory(destDir / "UCES01401_1.00_1.jpg"));
	EXPECT_TRUE(File::Exists(destDir / "UCES01401_1.00_1.ppst") && !File::IsDirectory(destDir / "UCES01401_1.00_1.ppst"));

	File::DeleteDirRecursively(tempRoot);
	return true;
}

static bool TestIsSafePathComponent() {
	EXPECT_TRUE(IsSafePathComponent("ULUS10041"));
	EXPECT_TRUE(IsSafePathComponent("NPJH-50465"));
	EXPECT_TRUE(IsSafePathComponent("a.b"));
	EXPECT_TRUE(IsSafePathComponent("My Game"));
	EXPECT_FALSE(IsSafePathComponent(""));
	EXPECT_FALSE(IsSafePathComponent("."));
	EXPECT_FALSE(IsSafePathComponent(".."));
	EXPECT_FALSE(IsSafePathComponent("..."));
	EXPECT_FALSE(IsSafePathComponent(".. "));
	EXPECT_FALSE(IsSafePathComponent("../ULUS10041"));
	EXPECT_FALSE(IsSafePathComponent("..\\ULUS10041"));
	EXPECT_FALSE(IsSafePathComponent("C:"));
	EXPECT_FALSE(IsSafePathComponent("ULUS10041:stream"));
	EXPECT_FALSE(IsSafePathComponent(std::string_view("ULUS\0/..", 8)));
	EXPECT_FALSE(IsSafePathComponent("ULUS\n10041"));
	return true;
}

bool TestZipSlip() {
	if (!TestHasParentDirComponent())
		return false;
	if (!TestIsSafePathComponent())
		return false;
	if (!TestZipSlipExtraction())
		return false;
	if (!TestSaveStateZipExtraction())
		return false;
	return true;
}

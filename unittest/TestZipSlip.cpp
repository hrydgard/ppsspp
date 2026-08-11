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

bool TestZipSlip() {
	if (!TestHasParentDirComponent())
		return false;
	if (!TestZipSlipExtraction())
		return false;
	return true;
}

#include <thread>
#include <vector>

#include "Common/Log.h"
#include "Common/File/VFS/ZipFileReader.h"

#include "UnitTest.h"

static bool CheckContainsDir(const std::vector<File::FileInfo> &listing, const char *name) {
	for (auto &file : listing) {
		if (file.name == name && file.isDirectory) {
			return true;
		}
	}
	return false;
}

static bool CheckContainsFile(const std::vector<File::FileInfo> &listing, const char *name) {
	for (auto &file : listing) {
		if (file.name == name && !file.isDirectory) {
			return true;
		}
	}
	return false;
}

// ziptest.zip file structure:
//
// ziptest/
//   data/
//     a/
//       in_a.txt
//     b/
//       in_b.txt
//     argh.txt
//     big.txt
//   lang/
//     en_us.txt
//     sv_se.txt
//   langregion.txt
// in_root.txt

// TODO: Also test the filter.
bool TestZipFile() {
	// First, check things relative to root, with an empty internal path.
	Path zipPath = Path("../source_assets/ziptest.zip");
	if (!File::Exists(zipPath)) {
		zipPath = Path("source_assets/ziptest.zip");
	}

	ZipFileReader *dir = ZipFileReader::Create(zipPath, "", true);
	EXPECT_TRUE(dir != nullptr);

	std::vector<File::FileInfo> listing;
	EXPECT_TRUE(dir->GetFileListing("", &listing, nullptr));
	EXPECT_EQ_INT(listing.size(), 2);
	EXPECT_TRUE(CheckContainsDir(listing, "ziptest"));
	EXPECT_TRUE(CheckContainsFile(listing, "in_root.txt"));
	EXPECT_FALSE(dir->GetFileListing("ziptestwrong", &listing, nullptr));

	// Next, do a file listing in a directory, but keep the root.
	EXPECT_TRUE(dir->GetFileListing("ziptest", &listing, nullptr));
	EXPECT_EQ_INT(listing.size(), 3);
	EXPECT_TRUE(CheckContainsDir(listing, "data"));
	EXPECT_TRUE(CheckContainsDir(listing, "lang"));
	EXPECT_TRUE(CheckContainsFile(listing, "langregion.txt"));
	delete dir;

	// Next, we'll destroy the reader and create a new one based in a subdirectory.
	dir = ZipFileReader::Create(zipPath, "ziptest/data", true);
	EXPECT_TRUE(dir != nullptr);
	EXPECT_TRUE(dir->GetFileListing("", &listing, nullptr));
	EXPECT_EQ_INT(listing.size(), 4);
	EXPECT_TRUE(CheckContainsDir(listing, "a"));
	EXPECT_TRUE(CheckContainsDir(listing, "b"));
	EXPECT_TRUE(CheckContainsFile(listing, "argh.txt"));
	EXPECT_TRUE(CheckContainsFile(listing, "big.txt"));

	EXPECT_TRUE(dir->GetFileListing("a", &listing, nullptr));
	EXPECT_TRUE(CheckContainsFile(listing, "in_a.txt"));
	EXPECT_EQ_INT(listing.size(), 1);
	EXPECT_TRUE(dir->GetFileListing("b", &listing, nullptr));
	EXPECT_TRUE(CheckContainsFile(listing, "in_b.txt"));
	EXPECT_EQ_INT(listing.size(), 1);
	delete dir;

	return true;
}

bool TestVFS() {
	if (!TestZipFile())
		return false;
	return true;
}

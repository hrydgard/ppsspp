// Tests for the ZArchive (.zar) reader and the memory stick overlay built on it.
// The archives are made by a small writer here, which lays them out the way the reference
// implementation (https://github.com/Exzap/ZArchive) does.

#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "ext/zstd/lib/zstd.h"

#include "Common/File/FileUtil.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/StringUtils.h"
#include "Core/FileLoaders/ZArchiveFileLoader.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/FileSystems/ZArchiveFileSystem.h"
#include "Core/Loaders.h"

#include "UnitTest.h"

namespace {

class MemoryFileLoader : public FileLoader {
public:
	MemoryFileLoader(std::vector<u8> data, const Path &path) : data_(std::move(data)), path_(path) {}
	bool Exists() override { return true; }
	bool IsDirectory() override { return false; }
	s64 FileSize() override { return (s64)data_.size(); }
	Path GetPath() const override { return path_; }
	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override {
		if (absolutePos < 0 || absolutePos >= (s64)data_.size())
			return 0;
		bytes = std::min(bytes, (size_t)(data_.size() - absolutePos));
		memcpy(data, data_.data() + absolutePos, bytes);
		return bytes;
	}

private:
	std::vector<u8> data_;
	Path path_;
};

static void PutBE(std::vector<u8> &out, u64 value, int bytes) {
	for (int i = bytes - 1; i >= 0; i--)
		out.push_back((u8)(value >> (i * 8)));
}

static char Lower(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Builds a ZArchive from path -> contents. Directories are implied by the paths.
static std::vector<u8> BuildZArchive(const std::map<std::string, std::string> &files) {
	const size_t BLOCK = 65536;
	struct Node {
		std::string name;
		bool isFile = false;
		u64 offset = 0;
		u64 size = 0;
		std::vector<Node *> children;
		u32 firstChild = 0;
	};
	std::vector<std::unique_ptr<Node>> nodes;
	nodes.emplace_back(new Node());
	Node *root = nodes[0].get();

	std::string data;
	for (const auto &file : files) {
		Node *dir = root;
		std::vector<std::string> parts;
		SplitString(file.first, '/', parts);
		for (size_t i = 0; i < parts.size(); i++) {
			const bool last = i + 1 == parts.size();
			Node *found = nullptr;
			for (Node *child : dir->children) {
				if (equalsNoCase(child->name, parts[i]))
					found = child;
			}
			if (!found) {
				nodes.emplace_back(new Node());
				found = nodes.back().get();
				found->name = parts[i];
				found->isFile = last;
				dir->children.push_back(found);
			}
			dir = found;
		}
		dir->offset = data.size();
		dir->size = file.second.size();
		data += file.second;
	}
	// Pad to a whole block, like the reference writer.
	data.resize((data.size() + BLOCK - 1) / BLOCK * BLOCK);

	std::vector<u8> out;
	// Compressed data and offset records.
	std::vector<u16> blockSizes;
	std::vector<u64> recordOffsets;
	std::vector<u8> buffer(ZSTD_compressBound(BLOCK));
	for (size_t pos = 0; pos < data.size(); pos += BLOCK) {
		if ((pos / BLOCK) % 16 == 0)
			recordOffsets.push_back(out.size());
		size_t size = ZSTD_compress(buffer.data(), buffer.size(), data.data() + pos, BLOCK, 6);
		if (ZSTD_isError(size) || size >= BLOCK) {
			out.insert(out.end(), data.begin() + pos, data.begin() + pos + BLOCK);
			size = BLOCK;
		} else {
			out.insert(out.end(), buffer.begin(), buffer.begin() + size);
		}
		blockSizes.push_back((u16)(size - 1));
	}
	const u64 compressedSize = out.size();
	while (out.size() % 8)
		out.push_back(0);

	const u64 recordsOffset = out.size();
	for (size_t r = 0; r < recordOffsets.size(); r++) {
		PutBE(out, recordOffsets[r], 8);
		for (size_t i = 0; i < 16; i++) {
			const size_t b = r * 16 + i;
			PutBE(out, b < blockSizes.size() ? blockSizes[b] : 0, 2);
		}
	}
	const u64 recordsSize = out.size() - recordsOffset;

	// Breadth first, children sorted case-insensitively.
	std::vector<Node *> order;
	std::queue<Node *> queue;
	queue.push(root);
	u32 next = 1;
	while (!queue.empty()) {
		Node *node = queue.front();
		queue.pop();
		order.push_back(node);
		std::sort(node->children.begin(), node->children.end(), [](Node *a, Node *b) {
			std::string la = a->name, lb = b->name;
			std::transform(la.begin(), la.end(), la.begin(), Lower);
			std::transform(lb.begin(), lb.end(), lb.begin(), Lower);
			return la < lb;
		});
		node->firstChild = next;
		next += (u32)node->children.size();
		for (Node *child : node->children)
			queue.push(child);
	}

	const u64 namesOffset = out.size();
	std::map<Node *, u32> nameOffsets;
	for (Node *node : order) {
		if (node == root)
			continue;
		nameOffsets[node] = (u32)(out.size() - namesOffset);
		if (node->name.size() >= 0x80) {
			out.push_back((u8)((node->name.size() & 0x7F) | 0x80));
			out.push_back((u8)(node->name.size() >> 7));
		} else {
			out.push_back((u8)node->name.size());
		}
		out.insert(out.end(), node->name.begin(), node->name.end());
	}
	const u64 namesSize = out.size() - namesOffset;

	const u64 treeOffset = out.size();
	for (Node *node : order) {
		const u32 nameOffset = node == root ? 0x7FFFFFFF : nameOffsets[node];
		PutBE(out, (node->isFile ? 0x80000000 : 0) | nameOffset, 4);
		if (node->isFile) {
			PutBE(out, (u32)node->offset, 4);
			PutBE(out, (u32)node->size, 4);
			PutBE(out, (u32)(((node->size >> 32) << 16) | (node->offset >> 32)), 4);
		} else {
			PutBE(out, node->firstChild, 4);
			PutBE(out, (u32)node->children.size(), 4);
			PutBE(out, 0, 4);
		}
	}
	const u64 treeSize = out.size() - treeOffset;
	const u64 metaOffset = out.size();

	const u64 sections[6][2] = {
		{ 0, compressedSize }, { recordsOffset, recordsSize }, { namesOffset, namesSize },
		{ treeOffset, treeSize }, { metaOffset, 0 }, { metaOffset, 0 },
	};
	for (const auto &section : sections) {
		PutBE(out, section[0], 8);
		PutBE(out, section[1], 8);
	}
	out.insert(out.end(), 32, 0);  // The hash, which we don't check.
	PutBE(out, out.size() + 16, 8);
	PutBE(out, 0x61bf3a01, 4);
	PutBE(out, 0x169f52d6, 4);
	return out;
}

static std::string Pattern(size_t size, u32 seed) {
	std::string s(size, '\0');
	for (size_t i = 0; i < size; i++)
		s[i] = (char)((i * 7 + seed + (i >> 9)) & 0xFF);
	return s;
}

// Doesn't compress, to get a block stored as is.
static std::string Noise(size_t size, u32 seed) {
	std::string s(size, '\0');
	u32 x = seed * 2654435761u + 1;
	for (size_t i = 0; i < size; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		s[i] = (char)x;
	}
	return s;
}

// Just the PBP header, with the PSAR (the last section) starting with the given magic.
static std::string FakePBP(const char *psarMagic) {
	std::string pbp("\0PBP", 4);
	pbp += std::string("\0\0\1\0", 4);  // Version.
	const u32 offset = 0x28;
	for (int i = 0; i < 8; i++) {
		pbp += std::string((const char *)&offset, 4);  // All sections empty, except the PSAR. Little endian.
	}
	pbp += psarMagic;
	return pbp;
}

static std::shared_ptr<ZArchive> OpenArchive(const std::vector<u8> &data) {
	FileLoader *loader = new MemoryFileLoader(data, Path("/test/game.zar"));
	std::string error;
	std::shared_ptr<ZArchive> archive = ZArchive::Open(loader, &error);
	if (!archive) {
		printf("ZArchive::Open failed: %s\n", error.c_str());
		delete loader;
	}
	return archive;
}

static std::string ReadAll(ZArchive &archive, const std::string &path) {
	ZArchive::NodeHandle node = archive.LookUp(path);
	std::string data((size_t)archive.GetFileSize(node), '\0');
	data.resize(archive.ReadFile(node, 0, data.size(), &data[0]));
	return data;
}

}  // namespace

static bool TestZArchiveReader() {
	const std::string longName = std::string(200, 'x') + ".bin";
	const std::string big = Pattern(300000, 1);
	const std::string noise = Noise(100000, 2);
	const std::map<std::string, std::string> files = {
		{ "Disc 10.iso", "ten" },
		{ "Disc 2.iso", "two" },
		{ "Disc 1.cso", "one" },
		{ "Disc 3.pbp", FakePBP("NPUMDIMG") },
		{ "Homebrew.PBP", FakePBP("PSISOIMG") },
		{ "._Disc 4.iso", "junk" },
		{ "readme.txt", "hello" },
		{ "PSP/GAME/ULUS12345/DLC.EDAT", big },
		{ "PSP/GAME/ULUS12345/noise.bin", noise },
		{ "PSP/GAME/ULUS12345/" + longName, "long" },
		{ "PSP/GAME/ULUS12345/empty.bin", "" },
	};
	std::vector<u8> data = BuildZArchive(files);

	std::unique_ptr<FileLoader> plain(new MemoryFileLoader(data, Path("/test/game.zar")));
	EXPECT_TRUE(ZArchive::HasFooterMagic(plain.get()));
	std::string error;
	EXPECT_TRUE(Identify_File(plain.get(), &error) == IdentifiedFileType::ZARCHIVE);

	std::shared_ptr<ZArchive> archive = OpenArchive(data);
	EXPECT_TRUE(archive != nullptr);

	// Lookups are case-insensitive and take either separator.
	EXPECT_TRUE(archive->IsDirectory(archive->LookUp("")));
	EXPECT_TRUE(archive->IsDirectory(archive->LookUp("/psp/Game/")));
	EXPECT_TRUE(archive->IsFile(archive->LookUp("PSP\\GAME\\ulus12345\\dlc.edat")));
	EXPECT_TRUE(archive->IsFile(archive->LookUp("./PSP//GAME/ULUS12345/./DLC.EDAT")));
	EXPECT_TRUE(archive->LookUp("PSP/GAME/ULUS12345/DLC.EDAT/more") == ZArchive::INVALID_NODE);
	EXPECT_TRUE(archive->LookUp("PSP/GAMES") == ZArchive::INVALID_NODE);
	// Names of 128 characters or more have a two byte length.
	EXPECT_TRUE(archive->IsFile(archive->LookUp("PSP/GAME/ULUS12345/" + longName)));
	EXPECT_EQ_STR(ReadAll(*archive, "PSP/GAME/ULUS12345/" + longName), std::string("long"));

	// Whole files, spanning blocks, including stored ones.
	EXPECT_TRUE(ReadAll(*archive, "PSP/GAME/ULUS12345/DLC.EDAT") == big);
	EXPECT_TRUE(ReadAll(*archive, "PSP/GAME/ULUS12345/noise.bin") == noise);
	EXPECT_EQ_INT((int)ReadAll(*archive, "PSP/GAME/ULUS12345/empty.bin").size(), 0);

	// Random access, across block boundaries, and past the end.
	ZArchive::NodeHandle node = archive->LookUp("PSP/GAME/ULUS12345/DLC.EDAT");
	std::vector<char> buf(200000);
	for (u64 offset : { (u64)0, (u64)65530, (u64)65536, (u64)131071, (u64)299990 }) {
		const size_t expected = std::min<size_t>(70000, big.size() - (size_t)offset);
		EXPECT_EQ_INT((int)archive->ReadFile(node, offset, 70000, buf.data()), (int)expected);
		EXPECT_TRUE(memcmp(buf.data(), big.data() + offset, expected) == 0);
	}
	EXPECT_EQ_INT((int)archive->ReadFile(node, big.size(), 10, buf.data()), 0);
	EXPECT_EQ_INT((int)archive->ReadFile(archive->LookUp("PSP"), 0, 10, buf.data()), 0);

	// What PPSSPP makes of it: discs in natural order, no junk, memstick content.
	ZArchiveContents contents;
	ScanZArchiveContents(*archive, &contents);
	// A PBP of a disc counts as one, another PBP doesn't.
	EXPECT_EQ_INT((int)contents.discs.size(), 4);
	EXPECT_EQ_STR(contents.discs[0], std::string("Disc 1.cso"));
	EXPECT_EQ_STR(contents.discs[1], std::string("Disc 2.iso"));
	EXPECT_EQ_STR(contents.discs[2], std::string("Disc 3.pbp"));
	EXPECT_EQ_STR(contents.discs[3], std::string("Disc 10.iso"));
	EXPECT_TRUE(contents.hasMemStick);
	EXPECT_EQ_STR(contents.BootTarget(1), std::string("Disc 2.iso"));
	EXPECT_EQ_STR(contents.BootTarget(7), std::string("Disc 1.cso"));

	ZArchiveFileLoader disc(archive, contents.discs[3]);
	EXPECT_TRUE(disc.Exists());
	EXPECT_EQ_INT((int)disc.FileSize(), 3);
	EXPECT_EQ_STR(disc.GetFileExtension(), std::string(".iso"));
	EXPECT_EQ_STR(disc.GetPath().ToString(), std::string("/test/game.zar"));
	char three[4]{};
	EXPECT_EQ_INT((int)disc.ReadAt(0, 4, three), 3);
	EXPECT_EQ_STR(std::string(three), std::string("ten"));
	EXPECT_FALSE(ZArchiveFileLoader(archive, "PSP/GAME").Exists());
	return true;
}

static bool TestZArchiveEbootOnly() {
	std::shared_ptr<ZArchive> archive = OpenArchive(BuildZArchive({
		{ "PSP/GAME/NPUZ00001/EBOOT.PBP", "pbp" },
		{ "PSP/GAME/NPUZ00001/DLC/A.EDAT", "dlc" },
		{ "PSP/SAVEDATA/NPUZ00001DATA/DATA.BIN", "save" },
	}));
	EXPECT_TRUE(archive != nullptr);
	ZArchiveContents contents;
	ScanZArchiveContents(*archive, &contents);
	EXPECT_EQ_INT((int)contents.discs.size(), 0);
	EXPECT_EQ_STR(contents.ebootPath, std::string("PSP/GAME/NPUZ00001/EBOOT.PBP"));
	EXPECT_EQ_STR(contents.BootTarget(0), std::string("PSP/GAME/NPUZ00001/EBOOT.PBP"));

	// Nothing to boot.
	std::vector<u8> data = BuildZArchive({ { "readme.txt", "nothing here" } });
	MemoryFileLoader *loader = new MemoryFileLoader(data, Path("/test/empty.zar"));
	std::string error;
	EXPECT_TRUE(OpenZArchiveBootTarget(loader, 0, &error) == nullptr);
	EXPECT_FALSE(error.empty());
	// Still ours.
	delete loader;
	return true;
}

static bool TestZArchiveCorrupt() {
	const std::vector<u8> good = BuildZArchive({ { "a/b.iso", Pattern(100000, 3) } });
	EXPECT_TRUE(OpenArchive(good) != nullptr);

	auto rejects = [](std::vector<u8> data) {
		MemoryFileLoader *loader = new MemoryFileLoader(std::move(data), Path("/test/bad.zar"));
		std::string error;
		std::shared_ptr<ZArchive> archive = ZArchive::Open(loader, &error);
		if (archive)
			return false;
		delete loader;
		return !error.empty();
	};

	// Truncated, like a partial download.
	std::vector<u8> data(good.begin() + 1000, good.end());
	EXPECT_TRUE(rejects(data));
	// Bad magic.
	data = good;
	data.back() ^= 1;
	EXPECT_TRUE(rejects(data));
	// A section pointing past the end.
	data = good;
	const size_t footer = data.size() - 144;
	data[footer + 16 * 3] = 0x7F;  // Tree offset.
	EXPECT_TRUE(rejects(data));
	// A directory claiming children beyond the tree.
	data = good;
	const u64 treeOffset = ((u64)data[footer + 48] << 56) | ((u64)data[footer + 49] << 48) | ((u64)data[footer + 50] << 40) | ((u64)data[footer + 51] << 32) |
		((u64)data[footer + 52] << 24) | ((u64)data[footer + 53] << 16) | ((u64)data[footer + 54] << 8) | data[footer + 55];
	data[(size_t)treeOffset + 8 + 3] = 50;  // Root child count.
	EXPECT_TRUE(rejects(data));
	// The root pointing at itself.
	data = good;
	data[(size_t)treeOffset + 4 + 3] = 0;
	EXPECT_TRUE(rejects(data));
	// Damaged compressed data fails the read, rather than returning garbage.
	data = good;
	data[0] ^= 0xFF;  // The first block's zstd frame magic.
	std::shared_ptr<ZArchive> archive = OpenArchive(data);
	EXPECT_TRUE(archive != nullptr);
	std::vector<u8> buf(100000);
	EXPECT_TRUE(archive->ReadFile(archive->LookUp("a/b.iso"), 0, buf.size(), buf.data()) < buf.size());
	return true;
}

struct StateHolder {
	IFileSystem *fs;
	void DoState(PointerWrap &p) {
		fs->DoState(p);
		// To check that the state ended where it should.
		u32 marker = 0x12345678;
		Do(p, marker);
		if (marker != 0x12345678)
			p.SetError(PointerWrap::ERROR_FAILURE);
	}
};

static std::string ReadFS(IFileSystem &fs, const std::string &path) {
	int fd = fs.OpenFile(path, FILEACCESS_READ);
	if (fd < 0)
		return "<error>";
	std::string result;
	char buf[1000];
	size_t bytes;
	while ((bytes = fs.ReadFile(fd, (u8 *)buf, sizeof(buf))) > 0)
		result.append(buf, bytes);
	fs.CloseFile(fd);
	return result;
}

static bool HasEntry(const std::vector<PSPFileInfo> &listing, const char *name) {
	return std::any_of(listing.begin(), listing.end(), [&](const PSPFileInfo &info) { return equalsNoCase(info.name, name); });
}

static bool TestZArchiveMemStickOverlay() {
	const Path root = UnitTestTempPath("unittest_zarchive_ms");
	File::DeleteDirRecursively(root);
	EXPECT_TRUE(File::CreateFullPath(root / "PSP/GAME/ULUS12345"));
	EXPECT_TRUE(File::CreateFullPath(root / "PSP/SAVEDATA"));
	EXPECT_TRUE(File::WriteStringToFile(true, "real", root / "PSP/GAME/ULUS12345/both.txt"));
	EXPECT_TRUE(File::WriteStringToFile(true, "mine", root / "PSP/GAME/ULUS12345/own.txt"));

	std::shared_ptr<ZArchive> archive = OpenArchive(BuildZArchive({
		{ "Game.iso", "disc" },
		{ "PSP/GAME/ULUS12345/both.txt", "archived" },
		{ "PSP/GAME/ULUS12345/dlc.edat", "dlc data" },
		{ "PSP/GAME/ULUS12345/sub/deep.bin", "deep" },
		{ "PSP/GAME/NPUZ00001/EBOOT.PBP", "other game" },
		{ "PSP/SAVEDATA/ULUS12345SAVE/DATA.BIN", "save" },
	}));
	EXPECT_TRUE(archive != nullptr);

	SequentialHandleAllocator handles;
	auto memstick = std::make_shared<DirectoryFileSystem>(&handles, root, FileSystemFlags::SIMULATE_FAT32 | FileSystemFlags::CARD);
	auto archiveFS = std::make_shared<ZArchiveFileSystem>(&handles, archive, FileSystemFlags::SIMULATE_FAT32 | FileSystemFlags::CARD);
	MemStickOverlayFileSystem fs(memstick, archiveFS);

	// Reads: the memory stick wins, the archive fills in.
	EXPECT_EQ_STR(ReadFS(fs, "/PSP/GAME/ULUS12345/both.txt"), std::string("real"));
	EXPECT_EQ_STR(ReadFS(fs, "/PSP/GAME/ULUS12345/DLC.EDAT"), std::string("dlc data"));
	EXPECT_EQ_STR(ReadFS(fs, "/PSP/GAME/ULUS12345/sub/deep.bin"), std::string("deep"));
	EXPECT_EQ_STR(ReadFS(fs, "/PSP/GAME/NPUZ00001/EBOOT.PBP"), std::string("other game"));
	EXPECT_TRUE(fs.GetFileInfo("/PSP/GAME/ULUS12345/dlc.edat").exists);
	EXPECT_EQ_INT((int)fs.GetFileInfo("/PSP/GAME/ULUS12345/dlc.edat").size, 8);
	EXPECT_TRUE(fs.GetFileInfo("/PSP/GAME/NPUZ00001").type == FILETYPE_DIRECTORY);
	// Only the PSP directory, and not its save data.
	EXPECT_FALSE(fs.GetFileInfo("/Game.iso").exists);
	EXPECT_FALSE(fs.GetFileInfo("/PSP/SAVEDATA/ULUS12345SAVE/DATA.BIN").exists);
	EXPECT_TRUE(fs.OpenFile("/PSP/SAVEDATA/ULUS12345SAVE/DATA.BIN", (FileAccess)(FILEACCESS_READ | FILEACCESS_PPSSPP_QUIET)) < 0);

	// Listings are merged.
	std::vector<PSPFileInfo> listing = fs.GetDirListing("/PSP/GAME/ULUS12345");
	EXPECT_TRUE(HasEntry(listing, "both.txt"));
	EXPECT_TRUE(HasEntry(listing, "own.txt"));
	EXPECT_TRUE(HasEntry(listing, "dlc.edat"));
	EXPECT_TRUE(HasEntry(listing, "sub"));
	EXPECT_EQ_INT((int)std::count_if(listing.begin(), listing.end(), [](const PSPFileInfo &i) { return equalsNoCase(i.name, "both.txt"); }), 1);
	bool exists = false;
	listing = fs.GetDirListing("/PSP/GAME/NPUZ00001", &exists);
	EXPECT_TRUE(exists);
	EXPECT_TRUE(HasEntry(listing, "EBOOT.PBP"));
	EXPECT_TRUE(HasEntry(listing, ".."));
	listing = fs.GetDirListing("/PSP/SAVEDATA");
	EXPECT_FALSE(HasEntry(listing, "ULUS12345SAVE"));
	listing = fs.GetDirListing("/");
	EXPECT_FALSE(HasEntry(listing, "Game.iso"));

	// A savestate with no archive files open is exactly the memory stick's.
	std::vector<u8> plainState, overlayState;
	StateHolder plainHolder{ memstick.get() };
	StateHolder overlayHolder{ &fs };
	EXPECT_TRUE(CChunkFileReader::MeasureAndSavePtr(plainHolder, &plainState) == CChunkFileReader::ERROR_NONE);
	EXPECT_TRUE(CChunkFileReader::MeasureAndSavePtr(overlayHolder, &overlayState) == CChunkFileReader::ERROR_NONE);
	EXPECT_TRUE(plainState == overlayState);
	// So an old state loads too.
	std::string stateError;
	EXPECT_TRUE(CChunkFileReader::LoadPtr(plainState.data(), plainState.size(), overlayHolder, &stateError) == CChunkFileReader::ERROR_NONE);

	// With one open, it's saved, and restored with its position.
	int fd = fs.OpenFile("/PSP/GAME/ULUS12345/dlc.edat", FILEACCESS_READ);
	EXPECT_TRUE(fd >= 0);
	EXPECT_TRUE(fs.OwnsHandle(fd));
	EXPECT_EQ_INT((int)fs.SeekFile(fd, 4, FILEMOVE_BEGIN), 4);
	std::vector<u8> state;
	EXPECT_TRUE(CChunkFileReader::MeasureAndSavePtr(overlayHolder, &state) == CChunkFileReader::ERROR_NONE);
	fs.CloseFile(fd);
	EXPECT_FALSE(fs.OwnsHandle(fd));
	EXPECT_TRUE(CChunkFileReader::LoadPtr(state.data(), state.size(), overlayHolder, &stateError) == CChunkFileReader::ERROR_NONE);
	EXPECT_TRUE(fs.OwnsHandle(fd));
	char rest[16]{};
	EXPECT_EQ_INT((int)fs.ReadFile(fd, (u8 *)rest, sizeof(rest)), 4);
	EXPECT_EQ_STR(std::string(rest), std::string("data"));
	fs.CloseFile(fd);
	// And loading a state without the section closes it again.
	fd = fs.OpenFile("/PSP/GAME/ULUS12345/dlc.edat", FILEACCESS_READ);
	EXPECT_TRUE(CChunkFileReader::LoadPtr(plainState.data(), plainState.size(), overlayHolder, &stateError) == CChunkFileReader::ERROR_NONE);
	EXPECT_FALSE(fs.OwnsHandle(fd));

	// Writes go to the memory stick. Modifying an archived file copies it first.
	fd = fs.OpenFile("/PSP/GAME/ULUS12345/dlc.edat", (FileAccess)(FILEACCESS_WRITE | FILEACCESS_APPEND));
	EXPECT_TRUE(fd >= 0);
	EXPECT_FALSE(archiveFS->OwnsHandle(fd));
	EXPECT_EQ_INT((int)fs.WriteFile(fd, (const u8 *)"!", 1), 1);
	fs.CloseFile(fd);
	std::string onDisk;
	EXPECT_TRUE(File::ReadTextFileToString(root / "PSP/GAME/ULUS12345/dlc.edat", &onDisk));
	EXPECT_EQ_STR(onDisk, std::string("dlc data!"));
	EXPECT_EQ_STR(ReadFS(fs, "/PSP/GAME/ULUS12345/dlc.edat"), std::string("dlc data!"));

	// A new file in a directory that's only in the archive creates it.
	fd = fs.OpenFile("/PSP/GAME/NPUZ00001/NEW.BIN", (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_TRUNCATE));
	EXPECT_TRUE(fd >= 0);
	fs.WriteFile(fd, (const u8 *)"new", 3);
	fs.CloseFile(fd);
	EXPECT_TRUE(File::Exists(root / "PSP/GAME/NPUZ00001/NEW.BIN"));
	EXPECT_FALSE(File::Exists(root / "PSP/GAME/NPUZ00001/EBOOT.PBP"));
	EXPECT_EQ_STR(ReadFS(fs, "/PSP/GAME/NPUZ00001/EBOOT.PBP"), std::string("other game"));

	// Exclusive create of something in the archive fails, it exists.
	EXPECT_TRUE(fs.OpenFile("/PSP/GAME/ULUS12345/sub/deep.bin", (FileAccess)(FILEACCESS_WRITE | FILEACCESS_CREATE | FILEACCESS_EXCL)) < 0);
	// Archived things can't be removed, and directories in it already exist.
	EXPECT_FALSE(fs.RemoveFile("/PSP/GAME/ULUS12345/sub/deep.bin"));
	EXPECT_FALSE(fs.MkDir("/PSP/GAME/ULUS12345/sub"));
	EXPECT_TRUE(fs.MkDir("/PSP/GAME/ULUS12345/sub/newdir"));
	EXPECT_TRUE(File::IsDirectory(root / "PSP/GAME/ULUS12345/sub/newdir"));
	// But the memory stick's own files can.
	EXPECT_TRUE(fs.RemoveFile("/PSP/GAME/ULUS12345/own.txt"));
	// Renaming an archived file copies it out.
	EXPECT_EQ_INT(fs.RenameFile("/PSP/GAME/ULUS12345/sub/deep.bin", "/PSP/GAME/ULUS12345/sub/moved.bin"), 0);
	EXPECT_EQ_STR(ReadFS(fs, "/PSP/GAME/ULUS12345/sub/moved.bin"), std::string("deep"));
	// The archive's copy is still there, of course.
	EXPECT_TRUE(fs.GetFileInfo("/PSP/GAME/ULUS12345/sub/deep.bin").exists);

	File::DeleteDirRecursively(root);
	return true;
}

bool TestZArchive() {
	RET(TestZArchiveReader());
	RET(TestZArchiveEbootOnly());
	RET(TestZArchiveCorrupt());
	RET(TestZArchiveMemStickOverlay());
	return true;
}

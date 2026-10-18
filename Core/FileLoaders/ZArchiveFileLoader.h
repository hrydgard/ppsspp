// Copyright (c) 2026- PPSSPP Project.

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

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Core/Loaders.h"

// ZArchive (.zar) is a read-only archive format with random access, made by Exzap for Cemu:
// https://github.com/Exzap/ZArchive (MIT-0). The data of all files is concatenated and cut into
// 64KB blocks that are compressed separately with zstd, so any byte can be reached by decompressing
// a single block. That makes it a good container for a game: the disc image(s) and the memstick
// content that goes with them (DLC, updates) in one file, compressed, with nothing to extract.
//
// This is a reader written from the format description, rather than the reference library, so that
// it reads through a FileLoader (which gets us Android content URIs, HTTP and the rest for free) and
// so that it treats the archive as the untrusted input it is.
//
// What PPSSPP looks for in a .zar, see ScanZArchiveContents():
//   /<name>.iso, .cso or .chd   Disc images, in the archive root. More than one is a multi-disc game,
//                               the discs are ordered by name ("Disc 1", "Disc 2", ... "Disc 10").
//   /<name>.pbp                 Also a disc image, if it's a PBP of a disc (PSN games, ISOs converted
//                               to PBP). Other PBPs (homebrew) are ignored there.
//   /PSP/...                    Memory stick content, overlaid read-only on ms0: while the game runs:
//                               PSP/GAME/<ID>/ for DLC and game updates (PBOOT.PBP). PSP/SAVEDATA is
//                               ignored, save data belongs on the real memory stick.
// Without a disc image, the first PSP/GAME/<dir>/EBOOT.PBP is booted instead (PSN games, homebrew).

class ZArchive {
public:
	typedef u32 NodeHandle;
	static constexpr NodeHandle INVALID_NODE = 0xFFFFFFFF;
	static constexpr NodeHandle ROOT_NODE = 0;

	struct DirEntry {
		std::string_view name;  // Points into the archive's name table, valid as long as the archive.
		NodeHandle node;
		bool isDirectory;
		u64 size;  // 0 for directories.
	};

	~ZArchive();

	// Cheap check of the magic at the end of the file. Doesn't validate anything else.
	static bool HasFooterMagic(FileLoader *loader);

	// On success, the archive takes ownership of the loader. On failure (nullptr), it doesn't, and
	// errorString (optional) says what was wrong.
	static std::shared_ptr<ZArchive> Open(FileLoader *loader, std::string *errorString);

	// Paths are relative to the root, separated by / or \, and matched case-insensitively (ASCII),
	// the way both ZArchive and the PSP's FAT do it. An empty path is the root.
	NodeHandle LookUp(std::string_view path) const;
	bool IsDirectory(NodeHandle node) const;
	bool IsFile(NodeHandle node) const;
	u64 GetFileSize(NodeHandle node) const;
	std::vector<DirEntry> ListDirectory(NodeHandle dir) const;
	// The uncompressed size of everything in the archive.
	u64 TotalFileSize() const;

	// Thread safe. Returns the number of bytes read, which is less than requested only at the end of
	// the file, or if the archive is damaged.
	size_t ReadFile(NodeHandle node, u64 offset, size_t length, void *buffer);

	Path GetPath() const { return loader_->GetPath(); }
	FileLoader *GetLoader() const { return loader_.get(); }

	// Gives up ownership of the loader, leaving the archive unusable. Only for someone holding the
	// only reference to a freshly opened archive, to hand the loader back after all.
	FileLoader *ReleaseLoader();

private:
	struct OffsetRecord {
		u64 baseOffset;
		u16 size[16];  // Compressed size - 1, of each of the 16 blocks.
	};

	struct TreeEntry {
		u32 nameOffsetAndType;  // Top bit set: file. The rest is the offset into the name table.
		u32 a;  // File: low 32 bits of the offset. Directory: index of the first child.
		u32 b;  // File: low 32 bits of the size. Directory: number of children.
		u32 c;  // File: high 16 bits of the size in the top half, of the offset in the bottom half.

		bool IsFile() const { return (nameOffsetAndType & 0x80000000) != 0; }
		u32 NameOffset() const { return nameOffsetAndType & 0x7FFFFFFF; }
		u64 FileOffset() const { return (u64)a | ((u64)(c & 0xFFFF) << 32); }
		u64 FileSize() const { return (u64)b | ((u64)(c >> 16) << 32); }
		u32 FirstChild() const { return a; }
		u32 ChildCount() const { return b; }
	};

	struct CacheBlock {
		u64 index = (u64)-1;
		u64 lastUse = 0;
		std::unique_ptr<u8[]> data;
	};

	ZArchive() {}
	bool Load(std::string *errorString);
	std::string_view GetName(u32 nameOffset) const;
	const TreeEntry *Entry(NodeHandle node) const;
	bool ReadBlock(u64 blockIndex, u8 *dest);  // Call with lock_ held.
	const u8 *GetCachedBlock(u64 blockIndex);  // Call with lock_ held.

	std::unique_ptr<FileLoader> loader_;
	std::vector<OffsetRecord> offsetRecords_;
	std::vector<u8> nameTable_;
	std::vector<TreeEntry> tree_;
	u64 compressedDataOffset_ = 0;
	u64 compressedDataSize_ = 0;
	u64 blockCount_ = 0;

	std::mutex lock_;
	void *dctx_ = nullptr;  // ZSTD_DCtx
	std::vector<u8> compressedBuffer_;
	std::vector<CacheBlock> cache_;
	u64 useCounter_ = 0;
};

// What ScanZArchiveContents found in an archive, see the description at the top.
struct ZArchiveContents {
	std::vector<std::string> discs;  // Archive paths of the disc images, in disc order.
	std::string ebootPath;  // PSP/GAME/<dir>/EBOOT.PBP, the boot target when there's no disc image.
	bool hasMemStick = false;  // There's a PSP directory to overlay on ms0:.

	bool HasBootable() const { return !discs.empty() || !ebootPath.empty(); }
	// The disc index if bootIndex is in range, otherwise the default boot target. Empty if none.
	std::string BootTarget(int bootIndex = 0) const;
};

void ScanZArchiveContents(ZArchive &archive, ZArchiveContents *contents);

// Exposes one file from an archive as a file loader. GetPath() is the archive's path, like
// ZipFileLoader: it's the archive that the user picked, it's what goes in recents, and so on.
class ZArchiveFileLoader : public FileLoader {
public:
	ZArchiveFileLoader(std::shared_ptr<ZArchive> archive, std::string_view innerPath);

	bool IsRemote() override { return archive_->GetLoader()->IsRemote(); }
	bool Exists() override { return node_ != ZArchive::INVALID_NODE; }
	bool ExistsFast() override { return Exists(); }
	bool IsDirectory() override { return false; }
	s64 FileSize() override { return (s64)size_; }
	Path GetPath() const override { return archive_->GetPath(); }
	std::string GetFileExtension() const override { return extension_; }

	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override;
	size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override;

	void Cancel() override { archive_->GetLoader()->Cancel(); }
	std::string LatestError() const override { return archive_->GetLoader()->LatestError(); }

	const std::shared_ptr<ZArchive> &GetArchive() const { return archive_; }
	// The path of the file in the archive, like "Disc 2.iso" or "PSP/GAME/NPUH10000/EBOOT.PBP".
	const std::string &GetInnerPath() const { return innerPath_; }

private:
	std::shared_ptr<ZArchive> archive_;
	std::string innerPath_;
	std::string extension_;
	ZArchive::NodeHandle node_;
	u64 size_ = 0;
};

// Opens the archive in archiveLoader and returns a loader for its boot target: disc bootIndex (if
// in range, else the first), or the EBOOT.PBP. On success, the returned loader owns archiveLoader.
// On failure, returns nullptr and doesn't take ownership.
FileLoader *OpenZArchiveBootTarget(FileLoader *archiveLoader, int bootIndex, std::string *errorString);

// Finds the ZArchiveFileLoader in a chain of proxied loaders (like the RAM cache on top of it).
ZArchiveFileLoader *FindZArchiveFileLoader(FileLoader *loader);

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

#include <algorithm>
#include <cstring>

#include <zstd.h>

#include "Common/Data/Format/IniFile.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/FileLoaders/ZArchiveFileLoader.h"

// The format, all integers big endian (see zarchivecommon.h in the reference implementation):
//
// [compressed data]  64KB blocks, each zstd compressed, or stored as is if that didn't make it smaller.
// [offset records]   One per 16 blocks: u64 offset of the first block, u16 (size - 1) of each of the 16.
// [name table]       Names, each with a 1 byte length prefix. If its top bit is set, the length is
//                    15 bits, the low 7 in the first byte and the high 8 in the next.
// [file tree]        16 byte entries. Entry 0 is the root directory. The children of a directory are
//                    consecutive entries, sorted by name. The tree is stored breadth first.
// [meta data]        Unused, empty in version 1.
// [footer]           144 bytes: offset and size of each section above, a SHA-256, the total file size,
//                    the version (0x61bf3a01) and finally the magic (0x169f52d6).

static const u32 ZAR_MAGIC = 0x169f52d6;
static const u32 ZAR_VERSION1 = 0x61bf3a01;
static const u32 ZAR_BLOCK_SIZE = 64 * 1024;
static const u32 ZAR_BLOCKS_PER_RECORD = 16;
static const u32 ZAR_FOOTER_SIZE = 16 * 6 + 32 + 8 + 4 + 4;
static const u32 ZAR_RECORD_SIZE = 8 + 2 * ZAR_BLOCKS_PER_RECORD;
static const u32 ZAR_TREE_ENTRY_SIZE = 16;
static const u32 ZAR_NO_NAME = 0x7FFFFFFF;

// Sanity limits for the tables we load into memory. The real thing is far smaller: a 4GB archive
// needs 160KB of offset records, and the tree takes 16 bytes per file.
static const u64 ZAR_MAX_TABLE_SIZE = 256 * 1024 * 1024;

// The decompressed block cache, per archive. 4MB, like the reference implementation.
static const size_t ZAR_CACHE_BLOCKS = 64;

static inline u16 ReadBE16(const u8 *p) {
	return (u16)((p[0] << 8) | p[1]);
}

static inline u32 ReadBE32(const u8 *p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static inline u64 ReadBE64(const u8 *p) {
	return ((u64)ReadBE32(p) << 32) | ReadBE32(p + 4);
}

static inline char AsciiToLower(char c) {
	// Not tolower, to avoid locale surprises like the Turkish I.
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool NameEquals(std::string_view a, std::string_view b) {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (AsciiToLower(a[i]) != AsciiToLower(b[i]))
			return false;
	}
	return true;
}

static bool ReadFully(FileLoader *loader, u64 offset, size_t size, void *dest) {
	return loader->ReadAt((s64)offset, size, dest) == size;
}

ZArchive::~ZArchive() {
	if (dctx_) {
		ZSTD_freeDCtx((ZSTD_DCtx *)dctx_);
	}
}

bool ZArchive::HasFooterMagic(FileLoader *loader) {
	if (!loader || loader->IsDirectory())
		return false;
	const s64 size = loader->FileSize();
	if (size <= (s64)ZAR_FOOTER_SIZE)
		return false;
	u8 tail[8];
	if (!ReadFully(loader, size - 8, sizeof(tail), tail))
		return false;
	return ReadBE32(tail) == ZAR_VERSION1 && ReadBE32(tail + 4) == ZAR_MAGIC;
}

std::shared_ptr<ZArchive> ZArchive::Open(FileLoader *loader, std::string *errorString) {
	std::string dummy;
	if (!errorString)
		errorString = &dummy;
	if (!loader || !loader->Exists() || loader->IsDirectory()) {
		*errorString = "ZArchive: file not readable";
		return nullptr;
	}

	std::shared_ptr<ZArchive> archive(new ZArchive());
	archive->loader_.reset(loader);
	if (!archive->Load(errorString)) {
		// Hand the loader back, the caller still owns it.
		archive->loader_.release();
		ERROR_LOG(Log::Loader, "Not a valid ZArchive: %s: %s", loader->GetPath().c_str(), errorString->c_str());
		return nullptr;
	}
	return archive;
}

bool ZArchive::Load(std::string *errorString) {
	const s64 fileSize = loader_->FileSize();
	if (fileSize <= (s64)ZAR_FOOTER_SIZE) {
		*errorString = "too small";
		return false;
	}

	u8 footer[ZAR_FOOTER_SIZE];
	if (!ReadFully(loader_.get(), fileSize - ZAR_FOOTER_SIZE, ZAR_FOOTER_SIZE, footer)) {
		*errorString = "failed to read the footer";
		return false;
	}
	if (ReadBE32(footer + ZAR_FOOTER_SIZE - 4) != ZAR_MAGIC || ReadBE32(footer + ZAR_FOOTER_SIZE - 8) != ZAR_VERSION1) {
		*errorString = "bad magic or unsupported version";
		return false;
	}
	if (ReadBE64(footer + ZAR_FOOTER_SIZE - 16) != (u64)fileSize) {
		// Most likely truncated, like an incomplete download.
		*errorString = StringFromFormat("size mismatch: the footer says %llu, the file is %llu", (unsigned long long)ReadBE64(footer + ZAR_FOOTER_SIZE - 16), (unsigned long long)fileSize);
		return false;
	}

	struct Section {
		u64 offset;
		u64 size;
	};
	enum { COMPRESSED_DATA, OFFSET_RECORDS, NAMES, FILE_TREE, META_DIRECTORY, META_DATA, SECTION_COUNT };
	Section sections[SECTION_COUNT];
	for (int i = 0; i < SECTION_COUNT; i++) {
		sections[i].offset = ReadBE64(footer + i * 16);
		sections[i].size = ReadBE64(footer + i * 16 + 8);
		// Written so it can't overflow.
		if (sections[i].offset > (u64)fileSize || sections[i].size > (u64)fileSize - sections[i].offset) {
			*errorString = StringFromFormat("section %d out of range", i);
			return false;
		}
	}

	const Section &records = sections[OFFSET_RECORDS];
	const Section &names = sections[NAMES];
	const Section &tree = sections[FILE_TREE];
	if (records.size % ZAR_RECORD_SIZE != 0 || tree.size % ZAR_TREE_ENTRY_SIZE != 0 || tree.size == 0) {
		*errorString = "bad table size";
		return false;
	}
	if (records.size > ZAR_MAX_TABLE_SIZE || names.size > ZAR_MAX_TABLE_SIZE || tree.size > ZAR_MAX_TABLE_SIZE) {
		*errorString = "implausibly large tables";
		return false;
	}

	std::vector<u8> raw((size_t)records.size);
	if (!raw.empty() && !ReadFully(loader_.get(), records.offset, raw.size(), raw.data())) {
		*errorString = "failed to read the offset records";
		return false;
	}
	offsetRecords_.resize(raw.size() / ZAR_RECORD_SIZE);
	for (size_t i = 0; i < offsetRecords_.size(); i++) {
		const u8 *p = raw.data() + i * ZAR_RECORD_SIZE;
		offsetRecords_[i].baseOffset = ReadBE64(p);
		for (u32 j = 0; j < ZAR_BLOCKS_PER_RECORD; j++) {
			offsetRecords_[i].size[j] = ReadBE16(p + 8 + j * 2);
		}
	}

	nameTable_.resize((size_t)names.size);
	if (!nameTable_.empty() && !ReadFully(loader_.get(), names.offset, nameTable_.size(), nameTable_.data())) {
		*errorString = "failed to read the name table";
		return false;
	}

	raw.resize((size_t)tree.size);
	if (!ReadFully(loader_.get(), tree.offset, raw.size(), raw.data())) {
		*errorString = "failed to read the file tree";
		return false;
	}
	tree_.resize(raw.size() / ZAR_TREE_ENTRY_SIZE);
	for (size_t i = 0; i < tree_.size(); i++) {
		const u8 *p = raw.data() + i * ZAR_TREE_ENTRY_SIZE;
		tree_[i].nameOffsetAndType = ReadBE32(p);
		tree_[i].a = ReadBE32(p + 4);
		tree_[i].b = ReadBE32(p + 8);
		tree_[i].c = ReadBE32(p + 12);
	}

	compressedDataOffset_ = sections[COMPRESSED_DATA].offset;
	compressedDataSize_ = sections[COMPRESSED_DATA].size;
	blockCount_ = (u64)offsetRecords_.size() * ZAR_BLOCKS_PER_RECORD;
	const u64 dataSize = blockCount_ * ZAR_BLOCK_SIZE;

	if (tree_[0].IsFile() || !GetName(tree_[0].NameOffset()).empty()) {
		*errorString = "bad root directory";
		return false;
	}

	// Validate the tree up front, so the lookups don't have to. Children must come after their parent
	// (the tree is breadth first), which also rules out loops.
	for (size_t i = 0; i < tree_.size(); i++) {
		const TreeEntry &e = tree_[i];
		if (e.IsFile()) {
			const u64 offset = e.FileOffset();
			const u64 size = e.FileSize();
			if (offset > dataSize || size > dataSize - offset) {
				*errorString = StringFromFormat("file %d out of range", (int)i);
				return false;
			}
		} else if (e.ChildCount() != 0) {
			if (e.FirstChild() <= i || (u64)e.FirstChild() + e.ChildCount() > tree_.size()) {
				*errorString = StringFromFormat("directory %d has bad children", (int)i);
				return false;
			}
		}
		if (i != 0 && GetName(e.NameOffset()).empty()) {
			*errorString = StringFromFormat("entry %d has a bad name", (int)i);
			return false;
		}
	}

	dctx_ = ZSTD_createDCtx();
	if (!dctx_) {
		*errorString = "failed to create a zstd context";
		return false;
	}

	INFO_LOG(Log::Loader, "Opened ZArchive %s: %d entries, %llu blocks", loader_->GetPath().c_str(), (int)tree_.size(), (unsigned long long)blockCount_);
	return true;
}

std::string_view ZArchive::GetName(u32 nameOffset) const {
	if (nameOffset == ZAR_NO_NAME || nameOffset >= nameTable_.size())
		return std::string_view();
	size_t pos = nameOffset;
	size_t length = nameTable_[pos] & 0x7F;
	if (nameTable_[pos] & 0x80) {
		// Extended 15-bit length. (The reference reader gets this wrong, it shifts the first byte.)
		if (pos + 1 >= nameTable_.size())
			return std::string_view();
		length |= (size_t)nameTable_[pos + 1] << 7;
		pos += 2;
	} else {
		pos += 1;
	}
	if (length > nameTable_.size() - pos)
		return std::string_view();
	return std::string_view((const char *)nameTable_.data() + pos, length);
}

const ZArchive::TreeEntry *ZArchive::Entry(NodeHandle node) const {
	return node < tree_.size() ? &tree_[node] : nullptr;
}

ZArchive::NodeHandle ZArchive::LookUp(std::string_view path) const {
	NodeHandle current = ROOT_NODE;
	size_t pos = 0;
	while (pos < path.size()) {
		// Next component, skipping separators and "." components.
		while (pos < path.size() && (path[pos] == '/' || path[pos] == '\\'))
			pos++;
		size_t end = pos;
		while (end < path.size() && path[end] != '/' && path[end] != '\\')
			end++;
		const std::string_view component = path.substr(pos, end - pos);
		pos = end;
		if (component.empty() || component == ".")
			continue;

		const TreeEntry &dir = tree_[current];
		if (dir.IsFile())
			return INVALID_NODE;
		NodeHandle found = INVALID_NODE;
		// The children are sorted, but ZArchive's order isn't quite plain case-insensitive for
		// non-letters, so rather than trusting it with a binary search, just scan. Directories are small.
		for (u32 i = 0; i < dir.ChildCount(); i++) {
			const NodeHandle child = dir.FirstChild() + i;
			if (NameEquals(GetName(tree_[child].NameOffset()), component)) {
				found = child;
				break;
			}
		}
		if (found == INVALID_NODE)
			return INVALID_NODE;
		current = found;
	}
	return current;
}

bool ZArchive::IsDirectory(NodeHandle node) const {
	const TreeEntry *e = Entry(node);
	return e && !e->IsFile();
}

bool ZArchive::IsFile(NodeHandle node) const {
	const TreeEntry *e = Entry(node);
	return e && e->IsFile();
}

u64 ZArchive::GetFileSize(NodeHandle node) const {
	const TreeEntry *e = Entry(node);
	return (e && e->IsFile()) ? e->FileSize() : 0;
}

std::vector<ZArchive::DirEntry> ZArchive::ListDirectory(NodeHandle node) const {
	std::vector<DirEntry> entries;
	const TreeEntry *dir = Entry(node);
	if (!dir || dir->IsFile())
		return entries;
	entries.reserve(dir->ChildCount());
	for (u32 i = 0; i < dir->ChildCount(); i++) {
		const NodeHandle child = dir->FirstChild() + i;
		const TreeEntry &e = tree_[child];
		DirEntry entry;
		entry.name = GetName(e.NameOffset());
		entry.node = child;
		entry.isDirectory = !e.IsFile();
		entry.size = e.IsFile() ? e.FileSize() : 0;
		entries.push_back(entry);
	}
	return entries;
}

u64 ZArchive::TotalFileSize() const {
	u64 total = 0;
	for (const TreeEntry &e : tree_) {
		if (e.IsFile())
			total += e.FileSize();
	}
	return total;
}

bool ZArchive::ReadBlock(u64 blockIndex, u8 *dest) {
	if (blockIndex >= blockCount_)
		return false;
	const OffsetRecord &record = offsetRecords_[(size_t)(blockIndex / ZAR_BLOCKS_PER_RECORD)];
	const u32 sub = (u32)(blockIndex % ZAR_BLOCKS_PER_RECORD);
	u64 offset = record.baseOffset;
	for (u32 i = 0; i < sub; i++) {
		offset += (u64)record.size[i] + 1;
	}
	const u32 compressedSize = (u32)record.size[sub] + 1;
	if (offset > compressedDataSize_ || compressedSize > compressedDataSize_ - offset) {
		ERROR_LOG(Log::Loader, "ZArchive: block %llu out of range", (unsigned long long)blockIndex);
		return false;
	}
	offset += compressedDataOffset_;

	if (compressedSize == ZAR_BLOCK_SIZE) {
		// Didn't compress, stored as is.
		return ReadFully(loader_.get(), offset, ZAR_BLOCK_SIZE, dest);
	}

	compressedBuffer_.resize(ZAR_BLOCK_SIZE);
	if (!ReadFully(loader_.get(), offset, compressedSize, compressedBuffer_.data())) {
		ERROR_LOG(Log::Loader, "ZArchive: failed to read block %llu", (unsigned long long)blockIndex);
		return false;
	}
	const size_t result = ZSTD_decompressDCtx((ZSTD_DCtx *)dctx_, dest, ZAR_BLOCK_SIZE, compressedBuffer_.data(), compressedSize);
	if (result != ZAR_BLOCK_SIZE) {
		ERROR_LOG(Log::Loader, "ZArchive: failed to decompress block %llu: %s", (unsigned long long)blockIndex,
			ZSTD_isError(result) ? ZSTD_getErrorName(result) : "wrong size");
		return false;
	}
	return true;
}

const u8 *ZArchive::GetCachedBlock(u64 blockIndex) {
	CacheBlock *victim = nullptr;
	for (CacheBlock &block : cache_) {
		if (block.index == blockIndex) {
			block.lastUse = ++useCounter_;
			return block.data.get();
		}
		if (!victim || block.lastUse < victim->lastUse)
			victim = &block;
	}

	if (cache_.size() < ZAR_CACHE_BLOCKS) {
		cache_.emplace_back();
		victim = &cache_.back();
		victim->data.reset(new u8[ZAR_BLOCK_SIZE]);
	}

	victim->index = (u64)-1;
	if (!ReadBlock(blockIndex, victim->data.get()))
		return nullptr;
	victim->index = blockIndex;
	victim->lastUse = ++useCounter_;
	return victim->data.get();
}

size_t ZArchive::ReadFile(NodeHandle node, u64 offset, size_t length, void *buffer) {
	const TreeEntry *e = Entry(node);
	if (!e || !e->IsFile())
		return 0;
	const u64 fileSize = e->FileSize();
	if (offset >= fileSize)
		return 0;
	length = (size_t)std::min<u64>(length, fileSize - offset);

	std::lock_guard<std::mutex> guard(lock_);
	u64 pos = e->FileOffset() + offset;
	u8 *out = (u8 *)buffer;
	size_t remaining = length;
	while (remaining > 0) {
		const u64 blockIndex = pos / ZAR_BLOCK_SIZE;
		const u32 blockOffset = (u32)(pos % ZAR_BLOCK_SIZE);
		const size_t chunk = std::min<size_t>(remaining, ZAR_BLOCK_SIZE - blockOffset);
		if (chunk == ZAR_BLOCK_SIZE) {
			// A whole block. Straight into the destination, no need to go through (and flush) the cache.
			if (!ReadBlock(blockIndex, out))
				break;
		} else {
			const u8 *data = GetCachedBlock(blockIndex);
			if (!data)
				break;
			memcpy(out, data + blockOffset, chunk);
		}
		pos += chunk;
		out += chunk;
		remaining -= chunk;
	}
	return length - remaining;
}

FileLoader *ZArchive::ReleaseLoader() {
	return loader_.release();
}

static bool IsJunkName(std::string_view name) {
	// macOS resource fork junk, they come along when zipping folders on a Mac.
	return startsWith(name, "._");
}

static bool IsDiscImageName(std::string_view name) {
	return endsWithNoCase(name, ".iso") || endsWithNoCase(name, ".cso") || endsWithNoCase(name, ".chd");
}

// A PBP that holds a disc image (a PSN game, or an ISO converted to PBP), rather than a program.
// Those boot like an ISO, the same as when they're loose files. Checked like Identify_File does.
static bool IsDiscImagePBP(ZArchive &archive, ZArchive::NodeHandle node) {
	u8 header[0x28];
	if (archive.ReadFile(node, 0, sizeof(header), header) != sizeof(header) || memcmp(header, "\0PBP", 4) != 0)
		return false;
	const u32 psarOffset = (u32)header[0x24] | ((u32)header[0x25] << 8) | ((u32)header[0x26] << 16) | ((u32)header[0x27] << 24);
	char psar[4];
	if (archive.ReadFile(node, psarOffset, sizeof(psar), psar) != sizeof(psar))
		return false;
	return memcmp(psar, "NPUM", 4) == 0;
}

// "Disc 2" before "Disc 10". Case-insensitive otherwise.
static bool NaturalLess(std::string_view a, std::string_view b) {
	size_t i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		const bool digitA = a[i] >= '0' && a[i] <= '9';
		const bool digitB = b[j] >= '0' && b[j] <= '9';
		if (digitA && digitB) {
			size_t endA = i, endB = j;
			while (endA < a.size() && a[endA] >= '0' && a[endA] <= '9')
				endA++;
			while (endB < b.size() && b[endB] >= '0' && b[endB] <= '9')
				endB++;
			// Compare the numbers without converting them, so any length works. Skip leading zeroes.
			size_t startA = i, startB = j;
			while (startA + 1 < endA && a[startA] == '0')
				startA++;
			while (startB + 1 < endB && b[startB] == '0')
				startB++;
			const std::string_view numA = a.substr(startA, endA - startA);
			const std::string_view numB = b.substr(startB, endB - startB);
			if (numA.size() != numB.size())
				return numA.size() < numB.size();
			if (numA != numB)
				return numA < numB;
			i = endA;
			j = endB;
		} else {
			const char ca = AsciiToLower(a[i]);
			const char cb = AsciiToLower(b[j]);
			if (ca != cb)
				return (unsigned char)ca < (unsigned char)cb;
			i++;
			j++;
		}
	}
	return (a.size() - i) < (b.size() - j);
}

void ScanZArchiveContents(ZArchive &archive, ZArchiveContents *contents) {
	*contents = ZArchiveContents();

	for (const ZArchive::DirEntry &entry : archive.ListDirectory(ZArchive::ROOT_NODE)) {
		if (entry.isDirectory) {
			if (NameEquals(entry.name, "PSP")) {
				contents->hasMemStick = true;
			}
		} else if (IsJunkName(entry.name)) {
			continue;
		} else if (IsDiscImageName(entry.name)) {
			contents->discs.emplace_back(entry.name);
		} else if (endsWithNoCase(entry.name, ".pbp")) {
			if (IsDiscImagePBP(archive, entry.node)) {
				contents->discs.emplace_back(entry.name);
			} else {
				WARN_LOG(Log::Loader, "Ignoring %s in the ZArchive root: not a game disc. Homebrew goes in PSP/GAME/<folder>/EBOOT.PBP", std::string(entry.name).c_str());
			}
		}
	}
	std::sort(contents->discs.begin(), contents->discs.end(), [](const std::string &a, const std::string &b) {
		return NaturalLess(a, b);
	});

	if (contents->discs.empty() && contents->hasMemStick) {
		// No disc, look for a memstick game.
		const ZArchive::NodeHandle gameDir = archive.LookUp("PSP/GAME");
		std::vector<std::string> candidates;
		for (const ZArchive::DirEntry &entry : archive.ListDirectory(gameDir)) {
			if (!entry.isDirectory)
				continue;
			for (const ZArchive::DirEntry &file : archive.ListDirectory(entry.node)) {
				if (!file.isDirectory && NameEquals(file.name, "EBOOT.PBP")) {
					candidates.push_back(join(join("PSP/GAME/", entry.name), join("/", file.name)));
					break;
				}
			}
		}
		std::sort(candidates.begin(), candidates.end(), [](const std::string &a, const std::string &b) {
			return NaturalLess(a, b);
		});
		if (!candidates.empty()) {
			if (candidates.size() > 1) {
				WARN_LOG(Log::Loader, "ZArchive has %d games in PSP/GAME, booting %s", (int)candidates.size(), candidates[0].c_str());
			}
			contents->ebootPath = candidates[0];
		}
	}
}

int ZArchiveContents::FindDisc(std::string_view disc) const {
	for (size_t i = 0; i < discs.size(); i++) {
		if (NameEquals(discs[i], disc))
			return (int)i;
	}
	return -1;
}

std::string ZArchiveContents::BootTarget(std::string_view preferredDisc) const {
	const int index = preferredDisc.empty() ? -1 : FindDisc(preferredDisc);
	if (index >= 0)
		return discs[index];
	if (!discs.empty())
		return discs[0];
	return ebootPath;
}

ZArchiveFileLoader::ZArchiveFileLoader(std::shared_ptr<ZArchive> archive, std::string_view innerPath)
	: archive_(std::move(archive)), innerPath_(innerPath) {
	node_ = archive_->LookUp(innerPath_);
	if (node_ != ZArchive::INVALID_NODE && !archive_->IsFile(node_)) {
		node_ = ZArchive::INVALID_NODE;
	}
	size_ = archive_->GetFileSize(node_);

	// Lowercase, with the dot, like Path::GetFileExtension(). That's what Identify_File expects.
	const size_t slash = innerPath_.find_last_of("/\\");
	const size_t dot = innerPath_.find_last_of('.');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
		extension_ = innerPath_.substr(dot);
		for (char &c : extension_) {
			c = AsciiToLower(c);
		}
	}
}

size_t ZArchiveFileLoader::ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags) {
	if (bytes == 0)
		return 0;
	return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
}

size_t ZArchiveFileLoader::ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags) {
	if (absolutePos < 0 || node_ == ZArchive::INVALID_NODE)
		return 0;
	return archive_->ReadFile(node_, (u64)absolutePos, bytes, data);
}

FileLoader *OpenZArchiveBootTarget(FileLoader *archiveLoader, std::string *errorString) {
	std::shared_ptr<ZArchive> archive = ZArchive::Open(archiveLoader, errorString);
	if (!archive)
		return nullptr;

	ZArchiveContents contents;
	ScanZArchiveContents(*archive, &contents);
	if (!contents.HasBootable()) {
		if (errorString)
			*errorString = "No PSP game in the ZArchive (expected an ISO, CSO or CHD in its root, or PSP/GAME/<dir>/EBOOT.PBP)";
		// We hold the only reference, so we can give the loader back.
		archive->ReleaseLoader();
		return nullptr;
	}

	const std::string target = contents.BootTarget(GetRememberedZArchiveDisc(archiveLoader->GetPath()));
	INFO_LOG(Log::Loader, "Booting %s from ZArchive %s", target.c_str(), archiveLoader->GetPath().c_str());
	return new ZArchiveFileLoader(archive, target);
}

ZArchiveFileLoader *FindZArchiveFileLoader(FileLoader *loader) {
	// The chain is short, but don't trust it not to loop.
	for (int depth = 0; loader && depth < 16; depth++) {
		if (ZArchiveFileLoader *zar = dynamic_cast<ZArchiveFileLoader *>(loader))
			return zar;
		ProxiedFileLoader *proxied = dynamic_cast<ProxiedFileLoader *>(loader);
		if (!proxied)
			break;
		loader = proxied->Backend();
	}
	return nullptr;
}

// Most recently used first. Not many people have this many multi-disc games.
static const size_t MAX_REMEMBERED_DISCS = 200;
static std::mutex g_discMemoryLock;
static std::vector<std::pair<std::string, std::string>> g_discMemory;

std::string GetRememberedZArchiveDisc(const Path &archivePath) {
	const std::string path = archivePath.ToString();
	std::lock_guard<std::mutex> guard(g_discMemoryLock);
	for (const auto &entry : g_discMemory) {
		if (entry.first == path)
			return entry.second;
	}
	return std::string();
}

void RememberZArchiveDisc(const Path &archivePath, std::string_view disc) {
	const std::string path = archivePath.ToString();
	std::lock_guard<std::mutex> guard(g_discMemoryLock);
	g_discMemory.erase(std::remove_if(g_discMemory.begin(), g_discMemory.end(), [&](const std::pair<std::string, std::string> &entry) {
		return entry.first == path;
	}), g_discMemory.end());
	if (!disc.empty()) {
		g_discMemory.insert(g_discMemory.begin(), std::make_pair(path, std::string(disc)));
		if (g_discMemory.size() > MAX_REMEMBERED_DISCS)
			g_discMemory.resize(MAX_REMEMBERED_DISCS);
	}
}

// As pairs of keys, Archive1 = path and Disc1 = name in the archive, and so on.
void LoadZArchiveDiscMemory(const Section *section) {
	std::lock_guard<std::mutex> guard(g_discMemoryLock);
	g_discMemory.clear();
	for (size_t i = 1; i <= MAX_REMEMBERED_DISCS; i++) {
		std::string path, disc;
		if (!section->Get(StringFromFormat("Archive%d", (int)i), &path) || !section->Get(StringFromFormat("Disc%d", (int)i), &disc))
			break;
		if (!path.empty() && !disc.empty())
			g_discMemory.emplace_back(path, disc);
	}
}

void SaveZArchiveDiscMemory(Section *section) {
	std::lock_guard<std::mutex> guard(g_discMemoryLock);
	section->Clear();
	for (size_t i = 0; i < g_discMemory.size(); i++) {
		section->Set(StringFromFormat("Archive%d", (int)i + 1), g_discMemory[i].first);
		section->Set(StringFromFormat("Disc%d", (int)i + 1), g_discMemory[i].second);
	}
}

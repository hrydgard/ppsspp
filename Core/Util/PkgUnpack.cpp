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
#include <memory>

#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/ELF/PBPReader.h"
#include "Core/Loaders.h"
#include "Core/System.h"
#include "Core/Util/PkgUnpack.h"

extern "C" {
#include "ext/libkirk/AES.h"
}

// See docs/pkg_notes.md. Field offsets in the 0xC0-byte header:
static const u32 PKG_MAGIC = 0x7F504B47;  // "\x7FPKG"
static const u32 PKG_TYPE_PSP = 2;        // 1 is PS3.
// 0xC0 of header plus the 0x40-byte extended header after it, which is where the key index is.
static const size_t PKG_HEADER_SIZE = 0x100;
static const size_t PKG_ITEM_SIZE = 0x20;

// The two keys a PSP package's contents are encrypted with, picked per item. Vita packages derive
// theirs from the riv instead, which we don't handle - nothing here reads Vita packages.
static const u8 PKG_PSP_KEY[16] = {
	0x07, 0xf2, 0xc6, 0x82, 0x90, 0xb5, 0x0d, 0x2c, 0x33, 0x81, 0x8d, 0x70, 0x9b, 0x60, 0xe6, 0x2b,
};
static const u8 PKG_PS3_KEY[16] = {
	0x2e, 0x7b, 0x71, 0xd7, 0xc9, 0xc9, 0xa1, 0x4e, 0xa3, 0x22, 0x1f, 0x18, 0x88, 0x28, 0xb8, 0xf8,
};

// An item table with more entries than this is corrupt, not something we should try to allocate
// for. The biggest update package seen has a few hundred.
static const u32 PKG_MAX_ITEMS = 65536;
// Same idea for a single filename.
static const u32 PKG_MAX_NAME = 1024;

static const size_t PKG_COPY_BLOCK = 512 * 1024;

// Everything in a PKG header, its metadata and its item table is big-endian; the PBP inside is
// little-endian like the rest of the PSP world. None of it is guaranteed aligned, so read bytes.
static u16 Read16BE(const u8 *p) {
	return (u16)(((u32)p[0] << 8) | p[1]);
}
static u32 Read32BE(const u8 *p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}
static u64 Read64BE(const u8 *p) {
	return ((u64)Read32BE(p) << 32) | (u64)Read32BE(p + 4);
}
static u32 Read32LE(const u8 *p) {
	return ((u32)p[3] << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | (u32)p[0];
}

// AES-128-CTR, with the counter block starting at `iv` and incrementing once per 16 bytes. Both
// the counter and the increment are big-endian, and the counter wraps over the whole 128 bits.
static void AesCtrXor(const u8 *key, const u8 *iv, u64 blockIndex, u8 *data, size_t size) {
	AES_ctx ctx;
	AES_set_key(&ctx, key, 128);

	u8 counter[16];
	memcpy(counter, iv, 16);
	// Add blockIndex to the 128-bit big-endian counter.
	u32 carry = 0;
	for (int i = 15; i >= 0; i--) {
		const u32 sum = (u32)counter[i] + (u32)(blockIndex & 0xFF) + carry;
		counter[i] = (u8)sum;
		carry = sum >> 8;
		blockIndex >>= 8;
	}

	u8 stream[16];
	for (size_t pos = 0; pos < size; pos += 16) {
		AES_encrypt(&ctx, counter, stream);
		const size_t chunk = std::min<size_t>(16, size - pos);
		for (size_t i = 0; i < chunk; i++) {
			data[pos + i] ^= stream[i];
		}
		// Increment the counter block.
		for (int i = 15; i >= 0; i--) {
			if (++counter[i] != 0)
				break;
		}
	}
}

const u8 *PkgReader::ItemKey(const PkgItem &item) const {
	return item.pspType == 0x90 ? mainKey_ : PKG_PS3_KEY;
}

bool PkgReader::ReadEncrypted(u64 offset, size_t size, const u8 *key, u8 *out) {
	if (size == 0) {
		return true;
	}
	// The counter runs on 16-byte boundaries, so read from the containing block and skip the
	// leading bytes afterwards.
	const size_t skip = (size_t)(offset & 0xF);
	const u64 base = offset - skip;
	const size_t alignedSize = (skip + size + 15) & ~(size_t)0xF;

	if (base > dataSize_ || alignedSize > dataSize_ - base) {
		return false;
	}

	std::vector<u8> buf(alignedSize);
	if (loader_->ReadAt(dataOffset_ + base, alignedSize, buf.data()) != alignedSize) {
		return false;
	}
	AesCtrXor(key, riv_, base / 16, buf.data(), alignedSize);
	memcpy(out, buf.data() + skip, size);
	return true;
}

bool PkgReader::ReadItemData(const PkgItem &item, u64 offset, size_t size, u8 *out) {
	if (offset > item.dataSize || size > item.dataSize - offset) {
		return false;
	}
	return ReadEncrypted(item.dataOffset + offset, size, ItemKey(item), out);
}

bool PkgReader::ReadItem(const PkgItem &item, std::vector<u8> *out, size_t maxSize) {
	if (item.dataSize > maxSize) {
		return false;
	}
	out->resize((size_t)item.dataSize);
	return ReadItemData(item, 0, out->size(), out->data());
}

bool PkgReader::Open(FileLoader *loader, std::string *error) {
	loader_ = loader;
	info_ = PkgInfo();

	u8 header[PKG_HEADER_SIZE];
	if (!loader || loader->ReadAt(0, sizeof(header), header) != sizeof(header)) {
		*error = "Not a PKG file: too short";
		return false;
	}
	if (Read32BE(header) != PKG_MAGIC) {
		*error = "Not a PKG file";
		return false;
	}

	const u32 type = Read16BE(header + 0x06);
	if (type != PKG_TYPE_PSP) {
		*error = "Not a PSP PKG file";
		return false;
	}
	// Key index 1 is the only one a PSP package uses; 2-4 are Vita, and derive a key from the riv.
	const u32 keyIndex = header[0xE7] & 7;
	if (keyIndex != 1) {
		*error = StringFromFormat("Unsupported PKG key index %d", keyIndex);
		return false;
	}
	memcpy(mainKey_, PKG_PSP_KEY, sizeof(mainKey_));

	const u32 metaOffset = Read32BE(header + 0x08);
	const u32 metaCount = Read32BE(header + 0x0C);
	const u32 itemCount = Read32BE(header + 0x14);
	const u64 totalSize = Read64BE(header + 0x18);
	dataOffset_ = Read64BE(header + 0x20);
	dataSize_ = Read64BE(header + 0x28);
	memcpy(riv_, header + 0x70, sizeof(riv_));

	char contentId[0x31]{};
	memcpy(contentId, header + 0x30, 0x30);
	info_.contentId = contentId;

	const s64 fileSize = loader->FileSize();
	if (fileSize < 0 || (u64)fileSize < totalSize || dataOffset_ + dataSize_ > (u64)fileSize) {
		*error = "PKG file is truncated";
		return false;
	}
	if (itemCount == 0 || itemCount > PKG_MAX_ITEMS) {
		*error = "PKG file has a broken item table";
		return false;
	}

	// The metadata is in the clear. We only need three things out of it, and one of them (the item
	// table offset) is zero in every update package seen - but read it rather than assume.
	u32 itemsOffset = 0;
	u64 metaPos = metaOffset;
	for (u32 i = 0; i < metaCount; i++) {
		u8 rec[8];
		if (loader->ReadAt(metaPos, sizeof(rec), rec) != sizeof(rec)) {
			*error = "PKG metadata is truncated";
			return false;
		}
		const u32 id = Read32BE(rec);
		const u32 size = Read32BE(rec + 4);
		if (size > 0x1000) {
			*error = "PKG metadata is corrupt";
			return false;
		}
		std::vector<u8> value(size);
		if (size && loader->ReadAt(metaPos + 8, size, value.data()) != size) {
			*error = "PKG metadata is truncated";
			return false;
		}
		switch (id) {
		case 2:
			if (size >= 4) {
				info_.contentType = Read32BE(value.data());
			}
			break;
		case 6:
			info_.titleId = std::string((const char *)value.data(), strnlen((const char *)value.data(), size));
			break;
		case 13:
			if (size >= 4) {
				itemsOffset = Read32BE(value.data());
			}
			break;
		default:
			break;
		}
		metaPos += 8 + size;
	}

	if (info_.contentType != kPkgContentTypePSP) {
		*error = StringFromFormat("PKG holds content type 0x%x, not a PSP game", info_.contentType);
		return false;
	}

	// Item table, then the filenames it points at. Both live in the encrypted area, but a
	// filename is encrypted with its own item's key rather than the table's.
	std::vector<u8> table((size_t)itemCount * PKG_ITEM_SIZE);
	if (!ReadEncrypted(itemsOffset, table.size(), mainKey_, table.data())) {
		*error = "Failed to read the PKG item table";
		return false;
	}

	info_.items.reserve(itemCount);
	for (u32 i = 0; i < itemCount; i++) {
		const u8 *rec = table.data() + (size_t)i * PKG_ITEM_SIZE;
		const u32 nameOffset = Read32BE(rec);
		const u32 nameSize = Read32BE(rec + 4);

		PkgItem item;
		item.dataOffset = Read64BE(rec + 8);
		item.dataSize = Read64BE(rec + 16);
		item.pspType = rec[0x18];
		item.flags = rec[0x1B];

		if (item.dataOffset > dataSize_ || item.dataSize > dataSize_ - item.dataOffset) {
			*error = "PKG item points outside the file";
			return false;
		}
		if (nameSize == 0 || nameSize > PKG_MAX_NAME) {
			*error = "PKG item has a broken name";
			return false;
		}
		item.name.resize(nameSize);
		if (!ReadEncrypted(nameOffset, nameSize, ItemKey(item), (u8 *)item.name.data())) {
			*error = "Failed to read a PKG item name";
			return false;
		}
		// Names aren't terminated, but be forgiving if one is anyway.
		item.name.resize(strnlen(item.name.c_str(), item.name.size()));
		info_.items.push_back(item);
	}

	// The package's own PARAM.SFO. There's no reliable pointer to it in the metadata for PSP
	// packages (the field is empty), so go by name.
	for (const PkgItem &item : info_.items) {
		if (item.name != "PARAM.SFO") {
			continue;
		}
		std::vector<u8> sfoData;
		ParamSFOData sfo;
		if (ReadItem(item, &sfoData, 64 * 1024) && sfo.ReadSFO(sfoData)) {
			info_.title = sfo.GetValueString("TITLE");
			info_.category = sfo.GetValueString("CATEGORY");
			if (info_.titleId.empty()) {
				info_.titleId = sfo.GetValueString("TITLE_ID");
			}
		}
		break;
	}

	for (const PkgItem &item : info_.items) {
		if (item.name == "USRDIR/CONTENT/PBOOT.PBP" && !item.IsDirectory()) {
			ReadPBOOTInfo(item);
			break;
		}
	}

	INFO_LOG(Log::Loader, "PKG: %s (%s), %d items, update=%d for %s v%s",
		info_.contentId.c_str(), info_.category.c_str(), (int)info_.items.size(),
		(int)info_.isGameUpdate, info_.discId.c_str(), info_.discVersion.c_str());
	return true;
}

// The PBOOT is a normal PBP - only its DATA.PSP is encrypted, and its PARAM.SFO is the one that
// says which disc and disc version this patches. That's what makes matching an installed update
// against a game at boot time possible without decrypting anything.
bool PkgReader::ReadPBOOTInfo(const PkgItem &pboot) {
	// PBP header: magic, version, then eight little-endian subfile offsets.
	u8 header[0x28];
	if (pboot.dataSize < sizeof(header) || !ReadItemData(pboot, 0, sizeof(header), header)) {
		return false;
	}
	if (memcmp(header, "\0PBP", 4) != 0) {
		WARN_LOG(Log::Loader, "PKG: PBOOT.PBP isn't a PBP");
		return false;
	}
	const u32 sfoOffset = Read32LE(header + 0x08);
	const u32 iconOffset = Read32LE(header + 0x0C);
	if (sfoOffset > iconOffset || iconOffset > pboot.dataSize) {
		return false;
	}
	const u32 sfoSize = iconOffset - sfoOffset;
	if (sfoSize == 0 || sfoSize > 64 * 1024) {
		return false;
	}

	std::vector<u8> sfoData(sfoSize);
	if (!ReadItemData(pboot, sfoOffset, sfoSize, sfoData.data())) {
		return false;
	}
	ParamSFOData sfo;
	if (!sfo.ReadSFO(sfoData)) {
		return false;
	}

	info_.discId = sfo.GetValueString("DISC_ID");
	info_.discVersion = sfo.GetValueString("DISC_VERSION");
	info_.appVer = sfo.GetValueString("APP_VER");
	info_.systemVer = sfo.GetValueString("PSP_SYSTEM_VER");
	info_.pbootTitle = sfo.GetValueString("PBOOT_TITLE");
	if (info_.title.empty()) {
		info_.title = sfo.GetValueString("TITLE");
	}
	if (info_.discVersion.empty()) {
		info_.discVersion = "1.00";
	}
	// A disc ID is what the install is keyed on, so without one there's nowhere to put this.
	info_.isGameUpdate = !info_.discId.empty();
	return info_.isGameUpdate;
}

// Rejects anything that could escape the destination directory. Package filenames are attacker
// data as far as we're concerned.
static bool IsSafeRelativePath(std::string_view name) {
	if (name.empty() || name.size() > PKG_MAX_NAME) {
		return false;
	}
	if (name.front() == '/' || name.find('\\') != std::string_view::npos || name.find(':') != std::string_view::npos) {
		return false;
	}
	size_t start = 0;
	while (start <= name.size()) {
		const size_t slash = name.find('/', start);
		const std::string_view part = name.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
		if (part.empty() || part == "." || part == "..") {
			return false;
		}
		for (char c : part) {
			// Control characters in a filename are never legitimate here.
			if ((unsigned char)c < 0x20) {
				return false;
			}
		}
		if (slash == std::string_view::npos) {
			break;
		}
		start = slash + 1;
	}
	return true;
}

std::string PkgItemInstallPath(const PkgItem &item) {
	// A package wraps its payload PS3-style. USRDIR/CONTENT/ is where the patch's files live, and
	// USRDIR/ itself only ever holds ISO.BIN.EDAT; both map to the game folder root.
	std::string_view name = item.name;
	if (name == "USRDIR" || name == "USRDIR/CONTENT") {
		// The wrappers themselves. Both stand for the game folder, which already exists.
		return std::string();
	}
	if (startsWith(name, "USRDIR/CONTENT/")) {
		name = name.substr(strlen("USRDIR/CONTENT/"));
	} else if (startsWith(name, "USRDIR/")) {
		name = name.substr(strlen("USRDIR/"));
	} else {
		// Everything at the root is store metadata - PARAM.SFO, PS3LOGO.DAT, ICON0.PNG and
		// friends. Installing the SFO would make PPSSPP mistake the folder for save data.
		return std::string();
	}
	if (name.empty() || !IsSafeRelativePath(name)) {
		return std::string();
	}
	return std::string(name);
}

u64 PkgInstalledSize(const PkgInfo &info) {
	u64 total = 0;
	for (const PkgItem &item : info.items) {
		if (!item.IsDirectory() && !PkgItemInstallPath(item).empty()) {
			total += item.dataSize;
		}
	}
	return total;
}

bool InstallPkg(PkgReader &reader, const Path &destDir, const std::function<void(float)> &progress, std::string *error) {
	const PkgInfo &info = reader.Info();

	const u64 totalBytes = PkgInstalledSize(info);
	if (totalBytes == 0) {
		*error = "Nothing to install in this PKG";
		return false;
	}

	if (!File::CreateFullPath(destDir)) {
		*error = "Failed to create the destination folder";
		return false;
	}

	u64 writtenBytes = 0;
	std::vector<u8> buffer(PKG_COPY_BLOCK);

	for (const PkgItem &item : info.items) {
		const std::string relative = PkgItemInstallPath(item);
		if (relative.empty()) {
			if (!item.IsDirectory() && !startsWith(item.name, "USRDIR")) {
				// Expected - store metadata. Anything else is worth a line in the log.
				VERBOSE_LOG(Log::Loader, "PKG: skipping '%s'", item.name.c_str());
			}
			continue;
		}
		const Path destPath = destDir / relative;
		if (item.IsDirectory()) {
			if (!File::CreateFullPath(destPath)) {
				*error = "Failed to create a folder in the destination";
				return false;
			}
			continue;
		}
		if (!File::CreateFullPath(destPath.NavigateUp())) {
			*error = "Failed to create a folder in the destination";
			return false;
		}

		FILE *f = File::OpenCFile(destPath, "wb");
		if (!f) {
			*error = "Failed to write to the destination folder";
			return false;
		}

		u64 pos = 0;
		bool failed = false;
		while (pos < item.dataSize) {
			const size_t chunk = (size_t)std::min<u64>(buffer.size(), item.dataSize - pos);
			if (!reader.ReadItemData(item, pos, chunk, buffer.data())) {
				*error = "Failed to read from the PKG file";
				failed = true;
				break;
			}
			if (fwrite(buffer.data(), 1, chunk, f) != chunk) {
				*error = "Failed to write to the destination folder";
				failed = true;
				break;
			}
			pos += chunk;
			writtenBytes += chunk;
			if (progress) {
				progress((float)((double)writtenBytes / (double)totalBytes));
			}
		}
		fclose(f);
		if (failed) {
			File::Delete(destPath);
			return false;
		}
		INFO_LOG(Log::Loader, "PKG: installed %s (%lld bytes)", relative.c_str(), (long long)item.dataSize);
	}

	if (progress) {
		progress(1.0f);
	}
	return true;
}

bool FindInstalledGameUpdate(std::string_view discId, InstalledGameUpdate *update) {
	if (discId.empty()) {
		return false;
	}
	const Path folder = GetSysDirectory(DIRECTORY_GAME) / std::string(discId);
	const Path pbootPath = folder / "PBOOT.PBP";
	if (!File::Exists(pbootPath)) {
		return false;
	}

	// The version we want is in the PBOOT's own PARAM.SFO, which isn't encrypted.
	std::unique_ptr<FileLoader> loader(ConstructFileLoader(pbootPath));
	if (!loader) {
		return false;
	}
	PBPReader pbp(loader.get());
	std::vector<u8> sfoData;
	ParamSFOData sfo;
	if (!pbp.IsValid() || !pbp.GetSubFile(PBP_PARAM_SFO, &sfoData) || !sfo.ReadSFO(sfoData)) {
		WARN_LOG(Log::Loader, "'%s' doesn't look like a game update", pbootPath.c_str());
		return false;
	}

	update->folder = folder;
	update->pbootPath = pbootPath;
	update->appVer = sfo.GetValueString("APP_VER");
	update->discVersion = sfo.GetValueString("DISC_VERSION");
	update->title = sfo.GetValueString("PBOOT_TITLE");
	update->sharesFolderWithGame = File::Exists(folder / "EBOOT.PBP");
	update->sizeOnDisk = update->sharesFolderWithGame
		? (u64)std::max<s64>(0, File::GetFileSize(pbootPath))
		: File::ComputeRecursiveDirectorySize(folder);
	return true;
}

bool DeleteInstalledGameUpdate(const InstalledGameUpdate &update) {
	const bool useTrash = System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN);
	// Only the PBOOT when the folder is a game in its own right - see the struct's comment.
	const Path target = update.sharesFolderWithGame ? update.pbootPath : update.folder;
	INFO_LOG(Log::Loader, "Removing game update '%s'", target.c_str());
	if (useTrash) {
		// TODO: No way to tell whether this succeeded.
		System_MoveToTrash(target);
		return true;
	}
	return update.sharesFolderWithGame ? File::Delete(target) : File::DeleteDirRecursively(target);
}

// Copyright (c) 2016- PPSSPP Project.

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

#include "ppsspp_config.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <png.h>

#include "ext/xxhash.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Format/ZIMLoad.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/LogReporting.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/Waitable.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/ThreadPools.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/Common/TextureReplacer.h"
#include "GPU/Common/TextureDecoder.h"

static const std::string INI_FILENAME = "textures.ini";
static const std::string ZIP_FILENAME = "textures.zip";
static const std::string NEW_TEXTURE_DIR = "new/";
static const int VERSION = 1;
static const int MAX_MIP_LEVELS = 12;  // 12 should be plenty, 8 is the max mip levels supported by the PSP.
static const double MAX_CACHE_SIZE = 4.0;

enum class ReplacedImageType {
	PNG,
	ZIM,
	INVALID,
};

static inline ReplacedImageType IdentifyMagic(const uint8_t magic[4]) {
	if (strncmp((const char *)magic, "ZIMG", 4) == 0)
		return ReplacedImageType::ZIM;
	if (magic[0] == 0x89 && strncmp((const char *)&magic[1], "PNG", 3) == 0)
		return ReplacedImageType::PNG;
	return ReplacedImageType::INVALID;
}

static ReplacedImageType Identify(VFSBackend *vfs, VFSOpenFile *openFile, std::string *outMagic) {
	uint8_t magic[4];
	if (vfs->Read(openFile, magic, 4) != 4) {
		*outMagic = "FAIL";
		return ReplacedImageType::INVALID;
	}
	// Turn the signature into a readable string that we can display in an error message.
	*outMagic = std::string((const char *)magic, 4);
	for (int i = 0; i < outMagic->size(); i++) {
		if ((s8)(*outMagic)[i] < 32) {
			(*outMagic)[i] = '_';
		}
	}
	vfs->Rewind(openFile);
	return IdentifyMagic(magic);
}

TextureReplacer::TextureReplacer(Draw::DrawContext *draw) {
	// TODO: Check draw for supported texture formats.
}

TextureReplacer::~TextureReplacer() {
	for (auto &iter : cache_) {
		delete iter.second;
	}

	delete vfs_;
}

void TextureReplacer::Init() {
	NotifyConfigChanged();
}

void TextureReplacer::NotifyConfigChanged() {
	gameID_ = g_paramSFO.GetDiscID();

	bool wasEnabled = enabled_;
	enabled_ = g_Config.bReplaceTextures || g_Config.bSaveNewTextures;
	if (enabled_) {
		basePath_ = GetSysDirectory(DIRECTORY_TEXTURES) / gameID_;

		Path newTextureDir = basePath_ / NEW_TEXTURE_DIR;

		// If we're saving, auto-create the directory.
		if (g_Config.bSaveNewTextures && !File::Exists(newTextureDir)) {
			File::CreateFullPath(newTextureDir);
			File::CreateEmptyFile(newTextureDir / ".nomedia");
		}

		enabled_ = File::IsDirectory(basePath_);
	} else if (wasEnabled) {
		delete vfs_;
		vfs_ = nullptr;
		Decimate(ReplacerDecimateMode::ALL);
	}

	if (enabled_) {
		enabled_ = LoadIni();
	}
}

bool TextureReplacer::LoadIni() {
	hash_ = ReplacedTextureHash::QUICK;
	aliases_.clear();
	hashranges_.clear();
	filtering_.clear();
	reducehashranges_.clear();

	allowVideo_ = false;
	ignoreAddress_ = false;
	reduceHash_ = false;
	reduceHashGlobalValue = 0.5;
	// Prevents dumping the mipmaps.
	ignoreMipmap_ = false;

	delete vfs_;
	vfs_ = nullptr;

	// First, check for textures.zip, which is used to reduce IO.
	VFSBackend *dir = ZipFileReader::Create(basePath_ / ZIP_FILENAME, "");
	if (!dir) {
		vfsIsZip_ = false;
		dir = new DirectoryReader(basePath_);
	} else {
		vfsIsZip_ = true;
	}

	IniFile ini;
	bool iniLoaded = ini.LoadFromVFS(g_VFS, (basePath_ / INI_FILENAME).ToString());

	if (iniLoaded) {
		if (!LoadIniValues(ini)) {
			delete dir;
			return false;
		}

		// Allow overriding settings per game id.
		std::string overrideFilename;
		if (ini.GetOrCreateSection("games")->Get(gameID_.c_str(), &overrideFilename, "")) {
			if (!overrideFilename.empty() && overrideFilename != INI_FILENAME) {
				IniFile overrideIni;
				iniLoaded = overrideIni.LoadFromVFS(*dir, overrideFilename);
				if (!iniLoaded) {
					ERROR_LOG(G3D, "Failed to load extra texture ini: %s", overrideFilename.c_str());
					// Since this error is most likely to occure for texture pack creators, let's just bail here
					// so that the creator is more likely to look in the logs for what happened.
					delete dir;
					return false;
				}

				INFO_LOG(G3D, "Loading extra texture ini: %s", overrideFilename.c_str());
				if (!LoadIniValues(overrideIni, true)) {
					delete dir;
					return false;
				}
			}
		}
	} else {
		if (vfsIsZip_) {
			// We don't accept zip files without inis.
			ERROR_LOG(G3D, "Texture pack lacking ini file: %s", basePath_.c_str());
			delete dir;
			return false;
		} else {
			WARN_LOG(G3D, "Texture pack lacking ini file: %s", basePath_.c_str());
		}
	}

	vfs_ = dir;
	INFO_LOG(G3D, "Texture pack activated from '%s'", basePath_.c_str());

	// The ini doesn't have to exist for the texture directory or zip to be valid.
	return true;
}

bool TextureReplacer::LoadIniValues(IniFile &ini, bool isOverride) {
	auto options = ini.GetOrCreateSection("options");
	std::string hash;
	options->Get("hash", &hash, "");
	if (strcasecmp(hash.c_str(), "quick") == 0) {
		hash_ = ReplacedTextureHash::QUICK;
	} else if (strcasecmp(hash.c_str(), "xxh32") == 0) {
		hash_ = ReplacedTextureHash::XXH32;
	} else if (strcasecmp(hash.c_str(), "xxh64") == 0) {
		hash_ = ReplacedTextureHash::XXH64;
	} else if (!isOverride || !hash.empty()) {
		ERROR_LOG(G3D, "Unsupported hash type: %s", hash.c_str());
		return false;
	}

	options->Get("video", &allowVideo_, allowVideo_);
	options->Get("ignoreAddress", &ignoreAddress_, ignoreAddress_);
	// Multiplies sizeInRAM/bytesPerLine in XXHASH by 0.5.
	options->Get("reduceHash", &reduceHash_, reduceHash_);
	options->Get("ignoreMipmap", &ignoreMipmap_, ignoreMipmap_);
	if (reduceHash_ && hash_ == ReplacedTextureHash::QUICK) {
		reduceHash_ = false;
		ERROR_LOG(G3D, "Texture Replacement: reduceHash option requires safer hash, use xxh32 or xxh64 instead.");
	}

	if (ignoreAddress_ && hash_ == ReplacedTextureHash::QUICK) {
		ignoreAddress_ = false;
		ERROR_LOG(G3D, "Texture Replacement: ignoreAddress option requires safer hash, use xxh32 or xxh64 instead.");
	}

	int version = 0;
	if (options->Get("version", &version, 0) && version > VERSION) {
		ERROR_LOG(G3D, "Unsupported texture replacement version %d, trying anyway", version);
	}

	bool filenameWarning = false;
	if (ini.HasSection("hashes")) {
		auto hashes = ini.GetOrCreateSection("hashes")->ToMap();
		// Format: hashname = filename.png
		bool checkFilenames = g_Config.bSaveNewTextures && !g_Config.bIgnoreTextureFilenames && !vfsIsZip_;

		std::map<ReplacementCacheKey, std::map<int, std::string>> filenameMap;

		for (const auto &item : hashes) {
			ReplacementCacheKey key(0, 0);
			int level = 0;  // sscanf might fail to pluck the level, but that's ok, we default to 0.
			if (sscanf(item.first.c_str(), "%16llx%8x_%d", &key.cachekey, &key.hash, &level) >= 1) {
				filenameMap[key][level] = item.second;
				if (checkFilenames) {
#if PPSSPP_PLATFORM(WINDOWS)
					// Uppercase probably means the filenames don't match.
					// Avoiding an actual check of the filenames to avoid performance impact.
					filenameWarning = filenameWarning || item.second.find_first_of("\\ABCDEFGHIJKLMNOPQRSTUVWXYZ:<>|?*") != std::string::npos;
#else
					filenameWarning = filenameWarning || item.second.find_first_of("\\:<>|?*") != std::string::npos;
#endif
				}
			} else {
				ERROR_LOG(G3D, "Unsupported syntax under [hashes]: %s", item.first.c_str());
			}
		}

		// Now, translate the filenameMap to the final aliasMap.
		for (auto &pair : filenameMap) {
			std::string alias;
			int mipIndex = 0;
			for (auto &level : pair.second) {
				if (level.first == mipIndex) {
					alias += level.second + "|";
					mipIndex++;
				} else {
					WARN_LOG(G3D, "Non-sequential mip index %d, breaking. filenames=%s", level.first, level.second.c_str());
					break;
				}
			}
			if (alias == "|") {
				alias = "";  // marker for no replacement
			}
			aliases_[pair.first] = alias;
		}
	}

	if (filenameWarning) {
		auto err = GetI18NCategory("Error");
		host->NotifyUserMessage(err->T("textures.ini filenames may not be cross-platform (banned characters)"), 6.0f);
	}

	if (ini.HasSection("hashranges")) {
		auto hashranges = ini.GetOrCreateSection("hashranges")->ToMap();
		// Format: addr,w,h = newW,newH
		for (const auto &item : hashranges) {
			ParseHashRange(item.first, item.second);
		}
	}

	if (ini.HasSection("filtering")) {
		auto filters = ini.GetOrCreateSection("filtering")->ToMap();
		// Format: hashname = nearest or linear
		for (const auto &item : filters) {
			ParseFiltering(item.first, item.second);
		}
	}

	if (ini.HasSection("reducehashranges")) {
		auto reducehashranges = ini.GetOrCreateSection("reducehashranges")->ToMap();
		// Format: w,h = reducehashvalues
		for (const auto& item : reducehashranges) {
			ParseReduceHashRange(item.first, item.second);
		}
	}

	return true;
}

void TextureReplacer::ParseHashRange(const std::string &key, const std::string &value) {
	std::vector<std::string> keyParts;
	SplitString(key, ',', keyParts);
	std::vector<std::string> valueParts;
	SplitString(value, ',', valueParts);

	if (keyParts.size() != 3 || valueParts.size() != 2) {
		ERROR_LOG(G3D, "Ignoring invalid hashrange %s = %s, expecting addr,w,h = w,h", key.c_str(), value.c_str());
		return;
	}

	u32 addr;
	u32 fromW;
	u32 fromH;
	if (!TryParse(keyParts[0], &addr) || !TryParse(keyParts[1], &fromW) || !TryParse(keyParts[2], &fromH)) {
		ERROR_LOG(G3D, "Ignoring invalid hashrange %s = %s, key format is 0x12345678,512,512", key.c_str(), value.c_str());
		return;
	}

	u32 toW;
	u32 toH;
	if (!TryParse(valueParts[0], &toW) || !TryParse(valueParts[1], &toH)) {
		ERROR_LOG(G3D, "Ignoring invalid hashrange %s = %s, value format is 512,512", key.c_str(), value.c_str());
		return;
	}

	if (toW > fromW || toH > fromH) {
		ERROR_LOG(G3D, "Ignoring invalid hashrange %s = %s, range bigger than source", key.c_str(), value.c_str());
		return;
	}

	const u64 rangeKey = ((u64)addr << 32) | ((u64)fromW << 16) | fromH;
	hashranges_[rangeKey] = WidthHeightPair(toW, toH);
}

void TextureReplacer::ParseFiltering(const std::string &key, const std::string &value) {
	ReplacementCacheKey itemKey(0, 0);
	if (sscanf(key.c_str(), "%16llx%8x", &itemKey.cachekey, &itemKey.hash) >= 1) {
		if (!strcasecmp(value.c_str(), "nearest")) {
			filtering_[itemKey] = TEX_FILTER_FORCE_NEAREST;
		} else if (!strcasecmp(value.c_str(), "linear")) {
			filtering_[itemKey] = TEX_FILTER_FORCE_LINEAR;
		} else if (!strcasecmp(value.c_str(), "auto")) {
			filtering_[itemKey] = TEX_FILTER_AUTO;
		} else {
			ERROR_LOG(G3D, "Unsupported syntax under [filtering]: %s", value.c_str());
		}
	} else {
		ERROR_LOG(G3D, "Unsupported syntax under [filtering]: %s", key.c_str());
	}
}

void TextureReplacer::ParseReduceHashRange(const std::string& key, const std::string& value) {
	std::vector<std::string> keyParts;
	SplitString(key, ',', keyParts);
	std::vector<std::string> valueParts;
	SplitString(value, ',', valueParts);

	if (keyParts.size() != 2 || valueParts.size() != 1) {
		ERROR_LOG(G3D, "Ignoring invalid reducehashrange %s = %s, expecting w,h = reducehashvalue", key.c_str(), value.c_str());
		return;
	}

	u32 forW;
	u32 forH;
	if (!TryParse(keyParts[0], &forW) || !TryParse(keyParts[1], &forH)) {
		ERROR_LOG(G3D, "Ignoring invalid reducehashrange %s = %s, key format is 512,512", key.c_str(), value.c_str());
		return;
	}

	float rhashvalue;
	if (!TryParse(valueParts[0], &rhashvalue)) {
		ERROR_LOG(G3D, "Ignoring invalid reducehashrange %s = %s, value format is 0.5", key.c_str(), value.c_str());
		return;
	}

	if (rhashvalue == 0) {
		ERROR_LOG(G3D, "Ignoring invalid hashrange %s = %s, reducehashvalue can't be 0", key.c_str(), value.c_str());
		return;
	}

	const u64 reducerangeKey = ((u64)forW << 16) | forH;
	reducehashranges_[reducerangeKey] = rhashvalue;
}

u32 TextureReplacer::ComputeHash(u32 addr, int bufw, int w, int h, GETextureFormat fmt, u16 maxSeenV) {
	_dbg_assert_msg_(enabled_, "Replacement not enabled");

	if (!LookupHashRange(addr, w, h)) {
		// There wasn't any hash range, let's fall back to maxSeenV logic.
		if (h == 512 && maxSeenV < 512 && maxSeenV != 0) {
			h = (int)maxSeenV;
		}
	}

	const u8 *checkp = Memory::GetPointerUnchecked(addr);
	if (reduceHash_) {
		reduceHashSize = LookupReduceHashRange(w, h);
		// default to reduceHashGlobalValue which default is 0.5
	}
	if (bufw <= w) {
		// We can assume the data is contiguous.  These are the total used pixels.
		const u32 totalPixels = bufw * h + (w - bufw);
		const u32 sizeInRAM = (textureBitsPerPixel[fmt] * totalPixels) / 8 * reduceHashSize;

		switch (hash_) {
		case ReplacedTextureHash::QUICK:
			return StableQuickTexHash(checkp, sizeInRAM);
		case ReplacedTextureHash::XXH32:
			return XXH32(checkp, sizeInRAM, 0xBACD7814);
		case ReplacedTextureHash::XXH64:
			return XXH64(checkp, sizeInRAM, 0xBACD7814);
		default:
			return 0;
		}
	} else {
		// We have gaps.  Let's hash each row and sum.
		const u32 bytesPerLine = (textureBitsPerPixel[fmt] * w) / 8 * reduceHashSize;
		const u32 stride = (textureBitsPerPixel[fmt] * bufw) / 8;

		u32 result = 0;
		switch (hash_) {
		case ReplacedTextureHash::QUICK:
			for (int y = 0; y < h; ++y) {
				u32 rowHash = StableQuickTexHash(checkp, bytesPerLine);
				result = (result * 11) ^ rowHash;
				checkp += stride;
			}
			break;

		case ReplacedTextureHash::XXH32:
			for (int y = 0; y < h; ++y) {
				u32 rowHash = XXH32(checkp, bytesPerLine, 0xBACD7814);
				result = (result * 11) ^ rowHash;
				checkp += stride;
			}
			break;

		case ReplacedTextureHash::XXH64:
			for (int y = 0; y < h; ++y) {
				u32 rowHash = XXH64(checkp, bytesPerLine, 0xBACD7814);
				result = (result * 11) ^ rowHash;
				checkp += stride;
			}
			break;

		default:
			break;
		}

		return result;
	}
}

ReplacedTexture *TextureReplacer::FindReplacement(u64 cachekey, u32 hash, int w, int h, double budget) {
	// Only actually replace if we're replacing.  We might just be saving.
	if (!Enabled() || !g_Config.bReplaceTextures) {
		return nullptr;
	}

	ReplacementCacheKey replacementKey(cachekey, hash);
	auto it = cache_.find(replacementKey);
	if (it != cache_.end()) {
		if (!it->second->prepareDone_ && budget > 0.0) {
			// We don't do this on a thread, but we only do it while within budget.
			PopulateReplacement(it->second, cachekey, hash, w, h);
		}
		return it->second;
	}

	// Okay, let's construct the result.

	ReplacedTexture *result = new ReplacedTexture();
	cache_[replacementKey] = result;
	result->vfs_ = this->vfs_;
	if (budget > 0.0) {
		PopulateReplacement(result, cachekey, hash, w, h);
	}
	return result;
}

void TextureReplacer::PopulateReplacement(ReplacedTexture *texture, u64 cachekey, u32 hash, int w, int h) {
	int newW = w;
	int newH = h;
	LookupHashRange(cachekey >> 32, newW, newH);

	if (ignoreAddress_) {
		cachekey = cachekey & 0xFFFFFFFFULL;
	}

	bool foundReplacement = false;
	bool ignored = false;
	const std::string hashfiles = LookupHashFile(cachekey, hash, &foundReplacement, &ignored);

	if (!foundReplacement || ignored) {
		// nothing to do?
		texture->prepareDone_ = true;
		return;
	}

	// INFO_LOG(G3D, "Found: %s", hashfiles.c_str());

	std::vector<std::string> filenames;
	SplitString(hashfiles, '|', filenames);

	for (int i = 0; i < std::min(MAX_MIP_LEVELS, (int)filenames.size()); ++i) {
		if (filenames[i].empty()) {
			// Out of valid mip levels.  Bail out.
			break;
		}

		const Path filename = basePath_ / filenames[i];

		// TODO: Here, if we find a file with multiple built-in mipmap levels,
		// we'll have to change a bit how things work...
		ReplacedTextureLevel level;
		level.file = filename;

		if (i == 0) {
			texture->fmt = Draw::DataFormat::R8G8B8A8_UNORM;
		}

		bool good;

		VFSFileReference *fileRef = vfs_->GetFile(filenames[i].c_str());
		if (!fileRef) {
			// If the file doesn't exist, let's just bail immediately here.
			break;
		}

		level.fileRef = fileRef;
		good = PopulateLevel(level, false);

		// We pad files that have been hashrange'd so they are the same texture size.
		level.w = (level.w * w) / newW;
		level.h = (level.h * h) / newH;

		if (good && i != 0) {
			// Check that the mipmap size is correct.  Can't load mips of the wrong size.
			if (level.w != (texture->levels_[0].w >> i) || level.h != (texture->levels_[0].h >> i)) {
				 WARN_LOG(G3D, "Replacement mipmap invalid: size=%dx%d, expected=%dx%d (level %d, '%s')", level.w, level.h, texture->levels_[0].w >> i, texture->levels_[0].h >> i, i, filename.c_str());
				 good = false;
			}
		}

		if (good)
			texture->levels_.push_back(level);
		// Otherwise, we're done loading mips (bad PNG or bad size, either way.)
		else
			break;
	}

	// Populate the data pointer.
	texture->levelData_ = &levelCache_[hashfiles];
	texture->prepareDone_ = true;
}

bool TextureReplacer::PopulateLevel(ReplacedTextureLevel &level, bool ignoreError) {
	bool good = false;

	if (!level.fileRef) {
		if (!ignoreError)
			ERROR_LOG(G3D, "Error opening replacement texture file '%s' in textures.zip", level.file.c_str());
		return false;
	}

	size_t fileSize;
	VFSOpenFile *file = vfs_->OpenFileForRead(level.fileRef, &fileSize);
	if (!file) {
		return false;
	}

	std::string magic;
	auto imageType = Identify(vfs_, file, &magic);

	if (imageType == ReplacedImageType::ZIM) {
		uint32_t ignore = 0;
		struct ZimHeader {
			uint32_t magic;
			uint32_t w;
			uint32_t h;
			uint32_t flags;
		} header;
		good = vfs_->Read(file, &header, sizeof(header)) == sizeof(header);
		level.w = header.w;
		level.h = header.h;
		good = (header.flags & ZIM_FORMAT_MASK) == ZIM_RGBA8888;
	} else if (imageType == ReplacedImageType::PNG) {
		PNGHeaderPeek headerPeek;
		good = vfs_->Read(file, &headerPeek, sizeof(headerPeek)) == sizeof(headerPeek);
		if (good && headerPeek.IsValidPNGHeader()) {
			level.w = headerPeek.Width();
			level.h = headerPeek.Height();
			good = true;
		} else {
			ERROR_LOG(G3D, "Could not get PNG dimensions: %s (zip)", level.file.ToVisualString().c_str());
			good = false;
		}
	} else {
		ERROR_LOG(G3D, "Could not load texture replacement info: %s - unsupported format %s", level.file.ToVisualString().c_str(), magic.c_str());
	}
	vfs_->CloseFile(file);

	return good;
}

static bool WriteTextureToPNG(png_imagep image, const Path &filename, int convert_to_8bit, const void *buffer, png_int_32 row_stride, const void *colormap) {
	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		ERROR_LOG(IO, "Unable to open texture file for writing.");
		return false;
	}

	if (png_image_write_to_stdio(image, fp, convert_to_8bit, buffer, row_stride, colormap)) {
		fclose(fp);
		return true;
	} else {
		ERROR_LOG(SYSTEM, "Texture PNG encode failed.");
		fclose(fp);
		remove(filename.c_str());
		return false;
	}
}

// We save textures on threadpool tasks since it's a fire-and-forget task, and both I/O and png compression
// can be pretty slow.
class SaveTextureTask : public Task {
public:
	std::vector<u8> rgbaData;

	int w = 0;
	int h = 0;
	int pitch = 0;  // bytes

	Path basePath;
	std::string hashfile;
	u32 replacedInfoHash = 0;

	bool skipIfExists = false;

	SaveTextureTask(std::vector<u8> &&_rgbaData) : rgbaData(std::move(_rgbaData)) {}

	// This must be set to I/O blocking because of Android storage (so we attach the thread to JNI), while being CPU heavy too.
	TaskType Type() const override { return TaskType::IO_BLOCKING; }

	TaskPriority Priority() const override {
		return TaskPriority::LOW;
	}

	void Run() override {
		const Path filename = basePath / hashfile;
		const Path saveFilename = basePath / NEW_TEXTURE_DIR / hashfile;

		// Should we skip writing if the newly saved data already exists?
		if (skipIfExists && File::Exists(saveFilename)) {
			return;
		}

		// And we always skip if the replace file already exists.
		if (File::Exists(filename))
			return;

		// Create subfolder as needed.
#ifdef _WIN32
		size_t slash = hashfile.find_last_of("/\\");
#else
		size_t slash = hashfile.find_last_of("/");
#endif
		if (slash != hashfile.npos) {
			// Create any directory structure as needed.
			const Path saveDirectory = basePath / NEW_TEXTURE_DIR / hashfile.substr(0, slash);
			if (!File::Exists(saveDirectory)) {
				File::CreateFullPath(saveDirectory);
				File::CreateEmptyFile(saveDirectory / ".nomedia");
			}
		}

		png_image png{};
		png.version = PNG_IMAGE_VERSION;
		png.format = PNG_FORMAT_RGBA;
		png.width = w;
		png.height = h;
		bool success = WriteTextureToPNG(&png, saveFilename, 0, rgbaData.data(), pitch, nullptr);
		png_image_free(&png);
		if (png.warning_or_error >= 2) {
			ERROR_LOG(G3D, "Saving screenshot to PNG produced errors.");
		} else if (success) {
			NOTICE_LOG(G3D, "Saving texture for replacement: %08x / %dx%d in '%s'", replacedInfoHash, w, h, saveFilename.ToVisualString().c_str());
		} else {
			ERROR_LOG(G3D, "Failed to write '%s'", saveFilename.c_str());
		}
	}
};

bool TextureReplacer::WillSave(const ReplacedTextureDecodeInfo &replacedInfo) {
	_assert_msg_(enabled_, "Replacement not enabled");
	if (!g_Config.bSaveNewTextures)
		return false;
	// Don't save the PPGe texture.
	if (replacedInfo.addr > 0x05000000 && replacedInfo.addr < PSP_GetKernelMemoryEnd())
		return false;
	if (replacedInfo.isVideo && !allowVideo_)
		return false;

	return true;
}

void TextureReplacer::NotifyTextureDecoded(const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int pitch, int level, int w, int h) {
	_assert_msg_(enabled_, "Replacement not enabled");
	if (!WillSave(replacedInfo)) {
		// Ignore.
		return;
	}
	if (ignoreMipmap_ && level > 0) {
		return;
	}

	u64 cachekey = replacedInfo.cachekey;
	if (ignoreAddress_) {
		cachekey = cachekey & 0xFFFFFFFFULL;
	}

	bool found = false, ignored = false;
	std::string hashfile = LookupHashFile(cachekey, replacedInfo.hash, &found, &ignored);

	// If it's empty, it's an ignored hash, we intentionally don't save.
	if (found) {
		// If it exists, must've been decoded and saved as a new texture already.
		return;
	}

	// Generate a new filename.
	hashfile = HashName(cachekey, replacedInfo.hash, level) + ".png";

	const Path filename = basePath_ / hashfile;

	ReplacementCacheKey replacementKey(cachekey, replacedInfo.hash);
	auto it = savedCache_.find(replacementKey);
	bool skipIfExists = false;
	double now = time_now_d();
	if (it != savedCache_.end()) {
		// We've already saved this texture.  Let's only save if it's bigger (e.g. scaled now.)
		// This check isn't backwards, it's just to check if we should *skip* saving, a bit confusing.
		if (it->second.levelW[level] >= w && it->second.levelH[level] >= h) {
			// If it's been more than 5 seconds, we'll check again.  Maybe they deleted.
			double age = now - it->second.lastTimeSaved;
			if (age < 5.0)
				return;
			skipIfExists = true;
		}
	}

	// Only save the hashed portion of the PNG.
	int lookupW = w / replacedInfo.scaleFactor;
	int lookupH = h / replacedInfo.scaleFactor;
	if (LookupHashRange(replacedInfo.addr, lookupW, lookupH)) {
		w = lookupW * replacedInfo.scaleFactor;
		h = lookupH * replacedInfo.scaleFactor;
	}

	std::vector<u8> saveBuf;

	// Copy data to a buffer so we can send it to the thread. Might as well compact-away the pitch
	// while we're at it.
	saveBuf.resize(w * h * 4);
	for (int y = 0; y < h; y++) {
		memcpy((u8 *)saveBuf.data() + y * w * 4, (const u8 *)data + y * pitch, w * sizeof(u32));
	}
	pitch = w * 4;

	SaveTextureTask *task = new SaveTextureTask(std::move(saveBuf));
	task->w = w;
	task->h = h;
	task->pitch = pitch;
	task->basePath = basePath_;
	task->hashfile = hashfile;
	task->replacedInfoHash = replacedInfo.hash;
	task->skipIfExists = skipIfExists;
	g_threadManager.EnqueueTask(task);  // We don't care about waiting for the task. It'll be fine.

	// Remember that we've saved this for next time.
	// Should be OK that the actual disk write may not be finished yet.
	SavedTextureCacheData &saveData = savedCache_[replacementKey];
	saveData.levelW[level] = w;
	saveData.levelH[level] = h;
	saveData.levelSaved[level] = true;
	saveData.lastTimeSaved = now;
}

void TextureReplacer::Decimate(ReplacerDecimateMode mode) {
	// Allow replacements to be cached for a long time, although they're large.
	double age = 1800.0;
	if (mode == ReplacerDecimateMode::FORCE_PRESSURE) {
		age = 90.0;
	} else if (mode == ReplacerDecimateMode::ALL) {
		age = 0.0;
	} else if (lastTextureCacheSizeGB_ > 1.0) {
		double pressure = std::min(MAX_CACHE_SIZE, lastTextureCacheSizeGB_) / MAX_CACHE_SIZE;
		// Get more aggressive the closer we are to the max.
		age = 90.0 + (1.0 - pressure) * 1710.0;
	}

	const double threshold = time_now_d() - age;
	for (auto &item : cache_) {
		item.second->PurgeIfOlder(threshold);
		// don't actually delete the items here, just clean out the data.
	}

	size_t totalSize = 0;
	for (auto &item : levelCache_) {
		std::lock_guard<std::mutex> guard(item.second.lock);
		totalSize += item.second.data.size();
	}

	double totalSizeGB = totalSize / (1024.0 * 1024.0 * 1024.0);
	if (totalSizeGB >= 1.0) {
		WARN_LOG(G3D, "Decimated replacements older than %fs, currently using %f GB of RAM", age, totalSizeGB);
	}
	lastTextureCacheSizeGB_ = totalSizeGB;
}

template <typename Key, typename Value>
static typename std::unordered_map<Key, Value>::const_iterator LookupWildcard(const std::unordered_map<Key, Value> &map, Key &key, u64 cachekey, u32 hash, bool ignoreAddress) {
	auto alias = map.find(key);
	if (alias != map.end())
		return alias;

	// Also check for a few more aliases with zeroed portions:
	// Only clut hash (very dangerous in theory, in practice not more than missing "just" data hash)
	key.cachekey = cachekey & 0xFFFFFFFFULL;
	key.hash = 0;
	alias = map.find(key);
	if (alias != map.end())
		return alias;

	if (!ignoreAddress) {
		// No data hash.
		key.cachekey = cachekey;
		key.hash = 0;
		alias = map.find(key);
		if (alias != map.end())
			return alias;
	}

	// No address.
	key.cachekey = cachekey & 0xFFFFFFFFULL;
	key.hash = hash;
	alias = map.find(key);
	if (alias != map.end())
		return alias;

	if (!ignoreAddress) {
		// Address, but not clut hash (in case of garbage clut data.)
		key.cachekey = cachekey & ~0xFFFFFFFFULL;
		key.hash = hash;
		alias = map.find(key);
		if (alias != map.end())
			return alias;
	}

	// Anything with this data hash (a little dangerous.)
	key.cachekey = 0;
	key.hash = hash;
	return map.find(key);
}

bool TextureReplacer::FindFiltering(u64 cachekey, u32 hash, TextureFiltering *forceFiltering) {
	if (!Enabled() || !g_Config.bReplaceTextures) {
		return false;
	}

	ReplacementCacheKey replacementKey(cachekey, hash);
	auto filter = LookupWildcard(filtering_, replacementKey, cachekey, hash, ignoreAddress_);
	if (filter == filtering_.end()) {
		// Allow a global wildcard.
		replacementKey.cachekey = 0;
		replacementKey.hash = 0;
		filter = filtering_.find(replacementKey);
	}
	if (filter != filtering_.end()) {
		*forceFiltering = filter->second;
		return true;
	}
	return false;
}

std::string TextureReplacer::LookupHashFile(u64 cachekey, u32 hash, bool *foundReplacement, bool *ignored) {
	ReplacementCacheKey key(cachekey, hash);
	auto alias = LookupWildcard(aliases_, key, cachekey, hash, ignoreAddress_);
	if (alias != aliases_.end()) {
		// Note: this will be blank if explicitly ignored.
		*foundReplacement = true;
		*ignored = alias->second.empty();
		return alias->second;
	}
	*foundReplacement = false;
	*ignored = false;
	return "";
}

std::string TextureReplacer::HashName(u64 cachekey, u32 hash, int level) {
	char hashname[16 + 8 + 1 + 11 + 1] = {};
	if (level > 0) {
		snprintf(hashname, sizeof(hashname), "%016llx%08x_%d", cachekey, hash, level);
	} else {
		snprintf(hashname, sizeof(hashname), "%016llx%08x", cachekey, hash);
	}

	return hashname;
}

bool TextureReplacer::LookupHashRange(u32 addr, int &w, int &h) {
	const u64 rangeKey = ((u64)addr << 32) | ((u64)w << 16) | h;
	auto range = hashranges_.find(rangeKey);
	if (range != hashranges_.end()) {
		const WidthHeightPair &wh = range->second;
		w = wh.first;
		h = wh.second;
		return true;
	}

	return false;
}

float TextureReplacer::LookupReduceHashRange(int& w, int& h) {
	const u64 reducerangeKey = ((u64)w << 16) | h;
	auto range = reducehashranges_.find(reducerangeKey);
	if (range != reducehashranges_.end()) {
		float rhv = range->second;
		return rhv;
	}
	else {
		return reduceHashGlobalValue;
	}
}

class ReplacedTextureTask : public Task {
public:
	ReplacedTextureTask(VFSBackend *vfs, ReplacedTexture &tex, LimitedWaitable *w) : vfs_(vfs), tex_(tex), waitable_(w) {}

	TaskType Type() const override {
		return TaskType::IO_BLOCKING;
	}

	TaskPriority Priority() const override {
		return TaskPriority::NORMAL;
	}

	void Run() override {
		tex_.Prepare(vfs_);
		waitable_->Notify();
	}

private:
	VFSBackend *vfs_;
	ReplacedTexture &tex_;
	LimitedWaitable *waitable_;
};

bool ReplacedTexture::IsReady(double budget) {
	_assert_(vfs_ != nullptr);
	lastUsed_ = time_now_d();
	if (threadWaitable_ && !threadWaitable_->WaitFor(budget)) {
		return false;
	}

	// Loaded already, or not yet on a thread?
	if (initDone_ && levelData_ && !levelData_->data.empty()) {
		// TODO: lock?
		levelData_->lastUsed = lastUsed_;
		return true;
	}

	// Let's not even start a new texture if we're already behind.
	if (budget < 0.0)
		return false;
	if (!prepareDone_)
		return false;

	if (threadWaitable_)
		delete threadWaitable_;
	threadWaitable_ = new LimitedWaitable();
	g_threadManager.EnqueueTask(new ReplacedTextureTask(vfs_, *this, threadWaitable_));
	if (threadWaitable_->WaitFor(budget)) {
		// If we finished all the levels, we're done.
		return initDone_ && levelData_ != nullptr && !levelData_->data.empty();
	}

	// Still pending on thread.
	return false;
}

void ReplacedTexture::Prepare(VFSBackend *vfs) {
	std::unique_lock<std::mutex> lock(mutex_);
	this->vfs_ = vfs;
	if (cancelPrepare_) {
		initDone_ = true;
		return;
	}

	for (int i = 0; i < (int)levels_.size(); ++i) {
		if (cancelPrepare_)
			break;
		PrepareData(i);
	}

	initDone_ = true;
	if (!cancelPrepare_ && threadWaitable_)
		threadWaitable_->Notify();
}

void ReplacedTexture::PrepareData(int level) {
	_assert_msg_((size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(levelData_ != nullptr, "Level cache not set");

	// We must lock around access to levelData_ in case two textures try to load it at once.
	std::lock_guard<std::mutex> guard(levelData_->lock);

	const ReplacedTextureLevel &info = levels_[level];

	if (levelData_->data.size() <= level) {
		levelData_->data.resize(level + 1);
	}

	std::vector<uint8_t> &out = levelData_->data[level];

	// Already populated from cache.
	if (!out.empty())
		return;

	ReplacedImageType imageType;

	size_t fileSize;
	VFSOpenFile *openFile = vfs_->OpenFileForRead(info.fileRef, &fileSize);

	std::string magic;
	imageType = Identify(vfs_, openFile, &magic);

	auto cleanup = [&] {
		vfs_->CloseFile(openFile);
	};

	if (imageType == ReplacedImageType::ZIM) {
		std::unique_ptr<uint8_t[]> zim(new uint8_t[fileSize]);
		if (!zim) {
			ERROR_LOG(G3D, "Failed to allocate memory for texture replacement");
			cleanup();
			return;
		}

		if (vfs_->Read(openFile, &zim[0], fileSize) != fileSize) {
			ERROR_LOG(G3D, "Could not load texture replacement: %s - failed to read ZIM", info.file.c_str());
			cleanup();
			return;
		}

		int w, h, f;
		uint8_t *image;
		if (LoadZIMPtr(&zim[0], fileSize, &w, &h, &f, &image)) {
			if (w > info.w || h > info.h) {
				ERROR_LOG(G3D, "Texture replacement changed since header read: %s", info.file.c_str());
				cleanup();
				return;
			}

			out.resize(info.w * info.h * 4);
			if (w == info.w) {
				memcpy(&out[0], image, info.w * 4 * info.h);
			} else {
				for (int y = 0; y < h; ++y) {
					memcpy(&out[info.w * 4 * y], image + w * 4 * y, w * 4);
				}
			}
			free(image);
		}

		CheckAlphaResult res = CheckAlpha32Rect((u32 *)&out[0], info.w, w, h, 0xFF000000);
		if (res == CHECKALPHA_ANY || level == 0) {
			alphaStatus_ = ReplacedTextureAlpha(res);
		}
	} else if (imageType == ReplacedImageType::PNG) {
		png_image png = {};
		png.version = PNG_IMAGE_VERSION;

		std::string pngdata;
		pngdata.resize(fileSize);
		pngdata.resize(vfs_->Read(openFile, &pngdata[0], fileSize));
		if (!png_image_begin_read_from_memory(&png, &pngdata[0], pngdata.size())) {
			ERROR_LOG(G3D, "Could not load texture replacement info: %s - %s (zip)", info.file.c_str(), png.message);
			cleanup();
			return;
		}
		if (png.width > (uint32_t)info.w || png.height > (uint32_t)info.h) {
			ERROR_LOG(G3D, "Texture replacement changed since header read: %s", info.file.c_str());
			cleanup();
			return;
		}

		bool checkedAlpha = false;
		if ((png.format & PNG_FORMAT_FLAG_ALPHA) == 0) {
			// Well, we know for sure it doesn't have alpha.
			if (level == 0) {
				alphaStatus_ = ReplacedTextureAlpha::FULL;
			}
			checkedAlpha = true;
		}
		png.format = PNG_FORMAT_RGBA;

		out.resize(info.w * info.h * 4);
		if (!png_image_finish_read(&png, nullptr, &out[0], info.w * 4, nullptr)) {
			ERROR_LOG(G3D, "Could not load texture replacement: %s - %s", info.file.c_str(), png.message);
			cleanup();
			out.resize(0);
			return;
		}
		png_image_free(&png);

		if (!checkedAlpha) {
			// This will only check the hashed bits.
			CheckAlphaResult res = CheckAlpha32Rect((u32 *)&out[0], info.w, png.width, png.height, 0xFF000000);
			if (res == CHECKALPHA_ANY || level == 0) {
				alphaStatus_ = ReplacedTextureAlpha(res);
			}
		}
	}

	cleanup();
}

void ReplacedTexture::PurgeIfOlder(double t) {
	if (threadWaitable_ && !threadWaitable_->WaitFor(0.0))
		return;
	if (lastUsed_ >= t)
		return;

	if (levelData_->lastUsed < t) {
		// We have to lock since multiple textures might reference this same data.
		std::lock_guard<std::mutex> guard(levelData_->lock);
		levelData_->data.clear();
		// This means we have to reload.  If we never purge any, there's no need.
		initDone_ = false;
	}
}

ReplacedTexture::~ReplacedTexture() {
	if (threadWaitable_) {
		cancelPrepare_ = true;

		std::unique_lock<std::mutex> lock(mutex_);
		threadWaitable_->WaitAndRelease();
		threadWaitable_ = nullptr;
	}

	for (auto &level : levels_) {
		vfs_->ReleaseFile(level.fileRef);
		level.fileRef = nullptr;
	}
}

bool ReplacedTexture::CopyLevelTo(int level, void *out, int rowPitch) {
	_assert_msg_((size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(out != nullptr && rowPitch > 0, "Invalid out/pitch");

	if (!initDone_) {
		WARN_LOG(G3D, "Init not done yet");
		return false;
	}

	// We probably could avoid this lock, but better to play it safe.
	std::lock_guard<std::mutex> guard(levelData_->lock);

	const ReplacedTextureLevel &info = levels_[level];
	const std::vector<uint8_t> &data = levelData_->data[level];

	if (data.empty()) {
		WARN_LOG(G3D, "Level %d is empty", level);
		return false;
	}

	if (rowPitch < info.w * 4) {
		ERROR_LOG(G3D, "Replacement rowPitch=%d, but w=%d (level=%d)", rowPitch, info.w * 4, level);
		return false;
	}
	_assert_msg_(data.size() == info.w * info.h * 4, "Data has wrong size");

	if (rowPitch == info.w * 4) {
		ParallelMemcpy(&g_threadManager, out, &data[0], info.w * 4 * info.h);
	} else {
		const int MIN_LINES_PER_THREAD = 4;
		ParallelRangeLoop(&g_threadManager, [&](int l, int h) {
			for (int y = l; y < h; ++y) {
				memcpy((uint8_t *)out + rowPitch * y, &data[0] + info.w * 4 * y, info.w * 4);
			}
		}, 0, info.h, MIN_LINES_PER_THREAD);
	}

	return true;
}

bool TextureReplacer::IniExists(const std::string &gameID) {
	if (gameID.empty())
		return false;

	Path texturesDirectory = GetSysDirectory(DIRECTORY_TEXTURES) / gameID;
	Path generatedFilename = texturesDirectory / INI_FILENAME;
	return File::Exists(generatedFilename);
}

bool TextureReplacer::GenerateIni(const std::string &gameID, Path &generatedFilename) {
	if (gameID.empty())
		return false;

	Path texturesDirectory = GetSysDirectory(DIRECTORY_TEXTURES) / gameID;
	if (!File::Exists(texturesDirectory)) {
		File::CreateFullPath(texturesDirectory);
	}

	generatedFilename = texturesDirectory / INI_FILENAME;
	if (File::Exists(generatedFilename))
		return true;

	FILE *f = File::OpenCFile(generatedFilename, "wb");
	if (f) {
		// Unicode byte order mark
		fwrite("\xEF\xBB\xBF", 1, 3, f);

		// Let's also write some defaults.
		fprintf(f, R"(# This file is optional and describes your textures.
# Some information on syntax available here:
# https://github.com/hrydgard/ppsspp/wiki/Texture-replacement-ini-syntax
[options]
version = 1
hash = quick
ignoreMipmap = false

[games]
# Used to make it easier to install, and override settings for other regions.
# Files still have to be copied to each TEXTURES folder.
%s = %s

[hashes]
# Use / for folders not \\, avoid special characters, and stick to lowercase.
# See wiki for more info.

[hashranges]

[filtering]

[reducehashranges]
)", gameID.c_str(), INI_FILENAME.c_str());
		fclose(f);
	}
	return File::Exists(generatedFilename);
}

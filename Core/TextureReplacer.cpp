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
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/Waitable.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/TextureReplacer.h"
#include "Core/ThreadPools.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/Common/TextureDecoder.h"

static const std::string INI_FILENAME = "textures.ini";
static const std::string NEW_TEXTURE_DIR = "new/";
static const int VERSION = 1;
static const int MAX_MIP_LEVELS = 12;  // 12 should be plenty, 8 is the max mip levels supported by the PSP.

TextureReplacer::TextureReplacer() {
	none_.alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
}

TextureReplacer::~TextureReplacer() {
}

void TextureReplacer::Init() {
	NotifyConfigChanged();
}

void TextureReplacer::NotifyConfigChanged() {
	gameID_ = g_paramSFO.GetDiscID();

	enabled_ = g_Config.bReplaceTextures || g_Config.bSaveNewTextures;
	if (enabled_) {
		basePath_ = GetSysDirectory(DIRECTORY_TEXTURES) / gameID_;

		Path newTextureDir = basePath_ / NEW_TEXTURE_DIR;

		// If we're saving, auto-create the directory.
		if (g_Config.bSaveNewTextures && !File::Exists(newTextureDir)) {
			File::CreateFullPath(newTextureDir);
			File::CreateEmptyFile(newTextureDir / ".nomedia");
		}

		enabled_ = File::Exists(basePath_) && File::IsDirectory(basePath_);
	}

	if (enabled_) {
		enabled_ = LoadIni();
	}
}

bool TextureReplacer::LoadIni() {
	// TODO: Use crc32c?
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

	if (File::Exists(basePath_ / INI_FILENAME)) {
		IniFile ini;
		ini.LoadFromVFS((basePath_ / INI_FILENAME).ToString());

		if (!LoadIniValues(ini)) {
			return false;
		}

		// Allow overriding settings per game id.
		std::string overrideFilename;
		if (ini.GetOrCreateSection("games")->Get(gameID_.c_str(), &overrideFilename, "")) {
			if (!overrideFilename.empty() && overrideFilename != INI_FILENAME) {
				INFO_LOG(G3D, "Loading extra texture ini: %s", overrideFilename.c_str());
				IniFile overrideIni;
				overrideIni.LoadFromVFS((basePath_ / overrideFilename).ToString());

				if (!LoadIniValues(overrideIni, true)) {
					return false;
				}
			}
		}
	}

	// The ini doesn't have to exist for it to be valid.
	return true;
}

bool TextureReplacer::LoadIniValues(IniFile &ini, bool isOverride) {
	auto options = ini.GetOrCreateSection("options");
	std::string hash;
	options->Get("hash", &hash, "");
	// TODO: crc32c.
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
		bool checkFilenames = g_Config.bSaveNewTextures && !g_Config.bIgnoreTextureFilenames;
		for (const auto &item : hashes) {
			ReplacementAliasKey key(0, 0, 0);
			if (sscanf(item.first.c_str(), "%16llx%8x_%d", &key.cachekey, &key.hash, &key.level) >= 1) {
				aliases_[key] = item.second;
				if (checkFilenames) {
#if PPSSPP_PLATFORM(WINDOWS)
					// Uppercase probably means the filenames don't match.
					// Avoiding an actual check of the filenames to avoid performance impact.
					filenameWarning = filenameWarning || item.second.find_first_of("\\ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos;
#else
					filenameWarning = filenameWarning || item.second.find_first_of("\\:<>|?*") != std::string::npos;
#endif
				}
			} else {
				ERROR_LOG(G3D, "Unsupported syntax under [hashes]: %s", item.first.c_str());
			}
		}
	}

	if (filenameWarning) {
		auto err = GetI18NCategory("Error");
		host->NotifyUserMessage(err->T("textures.ini filenames may not be cross-platform"), 6.0f);
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

	const u8 *checkp = Memory::GetPointer(addr);
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

ReplacedTexture &TextureReplacer::FindReplacement(u64 cachekey, u32 hash, int w, int h) {
	// Only actually replace if we're replacing.  We might just be saving.
	if (!Enabled() || !g_Config.bReplaceTextures) {
		return none_;
	}

	ReplacementCacheKey replacementKey(cachekey, hash);
	auto it = cache_.find(replacementKey);
	if (it != cache_.end()) {
		return it->second;
	}

	// Okay, let's construct the result.
	ReplacedTexture &result = cache_[replacementKey];
	result.alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
	PopulateReplacement(&result, cachekey, hash, w, h);
	return result;
}

void TextureReplacer::PopulateReplacement(ReplacedTexture *result, u64 cachekey, u32 hash, int w, int h) {
	int newW = w;
	int newH = h;
	LookupHashRange(cachekey >> 32, newW, newH);

	if (ignoreAddress_) {
		cachekey = cachekey & 0xFFFFFFFFULL;
	}

	for (int i = 0; i < MAX_MIP_LEVELS; ++i) {
		const std::string hashfile = LookupHashFile(cachekey, hash, i);
		const Path filename = basePath_ / hashfile;
		if (hashfile.empty() || !File::Exists(filename)) {
			// Out of valid mip levels.  Bail out.
			break;
		}

		ReplacedTextureLevel level;
		level.fmt = ReplacedTextureFormat::F_8888;
		level.file = filename;
		bool good = PopulateLevel(level);

		// We pad files that have been hashrange'd so they are the same texture size.
		level.w = (level.w * w) / newW;
		level.h = (level.h * h) / newH;

		if (good && i != 0) {
			// Check that the mipmap size is correct.  Can't load mips of the wrong size.
			if (level.w != (result->levels_[0].w >> i) || level.h != (result->levels_[0].h >> i)) {
				 WARN_LOG(G3D, "Replacement mipmap invalid: size=%dx%d, expected=%dx%d (level %d, '%s')", level.w, level.h, result->levels_[0].w >> i, result->levels_[0].h >> i, i, filename.c_str());
				 good = false;
			}
		}

		if (good)
			result->levels_.push_back(level);
		// Otherwise, we're done loading mips (bad PNG or bad size, either way.)
		else
			break;
	}

	result->alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
}

enum class ReplacedImageType {
	PNG,
	ZIM,
	INVALID,
};

static ReplacedImageType Identify(FILE *fp) {
	uint8_t magic[4];
	if (fread(magic, 1, 4, fp) != 4)
		return ReplacedImageType::INVALID;
	rewind(fp);

	if (strncmp((const char *)magic, "ZIMG", 4) == 0)
		return ReplacedImageType::ZIM;
	if (magic[0] == 0x89 && strncmp((const char *)&magic[1], "PNG", 3) == 0)
		return ReplacedImageType::PNG;
	return ReplacedImageType::INVALID;
}

bool TextureReplacer::PopulateLevel(ReplacedTextureLevel &level) {
	bool good = false;

	FILE *fp = File::OpenCFile(level.file, "rb");
	auto imageType = Identify(fp);
	if (imageType == ReplacedImageType::ZIM) {
		fseek(fp, 4, SEEK_SET);
		good = fread(&level.w, 4, 1, fp) == 1;
		good = good && fread(&level.h, 4, 1, fp) == 1;
		int flags;
		if (good && fread(&flags, 4, 1, fp) == 1) {
			good = (flags & ZIM_FORMAT_MASK) == ZIM_RGBA8888;
		}
	} else if (imageType == ReplacedImageType::PNG) {
		png_image png = {};
		png.version = PNG_IMAGE_VERSION;
		if (png_image_begin_read_from_stdio(&png, fp)) {
			// We pad files that have been hashrange'd so they are the same texture size.
			level.w = png.width;
			level.h = png.height;
			good = true;
		} else {
			ERROR_LOG(G3D, "Could not load texture replacement info: %s - %s", level.file.ToVisualString().c_str(), png.message);
		}
		png_image_free(&png);
	} else {
		ERROR_LOG(G3D, "Could not load texture replacement info: %s - unsupported format", level.file.ToVisualString().c_str());
	}
	fclose(fp);

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

class TextureSaveTask : public Task {
public:
	// Could probably just use a vector.
	SimpleBuf<u32> data;

	int w = 0;
	int h = 0;
	int pitch = 0;  // bytes

	Path basePath;
	std::string hashfile;
	u32 replacedInfoHash;

	bool skipIfExists = false;

	TextureSaveTask(SimpleBuf<u32> _data) : data(std::move(_data)) {}

	TaskType Type() const override { return TaskType::CPU_COMPUTE; }  // Also I/O blocking but dominated by compute
	void Run() override {
		const Path filename = basePath / hashfile;
		const Path saveFilename = basePath / NEW_TEXTURE_DIR / hashfile;

		// Should we skip writing if the newly saved data already exists?
		// We do this on the thread due to slow IO.
		if (skipIfExists && File::Exists(saveFilename))
			return;

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
		bool success = WriteTextureToPNG(&png, saveFilename, 0, data.data(), pitch, nullptr);
		png_image_free(&png);
		if (png.warning_or_error >= 2) {
			ERROR_LOG(COMMON, "Saving screenshot to PNG produced errors.");
		} else if (success) {
			NOTICE_LOG(G3D, "Saving texture for replacement: %08x / %dx%d", replacedInfoHash, w, h);
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

	std::string hashfile = LookupHashFile(cachekey, replacedInfo.hash, level);
	const Path filename = basePath_ / hashfile;

	// If it's empty, it's an ignored hash, we intentionally don't save.
	if (hashfile.empty()) {
		// If it exists, must've been decoded and saved as a new texture already.
		return;
	}

	ReplacementCacheKey replacementKey(cachekey, replacedInfo.hash);
	auto it = savedCache_.find(replacementKey);
	bool skipIfExists = false;
	double now = time_now_d();
	if (it != savedCache_.end()) {
		// We've already saved this texture.  Let's only save if it's bigger (e.g. scaled now.)
		if (it->second.first.w >= w && it->second.first.h >= h) {
			// If it's been more than 5 seconds, we'll check again.  Maybe they deleted.
			double age = now - it->second.second;
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

	SimpleBuf<u32> saveBuf;

	// Since we're copying, change the format meanwhile.  Not much extra cost.
	if (replacedInfo.fmt != ReplacedTextureFormat::F_8888) {
		saveBuf.resize((pitch * h) / sizeof(u16));
		switch (replacedInfo.fmt) {
		case ReplacedTextureFormat::F_5650:
			ConvertRGB565ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_5551:
			ConvertRGBA5551ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_4444:
			ConvertRGBA4444ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_0565_ABGR:
			ConvertBGR565ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_1555_ABGR:
			ConvertABGR1555ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_4444_ABGR:
			ConvertABGR4444ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_8888_BGRA:
			ConvertBGRA8888ToRGBA8888(saveBuf.data(), (const u32 *)data, (pitch * h) / sizeof(u32));
			break;
		case ReplacedTextureFormat::F_8888:
			// Impossible.  Just so we can get warnings on other missed formats.
			break;
		}

		data = saveBuf.data();
		if (replacedInfo.fmt != ReplacedTextureFormat::F_8888_BGRA) {
			// We doubled our pitch.
			pitch *= 2;
		}
	} else {
		// Copy data to a buffer so we can send it to the thread. Might as well compact-away the pitch
		// while we're at it.
		saveBuf.resize(w * h);
		for (int y = 0; y < h; y++) {
			memcpy((u8 *)saveBuf.data() + y * w * 4, (const u8 *)data + y * pitch, w * sizeof(u32));
		}
		pitch = w * 4;
	}

	TextureSaveTask *task = new TextureSaveTask(std::move(saveBuf));
	// Should probably do a proper move constructor but this'll work.
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
	ReplacedTextureLevel saved;
	saved.fmt = ReplacedTextureFormat::F_8888;
	saved.file = filename;
	saved.w = w;
	saved.h = h;
	savedCache_[replacementKey] = std::make_pair(saved, now);
}

void TextureReplacer::Decimate(bool forcePressure) {
	// Allow replacements to be cached for a long time, although they're large.
	const double age = forcePressure ? 90.0 : 1800.0;
	const double threshold = time_now_d() - age;
	for (auto &item : cache_) {
		item.second.PurgeIfOlder(threshold);
	}
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

std::string TextureReplacer::LookupHashFile(u64 cachekey, u32 hash, int level) {
	ReplacementAliasKey key(cachekey, hash, level);
	auto alias = LookupWildcard(aliases_, key, cachekey, hash, ignoreAddress_);
	if (alias != aliases_.end()) {
		// Note: this will be blank if explicitly ignored.
		return alias->second;
	}

	return HashName(cachekey, hash, level) + ".png";
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
	ReplacedTextureTask(ReplacedTexture &tex, LimitedWaitable *w) : tex_(tex), waitable_(w) {}

	TaskType Type() const override {
		return TaskType::IO_BLOCKING;
	}

	void Run() override {
		tex_.Prepare();
		waitable_->Notify();
	}

private:
	ReplacedTexture &tex_;
	LimitedWaitable *waitable_;
};

bool ReplacedTexture::IsReady(double budget) {
	lastUsed_ = time_now_d();
	if (threadWaitable_ && !threadWaitable_->WaitFor(budget)) {
		return false;
	}

	// Loaded already, or not yet on a thread?
	if (!levelData_.empty())
		return true;
	// Let's not even start a new texture if we're already behind.
	if (budget < 0.0)
		return false;

	if (g_Config.bReplaceTexturesAllowLate) {
		threadWaitable_ = new LimitedWaitable();
		g_threadManager.EnqueueTask(new ReplacedTextureTask(*this, threadWaitable_));

		if (threadWaitable_->WaitFor(budget)) {
			// If we finished all the levels, we're done.
			return !levelData_.empty();
		}
	} else {
		Prepare();
		return true;
	}

	// Still pending on thread.
	return false;
}

void ReplacedTexture::Prepare() {
	std::unique_lock<std::mutex> lock(mutex_);
	if (cancelPrepare_)
		return;

	levelData_.resize(MaxLevel() + 1);
	for (int i = 0; i <= MaxLevel(); ++i) {
		if (cancelPrepare_)
			break;
		PrepareData(i);
	}

	if (!cancelPrepare_ && threadWaitable_)
		threadWaitable_->Notify();
}

void ReplacedTexture::PrepareData(int level) {
	_assert_msg_((size_t)level < levels_.size(), "Invalid miplevel");

	const ReplacedTextureLevel &info = levels_[level];
	std::vector<uint8_t> &out = levelData_[level];

	FILE *fp = File::OpenCFile(info.file, "rb");
	if (!fp) {
		// Leaving the data sized at zero means failure.
		return;
	}

	auto imageType = Identify(fp);
	if (imageType == ReplacedImageType::ZIM) {
		size_t zimSize = File::GetFileSize(fp);
		std::unique_ptr<uint8_t[]> zim(new uint8_t[zimSize]);
		if (!zim) {
			ERROR_LOG(G3D, "Failed to allocate memory for texture replacement");
			fclose(fp);
			return;
		}

		if (fread(&zim[0], 1, zimSize, fp) != zimSize) {
			ERROR_LOG(G3D, "Could not load texture replacement: %s - failed to read ZIM", info.file.c_str());
			fclose(fp);
			return;
		}

		int w, h, f;
		uint8_t *image;

		if (LoadZIMPtr(&zim[0], zimSize, &w, &h, &f, &image)) {
			if (w > info.w || h > info.h) {
				ERROR_LOG(G3D, "Texture replacement changed since header read: %s", info.file.c_str());
				fclose(fp);
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

		if (!png_image_begin_read_from_stdio(&png, fp)) {
			ERROR_LOG(G3D, "Could not load texture replacement info: %s - %s", info.file.c_str(), png.message);
			fclose(fp);
			return;
		}
		if (png.width > (uint32_t)info.w || png.height > (uint32_t)info.h) {
			ERROR_LOG(G3D, "Texture replacement changed since header read: %s", info.file.c_str());
			fclose(fp);
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
			fclose(fp);
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

	fclose(fp);
}

void ReplacedTexture::PurgeIfOlder(double t) {
	if (lastUsed_ < t && (!threadWaitable_ || threadWaitable_->WaitFor(0.0))) {
		levelData_.clear();
	}
}

ReplacedTexture::~ReplacedTexture() {
	if (threadWaitable_) {
		cancelPrepare_ = true;

		std::unique_lock<std::mutex> lock(mutex_);
		threadWaitable_->WaitAndRelease();
		threadWaitable_ = nullptr;
	}
}

bool ReplacedTexture::Load(int level, void *out, int rowPitch) {
	_assert_msg_((size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(out != nullptr && rowPitch > 0, "Invalid out/pitch");

	if (levelData_.empty())
		return false;

	const ReplacedTextureLevel &info = levels_[level];
	const std::vector<uint8_t> &data = levelData_[level];

	if (data.empty())
		return false;
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
		fwrite("\xEF\xBB\xBF", 1, 3, f);

		// Let's also write some defaults.
		fprintf(f, "# This file is optional and describes your textures.\n");
		fprintf(f, "# Some information on syntax available here:\n");
		fprintf(f, "# https://github.com/hrydgard/ppsspp/wiki/Texture-replacement-ini-syntax \n");
		fprintf(f, "[options]\n");
		fprintf(f, "version = 1\n");
		fprintf(f, "hash = quick\n");
		fprintf(f, "ignoreMipmap = false\n");
		fprintf(f, "\n");
		fprintf(f, "[games]\n");
		fprintf(f, "# Used to make it easier to install, and override settings for other regions.\n");
		fprintf(f, "# Files still have to be copied to each TEXTURES folder.");
		fprintf(f, "%s = %s\n", gameID.c_str(), INI_FILENAME.c_str());
		fprintf(f, "\n");
		fprintf(f, "[hashes]\n");
		fprintf(f, "# Use / for folders not \\, avoid special characters, and stick to lowercase.\n");
		fprintf(f, "# See wiki for more info.\n");
		fprintf(f, "\n");
		fprintf(f, "[hashranges]\n");
		fprintf(f, "\n");
		fprintf(f, "[filtering]\n");
		fprintf(f, "\n");
		fprintf(f, "[reducehashranges]\n");
		fclose(f);
	}
	return File::Exists(generatedFilename);
}

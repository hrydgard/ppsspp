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

#ifndef USING_QT_UI
#include <libpng17/png.h>
#endif

#include <algorithm>
#include "ext/xxhash.h"
#include "file/ini_file.h"
#include "Common/ColorConv.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/TextureReplacer.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/Common/TextureDecoder.h"

static const std::string INI_FILENAME = "textures.ini";
static const std::string NEW_TEXTURE_DIR = "new/";
static const int VERSION = 1;
static const int MAX_MIP_LEVELS = 64;

TextureReplacer::TextureReplacer() : enabled_(false), allowVideo_(false), hash_(ReplacedTextureHash::QUICK) {
	none_.alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
}

TextureReplacer::~TextureReplacer() {
}

void TextureReplacer::Init() {
	NotifyConfigChanged();
}

void TextureReplacer::NotifyConfigChanged() {
	gameID_ = g_paramSFO.GetValueString("DISC_ID");

	enabled_ = !gameID_.empty() && (g_Config.bReplaceTextures || g_Config.bSaveNewTextures);
	if (enabled_) {
		basePath_ = GetSysDirectory(DIRECTORY_TEXTURES) + gameID_ + "/";

		// If we're saving, auto-create the directory.
		if (g_Config.bSaveNewTextures && !File::Exists(basePath_ + NEW_TEXTURE_DIR)) {
			File::CreateFullPath(basePath_ + NEW_TEXTURE_DIR);
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

	if (File::Exists(basePath_ + INI_FILENAME)) {
		IniFile ini;
		ini.LoadFromVFS(basePath_ + INI_FILENAME);

		auto options = ini.GetOrCreateSection("options");
		std::string hash;
		options->Get("hash", &hash, "");
		// TODO: crc32c.
		if (strcasecmp(hash.c_str(), "quick") == 0) {
			hash_ = ReplacedTextureHash::QUICK;
		} else {
			ERROR_LOG(G3D, "Unsupported hash type: %s", hash.c_str());
			return false;
		}

		options->Get("video", &allowVideo_, false);

		int version = 0;
		if (options->Get("version", &version, 0) && version > VERSION) {
			ERROR_LOG(G3D, "Unsupported texture replacement version %d, trying anyway", version);
		}

		std::vector<std::string> hashNames;
		if (ini.GetKeys("hashes", hashNames)) {
			auto hashes = ini.GetOrCreateSection("hashes");
			// Format: hashname = filename.png
			for (std::string hashName : hashNames) {
				ReplacementAliasKey key(0, 0, 0);
				if (sscanf(hashName.c_str(), "%16llx%8x_%d", &key.cachekey, &key.hash, &key.level) >= 1) {
					hashes->Get(hashName.c_str(), &aliases_[key], "");
				} else {
					ERROR_LOG(G3D, "Unsupported syntax under [hashes]: %s", hashName.c_str());
				}
			}
		}

		std::vector<std::string> hashrangeKeys;
		if (ini.GetKeys("hashranges", hashrangeKeys)) {
			auto hashranges = ini.GetOrCreateSection("hashranges");
			// Format: addr,w,h = newW,newH
			for (const std::string &key : hashrangeKeys) {
				std::string value;
				if (hashranges->Get(key.c_str(), &value, "")) {
					ParseHashRange(key, value);
				}
			}
		}
	}

	// The ini doesn't have to exist for it to be valid.
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

	const u64 rangeKey = ((u64)addr << 32) | (fromW << 16) | fromH;
	hashranges_[rangeKey] = WidthHeightPair(toW, toH);
}

u32 TextureReplacer::ComputeHash(u32 addr, int bufw, int w, int h, GETextureFormat fmt, u16 maxSeenV) {
	_dbg_assert_msg_(G3D, enabled_, "Replacement not enabled");

	if (!LookupHashRange(addr, w, h)) {
		// There wasn't any hash range, let's fall back to maxSeenV logic.
		if (h == 512 && maxSeenV < 512 && maxSeenV != 0) {
			h = (int)maxSeenV;
		}
	}

	const u8 *checkp = Memory::GetPointer(addr);
	if (bufw <= w) {
		// We can assume the data is contiguous.  These are the total used pixels.
		const u32 totalPixels = bufw * h + (w - bufw);
		const u32 sizeInRAM = (textureBitsPerPixel[fmt] * totalPixels) / 8;

		switch (hash_) {
		case ReplacedTextureHash::QUICK:
			return StableQuickTexHash(checkp, sizeInRAM);
		default:
			return 0;
		}
	} else {
		// We have gaps.  Let's hash each row and sum.
		const u32 bytesPerLine = (textureBitsPerPixel[fmt] * w) / 8;
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

	for (int i = 0; i < MAX_MIP_LEVELS; ++i) {
		const std::string hashfile = LookupHashFile(cachekey, hash, i);
		const std::string filename = basePath_ + hashfile;
		if (hashfile.empty() || !File::Exists(filename)) {
			// Out of valid mip levels.  Bail out.
			break;
		}

		ReplacedTextureLevel level;
		level.fmt = ReplacedTextureFormat::F_8888;
		level.file = filename;

#ifdef USING_QT_UI
		ERROR_LOG(G3D, "Replacement texture loading not implemented for Qt");
#else
		png_image png = {};
		png.version = PNG_IMAGE_VERSION;
		FILE *fp = File::OpenCFile(filename, "rb");
		if (png_image_begin_read_from_stdio(&png, fp)) {
			// We pad files that have been hashrange'd so they are the same texture size.
			level.w = (png.width * w) / newW;
			level.h = (png.height * h) / newH;

			result->levels_.push_back(level);
		} else {
			ERROR_LOG(G3D, "Could not load texture replacement info: %s - %s", filename.c_str(), png.message);
		}
		fclose(fp);

		png_image_free(&png);
#endif
	}

	result->alphaStatus_ = ReplacedTextureAlpha::UNKNOWN;
}

#ifndef USING_QT_UI
static bool WriteTextureToPNG(png_imagep image, const std::string &filename, int convert_to_8bit, const void *buffer, png_int_32 row_stride, const void *colormap) {
	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		ERROR_LOG(COMMON, "Unable to open texture file for writing.");
		return false;
	}

	if (png_image_write_to_stdio(image, fp, convert_to_8bit, buffer, row_stride, colormap)) {
		if (fclose(fp) != 0) {
			ERROR_LOG(COMMON, "Texture file write failed.");
			return false;
		}
		return true;
	} else {
		ERROR_LOG(COMMON, "Texture PNG encode failed.");
		fclose(fp);
		remove(filename.c_str());
		return false;
	}
}
#endif

void TextureReplacer::NotifyTextureDecoded(const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int pitch, int level, int w, int h) {
	_assert_msg_(G3D, enabled_, "Replacement not enabled");
	if (!g_Config.bSaveNewTextures) {
		// Ignore.
		return;
	}
	if (replacedInfo.addr > 0x05000000 && replacedInfo.addr < 0x08800000) {
		// Don't save the PPGe texture.
		return;
	}
	if (replacedInfo.isVideo && !allowVideo_) {
		return;
	}

	std::string hashfile = LookupHashFile(replacedInfo.cachekey, replacedInfo.hash, level);
	const std::string filename = basePath_ + hashfile;
	const std::string saveFilename = basePath_ + NEW_TEXTURE_DIR + hashfile;

	// If it's empty, it's an ignored hash, we intentionally don't save.
	if (hashfile.empty() || File::Exists(filename)) {
		// If it exists, must've been decoded and saved as a new texture already.
		return;
	}

	ReplacementCacheKey replacementKey(replacedInfo.cachekey, replacedInfo.hash);
	auto it = savedCache_.find(replacementKey);
	if (it != savedCache_.end() && File::Exists(saveFilename)) {
		// We've already saved this texture.  Let's only save if it's bigger (e.g. scaled now.)
		if (it->second.w >= w && it->second.h >= h) {
			return;
		}
	}

#ifdef _WIN32
	size_t slash = hashfile.find_last_of("/\\");
#else
	size_t slash = hashfile.find_last_of("/");
#endif
	if (slash != hashfile.npos) {
		// Create any directory structure as needed.
		const std::string saveDirectory = basePath_ + NEW_TEXTURE_DIR + hashfile.substr(0, slash);
		if (!File::Exists(saveDirectory)) {
			File::CreateFullPath(saveDirectory);
		}
	}

	// Only save the hashed portion of the PNG.
	int lookupW = w / replacedInfo.scaleFactor;
	int lookupH = h / replacedInfo.scaleFactor;
	if (LookupHashRange(replacedInfo.addr, lookupW, lookupH)) {
		w = lookupW * replacedInfo.scaleFactor;
		h = lookupH * replacedInfo.scaleFactor;
	}

#ifdef USING_QT_UI
	ERROR_LOG(G3D, "Replacement texture saving not implemented for Qt");
#else
	if (replacedInfo.fmt != ReplacedTextureFormat::F_8888) {
		saveBuf.resize((pitch * h) / sizeof(u16));
		switch (replacedInfo.fmt) {
		case ReplacedTextureFormat::F_5650:
			ConvertRGBA565ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_5551:
			ConvertRGBA5551ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_4444:
			ConvertRGBA4444ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
			break;
		case ReplacedTextureFormat::F_0565_ABGR:
			ConvertABGR565ToRGBA8888(saveBuf.data(), (const u16 *)data, (pitch * h) / sizeof(u16));
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
	}

	png_image png;
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;
	png.format = PNG_FORMAT_RGBA;
	png.width = w;
	png.height = h;
	bool success = WriteTextureToPNG(&png, saveFilename, 0, data, pitch, nullptr);
	png_image_free(&png);

	if (png.warning_or_error >= 2) {
		ERROR_LOG(COMMON, "Saving screenshot to PNG produced errors.");
	} else if (success) {
		NOTICE_LOG(G3D, "Saving texture for replacement: %08x / %dx%d", replacedInfo.hash, w, h);
	}
#endif

	// Remember that we've saved this for next time.
	ReplacedTextureLevel saved;
	saved.fmt = ReplacedTextureFormat::F_8888;
	saved.file = filename;
	saved.w = w;
	saved.h = h;
	savedCache_[replacementKey] = saved;
}

std::string TextureReplacer::LookupHashFile(u64 cachekey, u32 hash, int level) {
	ReplacementAliasKey key(cachekey, hash, level);
	auto alias = aliases_.find(key);
	if (alias == aliases_.end()) {
		// Also check for a few more aliases with zeroed portions:
		// No data hash.
		key.hash = 0;
		alias = aliases_.find(key);

		if (alias == aliases_.end()) {
			// No address.
			key.cachekey = cachekey & 0xFFFFFFFFULL;
			key.hash = hash;
			alias = aliases_.find(key);
		}

		if (alias == aliases_.end()) {
			// Address, but not clut hash (in case of garbage clut data.)
			key.cachekey = cachekey & ~0xFFFFFFFFULL;
			key.hash = hash;
			alias = aliases_.find(key);
		}

		if (alias == aliases_.end()) {
			// Anything with this data hash (a little dangerous.)
			key.cachekey = 0;
			key.hash = hash;
			alias = aliases_.find(key);
		}
	}

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
	const u64 rangeKey = ((u64)addr << 32) | (w << 16) | h;
	auto range = hashranges_.find(rangeKey);
	if (range != hashranges_.end()) {
		const WidthHeightPair &wh = range->second;
		w = wh.first;
		h = wh.second;
		return true;
	}

	return false;
}

void ReplacedTexture::Load(int level, void *out, int rowPitch) {
	_assert_msg_(G3D, (size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(G3D, out != nullptr && rowPitch > 0, "Invalid out/pitch");

	const ReplacedTextureLevel &info = levels_[level];

#ifdef USING_QT_UI
	ERROR_LOG(G3D, "Replacement texture loading not implemented for Qt");
#else
	png_image png = {};
	png.version = PNG_IMAGE_VERSION;

	FILE *fp = File::OpenCFile(info.file, "rb");
	if (!png_image_begin_read_from_stdio(&png, fp)) {
		ERROR_LOG(G3D, "Could not load texture replacement info: %s - %s", info.file.c_str(), png.message);
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

	if (!png_image_finish_read(&png, nullptr, out, rowPitch, nullptr)) {
		ERROR_LOG(G3D, "Could not load texture replacement: %s - %s", info.file.c_str(), png.message);
		return;
	}

	if (!checkedAlpha) {
		// This will only check the hashed bits.
		CheckAlphaResult res = CheckAlphaRGBA8888Basic((u32 *)out, rowPitch / sizeof(u32), png.width, png.height);
		if (res == CHECKALPHA_ANY || level == 0) {
			alphaStatus_ = ReplacedTextureAlpha(res);
		} else if (res == CHECKALPHA_ZERO && alphaStatus_ == ReplacedTextureAlpha::FULL) {
			alphaStatus_ = ReplacedTextureAlpha(res);
		}
	}

	fclose(fp);
	png_image_free(&png);
#endif
}

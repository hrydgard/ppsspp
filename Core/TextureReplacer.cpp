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

#ifdef USING_QT_UI
#include <QtGui/QImage>
#else
#include <libpng17/png.h>
#endif

#include <algorithm>
#include "i18n/i18n.h"
#include "ext/xxhash.h"
#include "file/ini_file.h"
#include "Common/ColorConv.h"
#include "Common/FileUtil.h"
#include "Core/Config.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/TextureReplacer.h"
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
		basePath_ = GetSysDirectory(DIRECTORY_TEXTURES) + gameID_ + "/";

		// If we're saving, auto-create the directory.
		if (g_Config.bSaveNewTextures && !File::Exists(basePath_ + NEW_TEXTURE_DIR)) {
			File::CreateFullPath(basePath_ + NEW_TEXTURE_DIR);
			File::CreateEmptyFile(basePath_ + NEW_TEXTURE_DIR + "/.nomedia");
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
		} else if (strcasecmp(hash.c_str(), "xxh32") == 0) {
			hash_ = ReplacedTextureHash::XXH32;
		} else if (strcasecmp(hash.c_str(), "xxh64") == 0) {
			hash_ = ReplacedTextureHash::XXH64;
		} else {
			ERROR_LOG(G3D, "Unsupported hash type: %s", hash.c_str());
			return false;
		}

		options->Get("video", &allowVideo_, false);
		options->Get("ignoreAddress", &ignoreAddress_, false);
		options->Get("reduceHash", &reduceHash_, false); // Multiplies sizeInRAM/bytesPerLine in XXHASH by 0.5
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
			bool checkFilenames = g_Config.bSaveNewTextures && g_Config.bIgnoreTextureFilenames;
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
			I18NCategory *err = GetI18NCategory("Error");
			host->NotifyUserMessage(err->T("textures.ini filenames may not be cross-platform"), 6.0f);
		}

		if (ini.HasSection("hashranges")) {
			auto hashranges = ini.GetOrCreateSection("hashranges")->ToMap();
			// Format: addr,w,h = newW,newH
			for (const auto &item : hashranges) {
				ParseHashRange(item.first, item.second);
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

	const u64 rangeKey = ((u64)addr << 32) | ((u64)fromW << 16) | fromH;
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
	float reduceHashSize = 1.0;
	if (reduceHash_)
		reduceHashSize = 0.5;
	if (bufw <= w) {
		// We can assume the data is contiguous.  These are the total used pixels.
		const u32 totalPixels = bufw * h + (w - bufw);
		const u32 sizeInRAM = (textureBitsPerPixel[fmt] * totalPixels) / 8 * reduceHashSize;

		switch (hash_) {
		case ReplacedTextureHash::QUICK:
			return StableQuickTexHash(checkp, sizeInRAM);
		case ReplacedTextureHash::XXH32:
			return DoReliableHash32(checkp, sizeInRAM, 0xBACD7814);
		case ReplacedTextureHash::XXH64:
			return DoReliableHash64(checkp, sizeInRAM, 0xBACD7814);
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
				u32 rowHash = DoReliableHash32(checkp, bytesPerLine, 0xBACD7814);
				result = (result * 11) ^ rowHash;
				checkp += stride;
			}
			break;

		case ReplacedTextureHash::XXH64:
			for (int y = 0; y < h; ++y) {
				u32 rowHash = DoReliableHash64(checkp, bytesPerLine, 0xBACD7814);
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
		const std::string filename = basePath_ + hashfile;
		if (hashfile.empty() || !File::Exists(filename)) {
			// Out of valid mip levels.  Bail out.
			break;
		}

		bool good = false;
		ReplacedTextureLevel level;
		level.fmt = ReplacedTextureFormat::F_8888;
		level.file = filename;

#ifdef USING_QT_UI
		QImage image(filename.c_str(), "PNG");
		if (image.isNull()) {
			ERROR_LOG(G3D, "Could not load texture replacement info: %s", filename.c_str());
		} else {
			level.w = (image.width() * w) / newW;
			level.h = (image.height() * h) / newH;
			good = true;
		}
#else
		png_image png = {};
		png.version = PNG_IMAGE_VERSION;
		FILE *fp = File::OpenCFile(filename, "rb");
		if (png_image_begin_read_from_stdio(&png, fp)) {
			// We pad files that have been hashrange'd so they are the same texture size.
			level.w = (png.width * w) / newW;
			level.h = (png.height * h) / newH;
			good = true;
		} else {
			ERROR_LOG(G3D, "Could not load texture replacement info: %s - %s", filename.c_str(), png.message);
		}
		fclose(fp);

		png_image_free(&png);
#endif

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

#ifndef USING_QT_UI
static bool WriteTextureToPNG(png_imagep image, const std::string &filename, int convert_to_8bit, const void *buffer, png_int_32 row_stride, const void *colormap) {
	FILE *fp = File::OpenCFile(filename, "wb");
	if (!fp) {
		ERROR_LOG(SYSTEM, "Unable to open texture file for writing.");
		return false;
	}

	if (png_image_write_to_stdio(image, fp, convert_to_8bit, buffer, row_stride, colormap)) {
		if (fclose(fp) != 0) {
			ERROR_LOG(SYSTEM, "Texture file write failed.");
			return false;
		}
		return true;
	} else {
		ERROR_LOG(SYSTEM, "Texture PNG encode failed.");
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
	if (replacedInfo.addr > 0x05000000 && replacedInfo.addr < PSP_GetKernelMemoryEnd()) {
		// Don't save the PPGe texture.
		return;
	}
	if (replacedInfo.isVideo && !allowVideo_) {
		return;
	}
	u64 cachekey = replacedInfo.cachekey;
	if (ignoreAddress_) {
		cachekey = cachekey & 0xFFFFFFFFULL;
	}

	std::string hashfile = LookupHashFile(cachekey, replacedInfo.hash, level);
	const std::string filename = basePath_ + hashfile;
	const std::string saveFilename = basePath_ + NEW_TEXTURE_DIR + hashfile;

	// If it's empty, it's an ignored hash, we intentionally don't save.
	if (hashfile.empty() || File::Exists(filename)) {
		// If it exists, must've been decoded and saved as a new texture already.
		return;
	}

	ReplacementCacheKey replacementKey(cachekey, replacedInfo.hash);
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
			File::CreateEmptyFile(saveDirectory + "/.nomedia");
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
		// Only clut hash (very dangerous in theory, in practice not more than missing "just" data hash)
		key.cachekey = cachekey & 0xFFFFFFFFULL;
		key.hash = 0;
		alias = aliases_.find(key);

		if (!ignoreAddress_ && alias == aliases_.end()) {
			// No data hash.
			key.cachekey = cachekey;
			key.hash = 0;
			alias = aliases_.find(key);
		}

		if (alias == aliases_.end()) {
			// No address.
			key.cachekey = cachekey & 0xFFFFFFFFULL;
			key.hash = hash;
			alias = aliases_.find(key);
		}

		if (!ignoreAddress_ && alias == aliases_.end()) {
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

void ReplacedTexture::Load(int level, void *out, int rowPitch) {
	_assert_msg_(G3D, (size_t)level < levels_.size(), "Invalid miplevel");
	_assert_msg_(G3D, out != nullptr && rowPitch > 0, "Invalid out/pitch");

	const ReplacedTextureLevel &info = levels_[level];

#ifdef USING_QT_UI
	QImage image(info.file.c_str(), "PNG");
	if (image.isNull()) {
		ERROR_LOG(G3D, "Could not load texture replacement info: %s", info.file.c_str());
		return;
	}

	image = image.convertToFormat(QImage::Format_ARGB32);
	bool alphaFull = true;
	for (int y = 0; y < image.height(); ++y) {
		const QRgb *src = (const QRgb *)image.constScanLine(y);
		uint8_t *outLine = (uint8_t *)out + y * rowPitch;
		for (int x = 0; x < image.width(); ++x) {
			outLine[x * 4 + 0] = qRed(src[x]);
			outLine[x * 4 + 1] = qGreen(src[x]);
			outLine[x * 4 + 2] = qBlue(src[x]);
			outLine[x * 4 + 3] = qAlpha(src[x]);
			// We're already scanning each pixel...
			if (qAlpha(src[x]) != 255) {
				alphaFull = false;
			}
		}
	}

	if (level == 0 || !alphaFull) {
		alphaStatus_ = alphaFull ? ReplacedTextureAlpha::FULL : ReplacedTextureAlpha::UNKNOWN;
	}
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
		}
	}

	fclose(fp);
	png_image_free(&png);
#endif
}

bool TextureReplacer::GenerateIni(const std::string &gameID, std::string *generatedFilename) {
	if (gameID.empty())
		return false;

	std::string texturesDirectory = GetSysDirectory(DIRECTORY_TEXTURES) + gameID + "/";
	if (!File::Exists(texturesDirectory)) {
		File::CreateFullPath(texturesDirectory);
	}

	if (generatedFilename)
		*generatedFilename = texturesDirectory + INI_FILENAME;
	if (File::Exists(texturesDirectory + INI_FILENAME))
		return true;

	FILE *f = File::OpenCFile(texturesDirectory + INI_FILENAME, "wb");
	if (f) {
		fwrite("\xEF\xBB\xBF", 0, 3, f);
		fclose(f);

		// Let's also write some defaults.
		std::fstream fs;
		File::OpenCPPFile(fs, texturesDirectory + INI_FILENAME, std::ios::out | std::ios::ate);
		fs << "# This file is optional and describes your textures.\n";
		fs << "# Some information on syntax available here:\n";
		fs << "# https://github.com/hrydgard/ppsspp/wiki/Texture-replacement-ini-syntax\n";
		fs << "[options]\n";
		fs << "version = 1\n";
		fs << "hash = quick\n";
		fs << "\n";
		fs << "# Use / for folders not \\, avoid special characters, and stick to lowercase.\n";
		fs << "# See wiki for more info.\n";
		fs << "[hashes]\n";
		fs << "\n";
		fs << "[hashranges]\n";
		fs.close();
	}
	return File::Exists(texturesDirectory + INI_FILENAME);
}

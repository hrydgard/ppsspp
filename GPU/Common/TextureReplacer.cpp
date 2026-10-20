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
#include <cstring>
#include <map>
#include <memory>
#include <png.h>

#include "ext/basis_universal/basisu_transcoder.h"
#include "ext/xxhash.h"

#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/File/FileUtil.h"
#include "Common/LogReporting.h"
#include "Common/File/VFS/VFS.h"
#include "Core/Util/PathUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/ELF/ParamSFO.h"
#include "GPU/Common/TextureReplacer.h"
#include "GPU/Common/TextureDecoder.h"

static const std::string INI_FILENAME = "textures.ini";
static const std::string ZIP_FILENAME = "textures.zip";
static const std::string NEW_TEXTURE_DIR = "new/";
static const std::string SHADOW_RANGES_FILENAME = "shadowranges.ini";
static const int VERSION = 1;
static const double MAX_CACHE_SIZE = 4.0;

// basisu only needs to be initialized once, to build some tables. This global is enough to track that.
// Doesn't need to be deinited.
static bool basisu_initialized = false;

static void ScanForHashNamedFiles(VFSBackend *dir, std::map<ReplacementCacheKey, std::map<int, std::string>> *filenameMap);
static void ComputeAliasMap(std::unordered_map<ReplacementCacheKey, std::string> *aliases, const std::map<ReplacementCacheKey, std::map<int, std::string>> &filenameMap);

TextureReplacer::TextureReplacer(Draw::DrawContext *draw) {
	if (!basisu_initialized) {
		basist::basisu_transcoder_init();
		basisu_initialized = true;
	}

	// We don't want to keep the draw object around, so extract the info we need.
	// In tests, draw may be null; formats then just default to unsupported.
	if (draw) {
		if (draw->GetDataFormatSupport(Draw::DataFormat::BC3_UNORM_BLOCK)) formatSupport_.bc123 = true;
		if (draw->GetDataFormatSupport(Draw::DataFormat::ASTC_4x4_UNORM_BLOCK)) formatSupport_.astc = true;
		if (draw->GetDataFormatSupport(Draw::DataFormat::BC7_UNORM_BLOCK)) formatSupport_.bc7 = true;
		if (draw->GetDataFormatSupport(Draw::DataFormat::ETC2_R8G8B8_UNORM_BLOCK)) formatSupport_.etc2 = true;
	}
}

TextureReplacer::~TextureReplacer() {
	// Whatever is still waiting would otherwise never be dumped.
	CommitAllShadows();
	for (const auto &iter : levelCache_) {
		delete iter.second;
	}
	delete vfs_;
}

bool TextureReplacer::LoadPackForTesting(const Path &basePath, std::string *error, bool enableSaving) {
	basePath_ = basePath;
	gameID_ = "";
	replaceEnabled_ = true;
	saveEnabled_ = enableSaving;
	replaceEnabled_ = LoadIni(error, false);
	return replaceEnabled_;
}

void TextureReplacer::NotifyConfigChanged() {
	// Held back dumps belong to the game and settings they were made with, so write them out before those change.
	CommitAllShadows();

	gameID_ = g_paramSFO.GetDiscID();

	bool wasReplaceEnabled = replaceEnabled_;
	replaceEnabled_ = g_Config.bReplaceTextures;
	saveEnabled_ = g_Config.bSaveNewTextures;
	if (replaceEnabled_ || saveEnabled_) {
		basePath_ = GetSysDirectory(DIRECTORY_TEXTURES) / gameID_;
		replaceEnabled_ = replaceEnabled_ && File::IsDirectory(basePath_);
		newTextureDir_ = basePath_ / NEW_TEXTURE_DIR;

		// If we're saving, auto-create the directory.
		if (saveEnabled_ && !File::Exists(newTextureDir_)) {
			INFO_LOG(Log::TexReplacement, "Creating new texture directory: '%s'", newTextureDir_.ToVisualString().c_str());
			File::CreateFullPath(newTextureDir_);
			// We no longer create a nomedia file here, since we put one
			// in the TEXTURES root.
		}
	}

	if (!replaceEnabled_ && wasReplaceEnabled) {
		DeleteVFS();
		Decimate(ReplacerDecimateMode::ALL);
	} else if (!wasReplaceEnabled && replaceEnabled_) {
		std::string error;
		replaceEnabled_ = LoadIni(&error);
		if (!error.empty() && !replaceEnabled_) {
			ERROR_LOG(Log::G3D, "ERROR: %s", error.c_str());
			g_OSD.Show(OSDType::MESSAGE_ERROR, error, 5.0f);
		}
	} else if (saveEnabled_) {
		// Even if just saving is enabled, it makes sense to reload the ini to get the correct
		// settings for saving. See issue #19086. This can be expensive though.
		std::string error;
		bool result = LoadIni(&error, false);
		if (!result) {
			// Ignore errors here, just log if we successfully loaded an ini. But the VFS is gone, so replacement is off.
			replaceEnabled_ = false;
		} else {
			INFO_LOG(Log::G3D, "Loaded INI file for saving.");
		}
	}
}

void TextureReplacer::DeleteVFS() {
	// Everything in levelCache_ uses the VFS, and may have a load task running against it.
	for (auto &repl : levelCache_) {
		repl.second->Unload();
	}
	delete vfs_;
	vfs_ = nullptr;
}

bool TextureReplacer::LoadIni(std::string *error, bool notify) {
	// Pending shadow dumps were keyed with the settings and names of the old ini.
	CommitAllShadows();

	textureHash_ = ReplacedTextureHash::QUICK;
	aliases_.clear();
	hashranges_.clear();
	filtering_.clear();
	reducehashranges_.clear();
	// These hold what the old ini said about each texture, including "no replacement" markers.
	// Only references go, the textures stay in levelCache_.
	cache_.clear();
	savedCache_.clear();

	indexed_.clear();
	ignoreAddress_ = false;
	reduceHash_ = false;
	reduceHashGlobalValue = 0.5;
	// Prevents dumping the mipmaps.
	ignoreMipmap_ = false;
	saveIndexed_ = SaveIndexedMode::OFF;

	shadowEnabled_ = false;
	shadowSettleSeconds_ = 4.0;
	shadowMaxRegions_ = 8;
	shadowRegionAlign_ = 1;
	shadowMemoryBudget_ = (size_t)1024 * 1024 * 1024;
	shadowSlots_.clear();
	shadowRangesDirty_ = false;

	DeleteVFS();

	Path zipPath = basePath_ / ZIP_FILENAME;

	// First, check for textures.zip, which is used to reduce IO.
	VFSBackend *dir = ZipFileReader::Create(zipPath, "", false);
	if (!dir) {
		INFO_LOG(Log::TexReplacement, "%s wasn't a zip file - opening the directory %s instead.", zipPath.c_str(), basePath_.c_str());
		vfsIsZip_ = false;
		dir = new DirectoryReader(basePath_, true);
	} else {
		if (!replaceEnabled_ && saveEnabled_) {
			WARN_LOG(Log::TexReplacement, "Found zip file even though only saving is enabled! This is weird.");
		}
		vfsIsZip_ = true;
	}

	IniFile ini;
	bool iniLoaded = ini.LoadFromVFS(*dir, INI_FILENAME);

	if (iniLoaded) {
		if (!LoadIniValues(ini, dir, false, error)) {
			delete dir;
			return false;
		}

		// Allow overriding settings per game id.
		std::string overrideFilename;
		if (ini.GetOrCreateSection("games")->Get(gameID_.c_str(), &overrideFilename)) {
			if (overrideFilename == "true") {
				// Ignore it
			} else if (HasParentDirComponent(overrideFilename)) {
				// Same reason the [hashes] filenames are checked: for a directory-backed pack,
				// DirectoryReader resolves this against the pack directory, so "../.." reaches
				// anything on disk. Third-party packs are just downloads.
				*error = "Override ini name must stay inside the pack: '" + overrideFilename + "'";
				ERROR_LOG(Log::TexReplacement, "%s", error->c_str());
				delete dir;
				return false;
			} else if (!overrideFilename.empty() && overrideFilename != INI_FILENAME) {
				IniFile overrideIni;
				iniLoaded = overrideIni.LoadFromVFS(*dir, overrideFilename);
				if (!iniLoaded) {
					*error = "Loading override ini failed: '" + overrideFilename + "'";
					ERROR_LOG(Log::TexReplacement, "Failed to load extra texture ini: '%s'", overrideFilename.c_str());
					// Since this error is most likely to occure for texture pack creators, let's just bail here
					// so that the creator is more likely to look in the logs for what happened.
					delete dir;
					return false;
				}

				INFO_LOG(Log::TexReplacement, "Loading extra texture ini: %s", overrideFilename.c_str());
				if (!LoadIniValues(overrideIni, nullptr, true, error)) {
					*error = "Override: " + *error;
					delete dir;
					return false;
				}
			}
		}
	} else {
		if (vfsIsZip_) {
			// We don't accept zip files without inis.
			ERROR_LOG(Log::TexReplacement, "Texture pack lacking ini file: %s", basePath_.c_str());
			*error = "Zip files without ini files will not load";
			delete dir;
			return false;
		} else {
			if (replaceEnabled_) {
				WARN_LOG(Log::TexReplacement, "Texture pack lacking ini file: %s  Proceeding with only hash-named textures in the root.", basePath_.c_str());
			}
			// Do what we can do anyway: Scan for textures and build the map.
			std::map<ReplacementCacheKey, std::map<int, std::string>> filenameMap;
			ScanForHashNamedFiles(dir, &filenameMap);

			if (filenameMap.empty()) {
				WARN_LOG(Log::TexReplacement, "No replacement textures found.");
				delete dir;
				return false;
			}

			ComputeAliasMap(&aliases_, filenameMap);
		}
	}

	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	if (replaceEnabled_ && notify) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, gr->T("Texture replacement pack activated"), 3.0f);
	}

	vfs_ = dir;

	// If we have stuff loaded from before, need to update the vfs pointers to avoid
	// crash on exit. The actual problem is that we tend to call LoadIni a little too much...
	for (auto &repl : levelCache_) {
		repl.second->vfs_ = vfs_;
	}

	if (replaceEnabled_) {
		if (vfsIsZip_) {
			INFO_LOG(Log::TexReplacement, "Texture pack activated from '%s'", (basePath_ / ZIP_FILENAME).c_str());
		} else {
			INFO_LOG(Log::TexReplacement, "Texture pack activated from '%s'", basePath_.c_str());
		}
	}

	if (shadowEnabled_ && saveEnabled_ && !newTextureDir_.empty()) {
		// Regions found in earlier dumping sessions, which may not have been copied to textures.ini yet.
		// Without them, SaveShadowRanges would leave them out of the file.
		IniFile shadowIni;
		if (shadowIni.Load(newTextureDir_ / SHADOW_RANGES_FILENAME)) {
			for (const char *sectionName : { "hashranges", "shadowranges" }) {
				if (shadowIni.HasSection(sectionName)) {
					// Both are addr,w,h = w,h(|w,h...), a hash range just has one region.
					for (const auto &[k, v] : shadowIni.GetOrCreateSection(sectionName)->ToMap()) {
						ParseShadowRange(k, v);
					}
				}
			}
		}
	}

	// The ini doesn't have to exist for the texture directory or zip to be valid.
	return true;
}

static void ScanForHashNamedFiles(VFSBackend *dir, std::map<ReplacementCacheKey, std::map<int, std::string>> *filenameMap) {
	// Scan the root of the texture folder/zip and preinitialize the hash map.
	// TODO: Could put VFSFileReference into the map...
	std::vector<File::FileInfo> filesInRoot;
	dir->GetFileListing("", &filesInRoot, nullptr);
	for (auto file : filesInRoot) {
		if (file.isDirectory)
			continue;
		if (file.name.empty() || file.name[0] == '.')
			continue;
		Path path(file.name);
		std::string ext = path.GetFileExtension();

		std::string hash = file.name.substr(0, file.name.size() - ext.size());
		if (!((hash.size() >= 26 && hash.size() <= 27 && hash[24] == '_') || hash.size() == 24)) {
			continue;
		}
		// OK, it's hash-like enough to try to parse it into the map.
		if (equalsNoCase(ext, ".ktx2") || equalsNoCase(ext, ".png") || equalsNoCase(ext, ".dds") || equalsNoCase(ext, ".zim")) {
			ReplacementCacheKey key(0, 0);
			int level = 0;  // sscanf might fail to pluck the level, but that's ok, we default to 0. sscanf doesn't write to non-matched outputs.
			if (sscanf(hash.c_str(), "%16llx%8x_%d", &key.cachekey, &key.hash, &level) >= 1) {
				// INFO_LOG(Log::TexReplacement, "hash-like file in root, adding: %s", file.name.c_str());
				(*filenameMap)[key][level] = file.name;
			}
		}
	}
}

static void ComputeAliasMap(std::unordered_map<ReplacementCacheKey, std::string> *aliases, const std::map<ReplacementCacheKey, std::map<int, std::string>> &filenameMap) {
	for (auto &pair : filenameMap) {
		std::string alias;
		int mipIndex = 0;
		for (auto &level : pair.second) {
			if (level.first == mipIndex) {
				alias += level.second + "|";
				mipIndex++;
			} else {
				WARN_LOG(Log::TexReplacement, "Non-sequential mip index %d, breaking. filenames=%s", level.first, level.second.c_str());
				break;
			}
		}
		if (alias == "|") {
			alias.clear();  // marker for no replacement
		}
		// Replace any '\' with '/', to be safe and consistent. Since these are from the ini file, we do this on all platforms.
		for (auto &c : alias) {
			if (c == '\\') {
				c = '/';
			}
		}
		(*aliases)[pair.first] = alias;
	}
}

bool TextureReplacer::LoadIniValues(IniFile &ini, VFSBackend *dir, bool isOverride, std::string *error) {
	INFO_LOG(Log::G3D, "Loading ini values...");

	auto options = ini.GetOrCreateSection("options");
	std::string hash;
	if (!options->Get("hash", &hash)) {
		*error = "textures.ini: Hash type not specified";
		return false;
	}
	if (strcasecmp(hash.c_str(), "quick") == 0) {
		textureHash_ = ReplacedTextureHash::QUICK;
	} else if (strcasecmp(hash.c_str(), "xxh32") == 0) {
		textureHash_ = ReplacedTextureHash::XXH32;
	} else if (strcasecmp(hash.c_str(), "xxh64") == 0) {
		textureHash_ = ReplacedTextureHash::XXH64;
	} else if (!isOverride || !hash.empty()) {
		*error = "textures.ini: Unsupported hash type: " + hash;
		return false;
	}

	options->Get("ignoreAddress", &ignoreAddress_);
	// Multiplies sizeInRAM/bytesPerLine in XXHASH by 0.5.
	options->Get("reduceHash", &reduceHash_);
	options->Get("ignoreMipmap", &ignoreMipmap_);
	std::string saveIndexed;
	if (options->Get("saveIndexed", &saveIndexed) && !saveIndexed.empty()) {
		if (!strcasecmp(saveIndexed.c_str(), "all")) {
			saveIndexed_ = SaveIndexedMode::ALL;
		} else if (!strcasecmp(saveIndexed.c_str(), "true") || saveIndexed == "1") {
			saveIndexed_ = SaveIndexedMode::LISTED;
		} else if (!strcasecmp(saveIndexed.c_str(), "false") || saveIndexed == "0") {
			saveIndexed_ = SaveIndexedMode::OFF;
		} else {
			ERROR_LOG(Log::TexReplacement, "Unsupported saveIndexed value: %s (use false, true or all)", saveIndexed.c_str());
			saveIndexed_ = SaveIndexedMode::OFF;
		}
	}
	options->Get("skipLastDXT1Blocks128x64", &skipLastDXT1Blocks128x64_);
	options->Get("skipLastDXT1Blocks128x128", &skipLastDXT1Blocks128x128_);
	if (reduceHash_ && textureHash_ == ReplacedTextureHash::QUICK) {
		reduceHash_ = false;
		ERROR_LOG(Log::TexReplacement, "Texture Replacement: reduceHash option requires safer hash, use xxh32 or xxh64 instead.");
	}

	if (ignoreAddress_ && textureHash_ == ReplacedTextureHash::QUICK) {
		ignoreAddress_ = false;
		ERROR_LOG(Log::TexReplacement, "Texture Replacement: ignoreAddress option requires safer hash, use xxh32 or xxh64 instead.");
	}

	options->Get("shadowTextures", &shadowEnabled_);
	options->Get("shadowSettleSeconds", &shadowSettleSeconds_);
	options->Get("shadowMaxRegions", &shadowMaxRegions_);
	options->Get("shadowRegionAlign", &shadowRegionAlign_);
	int shadowMemoryMB = 0;
	if (options->Get("shadowMemoryMB", &shadowMemoryMB) && shadowMemoryMB > 0) {
		shadowMemoryBudget_ = (size_t)shadowMemoryMB * 1024 * 1024;
	}
	shadowSettleSeconds_ = std::clamp(shadowSettleSeconds_, 0.0, 600.0);
	shadowMaxRegions_ = std::clamp(shadowMaxRegions_, 1, 64);
	shadowRegionAlign_ = std::clamp(shadowRegionAlign_, 1, 64);

	int version = 0;
	if (options->Get("version", &version) && version > VERSION) {
		ERROR_LOG(Log::TexReplacement, "Unsupported texture replacement version %d, trying anyway", version);
	}

	int badFileNameCount = 0;

	std::map<ReplacementCacheKey, std::map<int, std::string>> filenameMap;

	if (dir) {
		ScanForHashNamedFiles(dir, &filenameMap);
	}

	std::string badFilenames;
	constexpr int MaxBadFilenameCount = 5;  // How many to list in the popup

	if (ini.HasSection("hashes")) {
		const Section *hashesSection = ini.GetOrCreateSection("hashes");
		// Format: hashname = filename.png
		const bool checkFilenames = saveEnabled_ && !g_Config.bIgnoreTextureFilenames && !vfsIsZip_;

		for (const auto &line : hashesSection->Lines()) {
			if (line.Key().empty())
				continue;
			ReplacementCacheKey key(0, 0);
			// sscanf might fail to pluck the level if omitted from the line, but that's ok, we default level to 0.
			// sscanf doesn't write to non-matched outputs.
			int level = 0;
			char k[128];
			truncate_cpy(k, line.Key());
			std::string_view v = line.Value();
			if (sscanf(k, "%16llx%8x_%d", &key.cachekey, &key.hash, &level) >= 1) {
				// Reject path traversal: a "../" component could make us read
				// or write files outside the texture pack directory.
				if (HasParentDirComponent(v)) {
					ERROR_LOG(Log::TexReplacement, "Ignoring texture filename with parent dir component: %s", std::string(v).c_str());
					continue;
				}
				// We allow empty filenames, to mark textures that we don't want to keep saving.
				filenameMap[key][level] = v;
				if (checkFilenames) {
					// TODO: We should check for the union of these on all platforms, really.
#if PPSSPP_PLATFORM(WINDOWS)
					bool bad = v.find_first_of("\\ABCDEFGHIJKLMNOPQRSTUVWXYZ:<>|?*") != std::string::npos;
					// Uppercase probably means the filenames don't match.
					// Avoiding an actual check of the filenames to avoid performance impact.
#else
					bool bad = v.find_first_of("\\:<>|?*") != std::string::npos;
#endif
					if (bad) {
						badFileNameCount++;
						if (badFileNameCount == MaxBadFilenameCount) {
							badFilenames.append("...");
						} else if (badFileNameCount < MaxBadFilenameCount) {
							badFilenames.append(v);
							badFilenames.push_back('\n');
						}
					}
				}
			} else {
				ERROR_LOG(Log::TexReplacement, "Unsupported syntax under [hashes], ignoring: %s = ", k);
			}
		}
	}

	// Now, translate the filenameMap to the final aliasMap.
	ComputeAliasMap(&aliases_, filenameMap);

	if (badFileNameCount > 0) {
		auto err = GetI18NCategory(I18NCat::ERRORS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, err->T("textures.ini filenames may not be cross - platform(banned characters)"), badFilenames, 6.0f);
		WARN_LOG(Log::TexReplacement, "Potentially bad filenames: %s", badFilenames.c_str());
	}

	if (ini.HasSection("hashranges")) {
		auto hashranges = ini.GetOrCreateSection("hashranges")->ToMap();
		// Format: addr,w,h = newW,newH
		for (const auto &[k, v] : hashranges) {
			ParseHashRange(k, v);
		}
	}

	if (ini.HasSection("filtering")) {
		auto filters = ini.GetOrCreateSection("filtering")->ToMap();
		// Format: hashname = nearest or linear
		for (const auto &[k, v] : filters) {
			ParseFiltering(k, v);
		}
	}

	if (ini.HasSection("indexed")) {
		auto indexed = ini.GetOrCreateSection("indexed")->ToMap();
		// Format: hashname = true. The image files then hold palette indices, which the
		// game's palette colors, so one image serves every palette of the texture.
		for (const auto &[k, v] : indexed) {
			ParseIndexed(k, v);
		}
	}

	if (ini.HasSection("reducehashranges")) {
		auto reducehashranges = ini.GetOrCreateSection("reducehashranges")->ToMap();
		// Format: w,h = reducehashvalues
		for (const auto &[k, v] : reducehashranges) {
			ParseReduceHashRange(k, v);
		}
	}

	if (ini.HasSection("shadowranges")) {
		auto shadowranges = ini.GetOrCreateSection("shadowranges")->ToMap();
		// Format: addr,w,h = w,h|w,h|...
		for (const auto &[k, v] : shadowranges) {
			ParseShadowRange(k, v);
		}
	}

	// A plain hash range is a shadow range with one region. Shadow dumps of a texture with one region are
	// written as one, so packs made with them also work without shadowTextures.
	for (const auto &[rangeKey, wh] : hashranges_) {
		ShadowSlot &slot = shadowSlots_[rangeKey];
		if (slot.regions.empty()) {
			ShadowRegion region;
			region.w = (u16)wh.first;
			region.h = (u16)wh.second;
			AddShadowRegion(slot, region);
		}
	}

	return true;
}

void TextureReplacer::ParseShadowRange(const std::string &key, const std::string &value) {
	std::vector<std::string> keyParts;
	SplitString(key, ',', keyParts, true);
	std::vector<std::string> regionParts;
	SplitString(value, '|', regionParts, true);

	if (keyParts.size() != 3 || regionParts.empty()) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid shadowrange %s = %s, expecting addr,w,h = w,h|w,h", key.c_str(), value.c_str());
		return;
	}
	if (!startsWith(keyParts[0], "0x") && !startsWith(keyParts[0], "0X")) {
		keyParts[0] = "0x" + keyParts[0];
	}

	u32 addr;
	u32 fromW;
	u32 fromH;
	if (!TryParse(keyParts[0], &addr) || !TryParse(keyParts[1], &fromW) || !TryParse(keyParts[2], &fromH) || fromW > 1024 || fromH > 1024) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid shadowrange %s = %s, key format is 0x12345678,512,512", key.c_str(), value.c_str());
		return;
	}

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(addr, fromW, fromH)];
	for (const std::string &part : regionParts) {
		std::vector<std::string> valueParts;
		SplitString(part, ',', valueParts, true);
		u32 toW;
		u32 toH;
		if (valueParts.size() != 2 || !TryParse(valueParts[0], &toW) || !TryParse(valueParts[1], &toH) || toW == 0 || toH == 0 || toW > fromW || toH > fromH) {
			ERROR_LOG(Log::TexReplacement, "Ignoring invalid region '%s' in shadowrange %s", part.c_str(), key.c_str());
			continue;
		}
		ShadowRegion region;
		region.w = (u16)toW;
		region.h = (u16)toH;
		AddShadowRegion(slot, region);
	}
}

void TextureReplacer::ParseHashRange(const std::string &key, const std::string &value) {
	std::vector<std::string> keyParts;
	SplitString(key, ',', keyParts);
	std::vector<std::string> valueParts;
	SplitString(value, ',', valueParts);

	if (keyParts.size() != 3 || valueParts.size() != 2) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid hashrange %s = %s, expecting addr,w,h = w,h", key.c_str(), value.c_str());
		return;
	}

	// Allow addr not starting with 0x, for consistency. TryParse requires 0x to parse as hex.
	if (!startsWith(keyParts[0], "0x") && !startsWith(keyParts[0], "0X")) {
		keyParts[0] = "0x" + keyParts[0];
	}

	u32 addr;
	u32 fromW;
	u32 fromH;
	if (!TryParse(keyParts[0], &addr) || !TryParse(keyParts[1], &fromW) || !TryParse(keyParts[2], &fromH)) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid hashrange %s = %s, key format is 0x12345678,512,512", key.c_str(), value.c_str());
		return;
	}

	u32 toW;
	u32 toH;
	if (!TryParse(valueParts[0], &toW) || !TryParse(valueParts[1], &toH)) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid hashrange %s = %s, value format is 512,512", key.c_str(), value.c_str());
		return;
	}

	if (toW > fromW || toH > fromH) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid hashrange %s = %s, range bigger than source", key.c_str(), value.c_str());
		return;
	}

	if (toW == 0 || toH == 0) {
		// These end up as desc_.newW/newH, which ReplacedTexture::Prepare divides by.
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid hashrange %s = %s, range is empty", key.c_str(), value.c_str());
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
			ERROR_LOG(Log::TexReplacement, "Unsupported syntax under [filtering]: %s", value.c_str());
		}
	} else {
		ERROR_LOG(Log::TexReplacement, "Unsupported syntax under [filtering]: %s", key.c_str());
	}
}

void TextureReplacer::ParseIndexed(const std::string &key, const std::string &value) {
	ReplacementCacheKey itemKey(0, 0);
	if (sscanf(key.c_str(), "%16llx%8x", &itemKey.cachekey, &itemKey.hash) >= 1) {
		if (!strcasecmp(value.c_str(), "true") || value == "1") {
			indexed_[itemKey] = IndexedMode::CPU;
		} else if (!strcasecmp(value.c_str(), "gpu")) {
			indexed_[itemKey] = IndexedMode::GPU;
		} else if (!strcasecmp(value.c_str(), "false") || value == "0") {
			indexed_[itemKey] = IndexedMode::NONE;
		} else {
			ERROR_LOG(Log::TexReplacement, "Unsupported syntax under [indexed]: %s", value.c_str());
		}
	} else {
		ERROR_LOG(Log::TexReplacement, "Unsupported syntax under [indexed]: %s", key.c_str());
	}
}

void TextureReplacer::ParseReduceHashRange(const std::string& key, const std::string& value) {
	std::vector<std::string> keyParts;
	SplitString(key, ',', keyParts);
	std::vector<std::string> valueParts;
	SplitString(value, ',', valueParts);

	if (keyParts.size() != 2 || valueParts.size() != 1) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid reducehashrange %s = %s, expecting w,h = reducehashvalue", key.c_str(), value.c_str());
		return;
	}

	u32 forW;
	u32 forH;
	if (!TryParse(keyParts[0], &forW) || !TryParse(keyParts[1], &forH)) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid reducehashrange %s = %s, key format is 512,512", key.c_str(), value.c_str());
		return;
	}

	float rhashvalue;
	if (!TryParse(valueParts[0], &rhashvalue)) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid reducehashrange %s = %s, value format is 0.5", key.c_str(), value.c_str());
		return;
	}

	if (rhashvalue == 0) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid reducehashrange %s = %s, reducehashvalue can't be 0", key.c_str(), value.c_str());
		return;
	}

	const u64 reducerangeKey = ((u64)forW << 16) | forH;
	reducehashranges_[reducerangeKey] = rhashvalue;
}

u32 TextureReplacer::ComputeHash(u32 addr, int bufw, int w, int h, bool swizzled, GETextureFormat fmt, u16 maxSeenV) {
	_dbg_assert_msg_(replaceEnabled_ || saveEnabled_, "Replacement not enabled");

	// TODO: Take swizzled into account, like in ComputeTextureHash().
	// Note: Currently, only the MLB games are known to need this.

	if (!LookupHashRange(addr, w, h, &w, &h)) {
		// There wasn't any hash range, let's fall back to maxSeenV logic.
		if (h == 512 && maxSeenV < 512 && maxSeenV != 0) {
			h = (int)maxSeenV;
		}
	}

	return HashTextureData(Memory::GetPointerUnchecked(addr), Memory::MaxSizeAtAddress(addr), bufw, w, h, fmt);
}

u32 TextureReplacer::HashTextureData(const u8 *checkp, u32 availableBytes, int bufw, int w, int h, GETextureFormat fmt) {
	float reduceHashSize = 1.0f;
	if (reduceHash_) {
		reduceHashSize = LookupReduceHashRange(w, h);
		// default to reduceHashGlobalValue which default is 0.5
	}
	// It's a reduction factor and comes from the pack's ini, so don't let it hash more than the texture.
	reduceHashSize = std::clamp(reduceHashSize, 0.0f, 1.0f);

	if (bufw <= w) {
		// We can assume the data is contiguous.  These are the total used pixels.
		const u32 totalPixels = bufw * h + (w - bufw);
		u32 sizeInRAM = (textureBitsPerPixel[fmt] * totalPixels) / 8 * reduceHashSize;

		// Sanity check: Ignore textures that are at the end of RAM.
		if (availableBytes < sizeInRAM) {
			ERROR_LOG(Log::G3D, "Can't hash a %d bytes texture - end point is outside memory", sizeInRAM);
			return 0;
		}

		// Hack for Yu Gi Oh texture hashing problem. See issue #19714
		if (skipLastDXT1Blocks128x64_ && fmt == GE_TFMT_DXT1 && w == 128 && h == 64) {
			// Skip the last few blocks as specified.
			sizeInRAM -= 8 * skipLastDXT1Blocks128x64_;
		}
		if (skipLastDXT1Blocks128x128_ && fmt == GE_TFMT_DXT1 && w == 128 && h == 128) {
			sizeInRAM -= 8 * skipLastDXT1Blocks128x128_;
		}

		switch (textureHash_) {
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

		// Same sanity check as the contiguous path above - the rows are strided, so this is the
		// span we actually touch. Without it, a large h walks far past the end of the region.
		const u32 sizeInRAM = h > 0 ? (h - 1) * stride + bytesPerLine : 0;
		if (availableBytes < sizeInRAM) {
			ERROR_LOG(Log::G3D, "Can't hash a %d bytes texture - end point is outside memory", sizeInRAM);
			return 0;
		}

		u32 result = 0;
		switch (textureHash_) {
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

ReplacedTexture *TextureReplacer::FindReplacement(ReplacementCacheKey replacementKey, int w, int h) {
	return FindReplacement(replacementKey, w, h, 0, 0);
}

ReplacedTexture *TextureReplacer::FindReplacement(ReplacementCacheKey replacementKey, int w, int h, int regionW, int regionH) {
	// Only actually replace if we're replacing.  We might just be saving.
	if (!replaceEnabled_) {
		return nullptr;
	}

	auto it = cache_.find(replacementKey);
	if (it != cache_.end()) {
		return it->second.texture;
	}

	ReplacementDesc desc;
	desc.newW = w;
	desc.newH = h;
	desc.w = w;
	desc.h = h;
	desc.cacheKey = replacementKey;
	desc.forceFiltering = (TextureFiltering)0;  // invalid value

	if (regionW > 0 && regionH > 0 && regionW <= w && regionH <= h) {
		// The key was hashed over this region, so the replacement covers just that.
		desc.newW = regionW;
		desc.newH = regionH;
	} else {
		// Hash ranges are per address even with ignoreAddress, since ComputeHash applies them.
		LookupHashRange(replacementKey.Address(), w, h, &desc.newW, &desc.newH);
	}

	// cache_ stays keyed by the full key. ignoreAddress only affects finding the files.
	ReplacementCacheKey lookupKey = replacementKey;
	if (ignoreAddress_) {
		lookupKey.ZeroAddress();
	}

	bool foundAlias = false;
	bool ignored = false;
	std::string hashfiles = LookupHashFile(lookupKey, &foundAlias, &ignored);

	// Early-out for ignored textures, let's not bother even starting a thread task.
	if (ignored) {
		// WARN_LOG(Log::TexReplacement, "Not found/ignored: %s (%d, %d)", hashfiles.c_str(), (int)foundReplacement, (int)ignored);
		// Insert an entry into the cache for faster lookup next time.
		ReplacedTextureRef ref{};
		cache_.emplace(std::make_pair(replacementKey, ref));
		return nullptr;
	}

	FindFiltering(lookupKey, &desc.forceFiltering);
	const IndexedMode indexedMode = LookupIndexed(lookupKey);
	desc.indexed = indexedMode != IndexedMode::NONE;
	desc.indexedOnGpu = indexedMode == IndexedMode::GPU;

	if (foundAlias) {
		desc.logId = hashfiles;
		SplitString(hashfiles, '|', desc.filenames);
		desc.hashfiles = hashfiles;
	} else {
		DEBUG_LOG(Log::TexReplacement, "Replacement not found in alias list - skipping!");
		// Not found in the alias map.
		//
		// Since we've added aliases generated from the file listing, if we didn't found an alias, that means that
		// we shouldn't replace this texture. Add a marker to cache_.
		ReplacedTextureRef ref{};
		cache_.emplace(std::make_pair(replacementKey, ref));
		return nullptr;
	}

	_dbg_assert_(!hashfiles.empty());
	// OK, we might already have a matching texture. Textures sharing files can still differ in how
	// they're scaled and filtered, so those go in the level cache key too.
	std::string levelKey = StringFromFormat("%s#%dx%d>%dx%d#%d#%d", hashfiles.c_str(), desc.w, desc.h, desc.newW, desc.newH, (int)desc.forceFiltering, (int)indexedMode);
	auto iter = levelCache_.find(levelKey);
	if (iter != levelCache_.end()) {
		// Insert an entry into the cache for faster lookup next time.
		ReplacedTextureRef ref;
		ref.hashfiles = levelKey;
		ref.texture = iter->second;
		cache_.emplace(std::make_pair(replacementKey, ref));
		return iter->second;
	}

	// Final path - we actually need a new replacement texture, because we haven't seen "hashfiles" before.
	desc.basePath = basePath_;
	desc.formatSupport = formatSupport_;

	ReplacedTexture *texture = new ReplacedTexture(vfs_, desc);

	ReplacedTextureRef ref;
	ref.hashfiles = levelKey;
	ref.texture = texture;
	cache_.emplace(std::make_pair(replacementKey, ref));

	levelCache_.emplace(std::make_pair(levelKey, texture));
	return texture;
}

// We save textures on threadpool tasks since it's a fire-and-forget task, and both I/O and png compression
// can be pretty slow.
class SaveTextureTask : public Task {
public:
	// malloc'd
	u8 *rgbaData = nullptr;

	int w = 0;
	int h = 0;

	Path filename;
	Path saveFilename;

	u32 replacedInfoHash = 0;

	// With a palette, rgbaData holds one index per pixel instead.
	bool indexed = false;
	int numColors = 0;
	u32 palette[256]{};

	SaveTextureTask(u8 *_rgbaData) : rgbaData(_rgbaData) {}
	~SaveTextureTask() {
		free(rgbaData);
	}

	// This must be set to I/O blocking because of Android storage (so we attach the thread to JNI), while being CPU heavy too.
	TaskType Type() const override { return TaskType::IO_BLOCKING; }

	TaskPriority Priority() const override {
		return TaskPriority::LOW;
	}

	void Run() override {
		// Should we skip writing if the newly saved data already exists?
		if (File::Exists(saveFilename)) {
			return;
		}

		// And we always skip if the replace file already exists.
		if (File::Exists(filename)) {
			return;
		}

		Path saveDirectory = saveFilename.NavigateUp();
		if (!File::Exists(saveDirectory)) {
			// Previously, we created a .nomedia file here. This is unnecessary as they have recursive behavior.
			// When initializing (see NotifyConfigChange above) we create one in the "root" of the "new" folder.
			File::CreateFullPath(saveDirectory);
		}

		// Now that we've passed the checks, we change the file extension of the path we're actually
		// going to write to to .png.
		saveFilename = saveFilename.WithReplacedExtension(".png");

		bool success = indexed ? pngSaveIndexed(saveFilename, rgbaData, w, h, palette, numColors) : pngSave(saveFilename, rgbaData, w, h, 4);
		if (!success) {
			ERROR_LOG(Log::TexReplacement, "Saving texture to PNG produced errors.");
		} else {
			NOTICE_LOG(Log::TexReplacement, "Saving texture for replacement: %08x / %dx%d in '%s'", replacedInfoHash, w, h, saveFilename.ToVisualString().c_str());
		}
	}
};

bool TextureReplacer::WillSave(const ReplacedTextureDecodeInfo &replacedInfo) const {
	if (!saveEnabled_)
		return false;
	// Don't save the PPGe texture.
	if (replacedInfo.addr > 0x05000000 && replacedInfo.addr < PSP_GetKernelMemoryEnd())
		return false;
	if (replacedInfo.isVideo)
		return false;

	return true;
}

void TextureReplacer::NotifyTextureDecoded(ReplacedTexture *texture, const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int srcPitch, int level, int origW, int origH, int scaledW, int scaledH) {
	_assert_msg_(saveEnabled_, "Texture saving not enabled");
	_assert_(srcPitch >= 0);
	_assert_(data);
	_assert_(level >= 0);

	if (!WillSave(replacedInfo)) {
		// Ignore.
		return;
	}

	if (ignoreMipmap_ && level > 0) {
		// Not saving higher mips.
		return;
	}

	if (shadowEnabled_ && replacedInfo.texW != 0) {
		// Held back until the region of the texture that's used has settled, see ShadowTick.
		CaptureShadowLevel(replacedInfo, data, srcPitch, level, scaledW, scaledH);
		return;
	}

	u64 cachekey = replacedInfo.cachekey;
	if (ignoreAddress_) {
		cachekey = cachekey & 0xFFFFFFFFULL;
	}

	ReplacementCacheKey replacementKey(cachekey, replacedInfo.hash);

	bool foundAlias = false;
	bool ignored = false;
	std::string replacedLevelNames = LookupHashFile(replacementKey, &foundAlias, &ignored);
	if (ignored) {
		// The ini file entry was set to empty string. We can early-out.
		return;
	}

	// Alright, get the specified filename for the level.
	std::string hashfile;
	if (!replacedLevelNames.empty()) {
		// If the user has specified a name before, we get it here.
		std::vector<std::string> names;
		SplitString(replacedLevelNames, '|', names);
		hashfile = names[std::min(level, (int)(names.size() - 1))];
	} else {
		// Generate a new PNG filename, complete with level.
		hashfile = HashName(replacementKey, level) + ".png";
	}

	if (level >= (int)ARRAY_SIZE(SavedTextureCacheData::levelSaved)) {
		return;
	}
	auto it = savedCache_.find(replacementKey);
	if (it != savedCache_.end() && it->second.levelSaved[level]) {
		// We've already saved this level. Ignore it.
		// We don't really care about changing the scale factor during runtime, only confusing.
		return;
	}
	double now = time_now_d();

	// Width/height of the image to save.
	int w = scaledW;
	int h = scaledH;

	if (w == 0 || h == 0) {
		return;
	}

	// Only save the hashed portion of the PNG, in case we have specified it in the ini file.
	int lookupW;
	int lookupH;
	if (LookupHashRange(replacedInfo.addr, origW, origH, &lookupW, &lookupH)) {
		w = lookupW * (scaledW / origW);
		h = lookupH * (scaledH / origH);
	}

	constexpr int bpp = 4;
	size_t saveBufSize = w * h * bpp;
	u8 *saveBuf = (u8 *)malloc(saveBufSize);
	if (!saveBuf) {
		ERROR_LOG(Log::TexReplacement, "Failed to allocated %d bytes of memory for saving a texture", (int)saveBufSize);
		return;
	}

	// Copy data to a buffer so we can send it to the thread. Might as well compact-away the pitch
	// while we're at it.
	for (int y = 0; y < h; y++) {
		memcpy(saveBuf + y * w * bpp, (const u8 *)data + y * srcPitch, w * bpp);
	}

	SaveTextureTask *task = new SaveTextureTask(std::move(saveBuf));

	task->filename = basePath_ / hashfile;
	task->saveFilename = newTextureDir_ / hashfile;

	task->w = w;
	task->h = h;
	task->replacedInfoHash = replacedInfo.hash;
	g_threadManager.EnqueueTask(task);  // We don't care about waiting for the task. It'll be fine.

	// Remember that we've saved this for next time.
	// Should be OK that the actual disk write may not be finished yet.
	SavedTextureCacheData &saveData = savedCache_[replacementKey];
	saveData.levelW[level] = w;
	saveData.levelH[level] = h;
	saveData.levelSaved[level] = true;
	saveData.lastTimeSaved = now;
}

// One image serves every palette of a texture, so the CLUT hash is zeroed (and the address too, if ignored).
ReplacementCacheKey TextureReplacer::IndexedSaveKey(const ReplacedTextureDecodeInfo &replacedInfo) const {
	ReplacementCacheKey key(replacedInfo.cachekey, replacedInfo.hash);
	if (ignoreAddress_) {
		key.ZeroAddress();
	}
	key.ZeroClutHash();
	return key;
}

bool TextureReplacer::WillSaveIndexed(const ReplacedTextureDecodeInfo &replacedInfo) const {
	if (saveIndexed_ == SaveIndexedMode::OFF || !WillSave(replacedInfo)) {
		return false;
	}
	if (saveIndexed_ == SaveIndexedMode::ALL) {
		return true;
	}
	// Only the ones listed in [indexed], found the same way as when replacing.
	ReplacementCacheKey lookupKey(replacedInfo.cachekey, replacedInfo.hash);
	if (ignoreAddress_) {
		lookupKey.ZeroAddress();
	}
	return LookupIndexed(lookupKey) != IndexedMode::NONE;
}

bool TextureReplacer::HasSavedIndexed(const ReplacedTextureDecodeInfo &replacedInfo) const {
	return savedCache_.find(IndexedSaveKey(replacedInfo)) != savedCache_.end();
}

void TextureReplacer::NotifyIndexedTextureDecoded(const ReplacedTextureDecodeInfo &replacedInfo, const u8 *indices, int srcPitch, const u32 *palette, int numColors, int w, int h) {
	_assert_msg_(saveEnabled_, "Texture saving not enabled");
	_assert_(srcPitch >= w);
	_assert_(indices);
	_assert_(numColors >= 1 && numColors <= 256);

	if (!WillSaveIndexed(replacedInfo) || w <= 0 || h <= 0) {
		return;
	}

	if (shadowEnabled_ && replacedInfo.texW != 0) {
		// Held back like the others, see ShadowTick.
		CaptureShadowIndexed(replacedInfo, indices, srcPitch, palette, numColors, w, h);
		return;
	}

	const ReplacementCacheKey saveKey = IndexedSaveKey(replacedInfo);
	if (savedCache_.find(saveKey) != savedCache_.end()) {
		return;
	}

	bool foundAlias = false;
	bool ignored = false;
	std::string replacedLevelNames = LookupHashFile(saveKey, &foundAlias, &ignored);
	if (ignored) {
		// The ini file entry was set to empty string.
		return;
	}

	std::string hashfile;
	if (!replacedLevelNames.empty()) {
		// The user has named the file before, so it goes there (the first level, there's only one).
		std::vector<std::string> names;
		SplitString(replacedLevelNames, '|', names);
		hashfile = names[0];
	} else {
		hashfile = HashName(saveKey, 0) + "_indexed.png";
	}

	// Only save the hashed portion of the PNG, in case we have specified it in the ini file.
	int lookupW;
	int lookupH;
	if (LookupHashRange(replacedInfo.addr, w, h, &lookupW, &lookupH) && lookupW > 0 && lookupH > 0 && lookupW <= w && lookupH <= h) {
		w = lookupW;
		h = lookupH;
	}

	u8 *saveBuf = (u8 *)malloc((size_t)w * h);
	if (!saveBuf) {
		ERROR_LOG(Log::TexReplacement, "Failed to allocated %d bytes of memory for saving a texture", w * h);
		return;
	}
	for (int y = 0; y < h; y++) {
		memcpy(saveBuf + (size_t)y * w, indices + (size_t)y * srcPitch, w);
	}

	SaveTextureTask *task = new SaveTextureTask(saveBuf);
	task->filename = basePath_ / hashfile;
	task->saveFilename = newTextureDir_ / hashfile;
	task->w = w;
	task->h = h;
	task->replacedInfoHash = replacedInfo.hash;
	task->indexed = true;
	task->numColors = numColors;
	memcpy(task->palette, palette, numColors * sizeof(u32));
	g_threadManager.EnqueueTask(task);

	// Remember that we've saved this for next time.
	SavedTextureCacheData &saveData = savedCache_[saveKey];
	saveData.levelW[0] = w;
	saveData.levelH[0] = h;
	saveData.levelSaved[0] = true;
	saveData.lastTimeSaved = time_now_d();
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
	size_t totalSize = 0;
	for (auto &item : levelCache_) {
		// During decimation, it's fine to try-lock here to avoid blocking the main thread while
		// the level is being loaded - in that case we don't want to decimate anyway.
		if (item.second->lock_.try_lock()) {
			item.second->PurgeIfNotUsedSinceTime(threshold);
			totalSize += item.second->GetTotalDataSize();  // TODO: Make something better.
			item.second->lock_.unlock();
		}
		// don't actually delete the items here, just clean out the data.
	}

	double totalSizeGB = totalSize / (1024.0 * 1024.0 * 1024.0);
	if (totalSizeGB >= 1.0) {
		WARN_LOG(Log::TexReplacement, "Decimated replacements older than %fs, currently using %f GB of RAM", age, totalSizeGB);
	}
	lastTextureCacheSizeGB_ = totalSizeGB;
}

template <typename Value>
static typename std::unordered_map<ReplacementCacheKey, Value>::const_iterator LookupWildcard(const std::unordered_map<ReplacementCacheKey, Value> &map, ReplacementCacheKey key, bool ignoreAddress) {
	u64 cachekey = key.CacheKey();
	u32 hash = key.ContentsHash();

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

	if (!ignoreAddress) {
		// Address Only.
		key.cachekey = cachekey & ~0xFFFFFFFFULL;
		key.hash = 0;
		alias = map.find(key);
		if (alias != map.end())
			return alias;
	}

	// Anything with this data hash (a little dangerous.)
	key.cachekey = 0;
	key.hash = hash;
	return map.find(key);
}

bool TextureReplacer::FindFiltering(ReplacementCacheKey replacementKey, TextureFiltering *forceFiltering) {
	if (!replaceEnabled_) {
		return false;
	}

	auto filter = LookupWildcard(filtering_, replacementKey, ignoreAddress_);
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

TextureReplacer::IndexedMode TextureReplacer::LookupIndexed(ReplacementCacheKey key) const {
	auto found = LookupWildcard(indexed_, key, ignoreAddress_);
	return found != indexed_.end() ? found->second : IndexedMode::NONE;
}

std::string TextureReplacer::LookupHashFile(ReplacementCacheKey key, bool *foundAlias, bool *ignored) {
	auto alias = LookupWildcard(aliases_, key, ignoreAddress_);
	if (alias != aliases_.end()) {
		// Note: this will be blank if explicitly ignored.
		*foundAlias = true;
		*ignored = alias->second.empty();
		return alias->second;
	}
	*foundAlias = false;
	*ignored = false;
	return "";
}

std::string TextureReplacer::HashName(ReplacementCacheKey key, int level) {
	char hashname[16 + 8 + 1 + 11 + 1] = {};
	if (level > 0) {
		snprintf(hashname, sizeof(hashname), "%016llx%08x_%d", key.cachekey, key.hash, level);
	} else {
		snprintf(hashname, sizeof(hashname), "%016llx%08x", key.cachekey, key.hash);
	}

	return hashname;
}

bool TextureReplacer::LookupHashRange(u32 addr, int w, int h, int *newW, int *newH) {
	const u64 rangeKey = ((u64)addr << 32) | ((u64)w << 16) | h;
	auto range = hashranges_.find(rangeKey);
	if (range != hashranges_.end()) {
		const WidthHeightPair &wh = range->second;
		*newW = wh.first;
		*newH = wh.second;
		return true;
	} else {
		*newW = w;
		*newH = h;
		return false;
	}
}

float TextureReplacer::LookupReduceHashRange(int w, int h) {
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

// Shadow textures.
//
// A texture declared as 512x512 often only has an image in its top part, with whatever the game's allocator put
// after it below. Hashing all of it gives a new key, and a new dump, each time that other data changes. Here the
// key only covers what draws sample (known in through mode, from the vertex UVs), and dumps wait in RAM until
// that part stops growing. Each waiting dump keeps a copy of the texture's memory, so it can be hashed again
// with its final region, and contents that turn out to be the same there are only written once.

TextureReplacer::ShadowRegion TextureReplacer::NormalizeShadowRegion(const ShadowBounds &bounds, int w, int h) const {
	ShadowRegion region;
	region.w = (u16)w;
	region.h = (u16)h;
	if (!bounds.known || bounds.maxU == 0 || bounds.maxV == 0) {
		return region;
	}

	// Beyond the edge the texture wraps or clamps, which can show any part of it.
	int rw = bounds.maxU > w ? w : bounds.maxU;
	int rh = bounds.maxV > h ? h : bounds.maxV;
	if (shadowRegionAlign_ > 1) {
		rw = std::min(w, (rw + shadowRegionAlign_ - 1) / shadowRegionAlign_ * shadowRegionAlign_);
		rh = std::min(h, (rh + shadowRegionAlign_ - 1) / shadowRegionAlign_ * shadowRegionAlign_);
	}
	region.w = (u16)std::max(rw, 1);
	region.h = (u16)std::max(rh, 1);
	return region;
}

// Swizzled and DXT textures are stored in blocks, so only whole rows of blocks can be left out.
static void AdjustShadowRegionForLayout(u16 *rw, u16 *rh, int w, int h, bool swizzled, GETextureFormat fmt) {
	if (fmt == GE_TFMT_DXT1 || fmt == GE_TFMT_DXT3 || fmt == GE_TFMT_DXT5) {
		*rw = (u16)w;
		*rh = (u16)std::min(h, (*rh + 3) & ~3);
	} else if (swizzled) {
		// Blocks are 16 bytes wide and 8 rows tall.
		*rw = (u16)w;
		*rh = (u16)std::min(h, (*rh + 7) & ~7);
	}
}

void TextureReplacer::AddShadowRegion(ShadowSlot &slot, ShadowRegion region) {
	for (const ShadowRegion &r : slot.regions) {
		if (r == region) {
			return;
		}
	}
	slot.regions.push_back(region);
	// Largest first, so a content that was dumped with more of the texture wins over a part of it.
	std::sort(slot.regions.begin(), slot.regions.end(), [](const ShadowRegion &a, const ShadowRegion &b) {
		if (a.Area() != b.Area()) {
			return a.Area() > b.Area();
		}
		return a.h > b.h;
	});
}

ReplacementCacheKey TextureReplacer::NamingKey(u64 cachekey, u32 hash) const {
	ReplacementCacheKey key(cachekey, hash);
	if (ignoreAddress_) {
		key.ZeroAddress();
	}
	return key;
}

bool TextureReplacer::ShadowKeyKnown(ReplacementCacheKey key) {
	bool foundAlias = false;
	bool ignored = false;
	LookupHashFile(key, &foundAlias, &ignored);
	if (foundAlias || ignored) {
		return true;
	}
	auto it = savedCache_.find(key);
	if (it != savedCache_.end() && it->second.levelSaved[0]) {
		return true;
	}
	// Palette dumps serve every palette, so they're saved without the CLUT hash.
	key.ZeroClutHash();
	it = savedCache_.find(key);
	return it != savedCache_.end() && it->second.levelSaved[0];
}

std::unordered_map<ReplacementCacheKey, TextureReplacer::ShadowVariant>::iterator TextureReplacer::FindShadowVariant(u64 cachekey, u32 hash) {
	ReplacementCacheKey key(cachekey, hash);
	auto it = shadowVariants_.find(key);
	if (it != shadowVariants_.end()) {
		return it;
	}
	// A palette capture, shared by all palettes.
	key.ZeroClutHash();
	return shadowVariants_.find(key);
}

u32 TextureReplacer::ComputeShadowHash(const ShadowHashParams &p, u16 *regionW, u16 *regionH) {
	_dbg_assert_(ShadowEnabled());

	const u8 *checkp = Memory::GetPointerUnchecked(p.addr);
	const u32 available = Memory::MaxSizeAtAddress(p.addr);

	ShadowRegion seen = NormalizeShadowRegion(p.seen, p.w, p.h);
	AdjustShadowRegionForLayout(&seen.w, &seen.h, p.w, p.h, p.swizzled, p.fmt);

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(p.addr, p.w, p.h)];
	if (!p.seen.known) {
		slot.unbounded = true;
	} else {
		slot.learned.w = std::max(slot.learned.w, seen.w);
		slot.learned.h = std::max(slot.learned.h, seen.h);
	}

	ShadowRegion full;
	full.w = (u16)p.w;
	full.h = (u16)p.h;

	// Try the regions that dumps of this texture were made with, largest first. The whole texture is always
	// one of them, since that's what dumps of contents drawn without known UVs use. A region has to cover what
	// draws sample, otherwise the replacement would leave out what's drawn.
	std::vector<ShadowRegion> candidates = slot.regions;
	if (std::find(candidates.begin(), candidates.end(), full) == candidates.end()) {
		// Nothing is bigger than the whole texture.
		candidates.insert(candidates.begin(), full);
	}
	for (const ShadowRegion &region : candidates) {
		// A texel or two of slack, for draws that reach just past the edge of the image with bilinear filtering.
		constexpr int slack = 2;
		if (p.seen.known && (region.w + slack < seen.w || region.h + slack < seen.h)) {
			continue;
		}
		const u32 hash = HashTextureData(checkp, available, p.bufw, region.w, region.h, p.fmt);
		if (ShadowKeyKnown(NamingKey(p.cachekey, hash))) {
			*regionW = region.w;
			*regionH = region.h;
			return hash;
		}
	}

	// Nothing was made for this content yet. Key it by everything drawn from this texture so far, so data
	// outside that doesn't change the key. The dump picks its final region when it's written.
	ShadowRegion provisional = full;
	if (!slot.unbounded && slot.learned.w != 0 && slot.learned.h != 0) {
		provisional = slot.learned;
	}
	*regionW = provisional.w;
	*regionH = provisional.h;
	return HashTextureData(checkp, available, p.bufw, provisional.w, provisional.h, p.fmt);
}

void TextureReplacer::NotifyShadowDraw(u64 cachekey, u32 hash, u32 addr, int w, int h, bool swizzled, GETextureFormat fmt, const ShadowBounds &bounds) {
	ShadowRegion region = NormalizeShadowRegion(bounds, w, h);
	AdjustShadowRegionForLayout(&region.w, &region.h, w, h, swizzled, fmt);

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(addr, w, h)];
	if (!bounds.known) {
		slot.unbounded = true;
	} else {
		slot.learned.w = std::max(slot.learned.w, region.w);
		slot.learned.h = std::max(slot.learned.h, region.h);
	}

	if (!saveEnabled_) {
		return;
	}
	auto it = FindShadowVariant(cachekey, hash);
	if (it == shadowVariants_.end()) {
		return;
	}

	ShadowVariant &variant = it->second;
	variant.drawn = true;
	if (!bounds.known) {
		if (!variant.unbounded) {
			variant.unbounded = true;
			variant.lastChange = time_now_d();
		}
	} else if (!variant.seen.Covers(region)) {
		variant.seen.w = std::max(variant.seen.w, region.w);
		variant.seen.h = std::max(variant.seen.h, region.h);
		variant.lastChange = time_now_d();
	}
}

void TextureReplacer::NotifyShadowRekeyed(u64 cachekey, u32 oldHash, int w, int h, bool swizzled, GETextureFormat fmt, const ShadowBounds &seen) {
	if (!saveEnabled_) {
		return;
	}

	ShadowRekey rekey;
	rekey.valid = true;
	rekey.cachekey = cachekey;
	rekey.unbounded = !seen.known;
	if (seen.known) {
		rekey.seen = NormalizeShadowRegion(seen, w, h);
		AdjustShadowRegionForLayout(&rekey.seen.w, &rekey.seen.h, w, h, swizzled, fmt);
	}

	auto it = FindShadowVariant(cachekey, oldHash);
	if (it != shadowVariants_.end()) {
		const ShadowVariant &old = it->second;
		rekey.unbounded = rekey.unbounded || old.unbounded;
		rekey.seen.w = std::max(rekey.seen.w, old.seen.w);
		rekey.seen.h = std::max(rekey.seen.h, old.seen.h);
		shadowMemoryUsed_ -= std::min(shadowMemoryUsed_, old.memoryUsed);
		shadowVariants_.erase(it);
	}
	shadowRekey_ = rekey;
}

void TextureReplacer::CaptureShadowLevel(const ReplacedTextureDecodeInfo &replacedInfo, const void *data, int srcPitch, int level, int scaledW, int scaledH) {
	if (level >= (int)ARRAY_SIZE(ShadowVariant::levels) || scaledW <= 0 || scaledH <= 0) {
		return;
	}

	// Contents that were already dumped come back with the key of their dump, see ComputeShadowHash.
	const ReplacementCacheKey namingKey = NamingKey(replacedInfo.cachekey, replacedInfo.hash);
	auto saved = savedCache_.find(namingKey);
	if (saved != savedCache_.end() && saved->second.levelSaved[0]) {
		return;
	}
	bool foundAlias = false;
	bool ignored = false;
	LookupHashFile(namingKey, &foundAlias, &ignored);
	if (ignored || foundAlias) {
		// Ignored, or the pack already has it.
		return;
	}

	const ReplacementCacheKey variantKey(replacedInfo.cachekey, replacedInfo.hash);
	auto it = shadowVariants_.find(variantKey);
	if (it == shadowVariants_.end()) {
		if (level != 0 || !CreateShadowVariant(replacedInfo, variantKey)) {
			return;
		}
		it = shadowVariants_.find(variantKey);
	}

	ShadowVariant &variant = it->second;
	ShadowLevel &dst = variant.levels[level];
	if (!dst.rgba.empty()) {
		return;
	}

	constexpr int bpp = 4;
	dst.w = scaledW;
	dst.h = scaledH;
	dst.rgba.resize((size_t)scaledW * scaledH * bpp);
	for (int y = 0; y < scaledH; y++) {
		memcpy(dst.rgba.data() + (size_t)y * scaledW * bpp, (const u8 *)data + (size_t)y * srcPitch, (size_t)scaledW * bpp);
	}
	variant.numLevels = std::max(variant.numLevels, level + 1);
	variant.memoryUsed += dst.rgba.size();
	shadowMemoryUsed_ += dst.rgba.size();

	EnforceShadowBudget(variantKey);
}

void TextureReplacer::CaptureShadowIndexed(const ReplacedTextureDecodeInfo &replacedInfo, const u8 *indices, int srcPitch, const u32 *palette, int numColors, int w, int h) {
	if (w != replacedInfo.texW || h != replacedInfo.texH) {
		// Not level 0 of the texture that was hashed (fake mipmaps), the copy wouldn't match.
		return;
	}

	// One capture for all palettes: the indices and the hash don't depend on the CLUT.
	ReplacementCacheKey variantKey(replacedInfo.cachekey, replacedInfo.hash);
	variantKey.ZeroClutHash();
	ReplacementCacheKey namingKey = NamingKey(variantKey.cachekey, variantKey.hash);
	auto saved = savedCache_.find(namingKey);
	if (saved != savedCache_.end() && saved->second.levelSaved[0]) {
		return;
	}
	bool foundAlias = false;
	bool ignored = false;
	LookupHashFile(NamingKey(replacedInfo.cachekey, replacedInfo.hash), &foundAlias, &ignored);
	if (ignored || foundAlias) {
		return;
	}
	if (shadowVariants_.find(variantKey) != shadowVariants_.end()) {
		// Already have the indices, a different palette changes nothing.
		return;
	}

	ShadowVariant *variant = CreateShadowVariant(replacedInfo, variantKey);
	if (!variant) {
		return;
	}
	variant->indexed = true;
	variant->cachekey = variantKey.cachekey;
	variant->indices.resize((size_t)w * h);
	for (int y = 0; y < h; y++) {
		memcpy(variant->indices.data() + (size_t)y * w, indices + (size_t)y * srcPitch, w);
	}
	variant->numColors = numColors;
	memcpy(variant->palette, palette, numColors * sizeof(u32));
	variant->memoryUsed += variant->indices.size();
	shadowMemoryUsed_ += variant->indices.size();

	EnforceShadowBudget(variantKey);
}

TextureReplacer::ShadowVariant *TextureReplacer::CreateShadowVariant(const ReplacedTextureDecodeInfo &replacedInfo, ReplacementCacheKey variantKey) {
	if (!Memory::IsValidAddress(replacedInfo.addr)) {
		return nullptr;
	}

	// Enough to hash the whole texture again, with any region.
	const int texW = replacedInfo.texW;
	const int texH = replacedInfo.texH;
	const int rows = replacedInfo.swizzled ? ((texH + 7) & ~7) : texH;
	u64 rawSize = ((u64)textureBitsPerPixel[replacedInfo.texFormat] * std::max((int)replacedInfo.bufw, texW) * rows) / 8;
	rawSize = std::min(rawSize, (u64)Memory::MaxSizeAtAddress(replacedInfo.addr));
	if (rawSize == 0) {
		return nullptr;
	}

	ShadowVariant variant;
	variant.cachekey = replacedInfo.cachekey;
	variant.addr = replacedInfo.addr;
	variant.bufw = replacedInfo.bufw;
	variant.texW = texW;
	variant.texH = texH;
	variant.swizzled = replacedInfo.swizzled;
	variant.fmt = replacedInfo.texFormat;
	const u8 *src = Memory::GetPointerUnchecked(replacedInfo.addr);
	variant.raw.assign(src, src + rawSize);
	variant.created = time_now_d();
	variant.lastChange = variant.created;
	variant.memoryUsed = variant.raw.size();
	// The rekey carry is matched on the texture, whatever its palette.
	if (shadowRekey_.valid && (shadowRekey_.cachekey & ~0xFFFFFFFFULL) == (variant.cachekey & ~0xFFFFFFFFULL)) {
		// The same content under its new key, see NotifyShadowRekeyed.
		variant.drawn = true;
		variant.unbounded = shadowRekey_.unbounded;
		variant.seen = shadowRekey_.seen;
	}
	shadowRekey_.valid = false;
	shadowMemoryUsed_ += variant.memoryUsed;
	return &shadowVariants_.emplace(variantKey, std::move(variant)).first->second;
}

void TextureReplacer::EnforceShadowBudget(ReplacementCacheKey keep) {
	// Over the budget, the oldest ones are written now, with the region they have so far.
	while (shadowMemoryUsed_ > shadowMemoryBudget_ && shadowVariants_.size() > 1) {
		auto oldest = shadowVariants_.end();
		for (auto iter = shadowVariants_.begin(); iter != shadowVariants_.end(); ++iter) {
			if (iter->first == keep) {
				continue;
			}
			if (oldest == shadowVariants_.end() || iter->second.created < oldest->second.created) {
				oldest = iter;
			}
		}
		if (oldest == shadowVariants_.end()) {
			break;
		}
		WARN_LOG_N_TIMES(shadowbudget, 5, Log::TexReplacement, "Shadow textures: over the memory budget, dumping early");
		CommitShadow(oldest->second);
		shadowVariants_.erase(oldest);
	}
}

void TextureReplacer::CommitShadow(ShadowVariant &variant) {
	shadowMemoryUsed_ -= std::min(shadowMemoryUsed_, variant.memoryUsed);
	if (variant.indexed ? variant.indices.empty() : (variant.numLevels == 0 || variant.levels[0].rgba.empty())) {
		return;
	}

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(variant.addr, variant.texW, variant.texH)];

	ShadowRegion full;
	full.w = (u16)variant.texW;
	full.h = (u16)variant.texH;
	ShadowRegion region = full;
	if (variant.drawn && !variant.unbounded && variant.seen.w != 0 && variant.seen.h != 0) {
		region = variant.seen;
	}

	// Each region is one more hash to try for every new content of this texture, so there's a limit.
	const bool knownRegion = region == full || std::find(slot.regions.begin(), slot.regions.end(), region) != slot.regions.end();
	if (!knownRegion && (int)slot.regions.size() >= shadowMaxRegions_) {
		const ShadowRegion *best = nullptr;
		for (const ShadowRegion &r : slot.regions) {
			if (r.Covers(region) && (!best || r.Area() < best->Area())) {
				best = &r;
			}
		}
		region = best ? *best : full;
	}

	const u32 hash = HashTextureData(variant.raw.data(), (u32)variant.raw.size(), variant.bufw, region.w, region.h, variant.fmt);
	if (variant.indexed) {
		CommitShadowIndexed(variant, region, hash);
		return;
	}
	const ReplacementCacheKey namingKey = NamingKey(variant.cachekey, hash);

	bool foundAlias = false;
	bool ignored = false;
	std::string replacedLevelNames = LookupHashFile(namingKey, &foundAlias, &ignored);
	if (ignored) {
		return;
	}

	// This is where duplicates go away: contents that differed only outside the region end up here with the same key.
	SavedTextureCacheData &saveData = savedCache_[namingKey];
	if (saveData.levelSaved[0]) {
		return;
	}

	std::vector<std::string> names;
	if (!replacedLevelNames.empty()) {
		SplitString(replacedLevelNames, '|', names);
	}

	for (int level = 0; level < variant.numLevels; level++) {
		const ShadowLevel &src = variant.levels[level];
		if (src.rgba.empty()) {
			// Mip levels have to be consecutive.
			break;
		}

		// The level is saved scaled, so the region is scaled by the same amount. ReplacedTexture undoes this.
		const int saveW = std::clamp((int)((u64)region.w * src.w / variant.texW), 1, src.w);
		const int saveH = std::clamp((int)((u64)region.h * src.h / variant.texH), 1, src.h);

		constexpr int bpp = 4;
		u8 *saveBuf = (u8 *)malloc((size_t)saveW * saveH * bpp);
		if (!saveBuf) {
			ERROR_LOG(Log::TexReplacement, "Failed to allocate %d bytes of memory for saving a texture", saveW * saveH * bpp);
			break;
		}
		for (int y = 0; y < saveH; y++) {
			memcpy(saveBuf + (size_t)y * saveW * bpp, src.rgba.data() + (size_t)y * src.w * bpp, (size_t)saveW * bpp);
		}

		std::string hashfile;
		if (!names.empty()) {
			hashfile = names[std::min(level, (int)names.size() - 1)];
		} else {
			hashfile = HashName(namingKey, level) + ".png";
		}

		SaveTextureTask *task = new SaveTextureTask(saveBuf);
		task->filename = basePath_ / hashfile;
		task->saveFilename = newTextureDir_ / hashfile;
		task->w = saveW;
		task->h = saveH;
		task->replacedInfoHash = hash;
		g_threadManager.EnqueueTask(task);

		saveData.levelW[level] = saveW;
		saveData.levelH[level] = saveH;
		saveData.levelSaved[level] = true;
	}
	saveData.lastTimeSaved = time_now_d();

	const size_t numRegions = slot.regions.size();
	AddShadowRegion(slot, region);
	if (slot.regions.size() != numRegions) {
		shadowRangesDirty_ = true;
	}
	INFO_LOG(Log::TexReplacement, "Shadow texture %08x %dx%d dumped with region %dx%d", variant.addr, variant.texW, variant.texH, region.w, region.h);
}

void TextureReplacer::CommitShadowIndexed(ShadowVariant &variant, ShadowRegion region, u32 hash) {
	// variant.cachekey has no CLUT hash, the same as IndexedSaveKey.
	const ReplacementCacheKey namingKey = NamingKey(variant.cachekey, hash);

	bool foundAlias = false;
	bool ignored = false;
	std::string replacedLevelNames = LookupHashFile(namingKey, &foundAlias, &ignored);
	if (ignored) {
		return;
	}
	SavedTextureCacheData &saveData = savedCache_[namingKey];
	if (saveData.levelSaved[0]) {
		return;
	}

	std::string hashfile;
	if (!replacedLevelNames.empty()) {
		std::vector<std::string> names;
		SplitString(replacedLevelNames, '|', names);
		hashfile = names[0];
	} else {
		hashfile = HashName(namingKey, 0) + "_indexed.png";
	}

	// Indices aren't scaled, so the region is in texels.
	const int saveW = std::clamp((int)region.w, 1, variant.texW);
	const int saveH = std::clamp((int)region.h, 1, variant.texH);
	u8 *saveBuf = (u8 *)malloc((size_t)saveW * saveH);
	if (!saveBuf) {
		ERROR_LOG(Log::TexReplacement, "Failed to allocate %d bytes of memory for saving a texture", saveW * saveH);
		return;
	}
	for (int y = 0; y < saveH; y++) {
		memcpy(saveBuf + (size_t)y * saveW, variant.indices.data() + (size_t)y * variant.texW, saveW);
	}

	SaveTextureTask *task = new SaveTextureTask(saveBuf);
	task->filename = basePath_ / hashfile;
	task->saveFilename = newTextureDir_ / hashfile;
	task->w = saveW;
	task->h = saveH;
	task->replacedInfoHash = hash;
	task->indexed = true;
	task->numColors = variant.numColors;
	memcpy(task->palette, variant.palette, variant.numColors * sizeof(u32));
	g_threadManager.EnqueueTask(task);

	saveData.levelW[0] = saveW;
	saveData.levelH[0] = saveH;
	saveData.levelSaved[0] = true;
	saveData.lastTimeSaved = time_now_d();

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(variant.addr, variant.texW, variant.texH)];
	const size_t numRegions = slot.regions.size();
	AddShadowRegion(slot, region);
	if (slot.regions.size() != numRegions) {
		shadowRangesDirty_ = true;
	}
	INFO_LOG(Log::TexReplacement, "Shadow texture %08x %dx%d dumped as indices with region %dx%d", variant.addr, variant.texW, variant.texH, region.w, region.h);
}

void TextureReplacer::CommitAllShadows() {
	for (auto &[key, variant] : shadowVariants_) {
		CommitShadow(variant);
	}
	shadowVariants_.clear();
	shadowMemoryUsed_ = 0;
	if (shadowRangesDirty_) {
		SaveShadowRanges();
	}
}

void TextureReplacer::ShadowTick() {
	if (!shadowEnabled_ || !saveEnabled_) {
		return;
	}
	const double now = time_now_d();
	if (now - shadowLastTick_ < 0.5) {
		return;
	}
	shadowLastTick_ = now;

	for (auto it = shadowVariants_.begin(); it != shadowVariants_.end(); ) {
		if (now - it->second.lastChange >= shadowSettleSeconds_) {
			CommitShadow(it->second);
			it = shadowVariants_.erase(it);
		} else {
			++it;
		}
	}

	if (shadowRangesDirty_ && now - shadowRangesLastSaved_ >= 10.0) {
		SaveShadowRanges();
	}
}

void TextureReplacer::SaveShadowRanges() {
	shadowRangesDirty_ = false;
	shadowRangesLastSaved_ = time_now_d();
	if (newTextureDir_.empty()) {
		return;
	}

	// Sorted, so the file only changes where the regions did.
	std::map<u64, const ShadowSlot *> sorted;
	for (const auto &[key, slot] : shadowSlots_) {
		if (!slot.regions.empty()) {
			sorted[key] = &slot;
		}
	}

	std::string hashranges;
	std::string shadowranges;
	for (const auto &[key, slot] : sorted) {
		const u32 addr = (u32)(key >> 32);
		const int w = (int)((key >> 16) & 0xFFFF);
		const int h = (int)(key & 0xFFFF);
		std::vector<ShadowRegion> regions;
		bool hasFull = false;
		for (const ShadowRegion &r : slot->regions) {
			if (r.w == w && r.h == h) {
				hasFull = true;
			} else {
				regions.push_back(r);
			}
		}
		if (regions.empty()) {
			// The whole texture is always tried, no need to list it.
			continue;
		}
		const std::string slotName = StringFromFormat("0x%08x,%d,%d = ", addr, w, h);
		if (regions.size() == 1 && !hasFull) {
			hashranges += slotName + StringFromFormat("%d,%d\n", regions[0].w, regions[0].h);
		} else {
			std::string line = slotName;
			for (size_t i = 0; i < regions.size(); i++) {
				if (i != 0) {
					line += "|";
				}
				line += StringFromFormat("%d,%d", regions[i].w, regions[i].h);
			}
			shadowranges += line + "\n";
		}
	}

	const Path filename = newTextureDir_ / SHADOW_RANGES_FILENAME;
	FILE *f = File::OpenCFile(filename, "wb");
	if (!f) {
		ERROR_LOG(Log::TexReplacement, "Failed to write %s", filename.c_str());
		return;
	}
	fprintf(f, "# Regions found while dumping with shadowTextures = true. Replace the [hashranges] and [shadowranges]\n");
	fprintf(f, "# sections of textures.ini with these. Entries already in textures.ini are included.\n\n");
	fprintf(f, "[hashranges]\n%s\n[shadowranges]\n%s", hashranges.c_str(), shadowranges.c_str());
	fclose(f);
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
		fprintf(f, R"(# This describes your textures and set up options for texture replacement.
# Documentation about the options and syntax is available here:
# https://www.ppsspp.org/docs/reference/texture-replacement

[options]
version = 1
hash = quick             # options available: "quick", "xxh32" - more accurate, but slower, "xxh64" - more accurate and quite fast, but slower than xxh32 on 32 bit cpu's
ignoreMipmap = true      # Usually, can just generate them with basisu, no need to dump.
reduceHash = false       # Unsafe and can cause glitches in some cases, but allows to skip garbage data in some textures reducing endless duplicates as a side effect speeds up hashing as well, requires stronger hash like xxh32 or xxh64
ignoreAddress = false    # Reduces duplicates at the cost of making hash less reliable, requires stronger hash like xxh32 or xxh64. Basically automatically sets the address to 0 in the dumped filenames.
saveIndexed = false      # When dumping, textures with a palette (CLUT4/CLUT8) can be dumped as 8-bit palette PNGs of their indices, named <hash>_indexed.png, one for all palettes. "all" does that for every such texture, "true" only for the ones listed in [indexed]. Other textures are dumped as usual.

[games]
# Used to make it easier to install, and override settings for other regions.
# Files still have to be copied to each TEXTURES folder.
%s = %s

[hashes]
# Use / for folders not \\, avoid special characters, and stick to lowercase.
# See wiki for more info.

[hashranges]
# This is useful for images that very clearly have smaller dimensions, like 480x272 image. They'll need to be redumped, since the hash will change. See the documentation.
# Example: 08b31020,512,512 = 480,272
# Example: 0x08b31020,512,512 = 480,272

[filtering]
# You can enforce specific filtering modes with this. Available modes are linear, nearest, auto. See the docs.
# Example: 08d3961000000909ba70b2af = nearest

[indexed]
# Marks replacement images that hold palette indices instead of colors, for games that use many palettes
# for one texture or let the player change them. The game's current palette colors the image, so a single
# high resolution image works with every palette.
# Supported files:
# - PNG, either a palette (indexed) PNG, where the palette index of each pixel is used (the colors in the
#   file's palette are ignored), or a grayscale PNG of at most 8 bits, where the gray value is the index.
# - KTX2 in the R8_UNORM format (one byte per texel), uncompressed or with zstd supercompression, with the
#   mipmaps stored in the file. This is lossless, and loads faster than PNG.
# Lossy formats (Basis, DDS/BC, RGB/RGBA images) can't be used here, since a small error in an index picks
# the wrong color. Mipmaps are not generated, only the levels in the file are used.
# The image can have any resolution, for example 4x the original. The index values must match what the game uses.
# Use the hashes section to name the file, with the CLUT hash set to 00000000 to match every palette.
# Format: <address><clut hash><texture hash> = true
# Example: 08d3961000000000ba70b2af = true
# For textures where the game changes the palette often, use gpu instead of true. The indices are then
# uploaded as they are and the palette is applied in the shader, so a palette change costs no CPU time and
# builds no new texture. Only the first mip level is used, and the shader must support bitwise operations,
# otherwise it falls back to true.
# Example: 08d3961000000000ba70b2af = gpu
# With saveIndexed in [options], dumped palette PNGs have the palette that the game used when dumping (for
# viewing, the colors in it are ignored when loading). Add the file to [hashes] and the hash to this section
# to use it.

[reducehashranges]
# Lets you set texture sizes where the hash range is reduced by a factor. See the docs.
# Example:
512,512=0.5

# Experimental: shadowTextures = true in [options] hashes only the part of each texture that draws sample,
# and holds dumps in RAM until that part stops growing (shadowSettleSeconds, default 4), so data a game
# keeps below an image doesn't produce new dumps. The regions found are written to new/shadowranges.ini:
# copy its sections here. A texture whose contents were drawn with several regions gets a shadowrange,
# and the largest region that has a replacement wins:
#[shadowranges]
#0x08b31020,512,512 = 480,272|480,136

)", gameID.c_str(), INI_FILENAME.c_str());
		fclose(f);
	}
	return File::Exists(generatedFilename);
}

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
#include <type_traits>
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
#include "Common/File/DirListing.h"
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
#include "GPU/Common/TextureScalerCommon.h"

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
	for (auto &[key, slot] : layered_) {
		delete slot.texture;
	}
	for (ReplacedTexture *texture : retiredLayered_) {
		delete texture;
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
	ignoreRanges_.clear();
	ignoreAddress_ = false;
	reduceHash_ = false;
	reduceHashGlobalValue = 0.5;
	// Prevents dumping the mipmaps.
	ignoreMipmap_ = false;
	saveIndexed_ = SaveIndexedMode::OFF;

	shadowEnabled_ = false;
	shadowSettleSeconds_ = 4.0;
	shadowMinVisibleSeconds_ = 0.0;
	shadowMaxRegions_ = 8;
	shadowRegionAlign_ = 1;
	shadowMemoryBudget_ = (size_t)1024 * 1024 * 1024;
	shadowSlots_.clear();
	shadowKeys_.clear();
	shadowRangesDirty_ = false;
	// Cache entries may still point to these.
	for (auto &[key, slot] : layered_) {
		if (slot.texture) {
			retiredLayered_.push_back(slot.texture);
		}
	}
	layered_.clear();
	layeredTextScaler_ = TextureScalerCommon::XBRZ;
	layeredGlyphs_ = true;
	layeredGlyphCache_.clear();
	savedLayeredGlyphs_.clear();

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

	// After the override ini too, which can turn it on.
	ApplyIgnoreAddress();
	MarkExistingDumps();

	vfs_ = dir;

	// If we have stuff loaded from before, need to update the vfs pointers to avoid
	// crash on exit. The actual problem is that we tend to call LoadIni a little too much...
	for (auto &repl : levelCache_) {
		repl.second->vfs_ = vfs_;
	}
	for (ReplacedTexture *texture : retiredLayered_) {
		texture->vfs_ = vfs_;
	}

	if (replaceEnabled_) {
		if (vfsIsZip_) {
			INFO_LOG(Log::TexReplacement, "Texture pack activated from '%s'", (basePath_ / ZIP_FILENAME).c_str());
		} else {
			INFO_LOG(Log::TexReplacement, "Texture pack activated from '%s'", basePath_.c_str());
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
	options->Get("shadowMinVisibleSeconds", &shadowMinVisibleSeconds_);
	options->Get("layeredGlyphs", &layeredGlyphs_);
	std::string layeredTextScaler;
	if (options->Get("layeredTextScaler", &layeredTextScaler) && !layeredTextScaler.empty()) {
		if (!strcasecmp(layeredTextScaler.c_str(), "none")) {
			layeredTextScaler_ = -1;
		} else if (!strcasecmp(layeredTextScaler.c_str(), "xbrz")) {
			layeredTextScaler_ = TextureScalerCommon::XBRZ;
		} else if (!strcasecmp(layeredTextScaler.c_str(), "hybrid")) {
			layeredTextScaler_ = TextureScalerCommon::HYBRID;
		} else if (!strcasecmp(layeredTextScaler.c_str(), "bicubic")) {
			layeredTextScaler_ = TextureScalerCommon::BICUBIC;
		} else if (!strcasecmp(layeredTextScaler.c_str(), "hybrid_bicubic")) {
			layeredTextScaler_ = TextureScalerCommon::HYBRID_BICUBIC;
		} else {
			ERROR_LOG(Log::TexReplacement, "Unsupported layeredTextScaler value: %s (use none, xbrz, hybrid, bicubic or hybrid_bicubic)", layeredTextScaler.c_str());
		}
	}
	options->Get("shadowMaxRegions", &shadowMaxRegions_);
	options->Get("shadowRegionAlign", &shadowRegionAlign_);
	int shadowMemoryMB = 0;
	if (options->Get("shadowMemoryMB", &shadowMemoryMB) && shadowMemoryMB > 0) {
		shadowMemoryBudget_ = (size_t)shadowMemoryMB * 1024 * 1024;
	}
	shadowSettleSeconds_ = std::clamp(shadowSettleSeconds_, 0.0, 600.0);
	shadowMinVisibleSeconds_ = std::clamp(shadowMinVisibleSeconds_, 0.0, 60.0);
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

	if (ini.HasSection("layered")) {
		auto layered = ini.GetOrCreateSection("layered")->ToMap();
		// Format: addr,w,h = original.png|hd.png
		for (const auto &[k, v] : layered) {
			ParseLayered(k, v);
		}
	}

	if (ini.HasSection("ignore")) {
		// Format: start-end,w,h = (the range and the size are optional parts)
		for (const auto &[k, v] : ini.GetOrCreateSection("ignore")->ToMap()) {
			ParseIgnore(k);
		}
	}

	// Format of both: addr,w,h = w,h|x,y,w,h|...
	if (ini.HasSection("shadowranges")) {
		// Written by the pack's author. Dumps of these textures only use these regions.
		for (const auto &[k, v] : ini.GetOrCreateSection("shadowranges")->ToMap()) {
			ParseShadowRange(k, v, true);
		}
	}
	if (ini.HasSection("learnedshadowranges")) {
		// Found while dumping, see SaveShadowRanges.
		for (const auto &[k, v] : ini.GetOrCreateSection("learnedshadowranges")->ToMap()) {
			ParseShadowRange(k, v, false);
		}
	}

	// A hash range is one more region to try.
	for (const auto &[rangeKey, wh] : hashranges_) {
		ShadowRegion region;
		region.w = (u16)wh.first;
		region.h = (u16)wh.second;
		ShadowSlot &slot = shadowSlots_[rangeKey];
		slot.hashRange.clear();
		slot.hashRange.push_back(region);
	}

	return true;
}

void TextureReplacer::ParseIgnore(const std::string &key) {
	std::vector<std::string> parts;
	SplitString(key, ',', parts, true);
	std::vector<std::string> range;
	if (!parts.empty()) {
		SplitString(parts[0], '-', range, true);
	}
	auto parseAddr = [](std::string text, u32 *addr) {
		if (!startsWith(text, "0x") && !startsWith(text, "0X")) {
			text = "0x" + text;
		}
		return TryParse(text, addr);
	};

	IgnoreRange ignore;
	u32 w = 0;
	u32 h = 0;
	bool valid = (parts.size() == 1 || parts.size() == 3) && (range.size() == 1 || range.size() == 2);
	valid = valid && parseAddr(range[0], &ignore.start);
	ignore.end = ignore.start;
	if (valid && range.size() == 2) {
		valid = parseAddr(range[1], &ignore.end) && ignore.end >= ignore.start;
	}
	if (valid && parts.size() == 3) {
		valid = TryParse(parts[1], &w) && TryParse(parts[2], &h) && w > 0 && h > 0 && w <= 1024 && h <= 1024;
	}
	if (!valid) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid [ignore] entry %s, expecting 0x08800000-0x08900000 or 0x08812340, optionally followed by ,w,h", key.c_str());
		return;
	}
	// The same as in the names of dumps.
	ignore.start &= 0x3FFFFFFF;
	ignore.end &= 0x3FFFFFFF;
	ignore.w = (u16)w;
	ignore.h = (u16)h;
	ignoreRanges_.push_back(ignore);
}

bool TextureReplacer::IsIgnoredTexture(u32 addr, int w, int h) const {
	addr &= 0x3FFFFFFF;
	for (const IgnoreRange &ignore : ignoreRanges_) {
		if (addr >= ignore.start && addr <= ignore.end && (ignore.w == 0 || (ignore.w == w && ignore.h == h))) {
			return true;
		}
	}
	return false;
}

void TextureReplacer::ApplyIgnoreAddress() {
	if (!ignoreAddress_) {
		return;
	}

	// Entries with an address are found with it dropped. One without (from an older pack) wins over one with.
	std::unordered_map<ReplacementCacheKey, std::string> aliases;
	for (const auto &[key, value] : aliases_) {
		if (key.Address() == 0) {
			aliases[key] = value;
		}
	}
	for (const auto &[key, value] : aliases_) {
		if (key.Address() == 0) {
			continue;
		}
		if (key.ContentsHash() == 0 && key.ClutHash() == 0) {
			if (value.empty()) {
				// "Don't dump anything at this address", which needs the address. Matches the same as before.
				IgnoreRange ignore;
				ignore.start = ignore.end = key.Address();
				ignoreRanges_.push_back(ignore);
			}
			// Without the address, it would match everything.
			continue;
		}
		if (key.ContentsHash() == 0) {
			// Address and CLUT only. Without the address it would match that CLUT anywhere, so it's dropped.
			WARN_LOG(Log::TexReplacement, "%s in [hashes] has no texture hash, it's ignored with ignoreAddress", HashName(key, 0).c_str());
			continue;
		}
		ReplacementCacheKey zeroed = key;
		zeroed.ZeroAddress();
		aliases.emplace(zeroed, value);
	}
	aliases_ = std::move(aliases);

	auto zeroMap = [](auto &map) {
		std::remove_reference_t<decltype(map)> result;
		for (const auto &[key, value] : map) {
			if (key.Address() == 0) {
				result[key] = value;
			}
		}
		for (const auto &[key, value] : map) {
			if (key.Address() != 0 && key.ContentsHash() != 0) {
				ReplacementCacheKey zeroed = key;
				zeroed.ZeroAddress();
				result.emplace(zeroed, value);
			}
		}
		map = std::move(result);
	};
	zeroMap(filtering_);
	zeroMap(indexed_);
}

void TextureReplacer::MarkExistingDumps() {
	if (!saveEnabled_ || newTextureDir_.empty()) {
		return;
	}
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(newTextureDir_, &files, "png");
	int marked = 0;
	for (const File::FileInfo &file : files) {
		if (file.isDirectory) {
			continue;
		}
		// <address><CLUT hash><texture hash>[_level][_indexed].png
		std::string stem = file.name.substr(0, file.name.size() - 4);
		if (endsWith(stem, "_indexed")) {
			// Its CLUT hash is zero already, the same as IndexedSaveKey.
			stem = stem.substr(0, stem.size() - 8);
		}
		if (stem.size() < 24 || (stem.size() > 24 && stem[24] != '_')) {
			continue;
		}
		ReplacementCacheKey key(0, 0);
		int level = 0;
		if (sscanf(stem.c_str(), "%16llx%8x_%d", &key.cachekey, &key.hash, &level) < 2 || level < 0 || level >= (int)ARRAY_SIZE(SavedTextureCacheData::levelSaved)) {
			continue;
		}
		if (ignoreAddress_) {
			key.ZeroAddress();
		}
		savedCache_[key].levelSaved[level] = true;
		marked++;
	}
	if (marked) {
		INFO_LOG(Log::TexReplacement, "%d textures already dumped in %s", marked, newTextureDir_.c_str());
	}
}

void TextureReplacer::LogDump(const std::string &hashfile, u32 addr, ReplacementCacheKey key, int texW, int texH, GETextureFormat fmt, const char *region) {
	if (newTextureDir_.empty()) {
		return;
	}
	const Path filename = newTextureDir_ / "dumps.txt";
	const bool exists = File::Exists(filename);
	FILE *f = File::OpenCFile(filename, "ab");
	if (!f) {
		return;
	}
	if (!exists) {
		fprintf(f, "# One line for each texture dumped to this folder: the file, then where it came from.\n");
		fprintf(f, "# key is the name it would have with the address and palette hash in it, for [hashes] or [ignore].\n");
	}
	if (!dumpLogStarted_) {
		dumpLogStarted_ = true;
		fprintf(f, "\n# %s, dumping started\n", gameID_.c_str());
	}
	fprintf(f, "%s addr=0x%08x size=%dx%d format=%s key=%016llx%08x%s%s\n", hashfile.c_str(), addr & 0x3FFFFFFF, texW, texH,
		GeTextureFormatToString(fmt), (unsigned long long)key.cachekey, key.hash, region ? " region=" : "", region ? region : "");
	fclose(f);
}

void TextureReplacer::ParseShadowRange(const std::string &key, const std::string &value, bool declared) {
	std::vector<std::string> keyParts;
	SplitString(key, ',', keyParts, true);
	std::vector<std::string> regionParts;
	SplitString(value, '|', regionParts, true);

	if (keyParts.size() != 3 || regionParts.empty()) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid shadowrange %s = %s, expecting addr,w,h = w,h|x,y,w,h|...", key.c_str(), value.c_str());
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
	// w,h from (0,0), or x,y,w,h.
	auto parseRect = [&](const std::string &text, ShadowRegion *region) {
		std::vector<std::string> valueParts;
		SplitString(text, ',', valueParts, true);
		u32 values[4]{};
		const size_t count = valueParts.size();
		bool valid = count == 2 || count == 4;
		for (size_t i = 0; valid && i < count; i++) {
			valid = TryParse(valueParts[i], &values[count == 2 ? i + 2 : i]);
		}
		if (!valid || values[2] == 0 || values[3] == 0 || values[0] + values[2] > fromW || values[1] + values[3] > fromH) {
			return false;
		}
		region->x = (u16)values[0];
		region->y = (u16)values[1];
		region->w = (u16)values[2];
		region->h = (u16)values[3];
		return true;
	};
	for (const std::string &part : regionParts) {
		// Several rectangles joined with + are one region, hashed together.
		std::vector<std::string> rectTexts;
		SplitString(part, '+', rectTexts, true);
		std::vector<ShadowRegion> rects;
		bool valid = !rectTexts.empty();
		for (const std::string &rectText : rectTexts) {
			ShadowRegion rect;
			valid = valid && parseRect(rectText, &rect);
			rects.push_back(rect);
		}
		if (!valid || (rects.size() > 1 && !declared)) {
			ERROR_LOG(Log::TexReplacement, "Ignoring invalid region '%s' in shadowrange %s", part.c_str(), key.c_str());
			continue;
		}
		if (rects.size() == 1) {
			AddShadowRegion(declared ? slot.declared : slot.learned, rects[0]);
		} else if (std::find(slot.declaredCombos.begin(), slot.declaredCombos.end(), rects) == slot.declaredCombos.end()) {
			slot.declaredCombos.push_back(rects);
		}
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

	if (IsIgnoredTexture(replacementKey.Address(), w, h)) {
		// Marked as not found, for faster lookup next time.
		cache_.emplace(std::make_pair(replacementKey, ReplacedTextureRef{}));
		return nullptr;
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
	if (IsIgnoredTexture(replacedInfo.addr, replacedInfo.texW, replacedInfo.texH))
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

	if (IsLayered(replacedInfo.addr, replacedInfo.texW, replacedInfo.texH)) {
		// Its contents are composed from the [layered] images.
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
		// Generate a new PNG filename, complete with level. With ignoreAddress it still has the address, which is
		// dropped when it's read.
		hashfile = HashName(ReplacementCacheKey(replacedInfo.cachekey, replacedInfo.hash), level) + ".png";
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
	LogDump(hashfile, replacedInfo.addr, ReplacementCacheKey(replacedInfo.cachekey, replacedInfo.hash), replacedInfo.texW, replacedInfo.texH, replacedInfo.texFormat, nullptr);

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

	if (IsLayered(replacedInfo.addr, replacedInfo.texW, replacedInfo.texH)) {
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
		// Keeps the address, see NotifyTextureDecoded.
		ReplacementCacheKey nameKey(replacedInfo.cachekey, replacedInfo.hash);
		nameKey.ZeroClutHash();
		hashfile = HashName(nameKey, 0) + "_indexed.png";
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
	LogDump(hashfile, replacedInfo.addr, ReplacementCacheKey(replacedInfo.cachekey, replacedInfo.hash), replacedInfo.texW, replacedInfo.texH, replacedInfo.texFormat, nullptr);

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
// A texture declared as 512x512 often only has an image in part of it, with whatever the game's allocator put
// around it. Hashing all of it gives a new key, and a new dump, each time that other data changes. Here the
// key only covers what draws sample (known in through mode, from the vertex UVs), and dumps wait in RAM until
// that part stops growing. Each waiting dump keeps a copy of the texture's memory, so it can be hashed again
// with its final region, and contents that turn out to be the same there are only written once.

// A texel or two of slack, for draws that reach just past the edge of the image with bilinear filtering.
static constexpr int SHADOW_COVER_SLACK = 2;

void TextureReplacer::ShadowRegion::Merge(const ShadowRegion &o) {
	if (o.Empty()) {
		return;
	}
	if (Empty()) {
		*this = o;
		return;
	}
	const int right = std::max(Right(), o.Right());
	const int bottom = std::max(Bottom(), o.Bottom());
	x = std::min(x, o.x);
	y = std::min(y, o.y);
	w = (u16)(right - x);
	h = (u16)(bottom - y);
}

TextureReplacer::ShadowRegion TextureReplacer::FullRegion(int w, int h) {
	ShadowRegion region;
	region.w = (u16)w;
	region.h = (u16)h;
	return region;
}

TextureReplacer::ShadowRegion TextureReplacer::NormalizeShadowRegion(const ShadowBounds &bounds, int w, int h, bool swizzled, GETextureFormat fmt) const {
	if (!bounds.known || bounds.maxU < bounds.minU || bounds.maxV < bounds.minV) {
		return FullRegion(w, h);
	}

	int x0 = bounds.minU;
	int y0 = bounds.minV;
	int x1 = std::max((int)bounds.maxU, x0 + 1);
	int y1 = std::max((int)bounds.maxV, y0 + 1);
	// Beyond the edge the texture wraps or clamps, which can show any part of it.
	if (x1 > w || x0 >= w) {
		x0 = 0;
		x1 = w;
	}
	if (y1 > h || y0 >= h) {
		y0 = 0;
		y1 = h;
	}

	const int align = shadowRegionAlign_;
	if (align > 1) {
		x0 = x0 / align * align;
		y0 = y0 / align * align;
		x1 = std::min(w, (x1 + align - 1) / align * align);
		y1 = std::min(h, (y1 + align - 1) / align * align);
	}

	// Swizzled and DXT textures are stored in blocks, so only whole rows of blocks can be left out.
	if (fmt == GE_TFMT_DXT1 || fmt == GE_TFMT_DXT3 || fmt == GE_TFMT_DXT5) {
		x0 = 0;
		x1 = w;
		y0 &= ~3;
		y1 = std::min(h, (y1 + 3) & ~3);
	} else if (swizzled) {
		// Blocks are 16 bytes wide and 8 rows tall.
		x0 = 0;
		x1 = w;
		y0 &= ~7;
		y1 = std::min(h, (y1 + 7) & ~7);
	} else if (textureBitsPerPixel[fmt] < 8) {
		// Two texels per byte.
		x0 &= ~1;
		x1 = std::min(w, (x1 + 1) & ~1);
	}

	ShadowRegion region;
	region.x = (u16)x0;
	region.y = (u16)y0;
	region.w = (u16)(x1 - x0);
	region.h = (u16)(y1 - y0);
	return region;
}

u32 TextureReplacer::HashShadowRegion(const u8 *base, u32 availableBytes, int bufw, const ShadowRegion &region, GETextureFormat fmt) {
	// A region from (0,0) hashes the same as a hash range of its size.
	const u64 offset = ((u64)textureBitsPerPixel[fmt] * ((u64)region.y * bufw + region.x)) / 8;
	if (offset >= availableBytes) {
		return 0;
	}
	return HashTextureData(base + offset, availableBytes - (u32)offset, bufw, region.w, region.h, fmt);
}

bool TextureReplacer::AddShadowRegion(std::vector<ShadowRegion> &regions, ShadowRegion region) {
	if (std::find(regions.begin(), regions.end(), region) != regions.end()) {
		return false;
	}
	regions.push_back(region);
	// Largest first, so a content that was dumped with more of the texture wins over a part of it.
	std::sort(regions.begin(), regions.end(), [](const ShadowRegion &a, const ShadowRegion &b) {
		if (a.Area() != b.Area()) {
			return a.Area() > b.Area();
		}
		if (a.y != b.y) {
			return a.y < b.y;
		}
		return a.x < b.x;
	});
	return true;
}

bool TextureReplacer::AddDrawnPart(std::vector<ShadowRegion> &parts, const ShadowRegion &region) {
	if (region.Empty()) {
		return false;
	}
	for (const ShadowRegion &p : parts) {
		if (p.Covers(region)) {
			return false;
		}
	}
	parts.erase(std::remove_if(parts.begin(), parts.end(), [&](const ShadowRegion &p) { return region.Covers(p); }), parts.end());
	parts.push_back(region);
	// Draws all over the texture (like tiles of a sheet, one at a time) are kept as one rectangle.
	if (parts.size() > 16) {
		const ShadowRegion bounds = PartsBounds(parts);
		parts.clear();
		parts.push_back(bounds);
	}
	return true;
}

TextureReplacer::ShadowRegion TextureReplacer::PartsBounds(const std::vector<ShadowRegion> &parts) {
	ShadowRegion bounds;
	for (const ShadowRegion &p : parts) {
		bounds.Merge(p);
	}
	return bounds;
}

std::string TextureReplacer::ShadowRegionString(const ShadowRegion &r) {
	if (r.x == 0 && r.y == 0) {
		return StringFromFormat("%d,%d", r.w, r.h);
	}
	return StringFromFormat("%d,%d,%d,%d", r.x, r.y, r.w, r.h);
}

std::string TextureReplacer::PartsString(const std::vector<ShadowRegion> &parts) {
	std::string text;
	for (size_t i = 0; i < parts.size(); i++) {
		if (i != 0) {
			text += "+";
		}
		text += ShadowRegionString(parts[i]);
	}
	return text;
}

bool TextureReplacer::InParts(const std::vector<ShadowRegion> &parts, int x, int y) {
	for (const ShadowRegion &p : parts) {
		if (x >= p.x && x < p.Right() && y >= p.y && y < p.Bottom()) {
			return true;
		}
	}
	return false;
}

u32 TextureReplacer::HashShadowParts(const u8 *base, u32 availableBytes, int bufw, const std::vector<ShadowRegion> &parts, GETextureFormat fmt) {
	if (parts.size() == 1) {
		// The same as a hash range of it.
		return HashShadowRegion(base, availableBytes, bufw, parts[0], fmt);
	}
	u32 hash = 0x9E3779B9;
	for (const ShadowRegion &p : parts) {
		hash = (hash ^ HashShadowRegion(base, availableBytes, bufw, p, fmt)) * 0x01000193;
	}
	return hash;
}

std::vector<TextureReplacer::ShadowRegion> TextureReplacer::ShadowLookupRegions(const ShadowSlot &slot, int w, int h) {
	std::vector<ShadowRegion> regions;
	// The whole texture is always one of them, since that's what dumps of contents drawn without known UVs use.
	AddShadowRegion(regions, FullRegion(w, h));
	if (slot.IsDeclared()) {
		for (const ShadowRegion &r : slot.declared) {
			AddShadowRegion(regions, r);
		}
		return regions;
	}
	for (const ShadowRegion &r : slot.learned) {
		AddShadowRegion(regions, r);
	}
	for (const ShadowRegion &r : slot.hashRange) {
		AddShadowRegion(regions, r);
	}
	return regions;
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

u32 TextureReplacer::ComputeShadowHash(const ShadowHashParams &p, u16 *extentW, u16 *extentH) {
	_dbg_assert_(ShadowEnabled());

	const u8 *checkp = Memory::GetPointerUnchecked(p.addr);
	const u32 available = Memory::MaxSizeAtAddress(p.addr);
	const ShadowRegion full = FullRegion(p.w, p.h);

	// What draws of this texture sampled, under its previous key and now.
	bool unbounded = false;
	ShadowRegion seen;
	if (p.oldHash != 0) {
		auto old = shadowKeys_.find(ReplacementCacheKey(p.cachekey, p.oldHash));
		if (old != shadowKeys_.end()) {
			unbounded = old->second.unbounded;
			seen = old->second.seen;
		}
	}
	if (p.current) {
		if (!p.current->known) {
			unbounded = true;
		} else {
			seen.Merge(NormalizeShadowRegion(*p.current, p.w, p.h, p.swizzled, p.fmt));
		}
	}

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(p.addr, p.w, p.h)];
	if (unbounded) {
		slot.unbounded = true;
	} else {
		slot.drawn.Merge(seen);
	}
	const bool seenKnown = !unbounded && !seen.Empty();

	// Try the regions that dumps of this texture were made with, largest first. A region has to cover what draws
	// sample, otherwise the replacement would leave out what's drawn.
	const std::vector<ShadowRegion> candidates = ShadowLookupRegions(slot, p.w, p.h);
	ShadowRegion chosen;
	u32 hash = 0;
	bool found = false;
	for (const ShadowRegion &region : candidates) {
		if (seenKnown && !region.Covers(seen, SHADOW_COVER_SLACK)) {
			continue;
		}
		hash = HashShadowRegion(checkp, available, p.bufw, region, p.fmt);
		if (ShadowKeyKnown(NamingKey(p.cachekey, hash))) {
			chosen = region;
			found = true;
			break;
		}
	}

	// Then the ones made of several rectangles, which aren't larger than what they're in.
	for (size_t i = 0; !found && i < slot.declaredCombos.size(); i++) {
		const std::vector<ShadowRegion> &parts = slot.declaredCombos[i];
		const ShadowRegion bounds = PartsBounds(parts);
		if (seenKnown && !bounds.Covers(seen, SHADOW_COVER_SLACK)) {
			continue;
		}
		hash = HashShadowParts(checkp, available, p.bufw, parts, p.fmt);
		if (ShadowKeyKnown(NamingKey(p.cachekey, hash))) {
			chosen = bounds;
			found = true;
		}
	}

	if (!found) {
		// Nothing was made for this content yet. Key it by everything drawn from this texture so far, so data
		// outside that doesn't change the key. The dump picks its final region when it's written.
		chosen = full;
		if (!slot.unbounded && !slot.drawn.Empty()) {
			chosen = slot.drawn;
		}
		hash = HashShadowRegion(checkp, available, p.bufw, chosen, p.fmt);
	}

	ShadowKeyInfo &info = shadowKeys_[ReplacementCacheKey(p.cachekey, hash)];
	info.unbounded = info.unbounded || unbounded;
	info.seen.Merge(seen);
	info.region = chosen;
	info.lastUsed = shadowNow_;

	// The replacement is placed from (0,0), so it reaches to the bottom right of the region.
	*extentW = (u16)chosen.Right();
	*extentH = (u16)chosen.Bottom();
	return hash;
}

bool TextureReplacer::NotifyShadowDraw(u64 cachekey, u32 hash, u32 addr, int w, int h, bool swizzled, GETextureFormat fmt, const ShadowBounds *bounds) {
	const bool unknown = bounds && !bounds->known;
	ShadowRegion region;
	if (bounds && bounds->known) {
		region = NormalizeShadowRegion(*bounds, w, h, swizzled, fmt);
	}

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(addr, w, h)];
	if (unknown) {
		slot.unbounded = true;
	} else {
		slot.drawn.Merge(region);
	}

	bool beyondRegion = false;
	auto keyInfo = shadowKeys_.find(ReplacementCacheKey(cachekey, hash));
	if (keyInfo != shadowKeys_.end()) {
		ShadowKeyInfo &info = keyInfo->second;
		info.lastUsed = shadowNow_;
		if (unknown) {
			if (!info.unbounded) {
				info.unbounded = true;
				beyondRegion = !(info.region == FullRegion(w, h));
			}
		} else if (!region.Empty() && !info.seen.Covers(region)) {
			info.seen.Merge(region);
			beyondRegion = !info.region.Covers(info.seen, SHADOW_COVER_SLACK);
		}
	}

	if (!saveEnabled_) {
		return beyondRegion;
	}
	auto it = FindShadowVariant(cachekey, hash);
	if (it == shadowVariants_.end()) {
		return beyondRegion;
	}

	ShadowVariant &variant = it->second;
	if (!variant.drawn) {
		variant.drawn = true;
		variant.firstDrawn = shadowNow_;
	}
	variant.lastDrawn = shadowNow_;
	if (unknown) {
		if (!variant.unbounded) {
			variant.unbounded = true;
			variant.lastChange = shadowNow_;
		}
	} else if (AddDrawnPart(variant.drawnParts, region)) {
		variant.seen.Merge(region);
		variant.lastChange = shadowNow_;
	}
	return beyondRegion;
}

void TextureReplacer::NotifyShadowRekeyed(u64 cachekey, u32 oldHash) {
	if (!saveEnabled_) {
		return;
	}

	ShadowRekey rekey;
	rekey.valid = true;
	rekey.cachekey = cachekey;
	auto keyInfo = shadowKeys_.find(ReplacementCacheKey(cachekey, oldHash));
	if (keyInfo != shadowKeys_.end()) {
		rekey.unbounded = keyInfo->second.unbounded;
		rekey.seen = keyInfo->second.seen;
	}

	auto it = FindShadowVariant(cachekey, oldHash);
	if (it != shadowVariants_.end()) {
		const ShadowVariant &old = it->second;
		rekey.unbounded = rekey.unbounded || old.unbounded;
		rekey.seen.Merge(old.seen);
		rekey.drawnParts = old.drawnParts;
		rekey.drawn = old.drawn;
		rekey.firstDrawn = old.firstDrawn;
		shadowMemoryUsed_ -= std::min(shadowMemoryUsed_, old.memoryUsed);
		shadowVariants_.erase(it);
	}
	shadowRekey_ = rekey;
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
	variant.sourceCachekey = replacedInfo.cachekey;
	variant.addr = replacedInfo.addr;
	variant.bufw = replacedInfo.bufw;
	variant.texW = texW;
	variant.texH = texH;
	variant.swizzled = replacedInfo.swizzled;
	variant.fmt = replacedInfo.texFormat;
	const u8 *src = Memory::GetPointerUnchecked(replacedInfo.addr);
	variant.raw.assign(src, src + rawSize);
	variant.created = time_now_d();
	variant.lastChange = shadowNow_;
	variant.memoryUsed = variant.raw.size();
	// The rekey carry is matched on the texture's address, whatever its palette.
	if (shadowRekey_.valid && (shadowRekey_.cachekey & ~0xFFFFFFFFULL) == (variant.cachekey & ~0xFFFFFFFFULL)) {
		// The same content under its new key, see NotifyShadowRekeyed.
		variant.unbounded = shadowRekey_.unbounded;
		variant.seen = shadowRekey_.seen;
		variant.drawnParts = shadowRekey_.drawnParts;
		if (variant.drawnParts.empty() && !variant.seen.Empty()) {
			variant.drawnParts.push_back(variant.seen);
		}
		variant.drawn = shadowRekey_.drawn;
		variant.firstDrawn = shadowRekey_.firstDrawn;
		variant.lastDrawn = shadowNow_;
	}
	shadowRekey_.valid = false;
	shadowMemoryUsed_ += variant.memoryUsed;
	return &shadowVariants_.emplace(variantKey, std::move(variant)).first->second;
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
	const ReplacementCacheKey namingKey = NamingKey(variantKey.cachekey, variantKey.hash);
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

// While a content of a declared texture waits, its memory is compared with the copy taken when it was captured.
// Texels that change while what's drawn of it stays the same aren't part of the image. That tells a sprite sheet
// that fills the whole texture from one that has other data in its last rows, at the same address and size.
void TextureReplacer::CheckShadowStability(ShadowVariant &variant) {
	if (variant.contentGone || !variant.drawn || variant.unbounded || variant.seen.Empty()) {
		return;
	}
	auto slotIt = shadowSlots_.find(ShadowSlotKey(variant.addr, variant.texW, variant.texH));
	if (slotIt == shadowSlots_.end() || !slotIt->second.IsDeclared()) {
		return;
	}
	const u32 size = (u32)variant.raw.size();
	if (!Memory::IsValidRange(variant.addr, size)) {
		variant.contentGone = true;
		return;
	}
	const u8 *mem = Memory::GetPointerUnchecked(variant.addr);
	const u8 *raw = variant.raw.data();

	// If what's drawn changed, the game put something else there. Only the parts drawn, other data can sit
	// between them.
	const std::vector<ShadowRegion> drawnParts = variant.drawnParts.empty() ? std::vector<ShadowRegion>{ variant.seen } : variant.drawnParts;
	if (HashShadowParts(mem, size, variant.bufw, drawnParts, variant.fmt) != HashShadowParts(raw, size, variant.bufw, drawnParts, variant.fmt)) {
		variant.contentGone = true;
		return;
	}

	const int texW = variant.texW;
	const int texH = variant.texH;
	if (variant.volatileX0.empty()) {
		variant.volatileX0.assign(texH, 0xFFFF);
		variant.volatileX1.assign(texH, 0);
	}
	auto mark = [&](int y0, int y1, int x0, int x1) {
		x0 = std::max(x0, 0);
		x1 = std::min(x1, texW);
		y1 = std::min(y1, texH);
		if (x0 >= x1) {
			return;
		}
		for (int y = std::max(y0, 0); y < y1; y++) {
			variant.volatileX0[y] = std::min(variant.volatileX0[y], (u16)x0);
			variant.volatileX1[y] = std::max(variant.volatileX1[y], (u16)x1);
		}
	};

	const int bpp = textureBitsPerPixel[variant.fmt];
	const bool dxt = variant.fmt == GE_TFMT_DXT1 || variant.fmt == GE_TFMT_DXT3 || variant.fmt == GE_TFMT_DXT5;
	if (dxt || variant.swizzled) {
		// Stored in blocks: 4x4 texels for DXT, 16 bytes by 8 rows when swizzled.
		const int blockW = dxt ? 4 : 16 * 8 / bpp;
		const int blockH = dxt ? 4 : 8;
		const size_t blockBytes = dxt ? (variant.fmt == GE_TFMT_DXT1 ? 8 : 16) : 16 * 8;
		const int rowBlocks = dxt ? (variant.bufw + 3) / 4 : (variant.bufw * bpp / 8) / 16;
		for (int by = 0; by * blockH < texH; by++) {
			for (int bx = 0; bx < rowBlocks; bx++) {
				const size_t offset = ((size_t)by * rowBlocks + bx) * blockBytes;
				if (offset + blockBytes > size) {
					return;
				}
				if (memcmp(mem + offset, raw + offset, blockBytes) != 0) {
					mark(by * blockH, (by + 1) * blockH, bx * blockW, (bx + 1) * blockW);
				}
			}
		}
		return;
	}

	const size_t rowBytes = (size_t)variant.bufw * bpp / 8;
	const size_t usedBytes = std::min(((size_t)texW * bpp + 7) / 8, rowBytes);
	for (int y = 0; y < texH; y++) {
		const size_t offset = (size_t)y * rowBytes;
		if (offset >= size || usedBytes == 0) {
			break;
		}
		const size_t n = std::min(usedBytes, size - offset);
		const u8 *a = mem + offset;
		const u8 *b = raw + offset;
		if (memcmp(a, b, n) == 0) {
			continue;
		}
		size_t first = 0;
		while (a[first] == b[first]) {
			first++;
		}
		size_t last = n - 1;
		while (a[last] == b[last]) {
			last--;
		}
		mark(y, y + 1, (int)(first * 8 / bpp), (int)(((last + 1) * 8 + bpp - 1) / bpp));
	}
}

bool TextureReplacer::ShadowRegionStable(const ShadowVariant &variant, const ShadowRegion &region) const {
	if (variant.volatileX0.empty()) {
		return true;
	}
	const int y1 = std::min(region.Bottom(), (int)variant.volatileX0.size());
	for (int y = region.y; y < y1; y++) {
		if (variant.volatileX0[y] < variant.volatileX1[y] && variant.volatileX0[y] < region.Right() && variant.volatileX1[y] > region.x) {
			return false;
		}
	}
	return true;
}

bool TextureReplacer::ShadowTexel(const ShadowVariant &variant, int x, int y, u32 *value) const {
	const GETextureFormat fmt = variant.fmt;
	if (fmt == GE_TFMT_DXT1 || fmt == GE_TFMT_DXT3 || fmt == GE_TFMT_DXT5) {
		return false;
	}
	const int bpp = textureBitsPerPixel[fmt];
	const size_t rowBytes = (size_t)variant.bufw * bpp / 8;
	const size_t byteX = (size_t)x * bpp / 8;
	size_t offset;
	if (variant.swizzled) {
		// Blocks of 16 bytes by 8 rows, a row of blocks after another.
		const size_t rowBlocks = rowBytes / 16;
		offset = (((size_t)y / 8) * rowBlocks + byteX / 16) * 128 + (y % 8) * 16 + byteX % 16;
	} else {
		offset = (size_t)y * rowBytes + byteX;
	}
	const size_t bytes = std::max(bpp / 8, 1);
	if (offset + bytes > variant.raw.size()) {
		return false;
	}
	const u8 *p = variant.raw.data() + offset;
	switch (bpp) {
	case 4: *value = (x & 1) ? (*p >> 4) : (*p & 0xF); break;
	case 8: *value = *p; break;
	case 16: *value = p[0] | (p[1] << 8); break;
	default: *value = p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); break;
	}
	return true;
}

bool TextureReplacer::ExtraAreaUniform(const ShadowVariant &variant, const ShadowRegion &larger, const ShadowRegion &smaller) const {
	bool first = true;
	u32 expected = 0;
	for (int y = larger.y; y < larger.Bottom(); y++) {
		for (int x = larger.x; x < larger.Right(); x++) {
			if (x >= smaller.x && x < smaller.Right() && y >= smaller.y && y < smaller.Bottom()) {
				continue;
			}
			u32 value;
			if (!ShadowTexel(variant, x, y, &value)) {
				return false;
			}
			if (first) {
				expected = value;
				first = false;
			} else if (value != expected) {
				return false;
			}
		}
	}
	return true;
}

TextureReplacer::ShadowRegion TextureReplacer::PickCovering(const std::vector<ShadowRegion> &covering, const ShadowVariant &variant) const {
	// The largest one without data that changed. If nothing changed while it waited, that's the largest, so a
	// sheet that fills the texture isn't cut.
	size_t best = covering.size() - 1;
	for (size_t i = 0; i < covering.size(); i++) {
		if (ShadowRegionStable(variant, covering[i])) {
			best = i;
			break;
		}
	}
	// But not if what it adds to a smaller one is empty (all transparent, or one color): that holds no image.
	for (size_t i = best + 1; i < covering.size(); i++) {
		if (covering[best].Covers(covering[i]) && ExtraAreaUniform(variant, covering[best], covering[i])) {
			best = i;
		}
	}
	return covering[best];
}

std::vector<TextureReplacer::ShadowRegion> TextureReplacer::PickDeclaredParts(const ShadowSlot &slot, const ShadowVariant &variant, const ShadowRegion &seen) const {
	const ShadowRegion full = FullRegion(variant.texW, variant.texH);
	auto coveringOf = [&](const ShadowRegion &what) {
		// slot.declared is largest first, so this is too.
		std::vector<ShadowRegion> covering;
		for (const ShadowRegion &r : slot.declared) {
			if (r.Covers(what, SHADOW_COVER_SLACK)) {
				covering.push_back(r);
			}
		}
		return covering;
	};

	// One declared region that holds all that's drawn.
	const std::vector<ShadowRegion> covering = coveringOf(seen);
	if (!covering.empty()) {
		return { PickCovering(covering, variant) };
	}
	if (seen == full) {
		return { full };
	}

	// Several, for an image in parts. First the combinations from the ini or earlier dumps.
	std::vector<ShadowRegion> drawnParts = variant.drawnParts;
	if (drawnParts.empty()) {
		drawnParts.push_back(seen);
	}
	for (const std::vector<ShadowRegion> &combo : slot.declaredCombos) {
		bool holds = true;
		for (const ShadowRegion &d : drawnParts) {
			bool inOne = false;
			for (const ShadowRegion &c : combo) {
				inOne = inOne || c.Covers(d, SHADOW_COVER_SLACK);
			}
			holds = holds && inOne;
		}
		if (holds) {
			return combo;
		}
	}

	// Otherwise a declared region for each part drawn.
	std::vector<ShadowRegion> parts;
	for (const ShadowRegion &d : drawnParts) {
		const std::vector<ShadowRegion> partCovering = coveringOf(d);
		if (partCovering.empty()) {
			// It's drawn beyond all of them, so none would show all of it.
			return { full };
		}
		const ShadowRegion picked = PickCovering(partCovering, variant);
		if (std::find(parts.begin(), parts.end(), picked) == parts.end()) {
			parts.push_back(picked);
		}
	}
	// One inside another is just the other.
	std::vector<ShadowRegion> result;
	for (const ShadowRegion &p : parts) {
		bool inOther = false;
		for (const ShadowRegion &q : parts) {
			inOther = inOther || (!(p == q) && q.Covers(p));
		}
		if (!inOther) {
			result.push_back(p);
		}
	}
	// In a fixed order, so the same parts always hash the same.
	std::sort(result.begin(), result.end(), [](const ShadowRegion &a, const ShadowRegion &b) {
		return a.y != b.y ? a.y < b.y : a.x < b.x;
	});
	return result;
}

void TextureReplacer::NoteDumpRegion(ShadowSlot &slot, const std::vector<ShadowRegion> &parts) {
	if (parts.size() > 1) {
		if (std::find(slot.declaredCombos.begin(), slot.declaredCombos.end(), parts) == slot.declaredCombos.end()) {
			slot.declaredCombos.push_back(parts);
			shadowRangesDirty_ = true;
		}
		return;
	}
	// Learned, so the dump's key is found again (declared textures already have theirs).
	const ShadowRegion &region = parts[0];
	const bool inHashRange = std::find(slot.hashRange.begin(), slot.hashRange.end(), region) != slot.hashRange.end();
	if (!slot.IsDeclared() && !inHashRange && AddShadowRegion(slot.learned, region)) {
		shadowRangesDirty_ = true;
	}
}

void TextureReplacer::CommitShadow(ShadowVariant &variant) {
	shadowMemoryUsed_ -= std::min(shadowMemoryUsed_, variant.memoryUsed);
	if (variant.indexed ? variant.indices.empty() : (variant.numLevels == 0 || variant.levels[0].rgba.empty())) {
		return;
	}

	if (shadowMinVisibleSeconds_ > 0.0 && (!variant.drawn || variant.lastDrawn - variant.firstDrawn < shadowMinVisibleSeconds_)) {
		// Replaced before it was shown for long, like each letter of text that's typed out.
		DEBUG_LOG(Log::TexReplacement, "Shadow texture %08x %dx%d not dumped, it was only shown for %0.2fs", variant.addr, variant.texW, variant.texH, variant.lastDrawn - variant.firstDrawn);
		return;
	}

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(variant.addr, variant.texW, variant.texH)];

	const ShadowRegion full = FullRegion(variant.texW, variant.texH);
	ShadowRegion seen = full;
	if (variant.drawn && !variant.unbounded && !variant.seen.Empty()) {
		seen = variant.seen;
	}

	ShadowRegion region = seen;
	std::vector<ShadowRegion> parts;
	if (slot.IsDeclared()) {
		parts = PickDeclaredParts(slot, variant, seen);
		region = PartsBounds(parts);
	} else {
		// Each region is one more hash to try for every new content of this texture, so there's a limit.
		std::vector<ShadowRegion> known = slot.learned;
		for (const ShadowRegion &r : slot.hashRange) {
			AddShadowRegion(known, r);
		}
		const bool knownRegion = region == full || std::find(known.begin(), known.end(), region) != known.end();
		if (!knownRegion && (int)known.size() >= shadowMaxRegions_) {
			const ShadowRegion *best = nullptr;
			for (const ShadowRegion &r : known) {
				if (r.Covers(region) && (!best || r.Area() < best->Area())) {
					best = &r;
				}
			}
			region = best ? *best : full;
		}
		parts.push_back(region);
	}

	const u32 hash = HashShadowParts(variant.raw.data(), (u32)variant.raw.size(), variant.bufw, parts, variant.fmt);
	if (variant.indexed) {
		CommitShadowIndexed(variant, parts, hash);
		return;
	}
	const std::string regionText = PartsString(parts);
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

		// The image goes from (0,0) to the bottom right of the region, scaled like the level. ReplacedTexture undoes this.
		const int saveW = std::clamp((int)((u64)region.Right() * src.w / variant.texW), 1, src.w);
		const int saveH = std::clamp((int)((u64)region.Bottom() * src.h / variant.texH), 1, src.h);

		constexpr int bpp = 4;
		u8 *saveBuf = (u8 *)malloc((size_t)saveW * saveH * bpp);
		if (!saveBuf) {
			ERROR_LOG(Log::TexReplacement, "Failed to allocate %d bytes of memory for saving a texture", saveW * saveH * bpp);
			break;
		}
		for (int y = 0; y < saveH; y++) {
			memcpy(saveBuf + (size_t)y * saveW * bpp, src.rgba.data() + (size_t)y * src.w * bpp, (size_t)saveW * bpp);
		}
		if (parts.size() > 1) {
			// What's between the parts isn't part of the image, and changes. Transparent, so it's clear.
			u32 *pixels = (u32 *)saveBuf;
			for (int y = 0; y < saveH; y++) {
				const int ty = (int)((u64)y * variant.texH / src.h);
				for (int x = 0; x < saveW; x++) {
					if (!InParts(parts, (int)((u64)x * variant.texW / src.w), ty)) {
						pixels[(size_t)y * saveW + x] = 0;
					}
				}
			}
		}

		std::string hashfile;
		if (!names.empty()) {
			hashfile = names[std::min(level, (int)names.size() - 1)];
		} else {
			// Keeps the address with ignoreAddress, see NotifyTextureDecoded.
			hashfile = HashName(ReplacementCacheKey(variant.cachekey, hash), level) + ".png";
		}

		SaveTextureTask *task = new SaveTextureTask(saveBuf);
		task->filename = basePath_ / hashfile;
		task->saveFilename = newTextureDir_ / hashfile;
		task->w = saveW;
		task->h = saveH;
		task->replacedInfoHash = hash;
		g_threadManager.EnqueueTask(task);
		LogDump(hashfile, variant.addr, ReplacementCacheKey(variant.sourceCachekey, hash), variant.texW, variant.texH, variant.fmt, regionText.c_str());

		saveData.levelW[level] = saveW;
		saveData.levelH[level] = saveH;
		saveData.levelSaved[level] = true;
	}
	saveData.lastTimeSaved = time_now_d();

	NoteDumpRegion(slot, parts);
	INFO_LOG(Log::TexReplacement, "Shadow texture %08x %dx%d dumped with region %s", variant.addr, variant.texW, variant.texH, regionText.c_str());
}

void TextureReplacer::CommitShadowIndexed(ShadowVariant &variant, const std::vector<ShadowRegion> &parts, u32 hash) {
	const ShadowRegion region = PartsBounds(parts);
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
		// variant.cachekey has the address (no CLUT hash), see NotifyTextureDecoded.
		hashfile = HashName(ReplacementCacheKey(variant.cachekey, hash), 0) + "_indexed.png";
	}

	// Indices aren't scaled, so this is in texels.
	const int saveW = std::clamp(region.Right(), 1, variant.texW);
	const int saveH = std::clamp(region.Bottom(), 1, variant.texH);
	u8 *saveBuf = (u8 *)malloc((size_t)saveW * saveH);
	if (!saveBuf) {
		ERROR_LOG(Log::TexReplacement, "Failed to allocate %d bytes of memory for saving a texture", saveW * saveH);
		return;
	}
	for (int y = 0; y < saveH; y++) {
		memcpy(saveBuf + (size_t)y * saveW, variant.indices.data() + (size_t)y * variant.texW, saveW);
	}
	if (parts.size() > 1) {
		// What's between the parts isn't part of the image. Index 0, often transparent.
		for (int y = 0; y < saveH; y++) {
			for (int x = 0; x < saveW; x++) {
				if (!InParts(parts, x, y)) {
					saveBuf[(size_t)y * saveW + x] = 0;
				}
			}
		}
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
	const std::string regionText = PartsString(parts);
	LogDump(hashfile, variant.addr, ReplacementCacheKey(variant.sourceCachekey, hash), variant.texW, variant.texH, variant.fmt, regionText.c_str());

	saveData.levelW[0] = saveW;
	saveData.levelH[0] = saveH;
	saveData.levelSaved[0] = true;
	saveData.lastTimeSaved = time_now_d();

	ShadowSlot &slot = shadowSlots_[ShadowSlotKey(variant.addr, variant.texW, variant.texH)];
	NoteDumpRegion(slot, parts);
	INFO_LOG(Log::TexReplacement, "Shadow texture %08x %dx%d dumped as indices with region %s", variant.addr, variant.texW, variant.texH, regionText.c_str());
}

void TextureReplacer::CommitAllShadows() {
	for (auto &[key, variant] : shadowVariants_) {
		CommitShadow(variant);
	}
	shadowVariants_.clear();
	shadowKeys_.clear();
	shadowMemoryUsed_ = 0;
	if (shadowRangesDirty_) {
		SaveShadowRanges();
	}
}

void TextureReplacer::ShadowTick() {
	if (!ShadowEnabled()) {
		return;
	}
	const double now = time_now_d();
	shadowNow_ = now;
	if (now - shadowLastTick_ < 0.5) {
		return;
	}
	shadowLastTick_ = now;

	// What keys of contents that are long gone sampled isn't needed anymore.
	for (auto it = shadowKeys_.begin(); it != shadowKeys_.end(); ) {
		if (now - it->second.lastUsed > 60.0) {
			it = shadowKeys_.erase(it);
		} else {
			++it;
		}
	}

	if (!saveEnabled_) {
		return;
	}

	for (auto it = shadowVariants_.begin(); it != shadowVariants_.end(); ) {
		CheckShadowStability(it->second);
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
		if (slot.IsDeclared() || !slot.learned.empty()) {
			sorted[key] = &slot;
		}
	}

	auto regionLine = [&](u32 addr, int w, int h, const std::vector<ShadowRegion> &regions, const std::vector<std::vector<ShadowRegion>> &combos) {
		std::string line = StringFromFormat("0x%08x,%d,%d = ", addr, w, h);
		std::vector<std::string> items;
		for (const ShadowRegion &r : regions) {
			items.push_back(ShadowRegionString(r));
		}
		for (const std::vector<ShadowRegion> &combo : combos) {
			items.push_back(PartsString(combo));
		}
		for (size_t i = 0; i < items.size(); i++) {
			if (i != 0) {
				line += "|";
			}
			line += items[i];
		}
		return line + "\n";
	};

	std::string declared;
	std::string learned;
	for (const auto &[key, slot] : sorted) {
		const u32 addr = (u32)(key >> 32);
		const int w = (int)((key >> 16) & 0xFFFF);
		const int h = (int)(key & 0xFFFF);
		if (slot->IsDeclared()) {
			declared += regionLine(addr, w, h, slot->declared, slot->declaredCombos);
			continue;
		}
		// A texture that was only ever dumped whole needs no line, the whole texture is always tried.
		const ShadowRegion full = FullRegion(w, h);
		if (slot->learned.size() == 1 && slot->learned[0] == full) {
			continue;
		}
		learned += regionLine(addr, w, h, slot->learned, {});
	}

	const Path filename = newTextureDir_ / SHADOW_RANGES_FILENAME;
	FILE *f = File::OpenCFile(filename, "wb");
	if (!f) {
		ERROR_LOG(Log::TexReplacement, "Failed to write %s", filename.c_str());
		return;
	}
	fprintf(f, "# Written while dumping with shadowTextures = true, from textures.ini and the dumps of this session.\n");
	fprintf(f, "# Replace the [shadowranges] and [learnedshadowranges] sections of textures.ini with these.\n");
	fprintf(f, "#\n");
	fprintf(f, "# [shadowranges] are the ones in textures.ini, with the same regions, and the combinations of them (joined with +)\n");
	fprintf(f, "# dumps of an image in parts used. Dumps of those textures only use these.\n");
	fprintf(f, "# [learnedshadowranges] are the regions dumps were made with. They're needed to find the replacements for\n");
	fprintf(f, "# those dumps again. Check them, and move a line to [shadowranges] to set that texture's regions yourself.\n\n");
	fprintf(f, "[shadowranges]\n%s\n[learnedshadowranges]\n%s", declared.c_str(), learned.c_str());
	fclose(f);
}

// Layered replacement.
//
// Some games draw text into a texture on the CPU, one letter at a time, over a background like a dialog box.
// Every letter makes a new texture. Instead of replacing each, the background is replaced once: the texture
// without text (as dumped) and an HD version of it are listed in [layered]. The texels where the current
// contents differ from the original are the text (and its shadow), and are drawn over the HD image, scaled up.

void TextureReplacer::ParseLayered(const std::string &key, const std::string &value) {
	std::vector<std::string> keyParts;
	SplitString(key, ',', keyParts, true);
	std::vector<std::string> files;
	SplitString(value, '|', files, true);
	if (keyParts.size() != 3 || files.size() != 2 || files[0].empty() || files[1].empty()) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid layered %s = %s, expecting addr,w,h = original.png|hd.png", key.c_str(), value.c_str());
		return;
	}
	if (!startsWith(keyParts[0], "0x") && !startsWith(keyParts[0], "0X")) {
		keyParts[0] = "0x" + keyParts[0];
	}
	u32 addr;
	u32 w;
	u32 h;
	if (!TryParse(keyParts[0], &addr) || !TryParse(keyParts[1], &w) || !TryParse(keyParts[2], &h) || w == 0 || h == 0 || w > 1024 || h > 1024) {
		ERROR_LOG(Log::TexReplacement, "Ignoring invalid layered %s = %s, key format is 0x12345678,512,512", key.c_str(), value.c_str());
		return;
	}
	if (HasParentDirComponent(files[0]) || HasParentDirComponent(files[1])) {
		ERROR_LOG(Log::TexReplacement, "Ignoring layered %s with a parent dir component", key.c_str());
		return;
	}

	LayeredSlot &slot = layered_[ShadowSlotKey(addr & 0x3FFFFFFF, w, h)];
	slot.originalFile = files[0];
	slot.hdFile = files[1];
}

bool TextureReplacer::IsLayered(u32 addr, int w, int h) const {
	if (!replaceEnabled_ || layered_.empty()) {
		return false;
	}
	auto it = layered_.find(ShadowSlotKey(addr & 0x3FFFFFFF, w, h));
	return it != layered_.end() && !it->second.failed;
}

static bool LoadPNGFromVFS(VFSBackend *vfs, const std::string &filename, std::vector<u32> *pixels, int *w, int *h) {
	if (!vfs) {
		return false;
	}
	size_t size = 0;
	uint8_t *data = vfs->ReadFile(filename, &size);
	if (!data) {
		return false;
	}
	unsigned char *image = nullptr;
	const int result = pngLoadPtr(data, size, w, h, &image);
	delete[] data;
	if (result != 1 || !image) {
		return false;
	}
	pixels->resize((size_t)*w * *h);
	memcpy(pixels->data(), image, pixels->size() * sizeof(u32));
	free(image);
	return true;
}

bool TextureReplacer::LoadLayered(LayeredSlot &slot) {
	slot.loaded = true;
	int ow = 0;
	int oh = 0;
	int hw = 0;
	int hh = 0;
	if (!LoadPNGFromVFS(vfs_, slot.originalFile, &slot.original, &ow, &oh) || !LoadPNGFromVFS(vfs_, slot.hdFile, &slot.hd, &hw, &hh)) {
		ERROR_LOG(Log::TexReplacement, "Layered: failed to load %s or %s", slot.originalFile.c_str(), slot.hdFile.c_str());
		slot.failed = true;
		return false;
	}
	const int scale = ow > 0 ? hw / ow : 0;
	if (scale < 1 || scale > 16 || hw != ow * scale || hh != oh * scale) {
		ERROR_LOG(Log::TexReplacement, "Layered: %s (%dx%d) must be %s (%dx%d) scaled by a whole number", slot.hdFile.c_str(), hw, hh, slot.originalFile.c_str(), ow, oh);
		slot.failed = true;
		return false;
	}
	slot.w = ow;
	slot.h = oh;
	slot.scale = scale;
	return true;
}

// Letters bigger than this (in texels) aren't treated as glyphs, which also keeps a fade of the whole box out.
static constexpr int LAYERED_MAX_GLYPH_SIZE = 64;

const TextureReplacer::LayeredGlyph &TextureReplacer::LookupLayeredGlyph(u64 key) {
	auto it = layeredGlyphCache_.find(key);
	if (it != layeredGlyphCache_.end()) {
		return it->second;
	}
	// Looked for once, found or not.
	LayeredGlyph &glyph = layeredGlyphCache_[key];
	const std::string filename = StringFromFormat("glyphs/%016llx.png", (unsigned long long)key);
	glyph.found = LoadPNGFromVFS(vfs_, filename, &glyph.pixels, &glyph.w, &glyph.h);
	return glyph;
}

void TextureReplacer::SaveLayeredGlyph(u64 key, const u32 *pixels, int w, int h) {
	if (!saveEnabled_ || newTextureDir_.empty() || !savedLayeredGlyphs_.insert(key).second) {
		return;
	}
	u8 *saveBuf = (u8 *)malloc((size_t)w * h * sizeof(u32));
	if (!saveBuf) {
		return;
	}
	memcpy(saveBuf, pixels, (size_t)w * h * sizeof(u32));
	const std::string filename = StringFromFormat("glyphs/%016llx.png", (unsigned long long)key);
	SaveTextureTask *task = new SaveTextureTask(saveBuf);
	// Not written if the pack has it.
	task->filename = basePath_ / filename;
	task->saveFilename = newTextureDir_ / filename;
	task->w = w;
	task->h = h;
	task->replacedInfoHash = (u32)key;
	g_threadManager.EnqueueTask(task);
}

// Source over, for drawing an HD glyph with its transparency on the HD background.
static inline u32 BlendOver(u32 src, u32 dst) {
	const u32 a = src >> 24;
	if (a == 255) {
		return src;
	}
	if (a == 0) {
		return dst;
	}
	u32 result = 0;
	for (int shift = 0; shift < 24; shift += 8) {
		const u32 s = (src >> shift) & 0xFF;
		const u32 d = (dst >> shift) & 0xFF;
		result |= ((s * a + d * (255 - a) + 127) / 255) << shift;
	}
	const u32 da = dst >> 24;
	result |= (a + da * (255 - a) / 255) << 24;
	return result;
}

ReplacedTexture *TextureReplacer::ComposeLayered(u32 addr, int w, int h, const u32 *pixels, TextureScalerCommon *scaler) {
	auto it = layered_.find(ShadowSlotKey(addr & 0x3FFFFFFF, w, h));
	if (it == layered_.end() || it->second.failed) {
		return nullptr;
	}
	LayeredSlot &slot = it->second;
	if (!slot.loaded && !LoadLayered(slot)) {
		return nullptr;
	}
	if (slot.w > w || slot.h > h) {
		ERROR_LOG(Log::TexReplacement, "Layered: %s (%dx%d) is bigger than the texture (%dx%d)", slot.originalFile.c_str(), slot.w, slot.h, w, h);
		slot.failed = true;
		return nullptr;
	}

	const int scale = slot.scale;
	const int sw = slot.w;
	const int sh = slot.h;
	const int outW = sw * scale;
	const int outH = sh * scale;

	if (!slot.texture) {
		// Made here rather than loaded, so it's ACTIVE from the start and never purged (it has no files to reload from).
		ReplacementDesc desc{};
		desc.w = w;
		desc.h = h;
		desc.newW = sw;
		desc.newH = sh;
		desc.cacheKey = ReplacementCacheKey((u64)(addr & 0x3FFFFFFF) << 32, 0);
		desc.forceFiltering = (TextureFiltering)0;
		desc.logId = slot.hdFile;
		desc.basePath = basePath_;
		desc.formatSupport = formatSupport_;
		ReplacedTexture *texture = new ReplacedTexture(vfs_, desc);
		texture->fmt = Draw::DataFormat::R8G8B8A8_UNORM;
		ReplacedTextureLevel level;
		level.w = outW;
		level.h = outH;
		level.fullW = (outW * w) / sw;
		level.fullH = (outH * h) / sh;
		level.fullDataSize = level.fullW * level.fullH * 4;
		texture->levels_.push_back(level);
		texture->data_.resize(1);
		texture->data_[0].resize((size_t)outW * outH * 4);
		texture->alphaStatus_ = TextureAlpha::Any;
		texture->SetState(ReplacementState::ACTIVE);
		slot.texture = texture;
	}

	// The text: texels that differ from the original. Each connected piece of them (8 neighbors, so a letter
	// and its shadow stay together) is drawn as one glyph.
	const size_t count = (size_t)sw * sh;
	std::vector<u8> isText(count);
	for (int y = 0; y < sh; y++) {
		const u32 *src = pixels + (size_t)y * w;
		const u32 *orig = slot.original.data() + (size_t)y * sw;
		for (int x = 0; x < sw; x++) {
			isText[(size_t)y * sw + x] = src[x] != orig[x];
		}
	}

	struct Piece {
		int x0, y0, x1, y1;  // Inclusive.
		std::vector<int> texels;
		const LayeredGlyph *glyph = nullptr;
	};
	std::vector<Piece> pieces;
	std::vector<int> label(count, -1);
	std::vector<int> stack;
	for (size_t start = 0; start < count; start++) {
		if (!isText[start] || label[start] >= 0) {
			continue;
		}
		Piece piece;
		piece.x0 = piece.x1 = (int)(start % sw);
		piece.y0 = piece.y1 = (int)(start / sw);
		label[start] = (int)pieces.size();
		stack.push_back((int)start);
		while (!stack.empty()) {
			const int i = stack.back();
			stack.pop_back();
			piece.texels.push_back(i);
			const int px = i % sw;
			const int py = i / sw;
			piece.x0 = std::min(piece.x0, px);
			piece.x1 = std::max(piece.x1, px);
			piece.y0 = std::min(piece.y0, py);
			piece.y1 = std::max(piece.y1, py);
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++) {
					const int nx = px + dx;
					const int ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= sw || ny >= sh) {
						continue;
					}
					const int n = ny * sw + nx;
					if (isText[n] && label[n] < 0) {
						label[n] = (int)pieces.size();
						stack.push_back(n);
					}
				}
			}
		}
		pieces.push_back(std::move(piece));
	}

	// Find an HD glyph for each piece, and dump the ones the pack doesn't have.
	bool anyUnknown = false;
	for (size_t n = 0; n < pieces.size(); n++) {
		Piece &piece = pieces[n];
		const int bw = piece.x1 - piece.x0 + 1;
		const int bh = piece.y1 - piece.y0 + 1;
		if (layeredGlyphs_ && bw <= LAYERED_MAX_GLYPH_SIZE && bh <= LAYERED_MAX_GLYPH_SIZE) {
			// Keyed by its shape and main color. Blended texels (like a shadow over a textured background) change
			// with where the letter is drawn, its shape doesn't.
			std::vector<u8> shape((size_t)bw * bh);
			std::vector<u32> image((size_t)bw * bh);
			std::unordered_map<u32, int> colors;
			for (int i : piece.texels) {
				const int lx = i % sw - piece.x0;
				const int ly = i / sw - piece.y0;
				const u32 color = pixels[(size_t)(i / sw) * w + i % sw];
				shape[(size_t)ly * bw + lx] = 1;
				image[(size_t)ly * bw + lx] = color;
				colors[color]++;
			}
			u32 mainColor = 0;
			int mainCount = 0;
			for (const auto &[color, num] : colors) {
				if (num > mainCount || (num == mainCount && color < mainColor)) {
					mainColor = color;
					mainCount = num;
				}
			}
			const u64 seed = ((u64)bw << 48) ^ ((u64)bh << 32) ^ mainColor;
			const u64 key = XXH64(shape.data(), shape.size(), seed);

			const LayeredGlyph &glyph = LookupLayeredGlyph(key);
			if (glyph.found && glyph.w == bw * scale && glyph.h == bh * scale) {
				piece.glyph = &glyph;
			} else {
				if (glyph.found) {
					WARN_LOG_N_TIMES(layeredglyphsize, 5, Log::TexReplacement, "Layered: glyphs/%016llx.png is %dx%d, should be %dx%d", (unsigned long long)key, glyph.w, glyph.h, bw * scale, bh * scale);
				}
				SaveLayeredGlyph(key, image.data(), bw, bh);
			}
		}
		if (!piece.glyph) {
			anyUnknown = true;
		}
	}

	ReplacedTexture *texture = slot.texture;
	std::lock_guard<std::mutex> guard(texture->lock_);
	u32 *out = (u32 *)texture->data_[0].data();
	memcpy(out, slot.hd.data(), (size_t)outW * outH * sizeof(u32));

	// Letters without an HD glyph: the game's texels, upscaled. Upscaling the whole texture and taking the
	// upscaled texels that differ from the upscaled original keeps the smoothed edges, without the background.
	const int scalerType = layeredTextScaler_;
	const bool upscale = anyUnknown && scaler && scalerType >= 0 && scale >= 2 && scale <= 5;
	std::vector<u32> scaledCurrent;
	if (upscale) {
		if (slot.scaledOriginalType != scalerType) {
			slot.scaledOriginal.resize((size_t)outW * outH);
			scaler->ScaleWithType(scalerType, slot.scaledOriginal.data(), slot.original.data(), sw, sh, scale);
			slot.scaledOriginalType = scalerType;
		}
		std::vector<u32> current(count);
		for (int y = 0; y < sh; y++) {
			memcpy(current.data() + (size_t)y * sw, pixels + (size_t)y * w, sw * sizeof(u32));
		}
		scaledCurrent.resize((size_t)outW * outH);
		scaler->ScaleWithType(scalerType, scaledCurrent.data(), current.data(), sw, sh, scale);
	}

	for (const Piece &piece : pieces) {
		if (piece.glyph) {
			continue;
		}
		if (upscale) {
			// A texel around the piece too, where the smoothing reaches.
			const int x0 = std::max(piece.x0 - 1, 0) * scale;
			const int y0 = std::max(piece.y0 - 1, 0) * scale;
			const int x1 = std::min(piece.x1 + 2, sw) * scale;
			const int y1 = std::min(piece.y1 + 2, sh) * scale;
			for (int y = y0; y < y1; y++) {
				const size_t row = (size_t)y * outW;
				for (int x = x0; x < x1; x++) {
					if (scaledCurrent[row + x] != slot.scaledOriginal[row + x]) {
						out[row + x] = scaledCurrent[row + x];
					}
				}
			}
		} else {
			for (int i : piece.texels) {
				const u32 color = pixels[(size_t)(i / sw) * w + i % sw];
				const int bx = (i % sw) * scale;
				const int by = (i / sw) * scale;
				for (int sy = 0; sy < scale; sy++) {
					u32 *row = out + (size_t)(by + sy) * outW + bx;
					for (int sx = 0; sx < scale; sx++) {
						row[sx] = color;
					}
				}
			}
		}
	}

	// Letters with an HD glyph, over the background (and any upscaled neighbors).
	for (const Piece &piece : pieces) {
		if (!piece.glyph) {
			continue;
		}
		const LayeredGlyph &glyph = *piece.glyph;
		const int bx = piece.x0 * scale;
		const int by = piece.y0 * scale;
		for (int y = 0; y < glyph.h; y++) {
			u32 *row = out + (size_t)(by + y) * outW + bx;
			const u32 *src = glyph.pixels.data() + (size_t)y * glyph.w;
			for (int x = 0; x < glyph.w; x++) {
				row[x] = BlendOver(src[x], row[x]);
			}
		}
	}
	return texture;
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
ignoreAddress = false    # Reduces duplicates at the cost of making hash less reliable, requires stronger hash like xxh32 or xxh64. Dumped filenames still have the address they came from, it's ignored when they're matched.
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
# keeps around an image doesn't produce new dumps. The regions dumps were made with are written to
# new/shadowranges.ini: copy its sections here after each dumping session.
# A region is w,h from the top left, or x,y,w,h. When replacing, the largest region with a replacement wins.
# [shadowranges] sets a texture's regions yourself. Its dumps then only use these, the largest one whose
# texels don't change while the image stays (so a full sheet isn't cut, and data after a smaller image is left out):
#[shadowranges]
#0x08b31020,256,256 = 256,256|256,240
# [learnedshadowranges] are the regions found while dumping, from new/shadowranges.ini:
#[learnedshadowranges]
#0x08b31020,512,512 = 480,272|0,16,480,120

# Experimental: for a texture the game draws text into (one letter at a time, over a background like a
# dialog box), list the texture without text as dumped, and an HD version of it scaled by a whole number.
# The text is then drawn over the HD image, for any text. Each letter is dumped once to new/glyphs/, and an
# HD version of it in glyphs/ is drawn instead of the game's. See docs/shadow-textures.md.
#[layered]
#0x08b31020,256,128 = box_original.png|box_hd.png

[ignore]
# Textures at these addresses are neither dumped nor replaced, also with ignoreAddress. An address, or a range
# (both ends included), optionally followed by a size. new/dumps.txt has the address and size of each dump.
# Example: 0x09a00000-0x09c00000 =
# Example: 0x08b31020,32,16 =

)", gameID.c_str(), INI_FILENAME.c_str());
		fclose(f);
	}
	return File::Exists(generatedFilename);
}

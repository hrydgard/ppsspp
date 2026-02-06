// Copyright (c) 2012- PPSSPP Project.

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

#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/I18n.h"
#include "Core/FileLoaders/CachingFileLoader.h"
#include "Core/FileLoaders/DiskCachingFileLoader.h"
#include "Core/FileLoaders/HTTPFileLoader.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/FileLoaders/RetryingFileLoader.h"
#include "Core/FileLoaders/ZipFileLoader.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/FileSystems/BlockDevices.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/PSPLoaders.h"
#include "Core/MemMap.h"
#include "Core/Loaders.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/Util/GameManager.h"

struct PVD {
	u8 type;
	char identifier[5];
	char version;
	char pad0;
	char systemId[32];  // PSP GAME normally
	char volumeId[32];  // In PSP games, this sometimes has the name of the game but far from always.
};

FileLoader *ConstructFileLoader(const Path &filename) {
	if (filename.Type() == PathType::HTTP) {
		FileLoader *baseLoader = new RetryingFileLoader(new HTTPFileLoader(filename));
		// For headless, avoid disk caching since it's usually used for tests that might mutate.
		if (!PSP_CoreParameter().headLess) {
			baseLoader = new DiskCachingFileLoader(baseLoader);
		}
		return new CachingFileLoader(baseLoader);
	}
	return new LocalFileLoader(filename);
}

// TODO : improve, look in the file more
// Does not take ownership.
IdentifiedFileType Identify_File(FileLoader *fileLoader, std::string *errorString) {
	errorString->clear();
	if (fileLoader == nullptr) {
		*errorString = "Invalid fileLoader";
		return IdentifiedFileType::ERROR_IDENTIFYING;
	}
	if (fileLoader->GetPath().size() == 0) {
		*errorString = "Invalid filename " + fileLoader->GetPath().ToString();
		return IdentifiedFileType::ERROR_IDENTIFYING;
	}

	if (!fileLoader->Exists()) {
		*errorString = "IdentifyFile: File doesn't exist: " + fileLoader->GetPath().ToString();
		return IdentifiedFileType::ERROR_IDENTIFYING;
	}

	std::string extension = fileLoader->GetFileExtension();

	bool isDiscImage = false;

	if (extension == ".iso" || extension == ".cso" || extension == ".chd") {
		isDiscImage = true;
	} else if (extension == ".ppst") {
		return IdentifiedFileType::PPSSPP_SAVESTATE;
	} else if (extension == ".ppdmp") {
		char data[8]{};
		fileLoader->ReadAt(0, 8, data);
		if (memcmp(data, "PPSSPPGE", 8) == 0) {
			return IdentifiedFileType::PPSSPP_GE_DUMP;
		}
	}

	// First, check if it's a directory with an EBOOT.PBP in it.
	if (!isDiscImage && fileLoader->IsDirectory()) {
		Path filename = fileLoader->GetPath();
		if (filename.size() > 4) {
			// Check for existence of EBOOT.PBP, as required for "Directory games".
			if (File::Exists(filename / "EBOOT.PBP")) {
				return IdentifiedFileType::PSP_PBP_DIRECTORY;
			}

			// check if it's a disc directory
			if (File::Exists(filename / "PSP_GAME")) {
				return IdentifiedFileType::PSP_DISC_DIRECTORY;
			}

			// Not that, okay, let's guess it's a savedata directory if it has a param.sfo...
			if (File::Exists(filename / "PARAM.SFO")) {
				return IdentifiedFileType::PSP_SAVEDATA_DIRECTORY;
			}
		}

		return IdentifiedFileType::NORMAL_DIRECTORY;
	}

	// OK, quick methods of identification for common types failed. Moving on to more expensive methods,
	// starting by reading the first few bytes.
	// This can be necessary for weird Android content storage path types, see issue #17462
	if (isDiscImage || fileLoader->FileSize() >= 0x8800) {
		// Do the quick check for PSP ISOs here.
		std::string bdError;
		std::unique_ptr<BlockDevice> bd(ConstructBlockDevice(fileLoader, &bdError));
		if (bd) {
			u8 block16[2048]{};
			bd->ReadBlock(16, (u8 *)block16);
			PVD pvd;
			memcpy(&pvd, block16, sizeof(PVD));
			if (!memcmp(pvd.identifier, "CD001", 5)) {
				// It's a valid DVD-style ISO file. Let's see which type.
				if (!memcmp(pvd.systemId, "PSP GAME", 8) || !memcmp(pvd.systemId, "\"PSP GAME\"", 10)) {
					// Yes, a known proper PSP game, let's get it going.
					return IdentifiedFileType::PSP_ISO;
				} else if (!memcmp(pvd.systemId, "UMD VIDEO", 9) || !memcmp(pvd.systemId, "UMD AUDIO", 9)) {
					// This is rare so being slightly slow here shouldn't be a problem. Let's go check for the presence of
					// actual game data.
					SequentialHandleAllocator hAlloc;
					ISOFileSystem umd(&hAlloc, bd.release());
					if (umd.GetFileInfo("/PSP_GAME").exists) {
						INFO_LOG(Log::Loader, "Found an UMD VIDEO disc with game data. Treating as game.");
						*errorString = "UMD Video with PSP GAME data";
						return IdentifiedFileType::PSP_ISO;
					}

					// UMD AUDIO exists technically, but in reality, not really? Let's map it to VIDEO since we support neither.
					return IdentifiedFileType::PSP_UMD_VIDEO_ISO;
				} else if (!memcmp(pvd.systemId, "PS3", 3)) {
					*errorString = "PS3 ISO";
					return IdentifiedFileType::PS3_ISO;
				} else if (!memcmp(pvd.systemId, "PLAYSTATION", 11)) {
					// Just do a size heuristic here to differentiate. There are better ways but slower.
					if (bd->GetUncompressedSize() > 800LL * 1024LL * 1024LL) {
						*errorString = "PS2 ISO";
						return IdentifiedFileType::PS2_ISO;
					}
					*errorString = "PSX ISO?";
					return IdentifiedFileType::PSX_ISO;
				} else {
					// Let's go check for PSP game data.
					SequentialHandleAllocator hAlloc;
					ISOFileSystem umd(&hAlloc, bd.release());
					if (umd.GetFileInfo("/PSP_GAME").exists) {
						INFO_LOG(Log::Loader, "PSP ISO with unknown system ID: %.32s: %s", pvd.systemId, fileLoader->GetPath().c_str());
						return IdentifiedFileType::PSP_ISO;
					}

					INFO_LOG(Log::Loader, "Unknown ISO with unknown system ID: %.32s: %s", pvd.systemId, fileLoader->GetPath().c_str());
					*errorString = StringFromFormat("ISO with unknown system ID: %.32s", pvd.systemId);
					return IdentifiedFileType::UNKNOWN_ISO;
				}
			}

			// Do extra check for PSX ISO
			// may be a psx iso, they have 2352 byte sectors. You never know what some people try to open
			if ((fileLoader->FileSize() % 2352) == 0) {
				unsigned char sync[12];
				fileLoader->ReadAt(0, 12, sync);

				// each sector in a mode2 image starts with these 12 bytes
				if (memcmp(sync, "\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00", 12) == 0) {
					*errorString = "ISO is a CD - likely PSX";  // Mode 2 CDs are used for PSX games
					return IdentifiedFileType::PSX_ISO;
				}
			}
		}

		if (isDiscImage) {
			if (!bdError.empty()) {
				*errorString = bdError;
			} else {
				auto sy = GetI18NCategory(I18NCat::SYSTEM);
				*errorString = sy->T("Not a PSP game");
			}
			return IdentifiedFileType::UNKNOWN_ISO;
		}
	}

	u32 id;

	size_t readSize = fileLoader->ReadAt(0, 4, 1, &id);
	if (readSize != 1) {
		*errorString = "Failed to read identification bytes";
		return IdentifiedFileType::ERROR_IDENTIFYING;
	}

	u32_le psar_offset = 0, psar_id = 0;
	if (!memcmp(&id, "PK\x03\x04", 4) || !memcmp(&id, "PK\x05\x06", 4) || !memcmp(&id, "PK\x07\x08", 4)) {
		return IdentifiedFileType::ARCHIVE_ZIP;
	} else if (!memcmp(&id, "\x00PBP", 4)) {
		fileLoader->ReadAt(0x24, 4, 1, &psar_offset);
		fileLoader->ReadAt(psar_offset, 4, 1, &psar_id);
		// Fall through to the below if chain.
	} else if (!memcmp(&id, "Rar!", 4)) {
		return IdentifiedFileType::ARCHIVE_RAR;
	} else if (!memcmp(&id, "\x37\x7A\xBC\xAF", 4)) {
		return IdentifiedFileType::ARCHIVE_7Z;
	}

	if (id == 'FLE\x7F') {
		Path filename = fileLoader->GetPath();
		// There are a few elfs misnamed as pbp (like Trig Wars), accept that. Also accept extension-less paths.
		if (extension == ".plf" || strstr(filename.GetFilename().c_str(), "BOOT.BIN") ||
			extension == ".elf" || extension == ".prx" || extension == ".pbp" || extension == "") {
			return IdentifiedFileType::PSP_ELF;
		}
		return IdentifiedFileType::UNKNOWN_ELF;
	} else if (id == 'PBP\x00') {
		// Do this PS1 eboot check FIRST before checking other eboot types.
		// It seems like some are malformed and slip through the PSAR check below.
		PBPReader pbp(fileLoader);
		if (pbp.IsValid() && !pbp.IsELF()) {
			std::vector<u8> sfoData;
			if (pbp.GetSubFile(PBP_PARAM_SFO, &sfoData)) {
				ParamSFOData paramSFO;
				paramSFO.ReadSFO(sfoData);
				// PS1 Eboots are supposed to use "ME" as their PARAM SFO category.
				// If they don't, and they're still malformed (e.g. PSISOIMG0000 isn't found), there's nothing we can do.
				if (paramSFO.GetValueString("CATEGORY") == "ME")
					return IdentifiedFileType::PSP_PS1_PBP;
			}
		}

		if (psar_id == 'MUPN') {
			return IdentifiedFileType::PSP_ISO_NP;
		}
		// PS1 PSAR begins with "PSISOIMG0000"
		if (psar_id == 'SISP') {
			return IdentifiedFileType::PSP_PS1_PBP;
		}

		// Let's check if we got pointed to a PBP within such a directory.
		// If so we just move up and return the directory itself as the game.
		// If loading from memstick...
		if (fileLoader->GetPath().FilePathContainsNoCase("PSP/GAME/")) {
			return IdentifiedFileType::PSP_PBP_DIRECTORY;
		}
		return IdentifiedFileType::PSP_PBP;
	} else if (extension == ".pbp") {
		ERROR_LOG(Log::Loader, "A PBP with the wrong magic number?");
		return IdentifiedFileType::PSP_PBP;
	} else if (extension == ".bin") {
		return IdentifiedFileType::UNKNOWN_BIN;
	} else if (extension == ".zip") {
		return IdentifiedFileType::ARCHIVE_ZIP;
	} else if (extension == ".rar") {
		return IdentifiedFileType::ARCHIVE_RAR;
	} else if (extension == ".r00") {
		return IdentifiedFileType::ARCHIVE_RAR;
	} else if (extension == ".r01") {
		return IdentifiedFileType::ARCHIVE_RAR;
	} else if (extension == ".7z") {
		return IdentifiedFileType::ARCHIVE_7Z;
	}
	return IdentifiedFileType::UNKNOWN;
}

FileLoader *ResolveFileLoaderTarget(FileLoader *fileLoader, IdentifiedFileType *fileType, std::string *errorString) {
	*fileType = Identify_File(fileLoader, errorString);
	if (*fileType == IdentifiedFileType::PSP_PBP_DIRECTORY) {
		const Path ebootFilename = ResolvePBPFile(fileLoader->GetPath());
		if (ebootFilename != fileLoader->GetPath()) {
			// Switch fileLoader to the actual EBOOT.
			delete fileLoader;
			fileLoader = ConstructFileLoader(ebootFilename);
			// Re-identify the file.
			*fileType = Identify_File(fileLoader, errorString);
		}
	} else if (*fileType == IdentifiedFileType::ARCHIVE_ZIP) {
		// Handle zip files, take automatic action depending on contents.
		// Can also return nullptr.
		ZipFileLoader *zipLoader = new ZipFileLoader(fileLoader);

		ZipFileInfo zipFileInfo{};
		DetectZipFileContents(zipLoader->GetZip(), &zipFileInfo);

		switch (zipFileInfo.contents) {
		case ZipFileContents::ISO_FILE:
		case ZipFileContents::FRAME_DUMP:
		{
			zipLoader->Initialize(zipFileInfo.isoFileIndex);
			// Re-identify the file.
			*fileType = Identify_File(zipLoader, errorString);
			return zipLoader;
		}
		default:
		{
			// Nothing runnable in file. Take the original loader back and return it.
			fileLoader = zipLoader->Steal();
			delete zipLoader;
			return fileLoader;
		}
		}
	}
	return fileLoader;
}

Path ResolvePBPDirectory(const Path &filename) {
	if (filename.GetFilename() == "EBOOT.PBP") {
		return filename.NavigateUp();
	} else {
		return filename;
	}
}

Path ResolvePBPFile(const Path &filename) {
	if (filename.GetFilename() != "EBOOT.PBP") {
		return filename / "EBOOT.PBP";
	} else {
		return filename;
	}
}

bool UmdReplace(const Path &filepath, FileLoader **fileLoader, std::string &error) {
	IFileSystem *currentUMD = pspFileSystem.GetSystem("disc0:");

	if (!currentUMD) {
		error = "has no disc";
		return false;
	}

	FileLoader *loadedFile = ConstructFileLoader(filepath);

	if (!loadedFile || !loadedFile->Exists()) {
		error = loadedFile ? (loadedFile->GetPath().ToVisualString() + " doesn't exist") : "no loaded file";
		delete loadedFile;
		return false;
	}
	UpdateLoadedFile(loadedFile);

	std::string errorString;
	IdentifiedFileType fileType;
	loadedFile = ResolveFileLoaderTarget(loadedFile, &fileType, &errorString);

	*fileLoader = loadedFile;

	switch (fileType) {
	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
	case IdentifiedFileType::PSP_DISC_DIRECTORY:
		if (!MountGameISO(loadedFile, &error)) {
			error = "mounting the replaced ISO failed: " + error;
			return false;
		}
		break;
	default:
		error = "Unsupported file type: " + std::string(IdentifiedFileTypeToString(fileType)) + " " + errorString;
		return false;
		break;
	}
	return true;
}

// Close the return value with ZipClose (if non-null, of course).
ZipContainer ZipOpenPath(const Path &fileName) {
	ZipContainer z(fileName);
	if (z == nullptr) {
		ERROR_LOG(Log::HLE, "Failed to open ZIP file '%s'", fileName.c_str());
	}
	return z;
}

void ZipClose(ZipContainer &z) {
	z.close();
}

bool DetectZipFileContents(const Path &fileName, ZipFileInfo *info) {
	ZipContainer z = ZipOpenPath(fileName);
	if (!z) {
		info->contents = ZipFileContents::UNKNOWN;
		return false;
	}
	DetectZipFileContents(z, info);
	return true;
}

static int countSlashes(const std::string &fileName, int *slashLocation) {
	int slashCount = 0;
	int lastSlashLocation = -1;
	if (slashLocation) {
		*slashLocation = -1;
	}
	for (size_t i = 0; i < fileName.size(); i++) {
		if (fileName[i] == '/') {
			slashCount++;
			if (slashLocation) {
				*slashLocation = lastSlashLocation;
				lastSlashLocation = (int)i;
			}
		}
	}

	return slashCount;
}

inline char asciitolower(char in) {
	if (in <= 'Z' && in >= 'A')
		return in - ('Z' - 'z');
	return in;
}

static bool ZipExtractFileToMemory(struct zip *z, int fileIndex, std::string *data) {
	struct zip_stat zstat;
	zip_stat_index(z, fileIndex, 0, &zstat);
	if (zstat.size == 0) {
		data->clear();
		return true;
	}

	size_t readSize = zstat.size;
	data->resize(readSize);

	zip_file *zf = zip_fopen_index(z, fileIndex, 0);
	if (!zf) {
		ERROR_LOG(Log::HLE, "Failed to zip_fopen_index file %d from zip", fileIndex);
		return false;
	}

	zip_int64_t retval = zip_fread(zf, data->data(), readSize);
	zip_fclose(zf);

	if (retval < 0 || retval < (int)readSize) {
		ERROR_LOG(Log::HLE, "Failed to read %d bytes from zip (%d) - archive corrupt?", (int)readSize, (int)retval);
		return false;
	} else {
		return true;
	}
}

void DetectZipFileContents(zip_t *z, ZipFileInfo *info) {
	int numFiles = zip_get_num_files(z);
	_dbg_assert_(numFiles >= 0);

	// Verify that this is a PSP zip file with the correct layout. We also try
	// to detect simple zipped ISO files, those we'll just "install" to the current
	// directory of the Games tab (where else?).
	bool isPSPMemstickGame = false;
	bool isZippedISO = false;
	bool isTexturePack = false;
	bool isSaveStates = false;
	bool isFrameDump = false;
	int stripChars = 0;
	int isoFileIndex = -1;
	int stripCharsTexturePack = -1;
	int textureIniIndex = -1;
	int filesInRoot = 0;
	int directoriesInRoot = 0;
	bool hasParamSFO = false;
	bool isExtractedISO = false;
	bool hasIcon0PNG = false;
	s64 totalFileSize = 0;

	// TODO: It might be cleaner to write separate detection functions, but this big loop doing it all at once
	// is quite convenient and makes it easy to add shared heuristics.
	for (int i = 0; i < numFiles; i++) {
		const char *fn = zip_get_name(z, i, 0);

		zip_stat_t stat{};
		zip_stat_index(z, i, 0, &stat);
		totalFileSize += stat.size;

		std::string zippedName = fn;
		std::transform(zippedName.begin(), zippedName.end(), zippedName.begin(),
			[](unsigned char c) { return asciitolower(c); });  // Not using std::tolower to avoid Turkish I->ı conversion.
		// Ignore macos metadata stuff
		if (startsWith(zippedName, "__macosx/")) {
			continue;
		}
		if (endsWith(zippedName, "/")) {
			// A directory. Not all zips bother including these.
			continue;
		}

		int prevSlashLocation = -1;
		int slashCount = countSlashes(zippedName, &prevSlashLocation);
		if (zippedName.find("eboot.pbp") != std::string::npos) {
			if (slashCount >= 1 && (!isPSPMemstickGame || prevSlashLocation < stripChars + 1)) {
				stripChars = prevSlashLocation + 1;
				isPSPMemstickGame = true;
			} else {
				INFO_LOG(Log::HLE, "Wrong number of slashes (%i) in '%s'", slashCount, fn);
			}
			// TODO: Extract icon and param.sfo from the pbp to be able to display it on the install screen.
		} else if (endsWith(zippedName, ".iso") || endsWith(zippedName, ".cso") || endsWith(zippedName, ".chd")) {
			if (slashCount <= 1) {
				// We only do this if the ISO file is in the root or one level down.
				isZippedISO = true;
				INFO_LOG(Log::HLE, "ISO found in zip: %s", zippedName.c_str());
				if (isoFileIndex != -1) {
					INFO_LOG(Log::HLE, "More than one ISO file found in zip. Ignoring additional ones.");
				} else {
					isoFileIndex = i;
					info->contentName = zippedName;
				}
			}
		} else if (zippedName.find("textures.ini") != std::string::npos) {
			int slashLocation = (int)zippedName.find_last_of('/');
			if (stripCharsTexturePack == -1 || slashLocation < stripCharsTexturePack + 1) {
				stripCharsTexturePack = slashLocation + 1;
				isTexturePack = true;
				textureIniIndex = i;
			}
		} else if (endsWith(zippedName, ".ppdmp")) {
			isFrameDump = true;
			isoFileIndex = i;
			info->contentName = zippedName;
		} else if (endsWith(zippedName, ".ppst")) {
			int slashLocation = (int)zippedName.find_last_of('/');
			if (stripChars == 0 || slashLocation < stripChars + 1) {
				stripChars = slashLocation + 1;
			}
			isSaveStates = true;
			info->gameTitle = fn;
		} else if (endsWith(zippedName, "psp_game/sysdir/eboot.bin") || endsWith(zippedName, "psp_game/sysdir/boot.bin")) {
			isExtractedISO = true;
		} else if (endsWith(zippedName, "/param.sfo")) {
			// Get the game name so we can display it.
			std::string paramSFOContents;
			if (ZipExtractFileToMemory(z, i, &paramSFOContents)) {
				ParamSFOData sfo;
				if (sfo.ReadSFO((const u8 *)paramSFOContents.data(), paramSFOContents.size())) {
					if (sfo.HasKey("TITLE")) {
						info->gameTitle = sfo.GetValueString("TITLE");
						info->savedataTitle = sfo.GetValueString("SAVEDATA_TITLE");
						char buff[20];
						strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&stat.mtime));
						info->mTime = buff;
						info->savedataDetails = sfo.GetValueString("SAVEDATA_DETAIL");
						info->savedataDir = sfo.GetValueString("SAVEDATA_DIRECTORY");  // should also be parsable from the path.
						hasParamSFO = true;
					}
				}
			}
		} else if (endsWith(zippedName, "/icon0.png")) {
			hasIcon0PNG = true;
		}
		if (slashCount == 0) {
			filesInRoot++;
		}
	}

	info->stripChars = stripChars;
	info->numFiles = numFiles;
	info->isoFileIndex = isoFileIndex;
	info->textureIniIndex = textureIniIndex;
	info->ignoreMetaFiles = false;
	info->totalFileSize = totalFileSize;

	// Priority ordering for detecting the various kinds of zip file content.s
	if (isPSPMemstickGame) {
		info->contents = ZipFileContents::PSP_GAME_DIR;
	} else if (isZippedISO) {
		info->contents = ZipFileContents::ISO_FILE;
	} else if (isTexturePack) {
		info->stripChars = stripCharsTexturePack;
		info->ignoreMetaFiles = true;
		info->contents = ZipFileContents::TEXTURE_PACK;
	} else if (stripChars == 0 && filesInRoot == 0 && hasParamSFO && hasIcon0PNG && !isExtractedISO) {
		// As downloaded from GameFAQs, for example.
		info->contents = ZipFileContents::SAVE_DATA;
	} else if (isFrameDump) {
		info->contents = ZipFileContents::FRAME_DUMP;
	} else if (isSaveStates) {
		info->contents = ZipFileContents::SAVE_STATES;
	} else if (isExtractedISO && hasParamSFO) {
		info->contents = ZipFileContents::EXTRACTED_GAME;
	} else {
		info->contents = ZipFileContents::UNKNOWN;
	}
}

const char *IdentifiedFileTypeToString(IdentifiedFileType type) {
	switch (type) {
	case IdentifiedFileType::ERROR_IDENTIFYING: return "ERROR_IDENTIFYING";
	case IdentifiedFileType::PSP_PBP_DIRECTORY: return "PSP_PBP_DIRECTORY";
	case IdentifiedFileType::PSP_PBP: return "PSP_PBP";
	case IdentifiedFileType::PSP_ELF: return "PSP_ELF";
	case IdentifiedFileType::PSP_ISO: return "PSP_ISO";
	case IdentifiedFileType::PSP_ISO_NP: return "PSP_ISO_NP";
	case IdentifiedFileType::PSP_DISC_DIRECTORY: return "PSP_DISC_DIRECTORY";
	case IdentifiedFileType::UNKNOWN_BIN: return "UNKNOWN_BIN";
	case IdentifiedFileType::UNKNOWN_ELF: return "UNKNOWN_ELF";
	case IdentifiedFileType::UNKNOWN_ISO: return "UNKNOWN_ISO";
	case IdentifiedFileType::ARCHIVE_RAR: return "ARCHIVE_RAR";
	case IdentifiedFileType::ARCHIVE_ZIP: return "ARCHIVE_ZIP";
	case IdentifiedFileType::ARCHIVE_7Z: return "ARCHIVE_7Z";
	case IdentifiedFileType::PSP_PS1_PBP: return "PSP_PS1_PBP";
	case IdentifiedFileType::PSX_ISO: return "PSX_ISO";
	case IdentifiedFileType::PS2_ISO: return "PS2_ISO";
	case IdentifiedFileType::PS3_ISO: return "PS3_ISO";
	case IdentifiedFileType::PSP_UMD_VIDEO_ISO: return "UMD_VIDEO";
	case IdentifiedFileType::NORMAL_DIRECTORY: return "NORMAL_DIRECTORY";
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY: return "PSP_SAVEDATA_DIRECTORY";
	case IdentifiedFileType::PPSSPP_SAVESTATE: return "PPSSPP_SAVESTATE";
	case IdentifiedFileType::PPSSPP_GE_DUMP: return "PPSSPP_GE_DUMP";
	case IdentifiedFileType::UNKNOWN: return "UNKNOWN";
	default: return "INVALID_TYPE";
	}
}

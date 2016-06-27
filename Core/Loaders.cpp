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
#include <cstdio>

#include "base/stringutil.h"
#include "file/file_util.h"
#include "Common/FileUtil.h"

#include "Core/FileLoaders/CachingFileLoader.h"
#include "Core/FileLoaders/DiskCachingFileLoader.h"
#include "Core/FileLoaders/HTTPFileLoader.h"
#include "Core/FileLoaders/LocalFileLoader.h"
#include "Core/FileLoaders/RetryingFileLoader.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/PSPLoaders.h"
#include "Core/MemMap.h"
#include "Core/Loaders.h"
#include "Core/System.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"

FileLoader *ConstructFileLoader(const std::string &filename) {
	if (filename.find("http://") == 0 || filename.find("https://") == 0)
		return new CachingFileLoader(new DiskCachingFileLoader(new RetryingFileLoader(new HTTPFileLoader(filename))));
	return new LocalFileLoader(filename);
}

// TODO : improve, look in the file more
IdentifiedFileType Identify_File(FileLoader *fileLoader) {
	if (fileLoader == nullptr) {
		ERROR_LOG(LOADER, "Invalid fileLoader");
		return FILETYPE_ERROR;
	}
	if (fileLoader->Path().size() == 0) {
		ERROR_LOG(LOADER, "Invalid filename %s", fileLoader->Path().c_str());
		return FILETYPE_ERROR;
	}

	if (!fileLoader->Exists()) {
		return FILETYPE_ERROR;
	}

	std::string extension = fileLoader->Extension();
	if (!strcasecmp(extension.c_str(), ".iso"))
	{
		// may be a psx iso, they have 2352 byte sectors. You never know what some people try to open
		if ((fileLoader->FileSize() % 2352) == 0)
		{
			unsigned char sync[12];
			fileLoader->ReadAt(0, 12, sync);

			// each sector in a mode2 image starts with these 12 bytes
			if (memcmp(sync,"\x00\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x00",12) == 0)
			{
				return FILETYPE_ISO_MODE2;
			}

			// maybe it also just happened to have that size, 
		}
		return FILETYPE_PSP_ISO;
	}
	else if (!strcasecmp(extension.c_str(),".cso"))
	{
		return FILETYPE_PSP_ISO;
	}
	else if (!strcasecmp(extension.c_str(),".ppst"))
	{
		return FILETYPE_PPSSPP_SAVESTATE;
	}

	// First, check if it's a directory with an EBOOT.PBP in it.
	if (fileLoader->IsDirectory()) {
		std::string filename = fileLoader->Path();
		if (filename.size() > 4) {
			// Check for existence of EBOOT.PBP, as required for "Directory games".
			if (File::Exists((filename + "/EBOOT.PBP").c_str())) {
				return FILETYPE_PSP_PBP_DIRECTORY;
			}

			// check if it's a disc directory
			if (File::Exists((filename + "/PSP_GAME").c_str())) {
				return FILETYPE_PSP_DISC_DIRECTORY;
			}

			// Not that, okay, let's guess it's a savedata directory if it has a param.sfo...
			if (File::Exists((filename + "/PARAM.SFO").c_str())) {
				return FILETYPE_PSP_SAVEDATA_DIRECTORY;
			}
		}

		return FILETYPE_NORMAL_DIRECTORY;
	}

	u32_le id;

	size_t readSize = fileLoader->ReadAt(0, 4, 1, &id);
	if (readSize != 1) {
		return FILETYPE_ERROR;
	}

	u32 psar_offset = 0, psar_id = 0;
	u32 _id = id;
	switch (_id) {
	case 'PBP\x00':
		fileLoader->ReadAt(0x24, 4, 1, &psar_offset);
		fileLoader->ReadAt(psar_offset, 4, 1, &psar_id);
		break;
	case '!raR':
		return FILETYPE_ARCHIVE_RAR;
	case '\x04\x03KP':
	case '\x06\x05KP':
	case '\x08\x07KP':
		return FILETYPE_ARCHIVE_ZIP;
	}

	if (id == 'FLE\x7F') {
		std::string filename = fileLoader->Path();
		// There are a few elfs misnamed as pbp (like Trig Wars), accept that.
		if (!strcasecmp(extension.c_str(), ".plf") || strstr(filename.c_str(),"BOOT.BIN") ||
				!strcasecmp(extension.c_str(), ".elf") || !strcasecmp(extension.c_str(), ".prx") ||
				!strcasecmp(extension.c_str(), ".pbp")) {
			return FILETYPE_PSP_ELF;
		}
		return FILETYPE_UNKNOWN_ELF;
	}
	else if (id == 'PBP\x00') {
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
					return FILETYPE_PSP_PS1_PBP;
			}
		}

		if (psar_id == 'MUPN') {
			return FILETYPE_PSP_ISO_NP;
		}
		// PS1 PSAR begins with "PSISOIMG0000"
		if (psar_id == 'SISP') {
			return FILETYPE_PSP_PS1_PBP;
		}

		// Let's check if we got pointed to a PBP within such a directory.
		// If so we just move up and return the directory itself as the game.
		std::string path = File::GetDir(fileLoader->Path());
		// If loading from memstick...
		size_t pos = path.find("/PSP/GAME/");
		if (pos != std::string::npos) {
			return FILETYPE_PSP_PBP_DIRECTORY;
		}
		return FILETYPE_PSP_PBP;
	}
	else if (!strcasecmp(extension.c_str(),".pbp")) {
		ERROR_LOG(LOADER, "A PBP with the wrong magic number?");
		return FILETYPE_PSP_PBP;
	} else if (!strcasecmp(extension.c_str(),".bin")) {
		return FILETYPE_UNKNOWN_BIN;
	} else if (!strcasecmp(extension.c_str(),".zip")) {
		return FILETYPE_ARCHIVE_ZIP;
	} else if (!strcasecmp(extension.c_str(),".rar")) {
		return FILETYPE_ARCHIVE_RAR;
	} else if (!strcasecmp(extension.c_str(),".r00")) {
		return FILETYPE_ARCHIVE_RAR;
	} else if (!strcasecmp(extension.c_str(),".r01")) {
		return FILETYPE_ARCHIVE_RAR;
	} else if (!extension.empty() && !strcasecmp(extension.substr(1).c_str(), ".7z")) {
		return FILETYPE_ARCHIVE_7Z;
	}
	return FILETYPE_UNKNOWN;
}

FileLoader *ResolveFileLoaderTarget(FileLoader *fileLoader) {
	IdentifiedFileType type = Identify_File(fileLoader);
	if (type == FILETYPE_PSP_PBP_DIRECTORY) {
		const std::string ebootFilename = ResolvePBPFile(fileLoader->Path());
		if (ebootFilename != fileLoader->Path()) {
			// Switch fileLoader to the actual EBOOT.
			delete fileLoader;
			fileLoader = ConstructFileLoader(ebootFilename);
		}
	}
	return fileLoader;
}

std::string ResolvePBPDirectory(const std::string &filename) {
	bool hasPBP = endsWith(filename, "/EBOOT.PBP");
#ifdef _WIN32
	hasPBP = hasPBP || endsWith(filename, "\\EBOOT.PBP");
#endif

	if (hasPBP) {
		return filename.substr(0, filename.length() - strlen("/EBOOT.PBP"));
	}
	return filename;
}

std::string ResolvePBPFile(const std::string &filename) {
	bool hasPBP = endsWith(filename, "/EBOOT.PBP");
#ifdef _WIN32
	hasPBP = hasPBP || endsWith(filename, "\\EBOOT.PBP");
#endif

	if (!hasPBP) {
		return filename + "/EBOOT.PBP";
	}
	return filename;
}

bool LoadFile(FileLoader **fileLoaderPtr, std::string *error_string) {
	FileLoader *&fileLoader = *fileLoaderPtr;
	// Note that this can modify filename!
	switch (Identify_File(fileLoader)) {
	case FILETYPE_PSP_PBP_DIRECTORY:
		{
			// TODO: Perhaps we should/can never get here now?
			fileLoader = ResolveFileLoaderTarget(fileLoader);
			if (fileLoader->Exists()) {
				INFO_LOG(LOADER, "File is a PBP in a directory!");
				IdentifiedFileType ebootType = Identify_File(fileLoader);
				if (ebootType == FILETYPE_PSP_ISO_NP) {
					InitMemoryForGameISO(fileLoader);
					pspFileSystem.SetStartingDirectory("disc0:/PSP_GAME/USRDIR");
					return Load_PSP_ISO(fileLoader, error_string);
				}
				else if (ebootType == FILETYPE_PSP_PS1_PBP) {
					*error_string = "PS1 EBOOTs are not supported by PPSSPP.";
					return false;
				}
				std::string path = fileLoader->Path();
				size_t pos = path.find("/PSP/GAME/");
				if (pos != std::string::npos) {
					path = ResolvePBPDirectory(path);
					pspFileSystem.SetStartingDirectory("ms0:" + path.substr(pos));
				}
				return Load_PSP_ELF_PBP(fileLoader, error_string);
			} else {
				*error_string = "No EBOOT.PBP, misidentified game";
				return false;
			}
		}

	case FILETYPE_PSP_PBP:
	case FILETYPE_PSP_ELF:
		{
			INFO_LOG(LOADER,"File is an ELF or loose PBP!");
			return Load_PSP_ELF_PBP(fileLoader, error_string);
		}

	case FILETYPE_PSP_ISO:
	case FILETYPE_PSP_ISO_NP:
	case FILETYPE_PSP_DISC_DIRECTORY:	// behaves the same as the mounting is already done by now
		pspFileSystem.SetStartingDirectory("disc0:/PSP_GAME/USRDIR");
		return Load_PSP_ISO(fileLoader, error_string);

	case FILETYPE_PSP_PS1_PBP:
		*error_string = "PS1 EBOOTs are not supported by PPSSPP.";
		break;

	case FILETYPE_ERROR:
		ERROR_LOG(LOADER, "Could not read file");
		*error_string = "Error reading file";
		break;

	case FILETYPE_ARCHIVE_RAR:
#ifdef WIN32
		*error_string = "RAR file detected (Require WINRAR)";
#else
		*error_string = "RAR file detected (Require UnRAR)";
#endif
		break;

	case FILETYPE_ARCHIVE_ZIP:
#ifdef WIN32
		*error_string = "ZIP file detected (Require WINRAR)";
#else
		*error_string = "ZIP file detected (Require UnRAR)";
#endif
		break;

	case FILETYPE_ARCHIVE_7Z:
#ifdef WIN32
		*error_string = "7z file detected (Require 7-Zip)";
#else
		*error_string = "7z file detected (Require 7-Zip)";
#endif
		break;

	case FILETYPE_ISO_MODE2:
		*error_string = "PSX game image detected.";
		break;

	case FILETYPE_NORMAL_DIRECTORY:
		ERROR_LOG(LOADER, "Just a directory.");
		*error_string = "Just a directory.";
		break;

	case FILETYPE_PPSSPP_SAVESTATE:
		*error_string = "This is a saved state, not a game.";  // Actually, we could make it load it...
		break;

	case FILETYPE_PSP_SAVEDATA_DIRECTORY:
		*error_string = "This is save data, not a game."; // Actually, we could make it load it...
		break;

	case FILETYPE_UNKNOWN_BIN:
	case FILETYPE_UNKNOWN_ELF:
	case FILETYPE_UNKNOWN:
	default:
		ERROR_LOG(LOADER, "Failed to identify file");
		*error_string = "Failed to identify file";
		break;
	}
	return false;
}

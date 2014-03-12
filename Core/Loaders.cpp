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

#include "file/file_util.h"

#include "Common/FileUtil.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/PSPLoaders.h"
#include "Core/MemMap.h"
#include "Core/Loaders.h"
#include "Core/System.h"
#include "Core/ELF/PBPReader.h"
#include "Core/ELF/ParamSFO.h"

// TODO : improve, look in the file more
IdentifiedFileType Identify_File(std::string &filename)
{
	if (filename.size() == 0) {
		ERROR_LOG(LOADER, "invalid filename %s", filename.c_str());
		return FILETYPE_ERROR;
	}

	FileInfo info;
	if (!getFileInfo(filename.c_str(), &info)) {
		return FILETYPE_ERROR;
	}

	std::string extension = filename.size() >= 5 ? filename.substr(filename.size() - 4) : "";
	if (!strcasecmp(extension.c_str(),".iso"))
	{
		// may be a psx iso, they have 2352 byte sectors. You never know what some people try to open
		if ((info.size % 2352) == 0)
		{
			FILE *f = File::OpenCFile(filename.c_str(), "rb");
			if (!f)	{
				// File does not exists
				return FILETYPE_ERROR;
			}

			unsigned char sync[12];
			fread(sync,1,12,f);
			fclose(f);

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


	// First, check if it's a directory with an EBOOT.PBP in it.
	if (info.isDirectory) {
		if (filename.size() > 4) {
			FileInfo ebootInfo;
			// Check for existence of EBOOT.PBP, as required for "Directory games".
			if (getFileInfo((filename + "/EBOOT.PBP").c_str(), &ebootInfo)) {
				if (ebootInfo.exists) {
					return FILETYPE_PSP_PBP_DIRECTORY;
				}
			}

			// check if it's a disc directory
			if (getFileInfo((filename + "/PSP_GAME").c_str(), &ebootInfo)) {
				if (ebootInfo.exists) {
					return FILETYPE_PSP_DISC_DIRECTORY;
				}
			}
		}

		return FILETYPE_NORMAL_DIRECTORY;
	}

	FILE *f = File::OpenCFile(filename.c_str(), "rb");
	if (!f)	{
		// File does not exists
		return FILETYPE_ERROR;
	}

	u32_le id;

	size_t readSize = fread(&id, 4, 1, f);
	if (readSize != 1) {
		fclose(f);
		return FILETYPE_ERROR;
	}

	u32 psar_offset = 0, psar_id = 0;
	u32 _id = id;
	switch (_id) {
	case 'PBP\x00':
		fseek(f, 0x24, SEEK_SET);
		fread(&psar_offset, 4, 1, f);
		fseek(f, psar_offset, SEEK_SET);
		fread(&psar_id, 4, 1, f);
		break;
	case '!raR':
		return FILETYPE_ARCHIVE_RAR;
	case '\x04\x03KP':
	case '\x06\x05KP':
	case '\x08\x07KP':
		return FILETYPE_ARCHIVE_ZIP;
	}

	fclose(f);

	if (id == 'FLE\x7F') {
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
		PBPReader pbp(filename.c_str());
		if (pbp.IsValid()) {
			if (!pbp.IsELF()) {
				size_t sfoSize;
				u8 *sfoData = pbp.GetSubFile(PBP_PARAM_SFO, &sfoSize);
				{
					recursive_mutex _lock;
					lock_guard lock(_lock);
					ParamSFOData paramSFO;
					paramSFO.ReadSFO(sfoData, sfoSize);
					// PS1 Eboots are supposed to use "ME" as their PARAM SFO category.
					// If they don't, and they're still malformed (e.g. PSISOIMG0000 isn't found), there's nothing we can do.
					if (paramSFO.GetValueString("CATEGORY") == "ME")
						return FILETYPE_PSP_PS1_PBP;
				}
				delete[] sfoData;
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
		std::string path = getDir(filename);
		// If loading from memstick...
		size_t pos = path.find("/PSP/GAME/");
		if (pos != std::string::npos) {
			filename = path;
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
	}
	return FILETYPE_UNKNOWN;
}

bool LoadFile(std::string &filename, std::string *error_string) {
	// Note that this can modify filename!
	switch (Identify_File(filename)) {
	case FILETYPE_PSP_PBP_DIRECTORY:
		{
			std::string ebootFilename = filename + "/EBOOT.PBP";
			FileInfo fileInfo;
			getFileInfo((filename + "/EBOOT.PBP").c_str(), &fileInfo);

			if (fileInfo.exists) {
				INFO_LOG(LOADER, "File is a PBP in a directory!");
				std::string ebootPath = filename + "/EBOOT.PBP";
				IdentifiedFileType ebootType = Identify_File(ebootPath);
				if (ebootType == FILETYPE_PSP_ISO_NP) {
					InitMemoryForGameISO(ebootPath);
					pspFileSystem.SetStartingDirectory("disc0:/PSP_GAME/USRDIR");
					return Load_PSP_ISO(filename.c_str(), error_string);
				}
				else if (ebootType == FILETYPE_PSP_PS1_PBP) {
					*error_string = "PS1 EBOOTs are not supported by PPSSPP.";
					return false;
				}
				std::string path = filename;
				size_t pos = path.find("/PSP/GAME/");
				if (pos != std::string::npos)
					pspFileSystem.SetStartingDirectory("ms0:" + path.substr(pos));
				return Load_PSP_ELF_PBP(ebootFilename.c_str(), error_string);
			} else {
				*error_string = "No EBOOT.PBP, misidentified game";
				return false;
			}
		}

	case FILETYPE_PSP_PBP:
	case FILETYPE_PSP_ELF:
		{
			INFO_LOG(LOADER,"File is an ELF or loose PBP!");
			return Load_PSP_ELF_PBP(filename.c_str(), error_string);
		}

	case FILETYPE_PSP_ISO:
	case FILETYPE_PSP_ISO_NP:
	case FILETYPE_PSP_DISC_DIRECTORY:	// behaves the same as the mounting is already done by now
		pspFileSystem.SetStartingDirectory("disc0:/PSP_GAME/USRDIR");
		return Load_PSP_ISO(filename.c_str(), error_string);

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

	case FILETYPE_ISO_MODE2:
		*error_string = "PSX game image detected.";
		break;

	case FILETYPE_NORMAL_DIRECTORY:
		ERROR_LOG(LOADER, "Just a directory.");
		*error_string = "Just a directory.";
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

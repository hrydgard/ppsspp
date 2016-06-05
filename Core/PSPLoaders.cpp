// Copyright (C) 2012 PPSSPP Project

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

#ifdef __SYMBIAN32__
#include <sys/param.h>
#endif

#include "file/file_util.h"

#include "Common/StringUtils.h"

#include "Core/ELF/ElfReader.h"
#include "Core/ELF/ParamSFO.h"

#include "FileSystems/BlockDevices.h"
#include "FileSystems/DirectoryFileSystem.h"
#include "FileSystems/ISOFileSystem.h"
#include "FileSystems/MetaFileSystem.h"
#include "FileSystems/VirtualDiscFileSystem.h"

#include "Core/Loaders.h"
#include "Core/MemMap.h"
#include "Core/HDRemaster.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"

#include "Host.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/PSPLoaders.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelMemory.h"

static void UseLargeMem(int memsize) {
	if (memsize != 1) {
		// Nothing requested.
		return;
	}

	if (Memory::g_PSPModel != PSP_MODEL_FAT) {
		INFO_LOG(LOADER, "Game requested full PSP-2000 memory access");
		Memory::g_MemorySize = Memory::RAM_DOUBLE_SIZE;
	} else {
		WARN_LOG(LOADER, "Game requested full PSP-2000 memory access, ignoring in PSP-1000 mode");
	}
}

// We gather the game info before actually loading/booting the ISO
// to determine if the emulator should enable extra memory and
// double-sized texture coordinates.
void InitMemoryForGameISO(FileLoader *fileLoader) {
	if (!fileLoader->Exists()) {
		return;
	}

	IFileSystem *fileSystem = nullptr;
	IFileSystem *blockSystem = nullptr;
	bool actualIso = false;
	if (fileLoader->IsDirectory()) {
		fileSystem = new VirtualDiscFileSystem(&pspFileSystem, fileLoader->Path());
		blockSystem = fileSystem;
	} else {
		auto bd = constructBlockDevice(fileLoader);
		// Can't init anything without a block device...
		if (!bd)
			return;

		ISOFileSystem *iso = new ISOFileSystem(&pspFileSystem, bd);
		fileSystem = iso;
		blockSystem = new ISOBlockSystem(iso);
	}

	pspFileSystem.Mount("umd0:", blockSystem);
	pspFileSystem.Mount("umd1:", blockSystem);
	pspFileSystem.Mount("disc0:", fileSystem);
	pspFileSystem.Mount("umd:", blockSystem);
	// TODO: Should we do this?
	//pspFileSystem.Mount("host0:", fileSystem);

	std::string gameID;
	std::string umdData;

	std::string sfoPath("disc0:/PSP_GAME/PARAM.SFO");
	PSPFileInfo fileInfo = pspFileSystem.GetFileInfo(sfoPath.c_str());

	if (fileInfo.exists) {
		std::vector<u8> paramsfo;
		pspFileSystem.ReadEntireFile(sfoPath, paramsfo);
		if (g_paramSFO.ReadSFO(paramsfo)) {
			UseLargeMem(g_paramSFO.GetValueInt("MEMSIZE"));
			gameID = g_paramSFO.GetValueString("DISC_ID");
		}

		std::vector<u8> umdDataBin;
		if (pspFileSystem.ReadEntireFile("disc0:/UMD_DATA.BIN", umdDataBin) >= 0) {
			umdData = std::string((const char *)&umdDataBin[0], umdDataBin.size());
		}
	}

	for (size_t i = 0; i < g_HDRemastersCount; i++) {
		const auto &entry = g_HDRemasters[i];
		if (entry.gameID != gameID) {
			continue;
		}
		if (entry.umdDataValue && umdData.find(entry.umdDataValue) == umdData.npos) {
			continue;
		}

		g_RemasterMode = true;
		Memory::g_MemorySize = entry.memorySize;
		g_DoubleTextureCoordinates = entry.doubleTextureCoordinates;
		break;
	}
	if (g_RemasterMode) {
		INFO_LOG(LOADER, "HDRemaster found, using increased memory");
	}
}

void InitMemoryForGamePBP(FileLoader *fileLoader) {
	if (!fileLoader->Exists()) {
		return;
	}

	PBPReader pbp(fileLoader);
	if (pbp.IsValid() && !pbp.IsELF()) {
		std::vector<u8> sfoData;
		if (pbp.GetSubFile(PBP_PARAM_SFO, &sfoData)) {
			ParamSFOData paramSFO;
			if (paramSFO.ReadSFO(sfoData)) {
				// This is the parameter CFW uses to determine homebrew wants the full 64MB.
				UseLargeMem(paramSFO.GetValueInt("MEMSIZE"));
			}
		}
	}
}


// Chinese translators like to rename EBOOT.BIN and replace it with some kind of stub
// that probably loads a plugin and then launches the actual game. These stubs don't work in PPSSPP.
// No idea why they are doing this, but it works to just bypass it. They could stop
// inventing new filenames though...
static const char *altBootNames[] = {
	"disc0:/PSP_GAME/SYSDIR/EBOOT.OLD",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.DAT",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.BI",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.LLD",
	//"disc0:/PSP_GAME/SYSDIR/OLD_EBOOT.BIN", //Utawareru Mono Chinese version
	"disc0:/PSP_GAME/SYSDIR/EBOOT.123",
	"disc0:/PSP_GAME/SYSDIR/EBOOT_LRC_CH.BIN",
	"disc0:/PSP_GAME/SYSDIR/BOOT0.OLD",
	"disc0:/PSP_GAME/SYSDIR/BOOT1.OLD",
	"disc0:/PSP_GAME/SYSDIR/BINOT.BIN",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.FRY",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.Z.Y",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.LEI",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.DNR",
	"disc0:/PSP_GAME/SYSDIR/DBZ2.BIN",
	"disc0:/PSP_GAME/SYSDIR/ss.RAW",
};

bool Load_PSP_ISO(FileLoader *fileLoader, std::string *error_string)
{
	// Mounting stuff relocated to InitMemoryForGameISO due to HD Remaster restructuring of code.

	std::string sfoPath("disc0:/PSP_GAME/PARAM.SFO");
	PSPFileInfo fileInfo = pspFileSystem.GetFileInfo(sfoPath.c_str());
	if (fileInfo.exists)
	{
		std::vector<u8> paramsfo;
		pspFileSystem.ReadEntireFile(sfoPath, paramsfo);
		if (g_paramSFO.ReadSFO(paramsfo))
		{
			char title[1024];
			sprintf(title, "%s : %s", g_paramSFO.GetValueString("DISC_ID").c_str(), g_paramSFO.GetValueString("TITLE").c_str());
			INFO_LOG(LOADER, "%s", title);
			host->SetWindowTitle(title);
		}
	}


	std::string bootpath("disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");

	// Bypass Chinese translation patches, see comment above.
	for (size_t i = 0; i < ARRAY_SIZE(altBootNames); i++) {
		if (pspFileSystem.GetFileInfo(altBootNames[i]).exists) {
			bootpath = altBootNames[i];			
		}
	}

	// Bypass another more dangerous one where the file is in USRDIR - this could collide with files in some game.
	std::string id = g_paramSFO.GetValueString("DISC_ID");
	if (id == "NPJH50624" && pspFileSystem.GetFileInfo("disc0:/PSP_GAME/USRDIR/PAKFILE2.BIN").exists) {
		bootpath = "disc0:/PSP_GAME/USRDIR/PAKFILE2.BIN";
	}
	if (id == "NPJH00100" && pspFileSystem.GetFileInfo("disc0:/PSP_GAME/USRDIR/DATA/GIM/GBL").exists) {
		bootpath = "disc0:/PSP_GAME/USRDIR/DATA/GIM/GBL";
	}

	bool hasEncrypted = false;
	u32 fd;
	if ((fd = pspFileSystem.OpenFile(bootpath, FILEACCESS_READ)) != 0)
	{
		u8 head[4];
		pspFileSystem.ReadFile(fd, head, 4);
		if (memcmp(head, "~PSP", 4) == 0 || memcmp(head, "\x7F""ELF", 4) == 0) {
			hasEncrypted = true;
		}
		pspFileSystem.CloseFile(fd);
	}
	if (!hasEncrypted) {
		// try unencrypted BOOT.BIN
		bootpath = "disc0:/PSP_GAME/SYSDIR/BOOT.BIN";
	}

	// Fail early with a clearer message for some types of ISOs.
	if (!pspFileSystem.GetFileInfo(bootpath).exists) {
		// Can't tell for sure if it's PS1 or PS2, but doesn't much matter.
		if (pspFileSystem.GetFileInfo("disc0:/SYSTEM.CNF;1").exists || pspFileSystem.GetFileInfo("disc0:/PSX.EXE;1").exists) {
			*error_string = "PPSSPP plays PSP games, not Playstation 1 or 2 games.";
		} else if (pspFileSystem.GetFileInfo("disc0:/UMD_VIDEO/PLAYLIST.UMD").exists) {
			*error_string = "PPSSPP doesn't support UMD Video.";
		} else if (pspFileSystem.GetFileInfo("disc0:/UMD_AUDIO/PLAYLIST.UMD").exists) {
			*error_string = "PPSSPP doesn't support UMD Music.";
		} else if (pspFileSystem.GetDirListing("disc0:/").empty()) {
			*error_string = "Not a valid disc image.";
		} else {
			*error_string = "A PSP game couldn't be found on the disc.";
		}
		return false;
	}

	//in case we didn't go through EmuScreen::boot
	g_Config.loadGameConfig(id);
	INFO_LOG(LOADER,"Loading %s...", bootpath.c_str());
	return __KernelLoadExec(bootpath.c_str(), 0, error_string);
}

static std::string NormalizePath(const std::string &path)
{
#ifdef _WIN32
	char buf[512] = {0};
	if (GetFullPathNameA(path.c_str(), sizeof(buf) - 1, buf, NULL) == 0)
		return "";
#else
	char buf[PATH_MAX + 1];
	if (realpath(path.c_str(), buf) == NULL)
		return "";
#endif
	return buf;
}

bool Load_PSP_ELF_PBP(FileLoader *fileLoader, std::string *error_string)
{
	// This is really just for headless, might need tweaking later.
	if (PSP_CoreParameter().mountIsoLoader != nullptr)
	{
		auto bd = constructBlockDevice(PSP_CoreParameter().mountIsoLoader);
		if (bd != NULL) {
			ISOFileSystem *umd2 = new ISOFileSystem(&pspFileSystem, bd);

			pspFileSystem.Mount("umd1:", umd2);
			pspFileSystem.Mount("disc0:", umd2);
			pspFileSystem.Mount("umd:", umd2);
		}
	}

	std::string full_path = fileLoader->Path();
	std::string path, file, extension;
	SplitPath(ReplaceAll(full_path, "\\", "/"), &path, &file, &extension);
#ifdef _WIN32
	path = ReplaceAll(path, "/", "\\");
#endif

	if (!PSP_CoreParameter().mountRoot.empty()) {
		// We don't want to worry about .. and cwd and such.
		const std::string rootNorm = NormalizePath(PSP_CoreParameter().mountRoot + "/");
		const std::string pathNorm = NormalizePath(path + "/");

		// If root is not a subpath of path, we can't boot the game.
		if (!startsWith(pathNorm, rootNorm)) {
			*error_string = "Cannot boot ELF located outside mountRoot.";
			return false;
		}

		const std::string filepath = ReplaceAll(pathNorm.substr(rootNorm.size()), "\\", "/");
		file = filepath + "/" + file;
		path = rootNorm + "/";
		pspFileSystem.SetStartingDirectory(filepath);
	} else {
		size_t pos = path.find("/PSP/GAME/");
		if (pos != std::string::npos) {
			pspFileSystem.SetStartingDirectory("ms0:" + path.substr(pos));
		}
	}

	DirectoryFileSystem *fs = new DirectoryFileSystem(&pspFileSystem, path);
	pspFileSystem.Mount("umd0:", fs);

	std::string finalName = "umd0:/" + file + extension;
	return __KernelLoadExec(finalName.c_str(), 0, error_string);
}

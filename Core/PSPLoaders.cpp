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

#include <thread>

#include "file/file_util.h"
#include "util/text/utf8.h"
#include "thread/threadutil.h"

#include "Common/FileUtil.h"
#include "Common/StringUtils.h"
#ifdef _WIN32
#include "Common/CommonWindows.h"
#endif

#include "Core/ELF/ElfReader.h"
#include "Core/ELF/ParamSFO.h"

#include "Core/FileSystems/BlockDevices.h"
#include "Core/FileSystems/BlobFileSystem.h"
#include "Core/FileSystems/DirectoryFileSystem.h"
#include "Core/FileSystems/ISOFileSystem.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/FileSystems/VirtualDiscFileSystem.h"

#include "Core/Loaders.h"
#include "Core/MemMap.h"
#include "Core/HDRemaster.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"

#include "Host.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
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

bool Load_PSP_ISO(FileLoader *fileLoader, std::string *error_string) {
	// Mounting stuff relocated to InitMemoryForGameISO due to HD Remaster restructuring of code.

	std::string sfoPath("disc0:/PSP_GAME/PARAM.SFO");
	PSPFileInfo fileInfo = pspFileSystem.GetFileInfo(sfoPath.c_str());
	if (fileInfo.exists) {
		std::vector<u8> paramsfo;
		pspFileSystem.ReadEntireFile(sfoPath, paramsfo);
		if (g_paramSFO.ReadSFO(paramsfo)) {
			std::string title = StringFromFormat("%s : %s", g_paramSFO.GetValueString("DISC_ID").c_str(), g_paramSFO.GetValueString("TITLE").c_str());
			INFO_LOG(LOADER, "%s", title.c_str());
			host->SetWindowTitle(title.c_str());
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
		coreState = CORE_ERROR;
		return false;
	}

	//in case we didn't go through EmuScreen::boot
	g_Config.loadGameConfig(id);
	host->SendUIMessage("config_loaded", "");
	INFO_LOG(LOADER,"Loading %s...", bootpath.c_str());

	std::thread th([bootpath] {
		setCurrentThreadName("ExecLoader");
		PSP_LoadingLock guard;
		if (coreState != CORE_POWERUP)
			return;

		PSP_SetLoading("Loading executable...");
		// TODO: We can't use the initial error_string pointer.
		bool success = __KernelLoadExec(bootpath.c_str(), 0, &PSP_CoreParameter().errorString);
		if (success && coreState == CORE_POWERUP) {
			coreState = PSP_CoreParameter().startBreak ? CORE_STEPPING : CORE_RUNNING;
		} else {
			coreState = CORE_ERROR;
			// TODO: This is a crummy way to communicate the error...
			PSP_CoreParameter().fileToStart = "";
		}
	});
	th.detach();
	return true;
}

static std::string NormalizePath(const std::string &path) {
#ifdef _WIN32
	wchar_t buf[512] = {0};
	std::wstring wpath = ConvertUTF8ToWString(path);
	if (GetFullPathName(wpath.c_str(), (int)ARRAY_SIZE(buf) - 1, buf, NULL) == 0)
		return "";
	return ConvertWStringToUTF8(buf);
#else
	char buf[PATH_MAX + 1];
	if (realpath(path.c_str(), buf) == NULL)
		return "";
	return buf;
#endif
}

bool Load_PSP_ELF_PBP(FileLoader *fileLoader, std::string *error_string) {
	// This is really just for headless, might need tweaking later.
	if (PSP_CoreParameter().mountIsoLoader != nullptr) {
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

	size_t pos = path.find("/PSP/GAME/");
	std::string ms_path;
	if (pos != std::string::npos) {
		ms_path = "ms0:" + path.substr(pos);
	} else {
		// This is wrong, but it's better than not having a working directory at all.
		// Note that umd0:/ is actually the writable containing directory, in this case.
		ms_path = "umd0:/";
	}

#ifdef _WIN32
	// Turn the slashes back to the Windows way.
	path = ReplaceAll(path, "/", "\\");
#endif

	if (!PSP_CoreParameter().mountRoot.empty()) {
		// We don't want to worry about .. and cwd and such.
		const std::string rootNorm = NormalizePath(PSP_CoreParameter().mountRoot + "/");
		const std::string pathNorm = NormalizePath(path + "/");

		// If root is not a subpath of path, we can't boot the game.
		if (!startsWith(pathNorm, rootNorm)) {
			*error_string = "Cannot boot ELF located outside mountRoot.";
			coreState = CORE_ERROR;
			return false;
		}

		const std::string filepath = ReplaceAll(pathNorm.substr(rootNorm.size()), "\\", "/");
		file = filepath + "/" + file;
		path = rootNorm + "/";
		pspFileSystem.SetStartingDirectory(filepath);
	} else {
		pspFileSystem.SetStartingDirectory(ms_path);
	}

	DirectoryFileSystem *fs = new DirectoryFileSystem(&pspFileSystem, path);
	pspFileSystem.Mount("umd0:", fs);

	std::string finalName = ms_path + file + extension;

	std::string homebrewName = PSP_CoreParameter().fileToStart;
	std::size_t lslash = homebrewName.find_last_of("/");
	homebrewName = homebrewName.substr(lslash + 1);
	std::string madeUpID = g_paramSFO.GenerateFakeID();

	std::string title = StringFromFormat("%s : %s", madeUpID.c_str(), homebrewName.c_str());
	INFO_LOG(LOADER, "%s", title.c_str());
	host->SetWindowTitle(title.c_str());

	// Temporary code
	// TODO: Remove this after ~ 1.6
	// It checks for old filenames for homebrew savestates(folder name) and rename them to new fakeID format
	std::string savestateDir = GetSysDirectory(DIRECTORY_SAVESTATE);

	for (int i = 0; i < 5; i += 1) {
		std::string oldName = StringFromFormat("%s%s_%d.ppst", savestateDir.c_str(), homebrewName.c_str(), i);
		if (File::Exists(oldName)) {
			std::string newName = StringFromFormat("%s%s_1.00_%d.ppst", savestateDir.c_str(), madeUpID.c_str(), i);
			File::Rename(oldName, newName);
		}
	}
	for (int i = 0; i < 5; i += 1) {
		std::string oldName = StringFromFormat("%s%s_%d.jpg", savestateDir.c_str(), homebrewName.c_str(), i);
		if (File::Exists(oldName)) {
			std::string newName = StringFromFormat("%s%s_1.00_%d.jpg", savestateDir.c_str(), madeUpID.c_str(), i);
			File::Rename(oldName, newName);
		}
	}
	// End of temporary code

	std::thread th([finalName] {
		setCurrentThreadName("ExecLoader");
		PSP_LoadingLock guard;
		if (coreState != CORE_POWERUP)
			return;

		bool success = __KernelLoadExec(finalName.c_str(), 0, &PSP_CoreParameter().errorString);
		if (success && coreState == CORE_POWERUP) {
			coreState = PSP_CoreParameter().startBreak ? CORE_STEPPING : CORE_RUNNING;
		} else {
			coreState = CORE_ERROR;
			// TODO: This is a crummy way to communicate the error...
			PSP_CoreParameter().fileToStart = "";
		}
	});
	th.detach();
	return true;
}

bool Load_PSP_GE_Dump(FileLoader *fileLoader, std::string *error_string) {
	BlobFileSystem *umd = new BlobFileSystem(&pspFileSystem, fileLoader, "data.ppdmp");
	pspFileSystem.Mount("disc0:", umd);

	std::thread th([] {
		setCurrentThreadName("ExecLoader");
		PSP_LoadingLock guard;
		if (coreState != CORE_POWERUP)
			return;

		bool success = __KernelLoadGEDump("disc0:/data.ppdmp", &PSP_CoreParameter().errorString);
		if (success && coreState == CORE_POWERUP) {
			coreState = PSP_CoreParameter().startBreak ? CORE_STEPPING : CORE_RUNNING;
		} else {
			coreState = CORE_ERROR;
			// TODO: This is a crummy way to communicate the error...
			PSP_CoreParameter().fileToStart = "";
		}
	});
	th.detach();
	return true;
}

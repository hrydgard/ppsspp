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

#include "Common/Data/Encoding/Utf8.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/System/Request.h"

#include "Common/File/AndroidContentURI.h"
#include "Common/File/FileUtil.h"
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

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/PSPLoaders.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelModule.h"
#include "Core/HLE/sceKernelMemory.h"

static std::thread loadingThread;

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

	std::shared_ptr<IFileSystem> fileSystem;
	std::shared_ptr<IFileSystem> blockSystem;

	if (fileLoader->IsDirectory()) {
		fileSystem = std::shared_ptr<IFileSystem>(new VirtualDiscFileSystem(&pspFileSystem, fileLoader->GetPath()));
		blockSystem = fileSystem;
	} else {
		auto bd = constructBlockDevice(fileLoader);
		// Can't init anything without a block device...
		if (!bd)
			return;

		std::shared_ptr<IFileSystem> iso = std::shared_ptr<IFileSystem>(new ISOFileSystem(&pspFileSystem, bd));
		fileSystem = iso;
		blockSystem = std::shared_ptr<IFileSystem>(new ISOBlockSystem(iso));
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

bool ReInitMemoryForGameISO(FileLoader *fileLoader) {
	if (!fileLoader->Exists()) {
		return false;
	}

	std::shared_ptr<IFileSystem> fileSystem;
	std::shared_ptr<IFileSystem> blockSystem;

	if (fileLoader->IsDirectory()) {
		fileSystem = std::shared_ptr<IFileSystem>(new VirtualDiscFileSystem(&pspFileSystem, fileLoader->GetPath()));
		blockSystem = fileSystem;
	} else {
		auto bd = constructBlockDevice(fileLoader);
		if (!bd)
			return false;

		std::shared_ptr<IFileSystem> iso = std::shared_ptr<IFileSystem>(new ISOFileSystem(&pspFileSystem, bd));
		fileSystem = iso;
		blockSystem = std::shared_ptr<IFileSystem>(new ISOBlockSystem(iso));
	}

	pspFileSystem.Remount("umd0:", blockSystem);
	pspFileSystem.Remount("umd1:", blockSystem);
	pspFileSystem.Remount("umd:", blockSystem);
	pspFileSystem.Remount("disc0:", fileSystem);

	return true;
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

				// Take this moment to bring over the title, if set.
				std::string title = paramSFO.GetValueString("TITLE");
				if (g_paramSFO.GetValueString("TITLE").empty() && !title.empty()) {
					g_paramSFO.SetValue("TITLE", title, (int)title.size());
				}

				std::string discID = paramSFO.GetValueString("DISC_ID");
				std::string systemVer = paramSFO.GetValueString("PSP_SYSTEM_VER");
				// Homebrew typically always leave this zero.
				bool discTotalCheck = paramSFO.GetValueInt("DISC_TOTAL") != 0;
				// A lot of homebrew reuse real game disc IDs - avoid.
				bool formatCheck = discID.substr(0, 2) != "NP" && discID.substr(0, 2) != "UL" && discID.substr(0, 2) != "UC";
				char region = discID.size() > 3 ? discID[2] : '\0';
				bool regionCheck = region != 'A' && region != 'E' && region != 'H' && region != 'I' && region != 'J' && region != 'K' && region != 'U' && region != 'X';
				bool systemVerCheck = !systemVer.empty() && systemVer[0] >= '5';
				if ((formatCheck || regionCheck || discTotalCheck || systemVerCheck) && !discID.empty()) {
					g_paramSFO.SetValue("DISC_ID", discID, (int)discID.size());
					std::string ver = paramSFO.GetValueString("DISC_VERSION");
					if (ver.empty())
						ver = "1.00";
					g_paramSFO.SetValue("DISC_VERSION", ver, (int)ver.size());
				}
			}
		}
	}
}


// Chinese translators like to rename EBOOT.BIN and replace it with some kind of stub
// that probably loads a plugin and then launches the actual game. These stubs don't work in PPSSPP.
// No idea why they are doing this, but it works to just bypass it. They could stop
// inventing new filenames though...
static const char * const altBootNames[] = {
	"disc0:/PSP_GAME/SYSDIR/EBOOT.OLD",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.DAT",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.BI",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.LLD",
	//"disc0:/PSP_GAME/SYSDIR/OLD_EBOOT.BIN", //Utawareru Mono Chinese version
	"disc0:/PSP_GAME/SYSDIR/EBOOT.123",
	//"disc0:/PSP_GAME/SYSDIR/EBOOT_LRC_CH.BIN", // Hatsune Miku Project Diva Extend chinese version
	"disc0:/PSP_GAME/SYSDIR/BOOT0.OLD",
	"disc0:/PSP_GAME/SYSDIR/BOOT1.OLD",
	"disc0:/PSP_GAME/SYSDIR/BINOT.BIN",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.FRY",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.Z.Y",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.LEI",
	"disc0:/PSP_GAME/SYSDIR/EBOOT.DNR",
	"disc0:/PSP_GAME/SYSDIR/DBZ2.BIN",
	//"disc0:/PSP_GAME/SYSDIR/ss.RAW",//Code Geass: Lost Colors chinese version
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
			System_SetWindowTitle(title);
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
	int fd;
	if ((fd = pspFileSystem.OpenFile(bootpath, FILEACCESS_READ)) >= 0)
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
			*error_string = "PPSSPP plays PSP games, not PlayStation 1 or 2 games.";
		} else if (pspFileSystem.GetFileInfo("disc0:/UMD_VIDEO/PLAYLIST.UMD").exists) {
			*error_string = "PPSSPP doesn't support UMD Video.";
		} else if (pspFileSystem.GetFileInfo("disc0:/UMD_AUDIO/PLAYLIST.UMD").exists) {
			*error_string = "PPSSPP doesn't support UMD Music.";
		} else if (pspFileSystem.GetDirListing("disc0:/").empty()) {
			*error_string = "Not a valid disc image.";
		} else {
			*error_string = "A PSP game couldn't be found on the disc.";
		}
		coreState = CORE_BOOT_ERROR;
		return false;
	}

	//in case we didn't go through EmuScreen::boot
	g_Config.loadGameConfig(id, g_paramSFO.GetValueString("TITLE"));
	System_PostUIMessage(UIMessage::CONFIG_LOADED);
	INFO_LOG(LOADER, "Loading %s...", bootpath.c_str());

	PSPLoaders_Shutdown();
	// Note: this thread reads the game binary, loads caches, and links HLE while UI spins.
	// To do something deterministically when the game starts, disabling this thread won't be enough.
	// Instead: Use Core_ListenLifecycle() or watch coreState.
	loadingThread = std::thread([bootpath] {
		SetCurrentThreadName("ExecLoader");
		PSP_LoadingLock guard;
		if (coreState != CORE_POWERUP)
			return;

		AndroidJNIThreadContext jniContext;

		PSP_SetLoading("Loading executable...");
		// TODO: We can't use the initial error_string pointer.
		bool success = __KernelLoadExec(bootpath.c_str(), 0, &PSP_CoreParameter().errorString);
		if (success && coreState == CORE_POWERUP) {
			coreState = PSP_CoreParameter().startBreak ? CORE_STEPPING : CORE_RUNNING;
		} else {
			coreState = CORE_BOOT_ERROR;
			// TODO: This is a crummy way to communicate the error...
			PSP_CoreParameter().fileToStart.clear();
		}
	});
	return true;
}

static Path NormalizePath(const Path &path) {
	if (path.Type() != PathType::NATIVE) {
		// Nothing to do - these can't be non-normalized.
		return path;
	}

#ifdef _WIN32
	std::wstring wpath = path.ToWString();
	std::wstring buf;
	buf.resize(512);
	size_t sz = GetFullPathName(wpath.c_str(), (DWORD)buf.size(), &buf[0], nullptr);
	if (sz != 0 && sz < buf.size()) {
		buf.resize(sz);
	} else if (sz > buf.size()) {
		buf.resize(sz);
		sz = GetFullPathName(wpath.c_str(), (DWORD)buf.size(), &buf[0], nullptr);
		// This should truncate off the null terminator.
		buf.resize(sz);
	}
	return Path(buf);
#else
	char buf[PATH_MAX + 1];
	if (!realpath(path.c_str(), buf))
		return Path();
	return Path(buf);
#endif
}

bool Load_PSP_ELF_PBP(FileLoader *fileLoader, std::string *error_string) {
	// This is really just for headless, might need tweaking later.
	if (PSP_CoreParameter().mountIsoLoader != nullptr) {
		auto bd = constructBlockDevice(PSP_CoreParameter().mountIsoLoader);
		if (bd != NULL) {
			std::shared_ptr<IFileSystem> umd2 = std::shared_ptr<IFileSystem>(new ISOFileSystem(&pspFileSystem, bd));
			std::shared_ptr<IFileSystem> blockSystem = std::shared_ptr<IFileSystem>(new ISOBlockSystem(umd2));

			pspFileSystem.Mount("umd1:", blockSystem);
			pspFileSystem.Mount("disc0:", umd2);
			pspFileSystem.Mount("umd:", blockSystem);
		}
	}

	Path full_path = fileLoader->GetPath();
	std::string path = full_path.GetDirectory();
	std::string file = full_path.GetFilename();

	if (full_path.Type() == PathType::CONTENT_URI) {
		path = AndroidContentURI(full_path.GetDirectory()).FilePath();
	}

	size_t pos = path.find("PSP/GAME/");
	std::string ms_path;
	if (pos != std::string::npos) {
		ms_path = "ms0:/" + path.substr(pos) + "/";
	} else {
		// This is wrong, but it's better than not having a working directory at all.
		// Note that umd0:/ is actually the writable containing directory, in this case.
		ms_path = "umd0:/";
	}

	Path dir;
	if (!PSP_CoreParameter().mountRoot.empty()) {
		// We don't want to worry about .. and cwd and such.
		const Path rootNorm = NormalizePath(PSP_CoreParameter().mountRoot);
		Path pathNorm = NormalizePath(Path(path));

		if (full_path.Type() == PathType::CONTENT_URI) {
			pathNorm = full_path.NavigateUp();
		}

		// If root is not a subpath of path, we can't boot the game.
		if (!pathNorm.StartsWith(rootNorm)) {
			*error_string = "Cannot boot ELF located outside mountRoot.";
			coreState = CORE_BOOT_ERROR;
			return false;
		}

		std::string filepath;
		if (full_path.Type() == PathType::CONTENT_URI) {
			std::string rootFilePath = AndroidContentURI(rootNorm.c_str()).FilePath();
			std::string pathFilePath = AndroidContentURI(pathNorm.c_str()).FilePath();
			filepath = pathFilePath.substr(rootFilePath.size());
		} else {
			filepath = ReplaceAll(pathNorm.ToString().substr(rootNorm.ToString().size()), "\\", "/");
		}

		file = filepath + "/" + file;
		path = rootNorm.ToString();
		pspFileSystem.SetStartingDirectory(filepath);
		dir = Path(path);
	} else {
		pspFileSystem.SetStartingDirectory(ms_path);
		dir = full_path.NavigateUp();
	}

	std::shared_ptr<IFileSystem> fs = std::shared_ptr<IFileSystem>(new DirectoryFileSystem(&pspFileSystem, dir, FileSystemFlags::SIMULATE_FAT32 | FileSystemFlags::CARD));
	pspFileSystem.Mount("umd0:", fs);

	std::string finalName = ms_path + file;

	std::string homebrewName = PSP_CoreParameter().fileToStart.ToVisualString();
	std::size_t lslash = homebrewName.find_last_of("/");
#if PPSSPP_PLATFORM(UWP)
	if (lslash == homebrewName.npos) {
		lslash = homebrewName.find_last_of("\\");
	}
#endif
	if (lslash != homebrewName.npos)
		homebrewName = homebrewName.substr(lslash + 1);
	std::string homebrewTitle = g_paramSFO.GetValueString("TITLE");
	if (homebrewTitle.empty())
		homebrewTitle = homebrewName;
	std::string discID = g_paramSFO.GetDiscID();
	std::string discVersion = g_paramSFO.GetValueString("DISC_VERSION");
	std::string madeUpID = g_paramSFO.GenerateFakeID(Path());

	std::string title = StringFromFormat("%s : %s", discID.c_str(), homebrewTitle.c_str());
	INFO_LOG(LOADER, "%s", title.c_str());
	System_SetWindowTitle(title);

	// Migrate old save states from old versions of fake game IDs.
	const Path savestateDir = GetSysDirectory(DIRECTORY_SAVESTATE);
	for (int i = 0; i < 5; ++i) {
		Path newPrefix = savestateDir / StringFromFormat("%s_%s_%d", discID.c_str(), discVersion.c_str(), i);
		Path oldNamePrefix = savestateDir / StringFromFormat("%s_%d", homebrewName.c_str(), i);
		Path oldIDPrefix = savestateDir / StringFromFormat("%s_1.00_%d", madeUpID.c_str(), i);

		if (oldIDPrefix != newPrefix && File::Exists(oldIDPrefix.WithExtraExtension(".ppst")))
			File::Rename(oldIDPrefix.WithExtraExtension(".ppst"), newPrefix.WithExtraExtension(".ppst"));
		else if (File::Exists(oldNamePrefix.WithExtraExtension(".ppst")))
			File::Rename(oldNamePrefix.WithExtraExtension(".ppst"), newPrefix.WithExtraExtension(".ppst"));
		if (oldIDPrefix != newPrefix && File::Exists(oldIDPrefix.WithExtraExtension(".jpg")))
			File::Rename(oldIDPrefix.WithExtraExtension(".jpg"), newPrefix.WithExtraExtension(".jpg"));
		else if (File::Exists(oldNamePrefix.WithExtraExtension(".jpg")))
			File::Rename(oldNamePrefix.WithExtraExtension(".jpg"), newPrefix.WithExtraExtension(".jpg"));
	}

	PSPLoaders_Shutdown();
	// Note: See Load_PSP_ISO for notes about this thread.
	loadingThread = std::thread([finalName] {
		SetCurrentThreadName("ExecLoader");
		PSP_LoadingLock guard;
		if (coreState != CORE_POWERUP)
			return;

		AndroidJNIThreadContext jniContext;

		bool success = __KernelLoadExec(finalName.c_str(), 0, &PSP_CoreParameter().errorString);
		if (success && coreState == CORE_POWERUP) {
			coreState = PSP_CoreParameter().startBreak ? CORE_STEPPING : CORE_RUNNING;
		} else {
			coreState = CORE_BOOT_ERROR;
			// TODO: This is a crummy way to communicate the error...
			PSP_CoreParameter().fileToStart.clear();
		}
	});
	return true;
}

bool Load_PSP_GE_Dump(FileLoader *fileLoader, std::string *error_string) {
	std::shared_ptr<IFileSystem> umd = std::shared_ptr<IFileSystem>(new BlobFileSystem(&pspFileSystem, fileLoader, "data.ppdmp"));
	pspFileSystem.Mount("disc0:", umd);

	PSPLoaders_Shutdown();
	// Note: See Load_PSP_ISO for notes about this thread.
	loadingThread = std::thread([] {
		SetCurrentThreadName("ExecLoader");
		PSP_LoadingLock guard;
		if (coreState != CORE_POWERUP)
			return;

		AndroidJNIThreadContext jniContext;

		bool success = __KernelLoadGEDump("disc0:/data.ppdmp", &PSP_CoreParameter().errorString);
		if (success && coreState == CORE_POWERUP) {
			coreState = PSP_CoreParameter().startBreak ? CORE_STEPPING : CORE_RUNNING;
		} else {
			coreState = CORE_BOOT_ERROR;
			// TODO: This is a crummy way to communicate the error...
			PSP_CoreParameter().fileToStart.clear();
		}
	});
	return true;
}

void PSPLoaders_Shutdown() {
	if (loadingThread.joinable())
		loadingThread.join();
}

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

#include "ELF/ElfReader.h"

#include "FileSystems/BlockDevices.h"
#include "FileSystems/DirectoryFileSystem.h"
#include "FileSystems/ISOFileSystem.h"

#include "MemMap.h"

#include "MIPS/MIPS.h"
#include "MIPS/MIPSAnalyst.h"
#include "MIPS/MIPSCodeUtils.h"

#include "StringUtils.h"

#include "Host.h"

#include "System.h"
#include "PSPLoaders.h"
#include "HLE/HLE.h"
#include "HLE/sceKernel.h"
#include "HLE/sceKernelThread.h"
#include "HLE/sceKernelModule.h"
#include "HLE/sceKernelMemory.h"
#include "ELF/ParamSFO.h"

bool Load_PSP_ISO(const char *filename, std::string *error_string)
{
	ISOFileSystem *umd2 = new ISOFileSystem(&pspFileSystem, constructBlockDevice(filename));

	// Parse PARAM.SFO

	//pspFileSystem.Mount("host0:",umd2);
	pspFileSystem.Mount("umd0:", umd2);
	pspFileSystem.Mount("umd1:", umd2);
	pspFileSystem.Mount("disc0:", umd2);
	pspFileSystem.Mount("umd:", umd2);

	std::string sfoPath("disc0:/PSP_GAME/PARAM.SFO");
	PSPFileInfo fileInfo = pspFileSystem.GetFileInfo(sfoPath.c_str());
	if (fileInfo.exists)
	{
		u8 *paramsfo = new u8[(size_t)fileInfo.size];
		u32 fd = pspFileSystem.OpenFile(sfoPath, FILEACCESS_READ);
		pspFileSystem.ReadFile(fd, paramsfo, fileInfo.size);
		pspFileSystem.CloseFile(fd);
		if (g_paramSFO.ReadSFO(paramsfo, (size_t)fileInfo.size))
		{
			char title[1024];
			sprintf(title, "%s : %s", g_paramSFO.GetValueString("DISC_ID").c_str(), g_paramSFO.GetValueString("TITLE").c_str());
			INFO_LOG(LOADER, "%s", title);
			host->SetWindowTitle(title);
		}
		delete [] paramsfo;
	}


	std::string bootpath("disc0:/PSP_GAME/SYSDIR/EBOOT.BIN");
	// bypass patchers
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT.OLD").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.OLD";
	}
	// bypass another patchers
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT.DAT").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.DAT";
	}
	// bypass more patchers
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT.BI").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.BI";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT.LLD").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.LLD";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/OLD_EBOOT.BIN").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/OLD_EBOOT.BIN";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT.123").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.123";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT_LRC_CH.BIN").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT_LRC_CH.BIN";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/BOOT0.OLD").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/BOOT0.OLD";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/BOOT1.OLD").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/BOOT1.OLD";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/BINOT.BIN").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/BINOT.BIN";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT.FRY").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.FRY";
	}
	if (pspFileSystem.GetFileInfo("disc0:/PSP_GAME/SYSDIR/EBOOT.Z.Y").exists) {
		bootpath = "disc0:/PSP_GAME/SYSDIR/EBOOT.Z.Y";
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
	if (!hasEncrypted)
	{
		// try unencrypted BOOT.BIN
		bootpath = "disc0:/PSP_GAME/SYSDIR/BOOT.BIN";
	}

	INFO_LOG(LOADER,"Loading %s...", bootpath.c_str());
	return __KernelLoadExec(bootpath.c_str(), 0, error_string);
}

bool Load_PSP_ELF_PBP(const char *filename, std::string *error_string)
{
	// This is really just for headless, might need tweaking later.
	if (!PSP_CoreParameter().mountIso.empty())
	{
		ISOFileSystem *umd2 = new ISOFileSystem(&pspFileSystem, constructBlockDevice(PSP_CoreParameter().mountIso.c_str()));

		pspFileSystem.Mount("umd1:", umd2);
		pspFileSystem.Mount("disc0:", umd2);
		pspFileSystem.Mount("umd:", umd2);
	}

	std::string full_path = filename;
	std::string path, file, extension;
	SplitPath(ReplaceAll(full_path, "\\", "/"), &path, &file, &extension);
#ifdef _WIN32
	path = ReplaceAll(path, "/", "\\");
#endif

	DirectoryFileSystem *fs = new DirectoryFileSystem(&pspFileSystem, path);
	pspFileSystem.Mount("umd0:", fs);

	std::string finalName = "umd0:/" + file + extension;
	return __KernelLoadExec(finalName.c_str(), 0, error_string);
}

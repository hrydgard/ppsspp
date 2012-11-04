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

#include "FileSystems/DirectoryFileSystem.h"
#include "FileSystems/ISOFileSystem.h"

#include "MemMap.h"

#include "MIPS/MIPS.h"
#include "MIPS/MIPSAnalyst.h"
#include "MIPS/MIPSCodeUtils.h"

#include "StringUtil.h"

#include "Host.h"

#include "System.h"
#include "PSPLoaders.h"
#include "HLE/HLE.h"
#include "HLE/sceKernel.h"
#include "HLE/sceKernelThread.h"
#include "HLE/sceKernelModule.h"
#include "HLE/sceKernelMemory.h"


BlockDevice *constructBlockDevice(const char *filename)
{
	char firstInExtension = filename[strlen(filename)-3];
	if (firstInExtension == 'c')
	{
		return new CISOFileBlockDevice(filename);
	}
	else
	{
		return new FileBlockDevice(filename);
	}
}

bool Load_PSP_ISO(const char *filename, std::string *error_string)
{
	ISOFileSystem *umd2 = new ISOFileSystem(&pspFileSystem, constructBlockDevice(filename));

	//pspFileSystem.Mount("host0:",umd2);
	pspFileSystem.Mount("umd0:", umd2);
	pspFileSystem.Mount("umd1:", umd2);
	pspFileSystem.Mount("disc0:", umd2);
	pspFileSystem.Mount("UMD0:", umd2);
	pspFileSystem.Mount("UMD1:", umd2);
	pspFileSystem.Mount("DISC0:", umd2);

	std::string bootpath("disc0:/PSP_GAME/SYSDIR/BOOT.BIN");
	//std::string bootpath("/PSP_GAME/USRDIR/locoroco/locoroco.prx");

	INFO_LOG(LOADER,"Loading %s...", bootpath.c_str());
	return __KernelLoadExec(bootpath.c_str(), 0, error_string);
}

bool Load_PSP_ELF_PBP(const char *filename, std::string *error_string)
{
	std::string full_path = filename;
	std::string path, file, extension;
	SplitPath(ReplaceAll(full_path, "\\", "/"), &path, &file, &extension);
#ifdef _WIN32
	path = ReplaceAll(path, "/", "\\");
#endif
	DirectoryFileSystem *fs = new DirectoryFileSystem(&pspFileSystem, path);
	pspFileSystem.Mount("umd0:/", fs);

	std::string finalName = "umd0:/" + file + extension;
	return __KernelLoadExec(finalName.c_str(), 0, error_string);
}

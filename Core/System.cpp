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

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "MemMap.h"

#include "MIPS/MIPS.h"

#include "MIPS/JitCommon/JitCommon.h"

#include "System.h"
// Bad dependency
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/ShaderManager.h"

#include "PSPMixer.h"
#include "HLE/HLE.h"
#include "HLE/sceKernel.h"
#include "HLE/sceKernelMemory.h"
#include "HLE/sceAudio.h"
#include "Config.h"
#include "Core.h"
#include "CoreTiming.h"
#include "CoreParameter.h"
#include "FileSystems/MetaFileSystem.h"
#include "Loaders.h"
#include "ELF/ParamSFO.h"
#include "../Common/LogManager.h"

MetaFileSystem pspFileSystem;
ParamSFOData g_paramSFO;
GlobalUIState globalUIState;
static CoreParameter coreParameter;
static PSPMixer *mixer;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_STEPPING;
// Note: intentionally not used for CORE_NEXTFRAME.
volatile bool coreStatePending = false;

void Core_UpdateState(CoreState newState)
{
	if ((coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME) && newState != CORE_RUNNING)
		coreStatePending = true;
	coreState = newState;
}

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string)
{
	INFO_LOG(HLE, "PPSSPP %s", PPSSPP_GIT_VERSION);

	coreParameter = coreParam;
	currentCPU = &mipsr4k;
	numCPUs = 1;
	Memory::Init();
	mipsr4k.Reset();
	mipsr4k.pc = 0;

	host->AttemptLoadSymbolMap();

	if (coreParameter.enableSound)
	{
		mixer = new PSPMixer();
		host->InitSound(mixer);
	}

	if (coreParameter.disableG3Dlog)
	{
		LogManager::GetInstance()->SetEnable(LogTypes::G3D, false);
	}

	CoreTiming::Init();

	// Init all the HLE modules
	HLEInit();

	// TODO: Check Game INI here for settings, patches and cheats, and modify coreParameter accordingly

	std::string filename = coreParameter.fileToStart;
	if (!LoadFile(filename, error_string) || coreState == CORE_POWERDOWN)
	{
		pspFileSystem.Shutdown();
		CoreTiming::Shutdown();
		__KernelShutdown();
		HLEShutdown();
		host->ShutdownSound();
		Memory::Shutdown();
		coreParameter.fileToStart = "";
		return false;
	}

	if (coreParam.updateRecent)
		g_Config.AddRecent(filename);

	// Setup JIT here.
	if (coreParameter.startPaused)
		coreState = CORE_STEPPING;
	else
		coreState = CORE_RUNNING;
	return true;
}

bool PSP_IsInited()
{
	return currentCPU != 0;
}

void PSP_Shutdown()
{
	pspFileSystem.Shutdown();

	CoreTiming::Shutdown();

	if (g_Config.bAutoSaveSymbolMap)
		host->SaveSymbolMap();

	if (coreParameter.enableSound)
	{
		host->ShutdownSound();
		mixer = 0;  // deleted in ShutdownSound
	}
	__KernelShutdown();
	HLEShutdown();
	Memory::Shutdown();
	currentCPU = 0;
}

CoreParameter &PSP_CoreParameter()
{
	return coreParameter;
}


void GetSysDirectories(std::string &memstickpath, std::string &flash0path) {
#ifdef _WIN32
	char path_buffer[_MAX_PATH], drive[_MAX_DRIVE] ,dir[_MAX_DIR], file[_MAX_FNAME], ext[_MAX_EXT];
	char memstickpath_buf[_MAX_PATH];
	char flash0path_buf[_MAX_PATH];

	GetModuleFileName(NULL,path_buffer,sizeof(path_buffer));

	char *winpos = strstr(path_buffer, "Windows");
	if (winpos)
	*winpos = 0;
	strcat(path_buffer, "dummy.txt");

	_splitpath_s(path_buffer, drive, dir, file, ext );

	// Mount a couple of filesystems
	sprintf(memstickpath_buf, "%s%smemstick\\", drive, dir);
	sprintf(flash0path_buf, "%s%sflash0\\", drive, dir);

	memstickpath = memstickpath_buf;
	flash0path = flash0path_buf;
#else
	// TODO
	memstickpath = g_Config.memCardDirectory;
	flash0path = g_Config.flashDirectory;
#endif
}

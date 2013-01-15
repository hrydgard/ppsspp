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
#include "Core.h"
#include "CoreTiming.h"
#include "CoreParameter.h"
#include "FileSystems/MetaFileSystem.h"
#include "Loaders.h"
#include "ELF/ParamSFO.h"
#include "../Common/LogManager.h"

MetaFileSystem pspFileSystem;
ParamSFOData g_paramSFO;
static CoreParameter coreParameter;
static PSPMixer *mixer;

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string)
{
	coreParameter = coreParam;
	currentCPU = &mipsr4k;
	numCPUs = 1;
	Memory::Init();
	mipsr4k.Reset();
	mipsr4k.pc = 0;

	if (coreParameter.enableSound)
	{
		mixer = new PSPMixer();
		host->InitSound(mixer);
	}

	if (coreParameter.disableG3Dlog)
	{
		LogManager::GetInstance()->SetEnable(LogTypes::G3D, false);
	}

	// Init all the HLE modules
	HLEInit();

	// TODO: Check Game INI here for settings, patches and cheats, and modify coreParameter accordingly

	if (!LoadFile(coreParameter.fileToStart.c_str(), error_string))
	{
		pspFileSystem.Shutdown();
		CoreTiming::ClearPendingEvents();
		CoreTiming::UnregisterAllEvents();
		__KernelShutdown();
		HLEShutdown();
		host->ShutdownSound();
		Memory::Shutdown();
		coreParameter.fileToStart = "";
		return false;
	}

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

	TextureCache_Clear(true);

	CoreTiming::ClearPendingEvents();
	CoreTiming::UnregisterAllEvents();

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

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

#if defined(_WIN32) && !defined(_XBOX)
#include "Common/CommonWindows.h"
#include <ShlObj.h>
#include <string>
#include <codecvt>
#endif

#include "native/thread/thread.h"
#include "native/thread/threadutil.h"
#include "native/base/mutex.h"
#include "util/text/utf8.h"

#include "Core/MemMap.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

#include "Core/System.h"
#include "Core/PSPMixer.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceAudio.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Loaders.h"
#include "Core/PSPLoaders.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/SaveState.h"
#include "Common/LogManager.h"

#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"

enum CPUThreadState {
	CPU_THREAD_NOT_RUNNING,
	CPU_THREAD_PENDING,
	CPU_THREAD_STARTING,
	CPU_THREAD_RUNNING,
	CPU_THREAD_SHUTDOWN,

	CPU_THREAD_EXECUTE,
};

MetaFileSystem pspFileSystem;
ParamSFOData g_paramSFO;
GlobalUIState globalUIState;
static CoreParameter coreParameter;
static PSPMixer *mixer;
static std::thread *cpuThread = NULL;
static recursive_mutex cpuThreadLock;
static condition_variable cpuThreadCond;
static condition_variable cpuThreadReplyCond;
static u64 cpuThreadUntil;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_STEPPING;
// Note: intentionally not used for CORE_NEXTFRAME.
volatile bool coreStatePending = false;
static volatile CPUThreadState cpuThreadState = CPU_THREAD_NOT_RUNNING;

bool IsAudioInitialised() {
	return mixer != NULL;
}

void Audio_Init() {
	if(mixer == NULL) {
		mixer = new PSPMixer();
		host->InitSound(mixer);
	}
}

bool IsOnSeparateCPUThread() {
	if (cpuThread != NULL) {
		return cpuThread->get_id() == std::this_thread::get_id();
	} else {
		return false;
	}
}

void CPU_SetState(CPUThreadState to) {
	lock_guard guard(cpuThreadLock);
	cpuThreadState = to;
	cpuThreadCond.notify_one();
	cpuThreadReplyCond.notify_one();
}

bool CPU_NextState(CPUThreadState from, CPUThreadState to) {
	lock_guard guard(cpuThreadLock);
	if (cpuThreadState == from) {
		CPU_SetState(to);
		return true;
	} else {
		return false;
	}
}

bool CPU_NextStateNot(CPUThreadState from, CPUThreadState to) {
	lock_guard guard(cpuThreadLock);
	if (cpuThreadState != from) {
		CPU_SetState(to);
		return true;
	} else {
		return false;
	}
}

bool CPU_IsReady() {
	return cpuThreadState == CPU_THREAD_RUNNING || cpuThreadState == CPU_THREAD_NOT_RUNNING;
}

bool CPU_IsShutdown() {
	return cpuThreadState == CPU_THREAD_NOT_RUNNING;
}

bool CPU_HasPendingAction() {
	return cpuThreadState != CPU_THREAD_RUNNING;
}

void CPU_WaitStatus(condition_variable &cond, bool (*pred)()) {
	lock_guard guard(cpuThreadLock);
	while (!pred()) {
		cond.wait(cpuThreadLock);
	}
}

void CPU_Shutdown();

void CPU_Init() {
	coreState = CORE_POWERUP;
	currentCPU = &mipsr4k;
	numCPUs = 1;

	// Default memory settings
	// Seems to be the safest place currently..
	if (g_Config.iPSPModel == PSP_MODEL_FAT)
		Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE; // 32 MB of ram by default
	else
		Memory::g_MemorySize = Memory::RAM_DOUBLE_SIZE;

	g_RemasterMode = false;
	g_DoubleTextureCoordinates = false;
	Memory::g_PSPModel = g_Config.iPSPModel;

	std::string filename = coreParameter.fileToStart;
	IdentifiedFileType type = Identify_File(filename);

	switch (type) {
	case FILETYPE_PSP_ISO:
	case FILETYPE_PSP_ISO_NP:
	case FILETYPE_PSP_DISC_DIRECTORY:
		InitMemoryForGameISO(filename);
		break;
	default:
		break;
	}

	Memory::Init();
	mipsr4k.Reset();

	host->AttemptLoadSymbolMap();

	if (coreParameter.enableSound) {
		Audio_Init();
	}

	CoreTiming::Init();

	// Init all the HLE modules
	HLEInit();

	// TODO: Check Game INI here for settings, patches and cheats, and modify coreParameter accordingly

	// Why did we check for CORE_POWERDOWN here?
	if (!LoadFile(filename, &coreParameter.errorString)) {
		CPU_Shutdown();
		coreParameter.fileToStart = "";
		CPU_SetState(CPU_THREAD_NOT_RUNNING);
		return;
	}


	if (coreParameter.updateRecent) {
		g_Config.AddRecent(filename);
	}

	coreState = coreParameter.startPaused ? CORE_STEPPING : CORE_RUNNING;
}

void CPU_Shutdown() {
	if (g_Config.bAutoSaveSymbolMap) {
		host->SaveSymbolMap();
	}

	CoreTiming::Shutdown();
	__KernelShutdown();
	HLEShutdown();
	if (coreParameter.enableSound) {
		host->ShutdownSound();
		mixer = 0;  // deleted in ShutdownSound
	}
	pspFileSystem.Shutdown();
	Memory::Shutdown();
	currentCPU = 0;
}

void CPU_RunLoop() {
	setCurrentThreadName("CPUThread");
	if (!CPU_NextState(CPU_THREAD_PENDING, CPU_THREAD_STARTING)) {
		ERROR_LOG(CPU, "CPU thread in unexpected state: %d", cpuThreadState);
		return;
	}

	CPU_Init();
	CPU_NextState(CPU_THREAD_STARTING, CPU_THREAD_RUNNING);

	while (cpuThreadState != CPU_THREAD_SHUTDOWN)
	{
		CPU_WaitStatus(cpuThreadCond, &CPU_HasPendingAction);
		switch (cpuThreadState) {
		case CPU_THREAD_EXECUTE:
			mipsr4k.RunLoopUntil(cpuThreadUntil);
			gpu->FinishEventLoop();
			CPU_NextState(CPU_THREAD_EXECUTE, CPU_THREAD_RUNNING);
			break;

		// These are fine, just keep looping.
		case CPU_THREAD_RUNNING:
		case CPU_THREAD_SHUTDOWN:
			break;

		default:
			ERROR_LOG(CPU, "CPU thread in unexpected state: %d", cpuThreadState);
			// Begin shutdown, otherwise we'd just spin on this bad state.
			CPU_SetState(CPU_THREAD_SHUTDOWN);
			break;
		}
	}

	if (coreState != CORE_ERROR) {
		coreState = CORE_POWERDOWN;
	}

	// Let's make sure the gpu has already cleaned up before we start freeing memory.
	if (gpu) {
		gpu->FinishEventLoop();
		gpu->SyncThread(true);
	}

	CPU_Shutdown();
	CPU_SetState(CPU_THREAD_NOT_RUNNING);
}

void Core_UpdateState(CoreState newState) {
	if ((coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME) && newState != CORE_RUNNING)
		coreStatePending = true;
	coreState = newState;
	Core_UpdateSingleStep();
}

void System_Wake() {
	// Ping the threads so they check coreState.
	CPU_NextStateNot(CPU_THREAD_NOT_RUNNING, CPU_THREAD_SHUTDOWN);
	if (gpu) {
		gpu->FinishEventLoop();
	}
}

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string) {
	INFO_LOG(BOOT, "PPSSPP %s", PPSSPP_GIT_VERSION);

	coreParameter = coreParam;
	coreParameter.errorString = "";

	if (g_Config.bSeparateCPUThread) {
		Core_ListenShutdown(System_Wake);
		CPU_SetState(CPU_THREAD_PENDING);
		cpuThread = new std::thread(&CPU_RunLoop);
		cpuThread->detach();
		CPU_WaitStatus(cpuThreadReplyCond, &CPU_IsReady);
	} else {
		CPU_Init();
	}

	bool success = coreParameter.fileToStart != "";
	*error_string = coreParameter.errorString;
	if (success) {
		success = GPU_Init();
		if (!success) {
			PSP_Shutdown();
			*error_string = "Unable to initialize rendering engine.";
		}
	}
	return success;
}

bool PSP_IsInited() {
	return currentCPU != 0;
}

void PSP_Shutdown() {
	if (coreState == CORE_RUNNING)
		Core_UpdateState(CORE_ERROR);
	Core_NotifyShutdown();
	if (cpuThread != NULL) {
		CPU_NextStateNot(CPU_THREAD_NOT_RUNNING, CPU_THREAD_SHUTDOWN);
		CPU_WaitStatus(cpuThreadReplyCond, &CPU_IsShutdown);
		delete cpuThread;
		cpuThread = 0;
	} else {
		CPU_Shutdown();
	}
	GPU_Shutdown();
	host->SetWindowTitle(0);
}

void PSP_RunLoopUntil(u64 globalticks) {
	SaveState::Process();
	if (coreState == CORE_POWERDOWN || coreState == CORE_ERROR) {
		return;
	}

	if (cpuThread != NULL) {
		// Tell the gpu a new frame is about to begin, before we start the CPU.
		gpu->SyncBeginFrame();

		cpuThreadUntil = globalticks;
		if (CPU_NextState(CPU_THREAD_RUNNING, CPU_THREAD_EXECUTE)) {
			// The CPU doesn't actually respect cpuThreadUntil well, especially when skipping frames.
			// TODO: Something smarter?  Or force CPU to bail periodically?
			while (!CPU_IsReady()) {
				gpu->RunEventsUntil(CoreTiming::GetTicks() + msToCycles(1000));
				if (coreState != CORE_RUNNING) {
					CPU_WaitStatus(cpuThreadReplyCond, &CPU_IsReady);
				}
			}
		} else {
			ERROR_LOG(CPU, "Unable to execute CPU run loop, unexpected state: %d", cpuThreadState);
		}
	} else {
		mipsr4k.RunLoopUntil(globalticks);
	}
}

void PSP_RunLoopFor(int cycles) {
	PSP_RunLoopUntil(CoreTiming::GetTicks() + cycles);
}

CoreParameter &PSP_CoreParameter() {
	return coreParameter;
}

std::string GetSysDirectory(PSPDirectories directoryType) {
	switch (directoryType) {
	case DIRECTORY_CHEATS:
		return g_Config.memCardDirectory + "PSP/Cheats/";
	case DIRECTORY_GAME:
		return g_Config.memCardDirectory + "PSP/GAME/";
	case DIRECTORY_SAVEDATA:
		return g_Config.memCardDirectory + "PSP/SAVEDATA/";
	case DIRECTORY_SCREENSHOT:
		return g_Config.memCardDirectory + "PSP/SCREENSHOT/";
	case DIRECTORY_SYSTEM:
		return g_Config.memCardDirectory + "PSP/SYSTEM/";
	case DIRECTORY_PAUTH:
		return g_Config.memCardDirectory + "PAUTH/";
	// Just return the memory stick root if we run into some sort of problem.
	default:
		ERROR_LOG(FILESYS, "Unknown directory type.");
		return g_Config.memCardDirectory;
	}
}

#ifdef _WIN32
// Run this at startup time. Please use GetSysDirectory if you need to query where folders are.
void InitSysDirectories() {
	if (!g_Config.memCardDirectory.empty() && !g_Config.flash0Directory.empty())
		return;

	const std::string path = File::GetExeDirectory();

	// Mount a filesystem
	g_Config.flash0Directory = path + "flash0/";

	// Detect the "My Documents"(XP) or "Documents"(on Vista/7/8) folder.
	wchar_t myDocumentsPath[MAX_PATH];
	const HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
	const std::string myDocsPath = ConvertWStringToUTF8(myDocumentsPath) + "/PPSSPP/";

	const std::string installedFile = path + "installed.txt";
	const bool installed = File::Exists(installedFile);

	// If installed.txt exists(and we can determine the Documents directory)
	if (installed && (result == S_OK))	{
		std::ifstream inputFile(ConvertUTF8ToWString(installedFile));

		if (!inputFile.fail() && inputFile.is_open()) {
			std::string tempString;

			std::getline(inputFile, tempString);

			// Skip UTF-8 encoding bytes if there are any. There are 3 of them.
			if (tempString.substr(0, 3) == "\xEF\xBB\xBF")
				tempString = tempString.substr(3);

			g_Config.memCardDirectory = tempString;
		}
		inputFile.close();

		// Check if the file is empty first, before appending the slash.
		if (g_Config.memCardDirectory.empty())
			g_Config.memCardDirectory = myDocsPath;

		size_t lastSlash = g_Config.memCardDirectory.find_last_of("/");
		if (lastSlash != (g_Config.memCardDirectory.length() - 1))
			g_Config.memCardDirectory.append("/");
	} else {
		g_Config.memCardDirectory = path + "memstick/";
	}

	// Create the memstickpath before trying to write to it, and fall back on Documents yet again
	// if we can't make it.
	if (!File::Exists(g_Config.memCardDirectory)) {
		if (!File::CreateDir(g_Config.memCardDirectory))
			g_Config.memCardDirectory = myDocsPath;
	}

	const std::string testFile = "/_writable_test.$$$";

	// If any directory is read-only, fall back to the Documents directory.
	// We're screwed anyway if we can't write to Documents, or can't detect it.
	if (!File::CreateEmptyFile(g_Config.memCardDirectory + testFile))
		g_Config.memCardDirectory = myDocsPath;

	// Clean up our mess.
	if (File::Exists(g_Config.memCardDirectory + testFile))
		File::Delete(g_Config.memCardDirectory + testFile);

	if (g_Config.currentDirectory.empty()) {
		g_Config.currentDirectory = GetSysDirectory(DIRECTORY_GAME);
	}
}
#endif

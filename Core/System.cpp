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

#include "ppsspp_config.h"

#ifdef _WIN32
#pragma warning(disable:4091)
#include "Common/CommonWindows.h"
#include <ShlObj.h>
#include <string>
#include <codecvt>
#endif

#include <thread>
#include <mutex>
#include <condition_variable>

#include "base/timeutil.h"
#include "math/math_util.h"
#include "thread/threadutil.h"
#include "util/text/utf8.h"

#include "Core/MemMap.h"
#include "Core/HDRemaster.h"

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"

#include "Debugger/SymbolMap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceAudio.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/FileLoaders/RamCachingFileLoader.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Loaders.h"
#include "Core/PSPLoaders.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/SaveState.h"
#include "Common/LogManager.h"
#include "Core/HLE/sceAudiocodec.h"

#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"

enum CPUThreadState {
	CPU_THREAD_NOT_RUNNING,
	CPU_THREAD_PENDING,
	CPU_THREAD_STARTING,
	CPU_THREAD_RUNNING,
	CPU_THREAD_SHUTDOWN,
	CPU_THREAD_QUIT,

	CPU_THREAD_EXECUTE,
	CPU_THREAD_RESUME,
};

MetaFileSystem pspFileSystem;
ParamSFOData g_paramSFO;
static GlobalUIState globalUIState;
static CoreParameter coreParameter;
static FileLoader *loadedFile;

bool audioInitialized;

bool coreCollectDebugStats = false;
bool coreCollectDebugStatsForced = false;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_STEPPING;
// Note: intentionally not used for CORE_NEXTFRAME.
volatile bool coreStatePending = false;
static volatile CPUThreadState cpuThreadState = CPU_THREAD_NOT_RUNNING;

static GPUBackend gpuBackend;

void ResetUIState() {
	globalUIState = UISTATE_MENU;
}

void UpdateUIState(GlobalUIState newState) {
	// Never leave the EXIT state.
	if (globalUIState != newState && globalUIState != UISTATE_EXIT) {
		globalUIState = newState;
		host->UpdateDisassembly();
	}
}

GlobalUIState GetUIState() {
	return globalUIState;
}

void SetGPUBackend(GPUBackend type) {
	gpuBackend = type;
}

GPUBackend GetGPUBackend() {
	return gpuBackend;
}

bool IsAudioInitialised() {
	return audioInitialized;
}

void Audio_Init() {
	if (!audioInitialized) {
		audioInitialized = true;
		host->InitSound();
	}
}

void Audio_Shutdown() {
	if (audioInitialized) {
		audioInitialized = false;
		host->ShutdownSound();
	}
}

bool CPU_IsReady() {
	if (coreState == CORE_POWERUP)
		return false;
	return cpuThreadState == CPU_THREAD_RUNNING || cpuThreadState == CPU_THREAD_NOT_RUNNING;
}

bool CPU_IsShutdown() {
	return cpuThreadState == CPU_THREAD_NOT_RUNNING;
}

bool CPU_HasPendingAction() {
	return cpuThreadState != CPU_THREAD_RUNNING;
}

void CPU_Shutdown();

void CPU_Init() {
	coreState = CORE_POWERUP;
	currentMIPS = &mipsr4k;

	g_symbolMap = new SymbolMap();

	// Default memory settings
	// Seems to be the safest place currently..
	Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE; // 32 MB of ram by default

	g_RemasterMode = false;
	g_DoubleTextureCoordinates = false;
	Memory::g_PSPModel = g_Config.iPSPModel;

	std::string filename = coreParameter.fileToStart;
	loadedFile = ResolveFileLoaderTarget(ConstructFileLoader(filename));
#ifdef _M_X64
	if (g_Config.bCacheFullIsoInRam) {
		loadedFile = new RamCachingFileLoader(loadedFile);
	}
#endif
	IdentifiedFileType type = Identify_File(loadedFile);

	// TODO: Put this somewhere better?
	if (coreParameter.mountIso != "") {
		coreParameter.mountIsoLoader = ConstructFileLoader(coreParameter.mountIso);
	}

	MIPSAnalyst::Reset();
	Replacement_Init();

	switch (type) {
	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
	case IdentifiedFileType::PSP_DISC_DIRECTORY:
		InitMemoryForGameISO(loadedFile);
		break;
	case IdentifiedFileType::PSP_PBP:
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
		// This is normal for homebrew.
		// ERROR_LOG(LOADER, "PBP directory resolution failed.");
		InitMemoryForGamePBP(loadedFile);
		break;
	default:
		break;
	}

	// Here we have read the PARAM.SFO, let's see if we need any compatibility overrides.
	// Homebrew usually has an empty discID, and even if they do have a disc id, it's not
	// likely to collide with any commercial ones.
	std::string discID = g_paramSFO.GetDiscID();
	coreParameter.compat.Load(discID);

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

	// If they shut down early, we'll catch it when load completes.
	// Note: this may return before init is complete, which is checked if CPU_IsReady().
	if (!LoadFile(&loadedFile, &coreParameter.errorString)) {
		CPU_Shutdown();
		coreParameter.fileToStart = "";
		return;
	}


	if (coreParameter.updateRecent) {
		g_Config.AddRecent(filename);
	}
}

void CPU_Shutdown() {
	if (g_Config.bAutoSaveSymbolMap) {
		host->SaveSymbolMap();
	}

	Replacement_Shutdown();

	CoreTiming::Shutdown();
	__KernelShutdown();
	HLEShutdown();
	if (coreParameter.enableSound) {
		Audio_Shutdown();
	}
	pspFileSystem.Shutdown();
	mipsr4k.Shutdown();
	Memory::Shutdown();

	delete loadedFile;
	loadedFile = nullptr;

	delete coreParameter.mountIsoLoader;
	delete g_symbolMap;
	g_symbolMap = nullptr;

	coreParameter.mountIsoLoader = nullptr;
}

// TODO: Maybe loadedFile doesn't even belong here...
void UpdateLoadedFile(FileLoader *fileLoader) {
	delete loadedFile;
	loadedFile = fileLoader;
}

void Core_UpdateState(CoreState newState) {
	if ((coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME) && newState != CORE_RUNNING)
		coreStatePending = true;
	coreState = newState;
	Core_UpdateSingleStep();
}

void Core_UpdateDebugStats(bool collectStats) {
	if (coreCollectDebugStats != collectStats) {
		coreCollectDebugStats = collectStats;
		mipsr4k.ClearJitCache();
	}

	kernelStats.ResetFrame();
	gpuStats.ResetFrame();
}

// Ugly!
static bool pspIsInited = false;
static bool pspIsIniting = false;
static bool pspIsQuitting = false;

bool PSP_InitStart(const CoreParameter &coreParam, std::string *error_string) {
	if (pspIsIniting || pspIsQuitting) {
		return false;
	}

#if defined(_WIN32) && defined(_M_X64)
	INFO_LOG(BOOT, "PPSSPP %s Windows 64 bit", PPSSPP_GIT_VERSION);
#elif defined(_WIN32) && !defined(_M_X64)
	INFO_LOG(BOOT, "PPSSPP %s Windows 32 bit", PPSSPP_GIT_VERSION);
#else
	INFO_LOG(BOOT, "PPSSPP %s", PPSSPP_GIT_VERSION);
#endif

	GraphicsContext *temp = coreParameter.graphicsContext;
	coreParameter = coreParam;
	if (coreParameter.graphicsContext == nullptr) {
		coreParameter.graphicsContext = temp;
	}
	coreParameter.errorString = "";
	pspIsIniting = true;

	CPU_Init();

	*error_string = coreParameter.errorString;
	bool success = coreParameter.fileToStart != "";
	if (!success) {
		pspIsIniting = false;
	}
	return success;
}

bool PSP_InitUpdate(std::string *error_string) {
	if (pspIsInited || !pspIsIniting) {
		return true;
	}

	if (!CPU_IsReady()) {
		return false;
	}

	bool success = coreParameter.fileToStart != "";
	*error_string = coreParameter.errorString;
	if (success && gpu == nullptr) {
		success = GPU_Init(coreParameter.graphicsContext, coreParameter.thin3d);
		if (!success) {
			PSP_Shutdown();
			*error_string = "Unable to initialize rendering engine.";
		}
	}
	pspIsInited = success && GPU_IsReady();
	pspIsIniting = success && !pspIsInited;
	return !success || pspIsInited;
}

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string) {
	PSP_InitStart(coreParam, error_string);

	while (!PSP_InitUpdate(error_string))
		sleep_ms(10);
	return pspIsInited;
}

bool PSP_IsIniting() {
	return pspIsIniting;
}

bool PSP_IsInited() {
	return pspIsInited && !pspIsQuitting;
}

void PSP_Shutdown() {
	// Do nothing if we never inited.
	if (!pspIsInited && !pspIsIniting && !pspIsQuitting) {
		return;
	}

#ifndef MOBILE_DEVICE
	if (g_Config.bFuncHashMap) {
		MIPSAnalyst::StoreHashMap();
	}
#endif

	// Make sure things know right away that PSP memory, etc. is going away.
	pspIsQuitting = true;
	if (coreState == CORE_RUNNING)
		Core_UpdateState(CORE_ERROR);
	Core_NotifyShutdown();
	CPU_Shutdown();
	GPU_Shutdown();
	g_paramSFO.Clear();
	host->SetWindowTitle(0);
	currentMIPS = 0;
	pspIsInited = false;
	pspIsIniting = false;
	pspIsQuitting = false;
	g_Config.unloadGameConfig();
}

void PSP_BeginHostFrame() {
	// Reapply the graphics state of the PSP
	if (gpu) {
		gpu->BeginHostFrame();
	}
}

void PSP_EndHostFrame() {
	if (gpu) {
		gpu->EndHostFrame();
	}
}

void PSP_RunLoopUntil(u64 globalticks) {
	SaveState::Process();
	if (coreState == CORE_POWERDOWN || coreState == CORE_ERROR) {
		return;
	}

	mipsr4k.RunLoopUntil(globalticks);
	gpu->CleanupBeforeUI();
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
		return g_Config.memStickDirectory + "PSP/Cheats/";
	case DIRECTORY_GAME:
		return g_Config.memStickDirectory + "PSP/GAME/";
	case DIRECTORY_SAVEDATA:
		return g_Config.memStickDirectory + "PSP/SAVEDATA/";
	case DIRECTORY_SCREENSHOT:
		return g_Config.memStickDirectory + "PSP/SCREENSHOT/";
	case DIRECTORY_SYSTEM:
		return g_Config.memStickDirectory + "PSP/SYSTEM/";
	case DIRECTORY_PAUTH:
		return g_Config.memStickDirectory + "PAUTH/";
	case DIRECTORY_DUMP:
		return g_Config.memStickDirectory + "PSP/SYSTEM/DUMP/";
	case DIRECTORY_SAVESTATE:
		return g_Config.memStickDirectory + "PSP/PPSSPP_STATE/";
	case DIRECTORY_CACHE:
		return g_Config.memStickDirectory + "PSP/SYSTEM/CACHE/";
	case DIRECTORY_TEXTURES:
		return g_Config.memStickDirectory + "PSP/TEXTURES/";
	case DIRECTORY_APP_CACHE:
		if (!g_Config.appCacheDirectory.empty()) {
			return g_Config.appCacheDirectory;
		}
		return g_Config.memStickDirectory + "PSP/SYSTEM/CACHE/";
	case DIRECTORY_VIDEO:
		return g_Config.memStickDirectory + "PSP/VIDEO/";
	case DIRECTORY_AUDIO:
		return g_Config.memStickDirectory + "PSP/AUDIO/";
	// Just return the memory stick root if we run into some sort of problem.
	default:
		ERROR_LOG(FILESYS, "Unknown directory type.");
		return g_Config.memStickDirectory;
	}
}

#if defined(_WIN32)
// Run this at startup time. Please use GetSysDirectory if you need to query where folders are.
void InitSysDirectories() {
	if (!g_Config.memStickDirectory.empty() && !g_Config.flash0Directory.empty())
		return;

	const std::string path = File::GetExeDirectory();

	// Mount a filesystem
	g_Config.flash0Directory = path + "assets/flash0/";

	// Detect the "My Documents"(XP) or "Documents"(on Vista/7/8) folder.
#if PPSSPP_PLATFORM(UWP)
	// We set g_Config.memStickDirectory outside.

#else
	wchar_t myDocumentsPath[MAX_PATH];
	const HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
	const std::string myDocsPath = ConvertWStringToUTF8(myDocumentsPath) + "/PPSSPP/";
	const std::string installedFile = path + "installed.txt";
	const bool installed = File::Exists(installedFile);

	// If installed.txt exists(and we can determine the Documents directory)
	if (installed && (result == S_OK))	{
#if defined(_WIN32) && defined(__MINGW32__)
		std::ifstream inputFile(installedFile);
#else
		std::ifstream inputFile(ConvertUTF8ToWString(installedFile));
#endif

		if (!inputFile.fail() && inputFile.is_open()) {
			std::string tempString;

			std::getline(inputFile, tempString);

			// Skip UTF-8 encoding bytes if there are any. There are 3 of them.
			if (tempString.substr(0, 3) == "\xEF\xBB\xBF")
				tempString = tempString.substr(3);

			g_Config.memStickDirectory = tempString;
		}
		inputFile.close();

		// Check if the file is empty first, before appending the slash.
		if (g_Config.memStickDirectory.empty())
			g_Config.memStickDirectory = myDocsPath;

		size_t lastSlash = g_Config.memStickDirectory.find_last_of("/");
		if (lastSlash != (g_Config.memStickDirectory.length() - 1))
			g_Config.memStickDirectory.append("/");
	} else {
		g_Config.memStickDirectory = path + "memstick/";
	}

	// Create the memstickpath before trying to write to it, and fall back on Documents yet again
	// if we can't make it.
	if (!File::Exists(g_Config.memStickDirectory)) {
		if (!File::CreateDir(g_Config.memStickDirectory))
			g_Config.memStickDirectory = myDocsPath;
		INFO_LOG(COMMON, "Memstick directory not present, creating at '%s'", g_Config.memStickDirectory.c_str());
	}

	const std::string testFile = g_Config.memStickDirectory + "/_writable_test.$$$";

	// If any directory is read-only, fall back to the Documents directory.
	// We're screwed anyway if we can't write to Documents, or can't detect it.
	if (!File::CreateEmptyFile(testFile))
		g_Config.memStickDirectory = myDocsPath;

	// Clean up our mess.
	if (File::Exists(testFile))
		File::Delete(testFile);
#endif

	// Create the default directories that a real PSP creates. Good for homebrew so they can
	// expect a standard environment. Skipping THEME though, that's pointless.
	File::CreateDir(g_Config.memStickDirectory + "PSP");
	File::CreateDir(g_Config.memStickDirectory + "PSP/COMMON");
	File::CreateDir(g_Config.memStickDirectory + "PSP/GAME");
	File::CreateDir(g_Config.memStickDirectory + "PSP/SAVEDATA");
	File::CreateDir(g_Config.memStickDirectory + "PSP/PPSSPP_STATE");
#ifdef ANDROID
	// Avoid media scanners in PPSSPP_STATE directory
	File::CreateEmptyFile(g_Config.memStickDirectory + "PSP/PPSSPP_STATE/.nomedia");
#endif

	if (g_Config.currentDirectory.empty()) {
		g_Config.currentDirectory = GetSysDirectory(DIRECTORY_GAME);
	}
}
#endif

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
#if !PPSSPP_PLATFORM(UWP)
#include "Windows/W32Util/ShellUtil.h"
#endif
#endif

#include <thread>
#include <mutex>
#include <condition_variable>

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/Math/math_util.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/TimeUtil.h"
#include "Common/GraphicsContext.h"

#include "Core/RetroAchievements.h"
#include "Core/MemFault.h"
#include "Core/HDRemaster.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/System.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/Plugins.h"
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
#include "Common/ExceptionHandlerSetup.h"
#include "Core/HLE/sceAudiocodec.h"
#include "GPU/GPUState.h"
#include "GPU/GPUInterface.h"
#include "GPU/Debugger/RecordFormat.h"
#include "Core/RetroAchievements.h"

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
CoreParameter g_CoreParameter;
static FileLoader *g_loadedFile;
// For background loading thread.
static std::mutex loadingLock;
// For loadingReason updates.
static std::mutex loadingReasonLock;
static std::string loadingReason;

bool audioInitialized;

bool coreCollectDebugStats = false;
static int coreCollectDebugStatsCounter = 0;

// This can be read and written from ANYWHERE.
volatile CoreState coreState = CORE_STEPPING;
// If true, core state has been changed, but JIT has probably not noticed yet.
volatile bool coreStatePending = false;

static volatile CPUThreadState cpuThreadState = CPU_THREAD_NOT_RUNNING;

static GPUBackend gpuBackend;
static std::string gpuBackendDevice;

// Ugly!
static volatile bool pspIsInited = false;
static volatile bool pspIsIniting = false;
static volatile bool pspIsQuitting = false;
static volatile bool pspIsRebooting = false;

void ResetUIState() {
	globalUIState = UISTATE_MENU;
}

void UpdateUIState(GlobalUIState newState) {
	// Never leave the EXIT state.
	if (globalUIState != newState && globalUIState != UISTATE_EXIT) {
		globalUIState = newState;
		System_Notify(SystemNotification::DISASSEMBLY);
		const char *state = nullptr;
		switch (globalUIState) {
		case UISTATE_EXIT: state = "exit";  break;
		case UISTATE_INGAME: state = "ingame"; break;
		case UISTATE_MENU: state = "menu"; break;
		case UISTATE_PAUSEMENU: state = "pausemenu"; break;
		case UISTATE_EXCEPTION: state = "exception"; break;
		}
		if (state) {
			System_NotifyUIState(state);
		}
	}
}

GlobalUIState GetUIState() {
	return globalUIState;
}

void SetGPUBackend(GPUBackend type, const std::string &device) {
	gpuBackend = type;
	gpuBackendDevice = device;
}

GPUBackend GetGPUBackend() {
	return gpuBackend;
}

std::string GetGPUBackendDevice() {
	return gpuBackendDevice;
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

static Path SymbolMapFilename(const Path &currentFilename, const char *ext) {
	File::FileInfo info{};
	// can't fail, definitely exists if it gets this far
	File::GetFileInfo(currentFilename, &info);
	if (info.isDirectory) {
		return currentFilename / (std::string(".ppsspp-symbols") + ext);
	}
	return currentFilename.WithReplacedExtension(ext);
};

static bool LoadSymbolsIfSupported() {
	if (System_GetPropertyBool(SYSPROP_HAS_DEBUGGER)) {
		if (!g_symbolMap)
			return false;

		if (PSP_CoreParameter().fileToStart.Type() == PathType::HTTP) {
			// We don't support loading symbols over HTTP.
			g_symbolMap->Clear();
			return true;
		}

		bool result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".ppmap"));
		// Load the old-style map file.
		if (!result1)
			result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".map"));
		bool result2 = g_symbolMap->LoadNocashSym(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".sym"));
		return result1 || result2;
	} else {
		g_symbolMap->Clear();
		return true;
	}
}

static bool SaveSymbolMapIfSupported() {
	if (g_symbolMap) {
		return g_symbolMap->SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".ppmap"));
	}
	return false;
}

bool DiscIDFromGEDumpPath(const Path &path, FileLoader *fileLoader, std::string *id) {
	using namespace GPURecord;

	// For newer files, it's stored in the dump.
	Header header;
	if (fileLoader->ReadAt(0, sizeof(header), &header) == sizeof(header)) {
		const bool magicMatch = memcmp(header.magic, HEADER_MAGIC, sizeof(header.magic)) == 0;
		if (magicMatch && header.version <= VERSION && header.version >= 4) {
			size_t gameIDLength = strnlen(header.gameID, sizeof(header.gameID));
			if (gameIDLength != 0) {
				*id = std::string(header.gameID, gameIDLength);
				return true;
			}
		}
	}

	// Fall back to using the filename.
	std::string filename = path.GetFilename();
	// Could be more discerning, but hey..
	if (filename.size() > 10 && (filename[0] == 'U' || filename[0] == 'N') && filename[9] == '_') {
		*id = filename.substr(0, 9);
		return true;
	} else {
		return false;
	}
}

bool CPU_Init(std::string *errorString, FileLoader *loadedFile) {
	coreState = CORE_POWERUP;
	currentMIPS = &mipsr4k;

	g_symbolMap = new SymbolMap();

	// Default memory settings
	// Seems to be the safest place currently..
	Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE; // 32 MB of ram by default

	g_RemasterMode = false;
	g_DoubleTextureCoordinates = false;
	Memory::g_PSPModel = g_Config.iPSPModel;

	Path filename = g_CoreParameter.fileToStart;

	IdentifiedFileType type = Identify_File(loadedFile, errorString);

	// TODO: Put this somewhere better?
	if (!g_CoreParameter.mountIso.empty()) {
		g_CoreParameter.mountIsoLoader = ConstructFileLoader(g_CoreParameter.mountIso);
	}

	MIPSAnalyst::Reset();
	Replacement_Init();

	bool allowPlugins = true;
	std::string geDumpDiscID;

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
	case IdentifiedFileType::PSP_ELF:
		if (Memory::g_PSPModel != PSP_MODEL_FAT) {
			INFO_LOG(LOADER, "ELF, using full PSP-2000 memory access");
			Memory::g_MemorySize = Memory::RAM_DOUBLE_SIZE;
		}
		break;
	case IdentifiedFileType::PPSSPP_GE_DUMP:
		// Try to grab the disc ID from the filename or GE dump.
		if (DiscIDFromGEDumpPath(filename, loadedFile, &geDumpDiscID)) {
			// Store in SFO, otherwise it'll generate a fake disc ID.
			g_paramSFO.SetValue("DISC_ID", geDumpDiscID, 16);
		}
		allowPlugins = false;
		break;
	default:
		// Can we even get here?
		WARN_LOG(LOADER, "CPU_Init didn't recognize file. %s", errorString->c_str());
		break;
	}

	// Here we have read the PARAM.SFO, let's see if we need any compatibility overrides.
	// Homebrew usually has an empty discID, and even if they do have a disc id, it's not
	// likely to collide with any commercial ones.
	g_CoreParameter.compat.Load(g_paramSFO.GetDiscID());

	InitVFPU();

	if (allowPlugins)
		HLEPlugins::Init();
	if (!Memory::Init()) {
		// We're screwed.
		*errorString = "Memory init failed";
		return false;
	}
	mipsr4k.Reset();

	LoadSymbolsIfSupported();

	CoreTiming::Init();

	// Init all the HLE modules
	HLEInit();

	// TODO: Check Game INI here for settings, patches and cheats, and modify coreParameter accordingly

	// If they shut down early, we'll catch it when load completes.
	// Note: this may return before init is complete, which is checked if CPU_IsReady().
	g_loadedFile = loadedFile;
	if (!LoadFile(&loadedFile, &g_CoreParameter.errorString)) {
		CPU_Shutdown();
		g_CoreParameter.fileToStart.clear();
		return false;
	}

	if (g_CoreParameter.updateRecent) {
		g_Config.AddRecent(filename.ToString());
	}

	InstallExceptionHandler(&Memory::HandleFault);
	return true;
}

PSP_LoadingLock::PSP_LoadingLock() {
	loadingLock.lock();
}

PSP_LoadingLock::~PSP_LoadingLock() {
	loadingLock.unlock();
}

void CPU_Shutdown() {
	UninstallExceptionHandler();

	// Since we load on a background thread, wait for startup to complete.
	PSP_LoadingLock lock;
	PSPLoaders_Shutdown();

	if (g_Config.bAutoSaveSymbolMap) {
		SaveSymbolMapIfSupported();
	}

	Replacement_Shutdown();

	CoreTiming::Shutdown();
	__KernelShutdown();
	HLEShutdown();

	pspFileSystem.Shutdown();
	mipsr4k.Shutdown();
	Memory::Shutdown();
	HLEPlugins::Shutdown();

	delete g_loadedFile;
	g_loadedFile = nullptr;

	delete g_CoreParameter.mountIsoLoader;
	delete g_symbolMap;
	g_symbolMap = nullptr;

	g_CoreParameter.mountIsoLoader = nullptr;
}

// TODO: Maybe loadedFile doesn't even belong here...
void UpdateLoadedFile(FileLoader *fileLoader) {
	delete g_loadedFile;
	g_loadedFile = fileLoader;
}

void Core_UpdateState(CoreState newState) {
	if ((coreState == CORE_RUNNING || coreState == CORE_NEXTFRAME) && newState != CORE_RUNNING)
		coreStatePending = true;
	coreState = newState;
	Core_UpdateSingleStep();
}

void Core_UpdateDebugStats(bool collectStats) {
	bool newState = collectStats || coreCollectDebugStatsCounter > 0;
	if (coreCollectDebugStats != newState) {
		coreCollectDebugStats = newState;
		mipsr4k.ClearJitCache();
	}

	if (!PSP_CoreParameter().frozen && !Core_IsStepping()) {
		kernelStats.ResetFrame();
		gpuStats.ResetFrame();
	}
}

void Core_ForceDebugStats(bool enable) {
	if (enable) {
		coreCollectDebugStatsCounter++;
	} else {
		coreCollectDebugStatsCounter--;
	}
	_assert_(coreCollectDebugStatsCounter >= 0);
}

bool PSP_InitStart(const CoreParameter &coreParam, std::string *error_string) {
	if (pspIsIniting || pspIsQuitting) {
		return false;
	}

	if (!Achievements::IsReadyToStart()) {
		return false;
	}

#if defined(_WIN32) && PPSSPP_ARCH(AMD64)
	NOTICE_LOG(BOOT, "PPSSPP %s Windows 64 bit", PPSSPP_GIT_VERSION);
#elif defined(_WIN32) && !PPSSPP_ARCH(AMD64)
	NOTICE_LOG(BOOT, "PPSSPP %s Windows 32 bit", PPSSPP_GIT_VERSION);
#else
	NOTICE_LOG(BOOT, "PPSSPP %s", PPSSPP_GIT_VERSION);
#endif

	Core_NotifyLifecycle(CoreLifecycle::STARTING);
	GraphicsContext *temp = g_CoreParameter.graphicsContext;
	g_CoreParameter = coreParam;
	if (g_CoreParameter.graphicsContext == nullptr) {
		g_CoreParameter.graphicsContext = temp;
	}
	g_CoreParameter.errorString.clear();
	pspIsIniting = true;
	PSP_SetLoading("Loading game...");

	Path filename = g_CoreParameter.fileToStart;
	FileLoader *loadedFile = ResolveFileLoaderTarget(ConstructFileLoader(filename));
#if PPSSPP_ARCH(AMD64)
	if (g_Config.bCacheFullIsoInRam) {
		loadedFile = new RamCachingFileLoader(loadedFile);
	}
#endif

	if (g_Config.bAchievementsEnable) {
		// Need to re-identify after ResolveFileLoaderTarget - although in practice probably not,
		// but also, re-using the identification would require some plumbing, to be done later.
		std::string errorString;
		IdentifiedFileType type = Identify_File(loadedFile, &errorString);
		Achievements::SetGame(filename, type, loadedFile);
	}

	if (!CPU_Init(&g_CoreParameter.errorString, loadedFile)) {
		*error_string = g_CoreParameter.errorString;
		if (error_string->empty()) {
			*error_string = "Failed initializing CPU/Memory";
		}
		pspIsIniting = false;
		return false;
	}

	// Compat flags get loaded in CPU_Init (which is a bit of a misnomer) so we check for SW renderer here.
	if (g_Config.bSoftwareRendering || PSP_CoreParameter().compat.flags().ForceSoftwareRenderer) {
		g_CoreParameter.gpuCore = GPUCORE_SOFTWARE;
	}

	*error_string = g_CoreParameter.errorString;
	bool success = !g_CoreParameter.fileToStart.empty();
	if (!success) {
		Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
		pspIsRebooting = false;
		// In this case, we must call shutdown since the caller won't know to.
		// It must've partially started since CPU_Init returned true.
		PSP_Shutdown();
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

	bool success = !g_CoreParameter.fileToStart.empty();
	if (!g_CoreParameter.errorString.empty()) {
		*error_string = g_CoreParameter.errorString;
	}

	if (success && gpu == nullptr) {
		PSP_SetLoading("Starting graphics...");
		Draw::DrawContext *draw = g_CoreParameter.graphicsContext ? g_CoreParameter.graphicsContext->GetDrawContext() : nullptr;
		success = GPU_Init(g_CoreParameter.graphicsContext, draw);
		if (!success) {
			*error_string = "Unable to initialize rendering engine.";
		}
	}
	if (!success) {
		pspIsRebooting = false;
		PSP_Shutdown();
		return true;
	}

	pspIsInited = GPU_IsReady();
	pspIsIniting = !pspIsInited;
	if (pspIsInited) {
		Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
		pspIsRebooting = false;

		// If GPU init failed during IsReady checks, bail.
		if (!GPU_IsStarted()) {
			*error_string = "Unable to initialize rendering engine.";
			pspIsRebooting = false;
			PSP_Shutdown();
			return true;
		}
	}
	return pspIsInited;
}

bool PSP_Init(const CoreParameter &coreParam, std::string *error_string) {
	if (!PSP_InitStart(coreParam, error_string))
		return false;

	while (!PSP_InitUpdate(error_string))
		sleep_ms(10);
	return pspIsInited;
}

bool PSP_IsIniting() {
	return pspIsIniting;
}

bool PSP_IsInited() {
	return pspIsInited && !pspIsQuitting && !pspIsRebooting;
}

bool PSP_IsRebooting() {
	return pspIsRebooting;
}

bool PSP_IsQuitting() {
	return pspIsQuitting;
}

void PSP_Shutdown() {
	Achievements::UnloadGame();

	// Do nothing if we never inited.
	if (!pspIsInited && !pspIsIniting && !pspIsQuitting) {
		return;
	}

	// Make sure things know right away that PSP memory, etc. is going away.
	pspIsQuitting = !pspIsRebooting;
	if (coreState == CORE_RUNNING)
		Core_Stop();

	if (g_Config.bFuncHashMap) {
		MIPSAnalyst::StoreHashMap();
	}

	if (pspIsIniting)
		Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
	Core_NotifyLifecycle(CoreLifecycle::STOPPING);
	CPU_Shutdown();
	GPU_Shutdown();
	g_paramSFO.Clear();
	System_SetWindowTitle("");
	currentMIPS = 0;
	pspIsInited = false;
	pspIsIniting = false;
	pspIsQuitting = false;
	g_Config.unloadGameConfig();
	Core_NotifyLifecycle(CoreLifecycle::STOPPED);
}

bool PSP_Reboot(std::string *error_string) {
	if (!pspIsInited || pspIsQuitting)
		return false;

	pspIsRebooting = true;
	Core_Stop();
	Core_WaitInactive();
	PSP_Shutdown();
	std::string resetError;
	return PSP_Init(PSP_CoreParameter(), error_string);
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
	SaveState::Cleanup();
}

void PSP_RunLoopWhileState() {
	// We just run the CPU until we get to vblank. This will quickly sync up pretty nicely.
	// The actual number of cycles doesn't matter so much here as we will break due to CORE_NEXTFRAME, most of the time hopefully...
	int blockTicks = usToCycles(1000000 / 10);

	// Run until CORE_NEXTFRAME
	while (coreState == CORE_RUNNING || coreState == CORE_STEPPING) {
		PSP_RunLoopFor(blockTicks);
		if (coreState == CORE_STEPPING) {
			// Keep the UI responsive.
			break;
		}
	}
}

void PSP_RunLoopUntil(u64 globalticks) {
	SaveState::Process();
	if (coreState == CORE_POWERDOWN || coreState == CORE_BOOT_ERROR || coreState == CORE_RUNTIME_ERROR) {
		return;
	} else if (coreState == CORE_STEPPING) {
		Core_ProcessStepping();
		return;
	}

	mipsr4k.RunLoopUntil(globalticks);
}

void PSP_RunLoopFor(int cycles) {
	PSP_RunLoopUntil(CoreTiming::GetTicks() + cycles);
}

void PSP_SetLoading(const std::string &reason) {
	std::lock_guard<std::mutex> guard(loadingReasonLock);
	loadingReason = reason;
}

std::string PSP_GetLoading() {
	std::lock_guard<std::mutex> guard(loadingReasonLock);
	return loadingReason;
}

Path GetSysDirectory(PSPDirectories directoryType) {
	const Path &memStickDirectory = g_Config.memStickDirectory;
	Path pspDirectory;
	if (!strcasecmp(memStickDirectory.GetFilename().c_str(), "PSP")) {
		// Let's strip this off, to easily allow choosing a root directory named "PSP" on Android.
		pspDirectory = memStickDirectory;
	} else {
		pspDirectory = memStickDirectory / "PSP";
	}

	switch (directoryType) {
	case DIRECTORY_PSP:
		return pspDirectory;
	case DIRECTORY_CHEATS:
		return pspDirectory / "Cheats";
	case DIRECTORY_GAME:
		return pspDirectory / "GAME";
	case DIRECTORY_SAVEDATA:
		return pspDirectory / "SAVEDATA";
	case DIRECTORY_SCREENSHOT:
		return pspDirectory / "SCREENSHOT";
	case DIRECTORY_SYSTEM:
		return pspDirectory / "SYSTEM";
	case DIRECTORY_PAUTH:
		return memStickDirectory / "PAUTH";  // This one's at the root...
	case DIRECTORY_EXDATA:
		return memStickDirectory / "EXDATA";  // This one's traditionally at the root...
	case DIRECTORY_DUMP:
		return pspDirectory / "SYSTEM/DUMP";
	case DIRECTORY_SAVESTATE:
		return pspDirectory / "PPSSPP_STATE";
	case DIRECTORY_CACHE:
		return pspDirectory / "SYSTEM/CACHE";
	case DIRECTORY_TEXTURES:
		return pspDirectory / "TEXTURES";
	case DIRECTORY_PLUGINS:
		return pspDirectory / "PLUGINS";
	case DIRECTORY_APP_CACHE:
		if (!g_Config.appCacheDirectory.empty()) {
			return g_Config.appCacheDirectory;
		}
		return pspDirectory / "SYSTEM/CACHE";
	case DIRECTORY_VIDEO:
		return pspDirectory / "VIDEO";
	case DIRECTORY_AUDIO:
		return pspDirectory / "AUDIO";
	case DIRECTORY_CUSTOM_SHADERS:
		return pspDirectory / "shaders";
	case DIRECTORY_CUSTOM_THEMES:
		return pspDirectory / "themes";

	case DIRECTORY_MEMSTICK_ROOT:
		return g_Config.memStickDirectory;
	// Just return the memory stick root if we run into some sort of problem.
	default:
		ERROR_LOG(FILESYS, "Unknown directory type.");
		return g_Config.memStickDirectory;
	}
}

bool CreateSysDirectories() {
#if PPSSPP_PLATFORM(ANDROID)
	const bool createNoMedia = true;
#else
	const bool createNoMedia = false;
#endif

	Path pspDir = GetSysDirectory(DIRECTORY_PSP);
	INFO_LOG(IO, "Creating '%s' and subdirs:", pspDir.c_str());
	File::CreateDir(pspDir);
	if (!File::Exists(pspDir)) {
		INFO_LOG(IO, "Not a workable memstick directory. Giving up");
		return false;
	}

	// Create the default directories that a real PSP creates. Good for homebrew so they can
	// expect a standard environment. Skipping THEME though, that's pointless.
	static const PSPDirectories sysDirs[] = {
		DIRECTORY_CHEATS,
		DIRECTORY_SAVEDATA,
		DIRECTORY_SAVESTATE,
		DIRECTORY_GAME,
		DIRECTORY_SYSTEM,
		DIRECTORY_TEXTURES,
		DIRECTORY_PLUGINS,
		DIRECTORY_CACHE,
	};

	for (auto dir : sysDirs) {
		Path path = GetSysDirectory(dir);
		File::CreateFullPath(path);
		if (createNoMedia) {
			// Create a nomedia file in each specified subdirectory.
			File::CreateEmptyFile(path / ".nomedia");
		}
	}

	return true;
}

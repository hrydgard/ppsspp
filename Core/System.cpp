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
#if !PPSSPP_PLATFORM(UWP)
#include "Windows/W32Util/ShellUtil.h"
#endif
#endif

#include <mutex>

#if !PPSSPP_PLATFORM(SWITCH)
#include "ext/lua/lapi.h"
#endif

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/GraphicsContext.h"
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
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/FileLoaders/RamCachingFileLoader.h"
#if !PPSSPP_PLATFORM(SWITCH)
#include "Core/LuaContext.h"
#endif
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/Loaders.h"
#include "Core/PSPLoaders.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/SaveState.h"
#include "Core/Util/RecentFiles.h"
#include "Common/StringUtils.h"
#include "Common/ExceptionHandlerSetup.h"
#include "GPU/GPUCommon.h"
#include "GPU/Debugger/Playback.h"
#include "GPU/Debugger/RecordFormat.h"
#include "UI/DiscordIntegration.h"

enum CPUThreadState {
	CPU_THREAD_NOT_RUNNING,
	CPU_THREAD_RUNNING,
};

MetaFileSystem pspFileSystem;
ParamSFOData g_paramSFO;
static GlobalUIState globalUIState;
CoreParameter g_CoreParameter;
static FileLoader *g_loadedFile;
// For background loading thread.
static std::mutex loadingLock;
static std::thread g_loadingThread;

bool coreCollectDebugStats = false;
static int coreCollectDebugStatsCounter = 0;

static volatile CPUThreadState cpuThreadState = CPU_THREAD_NOT_RUNNING;

static GPUBackend gpuBackend;
static std::string gpuBackendDevice;

static BootState g_bootState = BootState::Off;

BootState PSP_GetBootState() {
	return g_bootState;
}

FileLoader *PSP_LoadedFile() {
	return g_loadedFile;
}

void ResetUIState() {
	globalUIState = UISTATE_MENU;
}

void UpdateUIState(GlobalUIState newState) {
	// Never leave the EXIT state.
	if (globalUIState != newState && globalUIState != UISTATE_EXIT) {
		globalUIState = newState;
		System_Notify(SystemNotification::DISASSEMBLY);
		System_Notify(SystemNotification::UI_STATE_CHANGED);
		System_SetKeepScreenBright(globalUIState == UISTATE_INGAME);
	}
}

GlobalUIState GetUIState() {
	return globalUIState;
}

void SetGPUBackend(GPUBackend type, std::string_view device) {
	gpuBackend = type;
	gpuBackendDevice = device;
}

GPUBackend GetGPUBackend() {
	return gpuBackend;
}

std::string GetGPUBackendDevice() {
	return gpuBackendDevice;
}

void CPU_Shutdown(bool success);

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

static void GetBootError(IdentifiedFileType type, std::string *errorString) {
	switch (type) {
	case IdentifiedFileType::ARCHIVE_RAR:
#ifdef WIN32
		*errorString = "RAR file detected (Require WINRAR)";
#else
		*errorString = "RAR file detected (Require UnRAR)";
#endif
		break;

	case IdentifiedFileType::ARCHIVE_ZIP:
#ifdef WIN32
		*errorString = "ZIP file detected (Require WINRAR)";
#else
		*errorString = "ZIP file detected (Require UnRAR)";
#endif
		break;

	case IdentifiedFileType::ARCHIVE_7Z: *errorString = "7z file detected (Require 7-Zip)"; break;
	case IdentifiedFileType::ISO_MODE2:  *errorString = "PSX game image detected."; break;
	case IdentifiedFileType::NORMAL_DIRECTORY: *errorString = "Just a directory."; break;
	case IdentifiedFileType::PPSSPP_SAVESTATE: *errorString = "This is a saved state, not a game."; break; // Actually, we could make it load it...
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY: *errorString = "This is save data, not a game."; break;
	case IdentifiedFileType::PSP_PS1_PBP: *errorString = "PS1 EBOOTs are not supported by PPSSPP."; break;
	case IdentifiedFileType::UNKNOWN_BIN:
	case IdentifiedFileType::UNKNOWN_ELF:
	case IdentifiedFileType::UNKNOWN_ISO:
	case IdentifiedFileType::UNKNOWN: *errorString = "Unknown executable file type."; break;
	case IdentifiedFileType::ERROR_IDENTIFYING: *errorString = "Error identifying file."; break;
	default:
		*errorString = StringFromFormat("Unhandled identified file type %d", (int)type);
		break;
	}
}

static void ShowCompatWarnings(const Compatibility &compat) {
	// UI changes are best done after PSP_InitStart.
	if (compat.flags().RequireBufferedRendering && g_Config.bSkipBufferEffects && !g_Config.bSoftwareRendering) {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("BufferedRenderingRequired", "Warning: This game requires Rendering Mode to be set to Buffered."), 10.0f);
	}

	if (compat.flags().RequireBlockTransfer && g_Config.iSkipGPUReadbackMode != (int)SkipGPUReadbackMode::NO_SKIP && !PSP_CoreParameter().compat.flags().ForceEnableGPUReadback) {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("BlockTransferRequired", "Warning: This game requires Skip GPU Readbacks be set to No."), 10.0f);
	}

	if (compat.flags().RequireDefaultCPUClock && g_Config.iLockedCPUSpeed != 0) {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("DefaultCPUClockRequired", "Warning: This game requires the CPU clock to be set to default."), 10.0f);
	}
}

extern const std::string INDEX_FILENAME;

// NOTE: The loader has already been fully resolved (ResolveFileLoaderTarget) and identified here.
static bool CPU_Init(FileLoader *fileLoader, IdentifiedFileType type, std::string *errorString) {
	// Default memory settings
	// Seems to be the safest place currently..
	Memory::g_MemorySize = Memory::RAM_NORMAL_SIZE; // 32 MB of ram by default

	g_RemasterMode = false;
	g_DoubleTextureCoordinates = false;
	Memory::g_PSPModel = g_Config.iPSPModel;

	g_CoreParameter.fileType = type;

	std::string geDumpDiscID;

	std::string gameTitle = "Unidentified PSP title";

	switch (type) {
	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
	case IdentifiedFileType::PSP_DISC_DIRECTORY:
		// Doesn't seem to take ownership of fileLoader?
		if (!MountGameISO(fileLoader)) {
			*errorString = "Failed to mount ISO file - invalid format?";
			return false;
		}
		if (LoadParamSFOFromDisc()) {
			InitMemorySizeForGame();
		}
		if (type == IdentifiedFileType::PSP_DISC_DIRECTORY) {
			// Check for existence of ppsspp-index.lst - if it exists, the user likely knows what they're doing.
			// TODO: Better would be to check that it was loaded successfully.
			if (!File::Exists(g_CoreParameter.fileToStart / INDEX_FILENAME)) {
				auto sc = GetI18NCategory(I18NCat::SCREEN);
				g_OSD.Show(OSDType::MESSAGE_CENTERED_WARNING, sc->T("ExtractedIsoWarning", "Extracted ISOs often don't work.\nPlay the ISO file directly."), g_CoreParameter.fileToStart.ToVisualString(), 7.0f);
			} else {
				INFO_LOG(Log::Loader, "Extracted ISO loaded without warning - %s is present.", INDEX_FILENAME.c_str());
			}
		}
		break;
	case IdentifiedFileType::PSP_PBP:
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
		// This is normal for homebrew.
		// ERROR_LOG(Log::Loader, "PBP directory resolution failed.");
		if (LoadParamSFOFromPBP(fileLoader)) {
			InitMemorySizeForGame();
		}
		break;
	case IdentifiedFileType::PSP_ELF:
		if (Memory::g_PSPModel != PSP_MODEL_FAT) {
			INFO_LOG(Log::Loader, "ELF, using full PSP-2000 memory access");
			Memory::g_MemorySize = Memory::RAM_DOUBLE_SIZE;
		}
		break;
	case IdentifiedFileType::PPSSPP_GE_DUMP:
		// Try to grab the disc ID from the filename or GE dump.
		if (DiscIDFromGEDumpPath(g_CoreParameter.fileToStart, fileLoader, &geDumpDiscID)) {
			// Store in SFO, otherwise it'll generate a fake disc ID.
			g_paramSFO.SetValue("DISC_ID", geDumpDiscID, 16);
			gameTitle = g_CoreParameter.fileToStart.GetFilename();
		}
		break;
	default:
	{
		// Trying to boot other things lands us here. We need to return a sensible error string.
		ERROR_LOG(Log::Loader, "CPU_Init didn't recognize file. %s", errorString->c_str());
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		*errorString = sy->T("Not a PSP game");  // best string we have.
		return false;
	}
	}

	// OK, pretty confident we have a PSP game.
	if (g_paramSFO.IsValid()) {
		gameTitle = SanitizeString(g_paramSFO.GetValueString("TITLE"), StringRestriction::NoLineBreaksOrSpecials);
	}

	const std::string id = g_paramSFO.GetValueString("DISC_ID");
	const std::string windowTitle = id.empty() ? gameTitle : id + " : " + gameTitle;
	INFO_LOG(Log::Loader, "%s", windowTitle.c_str());
	System_SetWindowTitle(windowTitle);

	auto sc = GetI18NCategory(I18NCat::SCREEN);

	g_symbolMap = new SymbolMap();

	MIPSAnalyst::Reset();
	Replacement_Init();

#if !PPSSPP_PLATFORM(SWITCH)
	g_lua.Init();
#endif

	// Here we have read the PARAM.SFO, let's see if we need any compatibility overrides.
	// Homebrew usually has an empty discID, and even if they do have a disc id, it's not
	// likely to collide with any commercial ones.
	g_CoreParameter.compat.Load(g_paramSFO.GetDiscID());
	ShowCompatWarnings(g_CoreParameter.compat);

	// Compat settings can override the software renderer, take care of that here.
	if (g_Config.bSoftwareRendering || PSP_CoreParameter().compat.flags().ForceSoftwareRenderer) {
		g_CoreParameter.gpuCore = GPUCORE_SOFTWARE;
	}

	// This must be before Memory::Init because plugins can override the memory size.
	if (type != IdentifiedFileType::PPSSPP_GE_DUMP) {
		HLEPlugins::Init();
	}

	// Initialize the memory map as early as possible (now that we've read the PARAM.SFO).
	if (!Memory::Init()) {
		// We're screwed.
		*errorString = "Memory init failed";
		return false;
	}

	InitVFPU();

	LoadSymbolsIfSupported();

	mipsr4k.Reset();

	CoreTiming::Init();

	// Init all the HLE modules
	HLEInit();

	// TODO: Put this somewhere better?
	if (!g_CoreParameter.mountIso.empty()) {
		g_CoreParameter.mountIsoLoader = ConstructFileLoader(g_CoreParameter.mountIso);
	}

	// TODO: Check Game INI here for settings, patches and cheats, and modify coreParameter accordingly

	// If they shut down early, we'll catch it when load completes.
	// Note: this may return before init is complete, which is checked if CPU_IsReady().
	g_loadedFile = fileLoader;

	switch (type) {
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
	{
		std::string dir = fileLoader->GetPath().GetDirectory();
		if (fileLoader->GetPath().Type() == PathType::CONTENT_URI) {
			dir = AndroidContentURI(dir).FilePath();
		}
		size_t pos = dir.find("PSP/GAME/");
		if (pos != std::string::npos) {
			dir = ResolvePBPDirectory(Path(dir)).ToString();
			pspFileSystem.SetStartingDirectory("ms0:/" + dir.substr(pos));
		}
		if (!Load_PSP_ELF_PBP(fileLoader, errorString)) {
			return false;
		}
		break;
	}
	// Looks like a wrong fall through but is not, both paths are handled above.

	case IdentifiedFileType::PSP_PBP:
	case IdentifiedFileType::PSP_ELF:
	{
		INFO_LOG(Log::Loader, "File is an ELF or loose PBP! %s", fileLoader->GetPath().c_str());
		if (!Load_PSP_ELF_PBP(fileLoader, errorString)) {
			return false;
		}
		break;
	}

	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_ISO_NP:
	case IdentifiedFileType::PSP_DISC_DIRECTORY:	// behaves the same as the mounting is already done by now
		pspFileSystem.SetStartingDirectory("disc0:/PSP_GAME/USRDIR");
		if (!Load_PSP_ISO(fileLoader, errorString)) {
			return false;
		}
		break;

	case IdentifiedFileType::PPSSPP_GE_DUMP:
		if (!Load_PSP_GE_Dump(fileLoader, errorString)) {
			return false;
		}
		break;

	default:
		GetBootError(type, errorString);
		g_CoreParameter.fileToStart.clear();
		return false;
	}

	if (g_CoreParameter.updateRecent) {
		g_recentFiles.Add(g_CoreParameter.fileToStart.ToString());
	}

	InstallExceptionHandler(&Memory::HandleFault);
	return true;
}

void CPU_Shutdown(bool success) {
	UninstallExceptionHandler();

	GPURecord::Replay_Unload();

	if (g_Config.bAutoSaveSymbolMap && success) {
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
	g_CoreParameter.mountIsoLoader = nullptr;
	delete g_symbolMap;
	g_symbolMap = nullptr;

#if !PPSSPP_PLATFORM(SWITCH)
	g_lua.Shutdown();
#endif
}

// Used for UMD switching only.
void UpdateLoadedFile(FileLoader *fileLoader) {
	delete g_loadedFile;
	g_loadedFile = fileLoader;
}

void PSP_UpdateDebugStats(bool collectStats) {
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

void PSP_ForceDebugStats(bool enable) {
	if (enable) {
		coreCollectDebugStatsCounter++;
	} else {
		coreCollectDebugStatsCounter--;
	}
	_assert_(coreCollectDebugStatsCounter >= 0);
}

bool PSP_InitStart(const CoreParameter &coreParam) {
	if (g_bootState != BootState::Off) {
		ERROR_LOG(Log::Loader, "Can't start loader thread - already on.");
		return false;
	}

	g_bootState = BootState::Booting;

	GraphicsContext *temp = g_CoreParameter.graphicsContext;
	g_CoreParameter = coreParam;
	if (g_CoreParameter.graphicsContext == nullptr) {
		g_CoreParameter.graphicsContext = temp;
	}
	g_CoreParameter.errorString.clear();

	std::string *error_string = &g_CoreParameter.errorString;

	INFO_LOG(Log::Loader, "Starting loader thread...");

	_dbg_assert_(!g_loadingThread.joinable());

	g_loadingThread = std::thread([error_string]() {
		SetCurrentThreadName("ExecLoader");

		AndroidJNIThreadContext jniContext;

		NOTICE_LOG(Log::Boot, "PPSSPP %s", PPSSPP_GIT_VERSION);

		Core_NotifyLifecycle(CoreLifecycle::STARTING);

		Path filename = g_CoreParameter.fileToStart;
		FileLoader *loadedFile = ResolveFileLoaderTarget(ConstructFileLoader(filename));

		IdentifiedFileType type = Identify_File(loadedFile, &g_CoreParameter.errorString);
		g_CoreParameter.fileType = type;

		if (System_GetPropertyBool(SYSPROP_ENOUGH_RAM_FOR_FULL_ISO)) {
			if (g_Config.bCacheFullIsoInRam) {
				switch (g_CoreParameter.fileType) {
				case IdentifiedFileType::PSP_ISO:
				case IdentifiedFileType::PSP_ISO_NP:
					loadedFile = new RamCachingFileLoader(loadedFile);
					break;
				default:
					INFO_LOG(Log::Loader, "RAM caching is on, but file is not an ISO, so ignoring");
					break;
				}
			}
		}

		// TODO: The reason we pass in g_CoreParameter.errorString here is that it's persistent -
		// it gets written to from the loader thread that gets spawned.
		if (!CPU_Init(loadedFile, type, &g_CoreParameter.errorString)) {
			CPU_Shutdown(false);
			g_CoreParameter.fileToStart.clear();
			*error_string = g_CoreParameter.errorString;
			if (error_string->empty()) {
				*error_string = "Failed initializing CPU/Memory";
			}
			g_bootState = BootState::Failed;
			return;
		}

		g_bootState = BootState::Complete;
	});

	return true;
}

BootState PSP_InitUpdate(std::string *error_string) {
	if (g_bootState == BootState::Booting || g_bootState == BootState::Off) {
		// Nothing to do right now.
		return g_bootState;
	}

	_dbg_assert_(g_bootState == BootState::Complete || g_bootState == BootState::Failed);

	// Since we load on a background thread, wait for startup to complete.
	_dbg_assert_(g_loadingThread.joinable());
	g_loadingThread.join();

	if (g_bootState == BootState::Failed) {
		// Failed! (Note: PSP_Shutdown was already called on the loader thread).
		Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
		*error_string = g_CoreParameter.errorString;
		return g_bootState;
	}

	// Ok, async boot completed, let's finish up things on the main thread.
	if (!gpu) {  // should be!
		INFO_LOG(Log::Loader, "Starting graphics...");
		Draw::DrawContext *draw = g_CoreParameter.graphicsContext ? g_CoreParameter.graphicsContext->GetDrawContext() : nullptr;
		// This set the `gpu` global.
		bool success = GPU_Init(g_CoreParameter.graphicsContext, draw);
		if (!success) {
			*error_string = "Unable to initialize rendering engine.";
			PSP_Shutdown(false);
			g_bootState = BootState::Failed;
			return g_bootState;
		}
	}

	// TODO: This should all be checked during GPU_Init.
	if (!GPU_IsStarted()) {
		*error_string = "Unable to initialize rendering engine.";
		PSP_Shutdown(false);
		g_bootState = BootState::Failed;
	}

	Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
	return g_bootState;
}

// Most platforms should not use this one, they should call PSP_InitStart and then do their thing
// while repeatedly calling PSP_InitUpdate. This is basically just for libretro convenience.
BootState PSP_Init(const CoreParameter &coreParam, std::string *error_string) {
	// InitStart doesn't really fail anymore.
	if (!PSP_InitStart(coreParam))
		return BootState::Failed;

	while (true) {
		BootState state = PSP_InitUpdate(error_string);
		if (state != BootState::Booting) {
			return state;
		}
		sleep_ms(5, "psp-init-poll");
	}
}

void PSP_Shutdown(bool success) {
	// Reduce the risk for weird races with the Windows GE debugger.
	gpuDebug = nullptr;

	// Do nothing if we never inited.
	if (g_bootState == BootState::Off) {
		return;
	}

	Core_Stop();

	if (g_Config.bFuncHashMap) {
		MIPSAnalyst::StoreHashMap();
	}

	if (g_bootState == BootState::Booting) {
		// This should only happen during failures.
		Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
	}
	Core_NotifyLifecycle(CoreLifecycle::STOPPING);

	CPU_Shutdown(success);
	GPU_Shutdown();
	g_paramSFO.Clear();
	System_SetWindowTitle("");

	currentMIPS = nullptr;

	g_Config.unloadGameConfig();

	Core_NotifyLifecycle(CoreLifecycle::STOPPED);

	if (success) {
		g_bootState = BootState::Off;
	}
}

// Call this after handling BootState::Failed.
void PSP_CancelBoot() {
	_dbg_assert_(g_bootState == BootState::Failed);
	g_bootState = BootState::Off;
}

BootState PSP_Reboot(std::string *error_string) {
	if (g_bootState != BootState::Complete) {
		return g_bootState;
	}

	Core_Stop();
	Core_WaitInactive();
	PSP_Shutdown(true);
	std::string resetError;
	return PSP_Init(PSP_CoreParameter(), error_string);
}

void PSP_BeginHostFrame() {
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
	PSP_RunLoopFor(blockTicks);
	// TODO: Check for frame timeout?
}

void PSP_RunLoopFor(int cycles) {
	Core_RunLoopUntil(CoreTiming::GetTicks() + cycles);
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
		ERROR_LOG(Log::FileSystem, "Unknown directory type.");
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
	INFO_LOG(Log::IO, "Creating '%s' and subdirs:", pspDir.c_str());
	File::CreateFullPath(pspDir);
	if (!File::Exists(pspDir)) {
		INFO_LOG(Log::IO, "Not a workable memstick directory. Giving up");
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

const char *DumpFileTypeToString(DumpFileType type) {
	switch (type) {
	case DumpFileType::EBOOT: return "EBOOT";
	case DumpFileType::PRX: return "PRX";
	case DumpFileType::Atrac3: return "AT3";
	default: return "N/A";
	}
}

const char *DumpFileTypeToFileExtension(DumpFileType type) {
	switch (type) {
	case DumpFileType::EBOOT: return ".BIN";
	case DumpFileType::PRX: return ".prx";
	case DumpFileType::Atrac3: return ".at3";
	default: return "N/A";
	}
}

void DumpFileIfEnabled(const u8 *dataPtr, const u32 length, std::string_view name, DumpFileType type) {
	if (!(g_Config.iDumpFileTypes & (int)type)) {
		return;
	}
	if (!dataPtr) {
		ERROR_LOG(Log::Loader, "Error dumping %s: invalid pointer", DumpFileTypeToString(DumpFileType::EBOOT));
		return;
	}
	if (length == 0) {
		ERROR_LOG(Log::Loader, "Error dumping %s: invalid length", DumpFileTypeToString(DumpFileType::EBOOT));
		return;
	}

	const char *extension = DumpFileTypeToFileExtension(type);
	std::string filenameToDumpTo = g_paramSFO.GetDiscID() + "_" + std::string(name);
	if (!endsWithNoCase(filenameToDumpTo, extension)) {
		filenameToDumpTo += extension;
	}
	const Path dumpDirectory = GetSysDirectory(DIRECTORY_DUMP);
	const Path fullPath = dumpDirectory / filenameToDumpTo;

	auto s = GetI18NCategory(I18NCat::SYSTEM);

	std::string_view titleStr = "Dump Decrypted Eboot";
	if (type != DumpFileType::EBOOT) {
		titleStr = s->T(DumpFileTypeToString(type));
	}

	// If the file already exists, don't dump it again.
	if (File::Exists(fullPath)) {
		INFO_LOG(Log::sceModule, "%s already exists for this game, skipping dump.", filenameToDumpTo.c_str());

		char *path = new char[strlen(fullPath.c_str()) + 1];
		strcpy(path, fullPath.c_str());

		g_OSD.Show(OSDType::MESSAGE_INFO, titleStr, fullPath.ToVisualString(), 5.0f);
		if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
			g_OSD.SetClickCallback("file_dumped", [](bool clicked, void *userdata) {
				char *path = (char *)userdata;
				if (clicked) {
					System_ShowFileInFolder(Path(path));
				} else {
					delete[] path;
				}
			}, path);
		}
		return;
	}

	// Make sure the dump directory exists before continuing.
	if (!File::Exists(dumpDirectory)) {
		if (!File::CreateDir(dumpDirectory)) {
			ERROR_LOG(Log::sceModule, "Unable to create directory for EBOOT dumping, aborting.");
			return;
		}
	}

	FILE *file = File::OpenCFile(fullPath, "wb");
	if (!file) {
		ERROR_LOG(Log::sceModule, "Unable to write decrypted EBOOT.");
		return;
	}

	const size_t lengthToWrite = length;

	fwrite(dataPtr, sizeof(u8), lengthToWrite, file);
	fclose(file);

	INFO_LOG(Log::sceModule, "Successfully wrote %s to %s", DumpFileTypeToString(type), fullPath.c_str());

	char *path = new char[strlen(fullPath.c_str()) + 1];
	strcpy(path, fullPath.c_str());

	// Re-suing the translation string here.
	g_OSD.Show(OSDType::MESSAGE_SUCCESS, titleStr, fullPath.ToVisualString(), 5.0f);
	if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
		g_OSD.SetClickCallback("decr", [](bool clicked, void *userdata) {
			char *path = (char *)userdata;
			if (clicked) {
				System_ShowFileInFolder(Path(path));
			}
			delete[] path;
		}, path);
	}
}

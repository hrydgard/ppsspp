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

#include "ext/lua/lapi.h"

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/OSD.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/Log/LogManager.h"
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
#include "Core/HLE/sceUtility.h"
#include "Core/HW/Display.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Util/PathUtil.h"
#include "Core/CoreTiming.h"
#include "Core/CoreParameter.h"
#include "Core/FileLoaders/RamCachingFileLoader.h"
#include "Core/LuaContext.h"
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
ParamSFOData g_paramSFORaw;
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
static bool g_fileLoggingWasEnabled;

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
	case IdentifiedFileType::PSX_ISO:  *errorString = "PSX game image detected."; break;
	case IdentifiedFileType::PS2_ISO:  *errorString = "PS2 game image detected."; break;
	case IdentifiedFileType::PS3_ISO:  *errorString = "PS2 game image detected."; break;
	case IdentifiedFileType::NORMAL_DIRECTORY: *errorString = "Just a directory."; break;
	case IdentifiedFileType::PPSSPP_SAVESTATE: *errorString = "This is a saved state, not a game."; break; // Actually, we could make it load it...
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY: *errorString = "This is save data, not a game."; break;
	case IdentifiedFileType::PSP_PS1_PBP: *errorString = "PS1 EBOOTs are not supported by PPSSPP."; break;
	case IdentifiedFileType::PSP_UMD_VIDEO_ISO: *errorString = "UMD Video ISOs are not supported by PPSSPP."; break;
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
		g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("BufferedRenderingRequired", "Warning: This game requires Rendering Mode to be set to Buffered."), 10.0f, "bufreq");
	} else {
		g_OSD.CancelById("bufreq");
	}

	if (compat.flags().RequireBlockTransfer && g_Config.iSkipGPUReadbackMode != (int)SkipGPUReadbackMode::NO_SKIP && !PSP_CoreParameter().compat.flags().ForceEnableGPUReadback) {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("BlockTransferRequired", "Warning: This game requires Skip GPU Readbacks be set to No."), 10.0f, "blockxfer");
	} else {
		g_OSD.CancelById("blockxfer");
	}

	if (compat.flags().RequireDefaultCPUClock && g_Config.iLockedCPUSpeed != 0) {
		auto gr = GetI18NCategory(I18NCat::GRAPHICS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, gr->T("DefaultCPUClockRequired", "Warning: This game requires the CPU clock to be set to default."), 10.0f, "defaultclock");
	} else {
		g_OSD.CancelById("defaultclock");
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
		if (!MountGameISO(fileLoader, errorString)) {
			*errorString = "Failed to mount ISO file: " + *errorString;
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
				g_OSD.Show(OSDType::MESSAGE_WARNING, sc->T("ExtractedIsoWarning", "Extracted ISOs often don't work.\nPlay the ISO file directly."), GetFriendlyPath(g_CoreParameter.fileToStart), 7.0f);
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
	case IdentifiedFileType::PSP_UMD_VIDEO_ISO:
	{
		ERROR_LOG(Log::Loader, "PPSSPP doesn't support UMD Video.");
		auto er = GetI18NCategory(I18NCat::ERRORS);
		*errorString = er->T("PPSSPP doesn't support UMD Video.");
		return false;
	}
	default:
	{
		// Trying to boot other things lands us here. We need to return a sensible error string.
		ERROR_LOG(Log::Loader, "CPU_Init didn't recognize file. %s", errorString->c_str());
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		if (errorString->empty()) {
			*errorString = sy->T("Not a PSP game");
		}
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

	_dbg_assert_(!g_symbolMap);
	g_symbolMap = new SymbolMap();

	MIPSAnalyst::Reset();
	Replacement_Init();

	g_lua.Init();

	// Here we have read the PARAM.SFO, let's see if we need any compatibility overrides.
	// Homebrew get fake disc IDs assigned to the global paramSFO, so they shouldn't clash with real games.
	g_CoreParameter.compat.Load(g_paramSFO.GetDiscID());
	ShowCompatWarnings(g_CoreParameter.compat);

	// This must be before Memory::Init because plugins can override the memory size.
	if (type != IdentifiedFileType::PPSSPP_GE_DUMP) {
		HLEPlugins::Init();
	}

	Memory::MemMapSetupFlags memMapFlags = Memory::MemMapSetupFlags::Default;
	if (g_CoreParameter.compat.flags().NullPageValid) {
		memMapFlags = Memory::MemMapSetupFlags::AllocNullPage;
	}

	// Initialize the memory map as early as possible (now that we've read the PARAM.SFO).
	if (!Memory::Init(memMapFlags)) {
		// We're screwed.
		*errorString = "Memory init failed";
		return false;
	}

	// If it was forced on the command line. We don't want to override that.
	g_fileLoggingWasEnabled = g_logManager.GetOutputsEnabled() & LogOutput::File;
	g_logManager.EnableOutput(LogOutput::File, g_Config.bEnableFileLogging || g_fileLoggingWasEnabled);

	if ((g_logManager.GetOutputsEnabled() & LogOutput::File) && !g_logManager.GetLogFilePath().empty()) {
		auto dev = GetI18NCategory(I18NCat::DEVELOPER);

		std::string logPath = g_logManager.GetLogFilePath().ToString();

		// TODO: Really need a cleaner way to make clickable path notifications.
		char *path = new char[logPath.size() + 1];
		strcpy(path, logPath.data());

		g_OSD.Show(OSDType::MESSAGE_INFO, ApplySafeSubstitutions("%1: %2", dev->T("Log to file"), g_logManager.GetLogFilePath().ToVisualString()), 0.0f, "log_to_file");
		if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
			g_OSD.SetClickCallback("log_to_file", [](bool clicked, void *userdata) {
				char *path = (char *)userdata;
				if (clicked) {
					System_ShowFileInFolder(Path(path));
				} else {
					delete[] path;
				}
			}, path);
		}
	}

	InitVFPU();

	LoadSymbolsIfSupported();

	mipsr4k.Reset();

	CoreTiming::Init();

	DisplayHWInit();

	// Init all the HLE modules
	HLEInit();

	// TODO: Put this somewhere better?
	if (!g_CoreParameter.mountIso.empty()) {
		g_CoreParameter.mountIsoLoader = ConstructFileLoader(g_CoreParameter.mountIso);
	}

	// Game-specific settings are load from for example Load_PSP_ISO (which calls g_Config.LoadGameConfig).
	// We can't do things that depend on these before the below switch. So for example, the adjustment of the GPU core
	// to software has now been moved below it.

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
		INFO_LOG(Log::Loader, "File is an ELF or loose PBP %s", fileLoader->GetPath().c_str());
		if (!Load_PSP_ELF_PBP(fileLoader, errorString)) {
			ERROR_LOG(Log::Loader, "Failed to load ELF or loose PBP: %s", errorString->c_str());
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

	// Update things that depend on game-specific config here.

	// Compat settings can override the software renderer, take care of that here.
	if (g_Config.bSoftwareRendering || g_CoreParameter.compat.flags().ForceSoftwareRenderer) {
		g_CoreParameter.gpuCore = GPUCORE_SOFTWARE;
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

	DisplayHWShutdown();

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

	g_lua.Shutdown();

	g_logManager.EnableOutput(LogOutput::File, g_fileLoggingWasEnabled);
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

static void InitGPU(std::string *error_string) {
	if (!gpu) {  // should be!
		INFO_LOG(Log::Loader, "Starting graphics...");
		Draw::DrawContext *draw = g_CoreParameter.graphicsContext ? g_CoreParameter.graphicsContext->GetDrawContext() : nullptr;
		// This set the `gpu` global.
		GPUCore gpuCore = PSP_CoreParameter().gpuCore;
		bool success = GPU_Init(gpuCore, g_CoreParameter.graphicsContext, draw);
		if (!success) {
			*error_string = "Unable to initialize rendering engine.";
			CPU_Shutdown(false);
			g_bootState = BootState::Failed;
		}
	}
}

bool PSP_InitStart(const CoreParameter &coreParam) {
	if (g_bootState != BootState::Off) {
		ERROR_LOG(Log::Loader, "Can't start loader thread - already on.");
		return false;
	}

	IncrementDebugCounter(DebugCounter::GAME_BOOT);

	g_bootState = BootState::Booting;

	GraphicsContext *temp = g_CoreParameter.graphicsContext;
	g_CoreParameter = coreParam;
	if (g_CoreParameter.graphicsContext == nullptr) {
		g_CoreParameter.graphicsContext = temp;
	}
	g_CoreParameter.errorString.clear();

	std::string *errorString = &g_CoreParameter.errorString;

	INFO_LOG(Log::Loader, "Starting loader thread...");

	_assert_msg_(!g_loadingThread.joinable(), "%s", coreParam.fileToStart.c_str());

	Core_NotifyLifecycle(CoreLifecycle::STARTING);

	g_loadingThread = std::thread([errorString]() {
		SetCurrentThreadName("ExecLoader");

		AndroidJNIThreadContext jniContext;

		NOTICE_LOG(Log::Boot, "PPSSPP %s", PPSSPP_GIT_VERSION);

		Path filename = g_CoreParameter.fileToStart;

		IdentifiedFileType fileType;
		FileLoader *loadedFile = ResolveFileLoaderTarget(ConstructFileLoader(filename), &fileType, errorString);

		if (System_GetPropertyBool(SYSPROP_ENOUGH_RAM_FOR_FULL_ISO)) {
			if (g_Config.bCacheFullIsoInRam) {
				switch (fileType) {
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

		g_CoreParameter.fileType = fileType;

		// TODO: The reason we pass in g_CoreParameter.errorString here is that it's persistent -
		// it gets written to from the loader thread that gets spawned.
		if (!CPU_Init(loadedFile, fileType, &g_CoreParameter.errorString)) {
			CPU_Shutdown(false);
			g_CoreParameter.fileToStart.clear();
			*errorString = g_CoreParameter.errorString;
			if (errorString->empty()) {
				*errorString = "Failed initializing CPU/Memory";
			}
			g_bootState = BootState::Failed;
			return;
		}

		// Initialize the GPU as far as we can here (do things like load cache files).
		_dbg_assert_(!gpu);
#ifndef __LIBRETRO__
		InitGPU(errorString);
#endif
		g_bootState = BootState::Complete;
	});

	return true;
}

BootState PSP_InitUpdate(std::string *error_string) {
	const BootState bootState = g_bootState;

	if (bootState == BootState::Booting || bootState == BootState::Off) {
		// Nothing to do right now.
		_dbg_assert_(bootState == BootState::Booting || !g_loadingThread.joinable());
		return bootState;
	}

	_dbg_assert_(bootState == BootState::Complete || bootState == BootState::Failed);

	// Since we load on a background thread, wait for startup to complete.
	_assert_msg_(g_loadingThread.joinable(), "bootstate: %d", (int)bootState);
	g_loadingThread.join();

	if (bootState == BootState::Failed) {
		// Failed! (Note: PSP_Shutdown was already called on the loader thread).
		Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
		*error_string = g_CoreParameter.errorString;
		g_bootState = BootState::Off;
		return BootState::Failed;
	}

#ifdef __LIBRETRO__
	InitGPU(error_string);
#endif

	// Ok, async part of the boot completed, let's finish up things on the main thread.
	if (gpu) {
		gpu->FinishInitOnMainThread();
	} else {
		_dbg_assert_(gpu);
	}

	Core_NotifyLifecycle(CoreLifecycle::START_COMPLETE);
	// The thread should have set it at this point.
	_dbg_assert_(bootState == BootState::Complete);
	return BootState::Complete;
}

// Most platforms should not use this one, they should call PSP_InitStart and then do their thing
// while repeatedly calling PSP_InitUpdate. This is basically just for libretro convenience.
BootState PSP_Init(const CoreParameter &coreParam, std::string *error_string) {
	// InitStart doesn't really fail anymore.
	if (!PSP_InitStart(coreParam)) {
		g_bootState = BootState::Off;
		return BootState::Failed;
	}

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
		ERROR_LOG(Log::Loader, "Unexpected PSP_Shutdown");
		return;
	}

	_assert_(g_bootState != BootState::Failed);

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
	g_paramSFORaw.Clear();
	System_SetWindowTitle("");

	currentMIPS = nullptr;

	if (g_Config.IsGameSpecific()) {
		g_Config.UnloadGameConfig();
	}

	Core_NotifyLifecycle(CoreLifecycle::STOPPED);

	if (success) {
		g_bootState = BootState::Off;
	}

	IncrementDebugCounter(DebugCounter::GAME_SHUTDOWN);
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
		} else {
			delete[] path;
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

	INFO_LOG(Log::sceModule, "Successfully wrote %s to %s", DumpFileTypeToString(type), fullPath.ToVisualString().c_str());

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

// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// NativeApp implementation for platforms that will use that framework, like:
// Android, Linux, MacOSX.
//
// Native is a cross platform framework. It's not very mature and mostly
// just built according to the needs of my own apps.
//
// Windows has its own code that bypasses the framework entirely.

#include "ppsspp_config.h"

// Background worker threads should be spawned in NativeInit and joined
// in NativeShutdown.
#include <errno.h>

#include <clocale>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include "Windows/WindowsAudio.h"
#include "Windows/MainWindow.h"
#endif

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
#include "Windows/CaptureDevice.h"
#endif

#include "Common/Net/HTTPClient.h"
#include "Common/Net/Resolve.h"
#include "Common/Net/URL.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/Text/draw_text.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d.h"
#include "Common/UI/UI.h"
#include "Common/UI/Screen.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/IconCache.h"

#include "android/jni/app-android.h"

#include "Common/System/Display.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Input/InputState.h"
#include "Common/Math/math_util.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/ZipFileReader.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/CPUDetect.h"
#include "Common/File/FileUtil.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log/LogManager.h"
#include "Common/MemArena.h"
#include "Common/GraphicsContext.h"
#include "Common/OSVersion.h"
#include "Common/GPU/ShaderTranslation.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Audio/AudioBackend.h"
#include "Common/UI/PopupScreens.h"
#include "Core/ControlMapper.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/FileLoaders/DiskCachingFileLoader.h"
#include "Core/FrameTiming.h"
#include "Core/KeyMap.h"
#include "Core/Reporting.h"
#include "Core/RetroAchievements.h"
#include "Core/SaveState.h"
#include "Core/Screenshot.h"
#include "Core/System.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"
#include "Core/HLE/proAdhoc.h"
#include "Core/HW/MemoryStick.h"
#include "Core/Util/GameManager.h"
#include "Core/Util/PortManager.h"
#include "Core/Util/AudioFormat.h"
#include "Core/Util/RecentFiles.h"
#include "Core/WebServer.h"
#include "Core/TiltEventProcessor.h"

#include "GPU/GPUCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "UI/AudioCommon.h"
#include "UI/Background.h"
#include "UI/BackgroundAudio.h"
#include "UI/ControlMappingScreen.h"
#include "UI/DevScreens.h"
#include "UI/DiscordIntegration.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GameSettingsScreen.h"
#include "UI/DeveloperToolsScreen.h"
#include "UI/GPUDriverTestScreen.h"
#include "UI/MiscScreens.h"
#include "UI/MemStickScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/RemoteISOScreen.h"
#include "UI/Theme.h"
#include "UI/UIAtlas.h"

#if PPSSPP_PLATFORM(UWP)
#include <dwrite_3.h>
#include "UWP/UWPHelpers/InputHelpers.h"
#endif
#if PPSSPP_PLATFORM(ANDROID)
#include "android/jni/app-android.h"
#endif

#if PPSSPP_ARCH(ARM) && defined(__ANDROID__)
#include "../../android/jni/ArmEmitterTest.h"
#elif PPSSPP_ARCH(ARM64) && defined(__ANDROID__)
#include "../../android/jni/Arm64EmitterTest.h"
#endif

#if PPSSPP_PLATFORM(IOS)
#include "ios/iOSCoreAudio.h"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
#include "UI/DarwinFileSystemServices.h"
#endif

#if !defined(__LIBRETRO__)
#include "Core/Util/GameDB.h"
#endif

#include <Core/HLE/Plugins.h>

bool HandleGlobalMessage(UIMessage message, const std::string &value);
static void ProcessWheelRelease(InputKeyCode keyCode, double now, bool keyPress);
void SaveFrameDump();

ScreenManager *g_screenManager;
std::string config_filename;

// Really need to clean this mess of globals up... but instead I add more :P
bool g_TakeScreenshot;
static bool resized = false;
static bool restarting = false;

static int renderCounter = 0;

struct PendingMessage {
	UIMessage message;
	std::string value;
};

static std::mutex g_pendingMutex;
static std::vector<PendingMessage> pendingMessages;
static Draw::DrawContext *g_draw;
static Draw::Pipeline *colorPipeline;
static Draw::Pipeline *texColorPipeline;
static UIContext *uiContext;
static int g_restartGraphics;
static bool g_windowHidden = false;
std::vector<std::function<void()>> g_pendingClosures;

AudioBackend *g_audioBackend = nullptr;

std::thread *graphicsLoadThread;

// globals
Path boot_filename;

// This is called before NativeInit so we do a little bit of initialization here.
void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape, std::string *version) {
	*app_nice_name = "PPSSPP";
	*app_dir_name = "ppsspp";
	*landscape = true;
	*version = PPSSPP_GIT_VERSION;

#if PPSSPP_ARCH(ARM) && defined(__ANDROID__)
	ArmEmitterTest();
#elif PPSSPP_ARCH(ARM64) && defined(__ANDROID__)
	Arm64EmitterTest();
#endif
}

#if defined(USING_WIN_UI) && !PPSSPP_PLATFORM(UWP)
static bool CheckFontIsUsable(const wchar_t *fontFace) {
	wchar_t actualFontFace[1024] = { 0 };

	HFONT f = CreateFont(0, 0, 0, 0, FW_LIGHT, 0, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY, VARIABLE_PITCH, fontFace);
	if (f != nullptr) {
		HDC hdc = CreateCompatibleDC(nullptr);
		if (hdc != nullptr) {
			SelectObject(hdc, f);
			GetTextFace(hdc, 1024, actualFontFace);
			DeleteDC(hdc);
		}
		DeleteObject(f);
	}

	// If we were able to get the font name, did it load?
	if (actualFontFace[0] != 0) {
		return wcsncmp(actualFontFace, fontFace, ARRAY_SIZE(actualFontFace)) == 0;
	}
	return false;
}
#endif

void PostLoadConfig() {
	if (g_Config.currentDirectory.empty()) {
		g_Config.currentDirectory = g_Config.defaultCurrentDirectory;
	}

	// Allow the lang directory to be overridden for testing purposes (e.g. Android, where it's hard to
	// test new languages without recompiling the entire app, which is a hassle).
	const Path langOverridePath = GetSysDirectory(DIRECTORY_SYSTEM) / "lang";

	// If we run into the unlikely case that "lang" is actually a file, just use the built-in translations.
	if (!File::Exists(langOverridePath) || !File::IsDirectory(langOverridePath))
		g_i18nrepo.LoadIni(g_Config.sLanguageIni);
	else
		g_i18nrepo.LoadIni(g_Config.sLanguageIni, langOverridePath);

#if !PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(UWP)
	CreateSysDirectories();
#endif
}

static Path GetFailedBackendsDir() {
	Path failedBackendsDir;
	if (System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS)) {
		failedBackendsDir = GetSysDirectory(DIRECTORY_APP_CACHE);
	} else {
		failedBackendsDir = GetSysDirectory(DIRECTORY_SYSTEM);
	}
	return failedBackendsDir;
}

static void CheckFailedGPUBackends() {
#ifdef _DEBUG
	// If you're in debug mode, you probably don't want a fallback. If you're in release mode, use IGNORE below.
	NOTICE_LOG(Log::Loader, "Not checking for failed graphics backends in debug mode");
	return;
#endif

#if PPSSPP_PLATFORM(ANDROID)
	if (System_GetPropertyInt(SYSPROP_SYSTEMVERSION) >= 30) {
		// In Android 11 or later, Vulkan is as stable as OpenGL, so let's not even bother.
		// Have also seen unexplained issues with random fallbacks to OpenGL for no good reason,
		// especially when debugging.
		return;
	}
#endif

	// We only want to do this once per process run and backend, to detect process crashes.
	// If NativeShutdown is called before we finish, we might call this multiple times.
	static int lastBackend = -1;
	if (lastBackend == g_Config.iGPUBackend) {
		return;
	}
	lastBackend = g_Config.iGPUBackend;

	const Path failedBackendsDir = GetFailedBackendsDir();
	const Path failedBackendsFile = failedBackendsDir / "FailedGraphicsBackends.txt";

	std::string data;
	if (File::ReadTextFileToString(failedBackendsFile, &data)) {
		g_Config.sFailedGPUBackends = data;
	}

	// Use this if you want to debug a graphics crash...
	if (g_Config.sFailedGPUBackends == "IGNORE")
		return;
	else if (!g_Config.sFailedGPUBackends.empty()) {
		ERROR_LOG(Log::Loader, "Failed graphics backends: %s", g_Config.sFailedGPUBackends.c_str());
	}

	// Okay, let's not try a backend in the failed list.
	g_Config.iGPUBackend = g_Config.NextValidBackend();
	if (lastBackend != g_Config.iGPUBackend) {
		// This is the expected path.
		std::string param = GPUBackendToString((GPUBackend)lastBackend) + " -> " + GPUBackendToString((GPUBackend)g_Config.iGPUBackend);
		System_GraphicsBackendFailedAlert(param);
		INFO_LOG(Log::Loader, "Failed graphics backend switched from %s (%d to %d)", param.c_str(), lastBackend, g_Config.iGPUBackend);
	} else {
		WARN_LOG(Log::Loader, "Did not switch failed backend! %d", g_Config.iGPUBackend);
	}

	// And then let's - for now - add the current to the failed list, in case it fails - we'll clear it again once it succeeds.
	const std::string curBackend = GPUBackendToString((GPUBackend)g_Config.iGPUBackend);
	if (g_Config.sFailedGPUBackends.empty()) {
		g_Config.sFailedGPUBackends = curBackend;
	} else if (g_Config.sFailedGPUBackends.find(curBackend) != std::string::npos) {
		// Backend already listed!
		ERROR_LOG(Log::Loader, "Unexpected: Backend already in failed backends. Should not have been attempted");
	} else if (g_Config.sFailedGPUBackends.find("ALL") == std::string::npos) {
		g_Config.sFailedGPUBackends += "," + GPUBackendToString((GPUBackend)g_Config.iGPUBackend);
	}

	// Let's try to create, in case it doesn't exist.
	File::CreateFullPath(failedBackendsDir);
	File::WriteStringToFile(true, g_Config.sFailedGPUBackends, failedBackendsFile);
}

static void ClearFailedGPUBackends() {
	if (g_Config.sFailedGPUBackends == "IGNORE")
		return;

	const Path failedBackendsDir = GetFailedBackendsDir();
	const Path failedBackendsFile = failedBackendsDir / "FailedGraphicsBackends.txt";
	// We've successfully started graphics without crashing, hurray.
	// In case they update drivers and have totally different problems much later, clear the failed list.
	g_Config.sFailedGPUBackends.clear();
	File::Delete(failedBackendsFile);
}

void NativeInit(int argc, const char *argv[], const char *savegame_dir, const char *external_dir, const char *cache_dir) {
	net::Init();  // This needs to happen before we load the config. So on Windows we also run it in Main. It's fine to call multiple times.

	IncrementDebugCounter(DebugCounter::APP_BOOT);

	// Probably an excessive timeout. it only causes delays on shutdown, though.
	__UPnPInit(2000);

	ShaderTranslationInit();

	g_threadManager.Init(cpu_info.num_cores, cpu_info.logical_cpu_count);

	g_recentFiles.EnsureThread();

	// Make sure UI state is MENU.
	ResetUIState();

	bool skipLogo = false;
	setlocale( LC_ALL, "C" );
	std::string user_data_path = savegame_dir;
	pendingMessages.clear();
	g_pendingClosures.clear();
	g_requestManager.Clear();

	// external_dir has all kinds of meanings depending on platform.
	// on iOS it's even the path to bundled app assets. It's a mess.

	// We want this to be FIRST.
#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
	// Packed assets are included in app
	g_VFS.Register("", new DirectoryReader(Path(external_dir)));
#endif
#if defined(ASSETS_DIR)
	g_VFS.Register("", new DirectoryReader(Path(ASSETS_DIR)));
#endif
#if !defined(MOBILE_DEVICE) && !defined(_WIN32) && !PPSSPP_PLATFORM(SWITCH)
	g_VFS.Register("", new DirectoryReader(File::GetExeDirectory() / "assets"));
	g_VFS.Register("", new DirectoryReader(File::GetExeDirectory()));
	g_VFS.Register("", new DirectoryReader(Path("/usr/local/share/ppsspp/assets")));
	g_VFS.Register("", new DirectoryReader(Path("/usr/local/share/games/ppsspp/assets")));
	g_VFS.Register("", new DirectoryReader(Path("/usr/share/ppsspp/assets")));
	g_VFS.Register("", new DirectoryReader(Path("/usr/share/games/ppsspp/assets")));
#endif

#if PPSSPP_PLATFORM(SWITCH)
	Path assetPath = Path(user_data_path) / "assets";
	g_VFS.Register("", new DirectoryReader(assetPath));
#else
	g_VFS.Register("", new DirectoryReader(Path("assets")));
#endif
	g_VFS.Register("", new DirectoryReader(Path(savegame_dir)));

#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(MAC)
	g_Config.defaultCurrentDirectory = Path(System_GetProperty(SYSPROP_USER_DOCUMENTS_DIR));
#else
	g_Config.defaultCurrentDirectory = Path("/");
#endif

#if !PPSSPP_PLATFORM(UWP)
	g_Config.internalDataDirectory = Path(savegame_dir);
#endif

#if PPSSPP_PLATFORM(ANDROID)
	// In Android 12 with scoped storage, due to the above, the external directory
	// is no longer the plain root of external storage, but it's an app specific directory
	// on external storage (g_extFilesDir).
	if (System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
		// There's no sensible default directory. Let the user browse for files.
		g_Config.defaultCurrentDirectory.clear();
	} else {
		g_Config.memStickDirectory = Path(external_dir);
		g_Config.defaultCurrentDirectory = Path(external_dir);
	}

	// Might also add an option to move it to internal / non-visible storage, but there's
	// little point, really.

	g_Config.flash0Directory = Path(external_dir) / "flash0";

	Path memstickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
	if (File::Exists(memstickDirFile)) {
		INFO_LOG(Log::System, "Reading '%s' to find memstick dir.", memstickDirFile.c_str());
		std::string memstickDir;
		if (File::ReadTextFileToString(memstickDirFile, &memstickDir)) {
			Path memstickPath(memstickDir);
			if (!memstickPath.empty() && File::Exists(memstickPath)) {
				g_Config.memStickDirectory = memstickPath;
				INFO_LOG(Log::System, "Memstick Directory from memstick_dir.txt: '%s'", g_Config.memStickDirectory.c_str());
			} else {
				ERROR_LOG(Log::System, "Couldn't read directory '%s' specified by memstick_dir.txt.", memstickDir.c_str());
				if (System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
					// Ask the user to configure a memstick directory.
					INFO_LOG(Log::System, "Asking the user.");
					g_Config.memStickDirectory.clear();
				}
			}
		}
	} else {
		INFO_LOG(Log::System, "No memstick directory file found (tried to open '%s')", memstickDirFile.c_str());
	}

	// Attempt to create directories after reading the path.
	if (!System_GetPropertyBool(SYSPROP_ANDROID_SCOPED_STORAGE)) {
		CreateSysDirectories();
	}
#elif PPSSPP_PLATFORM(UWP) && !defined(__LIBRETRO__)
	Path memstickDirFile = g_Config.internalDataDirectory / "memstick_dir.txt";
	if (File::Exists(memstickDirFile)) {
		INFO_LOG(Log::System, "Reading '%s' to find memstick dir.", memstickDirFile.c_str());
		std::string memstickDir;
		if (File::ReadTextFileToString(memstickDirFile, &memstickDir)) {
			Path memstickPath(memstickDir);
			if (!memstickPath.empty() && File::Exists(memstickPath)) {
				g_Config.memStickDirectory = memstickPath;
				g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
				g_Config.Reload();
				INFO_LOG(Log::System, "Memstick Directory from memstick_dir.txt: '%s'", g_Config.memStickDirectory.c_str());
			} else {
				ERROR_LOG(Log::System, "Couldn't read directory '%s' specified by memstick_dir.txt.", memstickDir.c_str());
				g_Config.memStickDirectory.clear();
			}
		}
	}
	else {
		INFO_LOG(Log::System, "No memstick directory file found (tried to open '%s')", memstickDirFile.c_str());
	}
#elif PPSSPP_PLATFORM(IOS)
	g_Config.defaultCurrentDirectory = g_Config.internalDataDirectory;
	g_Config.memStickDirectory = DarwinFileSystemServices::appropriateMemoryStickDirectoryToUse();
	g_Config.flash0Directory = Path(external_dir) / "flash0";
#elif PPSSPP_PLATFORM(MAC)
	g_Config.memStickDirectory = DarwinFileSystemServices::appropriateMemoryStickDirectoryToUse();
	g_Config.flash0Directory = Path(external_dir) / "flash0";
#elif PPSSPP_PLATFORM(SWITCH)
	g_Config.memStickDirectory = g_Config.internalDataDirectory / "config/ppsspp";
	g_Config.flash0Directory = g_Config.internalDataDirectory / "assets/flash0";
#elif !PPSSPP_PLATFORM(WINDOWS)
	std::string config;
	if (getenv("XDG_CONFIG_HOME") != NULL)
		config = getenv("XDG_CONFIG_HOME");
	else if (getenv("HOME") != NULL)
		config = getenv("HOME") + std::string("/.config");
	else // Just in case
		config = "./config";

	g_Config.memStickDirectory = Path(config) / "ppsspp";
	g_Config.flash0Directory = File::GetExeDirectory() / "assets/flash0";
	if (getenv("HOME") != nullptr) {
		g_Config.defaultCurrentDirectory = Path(getenv("HOME"));
	} else {
		// Hm, should probably actually explicitly set the current directory..
		// Though it's not many platforms that'll land us here.
		g_Config.currentDirectory = Path(".");
	}
#endif

	if (g_Config.currentDirectory.empty()) {
		g_Config.currentDirectory = g_Config.defaultCurrentDirectory;
	}

	if (cache_dir && strlen(cache_dir)) {
		g_Config.appCacheDirectory = Path(cache_dir);
		DiskCachingFileLoaderCache::SetCacheDir(g_Config.appCacheDirectory);
	}

	g_logManager.Init(&g_Config.bEnableLogging);

#if !PPSSPP_PLATFORM(WINDOWS)
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));

	// Note that if we don't have storage permission here, loading the config will
	// fail and it will be set to the default. Later, we load again when we get permission.
	g_Config.Load();
#endif

	const char *fileToLog = nullptr;
	Path stateToLoad;

	bool gotBootFilename = false;
	bool gotoGameSettings = false;
	bool gotoTouchScreenTest = false;
	bool gotoDeveloperTools = false;
	boot_filename.clear();

	// Parse command line
	LogLevel logLevel = LogLevel::LINFO;
	bool forceLogLevel = false;
	const auto setLogLevel = [&logLevel, &forceLogLevel](LogLevel level) {
		logLevel = level;
		forceLogLevel = true;
	};

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
#if defined(__APPLE__)
			// On Apple system debugged executable may get -NSDocumentRevisionsDebugMode YES in argv.
			if (!strcmp(argv[i], "-NSDocumentRevisionsDebugMode") && argc - 1 > i) {
				i++;
				continue;
			}
#endif
			switch (argv[i][1]) {
			case 'd':
				// Enable debug logging
				// Note that you must also change the max log level in Log.h.
				setLogLevel(LogLevel::LDEBUG);
				break;
			case 'v':
				// Enable verbose logging
				// Note that you must also change the max log level in Log.h.
				setLogLevel(LogLevel::LVERBOSE);
				break;
			case 'j':
				g_Config.iCpuCore = (int)CPUCore::JIT;
				g_Config.bSaveSettings = false;
				break;
			case 'i':
				g_Config.iCpuCore = (int)CPUCore::INTERPRETER;
				g_Config.bSaveSettings = false;
				break;
			case 'r':
				g_Config.iCpuCore = (int)CPUCore::IR_INTERPRETER;
				g_Config.bSaveSettings = false;
				break;
			case 'J':
				g_Config.iCpuCore = (int)CPUCore::JIT_IR;
				g_Config.bSaveSettings = false;
				break;
			case '-':
				if (!strncmp(argv[i], "--loglevel=", strlen("--loglevel=")) && strlen(argv[i]) > strlen("--loglevel="))
					setLogLevel(static_cast<LogLevel>(std::atoi(argv[i] + strlen("--loglevel="))));
				if (!strncmp(argv[i], "--log=", strlen("--log=")) && strlen(argv[i]) > strlen("--log="))
					fileToLog = argv[i] + strlen("--log=");
				if (!strncmp(argv[i], "--state=", strlen("--state=")) && strlen(argv[i]) > strlen("--state="))
					stateToLoad = Path(argv[i] + strlen("--state="));
				if (!strncmp(argv[i], "--escape-exit", strlen("--escape-exit")))
					g_Config.bPauseExitsEmulator = true;
				if (!strncmp(argv[i], "--pause-menu-exit", strlen("--pause-menu-exit")))
					g_Config.bPauseMenuExitsEmulator = true;
				if (!strcmp(argv[i], "--fullscreen")) {
					g_Config.iForceFullScreen = 1;
					System_ToggleFullscreenState("1");
				}
				if (!strcmp(argv[i], "--windowed")) {
					g_Config.iForceFullScreen = 0;
					System_ToggleFullscreenState("0");
				}
				if (!strcmp(argv[i], "--touchscreentest"))
					gotoTouchScreenTest = true;
				if (!strcmp(argv[i], "--gamesettings"))
					gotoGameSettings = true;
				if (!strcmp(argv[i], "--developertools"))
					gotoDeveloperTools = true;
				if (!strncmp(argv[i], "--appendconfig=", strlen("--appendconfig=")) && strlen(argv[i]) > strlen("--appendconfig=")) {
					g_Config.SetAppendedConfigIni(Path(argv[i] + strlen("--appendconfig=")));
					g_Config.LoadAppendedConfig();
				}
				break;
			}
		} else {
			// This parameter should be a boot filename. Only accept it if we
			// don't already have one.
			if (!gotBootFilename) {
				gotBootFilename = true;
				INFO_LOG(Log::System, "Boot filename found in args: '%s'", argv[i]);

				bool okToLoad = true;
				bool okToCheck = true;
				if (System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS)) {
					PermissionStatus status = System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE);
					if (status == PERMISSION_STATUS_DENIED) {
						ERROR_LOG(Log::IO, "Storage permission denied. Launching without argument.");
						okToLoad = false;
						okToCheck = false;
					} else if (status != PERMISSION_STATUS_GRANTED) {
						ERROR_LOG(Log::IO, "Storage permission not granted. Launching without argument check.");
						okToCheck = false;
					} else {
						INFO_LOG(Log::IO, "Storage permission granted.");
					}
				}
				if (okToLoad) {
					std::string str = std::string(argv[i]);
					// Handle file:/// URIs, since you get those when creating shortcuts on some Android systems.
					if (startsWith(str, "file:///")) {
						str = UriDecode(str.substr(7));
						INFO_LOG(Log::IO, "Decoding '%s' to '%s'", argv[i], str.c_str());
					}

					boot_filename = Path(str);
					skipLogo = true;
				}
				if (okToLoad && okToCheck) {
					std::unique_ptr<FileLoader> fileLoader(ConstructFileLoader(boot_filename));
					if (!fileLoader->Exists()) {
						fprintf(stderr, "File not found: %s\n", boot_filename.c_str());
#if defined(_WIN32) || defined(__ANDROID__)
						// Ignore and proceed.
						boot_filename.clear();
#else
						// Bail.
						exit(1);
#endif
					}
				}
			} else {
				fprintf(stderr, "Syntax error: Can only boot one file.\nNote: Many command line args need a =, like --appendconfig=FILENAME.ini.\n");
#if defined(_WIN32) || defined(__ANDROID__)
				// Ignore and proceed.
#else
				// Bail.
				exit(1);
#endif
			}
		}
	}

	if (fileToLog) {
		g_logManager.EnableOutput(LogOutput::File);
		g_logManager.SetFileLogPath(Path(fileToLog));
	} else {
		// Set a default file logging path, in case the user enables it with the checkbox later.
		g_logManager.SetFileLogPath(GetSysDirectory(DIRECTORY_DUMP) / "log.txt");
	}

	if (forceLogLevel) {
		NOTICE_LOG(Log::System, "Setting log level to %d due to command line override", (int)logLevel);
		g_logManager.SetAllLogLevels(logLevel);
	}

	PostLoadConfig();

#if PPSSPP_PLATFORM(ANDROID)
	// Stdio is used for Android logging too.
	g_logManager.EnableOutput(LogOutput::Stdio);
#elif (defined(MOBILE_DEVICE) && !defined(_DEBUG))
	// Enable basic logging for any kind of mobile device, since LogManager doesn't.
	// The MOBILE_DEVICE/_DEBUG condition matches LogManager.cpp.
	// TODO: Why not use stdio?
	g_logManager.EnableOutput(LogOutput::Printf);
#endif

	if (System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS)) {
		if (System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE) != PERMISSION_STATUS_GRANTED) {
			System_AskForPermission(SYSTEM_PERMISSION_STORAGE);
		}
	}

	g_BackgroundAudio.SFX().Init();

	if (!boot_filename.empty() && stateToLoad.Valid()) {
		SaveState::Load(stateToLoad, -1, [](SaveState::Status status, std::string_view message) {
			if (!message.empty() && (!g_Config.bDumpFrames || !g_Config.bDumpVideoOutput)) {
				g_OSD.Show(status == SaveState::Status::SUCCESS ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_ERROR,
					message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
			}
		});
	}

	if (g_Config.bAchievementsEnable) {
		FILE *iconCacheFile = File::OpenCFile(GetSysDirectory(DIRECTORY_CACHE) / "icon.cache", "rb");
		if (iconCacheFile) {
			g_iconCache.LoadFromFile(iconCacheFile);
			fclose(iconCacheFile);
		}
	}

	g_DownloadManager.SetCacheDir(GetSysDirectory(DIRECTORY_APP_CACHE));

	DEBUG_LOG(Log::System, "ScreenManager!");
	g_screenManager = new ScreenManager();
	if (g_Config.memStickDirectory.empty()) {
		INFO_LOG(Log::System, "No memstick directory! Asking for one to be configured.");
		g_screenManager->switchScreen(new LogoScreen(AfterLogoScreen::MEMSTICK_SCREEN_INITIAL_SETUP));
	} else if (gotoGameSettings) {
		g_screenManager->switchScreen(new LogoScreen(AfterLogoScreen::TO_GAME_SETTINGS));
	} else if (gotoTouchScreenTest) {
		g_screenManager->switchScreen(new MainScreen());
		g_screenManager->push(new TouchTestScreen(Path()));
	} else if (gotoDeveloperTools) {
		g_screenManager->switchScreen(new MainScreen());
		g_screenManager->push(new DeveloperToolsScreen(Path()));
	} else if (skipLogo && !boot_filename.empty()) {
		INFO_LOG(Log::System, "Launching EmuScreen with boot filename '%s'", boot_filename.c_str());
		g_screenManager->switchScreen(new EmuScreen(boot_filename));
	} else {
		g_screenManager->switchScreen(new LogoScreen(AfterLogoScreen::DEFAULT));
	}

	g_screenManager->SetBackgroundOverlayScreens(new BackgroundScreen(), new OSDOverlayScreen());

	// Easy testing
	// screenManager->push(new GPUDriverTestScreen());

	WebServerFlags flags = (WebServerFlags)0;
	if (g_Config.bRemoteShareOnStartup) {
		flags |= WebServerFlags::DISCS;
	}
	if (g_Config.bRemoteDebuggerOnStartup) {
		flags |= WebServerFlags::DEBUGGER;
	}
	if (flags != WebServerFlags::NONE) {
		StartWebServer(WebServerFlags::ALL);
	}

	std::string sysName = System_GetProperty(SYSPROP_NAME);

	// We do this here, instead of in NativeInitGraphics, because the display may be reset.
	// When it's reset we don't want to forget all our managed things.
	CheckFailedGPUBackends();
	SetGPUBackend((GPUBackend)g_Config.iGPUBackend);
	renderCounter = 0;

	// Initialize retro achievements runtime.
	Achievements::Initialize();

	// Must be done restarting by now.
	restarting = false;
}

void CallbackPostRender(UIContext *dc, void *userdata);
bool CreateGlobalPipelines();

// TODO: Add faster special case for channels == 2.
static void NativeMixWrapper(float *dest, int framesToWrite, int sampleRateHz, void *userdata) {
	static int16_t *buffer;
	static int bufSize;
	if (bufSize < framesToWrite * 2) {
		buffer = new int16_t[framesToWrite * 2];
		bufSize = framesToWrite * 2;
	}

	NativeMix(buffer, framesToWrite, sampleRateHz, userdata);

	for (int i = 0; i < framesToWrite * 2; i++) {
		dest[i] = (float)buffer[i] * (float)(1.0f / 32767.0f);
	}
}

bool NativeInitGraphics(GraphicsContext *graphicsContext) {
	INFO_LOG(Log::System, "NativeInitGraphics");

	_assert_msg_(g_screenManager, "No screenmanager, bad init order. Backend = %d", g_Config.iGPUBackend);

	// We set this now so any resize during init is processed later.
	resized = false;

	Core_SetGraphicsContext(graphicsContext);
	g_draw = graphicsContext->GetDrawContext();

	_assert_(g_draw);

	if (!CreateGlobalPipelines()) {
		ERROR_LOG(Log::G3D, "Failed to create global pipelines");
		return false;
	}

	ui_draw2d.SetAtlas(GetUIAtlas());
	ui_draw2d.SetFontAtlas(GetFontAtlas());

	uiContext = new UIContext();
	uiContext->SetTheme(GetTheme());
	uiContext->SetAtlasProvider(&AtlasProvider);
	UpdateTheme();

	ui_draw2d.Init(g_draw, texColorPipeline);

	uiContext->Init(g_draw, texColorPipeline, colorPipeline, &ui_draw2d);
	if (uiContext->Text()) {
		// This seems unnecessary.
		// uiContext->Text()->SetOrCreateFont(FontStyle(FontID::invalid(), FontFamily::SansSerif, 20, FontStyleFlags::Default));
	}

	g_screenManager->setUIContext(uiContext);
	g_screenManager->setPostRenderCallback(&CallbackPostRender, nullptr);
	g_screenManager->deviceRestored(g_draw);

	g_audioBackend = System_CreateAudioBackend();
	if (g_audioBackend) {
		g_audioBackend->SetRenderCallback(&NativeMixWrapper, nullptr);
		bool reverted = false;
		g_audioBackend->InitOutputDevice(g_Config.sAudioDevice, LatencyMode::Aggressive, &reverted);
		if (reverted) {
			g_Config.sAudioDevice.clear();
		}
	}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	if (IsWin7OrHigher()) {
		winCamera = new WindowsCaptureDevice(CAPTUREDEVIDE_TYPE::VIDEO);
		winCamera->sendMessage({ CAPTUREDEVIDE_COMMAND::INITIALIZE, nullptr });
		winMic = new WindowsCaptureDevice(CAPTUREDEVIDE_TYPE::Audio);
		winMic->sendMessage({ CAPTUREDEVIDE_COMMAND::INITIALIZE, nullptr });
	}
#endif

	// Warn about low refresh rates on desktop. Might add other platforms later.
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(MAC)
	const double displayHz = System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE);
	if (displayHz < 55.0f) {
		// This is a warning, not an error.
		auto g = GetI18NCategory(I18NCat::GRAPHICS);
		g_OSD.Show(OSDType::MESSAGE_WARNING, ApplySafeSubstitutions(g->T("Your display is set to a low refresh rate: %1 Hz. 60 Hz or higher is recommended."), (int)displayHz), 8.0f, "low_refresh");
		g_OSD.SetClickCallback("low_refresh", [](bool clicked, void *) {
			if (clicked) {
				// Open the display settings.
				System_OpenDisplaySettings();
			}
		}, nullptr);
	}
#endif

	g_gameInfoCache = new GameInfoCache();

	if (gpu) {
		PSP_CoreParameter().pixelWidth = g_display.pixel_xres;
		PSP_CoreParameter().pixelHeight = g_display.pixel_yres;
		gpu->DeviceRestore(g_draw);
	}

	INFO_LOG(Log::System, "NativeInitGraphics completed");

	return true;
}

bool CreateGlobalPipelines() {
	using namespace Draw;

	ShaderModule *vs_color_2d = g_draw->GetVshaderPreset(VS_COLOR_2D);
	ShaderModule *fs_color_2d = g_draw->GetFshaderPreset(FS_COLOR_2D);
	ShaderModule *vs_texture_color_2d = g_draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D);
	ShaderModule *fs_texture_color_2d = g_draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D);

	if (!vs_color_2d || !fs_color_2d || !vs_texture_color_2d || !fs_texture_color_2d) {
		ERROR_LOG(Log::G3D, "Failed to get shader preset");
		return false;
	}

	InputLayout *inputLayout = ui_draw2d.CreateInputLayout(g_draw);
	BlendState *blendNormal = g_draw->CreateBlendState({ true, 0xF, BlendFactor::ONE, BlendFactor::ONE_MINUS_SRC_ALPHA });
	DepthStencilState *depth = g_draw->CreateDepthStencilState({ false, false, Comparison::LESS });
	RasterState *rasterNoCull = g_draw->CreateRasterState({});

	PipelineDesc colorDesc{
		Primitive::TRIANGLE_LIST,
		{ vs_color_2d, fs_color_2d },
		inputLayout, depth, blendNormal, rasterNoCull, &vsColBufDesc,
	};
	PipelineDesc texColorDesc{
		Primitive::TRIANGLE_LIST,
		{ vs_texture_color_2d, fs_texture_color_2d },
		inputLayout, depth, blendNormal, rasterNoCull, &vsTexColBufDesc,
	};

	colorPipeline = g_draw->CreateGraphicsPipeline(colorDesc, "global_color");
	if (!colorPipeline) {
		// Something really critical is wrong, don't care much about correct releasing of the states.
		return false;
	}

	texColorPipeline = g_draw->CreateGraphicsPipeline(texColorDesc, "global_texcolor");
	if (!texColorPipeline) {
		// Something really critical is wrong, don't care much about correct releasing of the states.
		return false;
	}

	// Release these now, reference counting should ensure that they get completely released
	// once we delete both pipelines.
	inputLayout->Release();
	rasterNoCull->Release();
	blendNormal->Release();
	depth->Release();
	return true;
}

void NativeShutdownGraphics() {
	INFO_LOG(Log::System, "NativeShutdownGraphics begin");

	if (g_screenManager) {
		g_screenManager->deviceLost();
	}
	g_iconCache.ClearTextures();

	// TODO: This is not really necessary with Vulkan on Android - could keep shaders etc in memory
	if (gpu)
		gpu->DeviceLost();

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	if (winCamera) {
		winCamera->waitShutDown();
		delete winCamera;
		winCamera = nullptr;
	}
	if (winMic) {
		winMic->waitShutDown();
		delete winMic;
		winMic = nullptr;
	}
#endif

	if (g_audioBackend) {
		delete g_audioBackend;
		g_audioBackend = nullptr;
	}

	UIBackgroundShutdown();

	delete g_gameInfoCache;
	g_gameInfoCache = nullptr;

	delete uiContext;
	uiContext = nullptr;

	ui_draw2d.Shutdown();

	if (colorPipeline) {
		colorPipeline->Release();
		colorPipeline = nullptr;
	}
	if (texColorPipeline) {
		texColorPipeline->Release();
		texColorPipeline = nullptr;
	}

	INFO_LOG(Log::System, "NativeShutdownGraphics end");
}

static void TakeScreenshot(Draw::DrawContext *draw) {
	Path path = GetSysDirectory(DIRECTORY_SCREENSHOT);
	if (!File::Exists(path)) {
		File::CreateDir(path);
	}

	// First, find a free filename.
	//
	// NOTE: On Android, the old approach of checking filenames one by one doesn't scale.
	// So let's just grab the full file listing, and then find a name that's not in it.
	//
	// TODO: Also, we could do this on a thread too. Not sure if worth it.

	const std::string gameId = g_paramSFO.GetDiscID();

	// TODO: Make something like IterateFileInDir instead.
	std::vector<File::FileInfo> files;
	const std::string prefix = gameId + "_";
	File::GetFilesInDir(path, &files, nullptr, 0, prefix);
	std::set<std::string> existingNames;
	for (auto &file : files) {
		existingNames.insert(file.name);
	}

	Path filename;
	int i = 0;
	for (int i = 0; i < 20000; i++) {
		const std::string pngName = prefix + StringFromFormat("%05d.png", i);
		const std::string jpgName = prefix + StringFromFormat("%05d.jpg", i);
		if (existingNames.find(pngName) == existingNames.end() && existingNames.find(jpgName) == existingNames.end()) {
			filename = path / (g_Config.bScreenshotsAsPNG ? pngName : jpgName);
			break;
		}
	}

	if (filename.empty()) {
		// Overwrite this one over and over.
		filename = path / (prefix + (g_Config.bScreenshotsAsPNG ? "20000.png" : "20000.jpg"));
	}

	const ScreenshotType type = g_Config.iScreenshotMode == (int)ScreenshotMode::GameImage ? SCREENSHOT_DISPLAY : SCREENSHOT_OUTPUT;

	const ScreenshotResult result = TakeGameScreenshot(draw, filename, g_Config.bScreenshotsAsPNG ? ScreenshotFormat::PNG : ScreenshotFormat::JPG, type, -1, [filename](bool success) {
		if (success) {
			g_OSD.Show(OSDType::MESSAGE_FILE_LINK, filename.ToVisualString(), 0.0f, "screenshot_link");
			if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
				g_OSD.SetClickCallback("screenshot_link", [](bool clicked, void *data) -> void {
					Path *path = reinterpret_cast<Path *>(data);
					if (clicked) {
						System_ShowFileInFolder(*path);
					} else {
						delete path;
					}
				}, new Path(filename));
			}
		} else {
			auto err = GetI18NCategory(I18NCat::ERRORS);
			g_OSD.Show(OSDType::MESSAGE_ERROR, err->T("Could not save screenshot file"));
			WARN_LOG(Log::System, "Failed to take screenshot.");
		}
	});
}

void CallbackPostRender(UIContext *dc, void *userdata) {
	if (g_TakeScreenshot) {
		TakeScreenshot(dc->GetDrawContext());
		g_TakeScreenshot = false;
	}
}

static void SendMouseDeltaAxis();

void NativeFrame(GraphicsContext *graphicsContext) {
	PROFILE_END_FRAME();

	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_DESKTOP) {
		if (g_windowHidden && g_Config.bPauseWhenMinimized) {
			sleep_ms(16, "window-hidden");
			return;
		}
	}

	// This can only be accessed from Windows currently, and causes linking errors with headless etc.
	if (g_restartGraphics == 1) {
		// Used for debugging only.
		NativeShutdownGraphics();
		g_restartGraphics++;
		return;
	}
	else if (g_restartGraphics == 2) {
		NativeInitGraphics(graphicsContext);
		g_restartGraphics = 0;
	}

	double startTime = time_now_d();

	ProcessWheelRelease(NKCODE_EXT_MOUSEWHEEL_UP, startTime, false);
	ProcessWheelRelease(NKCODE_EXT_MOUSEWHEEL_DOWN, startTime, false);

	SetOverrideScreenFrame(nullptr);

	// it's ok to call this redundantly with DoFrame from EmuScreen
	Achievements::Idle();

	g_DownloadManager.Update();

	g_Discord.Update();

	g_OSD.Update();

	_dbg_assert_(graphicsContext != nullptr);
	_dbg_assert_(g_screenManager != nullptr);

	g_GameManager.Update();

	if (GetUIState() != UISTATE_INGAME) {
		// Note: We do this from NativeFrame so that the graphics context is
		// guaranteed valid, to be safe - g_gameInfoCache messes around with textures.
		g_BackgroundAudio.Update();
	}

	g_iconCache.FrameUpdate();

	g_screenManager->update();

	if (g_audioBackend) {
		g_audioBackend->FrameUpdate(g_Config.bAutoAudioDevice);
	}

	// Do this after g_screenManager.update() so we can receive setting changes before rendering.
	{
		std::vector<PendingMessage> toProcess;
		std::vector<std::function<void()>> toRun;
		{
			std::lock_guard<std::mutex> lock(g_pendingMutex);
			toProcess = std::move(pendingMessages);
			toRun = std::move(g_pendingClosures);
			pendingMessages.clear();
			g_pendingClosures.clear();
		}

		for (auto &item : toRun) {
			item();
		}

		for (const auto &item : toProcess) {
			// Hack.
			if (item.message == UIMessage::WINDOW_RESTORED && graphicsContext) {
				graphicsContext->NotifyWindowRestored();
			}

			if (HandleGlobalMessage(item.message, item.value)) {
				// TODO: Add a to-string thingy.
				VERBOSE_LOG(Log::System, "Handled global message: %d / %s", (int)item.message, item.value.c_str());
			}
			g_screenManager->sendMessage(item.message, item.value.c_str());
		}
	}

	g_requestManager.ProcessRequests();

	g_breakpoints.Frame();

	// Apply the UIContext bounds as a 2D transformation matrix.
	Matrix4x4 ortho = ComputeOrthoMatrix(g_display.dp_xres, g_display.dp_yres, graphicsContext->GetDrawContext()->GetDeviceCaps().coordConvention);

	Draw::DebugFlags debugFlags = Draw::DebugFlags::NONE;
	if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::GPU_PROFILE)
		debugFlags |= Draw::DebugFlags::PROFILE_TIMESTAMPS;
	if (g_Config.bGpuLogProfiler)
		debugFlags |= Draw::DebugFlags::PROFILE_SCOPES;

	// Can be overridden by sceDisplay which may pass true for the second argument.
	g_frameTiming.ComputePresentMode(g_draw, false);

	g_draw->BeginFrame(debugFlags);

	ui_draw2d.PushDrawMatrix(ortho);

	g_screenManager->getUIContext()->SetTintSaturation(g_Config.fUITint, g_Config.fUISaturation);

	// All actual rendering happen in here.
	ScreenRenderFlags renderFlags = g_screenManager->render();
	if (g_screenManager->getUIContext()->Text()) {
		g_screenManager->getUIContext()->Text()->OncePerFrame();
	}

	ui_draw2d.PopDrawMatrix();

	g_draw->EndFrame();

	// This, between EndFrame and Present, is where we should actually wait to do present time management.
	// There might not be a meaningful distinction here for all backends..
	g_frameTiming.PostSubmit();

	if (renderCounter < 10 && ++renderCounter == 10) {
		// We're rendering fine, clear out failure info.
		ClearFailedGPUBackends();
	}

	g_draw->Present(g_frameTiming.PresentMode());

	if (resized) {
		INFO_LOG(Log::G3D, "Resized flag set - recalculating bounds");
		resized = false;

		if (uiContext) {
			// Modifying the bounds here can be used to "inset" the whole image to gain borders for TV overscan etc.
			// The UI now supports any offset but not the EmuScreen yet.
			uiContext->SetBounds(Bounds(0, 0, g_display.dp_xres, g_display.dp_yres));

			// OSX 10.6 and SDL 1.2 bug.
#if defined(__APPLE__) && !defined(USING_QT_UI)
			static int dp_xres_old = g_display.dp_xres;
			if (g_display.dp_xres != dp_xres_old) {
				dp_xres_old = g_display.dp_xres;
			}
#endif
		}

		graphicsContext->Resize();
		g_screenManager->resized();

		// TODO: Move this to the GraphicsContext objects for each backend.
#if !PPSSPP_PLATFORM(WINDOWS) && !defined(ANDROID)
		PSP_CoreParameter().pixelWidth = g_display.pixel_xres;
		PSP_CoreParameter().pixelHeight = g_display.pixel_yres;
		System_PostUIMessage(UIMessage::GPU_DISPLAY_RESIZED);
#endif
	} else {
		// INFO_LOG(Log::G3D, "Polling graphics context");
		graphicsContext->Poll();
	}

	SendMouseDeltaAxis();

	if (!(renderFlags & ScreenRenderFlags::HANDLED_THROTTLING)) {
		// TODO: We should ideally mix this with game audio.
		g_BackgroundAudio.Play();

		float refreshRate = System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE);
		static double lastTime = 0.0;
		if (lastTime > 0.0) {
			double now = time_now_d();
			// Simple throttling to not burn the GPU in the menu.
			// TODO: This should move into NativeFrame.
			double diffTime = now - lastTime;
			int sleepTimeUs = (int)(1000000 * ((1.0 / refreshRate) - diffTime));
			// printf("sleep: %0.3f ms (diff: %0.3f) %f\n", (double)sleepTimeUs / 1000, diffTime * 1000.0, refreshRate);

			// If presentation mode is FIFO, we don't need to sleep a lot, we'll be throttled by
			// presentation. But still, let's sleep a bit.
			// Actually, for some reason this increases latency a lot, to the degree that the UI
			// gets hard to use.. Commenting out for now.
			// if (g_frameTiming.PresentMode() == Draw::PresentMode::FIFO) {
			//    sleepTimeUs = std::min(2000, sleepTimeUs); // 2 ms
			// }

			if (sleepTimeUs > 0)
				sleep_us(sleepTimeUs, "fallback-throttle");
		}
		lastTime = time_now_d();
	}
}

bool HandleGlobalMessage(UIMessage message, const std::string &value) {
	if (message == UIMessage::RESTART_GRAPHICS) {
		g_restartGraphics = 1;
		return true;
	} else if (message == UIMessage::SAVESTATE_DISPLAY_SLOT) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		std::string msg = StringFromFormat("%s: %d", sy->T_cstr("Savestate Slot"), SaveState::GetCurrentSlot() + 1);
		// Show for the same duration as the preview.
		g_OSD.Show(OSDType::MESSAGE_INFO, msg, 2.0f, "savestate_slot");
		return true;
	}
	else if (message == UIMessage::GPU_DISPLAY_RESIZED) {
		if (gpu) {
			gpu->NotifyDisplayResized();
		}
		return true;
	}
	else if (message == UIMessage::GPU_RENDER_RESIZED) {
		if (gpu) {
			DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
			gpu->NotifyRenderResized(config);
		}
		return true;
	}
	else if (message == UIMessage::GPU_CONFIG_CHANGED) {
		if (gpu) {
			gpu->NotifyConfigChanged();
		}
		Reporting::UpdateConfig();
		return true;
	}
	else if (message == UIMessage::POWER_SAVING) {
		if (value != "false") {
			auto sy = GetI18NCategory(I18NCat::SYSTEM);
#if PPSSPP_PLATFORM(ANDROID)
			g_OSD.Show(OSDType::MESSAGE_WARNING, sy->T("WARNING: Android battery save mode is on"), 2.0f, "core_powerSaving");
#else
			g_OSD.Show(OSDType::MESSAGE_WARNING, sy->T("WARNING: Battery save mode is on"), 2.0f, "core_powerSaving");
#endif
		}
		Core_SetPowerSaving(value != "false");
		return true;
	}
	else if (message == UIMessage::PERMISSION_GRANTED && value == "storage") {
		CreateSysDirectories();
		// We must have failed to load the config before, so load it now to avoid overwriting the old config
		// with a freshly generated one.
		// NOTE: If graphics backend isn't what's in the config (due to error fallback, or not matching the default
		// and then getting permission), it will get out of sync. So we save and restore g_Config.iGPUBackend.
		// Ideally we should simply reinitialize graphics to the mode from the config, but there are potential issues.
		int gpuBackend = g_Config.iGPUBackend;
		INFO_LOG(Log::IO, "Reloading config after storage permission grant.");
		g_Config.Reload();
		PostLoadConfig();
		g_Config.iGPUBackend = gpuBackend;
		return true;
	} else if (message == UIMessage::APP_RESUMED || message == UIMessage::GOT_FOCUS) {
		// Assume that the user may have modified things.
		MemoryStick_NotifyWrite();
		return true;
	} else if (message == UIMessage::SAVE_FRAME_DUMP) {
		SaveFrameDump();
		return true;
	} else {
		return false;
	}
}

bool NativeIsAtTopLevel() {
	// This might need some synchronization?
	if (!g_screenManager) {
		ERROR_LOG(Log::System, "No screen manager active");
		return false;
	}
	Screen *currentScreen = g_screenManager->topScreen();
	if (currentScreen) {
		bool top = currentScreen->isTopLevel();
		return currentScreen->isTopLevel();
	} else {
		ERROR_LOG(Log::System, "No current screen");
		return false;
	}
}

void NativeTouch(const TouchInput &touch) {
	if (!g_screenManager) {
		return;
	}

	// Brute force prevent NaNs from getting into the UI system.
	// Don't think this is actually necessary in practice.
	if (my_isnan(touch.x) || my_isnan(touch.y)) {
		return;
	}
	g_screenManager->touch(touch);
}

// up, down
static double g_wheelReleaseTime[2]{};

static void ProcessWheelRelease(InputKeyCode keyCode, double now, bool keyPress) {
	int dir = keyCode - NKCODE_EXT_MOUSEWHEEL_UP;
	if (g_wheelReleaseTime[dir] != 0.0 && (keyPress || now >= g_wheelReleaseTime[dir])) {
		g_wheelReleaseTime[dir] = 0.0;
		KeyInput key{};
		key.deviceId = DEVICE_ID_MOUSE;
		key.keyCode = keyCode;
		key.flags = KEY_UP;
		NativeKey(key);
	}

	if (keyPress) {
		float releaseTime = (float)g_Config.iMouseWheelUpDelayMs * (1.0f / 1000.0f);
		g_wheelReleaseTime[dir] = now + releaseTime;
	}
}

bool NativeKey(const KeyInput &key) {
	double now = time_now_d();

	// VR actions
	if ((IsVREnabled() || g_Config.bForceVR) && !UpdateVRKeys(key)) {
		return false;
	}

#if PPSSPP_PLATFORM(UWP)
	// Ignore if key sent from OnKeyDown/OnKeyUp/XInput while text edit active
	// it's already handled by `OnCharacterReceived`
	if (IgnoreInput(key.keyCode) && !(key.flags & KEY_CHAR)) {
		return false;
	}
#endif

	// INFO_LOG(Log::System, "Key code: %i flags: %i", key.keyCode, key.flags);
#if !defined(MOBILE_DEVICE)
	if (g_Config.bPauseExitsEmulator) {
		std::vector<int> pspKeys;
		pspKeys.clear();
		if (KeyMap::InputMappingToPspButton(InputMapping(key.deviceId, key.keyCode), &pspKeys)) {
			if (std::find(pspKeys.begin(), pspKeys.end(), VIRTKEY_PAUSE) != pspKeys.end()) {
				System_ExitApp();
				return true;
			}
		}
	}
#endif

#ifdef _DEBUG
	// Debug hack: Randomize the language with F9!
	if (false && (key.keyCode == NKCODE_F9 && (key.flags & KEY_DOWN))) {
		std::vector<File::FileInfo> tempLangs;
		g_VFS.GetFileListing("lang", &tempLangs, "ini");
		int x = rand() % tempLangs.size();
		std::string_view code, part2;
		if (SplitStringOnce(tempLangs[x].name, &code, &part2, '.')) {
			g_Config.sLanguageIni = code;
			INFO_LOG(Log::System, "Switching to random language: %s", g_Config.sLanguageIni.c_str());
			if (g_i18nrepo.LoadIni(g_Config.sLanguageIni)) {
				g_screenManager->RecreateAllViews();
				System_Notify(SystemNotification::UI);
			}
		}
	}
#endif

	if (!g_screenManager) {
		return false;
	}

	// Handle releases of mousewheel keys.
	if ((key.flags & KEY_DOWN) && key.deviceId == DEVICE_ID_MOUSE && (key.keyCode == NKCODE_EXT_MOUSEWHEEL_UP || key.keyCode == NKCODE_EXT_MOUSEWHEEL_DOWN)) {
		ProcessWheelRelease(key.keyCode, now, true);
	}

	HLEPlugins::SetKey(key.keyCode, (key.flags & KEY_DOWN) ? 1 : 0);
	// Dispatch the key event.
	bool retval = g_screenManager->key(key);

	// The Mode key can have weird consequences on some devices, see #17245.
	if (key.keyCode == NKCODE_BUTTON_MODE) {
		// Tell the caller that we handled the key.
		retval = true;
	}

	return retval;
}

void NativeAxis(const AxisInput *axes, size_t count) {
	// VR actions
	if ((IsVREnabled() || g_Config.bForceVR) && !UpdateVRAxis(axes, count)) {
		return;
	}

	if (!g_screenManager) {
		// Too early.
		return;
	}

	g_screenManager->axis(axes, count);

	for (size_t i = 0; i < count; i++) {
		const AxisInput &axis = axes[i];
		HLEPlugins::PluginDataAxis[axis.axisId] = axis.value;
	}
}

// Called from NativeFrame and from NativeMouseDelta.
static void SendMouseDeltaAxis() {
	float mx, my;
	MouseEventProcessor::MouseDeltaToAxes(time_now_d(), &mx, &my);

	AxisInput axis[2];
	axis[0].axisId = JOYSTICK_AXIS_MOUSE_REL_X;
	axis[0].deviceId = DEVICE_ID_MOUSE;
	axis[0].value = mx;
	axis[1].axisId = JOYSTICK_AXIS_MOUSE_REL_Y;
	axis[1].deviceId = DEVICE_ID_MOUSE;
	axis[1].value = my;

	HLEPlugins::PluginDataAxis[JOYSTICK_AXIS_MOUSE_REL_X] = mx;
	HLEPlugins::PluginDataAxis[JOYSTICK_AXIS_MOUSE_REL_Y] = my;

	//NOTICE_LOG(Log::System, "delta: %0.2f %0.2f    mx/my: %0.2f %0.2f   dpi: %f  sens: %f ",
	//	g_mouseDeltaX, g_mouseDeltaY, mx, my, g_display.dpi_scale_x, g_Config.fMouseSensitivity);

	if (GetUIState() == UISTATE_INGAME || g_IsMappingMouseInput) {
		NativeAxis(axis, 2);
	}
}

void NativeMouseDelta(float dx, float dy) {
	if (!g_Config.bMouseControl)
		return;

	// Remap, shared code. Then send it as a regular axis event.
	MouseEventProcessor::ProcessDelta(time_now_d(), dx, dy);

	SendMouseDeltaAxis();
}

// TODO: Should include a device ID here, since accelerometers can be on pads for example (DualSense).
void NativeAccelerometer(float tiltX, float tiltY, float tiltZ) {
	if (g_Config.iTiltInputType == TILT_NULL) {
		// if tilt events are disabled, don't do anything special.
		return;
	}

	// create the base coordinate tilt system from the calibration data.
	float tiltBaseAngleY = g_Config.fTiltBaseAngleY;

	// Figure out the sensitivity of the tilt. (sensitivity is originally 0 - 100)
	// We divide by 50, so that the rest of the 50 units can be used to overshoot the
	// target. If you want precise control, you'd keep the sensitivity ~50.
	// For games that don't need much control but need fast reactions,
	// then a value of 70-80 is the way to go.
	float xSensitivity = g_Config.iTiltSensitivityX / 50.0;
	float ySensitivity = g_Config.iTiltSensitivityY / 50.0;

	// x and y are flipped if we are in landscape orientation. The events are
	// sent with respect to the portrait coordinate system, while we
	// take all events in landscape.
	// see [http://developer.android.com/guide/topics/sensors/sensors_overview.html] for details
	bool landscape = g_display.dp_yres < g_display.dp_xres;
	// now transform out current tilt to the calibrated coordinate system
	TiltEventProcessor::ProcessTilt(landscape, tiltBaseAngleY, tiltX, tiltY, tiltZ,
		g_Config.bInvertTiltX, g_Config.bInvertTiltY,
		xSensitivity, ySensitivity);

	HLEPlugins::PluginDataAxis[JOYSTICK_AXIS_ACCELEROMETER_X] = tiltX;
	HLEPlugins::PluginDataAxis[JOYSTICK_AXIS_ACCELEROMETER_Y] = tiltY;
	HLEPlugins::PluginDataAxis[JOYSTICK_AXIS_ACCELEROMETER_Z] = tiltZ;
}

void System_PostUIMessage(UIMessage message, std::string_view param) {
	std::lock_guard<std::mutex> lock(g_pendingMutex);
	PendingMessage pendingMessage;
	pendingMessage.message = message;
	pendingMessage.value = param;
	pendingMessages.push_back(pendingMessage);
}

void System_RunOnMainThread(std::function<void()> func) {
	std::lock_guard<std::mutex> lock(g_pendingMutex);
	g_pendingClosures.push_back(std::move(func));
}

void NativeResized() {
	// NativeResized can come from any thread so we just set a flag, then process it later.
	VERBOSE_LOG(Log::G3D, "NativeResized - setting flag");
	resized = true;
}

void NativeVSync(int64_t vsyncId, double frameTime, double expectedPresentationTime) {
	// INFO_LOG(Log::System, "NativeVSync called with id %lld, frameTime %f, expectedPresentationTime %f", (long long)vsyncId, frameTime, expectedPresentationTime);
	// TODO: Make use of this.
}

void NativeSetRestarting() {
	restarting = true;
}

bool NativeIsRestarting() {
	return restarting;
}

void NativeShutdown() {
	INFO_LOG(Log::System, "NativeShutdown begin");

	Achievements::Shutdown();

	if (g_Config.bAchievementsEnable) {
		FILE *iconCacheFile = File::OpenCFile(GetSysDirectory(DIRECTORY_CACHE) / "icon.cache", "wb");
		if (iconCacheFile) {
			g_iconCache.SaveToFile(iconCacheFile);
			fclose(iconCacheFile);
		}
	}

	if (g_screenManager) {
		g_screenManager->shutdown();
		delete g_screenManager;
		g_screenManager = nullptr;
	}

	g_Config.Save("NativeShutdown");

	g_i18nrepo.LogMissingKeys();

	ShutdownWebServer();

	__UPnPShutdown();

	g_PortManager.Shutdown();

	net::Shutdown();

	g_Discord.Shutdown();

	ShaderTranslationShutdown();

	// Avoid shutting this down when restarting core.
	if (!restarting) {
		g_logManager.Shutdown();
	}

	g_threadManager.Teardown();

#if !PPSSPP_PLATFORM(IOS)
	System_ExitApp();
#endif

	// Previously we did exit() here on Android but that makes it hard to do things like restart on backend change.
	// I think we handle most globals correctly or correct-enough now.
	INFO_LOG(Log::System, "NativeShutdown end");
}

// In the future, we might make this more sophisticated, such as storing in the app private directory on Android.
// Right now we just store secrets in separate files next to ppsspp.ini. The important thing is keeping them out of it
// since we often ask people to post or send the ini for debugging.
static Path GetSecretPath(std::string_view nameOfSecret) {
	return GetSysDirectory(DIRECTORY_SYSTEM) / ("ppsspp_" + std::string(nameOfSecret) + ".dat");
}

// name should be simple alphanumerics to avoid problems on Windows.
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data) {
	Path path = GetSecretPath(nameOfSecret);
	if (data.empty() && File::Exists(path)) {
		return File::Delete(path);
	} else if (!File::WriteDataToFile(false, data.data(), data.size(), path)) {
		WARN_LOG(Log::System, "Failed to write secret '%.*s' to path '%s'", (int)nameOfSecret.size(), nameOfSecret.data(), path.c_str());
		return false;
	}
	return true;
}

// On failure, returns an empty string. Good enough since any real secret is non-empty.
std::string NativeLoadSecret(std::string_view nameOfSecret) {
	Path path = GetSecretPath(nameOfSecret);
	std::string data;
	if (!File::ReadBinaryFileToString(path, &data)) {
		data.clear();  // just to be sure.
	}
	return data;
}

void Native_NotifyWindowHidden(bool hidden) {
	g_windowHidden = hidden;
	// TODO: Wait until we can react?
}

bool Native_IsWindowHidden() {
	return g_windowHidden;
}

static bool IsWindowSmall(int pixelWidth, int pixelHeight) {
	if (!g_Config.bShrinkIfWindowSmall) {
		return false;
	}

	// Can't take this from config as it will not be set if windows is maximized.
	int w = (int)(pixelWidth * g_display.dpi_scale_real_x);
	int h = (int)(pixelHeight * g_display.dpi_scale_real_y);
	DisplayLayoutConfig &config = g_Config.GetDisplayLayoutConfig(g_display.GetDeviceOrientation());
	return config.InternalRotationIsPortrait() ? (h < 480 + 80) : (w < 480 + 80);
}

bool Native_UpdateScreenScale(int pixel_width, int pixel_height, float customScale) {
	_dbg_assert_(customScale > 0.1f);
	float g_logical_dpi = System_GetPropertyFloat(SYSPROP_DISPLAY_LOGICAL_DPI);
	float dpi = System_GetPropertyFloat(SYSPROP_DISPLAY_DPI);

	if (dpi < 0.0f) {
		dpi = 96.0f;
	}
	if (g_logical_dpi < 0.0f) {
		g_logical_dpi = 96.0f;
	}

	bool smallWindow = IsWindowSmall(pixel_width, pixel_height);
	if (smallWindow) {
		customScale *= 0.5f;
	} else {
		customScale = UIScaleFactorToMultiplier(g_Config.iUIScaleFactor);
	}

	if (g_display.Recalculate(pixel_width, pixel_height, g_logical_dpi / dpi, g_logical_dpi / dpi, customScale)) {
		NativeResized();
		return true;
	} else {
		return false;
	}
}

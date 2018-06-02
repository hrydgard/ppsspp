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

#include <locale.h>
// Linux doesn't like using std::find with std::vector<int> without this :/
#if !defined(MOBILE_DEVICE)
#include <algorithm>
#endif
#include <memory>
#include <thread>
#include <mutex>

#if defined(_WIN32)
#include "Windows/DSoundStream.h"
#include "Windows/MainWindow.h"
#endif

#include "base/display.h"
#include "base/timeutil.h"
#include "base/logging.h"
#include "base/NativeApp.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "net/http_client.h"
#include "net/resolve.h"
#include "gfx_es2/draw_text.h"
#include "gfx_es2/gpu_features.h"
#include "i18n/i18n.h"
#include "input/input_state.h"
#include "math/fast/fast_math.h"
#include "math/math_util.h"
#include "math/lin/matrix4x4.h"
#include "profiler/profiler.h"
#include "thin3d/thin3d.h"
#include "ui/ui.h"
#include "ui/screen.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "util/text/utf8.h"

#include "Common/CPUDetect.h"
#include "Common/FileUtil.h"
#include "Common/LogManager.h"
#include "Common/MemArena.h"
#include "Common/GraphicsContext.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/FileLoaders/DiskCachingFileLoader.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/SaveState.h"
#include "Core/Screenshot.h"
#include "Core/System.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"
#include "Core/Util/GameManager.h"
#include "Core/Util/AudioFormat.h"
#include "GPU/GPUInterface.h"

#include "ui_atlas.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/HostTypes.h"
#include "UI/OnScreenDisplay.h"
#include "UI/MiscScreens.h"
#include "UI/RemoteISOScreen.h"
#include "UI/TiltEventProcessor.h"
#include "UI/BackgroundAudio.h"
#include "UI/TextureUtil.h"

#if !defined(MOBILE_DEVICE)
#include "Common/KeyMap.h"
#endif

#if !defined(MOBILE_DEVICE) && defined(USING_QT_UI)
#include "Qt/QtHost.h"
#endif

// The new UI framework, for initialization

static UI::Theme ui_theme;

#if defined(ARM) && defined(__ANDROID__)
#include "../../android/jni/ArmEmitterTest.h"
#elif defined(ARM64) && defined(__ANDROID__)
#include "../../android/jni/Arm64EmitterTest.h"
#endif

#ifdef IOS
#include "ios/iOSCoreAudio.h"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// https://github.com/richq/android-ndk-profiler
#ifdef ANDROID_NDK_PROFILER
#include <stdlib.h>
#include "android/android-ndk-profiler/prof.h"
#endif

ScreenManager *screenManager;
std::string config_filename;

bool g_graphicsInited;

// Really need to clean this mess of globals up... but instead I add more :P
bool g_TakeScreenshot;
static bool isOuya;
static bool resized = false;
static bool restarting = false;

static bool askedForStoragePermission = false;

struct PendingMessage {
	std::string msg;
	std::string value;
};

static std::mutex pendingMutex;
static std::vector<PendingMessage> pendingMessages;
static Draw::DrawContext *g_draw;
static Draw::Pipeline *colorPipeline;
static Draw::Pipeline *texColorPipeline;
static UIContext *uiContext;
static std::vector<std::string> inputboxValue;

#ifdef _WIN32
WindowsAudioBackend *winAudioBackend;
#endif

std::thread *graphicsLoadThread;

class AndroidLogger : public LogListener {
public:
	void Log(const LogMessage &message) override {
		// Log with simplified headers as Android already provides timestamp etc.
		switch (message.level) {
		case LogTypes::LVERBOSE:
		case LogTypes::LDEBUG:
		case LogTypes::LINFO:
			ILOG("[%s] %s", message.log, message.msg.c_str());
			break;
		case LogTypes::LERROR:
			ELOG("[%s] %s", message.log, message.msg.c_str());
			break;
		case LogTypes::LWARNING:
			WLOG("[%s] %s", message.log, message.msg.c_str());
			break;
		case LogTypes::LNOTICE:
		default:
			ILOG("[%s] !!! %s", message.log, message.msg.c_str());
			break;
		}
	}
};

#ifdef _WIN32
int Win32Mix(short *buffer, int numSamples, int bits, int rate, int channels) {
	return NativeMix(buffer, numSamples);
}
#endif

// globals
static AndroidLogger *logger = nullptr;
std::string boot_filename = "";

void NativeHost::InitSound() {
#ifdef IOS
	iOSCoreAudioInit();
#endif
}

void NativeHost::ShutdownSound() {
#ifdef IOS
	iOSCoreAudioShutdown();
#endif
}

#if !defined(MOBILE_DEVICE) && defined(USING_QT_UI)
void QtHost::InitSound() { }
void QtHost::ShutdownSound() { }
#endif

std::string NativeQueryConfig(std::string query) {
	char temp[128];
	if (query == "screenRotation") {
		ILOG("g_Config.screenRotation = %d", g_Config.iScreenRotation);
		snprintf(temp, sizeof(temp), "%d", g_Config.iScreenRotation);
		return std::string(temp);
	} else if (query == "immersiveMode") {
		return std::string(g_Config.bImmersiveMode ? "1" : "0");
	} else if (query == "hwScale") {
		int scale = g_Config.iAndroidHwScale;
		if (scale == 1) {
			// If g_Config.iInternalResolution is also set to Auto (1), we fall back to "Device resolution" (0). It works out.
			scale = g_Config.iInternalResolution;
		} else if (scale >= 2) {
			scale -= 1;
		}

		int max_res = std::max(System_GetPropertyInt(SYSPROP_DISPLAY_XRES), System_GetPropertyInt(SYSPROP_DISPLAY_YRES)) / 480 + 1;
		snprintf(temp, sizeof(temp), "%d", std::min(scale, max_res));
		return std::string(temp);
	} else if (query == "force44khz") {
		return std::string("0");
	} else if (query == "androidJavaGL") {
		// If we're using Vulkan, we say no... need C++ to use Vulkan.
		if (GetGPUBackend() == GPUBackend::VULKAN) {
			return "false";
		}
		// Otherwise, some devices prefer the Java init so play it safe.
		return "true";
	} else if (query == "sustainedPerformanceMode") {
		return std::string(g_Config.bSustainedPerformanceMode ? "1" : "0");
	} else {
		return "";
	}
}

int NativeMix(short *audio, int num_samples) {
	if (GetUIState() != UISTATE_INGAME) {
		PlayBackgroundAudio();
	}

	int sample_rate = System_GetPropertyInt(SYSPROP_AUDIO_SAMPLE_RATE);
	num_samples = __AudioMix(audio, num_samples, sample_rate > 0 ? sample_rate : 44100);

#ifdef _WIN32
	winAudioBackend->Update();
#endif

	return num_samples;
}

// This is called before NativeInit so we do a little bit of initialization here.
void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape, std::string *version) {
	*app_nice_name = "PPSSPP";
	*app_dir_name = "ppsspp";
	*landscape = true;
	*version = PPSSPP_GIT_VERSION;

#if defined(ARM) && defined(__ANDROID__)
	ArmEmitterTest();
#elif defined(ARM64) && defined(__ANDROID__)
	Arm64EmitterTest();
#endif
}

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
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

static void PostLoadConfig() {
	// On Windows, we deal with currentDirectory in InitSysDirectories().
#ifndef _WIN32
	if (g_Config.currentDirectory.empty()) {
#if defined(__ANDROID__)
		g_Config.currentDirectory = g_Config.externalDirectory;
#elif defined(IOS)
		g_Config.currentDirectory = g_Config.internalDataDirectory;
#else
		if (getenv("HOME") != nullptr)
			g_Config.currentDirectory = getenv("HOME");
		else
			g_Config.currentDirectory = "./";
#endif
	}
#endif

	// Allow the lang directory to be overridden for testing purposes (e.g. Android, where it's hard to
	// test new languages without recompiling the entire app, which is a hassle).
	const std::string langOverridePath = g_Config.memStickDirectory + "PSP/SYSTEM/lang/";

	// If we run into the unlikely case that "lang" is actually a file, just use the built-in translations.
	if (!File::Exists(langOverridePath) || !File::IsDirectory(langOverridePath))
		i18nrepo.LoadIni(g_Config.sLanguageIni);
	else
		i18nrepo.LoadIni(g_Config.sLanguageIni, langOverridePath);
}

void CreateDirectoriesAndroid() {
	// On Android, create a PSP directory tree in the external_dir,
	// to hopefully reduce confusion a bit.
	ILOG("Creating %s", (g_Config.memStickDirectory + "PSP").c_str());
	File::CreateDir(g_Config.memStickDirectory + "PSP");
	File::CreateDir(g_Config.memStickDirectory + "PSP/SAVEDATA");
	File::CreateDir(g_Config.memStickDirectory + "PSP/PPSSPP_STATE");
	File::CreateDir(g_Config.memStickDirectory + "PSP/GAME");
	File::CreateDir(g_Config.memStickDirectory + "PSP/SYSTEM");

	// Avoid media scanners in PPSSPP_STATE and SAVEDATA directories
	File::CreateEmptyFile(g_Config.memStickDirectory + "PSP/PPSSPP_STATE/.nomedia");
	File::CreateEmptyFile(g_Config.memStickDirectory + "PSP/SAVEDATA/.nomedia");
}

void NativeInit(int argc, const char *argv[], const char *savegame_dir, const char *external_dir, const char *cache_dir, bool fs) {
	net::Init();  // This needs to happen before we load the config. So on Windows we also run it in Main. It's fine to call multiple times.

	InitFastMath(cpu_info.bNEON);
	SetupAudioFormats();

	// Make sure UI state is MENU.
	ResetUIState();

	bool skipLogo = false;
	setlocale( LC_ALL, "C" );
	std::string user_data_path = savegame_dir;
	pendingMessages.clear();
#ifdef IOS
	user_data_path += "/";
#endif

	// We want this to be FIRST.
#ifdef USING_QT_UI
	VFSRegister("", new AssetsAssetReader());
#elif defined(IOS)
	// Packed assets are included in app
	VFSRegister("", new DirectoryAssetReader(external_dir));
#endif
#if !defined(MOBILE_DEVICE) && !defined(_WIN32)
	VFSRegister("", new DirectoryAssetReader((File::GetExeDirectory() + "assets/").c_str()));
	VFSRegister("", new DirectoryAssetReader((File::GetExeDirectory()).c_str()));
	VFSRegister("", new DirectoryAssetReader("/usr/share/ppsspp/assets/"));
#endif
	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(savegame_dir));

#if (defined(MOBILE_DEVICE) || !defined(USING_QT_UI)) && !PPSSPP_PLATFORM(UWP)
	if (host == nullptr) {
		host = new NativeHost();
	}
#endif

	g_Config.internalDataDirectory = savegame_dir;
	g_Config.externalDirectory = external_dir;

#if defined(__ANDROID__)
	// Maybe there should be an option to use internal memory instead, but I think
	// that for most people, using external memory (SDCard/USB Storage) makes the
	// most sense.
	g_Config.memStickDirectory = std::string(external_dir) + "/";
	g_Config.flash0Directory = std::string(external_dir) + "/flash0/";
#elif defined(IOS)
	g_Config.memStickDirectory = user_data_path;
	g_Config.flash0Directory = std::string(external_dir) + "/flash0/";
#elif !defined(_WIN32)
	std::string config;
	if (getenv("XDG_CONFIG_HOME") != NULL)
		config = getenv("XDG_CONFIG_HOME");
	else if (getenv("HOME") != NULL)
		config = getenv("HOME") + std::string("/.config");
	else // Just in case
		config = "./config";

	g_Config.memStickDirectory = config + "/ppsspp/";
	g_Config.flash0Directory = File::GetExeDirectory() + "/assets/flash0/";
#endif

	if (cache_dir && strlen(cache_dir)) {
		DiskCachingFileLoaderCache::SetCacheDir(cache_dir);
		g_Config.appCacheDirectory = cache_dir;
	}

	if (!LogManager::GetInstance())
		LogManager::Init();

#ifndef _WIN32
	g_Config.AddSearchPath(user_data_path);
	g_Config.AddSearchPath(g_Config.memStickDirectory + "PSP/SYSTEM/");
	g_Config.SetDefaultPath(g_Config.memStickDirectory + "PSP/SYSTEM/");

	// Note that if we don't have storage permission here, loading the config will
	// fail and it will be set to the default. Later, we load again when we get permission.
	g_Config.Load();
#endif
	LogManager *logman = LogManager::GetInstance();

#ifdef __ANDROID__
	CreateDirectoriesAndroid();
#endif

	const char *fileToLog = 0;
	const char *stateToLoad = 0;

	bool gotBootFilename = false;

	// Parse command line
	LogTypes::LOG_LEVELS logLevel = LogTypes::LINFO;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'd':
				// Enable debug logging
				// Note that you must also change the max log level in Log.h.
				logLevel = LogTypes::LDEBUG;
				break;
			case 'v':
				// Enable verbose logging
				// Note that you must also change the max log level in Log.h.
				logLevel = LogTypes::LVERBOSE;
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
				g_Config.iCpuCore = (int)CPUCore::IR_JIT;
				g_Config.bSaveSettings = false;
				break;
			case '-':
				if (!strncmp(argv[i], "--log=", strlen("--log=")) && strlen(argv[i]) > strlen("--log="))
					fileToLog = argv[i] + strlen("--log=");
				if (!strncmp(argv[i], "--state=", strlen("--state=")) && strlen(argv[i]) > strlen("--state="))
					stateToLoad = argv[i] + strlen("--state=");
				if (!strncmp(argv[1], "--PS3", strlen("--PS3")))
					g_Config.bPS3Controller = true;
#if !defined(MOBILE_DEVICE)
				if (!strncmp(argv[i], "--escape-exit", strlen("--escape-exit")))
					g_Config.bPauseExitsEmulator = true;
#endif
				if (!strncmp(argv[i], "--pause-menu-exit", strlen("--pause-menu-exit")))
					g_Config.bPauseMenuExitsEmulator = true;
				break;
			}
		} else {
			// This parameter should be a boot filename. Only accept it if we
			// don't already have one.
			if (!gotBootFilename) {
				gotBootFilename = true;
				ILOG("Boot filename found in args: '%s'", argv[i]);

				bool okToLoad = true;
				bool okToCheck = true;
				if (System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS)) {
					PermissionStatus status = System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE);
					if (status == PERMISSION_STATUS_DENIED) {
						ELOG("Storage permission denied. Launching without argument.");
						okToLoad = false;
						okToCheck = false;
					} else if (status != PERMISSION_STATUS_GRANTED) {
						ELOG("Storage permission not granted. Launching without argument check.");
						okToCheck = false;
					} else {
						ILOG("Storage permission granted.");
					}
				}
				if (okToLoad) {
					boot_filename = argv[i];
#ifdef _WIN32
					boot_filename = ReplaceAll(boot_filename, "\\", "/");
#endif
					skipLogo = true;
				}
				if (okToLoad && okToCheck) {
					std::unique_ptr<FileLoader> fileLoader(ConstructFileLoader(boot_filename));
					if (!fileLoader->Exists()) {
						fprintf(stderr, "File not found: %s\n", boot_filename.c_str());
#ifdef _WIN32
						// Ignore and proceed.
#else
						// Bail.
						exit(1);
#endif
					}
				}
			} else {
				fprintf(stderr, "Can only boot one file");
#ifdef _WIN32
				// Ignore and proceed.
#else
				// Bail.
				exit(1);
#endif
			}
		}
	}

	if (fileToLog)
		LogManager::GetInstance()->ChangeFileLog(fileToLog);

	PostLoadConfig();

	// Hard reset the logs. TODO: Get rid of this and read from config.
#ifndef _WIN32
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++) {
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		logman->SetEnabled(type, true);
		logman->SetLogLevel(type, logLevel);
	}
#endif

#if defined(__ANDROID__) || (defined(MOBILE_DEVICE) && !defined(_DEBUG))
	// Enable basic logging for any kind of mobile device, since LogManager doesn't.
	// The MOBILE_DEVICE/_DEBUG condition matches LogManager.cpp.
	logger = new AndroidLogger();
	logman->AddListener(logger);
#endif

	if (System_GetPropertyBool(SYSPROP_SUPPORTS_PERMISSIONS)) {
		if (System_GetPermissionStatus(SYSTEM_PERMISSION_STORAGE) != PERMISSION_STATUS_GRANTED) {
			System_AskForPermission(SYSTEM_PERMISSION_STORAGE);
		}
	}


	I18NCategory *des = GetI18NCategory("DesktopUI");
	// Note to translators: do not translate this/add this to PPSSPP-lang's files.
	// It's intended to be custom for every user.
	// Only add it to your own personal copies of PPSSPP.
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	// TODO: Could allow a setting to specify a font file to load?
	// TODO: Make this a constant if we can sanely load the font on other systems?
	AddFontResourceEx(L"assets/Roboto-Condensed.ttf", FR_PRIVATE, NULL);
	// The font goes by two names, let's allow either one.
	if (CheckFontIsUsable(L"Roboto Condensed")) {
		g_Config.sFont = des->T("Font", "Roboto Condensed");
	} else {
		g_Config.sFont = des->T("Font", "Roboto");
	}
#endif

	if (!boot_filename.empty() && stateToLoad != NULL) {
		SaveState::Load(stateToLoad, [](bool status, const std::string &message, void *) {
			if (!message.empty()) {
				osm.Show(message, 2.0);
			}
		});
	}

	screenManager = new ScreenManager();
	if (skipLogo) {
		screenManager->switchScreen(new EmuScreen(boot_filename));
	} else {
		screenManager->switchScreen(new LogoScreen());
	}

	if (g_Config.bRemoteShareOnStartup) {
		StartRemoteISOSharing();
	}

	std::string sysName = System_GetProperty(SYSPROP_NAME);
	isOuya = KeyMap::IsOuya(sysName);

#if !defined(MOBILE_DEVICE) && defined(USING_QT_UI)
	MainWindow* mainWindow = new MainWindow(0,fs);
	mainWindow->show();
	if (host == nullptr) {
		host = new QtHost(mainWindow);
	}
#endif

	// We do this here, instead of in NativeInitGraphics, because the display may be reset.
	// When it's reset we don't want to forget all our managed things.
	SetGPUBackend((GPUBackend) g_Config.iGPUBackend);

	// Must be done restarting by now.
	restarting = false;
}

static UI::Style MakeStyle(uint32_t fg, uint32_t bg) {
	UI::Style s;
	s.background = UI::Drawable(bg);
	s.fgColor = fg;

	return s;
}

static void UIThemeInit() {
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	ui_theme.uiFont = UI::FontStyle(UBUNTU24, g_Config.sFont.c_str(), 22);
	ui_theme.uiFontSmall = UI::FontStyle(UBUNTU24, g_Config.sFont.c_str(), 15);
	ui_theme.uiFontSmaller = UI::FontStyle(UBUNTU24, g_Config.sFont.c_str(), 12);
#else
	ui_theme.uiFont = UI::FontStyle(UBUNTU24, "", 20);
	ui_theme.uiFontSmall = UI::FontStyle(UBUNTU24, "", 14);
	ui_theme.uiFontSmaller = UI::FontStyle(UBUNTU24, "", 11);
#endif

	ui_theme.checkOn = I_CHECKEDBOX;
	ui_theme.checkOff = I_SQUARE;
	ui_theme.whiteImage = I_SOLIDWHITE;
	ui_theme.sliderKnob = I_CIRCLE;
	ui_theme.dropShadow4Grid = I_DROP_SHADOW;

	ui_theme.itemStyle = MakeStyle(g_Config.uItemStyleFg, g_Config.uItemStyleBg);
	ui_theme.itemFocusedStyle = MakeStyle(g_Config.uItemFocusedStyleFg, g_Config.uItemFocusedStyleBg);
	ui_theme.itemDownStyle = MakeStyle(g_Config.uItemDownStyleFg, g_Config.uItemDownStyleBg);
	ui_theme.itemDisabledStyle = MakeStyle(g_Config.uItemDisabledStyleFg, g_Config.uItemDisabledStyleBg);
	ui_theme.itemHighlightedStyle = MakeStyle(g_Config.uItemHighlightedStyleFg, g_Config.uItemHighlightedStyleBg);

	ui_theme.buttonStyle = MakeStyle(g_Config.uButtonStyleFg, g_Config.uButtonStyleBg);
	ui_theme.buttonFocusedStyle = MakeStyle(g_Config.uButtonFocusedStyleFg, g_Config.uButtonFocusedStyleBg);
	ui_theme.buttonDownStyle = MakeStyle(g_Config.uButtonDownStyleFg, g_Config.uButtonDownStyleBg);
	ui_theme.buttonDisabledStyle = MakeStyle(g_Config.uButtonDisabledStyleFg, g_Config.uButtonDisabledStyleBg);
	ui_theme.buttonHighlightedStyle = MakeStyle(g_Config.uButtonHighlightedStyleFg, g_Config.uButtonHighlightedStyleBg);

	ui_theme.headerStyle.fgColor = g_Config.uHeaderStyleFg;
	ui_theme.infoStyle = MakeStyle(g_Config.uInfoStyleFg, g_Config.uInfoStyleBg);

	ui_theme.popupTitle.fgColor = g_Config.uPopupTitleStyleFg;
	ui_theme.popupStyle = MakeStyle(g_Config.uPopupStyleFg, g_Config.uPopupStyleBg);
}

void RenderOverlays(UIContext *dc, void *userdata);

bool NativeInitGraphics(GraphicsContext *graphicsContext) {
	ILOG("NativeInitGraphics");
	_assert_msg_(G3D, graphicsContext, "No graphics context!");

	using namespace Draw;
	Core_SetGraphicsContext(graphicsContext);
	g_draw = graphicsContext->GetDrawContext();
	_assert_msg_(G3D, g_draw, "No draw context available!");

	ui_draw2d.SetAtlas(&ui_atlas);
	ui_draw2d_front.SetAtlas(&ui_atlas);

	UIThemeInit();

	uiContext = new UIContext();
	uiContext->theme = &ui_theme;

	Draw::InputLayout *inputLayout = ui_draw2d.CreateInputLayout(g_draw);
	Draw::BlendState *blendNormal = g_draw->CreateBlendState({ true, 0xF, BlendFactor::SRC_ALPHA, BlendFactor::ONE_MINUS_SRC_ALPHA });
	Draw::DepthStencilState *depth = g_draw->CreateDepthStencilState({ false, false, Comparison::LESS });
	Draw::RasterState *rasterNoCull = g_draw->CreateRasterState({});

	PipelineDesc colorDesc{
		Primitive::TRIANGLE_LIST,
		{ g_draw->GetVshaderPreset(VS_COLOR_2D), g_draw->GetFshaderPreset(FS_COLOR_2D) },
		inputLayout, depth, blendNormal, rasterNoCull, &vsColBufDesc,
	};
	PipelineDesc texColorDesc{
		Primitive::TRIANGLE_LIST,
		{ g_draw->GetVshaderPreset(VS_TEXTURE_COLOR_2D), g_draw->GetFshaderPreset(FS_TEXTURE_COLOR_2D) },
		inputLayout, depth, blendNormal, rasterNoCull, &vsTexColBufDesc,
	};

	colorPipeline = g_draw->CreateGraphicsPipeline(colorDesc);
	texColorPipeline = g_draw->CreateGraphicsPipeline(texColorDesc);

	_assert_(colorPipeline);
	_assert_(texColorPipeline);

	// Release these now, reference counting should ensure that they get completely released
	// once we delete both pipelines.
	inputLayout->Release();
	rasterNoCull->Release();
	blendNormal->Release();
	depth->Release();

	ui_draw2d.Init(g_draw, texColorPipeline);
	ui_draw2d_front.Init(g_draw, texColorPipeline);

	uiContext->Init(g_draw, texColorPipeline, colorPipeline, &ui_draw2d, &ui_draw2d_front);
	RasterStateDesc desc;
	desc.cull = CullMode::NONE;
	desc.frontFace = Facing::CCW;

	if (uiContext->Text())
		uiContext->Text()->SetFont("Tahoma", 20, 0);

	screenManager->setUIContext(uiContext);
	screenManager->setDrawContext(g_draw);
	screenManager->setPostRenderCallback(&RenderOverlays, nullptr);
	screenManager->deviceRestored();

#ifdef _WIN32
	winAudioBackend = CreateAudioBackend((AudioBackendType)g_Config.iAudioBackend);
#if PPSSPP_PLATFORM(UWP)
	winAudioBackend->Init(0, &Win32Mix, 44100);
#else
	winAudioBackend->Init(MainWindow::GetHWND(), &Win32Mix, 44100);
#endif
#endif

	g_gameInfoCache = new GameInfoCache();

	if (gpu)
		gpu->DeviceRestore();

	g_graphicsInited = true;
	ILOG("NativeInitGraphics completed");
	return true;
}

void NativeShutdownGraphics() {
	screenManager->deviceLost();

	if (gpu)
		gpu->DeviceLost();

	g_graphicsInited = false;
	ILOG("NativeShutdownGraphics");

#ifdef _WIN32
	delete winAudioBackend;
	winAudioBackend = nullptr;
#endif

	delete g_gameInfoCache;
	g_gameInfoCache = nullptr;

	UIBackgroundShutdown();

	delete uiContext;
	uiContext = nullptr;

	ui_draw2d.Shutdown();
	ui_draw2d_front.Shutdown();

	if (colorPipeline) {
		colorPipeline->Release();
		colorPipeline = nullptr;
	}
	if (texColorPipeline) {
		texColorPipeline->Release();
		texColorPipeline = nullptr;
	}

	ILOG("NativeShutdownGraphics done");
}

void TakeScreenshot() {
	g_TakeScreenshot = false;

#if defined(_WIN32) || (defined(USING_QT_UI) && !defined(MOBILE_DEVICE))
	std::string path = GetSysDirectory(DIRECTORY_SCREENSHOT);
	while (path.length() > 0 && path.back() == '/') {
		path.resize(path.size() - 1);
	}
	if (!File::Exists(path)) {
		File::CreateDir(path);
	}

	// First, find a free filename.
	int i = 0;

	std::string gameId = g_paramSFO.GetDiscID();

	char filename[2048];
	while (i < 10000){
		if (g_Config.bScreenshotsAsPNG)
			snprintf(filename, sizeof(filename), "%s/%s_%05d.png", path.c_str(), gameId.c_str(), i);
		else
			snprintf(filename, sizeof(filename), "%s/%s_%05d.jpg", path.c_str(), gameId.c_str(), i);
		FileInfo info;
		if (!getFileInfo(filename, &info))
			break;
		i++;
	}

	bool success = TakeGameScreenshot(filename, g_Config.bScreenshotsAsPNG ? ScreenshotFormat::PNG : ScreenshotFormat::JPG, SCREENSHOT_OUTPUT);
	if (success) {
		osm.Show(filename);
	} else {
		I18NCategory *err = GetI18NCategory("Error");
		osm.Show(err->T("Could not save screenshot file"));
	}
#endif
}

void RenderOverlays(UIContext *dc, void *userdata) {
	// Thin bar at the top of the screen like Chrome.
	std::vector<float> progress = g_DownloadManager.GetCurrentProgress();
	if (!progress.empty()) {
		static const uint32_t colors[4] = {
			0xFFFFFFFF,
			0xFFCCCCCC,
			0xFFAAAAAA,
			0xFF777777,
		};

		dc->Begin();
		int h = 5;
		for (size_t i = 0; i < progress.size(); i++) {
			float barWidth = 10 + (dc->GetBounds().w - 10) * progress[i];
			Bounds bounds(0, h * i, barWidth, h);
			UI::Drawable solid(colors[i & 3]);
			dc->FillRect(solid, bounds);
		}
		dc->End();
		dc->Flush();
	}

	if (g_TakeScreenshot) {
		TakeScreenshot();
	}
}

void NativeRender(GraphicsContext *graphicsContext) {
	g_GameManager.Update();

	float xres = dp_xres;
	float yres = dp_yres;

	// Apply the UIContext bounds as a 2D transformation matrix.
	// TODO: This should be moved into the draw context...
	Matrix4x4 ortho;
	switch (GetGPUBackend()) {
	case GPUBackend::VULKAN:
		ortho.setOrthoD3D(0.0f, xres, 0, yres, -1.0f, 1.0f);
		break;
	case GPUBackend::DIRECT3D9:
		ortho.setOrthoD3D(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
		Matrix4x4 translation;
		// Account for the small window adjustment.
		translation.setTranslation(Vec3(-0.5f * g_dpi_scale_x / g_dpi_scale_real_x, -0.5f * g_dpi_scale_y / g_dpi_scale_real_y, 0.0f));
		ortho = translation * ortho;
		break;
	case GPUBackend::DIRECT3D11:
		ortho.setOrthoD3D(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
		break;
	case GPUBackend::OPENGL:
		ortho.setOrtho(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
		break;
	}

	if (g_display_rotation != DisplayRotation::ROTATE_0) {
		ortho = ortho * g_display_rot_matrix;
	}

	ui_draw2d.PushDrawMatrix(ortho);
	ui_draw2d_front.PushDrawMatrix(ortho);

	// All actual rendering happen in here.
	screenManager->render();
	if (screenManager->getUIContext()->Text()) {
		screenManager->getUIContext()->Text()->OncePerFrame();
	}

	if (resized) {
		resized = false;

		if (uiContext) {
			// Modifying the bounds here can be used to "inset" the whole image to gain borders for TV overscan etc.
			// The UI now supports any offset but not the EmuScreen yet.
			uiContext->SetBounds(Bounds(0, 0, dp_xres, dp_yres));
			// uiContext->SetBounds(Bounds(dp_xres/2, 0, dp_xres / 2, dp_yres / 2));


			// OSX 10.6 and SDL 1.2 bug.
#if defined(__APPLE__) && !defined(USING_QT_UI)
			static int dp_xres_old = dp_xres;
			if (dp_xres != dp_xres_old) {
				// uiTexture->Load("ui_atlas.zim");
				dp_xres_old = dp_xres;
			}
#endif
		}

		// Test lost/restore on PC
#if 0
		if (gpu) {
			gpu->DeviceLost();
			gpu->DeviceRestore();
		}
#endif

		graphicsContext->Resize();
		screenManager->resized();

		// TODO: Move this to new GraphicsContext objects for each backend.
#ifndef _WIN32
		if (GetGPUBackend() == GPUBackend::OPENGL) {
			PSP_CoreParameter().pixelWidth = pixel_xres;
			PSP_CoreParameter().pixelHeight = pixel_yres;
			NativeMessageReceived("gpu_resized", "");
		}
#endif
	}

	ui_draw2d.PopDrawMatrix();
	ui_draw2d_front.PopDrawMatrix();
}

void HandleGlobalMessage(const std::string &msg, const std::string &value) {
	if (msg == "inputDeviceConnected") {
		KeyMap::NotifyPadConnected(value);
	}
	if (msg == "inputbox_completed") {
		SplitString(value, ':', inputboxValue);
		std::string setString = inputboxValue.size() > 1 ? inputboxValue[1] : "";
		if (inputboxValue[0] == "IP")
			g_Config.proAdhocServer = setString;
		else if (inputboxValue[0] == "nickname")
			g_Config.sNickName = setString;
		else if (inputboxValue[0] == "remoteiso_subdir")
			g_Config.sRemoteISOSubdir = setString;
		else if (inputboxValue[0] == "remoteiso_server")
			g_Config.sLastRemoteISOServer = setString;
		inputboxValue.clear();
	}
	if (msg == "bgImage_updated") {
		if (!value.empty()) {
			std::string dest = GetSysDirectory(DIRECTORY_SYSTEM) + (endsWithNoCase(value, ".jpg") ? "background.jpg" : "background.png");
			File::Copy(value, dest);
		}
		UIBackgroundShutdown();
		UIBackgroundInit(*uiContext);
	}
	if (msg == "savestate_displayslot") {
		I18NCategory *sy = GetI18NCategory("System");
		std::string msg = StringFromFormat("%s: %d", sy->T("Savestate Slot"), SaveState::GetCurrentSlot() + 1);
		// Show for the same duration as the preview.
		osm.Show(msg, 2.0f, 0xFFFFFF, -1, true, "savestate_slot");
	}
	if (msg == "gpu_resized" || msg == "gpu_clearCache") {
		if (gpu) {
			gpu->ClearCacheNextFrame();
			gpu->Resized();
		}
		Reporting::UpdateConfig();
	}
	if (msg == "core_powerSaving") {
		if (value != "false") {
			I18NCategory *sy = GetI18NCategory("System");
#ifdef __ANDROID__
			osm.Show(sy->T("WARNING: Android battery save mode is on"), 2.0f, 0xFFFFFF, -1, true, "core_powerSaving");
#else
			osm.Show(sy->T("WARNING: Battery save mode is on"), 2.0f, 0xFFFFFF, -1, true, "core_powerSaving");
#endif
		}
		Core_SetPowerSaving(value != "false");
	}
	if (msg == "permission_granted" && value == "storage") {
#ifdef __ANDROID__
		CreateDirectoriesAndroid();
#endif
		// We must have failed to load the config before, so load it now to avoid overwriting the old config
		// with a freshly generated one.
		ILOG("Reloading config after storage permission grant.");
		g_Config.Load();
		PostLoadConfig();
	}
}

void NativeUpdate() {
	PROFILE_END_FRAME();

	std::vector<PendingMessage> toProcess;
	{
		std::lock_guard<std::mutex> lock(pendingMutex);
		toProcess = std::move(pendingMessages);
		pendingMessages.clear();
	}

	for (size_t i = 0; i < toProcess.size(); i++) {
		HandleGlobalMessage(toProcess[i].msg, toProcess[i].value);
		screenManager->sendMessage(toProcess[i].msg.c_str(), toProcess[i].value.c_str());
	}

	g_DownloadManager.Update();
	screenManager->update();
}

bool NativeIsAtTopLevel() {
	// This might need some synchronization?
	if (!screenManager) {
		ELOG("No screen manager active");
		return false;
	}
	Screen *currentScreen = screenManager->topScreen();
	if (currentScreen) {
		bool top = currentScreen->isTopLevel();
		ILOG("Screen toplevel: %i", (int)top);
		return currentScreen->isTopLevel();
	} else {
		ELOG("No current screen");
		return false;
	}
}

bool NativeTouch(const TouchInput &touch) {
	if (screenManager) {
		// Brute force prevent NaNs from getting into the UI system
		if (my_isnan(touch.x) || my_isnan(touch.y)) {
			return false;
		}
		screenManager->touch(touch);
		return true;
	} else {
		return false;
	}
}

bool NativeKey(const KeyInput &key) {
	// ILOG("Key code: %i flags: %i", key.keyCode, key.flags);
#if !defined(MOBILE_DEVICE)
	if (g_Config.bPauseExitsEmulator) {
		static std::vector<int> pspKeys;
		pspKeys.clear();
		if (KeyMap::KeyToPspButton(key.deviceId, key.keyCode, &pspKeys)) {
			if (std::find(pspKeys.begin(), pspKeys.end(), VIRTKEY_PAUSE) != pspKeys.end()) {
				System_SendMessage("finish", "");
				return true;
			}
		}
	}
#endif
	bool retval = false;
	if (screenManager)
		retval = screenManager->key(key);
	return retval;
}

bool NativeAxis(const AxisInput &axis) {
	using namespace TiltEventProcessor;

	// only handle tilt events if tilt is enabled.
	if (g_Config.iTiltInputType == TILT_NULL) {
		// if tilt events are disabled, then run it through the usual way. 
		if (screenManager) {
			return screenManager->axis(axis);
		} else {
			return false;
		}
	}

	// create the base coordinate tilt system from the calibration data.
	// This is static for no particular reason, can be un-static'ed
	static Tilt baseTilt;
	baseTilt.x_ = g_Config.fTiltBaseX;
	baseTilt.y_ = g_Config.fTiltBaseY;

	// figure out what the current tilt orientation is by checking the axis event
	// This is static, since we need to remember where we last were (in terms of orientation)
	static Tilt currentTilt;

	// x and y are flipped if we are in landscape orientation. The events are
	// sent with respect to the portrait coordinate system, while we
	// take all events in landscape.
	// see [http://developer.android.com/guide/topics/sensors/sensors_overview.html] for details
	bool portrait = dp_yres > dp_xres;
	switch (axis.axisId) {
		case JOYSTICK_AXIS_ACCELEROMETER_X:
			if (portrait) {
				currentTilt.x_ = axis.value;
			} else {
				currentTilt.y_ = axis.value;
			}
			break;

		case JOYSTICK_AXIS_ACCELEROMETER_Y:
			if (portrait) {
				currentTilt.y_ = axis.value;
			} else {
				currentTilt.x_ = axis.value;
			}
			break;

		case JOYSTICK_AXIS_ACCELEROMETER_Z:
			//don't handle this now as only landscape is enabled.
			//TODO: make this generic.
			return false;
			
		case JOYSTICK_AXIS_OUYA_UNKNOWN1:
		case JOYSTICK_AXIS_OUYA_UNKNOWN2:
		case JOYSTICK_AXIS_OUYA_UNKNOWN3:
		case JOYSTICK_AXIS_OUYA_UNKNOWN4:
			//Don't know how to handle these. Someone should figure it out.
			//Does the Ouya even have an accelerometer / gyro? I can't find any reference to these
			//in the Ouya docs...
			return false;

		default:
			// Don't take over completely!
			return screenManager->axis(axis);
	}

	//figure out the sensitivity of the tilt. (sensitivity is originally 0 - 100)
	//We divide by 50, so that the rest of the 50 units can be used to overshoot the
	//target. If you want control, you'd keep the sensitivity ~50.
	//For games that don't need much control but need fast reactions,
	//then a value of 70-80 is the way to go.
	float xSensitivity = g_Config.iTiltSensitivityX / 50.0;
	float ySensitivity = g_Config.iTiltSensitivityY / 50.0;
	
	//now transform out current tilt to the calibrated coordinate system
	Tilt trueTilt = GenTilt(baseTilt, currentTilt, g_Config.bInvertTiltX, g_Config.bInvertTiltY, g_Config.fDeadzoneRadius, xSensitivity, ySensitivity);

	TranslateTiltToInput(trueTilt);
	return true;
}

void NativeMessageReceived(const char *message, const char *value) {
	// We can only have one message queued.
	std::lock_guard<std::mutex> lock(pendingMutex);
	PendingMessage pendingMessage;
	pendingMessage.msg = message;
	pendingMessage.value = value;
	pendingMessages.push_back(pendingMessage);
}

void NativeResized() {
	// NativeResized can come from any thread so we just set a flag, then process it later.
	if (g_graphicsInited) {
		resized = true;
	} else {
		ILOG("NativeResized ignored, not initialized");
	}
}

void NativeSetRestarting() {
	restarting = true;
}

bool NativeIsRestarting() {
	return restarting;
}

void NativeShutdown() {
	if (screenManager)
		screenManager->shutdown();
	delete screenManager;
	screenManager = nullptr;

	host->ShutdownGraphics();

#if !PPSSPP_PLATFORM(UWP)
	delete host;
	host = nullptr;
#endif
	g_Config.Save();

	// Avoid shutting this down when restarting core.
	if (!restarting)
		LogManager::Shutdown();

#ifdef ANDROID_NDK_PROFILER
	moncleanup();
#endif

	ILOG("NativeShutdown called");

	System_SendMessage("finish", "");

	net::Shutdown();

	delete logger;
	logger = nullptr;

	// Previously we did exit() here on Android but that makes it hard to do things like restart on backend change.
	// I think we handle most globals correctly or correct-enough now.
}

void PushNewGpsData(float latitude, float longitude, float altitude, float speed, float bearing, long long time) {
	GPS::setGpsData(latitude, longitude, altitude, speed, bearing, time);
}

void PushCameraImage(long long length, unsigned char* image) {
	Camera::pushCameraImage(length, image);
}

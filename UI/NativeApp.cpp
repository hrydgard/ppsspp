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


// Background worker threads should be spawned in NativeInit and joined
// in NativeShutdown.

#include <locale.h>
// Linux doesn't like using std::find with std::vector<int> without this :/
#if !defined(MOBILE_DEVICE)
#include <algorithm>
#endif
#include <memory>

#if defined(_WIN32)
#include "Windows/DSoundStream.h"
#include "Windows/WndMainWindow.h"
#include "Windows/D3D9Base.h"
#endif

#include "base/display.h"
#include "base/logging.h"
#include "base/mutex.h"
#include "base/NativeApp.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "thread/thread.h"
#include "net/http_client.h"
#include "gfx_es2/gl_state.h"  // TODO: Get rid of this from here
#include "gfx_es2/draw_text.h"
#include "gfx/gl_lost_manager.h"
#include "gfx/texture.h"
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
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/SaveState.h"
#include "Core/Screenshot.h"
#include "Core/System.h"
#include "Core/HLE/__sceAudio.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Util/GameManager.h"
#include "Core/Util/AudioFormat.h"

#include "ui_atlas.h"
#include "EmuScreen.h"
#include "GameInfoCache.h"
#include "HostTypes.h"
#ifdef _WIN32
#include "GPU/Directx9/helper/dx_state.h"
#endif
#include "UI/OnScreenDisplay.h"
#include "UI/MiscScreens.h"
#include "UI/TiltEventProcessor.h"
#include "UI/BackgroundAudio.h"

#if !defined(MOBILE_DEVICE)
#include "Common/KeyMap.h"
#endif

#ifdef __SYMBIAN32__
#define unique_ptr auto_ptr
#endif

// The new UI framework, for initialization

static UI::Theme ui_theme;

#ifdef ARM
#include "../../android/jni/ArmEmitterTest.h"
#elif defined(ARM64)
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

Thin3DTexture *uiTexture;

ScreenManager *screenManager;
std::string config_filename;

#ifdef IOS
bool iosCanUseJit;
#endif

// Really need to clean this mess of globals up... but instead I add more :P
bool g_TakeScreenshot;
static bool isOuya;
static bool resized = false;

struct PendingMessage {
	std::string msg;
	std::string value;
};

static recursive_mutex pendingMutex;
static std::vector<PendingMessage> pendingMessages;
static Thin3DContext *thin3d;
static UIContext *uiContext;

#ifdef _WIN32
WindowsAudioBackend *winAudioBackend;
#endif

Thin3DContext *GetThin3D() {
	return thin3d;
}

std::thread *graphicsLoadThread;

class AndroidLogger : public LogListener {
public:
	void Log(LogTypes::LOG_LEVELS level, const char *msg) {
		switch (level) {
		case LogTypes::LVERBOSE:
		case LogTypes::LDEBUG:
		case LogTypes::LINFO:
			ILOG("%s", msg);
			break;
		case LogTypes::LERROR:
			ELOG("%s", msg);
			break;
		case LogTypes::LWARNING:
			WLOG("%s", msg);
			break;
		case LogTypes::LNOTICE:
		default:
			ILOG("%s", msg);
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
#ifndef _WIN32
static AndroidLogger *logger = 0;
#endif

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
		sprintf(temp, "%i", g_Config.iScreenRotation);
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
		sprintf(temp, "%i", std::min(scale, max_res));
		return std::string(temp);
	} else if (query == "force44khz") {
		return std::string("0");
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

#if defined(ARM) && defined(ANDROID)
	ArmEmitterTest();
#elif defined(ARM64) && defined(ANDROID)
	Arm64EmitterTest();
#endif
}

void NativeInit(int argc, const char *argv[],
								const char *savegame_directory, const char *external_directory, const char *installID, bool fs) {
#ifdef ANDROID_NDK_PROFILER
	setenv("CPUPROFILE_FREQUENCY", "500", 1);
	setenv("CPUPROFILE", "/sdcard/gmon.out", 1);
	monstartup("ppsspp_jni.so");
#endif

	InitFastMath(cpu_info.bNEON);
	SetupAudioFormats();

	// Sets both FZ and DefaultNaN on ARM, flipping some ARM implementations into "RunFast" mode for VFP.
	// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0274h/Babffifj.html
	// Do we need to do this on all threads?
	// Also, the FZ thing may actually be a little bit dangerous, I'm not sure how compliant the MIPS
	// CPU is with denormal handling. Needs testing. Default-NAN should be reasonably safe though.
	FPU_SetFastMode();

	bool skipLogo = false;
	setlocale( LC_ALL, "C" );
	std::string user_data_path = savegame_directory;
	pendingMessages.clear();
#ifdef IOS
	user_data_path += "/";
#endif

	// We want this to be FIRST.
#ifdef USING_QT_UI
	VFSRegister("", new AssetsAssetReader());
#elif defined(BLACKBERRY) || defined(IOS)
	// Packed assets are included in app
	VFSRegister("", new DirectoryAssetReader(external_directory));
#elif !defined(MOBILE_DEVICE) && !defined(_WIN32)
	VFSRegister("", new DirectoryAssetReader((File::GetExeDirectory() + "assets/").c_str()));
	VFSRegister("", new DirectoryAssetReader((File::GetExeDirectory()).c_str()));
	VFSRegister("", new DirectoryAssetReader("/usr/share/ppsspp/assets/"));
#else
	VFSRegister("", new DirectoryAssetReader("assets/"));
#endif
	VFSRegister("", new DirectoryAssetReader(savegame_directory));

#if defined(MOBILE_DEVICE) || !defined(USING_QT_UI)
	host = new NativeHost();
#endif

#if defined(ANDROID)
	g_Config.internalDataDirectory = savegame_directory;
	// Maybe there should be an option to use internal memory instead, but I think
	// that for most people, using external memory (SDCard/USB Storage) makes the
	// most sense.
	g_Config.memStickDirectory = std::string(external_directory) + "/";
	g_Config.flash0Directory = std::string(external_directory) + "/flash0/";
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__) || defined(MAEMO) || defined(IOS)
	g_Config.memStickDirectory = user_data_path;
	g_Config.flash0Directory = std::string(external_directory) + "/flash0/";
#elif !defined(_WIN32)
	std::string config;
	if (getenv("XDG_CONFIG_HOME") != NULL)
		config = getenv("XDG_CONFIG_HOME");
	else if (getenv("HOME") != NULL)
		config = getenv("HOME") + std::string("/.config");
	else // Just in case
		config = "./config";

	g_Config.memStickDirectory = config + "/ppsspp/";
	g_Config.flash0Directory = File::GetExeDirectory() + "/flash0/";
#endif

#ifndef _WIN32
	logger = new AndroidLogger();

	LogManager::Init();
	LogManager *logman = LogManager::GetInstance();

	g_Config.AddSearchPath(user_data_path);
	g_Config.AddSearchPath(g_Config.memStickDirectory + "PSP/SYSTEM/");
	g_Config.SetDefaultPath(g_Config.memStickDirectory + "PSP/SYSTEM/");
	g_Config.Load();
	g_Config.externalDirectory = external_directory;
#endif

#ifdef ANDROID
	// On Android, create a PSP directory tree in the external_directory,
	// to hopefully reduce confusion a bit.
	ILOG("Creating %s", (g_Config.memStickDirectory + "PSP").c_str());
	mkDir((g_Config.memStickDirectory + "PSP").c_str());
	mkDir((g_Config.memStickDirectory + "PSP/SAVEDATA").c_str());
	mkDir((g_Config.memStickDirectory + "PSP/GAME").c_str());
#endif

	const char *fileToLog = 0;
	const char *stateToLoad = 0;

	bool gfxLog = false;
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
			case 'g':
				gfxLog = true;
				break;
			case 'j':
				g_Config.bJit = true;
				g_Config.bSaveSettings = false;
				break;
			case 'i':
				g_Config.bJit = false;
				g_Config.bSaveSettings = false;
				break;
			case '-':
				if (!strncmp(argv[i], "--log=", strlen("--log=")) && strlen(argv[i]) > strlen("--log="))
					fileToLog = argv[i] + strlen("--log=");
				if (!strncmp(argv[i], "--state=", strlen("--state=")) && strlen(argv[i]) > strlen("--state="))
					stateToLoad = argv[i] + strlen("--state=");
#if !defined(MOBILE_DEVICE)
				if (!strncmp(argv[i], "--escape-exit", strlen("--escape-exit")))
					g_Config.bPauseExitsEmulator = true;
#endif
				break;
			}
		} else {
			if (boot_filename.empty()) {
				boot_filename = argv[i];
				skipLogo = true;

				std::unique_ptr<FileLoader> fileLoader(ConstructFileLoader(boot_filename));
				if (!fileLoader->Exists()) {
					fprintf(stderr, "File not found: %s\n", boot_filename.c_str());
					exit(1);
				}
			} else {
				fprintf(stderr, "Can only boot one file");
				exit(1);
			}
		}
	}

	if (fileToLog != NULL)
		LogManager::GetInstance()->ChangeFileLog(fileToLog);

#ifndef _WIN32
	if (g_Config.currentDirectory == "") {
#if defined(ANDROID)
		g_Config.currentDirectory = external_directory;
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__) || defined(MAEMO) || defined(IOS) || defined(_WIN32)
		g_Config.currentDirectory = savegame_directory;
#else
		if (getenv("HOME") != NULL)
			g_Config.currentDirectory = getenv("HOME");
		else
			g_Config.currentDirectory = "./";
#endif
	}

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		logman->SetEnable(type, true);
		logman->SetLogLevel(type, gfxLog && i == LogTypes::G3D ? LogTypes::LDEBUG : logLevel);
#ifdef ANDROID
		logman->AddListener(type, logger);
#endif
	}
	// Special hack for G3D as it's very spammy. Need to make a flag for this.
	if (!gfxLog)
		logman->SetLogLevel(LogTypes::G3D, LogTypes::LERROR);
#endif
	// Allow the lang directory to be overridden for testing purposes (e.g. Android, where it's hard to 
	// test new languages without recompiling the entire app, which is a hassle).
	const std::string langOverridePath = g_Config.memStickDirectory + "PSP/SYSTEM/lang/";

	// If we run into the unlikely case that "lang" is actually a file, just use the built-in translations.
	if (!File::Exists(langOverridePath) || !File::IsDirectory(langOverridePath))
		i18nrepo.LoadIni(g_Config.sLanguageIni);
	else
		i18nrepo.LoadIni(g_Config.sLanguageIni, langOverridePath);

	I18NCategory *d = GetI18NCategory("DesktopUI");
	// Note to translators: do not translate this/add this to PPSSPP-lang's files.
	// It's intended to be custom for every user.
	// Only add it to your own personal copies of PPSSPP.
#ifdef _WIN32
	// TODO: Could allow a setting to specify a font file to load?
	// TODO: Make this a constant if we can sanely load the font on other systems?
	AddFontResourceEx(L"assets/Roboto-Condensed.ttf", FR_PRIVATE, NULL);
	g_Config.sFont = d->T("Font", "Roboto");
#endif

	if (!boot_filename.empty() && stateToLoad != NULL)
		SaveState::Load(stateToLoad);

	g_gameInfoCache.Init();

	screenManager = new ScreenManager();
	if (skipLogo) {
		screenManager->switchScreen(new EmuScreen(boot_filename));
	} else {
		screenManager->switchScreen(new LogoScreen());
	}

	std::string sysName = System_GetProperty(SYSPROP_NAME);
	isOuya = KeyMap::IsOuya(sysName);

#if !defined(MOBILE_DEVICE) && defined(USING_QT_UI)
	MainWindow* mainWindow = new MainWindow(0,fs);
	mainWindow->show();
	host = new QtHost(mainWindow);
#endif

	// We do this here, instead of in NativeInitGraphics, because the display may be reset.
	// When it's reset we don't want to forget all our managed things.
	if (g_Config.iGPUBackend == GPU_BACKEND_OPENGL) {
		gl_lost_manager_init();
	}
}

void NativeInitGraphics() {
	FPU_SetFastMode();

#ifndef _WIN32
	// Force backend to GL
	g_Config.iGPUBackend = GPU_BACKEND_OPENGL;
#endif

	if (g_Config.iGPUBackend == GPU_BACKEND_OPENGL) {
		thin3d = T3DCreateGLContext();
		CheckGLExtensions();
	} else {
#ifdef _WIN32
		thin3d = D3D9_CreateThin3DContext();
#endif
	}

	ui_draw2d.SetAtlas(&ui_atlas);
	ui_draw2d_front.SetAtlas(&ui_atlas);

	// memset(&ui_theme, 0, sizeof(ui_theme));
	// New style theme
#ifdef _WIN32
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

	ui_theme.itemStyle.background = UI::Drawable(0x55000000);
	ui_theme.itemStyle.fgColor = 0xFFFFFFFF;
	ui_theme.itemFocusedStyle.background = UI::Drawable(0xFFedc24c);
	ui_theme.itemDownStyle.background = UI::Drawable(0xFFbd9939);
	ui_theme.itemDownStyle.fgColor = 0xFFFFFFFF;
	ui_theme.itemDisabledStyle.background = UI::Drawable(0x55E0D4AF);
	ui_theme.itemDisabledStyle.fgColor = 0x80EEEEEE;
	ui_theme.itemHighlightedStyle.background = UI::Drawable(0x55bdBB39);
	ui_theme.itemHighlightedStyle.fgColor = 0xFFFFFFFF;

	ui_theme.buttonStyle = ui_theme.itemStyle;
	ui_theme.buttonFocusedStyle = ui_theme.itemFocusedStyle;
	ui_theme.buttonDownStyle = ui_theme.itemDownStyle;
	ui_theme.buttonDisabledStyle = ui_theme.itemDisabledStyle;
	ui_theme.buttonHighlightedStyle = ui_theme.itemHighlightedStyle;

	ui_theme.popupTitle.fgColor = 0xFFE3BE59;

#ifdef GOLD
	ui_theme.itemFocusedStyle.background = UI::Drawable(0xFF4cc2ed);
	ui_theme.itemDownStyle.background = UI::Drawable(0xFF39a9ee);
	ui_theme.itemDisabledStyle.background = UI::Drawable(0x55AFD4E0);
	ui_theme.itemHighlightedStyle.background = UI::Drawable(0x5539BBbd);

	ui_theme.popupTitle.fgColor = 0xFF59BEE3;
#endif

	ui_draw2d.Init(thin3d);
	ui_draw2d_front.Init(thin3d);

#ifdef USING_QT_UI
	uiTexture = thin3d->CreateTextureFromFile("ui_atlas_lowmem.zim", T3DImageType::ZIM);
	if (!uiTexture) {
#else
	uiTexture = thin3d->CreateTextureFromFile("ui_atlas.zim", T3DImageType::ZIM);
	if (!uiTexture) {
#endif
		PanicAlert("Failed to load ui_atlas.zim.\n\nPlace it in the directory \"assets\" under your PPSSPP directory.");
		ELOG("Failed to load ui_atlas.zim");
#ifdef _WIN32
		UINT ExitCode = 0;
		ExitProcess(ExitCode);
#endif
	}

	uiContext = new UIContext();
	uiContext->theme = &ui_theme;

	uiContext->Init(thin3d, thin3d->GetShaderSetPreset(SS_TEXTURE_COLOR_2D), thin3d->GetShaderSetPreset(SS_COLOR_2D), uiTexture, &ui_draw2d, &ui_draw2d_front);
	if (uiContext->Text())
		uiContext->Text()->SetFont("Tahoma", 20, 0);

	screenManager->setUIContext(uiContext);
	screenManager->setThin3DContext(thin3d);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

#ifdef _WIN32
	winAudioBackend = CreateAudioBackend((AudioBackendType)g_Config.iAudioBackend);
	winAudioBackend->Init(MainWindow::GetHWND(), &Win32Mix, 44100);
#endif
}

void NativeShutdownGraphics() {
#ifdef _WIN32
	delete winAudioBackend;
	winAudioBackend = NULL;
#endif

	screenManager->deviceLost();

	g_gameInfoCache.Clear();

	uiTexture->Release();

	delete uiContext;
	uiContext = NULL;

	ui_draw2d.Shutdown();
	ui_draw2d_front.Shutdown();

	thin3d->Release();
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

	std::string gameId = g_paramSFO.GetValueString("DISC_ID");
	if (gameId.empty()) {
		gameId = "MENU";
	}

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

	bool success = TakeGameScreenshot(filename, g_Config.bScreenshotsAsPNG ? SCREENSHOT_PNG : SCREENSHOT_JPG, SCREENSHOT_DISPLAY);
	if (success) {
		osm.Show(filename);
	} else {
		I18NCategory *err = GetI18NCategory("Error");
		osm.Show(err->T("Could not save screenshot file"));
	}
#endif
}

void DrawDownloadsOverlay(UIContext &dc) {
	// Thin bar at the top of the screen like Chrome.
	std::vector<float> progress = g_DownloadManager.GetCurrentProgress();
	if (progress.empty()) {
		return;
	}

	static const uint32_t colors[4] = {
		0xFFFFFFFF,
		0xFFCCCCCC,
		0xFFAAAAAA,
		0xFF777777,
	};

	dc.Begin();
	int h = 5;
	for (size_t i = 0; i < progress.size(); i++) {
		float barWidth = 10 + (dc.GetBounds().w - 10) * progress[i];
		Bounds bounds(0, h * i, barWidth, h);
		UI::Drawable solid(colors[i & 3]);
		dc.FillRect(solid, bounds);
	}
	dc.End();
	dc.Flush();
}

void NativeRender() {
	g_GameManager.Update();

	thin3d->Clear(T3DClear::COLOR | T3DClear::DEPTH | T3DClear::STENCIL, 0xFF000000, 0.0f, 0);

	T3DViewport viewport;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = pixel_xres;
	viewport.Height = pixel_yres;
	viewport.MaxDepth = 1.0;
	viewport.MinDepth = 0.0;
	thin3d->SetViewports(1, &viewport);

	if (g_Config.iGPUBackend == GPU_BACKEND_OPENGL) {
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glstate.Restore();
	} else {
#ifdef _WIN32
		DX9::dxstate.depthWrite.set(true);
		DX9::dxstate.colorMask.set(true, true, true, true);
		DX9::dxstate.Restore();
#endif
	}

	thin3d->SetTargetSize(pixel_xres, pixel_yres);

	float xres = dp_xres;
	float yres = dp_yres;

	// Apply the UIContext bounds as a 2D transformation matrix.
	Matrix4x4 ortho;
	if (g_Config.iGPUBackend == GPU_BACKEND_DIRECT3D9) {
		ortho.setOrthoD3D(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
		Matrix4x4 translation;
		translation.setTranslation(Vec3(-0.5f, -0.5f, 0.0f));
		ortho = translation * ortho;
	} else {
		ortho.setOrtho(0.0f, xres, yres, 0.0f, -1.0f, 1.0f);
	}

	ui_draw2d.SetDrawMatrix(ortho);
	ui_draw2d_front.SetDrawMatrix(ortho);

	screenManager->render();
	if (screenManager->getUIContext()->Text()) {
		screenManager->getUIContext()->Text()->OncePerFrame();
	}

	DrawDownloadsOverlay(*screenManager->getUIContext());

	if (g_TakeScreenshot) {
		TakeScreenshot();
	}

	thin3d->SetScissorEnabled(false);
	if (g_Config.iGPUBackend == GPU_BACKEND_OPENGL) {
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	} else {
#ifdef _WIN32
		DX9::dxstate.depthWrite.set(true);
		DX9::dxstate.colorMask.set(true, true, true, true);
#endif
	}

	if (resized) {
		resized = false;
		if (g_Config.iGPUBackend == GPU_BACKEND_DIRECT3D9) {
#ifdef _WIN32
			D3D9_Resize(0);
#endif
		}
	}
}

void HandleGlobalMessage(const std::string &msg, const std::string &value) {
	if (msg == "inputDeviceConnected") {
		KeyMap::NotifyPadConnected(value);
	}
}

void NativeUpdate(InputState &input) {
	PROFILE_END_FRAME();

	{
		lock_guard lock(pendingMutex);
		for (size_t i = 0; i < pendingMessages.size(); i++) {
			HandleGlobalMessage(pendingMessages[i].msg, pendingMessages[i].value);
			screenManager->sendMessage(pendingMessages[i].msg.c_str(), pendingMessages[i].value.c_str());
		}
		pendingMessages.clear();
	}

	g_DownloadManager.Update();
	screenManager->update(input);
}

void NativeDeviceLost() {
	g_gameInfoCache.Clear();
	screenManager->deviceLost();

	if (g_Config.iGPUBackend == GPU_BACKEND_OPENGL) {
		gl_lost();
		glstate.Restore();
	}
	// Should dirty EVERYTHING
}

bool NativeIsAtTopLevel() {
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
	g_buttonTracker.Process(key);
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

	//now send the appropriate tilt event
	switch (g_Config.iTiltInputType) {
		case TILT_ANALOG:
			GenerateAnalogStickEvent(trueTilt);
			break;
		
		case TILT_DPAD:
			GenerateDPadEvent(trueTilt);
			break;
		
		case TILT_ACTION_BUTTON:
			GenerateActionButtonEvent(trueTilt);
			break;
	}
	return true;
}

void NativeMessageReceived(const char *message, const char *value) {
	// We can only have one message queued.
	lock_guard lock(pendingMutex);
	PendingMessage pendingMessage;
	pendingMessage.msg = message;
	pendingMessage.value = value;
	pendingMessages.push_back(pendingMessage);
}

void NativeResized() {
	resized = true;

	if (uiContext) {
		// Modifying the bounds here can be used to "inset" the whole image to gain borders for TV overscan etc.
		// The UI now supports any offset but not the EmuScreen yet.
		uiContext->SetBounds(Bounds(0, 0, dp_xres, dp_yres));
		// uiContext->SetBounds(Bounds(dp_xres/2, 0, dp_xres / 2, dp_yres / 2));


// OSX 10.6 and SDL 1.2 bug.
#if defined(__APPLE__) && !defined(USING_QT_UI)
		static int dp_xres_old=dp_xres;
		if (dp_xres != dp_xres_old) {
			// uiTexture->Load("ui_atlas.zim");
			dp_xres_old = dp_xres;
		}
#endif
	}
}

void NativeShutdown() {
	if (g_Config.iGPUBackend == GPU_BACKEND_OPENGL) {
		gl_lost_manager_shutdown();
	}

	screenManager->shutdown();
	delete screenManager;
	screenManager = 0;

	g_gameInfoCache.Shutdown();

	delete host;
	host = 0;
	g_Config.Save();
#ifndef _WIN32
	LogManager::Shutdown();
#endif
#ifdef ANDROID_NDK_PROFILER
	moncleanup();
#endif

	ILOG("NativeShutdown called");

	System_SendMessage("finish", "");
	// This means that the activity has been completely destroyed. PPSSPP does not
	// boot up correctly with "dirty" global variables currently, so we hack around that
	// by simply exiting.
#ifdef ANDROID
	exit(0);
#endif

#ifdef _WIN32
	RemoveFontResourceEx(L"assets/Roboto-Condensed.ttf", FR_PRIVATE, NULL);
#endif
}

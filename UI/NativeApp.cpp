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

#include "base/logging.h"
#include "base/mutex.h"
#include "base/NativeApp.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "ext/stb_image_write/stb_image_writer.h"
#include "ext/jpge/jpge.h"
#include "thread/thread.h"
#include "net/http_client.h"
#include "gfx_es2/gl_state.h"
#include "gfx_es2/draw_text.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx/gl_lost_manager.h"
#include "gfx/texture.h"
#include "i18n/i18n.h"
#include "input/input_state.h"
#include "math/math_util.h"
#include "math/lin/matrix4x4.h"
#include "ui/ui.h"
#include "ui/screen.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "util/text/utf8.h"

#include "Common/FileUtil.h"
#include "Common/LogManager.h"
#include "Core/PSPMixer.h"
#include "Core/CPU.h"
#include "Core/Config.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Host.h"
#include "Core/SaveState.h"
#include "Core/Util/GameManager.h"
#include "Common/MemArena.h"

#include "ui_atlas.h"
#include "EmuScreen.h"
#include "GameInfoCache.h"
#include "UIShader.h"

#include "UI/OnScreenDisplay.h"
#include "UI/MiscScreens.h"

// The new UI framework, for initialization

static UI::Theme ui_theme;

#ifdef ARM
#include "../../android/jni/ArmEmitterTest.h"
#endif

#if defined(__APPLE__) && !defined(IOS)
#include <mach-o/dyld.h>
#endif

#ifdef IOS
#include "ios/iOSCoreAudio.h"
#endif

// https://github.com/richq/android-ndk-profiler
#ifdef ANDROID_NDK_PROFILER
#include <stdlib.h>
#include "android/android-ndk-profiler/prof.h"
#endif

Texture *uiTexture;

ScreenManager *screenManager;
std::string config_filename;
std::string game_title;

#ifdef IOS
bool iosCanUseJit;
#endif

// Really need to clean this mess of globals up... but instead I add more :P
bool g_TakeScreenshot;
static bool isOuya;
recursive_mutex pendingMutex;
static bool isMessagePending;
static std::string pendingMessage;
static std::string pendingValue;
static UIContext *uiContext;

std::thread *graphicsLoadThread;

class AndroidLogger : public LogListener
{
public:
	void Log(LogTypes::LOG_LEVELS level, const char *msg)
	{
		switch (level)
		{
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


// TODO: Get rid of this junk
class NativeHost : public Host
{
public:
	NativeHost() {
		// hasRendered = false;
	}

	virtual void UpdateUI() {}

	virtual void UpdateMemView() {}
	virtual void UpdateDisassembly() {}

	virtual void SetDebugMode(bool mode) { }

	virtual bool InitGL(std::string *error_message) { return true; }
	virtual void ShutdownGL() {}

	virtual void InitSound(PMixer *mixer);
	virtual void UpdateSound() {}
	virtual void ShutdownSound();

	// this is sent from EMU thread! Make sure that Host handles it properly!
	virtual void BootDone() {}

	virtual bool IsDebuggingEnabled() {return false;}
	virtual bool AttemptLoadSymbolMap() {return false;}
	virtual void ResetSymbolMap() {}
	virtual void AddSymbol(std::string name, u32 addr, u32 size, int type=0) {}
	virtual void SetWindowTitle(const char *message) {
		if (message)
			game_title = message;
		else
			game_title = "";
	}
};

// globals
static PMixer *g_mixer = 0;
#ifndef _WIN32
static AndroidLogger *logger = 0;
#endif

std::string boot_filename = "";

void NativeHost::InitSound(PMixer *mixer) {
	g_mixer = mixer;
#ifdef IOS
	iOSCoreAudioInit();
#endif
}

void NativeHost::ShutdownSound() {
#ifdef IOS
	iOSCoreAudioShutdown();
#endif
	g_mixer = 0;
}

int NativeMix(short *audio, int num_samples) {
	if (g_mixer) {
		num_samples = g_mixer->Mix(audio, num_samples);
	}	else {
		memset(audio, 0, num_samples * 2 * sizeof(short));
	}
	return num_samples;
}

void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape) {
	*app_nice_name = "PPSSPP";
	*app_dir_name = "ppsspp";
	*landscape = true;

#if defined(ARM) && defined(ANDROID)
	ArmEmitterTest();
#endif
}

void NativeInit(int argc, const char *argv[],
								const char *savegame_directory, const char *external_directory, const char *installID) {
#ifdef ANDROID_NDK_PROFILER
	setenv("CPUPROFILE_FREQUENCY", "500", 1);
	setenv("CPUPROFILE", "/sdcard/gmon.out", 1);
	monstartup("ppsspp_jni.so");
#endif

	bool skipLogo = false;
	EnableFZ();
	setlocale( LC_ALL, "C" );
	std::string user_data_path = savegame_directory;
	isMessagePending = false;

#ifdef IOS
	user_data_path += "/";
#elif defined(__APPLE__)
	if (File::Exists(File::GetExeDirectory() + "assets"))
		VFSRegister("", new DirectoryAssetReader((File::GetExeDirectory() + "assets/").c_str()));
	// It's common to be in a build-xyz/ directory.
	else
		VFSRegister("", new DirectoryAssetReader((File::GetExeDirectory() + "../assets/").c_str()));
	VFSRegister("", new DirectoryAssetReader((File::GetExeDirectory()).c_str()));
#endif

	// We want this to be FIRST.
#ifdef USING_QT_UI
	VFSRegister("", new AssetsAssetReader());
#elif defined(BLACKBERRY) || defined(IOS)
	// Packed assets are included in app
	VFSRegister("", new DirectoryAssetReader(external_directory));
#else
	VFSRegister("", new DirectoryAssetReader("assets/"));
#endif
	VFSRegister("", new DirectoryAssetReader(savegame_directory));

	host = new NativeHost();

#if defined(ANDROID)
	g_Config.internalDataDirectory = savegame_directory;
	// Maybe there should be an option to use internal memory instead, but I think
	// that for most people, using external memory (SDCard/USB Storage) makes the
	// most sense.
	g_Config.memCardDirectory = std::string(external_directory) + "/";
	g_Config.flash0Directory = std::string(external_directory) + "/flash0/";
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__) || defined(MEEGO_EDITION_HARMATTAN) || defined(IOS)
	g_Config.memCardDirectory = user_data_path;
	g_Config.flash0Directory = std::string(external_directory) + "/flash0/";
#elif !defined(_WIN32)
	char* config = getenv("XDG_CONFIG_HOME");
	if (!config) {
		config = getenv("HOME");
		strcat(config, "/.config");
	}
	g_Config.memCardDirectory = std::string(config) + "/ppsspp/";
	std::string program_path = File::GetExeDirectory();
	if (program_path.empty())
		g_Config.flash0Directory = g_Config.memCardDirectory + "/flash0/";
	else if (File::Exists(program_path + "flash0"))
		g_Config.flash0Directory = program_path + "flash0/";
	// It's common to be in a build-xyz/ directory.
	else
		g_Config.flash0Directory = program_path + "../flash0/";
#endif

#ifndef _WIN32
	logger = new AndroidLogger();

	LogManager::Init();
	LogManager *logman = LogManager::GetInstance();
	ILOG("Logman: %p", logman);

	g_Config.AddSearchPath(user_data_path);
	g_Config.AddSearchPath(g_Config.memCardDirectory + "PSP/SYSTEM/");
	g_Config.SetDefaultPath(g_Config.memCardDirectory + "PSP/SYSTEM/");
	g_Config.Load();
	g_Config.externalDirectory = external_directory;
#endif

#ifdef ANDROID
	// On Android, create a PSP directory tree in the external_directory,
	// to hopefully reduce confusion a bit.
	ILOG("Creating %s", (g_Config.memCardDirectory + "PSP").c_str());
	mkDir((g_Config.memCardDirectory + "PSP").c_str());
	mkDir((g_Config.memCardDirectory + "PSP/SAVEDATA").c_str());
	mkDir((g_Config.memCardDirectory + "PSP/GAME").c_str());
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
				break;
			}
		} else {
			if (boot_filename.empty()) {
				boot_filename = argv[i];
				skipLogo = true;

				FileInfo info;
				if (!getFileInfo(boot_filename.c_str(), &info) || info.exists == false) {
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
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__) || defined(MEEGO_EDITION_HARMATTAN) || defined(IOS) || defined(_WIN32)
		g_Config.currentDirectory = savegame_directory;
#else
		g_Config.currentDirectory = getenv("HOME");
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
	INFO_LOG(BOOT, "Logger inited.");
#endif

	i18nrepo.LoadIni(g_Config.sLanguageIni);
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
}

void NativeInitGraphics() {
	CheckGLExtensions();
	gl_lost_manager_init();
	ui_draw2d.SetAtlas(&ui_atlas);
	ui_draw2d_front.SetAtlas(&ui_atlas);

	UIShader_Init();

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

	/*
	ui_theme.buttonStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON);
	ui_theme.buttonStyle.fgColor = 0xFFFFFFFF;
	ui_theme.buttonStyle.image = I_BUTTON;
	ui_theme.buttonFocusedStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON, 0xFFe0e0e0);
	ui_theme.buttonFocusedStyle.fgColor = 0xFFFFFFFF;
	ui_theme.buttonDownStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON_SELECTED, 0xFFFFFFFF);
	ui_theme.buttonDownStyle.fgColor = 0xFFFFFFFF;
	ui_theme.buttonDisabledStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON, 0xFF404040);
	ui_theme.buttonDisabledStyle.fgColor = 0xFF707070;
	*/

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
	ui_draw2d.Init();
	ui_draw2d_front.Init();

	uiTexture = new Texture();
	if (!uiTexture->Load("ui_atlas.zim")) {
		PanicAlert("Failed to load ui_atlas.zim.\n\nPlace it in the directory \"assets\" under your PPSSPP directory.");
		ELOG("Failed to load ui_atlas.zim");
	}
	uiTexture->Bind(0);

	uiContext = new UIContext();
	uiContext->theme = &ui_theme;
	uiContext->Init(UIShader_Get(), UIShader_GetPlain(), uiTexture, &ui_draw2d, &ui_draw2d_front);
	if (uiContext->Text())
		uiContext->Text()->SetFont("Tahoma", 20, 0);
	screenManager->setUIContext(uiContext);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glstate.viewport.set(0, 0, pixel_xres, pixel_yres);
}

void NativeShutdownGraphics() {
	screenManager->deviceLost();

	g_gameInfoCache.Clear();

	delete uiTexture;
	uiTexture = NULL;

	delete uiContext;
	uiContext = NULL;

	ui_draw2d.Shutdown();
	ui_draw2d_front.Shutdown();

	UIShader_Shutdown();

	gl_lost_manager_shutdown();
}

void TakeScreenshot() {
#ifdef _WIN32
	g_TakeScreenshot = false;
	mkDir(g_Config.memCardDirectory + "/PSP/SCREENSHOT");

	// First, find a free filename.
	int i = 0;

	char temp[256];
	while (i < 10000){
		if(g_Config.bScreenshotsAsPNG)
			sprintf(temp, "%s/PSP/SCREENSHOT/screen%05d.png", g_Config.memCardDirectory.c_str(), i);
		else
			sprintf(temp, "%s/PSP/SCREENSHOT/screen%05d.jpg", g_Config.memCardDirectory.c_str(), i);
		FileInfo info;
		if (!getFileInfo(temp, &info))
			break;
		i++;
	}

	// Okay, allocate a buffer.
	u8 *buffer = new u8[4 * pixel_xres * pixel_yres];
	// Silly openGL reads upside down, we flip to another buffer for simplicity.
	u8 *flipbuffer = new u8[4 * pixel_xres * pixel_yres];

	glReadPixels(0, 0, pixel_xres, pixel_yres, GL_RGBA, GL_UNSIGNED_BYTE, buffer);

	for (int y = 0; y < pixel_yres; y++) {
		memcpy(flipbuffer + y * pixel_xres * 4, buffer + (pixel_yres - y - 1) * pixel_xres * 4, pixel_xres * 4);
	}

	if (g_Config.bScreenshotsAsPNG) {
		stbi_write_png(temp, pixel_xres, pixel_yres, 4, flipbuffer, pixel_xres * 4);
	} else {
		jpge::params params;
		params.m_quality = 90;
		compress_image_to_jpeg_file(temp, pixel_xres, pixel_yres, 4, flipbuffer, params);
	}

	delete [] buffer;
	delete [] flipbuffer;

	osm.Show(temp);
#endif
}

void DrawDownloadsOverlay(UIContext &ctx) {
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

	ctx.Begin();
	int h = 5;
	for (int i = 0; i < progress.size(); i++) {
		float barWidth = 10 + (dp_xres - 10) * progress[i];
		Bounds bounds(0, h * i, barWidth, h);
		UI::Drawable solid(colors[i & 3]);
		ctx.FillRect(solid, bounds);
	}
	ctx.End();
	ctx.Flush();
}

void NativeRender() {
	g_GameManager.Update();

	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	// Clearing the screen at the start of the frame is an optimization for tiled mobile GPUs, as it then doesn't need to keep it around between frames.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glstate.viewport.set(0, 0, pixel_xres, pixel_yres);
	glstate.Restore();

	Matrix4x4 ortho;
	ortho.setOrtho(0.0f, dp_xres, dp_yres, 0.0f, -1.0f, 1.0f);
	glsl_bind(UIShader_Get());
	glUniformMatrix4fv(UIShader_Get()->u_worldviewproj, 1, GL_FALSE, ortho.getReadPtr());

	screenManager->render();
	if (screenManager->getUIContext()->Text()) {
		screenManager->getUIContext()->Text()->OncePerFrame();
	}

	DrawDownloadsOverlay(*screenManager->getUIContext());

	if (g_TakeScreenshot) {
		TakeScreenshot();
	}
}

void NativeUpdate(InputState &input) {
	{
		lock_guard lock(pendingMutex);
		if (isMessagePending) {
			screenManager->sendMessage(pendingMessage.c_str(), pendingValue.c_str());
			isMessagePending = false;
		}
	}

	g_DownloadManager.Update();
	screenManager->update(input);
}

void NativeDeviceLost() {
	g_gameInfoCache.Clear();
	screenManager->deviceLost();
	gl_lost();
	glstate.Restore();
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

void NativeTouch(const TouchInput &touch) {
	if (screenManager)
		screenManager->touch(touch);
}

void NativeKey(const KeyInput &key) {
	// ILOG("Key code: %i flags: %i", key.keyCode, key.flags);
	g_buttonTracker.Process(key);
	if (screenManager)
		screenManager->key(key);
}

void NativeAxis(const AxisInput &key) {
	// ILOG("Axis id: %i value: %f", (int)key.axisId, key.value);
	if (key.axisId >= JOYSTICK_AXIS_ACCELEROMETER_X && key.axisId <= JOYSTICK_AXIS_ACCELEROMETER_Z)	{
		// Disable accelerometer as an axis for now.
		return;
	}
	if (isOuya && key.axisId >= JOYSTICK_AXIS_OUYA_UNKNOWN1 && key.axisId <= JOYSTICK_AXIS_OUYA_UNKNOWN4) {
		return;
	}
	if (screenManager)
		screenManager->axis(key);
}

void NativeMessageReceived(const char *message, const char *value) {
	// We can only have one message queued.
	lock_guard lock(pendingMutex);
	if (!isMessagePending) {
		pendingMessage = message;
		pendingValue = value;
		isMessagePending = true;
	}
}


void NativeShutdown() {
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
	// This means that the activity has been completely destroyed. PPSSPP does not
	// boot up correctly with "dirty" global variables currently, so we hack around that
	// by simply exiting.
#ifdef ANDROID
	ILOG("NativeShutdown called");
	exit(0);
#endif

#ifdef _WIN32
	RemoveFontResourceEx(L"assets/Roboto-Condensed.ttf", FR_PRIVATE, NULL);
#endif
}

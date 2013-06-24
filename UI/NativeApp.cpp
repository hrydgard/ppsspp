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
#include "ext/jpge/jpge.h"
#include "gfx_es2/gl_state.h"
#include "gfx/gl_lost_manager.h"
#include "gfx/texture.h"
#include "i18n/i18n.h"
#include "input/input_state.h"
#include "math/math_util.h"
#include "math/lin/matrix4x4.h"
#include "ui/screen.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "ui/view.h"

#include "Common/FileUtil.h"
#include "Common/LogManager.h"
#include "Core/PSPMixer.h"
#include "Core/CPU.h"
#include "Core/Config.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Host.h"
#include "Core/SaveState.h"
#include "Core/HW/atrac3plus.h"
#include "Common/MemArena.h"

#include "ui_atlas.h"
#include "EmuScreen.h"
#include "MenuScreens.h"
#include "GameInfoCache.h"
#include "UIShader.h"

#include "UI/PluginScreen.h"
#include "UI/OnScreenDisplay.h"

// The new UI framework, for initialization

static UI::Theme ui_theme;

#ifdef ARM
#include "../../android/jni/ArmEmitterTest.h"
#endif

#if defined(__APPLE__) && !defined(IOS)
#include <mach-o/dyld.h>
#endif

Texture *uiTexture;

ScreenManager *screenManager;
std::string config_filename;
std::string game_title;

#ifdef IOS
bool isJailed;
#endif

// Really need to clean this mess of globals up... but instead I add more :P
bool g_TakeScreenshot;

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
		game_title = message;
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
}

void NativeHost::ShutdownSound() {
	g_mixer = 0;
}

int NativeMix(short *audio, int num_samples) {
	// ILOG("Entering mixer");
	if (g_mixer) {
		num_samples = g_mixer->Mix(audio, num_samples);
	}	else {
		memset(audio, 0, num_samples * 2 * sizeof(short));
	}
	// ILOG("Leaving mixer");
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
	EnableFZ();
	setlocale( LC_ALL, "C" );
	std::string user_data_path = savegame_directory;
	isMessagePending = false;
	// We want this to be FIRST.
#ifndef USING_QT_UI
#ifdef BLACKBERRY
	// Packed assets are included in app/native/ dir
	VFSRegister("", new DirectoryAssetReader("app/native/assets/"));
#elif defined(IOS)
	VFSRegister("", new DirectoryAssetReader(external_directory));
	user_data_path += "/";
#elif defined(__APPLE__)
    char program_path[4090];
    uint32_t program_path_size = sizeof(program_path);
    _NSGetExecutablePath(program_path,&program_path_size);
    *(strrchr(program_path, '/')+1) = '\0';
    char assets_path[4096];
    sprintf(assets_path,"%sassets/",program_path);
    VFSRegister("", new DirectoryAssetReader(assets_path));
    VFSRegister("", new DirectoryAssetReader("assets/"));
#else
	VFSRegister("", new DirectoryAssetReader("assets/"));
#endif
#endif
	VFSRegister("", new DirectoryAssetReader(user_data_path.c_str()));

	host = new NativeHost();

#ifndef _WIN32
	logger = new AndroidLogger();

	LogManager::Init();
	LogManager *logman = LogManager::GetInstance();
	ILOG("Logman: %p", logman);

	config_filename = user_data_path + "/ppsspp.ini";
	g_Config.Load(config_filename.c_str());
	g_Config.externalDirectory = external_directory;
#endif

#ifdef ANDROID
	// On Android, create a PSP directory tree in the external_directory,
	// to hopefully reduce confusion a bit. 
	ILOG("Creating %s", (g_Config.externalDirectory + "/PSP").c_str());
	mkDir((g_Config.externalDirectory + "/PSP").c_str());
	mkDir((g_Config.externalDirectory + "/PSP/SAVEDATA").c_str());
	mkDir((g_Config.externalDirectory + "/PSP/GAME").c_str());
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
				if (!File::Exists(boot_filename))
				{
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

#if defined(ANDROID)
	g_Config.internalDataDirectory = savegame_directory;
	// Maybe there should be an option to use internal memory instead, but I think
	// that for most people, using external memory (SDCard/USB Storage) makes the
	// most sense.
	g_Config.memCardDirectory = std::string(external_directory) + "/";
	g_Config.flashDirectory = std::string(external_directory)+"/flash/";
#elif defined(BLACKBERRY) || defined(__SYMBIAN32__) || defined(MEEGO_EDITION_HARMATTAN) || defined(IOS) || defined(_WIN32)
	g_Config.memCardDirectory = user_data_path;
#ifdef BLACKBERRY
	g_Config.flashDirectory = "app/native/assets/flash/";
#elif defined(IOS)
	g_Config.flashDirectory = std::string(external_directory) + "flash0/";
#elif defined(MEEGO_EDITION_HARMATTAN)
	g_Config.flashDirectory = "/opt/PPSSPP/flash/";
#else
	g_Config.flashDirectory = user_data_path+"/flash/";
#endif
#else
	g_Config.memCardDirectory = std::string(getenv("HOME"))+"/.ppsspp/";
	g_Config.flashDirectory = g_Config.memCardDirectory+"/flash/";
#endif

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		logman->SetEnable(type, true);
		logman->SetLogLevel(type, gfxLog && i == LogTypes::G3D ? LogTypes::LDEBUG : logLevel);
#ifdef ANDROID
		logman->AddListener(type, logger);
#endif
	}
#ifdef __SYMBIAN32__
	g_Config.bHardwareTransform = true;
	g_Config.bUseVBO = false;
#endif
	// Special hack for G3D as it's very spammy. Need to make a flag for this.
	if (!gfxLog)
		logman->SetLogLevel(LogTypes::G3D, LogTypes::LERROR);
	INFO_LOG(BOOT, "Logger inited.");
#else
	g_Config.memCardDirectory = "MemStick/";
#endif	

	i18nrepo.LoadIni(g_Config.languageIni);

	if (!boot_filename.empty() && stateToLoad != NULL)
		SaveState::Load(stateToLoad);
	
	g_gameInfoCache.Init();
}

void NativeInitGraphics() {
	gl_lost_manager_init();
	ui_draw2d.SetAtlas(&ui_atlas);

	screenManager = new ScreenManager();

	if (boot_filename.empty()) {
#if (defined(_WIN32) && (defined(_M_IX86) || defined(_M_X64))) || defined(ARMEABI) || defined(ARMEABI_V7A) || (defined(MACOSX) && defined(_M_IX64))
		if (Atrac3plus_Decoder::CanAutoInstall()) {
			Atrac3plus_Decoder::DoAutoInstall();
			screenManager->switchScreen(new LogoScreen(boot_filename));
		} else {
			screenManager->switchScreen(new LogoScreen(boot_filename));
			// If first run and can't autoinstall, let's send the user to the atrac3plus download screen.
			if (g_Config.bFirstRun && !Atrac3plus_Decoder::IsInstalled())
				screenManager->push(new PluginScreen());
		}
#else
		screenManager->switchScreen(new LogoScreen(boot_filename));
#endif
	} else {
		// Go directly into the game.
		screenManager->switchScreen(new EmuScreen(boot_filename));
	}

	UIShader_Init();

	// Old style theme, to be removed later
	UITheme theme = {0};
	theme.uiFont = UBUNTU24;
	theme.uiFontSmall = UBUNTU24;
	theme.uiFontSmaller = UBUNTU24;
	theme.buttonImage = I_BUTTON;
	theme.buttonSelected = I_BUTTON_SELECTED;
	theme.checkOn = I_CHECKEDBOX;
	theme.checkOff = I_SQUARE;

	// memset(&ui_theme, 0, sizeof(ui_theme));
	// New style theme
	ui_theme.uiFont = UBUNTU24;
	ui_theme.uiFontSmall = UBUNTU24;
	ui_theme.uiFontSmaller = UBUNTU24;
	ui_theme.checkOn = I_CHECKEDBOX;
	ui_theme.checkOff = I_SQUARE;
	ui_theme.whiteImage = SOLIDWHITE;
	ui_theme.buttonStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON);
	ui_theme.buttonStyle.fgColor = 0xFFFFFFFF;
	ui_theme.buttonStyle.image = I_BUTTON;
	ui_theme.buttonFocusedStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON, 0xFFe0e0e0);
	ui_theme.buttonFocusedStyle.fgColor = 0xFFFFFFFF;
	ui_theme.buttonDownStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON_SELECTED, 0xFFFFFFFF);
	ui_theme.buttonDownStyle.fgColor = 0xFFFFFFFF;
	ui_theme.buttonDisabledStyle.background = UI::Drawable(UI::DRAW_4GRID, I_BUTTON, 0xFF404040);
	ui_theme.buttonDisabledStyle.fgColor = 0xFF707070;
	ui_theme.itemStyle.background = UI::Drawable(0x55000000);
	ui_theme.itemStyle.fgColor = 0xFFFFFFFF;
	ui_theme.itemFocusedStyle.background = UI::Drawable(0xCC909080);
	ui_theme.itemDownStyle.background = UI::Drawable(0xFFFFc080);
	ui_theme.itemDownStyle.fgColor = 0xFF000000;

	ui_draw2d.Init();
	ui_draw2d_front.Init();

	UIInit(&ui_atlas, theme);

	uiTexture = new Texture();
	if (!uiTexture->Load("ui_atlas.zim")) {
		PanicAlert("Failed to load ui_atlas.zim.\n\nPlace it in the directory \"assets\" under your PPSSPP directory.");
		ELOG("Failed to load ui_atlas.zim");
	}
	uiTexture->Bind(0);

	uiContext = new UIContext();
	uiContext->theme = &ui_theme;
	uiContext->Init(UIShader_Get(), UIShader_GetPlain(), uiTexture, &ui_draw2d, &ui_draw2d_front);

	screenManager->setUIContext(uiContext);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glstate.viewport.set(0, 0, pixel_xres, pixel_yres);
}

void TakeScreenshot() {
#ifdef _WIN32
	g_TakeScreenshot = false;
	mkDir("screenshots");

	// First, find a free filename.
	int i = 0;

	char temp[256];
	while (i < 10000) {
		sprintf(temp, "screenshots/screen%05d.jpg", i);
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

	jpge::params params;
	params.m_quality = 90;
	compress_image_to_jpeg_file(temp, pixel_xres, pixel_yres, 4, flipbuffer, params);

	delete [] buffer;
	delete [] flipbuffer;

	osm.Show(temp);
#endif
}

void NativeRender() {
	EnableFZ();

	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	// Clearing the screen at the start of the frame is an optimization for tiled mobile GPUs, as it then doesn't need to keep it around between frames.
	glClearColor(0,0,0,1);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glstate.viewport.set(0, 0, pixel_xres, pixel_yres);
	glstate.Restore();

	Matrix4x4 ortho;
	ortho.setOrtho(0.0f, dp_xres, dp_yres, 0.0f, -1.0f, 1.0f);
	glsl_bind(UIShader_Get());
	glUniformMatrix4fv(UIShader_Get()->u_worldviewproj, 1, GL_FALSE, ortho.getReadPtr());

	screenManager->render();

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

	UIUpdateMouse(0, input.pointer_x[0], input.pointer_y[0], input.pointer_down[0]);
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
	// TODO
	return false;
}

void NativeTouch(const TouchInput &touch) {
	if (screenManager)
		screenManager->touch(touch);
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

void NativeShutdownGraphics() {
	delete uiTexture;
	uiTexture = NULL;

	screenManager->shutdown();
	delete screenManager;
	screenManager = 0;

	ui_draw2d.Shutdown();
	ui_draw2d_front.Shutdown();

	UIShader_Shutdown();

	gl_lost_manager_shutdown();
}

void NativeShutdown() {
	g_gameInfoCache.Shutdown();

	delete host;
	host = 0;
	g_Config.Save();
#ifndef _WIN32
	LogManager::Shutdown();
#endif
	// This means that the activity has been completely destroyed. PPSSPP does not
	// boot up correctly with "dirty" global variables currently, so we hack around that
	// by simply exiting.
#ifdef ANDROID
	exit(0);
#endif
}

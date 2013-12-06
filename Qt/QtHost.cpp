// This file is Qt Desktop's equivalent of NativeApp.cpp

#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>

#include "QtHost.h"
#include "LogManager.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Config.h"
#include "base/NativeApp.h"
#include "i18n/i18n.h"
#include "UI/EmuScreen.h"
#include "UI/UIShader.h"
#include "UI/MiscScreens.h"
#include "UI/GameInfoCache.h"
#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"
#include "ui/ui.h"
#include "ui/ui_context.h"
#include "gfx_es2/draw_text.h"
#include "GPU/ge_constants.h"

static UI::Theme ui_theme;

const char *stateToLoad = NULL;

std::string boot_filename = "";
Texture *uiTexture;
UIContext *uiContext;

ScreenManager *screenManager;
std::string game_title;

event m_hGPUStepEvent;
recursive_mutex m_hGPUStepMutex;

recursive_mutex pendingMutex;
static bool isMessagePending;
static std::string pendingMessage;
static std::string pendingValue;

QtHost::QtHost(MainWindow *mainWindow_)
    : mainWindow(mainWindow_)
	, m_GPUStep(false)
	, m_GPUFlag(0)
{
	QObject::connect(this,SIGNAL(BootDoneSignal()),mainWindow,SLOT(Boot()));
}

bool QtHost::InitGL(std::string *error_string)
{
	return true;
}

void QtHost::ShutdownGL()
{
}

void QtHost::SetWindowTitle(const char *message)
{
	QString title = "PPSSPP " + QString(PPSSPP_GIT_VERSION) + " - " + QString::fromUtf8(message);

	mainWindow->setWindowTitle(title);
}

void QtHost::UpdateUI()
{
	mainWindow->updateMenus();
}


void QtHost::UpdateMemView()
{
	if(mainWindow->GetDialogMemory())
		mainWindow->GetDialogMemory()->Update();
}

void QtHost::UpdateDisassembly()
{
	if(mainWindow->GetDialogDisasm())
	{
		mainWindow->GetDialogDisasm()->Update();
	}
	if(mainWindow->GetDialogDisplaylist())
	{
		mainWindow->GetDialogDisplaylist()->Update();
	}
}

void QtHost::SetDebugMode(bool mode)
{
	if(mainWindow->GetDialogDisasm())
		mainWindow->GetDialogDisasm()->SetDebugMode(mode);
}


void QtHost::BeginFrame()
{
}

void QtHost::EndFrame()
{
}

void QtHost::InitSound(PMixer *mixer)
{
	g_mixer = mixer;
}

void QtHost::ShutdownSound()
{
	g_mixer = 0;
}

void QtHost::BootDone()
{
	symbolMap.SortSymbols();
	emit BootDoneSignal();
}


static const char* SymbolMapFilename(std::string currentFilename)
{
	std::string result = currentFilename;
	size_t dot = result.rfind('.');
	if (dot == result.npos)
		return (result + ".map").c_str();

	result.replace(dot, result.npos, ".map");
	return result.c_str();
}

bool QtHost::AttemptLoadSymbolMap()
{
	return symbolMap.LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart));
}

void QtHost::PrepareShutdown()
{
	symbolMap.SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart));
}

void QtHost::AddSymbol(std::string name, u32 addr, u32 size, int type=0)
{

}

bool QtHost::IsDebuggingEnabled()
{
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}

bool QtHost::GPUDebuggingActive()
{
	auto dialogDisplayList = mainWindow->GetDialogDisplaylist();
	if (dialogDisplayList && dialogDisplayList->isVisible())
	{
		if (GpuStep())
			SendGPUStart();

		return true;
	}
	return false;
}

void QtHost::GPUNotifyCommand(u32 pc)
{
	u32 op = Memory::ReadUnchecked_U32(pc);
	u32 cmd = op >> 24;
	if (GpuStep())
		SendGPUWait(cmd, pc, &gstate);
}

void QtHost::SendCoreWait(bool isWaiting)
{
}

bool QtHost::GpuStep()
{
	return m_GPUStep;
}

void QtHost::SendGPUStart()
{
	if(m_GPUFlag == -1)
	{
		m_GPUFlag = 0;
	}
}

void QtHost::SendGPUWait(u32 cmd, u32 addr, void *data)
{
	if((m_GPUFlag == 1 && (cmd == GE_CMD_PRIM || cmd == GE_CMD_BEZIER || cmd == GE_CMD_SPLINE)))
	{
		// Break after the draw
		m_GPUFlag = 0;
	}
	else if(m_GPUFlag == 0)
	{
		mainWindow->GetDialogDisasm()->UpdateDisplayList();
		mainWindow->GetDialogDisplaylist()->Update();
		m_hGPUStepEvent.wait(m_hGPUStepMutex);
	}
	else if(m_GPUFlag == 2 && addr == m_GPUData)
	{
		mainWindow->GetDialogDisasm()->UpdateDisplayList();
		mainWindow->GetDialogDisplaylist()->Update();
		m_hGPUStepEvent.wait(m_hGPUStepMutex);
	}
	else if(m_GPUFlag == 3 && (cmd == GE_CMD_PRIM || cmd == GE_CMD_BEZIER || cmd == GE_CMD_SPLINE))
	{
		GPUgstate *state = (GPUgstate*)data;
		u32 texAddr = (state->texaddr[0] & 0xFFFFF0) | ((state->texbufwidth[0]<<8) & 0x0F000000);
		if(texAddr == m_GPUData)
		{
			mainWindow->GetDialogDisasm()->UpdateDisplayList();
			mainWindow->GetDialogDisplaylist()->Update();
			m_hGPUStepEvent.wait(m_hGPUStepMutex);
		}
	}
}

void QtHost::SetGPUStep(bool value, int flag, u32 data)
{
	m_GPUStep = value;
	m_GPUFlag = flag;
	m_GPUData = data;
}

void QtHost::NextGPUStep()
{
	m_hGPUStepEvent.notify_one();
}

void NativeInit(int argc, const char *argv[], const char *savegame_directory, const char *external_directory, const char *installID)
{
	isMessagePending = false;

	std::string user_data_path = savegame_directory;
#ifdef Q_OS_LINUX
	char* config = getenv("XDG_CONFIG_HOME");
	if (!config) {
		config = getenv("HOME");
		strcat(config, "/.config");
	}
	std::string memcard_path = std::string(config) + "/ppsspp/";
#else
	std::string memcard_path = QDir::homePath().toStdString() + "/.ppsspp/";
#endif

	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(user_data_path.c_str()));
	VFSRegister("", new AssetsAssetReader());

	g_Config.AddSearchPath(user_data_path);
	g_Config.AddSearchPath(memcard_path + "PSP/SYSTEM/");
	g_Config.SetDefaultPath(memcard_path + "PSP/SYSTEM/");
	g_Config.Load();
	i18nrepo.LoadIni(g_Config.sLanguageIni);

	const char *fileToLog = 0;

	// Parse command line
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'j':
				g_Config.bJit = true;
				g_Config.bSaveSettings = false;
				break;
			case 'i':
				g_Config.bJit = false;
				g_Config.bSaveSettings = false;
				break;
			case 's':
				g_Config.bAutoRun = false;
				g_Config.bSaveSettings = false;
				break;
			case '-':
				if (!strcmp(argv[i], "--log") && i < argc - 1)
					fileToLog = argv[++i];
				if (!strncmp(argv[i], "--log=", strlen("--log=")) && strlen(argv[i]) > strlen("--log="))
					fileToLog = argv[i] + strlen("--log=");
				if (!strcmp(argv[i], "--state") && i < argc - 1)
					stateToLoad = argv[++i];
				if (!strncmp(argv[i], "--state=", strlen("--state=")) && strlen(argv[i]) > strlen("--state="))
					stateToLoad = argv[i] + strlen("--state=");
				break;
			}
		}
		else if (fileToStart.isNull())
		{
			fileToStart = QString(argv[i]);
			if (!QFile::exists(fileToStart))
			{
				qCritical("File '%s' does not exist!", qPrintable(fileToStart));
				exit(1);
			}
		}
		else
		{
			qCritical("Can only boot one file. Ignoring file '%s'", qPrintable(fileToStart));
		}
	}

	if (g_Config.currentDirectory == "")
	{
		g_Config.currentDirectory = QDir::homePath().toStdString();
	}

	g_Config.memCardDirectory = memcard_path;

#if defined(Q_OS_LINUX)
	std::string program_path = QCoreApplication::applicationDirPath().toStdString();
	if (File::Exists(program_path + "/flash0"))
		g_Config.flash0Directory = program_path + "/flash0/";
	else if (File::Exists(program_path + "/../flash0"))
		g_Config.flash0Directory = program_path + "/../flash0/";
	else
		g_Config.flash0Directory = g_Config.memCardDirectory + "/flash0/";
#else
	g_Config.flash0Directory = g_Config.memCardDirectory + "/flash0/";
#endif

	LogManager::Init();
	if (fileToLog != NULL)
		LogManager::GetInstance()->ChangeFileLog(fileToLog);

	g_gameInfoCache.Init();

	// Start Desktop UI
	MainWindow* mainWindow = new MainWindow();
	mainWindow->show();
}

int NativeMix(short *audio, int num_samples)
{
	if (g_mixer)
		return g_mixer->Mix(audio, num_samples);
	else
		return 0;
}

void NativeInitGraphics()
{
	gl_lost_manager_init();
	ui_draw2d.SetAtlas(&ui_atlas);

	screenManager = new ScreenManager();
	if (boot_filename.empty()) {
		screenManager->switchScreen(new LogoScreen());
	} else {
		// Go directly into the game.
		screenManager->switchScreen(new EmuScreen(boot_filename));
	}

	UIShader_Init();

	UITheme theme = {0};
	theme.uiFont = UBUNTU24;
	theme.uiFontSmall = UBUNTU24;
	theme.uiFontSmaller = UBUNTU24;
	theme.buttonImage = I_SOLIDWHITE;  // not using classic buttons
	theme.buttonSelected = I_SOLIDWHITE;
	theme.checkOn = I_CHECKEDBOX;
	theme.checkOff = I_SQUARE;

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
	ui_theme.itemDisabledStyle.fgColor = 0xFFcccccc;

	ui_theme.buttonStyle = ui_theme.itemStyle;
	ui_theme.buttonFocusedStyle = ui_theme.itemFocusedStyle;
	ui_theme.buttonDownStyle = ui_theme.itemDownStyle;
	ui_theme.buttonDisabledStyle = ui_theme.itemDisabledStyle;

	ui_theme.popupTitle.fgColor = 0xFFE3BE59;

	ui_draw2d.Init();
	ui_draw2d_front.Init();

	UIInit(&ui_atlas, theme);

	uiTexture = new Texture();
	if (!uiTexture->Load("ui_atlas.zim"))
	{
		qDebug() << "Failed to load texture";
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

void NativeRender()
{
	// Clearing the screen at the start of the frame is an optimization for tiled mobile GPUs, as it then doesn't need to keep it around between frames.
	glClearColor(0,0,0,1);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	glstate.Restore();
	glViewport(0, 0, pixel_xres, pixel_yres);
	Matrix4x4 ortho;
	ortho.setOrtho(0.0f, dp_xres, dp_yres, 0.0f, -1.0f, 1.0f);
	glsl_bind(UIShader_Get());
	glUniformMatrix4fv(UIShader_Get()->u_worldviewproj, 1, GL_FALSE, ortho.getReadPtr());

	screenManager->render();
	if (screenManager->getUIContext()->Text()) {
		screenManager->getUIContext()->Text()->OncePerFrame();
	}
}

void NativeMessageReceived(const char *message, const char *value)
{
	lock_guard lock(pendingMutex);
	if (!isMessagePending) {
		pendingMessage = message;
		pendingValue = value;
		isMessagePending = true;
	}
}

void NativeUpdate(InputState &input)
{
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

void NativeTouch(const TouchInput &touch) {
	if (screenManager)
		screenManager->touch(touch);
}

void NativeKey(const KeyInput &key) {
	g_buttonTracker.Process(key);
	if (screenManager)
		screenManager->key(key);
}

void NativeAxis(const AxisInput &key) {
	if (screenManager)
		screenManager->axis(key);
}

void NativeShutdownGraphics()
{
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

void NativeShutdown()
{
	delete host;
	host = 0;
	g_Config.Save();
	LogManager::Shutdown();
	// This means that the activity has been completely destroyed. PPSSPP does not
	// boot up correctly with "dirty" global variables currently, so we hack around that
	// by simply exiting.
#ifdef ANDROID
	exit(0);
#endif
}

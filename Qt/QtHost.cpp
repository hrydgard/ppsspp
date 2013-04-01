// This file is Qt's equivalent of NativeApp.cpp

#include <QFileInfo>
#include <QDebug>

#include "QtHost.h"
#include "LogManager.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Config.h"
#include "base/NativeApp.h"
#include "UI/MenuScreens.h"
#include "UI/EmuScreen.h"
#include "UI/UIShader.h"
#include "UI/ui_atlas.h"
#include "GPU/ge_constants.h"
#include "EmuThread.h"

std::string boot_filename = "";
Texture *uiTexture;

ScreenManager *screenManager;
std::string config_filename;
std::string game_title;

event m_hGPUStepEvent;
recursive_mutex m_hGPUStepMutex;

QtHost::QtHost(MainWindow *mainWindow_)
    : mainWindow(mainWindow_)
	, m_GPUStep(false)
	, m_GPUFlag(0)
{
	QObject::connect(this,SIGNAL(BootDoneSignal()),mainWindow,SLOT(Boot()));
}

bool QtHost::InitGL(std::string *error_string)
{
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
	mainWindow->UpdateMenus();
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
		mainWindow->GetDialogDisasm()->GotoPC();
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
	mainWindow->Update();
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


static QString SymbolMapFilename(QString currentFilename)
{
	std::string result = currentFilename.toStdString();
	size_t dot = result.rfind('.');
	if (dot == result.npos)
		return (result + ".map").c_str();

	result.replace(dot, result.npos, ".map");
	return result.c_str();
}

bool QtHost::AttemptLoadSymbolMap()
{
	return symbolMap.LoadSymbolMap(SymbolMapFilename(GetCurrentFilename()).toStdString().c_str());
}

void QtHost::PrepareShutdown()
{
	symbolMap.SaveSymbolMap(SymbolMapFilename(GetCurrentFilename()).toStdString().c_str());
}

void QtHost::AddSymbol(std::string name, u32 addr, u32 size, int type=0)
{
	symbolMap.AddSymbol(name.c_str(), addr, size, (SymbolType)type);
}

bool QtHost::IsDebuggingEnabled()
{
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}

void QtHost::SendCoreWait(bool isWaiting)
{
	mainWindow->CoreEmitWait(isWaiting);
}

bool QtHost::GpuStep()
{
	return m_GPUStep;
}

void QtHost::SendGPUStart()
{
	EmuThread_LockDraw(false);

	if(m_GPUFlag == -1)
	{
		m_GPUFlag = 0;
	}

	EmuThread_LockDraw(true);
}

void QtHost::SendGPUWait(u32 cmd, u32 addr, void *data)
{
	EmuThread_LockDraw(false);

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

	EmuThread_LockDraw(true);
}

void QtHost::SetGPUStep(bool value, int flag, int data)
{
	m_GPUStep = value;
	m_GPUFlag = flag;
	m_GPUData = data;
}

void QtHost::NextGPUStep()
{
	m_hGPUStepEvent.notify_one();
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
	INFO_LOG(BOOT, "NativeInitGraphics - should only be called once!");
	gl_lost_manager_init();
	ui_draw2d.SetAtlas(&ui_atlas);

	screenManager = new ScreenManager();
	if (boot_filename.empty()) {
		screenManager->switchScreen(new LogoScreen(boot_filename));
	} else {
		// Go directly into the game.
		screenManager->switchScreen(new EmuScreen(boot_filename));
	}
	// screenManager->switchScreen(new FileSelectScreen());

	UIShader_Init();

	UITheme theme = {0};
	theme.uiFont = UBUNTU24;
	theme.uiFontSmall = UBUNTU24;
	theme.uiFontSmaller = UBUNTU24;
	theme.buttonImage = I_BUTTON;
	theme.buttonSelected = I_BUTTON_SELECTED;
	theme.checkOn = I_CHECKEDBOX;
	theme.checkOff = I_SQUARE;

	UIInit(&ui_atlas, theme);

	uiTexture = new Texture();
	if (!uiTexture->Load("ui_atlas.zim"))
	{
		qDebug() << "Failed to load texture";
	}
	uiTexture->Bind(0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glstate.viewport.set(0, 0, pixel_xres, pixel_yres);
}

void NativeRender()
{
	EnableFZ();
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
}

void NativeUpdate(InputState &input)
{
	UIUpdateMouse(0, input.pointer_x[0], input.pointer_y[0], input.pointer_down[0]);
	screenManager->update(input);
}

void NativeShutdownGraphics()
{
	delete uiTexture;
	uiTexture = NULL;

	screenManager->shutdown();
	delete screenManager;
	screenManager = 0;

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

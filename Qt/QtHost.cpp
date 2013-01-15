// This file is Qt's equivalent of NativeApp.cpp

#include <QFileInfo>
#include <QDebug>

#include "QtHost.h"
#include "LogManager.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Config.h"
#include "base/NativeApp.h"
#include "android/jni/MenuScreens.h"
#include "android/jni/EmuScreen.h"
#include "android/jni/UIShader.h"
#include "android/jni/ui_atlas.h"

std::string boot_filename = "";
Texture *uiTexture;

ScreenManager *screenManager;
std::string config_filename;
std::string game_title;

QtHost::QtHost(MainWindow *mainWindow_)
    : mainWindow(mainWindow_)
{
	QObject::connect(this,SIGNAL(BootDoneSignal()),mainWindow,SLOT(Boot()));
}

void QtHost::InitGL()
{

}

void QtHost::ShutdownGL()
{
	//GL_Shutdown();
}

void QtHost::SetWindowTitle(const char *message)
{
	// Really need a better way to deal with versions.
	QString title = "PPSSPP v0.5 - " + QString::fromUtf8(message);

	mainWindow->setWindowTitle(title);
}

void QtHost::UpdateUI()
{
	//mainWindow->Update();
	mainWindow->UpdateMenus();
}


void QtHost::UpdateMemView()
{
	/*for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();*/
}

void QtHost::UpdateDisassembly()
{
	/*for (int i=0; i<numCPUs; i++)
		if (disasmWindow[i])
			disasmWindow[i]->Update();*/
	/*if(dialogDisasm)
		dialogDisasm->Update();*/
}

void QtHost::SetDebugMode(bool mode)
{
	/*for (int i=0; i<numCPUs; i++)*/
	if(mainWindow->GetDialogDisasm())
		mainWindow->GetDialogDisasm()->SetDebugMode(mode);
}


void QtHost::BeginFrame()
{
	/*for (auto iter = this->input.begin(); iter != this->input.end(); iter++)
		if ((*iter)->UpdateState() == 0)
			break; // *iter is std::shared_ptr, **iter is InputDevice
	GL_BeginFrame();*/
}
void QtHost::EndFrame()
{
	//GL_EndFrame();
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

bool QtHost::AttemptLoadSymbolMap()
{
	QFileInfo currentFile(fileToStart);
	QString ret = currentFile.baseName() + ".map";
	return symbolMap.LoadSymbolMap(ret.toAscii().constData());
}

void QtHost::PrepareShutdown()
{
	QFileInfo currentFile(fileToStart);
	QString ret = currentFile.baseName() + ".map";
	symbolMap.SaveSymbolMap(ret.toAscii().constData());
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

void NativeMix(short *audio, int num_samples)
{
	if (g_mixer)
		g_mixer->Mix(audio, num_samples);
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

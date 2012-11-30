#include "../Core/Core.h"
#include "EmuThread.h"
#include "DSoundStream.h"
#include "WindowsHost.h"
#include "WndMainWindow.h"
#include "OpenGLBase.h"

#include "../Windows/Debugger/Debugger_Disasm.h"
#include "../Windows/Debugger/Debugger_MemoryDlg.h"
#include "../Core/Debugger/SymbolMap.h"

#include "main.h"


static PMixer *curMixer;

int MyMix(short *buffer, int numSamples, int bits, int rate, int channels)
{
	if (curMixer && !Core_IsStepping())
		return curMixer->Mix(buffer, numSamples);
	else
	{
		memset(buffer,0,numSamples*sizeof(short)*2);
		return numSamples;
	}
}

void WindowsHost::InitGL()
{
	GL_Init(MainWindow::GetDisplayHWND());
}

void WindowsHost::ShutdownGL()
{
	GL_Shutdown();
}

void WindowsHost::SetWindowTitle(const char *message)
{
	// Really need a better way to deal with versions.
	std::string title = "PPSSPP v0.31 - ";
	title += message;
	SetWindowText(mainWindow_, title.c_str());
}

void WindowsHost::InitSound(PMixer *mixer)
{
	curMixer = mixer;
	DSound::DSound_StartSound(MainWindow::GetHWND(), MyMix);
}

void WindowsHost::UpdateSound()
{
	DSound::DSound_UpdateSound();
}

void WindowsHost::ShutdownSound()
{
	DSound::DSound_StopSound();
	delete curMixer;
	curMixer = 0;
}

void WindowsHost::UpdateUI()
{
	MainWindow::Update();
	MainWindow::UpdateMenus();
}


void WindowsHost::UpdateMemView() 
{
	for (int i=0; i<numCPUs; i++)
		if (memoryWindow[i])
			memoryWindow[i]->Update();
}

void WindowsHost::UpdateDisassembly()
{
	for (int i=0; i<numCPUs; i++)
		if (disasmWindow[i])
			disasmWindow[i]->Update();
}

void WindowsHost::SetDebugMode(bool mode)
{
	for (int i=0; i<numCPUs; i++)
		if (disasmWindow[i])
			disasmWindow[i]->SetDebugMode(mode);
}


void WindowsHost::BeginFrame()
{
	for (auto iter = this->input.begin(); iter != this->input.end(); iter++)
		if ((*iter)->UpdateState() == 0)
			break; // *iter is std::shared_ptr, **iter is InputDevice
	GL_BeginFrame();
}
void WindowsHost::EndFrame()
{
	GL_EndFrame();
}

void WindowsHost::BootDone()
{
	symbolMap.SortSymbols();
	SendMessage(MainWindow::GetHWND(), WM_USER+1, 0,0);
}

bool WindowsHost::AttemptLoadSymbolMap()
{
	char filename[256];
	strcpy(filename, GetCurrentFilename());
	int len = strlen(filename);
	int ptpos = len-1;
	while (filename[ptpos]!='.' && ptpos>len-8)
		ptpos--;
	filename[ptpos+1] = 'm';
	filename[ptpos+2] = 'a';
	filename[ptpos+3] = 'p';
	return symbolMap.LoadSymbolMap(filename);
}

void WindowsHost::PrepareShutdown()
{
	char filename[256];
	strcpy(filename, GetCurrentFilename());
    int len = strlen(filename);
	int ptpos = len-1;
	while (filename[ptpos]!='.' && ptpos>len-8)
		ptpos--;
	filename[ptpos+1] = 'm';
	filename[ptpos+2] = 'a';
	filename[ptpos+3] = 'p';
	symbolMap.SaveSymbolMap(filename);
}

void WindowsHost::AddSymbol(std::string name, u32 addr, u32 size, int type=0) 
{
	symbolMap.AddSymbol(name.c_str(), addr, size, (SymbolType)type);
}

bool WindowsHost::IsDebuggingEnabled()
{
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}

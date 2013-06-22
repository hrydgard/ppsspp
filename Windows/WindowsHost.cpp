// Copyright (c) 2012- PPSSPP Project.

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

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
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

bool WindowsHost::InitGL(std::string *error_message)
{
	return GL_Init(MainWindow::GetDisplayHWND(), error_message);
}

void WindowsHost::ShutdownGL()
{
	GL_Shutdown();
}

void WindowsHost::SetWindowTitle(const char *message)
{
	std::string title = std::string("PPSSPP ") + PPSSPP_GIT_VERSION + " - " + message;

	int size = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), (int) title.size(), NULL, 0);
	if (size > 0)
	{
		// VC++6.0 any more?
		wchar_t *utf16_title = new(std::nothrow) wchar_t[size + 1];
		if (utf16_title)
			size = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), (int) title.size(), utf16_title, size);
		else
			size = 0;

		if (size > 0)
		{
			utf16_title[size] = 0;
			// Don't use SetWindowTextW because it will internally use DefWindowProcA.
			DefWindowProcW(mainWindow_, WM_SETTEXT, 0, (LPARAM) utf16_title);
			delete[] utf16_title;
		}
	}

	// Something went wrong, fall back to using the local codepage.
	if (size <= 0)
		SetWindowTextA(mainWindow_, title.c_str());
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
	if (curMixer != NULL)
		delete curMixer;
	curMixer = 0;
}

void WindowsHost::UpdateUI()
{
	MainWindow::Update();
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

extern BOOL g_bFullScreen;

void WindowsHost::PollControllers(InputState &input_state)
{
	bool doPad = true;
	for (auto iter = this->input.begin(); iter != this->input.end(); iter++)
	{
		auto device = *iter;
		if (!doPad && device->IsPad())
			continue;
		if (device->UpdateState(input_state) == InputDevice::UPDATESTATE_SKIP_PAD)
			doPad = false;
	}
}

void WindowsHost::BootDone()
{
	symbolMap.SortSymbols();
	SendMessage(MainWindow::GetHWND(), WM_USER+1, 0,0);

	SetDebugMode(!g_Config.bAutoRun);
	Core_EnableStepping(!g_Config.bAutoRun);
}

static std::string SymbolMapFilename(const char *currentFilename)
{
	std::string result = currentFilename;
	size_t dot = result.rfind('.');
	if (dot == result.npos)
		return result + ".map";

	result.replace(dot, result.npos, ".map");
	return result;
}

bool WindowsHost::AttemptLoadSymbolMap()
{
	if (loadedSymbolMap_)
		return true;
	loadedSymbolMap_ = symbolMap.LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str()).c_str());
	return loadedSymbolMap_;
}

void WindowsHost::SaveSymbolMap()
{
	symbolMap.SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart.c_str()).c_str());
	loadedSymbolMap_ = false;
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

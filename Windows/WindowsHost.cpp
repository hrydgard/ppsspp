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

#include "ppsspp_config.h"

#include <algorithm>

// For shell links
#include "Common/CommonWindows.h"
#include "winnls.h"
#include "shobjidl.h"
#include "objbase.h"
#include "objidl.h"
#include "shlguid.h"
#pragma warning(push)
#pragma warning(disable:4091)  // workaround bug in VS2015 headers
#include "shlobj.h"
#pragma warning(pop)

// native stuff
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/DirListing.h"
#include "Common/StringUtils.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/Instance.h"

#include "Windows/EmuThread.h"
#include "Windows/WindowsAudio.h"
#include "Windows/WindowsHost.h"
#include "Windows/MainWindow.h"

#if PPSSPP_API(ANY_GL)
#include "Windows/GPU/WindowsGLContext.h"
#endif
#include "Windows/GPU/WindowsVulkanContext.h"
#include "Windows/GPU/D3D9Context.h"
#include "Windows/GPU/D3D11Context.h"

#include "Windows/Debugger/DebuggerShared.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"

#ifndef _M_ARM
#include "Windows/DinputDevice.h"
#endif
#include "Windows/XinputDevice.h"

#include "Windows/main.h"
#include "UI/OnScreenDisplay.h"

float g_mouseDeltaX = 0;
float g_mouseDeltaY = 0;

static BOOL PostDialogMessage(Dialog *dialog, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
	return PostMessage(dialog->GetDlgHandle(), message, wParam, lParam);
}

WindowsHost::WindowsHost(HINSTANCE hInstance, HWND mainWindow, HWND displayWindow)
	: hInstance_(hInstance),
		displayWindow_(displayWindow),
		mainWindow_(mainWindow)
{
	g_mouseDeltaX = 0;
	g_mouseDeltaY = 0;

	//add first XInput device to respond
	input.push_back(std::make_unique<XinputDevice>());
#ifndef _M_ARM
	//find all connected DInput devices of class GamePad
	numDinputDevices_ = DinputDevice::getNumPads();
	for (size_t i = 0; i < numDinputDevices_; i++) {
		input.push_back(std::make_unique<DinputDevice>(static_cast<int>(i)));
	}
#endif
	SetConsolePosition();
}

void WindowsHost::SetConsolePosition() {
	HWND console = GetConsoleWindow();
	if (console != NULL && g_Config.iConsoleWindowX != -1 && g_Config.iConsoleWindowY != -1) {
		SetWindowPos(console, NULL, g_Config.iConsoleWindowX, g_Config.iConsoleWindowY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	}
}

void WindowsHost::UpdateConsolePosition() {
	RECT rc;
	HWND console = GetConsoleWindow();
	if (console != NULL && GetWindowRect(console, &rc) && !IsIconic(console)) {
		g_Config.iConsoleWindowX = rc.left;
		g_Config.iConsoleWindowY = rc.top;
	}
}

bool WindowsHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
	WindowsGraphicsContext *graphicsContext = nullptr;
	switch (g_Config.iGPUBackend) {
#if PPSSPP_API(ANY_GL)
	case (int)GPUBackend::OPENGL:
		graphicsContext = new WindowsGLContext();
		break;
#endif
	case (int)GPUBackend::DIRECT3D9:
		graphicsContext = new D3D9Context();
		break;
	case (int)GPUBackend::DIRECT3D11:
		graphicsContext = new D3D11Context();
		break;
	case (int)GPUBackend::VULKAN:
		graphicsContext = new WindowsVulkanContext();
		break;
	default:
		return false;
	}

	if (graphicsContext->Init(hInstance_, displayWindow_, error_message)) {
		*ctx = graphicsContext;
		gfx_ = graphicsContext;
		return true;
	} else {
		delete graphicsContext;
		*ctx = nullptr;
		gfx_ = nullptr;
		return false;
	}
}

void WindowsHost::ShutdownGraphics() {
	gfx_->Shutdown();
	delete gfx_;
	gfx_ = nullptr;
}

void WindowsHost::SetWindowTitle(const char *message) {
#ifdef GOLD
	const char *name = "PPSSPP Gold ";
#else
	const char *name = "PPSSPP ";
#endif
	std::wstring winTitle = ConvertUTF8ToWString(std::string(name) + PPSSPP_GIT_VERSION);
	if (message != nullptr) {
		winTitle.append(ConvertUTF8ToWString(" - "));
		winTitle.append(ConvertUTF8ToWString(message));
	}
#ifdef _DEBUG
	winTitle.append(L" (debug)");
#endif
	lastTitle_ = winTitle;

	MainWindow::SetWindowTitle(winTitle.c_str());
	PostMessage(mainWindow_, MainWindow::WM_USER_WINDOW_TITLE_CHANGED, 0, 0);
}

void WindowsHost::InitSound() {
}

// UGLY!
extern WindowsAudioBackend *winAudioBackend;

void WindowsHost::UpdateSound() {
	if (winAudioBackend)
		winAudioBackend->Update();
}

void WindowsHost::ShutdownSound() {
}

void WindowsHost::UpdateUI() {
	PostMessage(mainWindow_, MainWindow::WM_USER_UPDATE_UI, 0, 0);

	int peers = GetInstancePeerCount();
	if (PPSSPP_ID >= 1 && peers != lastNumInstances_) {
		lastNumInstances_ = peers;
		PostMessage(mainWindow_, MainWindow::WM_USER_WINDOW_TITLE_CHANGED, 0, 0);
	}
}

void WindowsHost::UpdateMemView() {
	if (memoryWindow)
		PostDialogMessage(memoryWindow, WM_DEB_UPDATE);
}

void WindowsHost::UpdateDisassembly() {
	if (disasmWindow)
		PostDialogMessage(disasmWindow, WM_DEB_UPDATE);
}

void WindowsHost::SetDebugMode(bool mode) {
	if (disasmWindow)
		PostDialogMessage(disasmWindow, WM_DEB_SETDEBUGLPARAM, 0, (LPARAM)mode);
}

void WindowsHost::PollControllers() {
	static int checkCounter = 0;
	static const int CHECK_FREQUENCY = 71;
	if (checkCounter++ > CHECK_FREQUENCY) {
#ifndef _M_ARM
		size_t newCount = DinputDevice::getNumPads();
		if (newCount > numDinputDevices_) {
			INFO_LOG(SYSTEM, "New controller device detected");
			for (size_t i = numDinputDevices_; i < newCount; i++) {
				input.push_back(std::make_unique<DinputDevice>(static_cast<int>(i)));
			}
			numDinputDevices_ = newCount;
		}
#endif
		checkCounter = 0;
	}

	for (const auto &device : input) {
		if (device->UpdateState() == InputDevice::UPDATESTATE_SKIP_PAD)
			break;
	}

	// Disabled by default, needs a workaround to map to psp keys.
	if (g_Config.bMouseControl) {
		float scaleFactor_x = g_dpi_scale_x * 0.1 * g_Config.fMouseSensitivity;
		float scaleFactor_y = g_dpi_scale_y * 0.1 * g_Config.fMouseSensitivity;

		float mx = std::max(-1.0f, std::min(1.0f, g_mouseDeltaX * scaleFactor_x));
		float my = std::max(-1.0f, std::min(1.0f, g_mouseDeltaY * scaleFactor_y));
		AxisInput axisX, axisY;
		axisX.axisId = JOYSTICK_AXIS_MOUSE_REL_X;
		axisX.deviceId = DEVICE_ID_MOUSE;
		axisX.value = mx;
		axisY.axisId = JOYSTICK_AXIS_MOUSE_REL_Y;
		axisY.deviceId = DEVICE_ID_MOUSE;
		axisY.value = my;

		if (GetUIState() == UISTATE_INGAME || g_Config.bMapMouse) {
			NativeAxis(axisX);
			NativeAxis(axisY);
		}
	}

	g_mouseDeltaX *= g_Config.fMouseSmoothing;
	g_mouseDeltaY *= g_Config.fMouseSmoothing;
}

void WindowsHost::BootDone() {
	if (g_symbolMap)
		g_symbolMap->SortSymbols();
	PostMessage(mainWindow_, WM_USER + 1, 0, 0);

	SetDebugMode(!g_Config.bAutoRun);
}

static Path SymbolMapFilename(const Path &currentFilename, const char *ext) {
	File::FileInfo info{};
	// can't fail, definitely exists if it gets this far
	File::GetFileInfo(currentFilename, &info);
	if (info.isDirectory) {
		return currentFilename / (std::string(".ppsspp-symbols") + ext);
	}
	return currentFilename.WithReplacedExtension(ext);
}

bool WindowsHost::AttemptLoadSymbolMap() {
	if (!g_symbolMap)
		return false;
	bool result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".ppmap"));
	// Load the old-style map file.
	if (!result1)
		result1 = g_symbolMap->LoadSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".map"));
	bool result2 = g_symbolMap->LoadNocashSym(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".sym"));
	return result1 || result2;
}

void WindowsHost::SaveSymbolMap() {
	if (g_symbolMap)
		g_symbolMap->SaveSymbolMap(SymbolMapFilename(PSP_CoreParameter().fileToStart, ".ppmap"));
}

void WindowsHost::NotifySymbolMapUpdated() {
	if (g_symbolMap)
		g_symbolMap->SortSymbols();
	PostMessage(mainWindow_, WM_USER + 1, 0, 0);
}

bool WindowsHost::IsDebuggingEnabled() {
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}

// http://msdn.microsoft.com/en-us/library/aa969393.aspx
HRESULT CreateLink(LPCWSTR lpszPathObj, LPCWSTR lpszArguments, LPCWSTR lpszPathLink, LPCWSTR lpszDesc) { 
	HRESULT hres; 
	IShellLink *psl = nullptr;
	hres = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hres))
		return hres;

	// Get a pointer to the IShellLink interface. It is assumed that CoInitialize
	// has already been called.
	hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl); 
	if (SUCCEEDED(hres) && psl) {
		IPersistFile *ppf = nullptr;

		// Set the path to the shortcut target and add the description. 
		psl->SetPath(lpszPathObj); 
		psl->SetArguments(lpszArguments);
		psl->SetDescription(lpszDesc); 

		// Query IShellLink for the IPersistFile interface, used for saving the 
		// shortcut in persistent storage. 
		hres = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf); 

		if (SUCCEEDED(hres) && ppf) {
			// Save the link by calling IPersistFile::Save. 
			hres = ppf->Save(lpszPathLink, TRUE); 
			ppf->Release(); 
		} 
		psl->Release(); 
	}
	CoUninitialize();

	return hres; 
}

bool WindowsHost::CanCreateShortcut() { 
	return false;  // Turn on when below function fixed
}

bool WindowsHost::CreateDesktopShortcut(std::string argumentPath, std::string gameTitle) {
	// TODO: not working correctly
	return false;


	// Get the desktop folder
	// TODO: Not long path safe.
	wchar_t *pathbuf = new wchar_t[MAX_PATH + gameTitle.size() + 100];
	SHGetFolderPath(0, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, pathbuf);
	
	// Sanitize the game title for banned characters.
	const char bannedChars[] = "<>:\"/\\|?*";
	for (size_t i = 0; i < gameTitle.size(); i++) {
		for (char c : bannedChars) {
			if (gameTitle[i] == c) {
				gameTitle[i] = '_';
				break;
			}
		}
	}

	wcscat(pathbuf, L"\\");
	wcscat(pathbuf, ConvertUTF8ToWString(gameTitle).c_str());

	std::wstring moduleFilename;
	size_t sz;
	do {
		moduleFilename.resize(moduleFilename.size() + MAX_PATH);
		// On failure, this will return the same value as passed in, but success will always be one lower.
		sz = GetModuleFileName(nullptr, &moduleFilename[0], (DWORD)moduleFilename.size());
	} while (sz >= moduleFilename.size());
	moduleFilename.resize(sz);

	CreateLink(moduleFilename.c_str(), ConvertUTF8ToWString(argumentPath).c_str(), pathbuf, ConvertUTF8ToWString(gameTitle).c_str());

	delete [] pathbuf;
	return false;
}

void WindowsHost::ToggleDebugConsoleVisibility() {
	MainWindow::ToggleDebugConsoleVisibility();
}

void WindowsHost::NotifyUserMessage(const std::string &message, float duration, u32 color, const char *id) {
	osm.Show(message, duration, color, -1, true, id);
}

void WindowsHost::SendUIMessage(const std::string &message, const std::string &value) {
	NativeMessageReceived(message.c_str(), value.c_str());
}

void WindowsHost::NotifySwitchUMDUpdated() {
	PostMessage(mainWindow_, MainWindow::WM_USER_SWITCHUMD_UPDATED, 0, 0);
}

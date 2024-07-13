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

#include "stdafx.h"
#include <algorithm>
#include <cmath>
#include <functional>

#include "Common/CommonWindows.h"
#include "Common/File/FileUtil.h"
#include "Common/OSVersion.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "ppsspp_config.h"

#include <mmsystem.h>
#include <shellapi.h>
#include <Wbemidl.h>
#include <ShlObj.h>

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Net/Resolve.h"
#include "Common/TimeUtil.h"
#include "W32Util/DarkMode.h"
#include "W32Util/ShellUtil.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/SaveState.h"
#include "Core/Instance.h"
#include "Windows/EmuThread.h"
#include "Windows/WindowsAudio.h"
#include "ext/disarm.h"

#include "Common/LogManager.h"
#include "Common/ConsoleListener.h"
#include "Common/StringUtils.h"

#include "Commctrl.h"

#include "UI/GameInfoCache.h"
#include "Windows/resource.h"

#include "Windows/MainWindow.h"
#include "Windows/Debugger/Debugger_Disasm.h"
#include "Windows/Debugger/Debugger_MemoryDlg.h"
#include "Windows/Debugger/Debugger_VFPUDlg.h"
#if PPSSPP_API(ANY_GL)
#include "Windows/GEDebugger/GEDebugger.h"
#endif
#include "Windows/W32Util/ContextMenu.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"

#include "Windows/Debugger/CtrlDisAsmView.h"
#include "Windows/Debugger/CtrlMemView.h"
#include "Windows/Debugger/CtrlRegisterList.h"
#include "Windows/Debugger/DebuggerShared.h"
#include "Windows/InputBox.h"

#include "Windows/WindowsHost.h"
#include "Windows/main.h"


// Nvidia OpenGL drivers >= v302 will check if the application exports a global
// variable named NvOptimusEnablement to know if it should run the app in high
// performance graphics mode or using the IGP.
extern "C" {
	__declspec(dllexport) DWORD NvOptimusEnablement = 1;
}

// Also on AMD PowerExpress: https://community.amd.com/thread/169965
extern "C" {
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#if PPSSPP_API(ANY_GL)
CGEDebugger* geDebuggerWindow = nullptr;
#endif

CDisasm *disasmWindow = nullptr;
CMemoryDlg *memoryWindow = nullptr;
CVFPUDlg *vfpudlg = nullptr;

static std::string langRegion;
static std::string osName;
static std::string gpuDriverVersion;

static std::string restartArgs;

int g_activeWindow = 0;

WindowsInputManager g_inputManager;

int g_lastNumInstances = 0;

static double g_lastActivity = 0.0;
static double g_lastKeepAwake = 0.0;
// Time until we stop considering the core active without user input.
// Should this be configurable?  2 hours currently.
static const double ACTIVITY_IDLE_TIMEOUT = 2.0 * 3600.0;

void System_LaunchUrl(LaunchUrlType urlType, const char *url) {
	ShellExecute(NULL, L"open", ConvertUTF8ToWString(url).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void System_Vibrate(int length_ms) {
	// Ignore on PC
}

static void AddDebugRestartArgs() {
	if (LogManager::GetInstance()->GetConsoleListener()->IsOpen())
		restartArgs += " -l";
}

// Adapted mostly as-is from http://www.gamedev.net/topic/495075-how-to-retrieve-info-about-videocard/?view=findpost&p=4229170
// so credit goes to that post's author, and in turn, the author of the site mentioned in that post (which seems to be down?).
std::string GetVideoCardDriverVersion() {
	std::string retvalue = "";

	HRESULT hr;
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		return retvalue;
	}

	IWbemLocator *pIWbemLocator = NULL;
	hr = CoCreateInstance(__uuidof(WbemLocator), NULL, CLSCTX_INPROC_SERVER,
		__uuidof(IWbemLocator), (LPVOID *)&pIWbemLocator);
	if (FAILED(hr)) {
		CoUninitialize();
		return retvalue;
	}

	BSTR bstrServer = SysAllocString(L"\\\\.\\root\\cimv2");
	IWbemServices *pIWbemServices;
	hr = pIWbemLocator->ConnectServer(bstrServer, NULL, NULL, 0L, 0L, NULL,	NULL, &pIWbemServices);
	if (FAILED(hr)) {
		pIWbemLocator->Release();
		SysFreeString(bstrServer);
		CoUninitialize();
		return retvalue;
	}

	hr = CoSetProxyBlanket(pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
		NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL,EOAC_DEFAULT);

	BSTR bstrWQL = SysAllocString(L"WQL");
	BSTR bstrPath = SysAllocString(L"select * from Win32_VideoController");
	IEnumWbemClassObject* pEnum;
	hr = pIWbemServices->ExecQuery(bstrWQL, bstrPath, WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);

	ULONG uReturned = 0;
	VARIANT var{};
	IWbemClassObject* pObj = NULL;
	if (!FAILED(hr)) {
		hr = pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturned);
	}

	if (!FAILED(hr) && uReturned) {
		hr = pObj->Get(L"DriverVersion", 0, &var, NULL, NULL);
		if (SUCCEEDED(hr)) {
			char str[MAX_PATH];
			WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, str, sizeof(str), NULL, NULL);
			retvalue = str;
		}
	}

	pEnum->Release();
	SysFreeString(bstrPath);
	SysFreeString(bstrWQL);
	pIWbemServices->Release();
	pIWbemLocator->Release();
	SysFreeString(bstrServer);
	CoUninitialize();
	return retvalue;
}

std::string System_GetProperty(SystemProperty prop) {
	static bool hasCheckedGPUDriverVersion = false;
	switch (prop) {
	case SYSPROP_NAME:
		return osName;
	case SYSPROP_LANGREGION:
		return langRegion;
	case SYSPROP_CLIPBOARD_TEXT:
		{
			std::string retval;
			if (OpenClipboard(MainWindow::GetDisplayHWND())) {
				HANDLE handle = GetClipboardData(CF_UNICODETEXT);
				const wchar_t *wstr = (const wchar_t*)GlobalLock(handle);
				if (wstr)
					retval = ConvertWStringToUTF8(wstr);
				else
					retval.clear();
				GlobalUnlock(handle);
				CloseClipboard();
			}
			return retval;
		}
	case SYSPROP_GPUDRIVER_VERSION:
		if (!hasCheckedGPUDriverVersion) {
			hasCheckedGPUDriverVersion = true;
			gpuDriverVersion = GetVideoCardDriverVersion();
		}
		return gpuDriverVersion;
	case SYSPROP_BUILD_VERSION:
		return PPSSPP_GIT_VERSION;
	case SYSPROP_USER_DOCUMENTS_DIR:
		return Path(W32Util::UserDocumentsPath()).ToString();  // this'll reverse the slashes.
	default:
		return "";
	}
}

std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) {
	std::vector<std::string> result;
	switch (prop) {
	case SYSPROP_TEMP_DIRS:
	{
		std::wstring tempPath(MAX_PATH, '\0');
		size_t sz = GetTempPath((DWORD)tempPath.size(), &tempPath[0]);
		if (sz >= tempPath.size()) {
			tempPath.resize(sz);
			sz = GetTempPath((DWORD)tempPath.size(), &tempPath[0]);
		}
		// Need to resize off the null terminator either way.
		tempPath.resize(sz);
		result.push_back(ConvertWStringToUTF8(tempPath));

		if (getenv("TMPDIR") && strlen(getenv("TMPDIR")) != 0)
			result.push_back(getenv("TMPDIR"));
		if (getenv("TMP") && strlen(getenv("TMP")) != 0)
			result.push_back(getenv("TMP"));
		if (getenv("TEMP") && strlen(getenv("TEMP")) != 0)
			result.push_back(getenv("TEMP"));
		return result;
	}

	default:
		return result;
	}
}

// Ugly!
extern WindowsAudioBackend *winAudioBackend;

#ifdef _WIN32
#if PPSSPP_PLATFORM(UWP)
static float ScreenDPI() {
	return 96.0f;  // TODO UWP
}
#else
static float ScreenDPI() {
	HDC screenDC = GetDC(nullptr);
	int dotsPerInch = GetDeviceCaps(screenDC, LOGPIXELSY);
	ReleaseDC(nullptr, screenDC);
	return dotsPerInch ? (float)dotsPerInch : 96.0f;
}
#endif
#endif

static float ScreenRefreshRateHz() {
	static float rate = 0.0f;
	static double lastCheck = 0.0;
	const double now = time_now_d();
	if (!rate || lastCheck < now - 10.0) {
		lastCheck = now;
		DEVMODE lpDevMode{};
		lpDevMode.dmSize = sizeof(DEVMODE);
		lpDevMode.dmDriverExtra = 0;

		// TODO: Use QueryDisplayConfig instead (Win7+) so we can get fractional refresh rates correctly.

		if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode) == 0) {
			rate = 60.0f;  // default value
		} else {
			if (lpDevMode.dmFields & DM_DISPLAYFREQUENCY) {
				rate = (float)(lpDevMode.dmDisplayFrequency > 60 ? lpDevMode.dmDisplayFrequency : 60);
			} else {
				rate = 60.0f;
			}
		}
	}
	return rate;
}

int64_t System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_MAIN_WINDOW_HANDLE:
		return (int64_t)MainWindow::GetHWND();
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return winAudioBackend ? winAudioBackend->GetSampleRate() : -1;
	case SYSPROP_DEVICE_TYPE:
		return DEVICE_TYPE_DESKTOP;
	case SYSPROP_DISPLAY_COUNT:
		return GetSystemMetrics(SM_CMONITORS);
	case SYSPROP_KEYBOARD_LAYOUT:
	{
		HKL localeId = GetKeyboardLayout(0);
		// TODO: Is this list complete enough?
		switch ((int)(intptr_t)localeId & 0xFFFF) {
		case 0x407:
			return KEYBOARD_LAYOUT_QWERTZ;
		case 0x040c:
		case 0x080c:
		case 0x1009:
			return KEYBOARD_LAYOUT_AZERTY;
		default:
			return KEYBOARD_LAYOUT_QWERTY;
		}
	}
	default:
		return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return ScreenRefreshRateHz();
	case SYSPROP_DISPLAY_DPI:
		return ScreenDPI();
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return 0.0f;
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_DEBUGGER:
	case SYSPROP_HAS_FILE_BROWSER:
	case SYSPROP_HAS_FOLDER_BROWSER:
	case SYSPROP_HAS_OPEN_DIRECTORY:
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
	case SYSPROP_CAN_CREATE_SHORTCUT:
	case SYSPROP_CAN_SHOW_FILE:
		return true;
	case SYSPROP_HAS_IMAGE_BROWSER:
		return true;
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_HAS_LOGIN_DIALOG:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	case SYSPROP_CAN_JIT:
		return true;
	case SYSPROP_HAS_KEYBOARD:
		return true;
	case SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR:
		return true;  // FileUtil.cpp: OpenFileInEditor
	case SYSPROP_SUPPORTS_HTTPS:
		return !g_Config.bDisableHTTPS;
	case SYSPROP_DEBUGGER_PRESENT:
		return IsDebuggerPresent();
	case SYSPROP_OK_BUTTON_LEFT:
		return true;
	default:
		return false;
	}
}

static BOOL PostDialogMessage(Dialog *dialog, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
	return PostMessage(dialog->GetDlgHandle(), message, wParam, lParam);
}

// This can come from any thread, so this mostly uses PostMessage. Can't access most data directly.
void System_Notify(SystemNotification notification) {
	switch (notification) {
	case SystemNotification::BOOT_DONE:
	{
		if (g_symbolMap)
			g_symbolMap->SortSymbols();  // internal locking is performed here
		PostMessage(MainWindow::GetHWND(), WM_USER + 1, 0, 0);

		if (disasmWindow)
			PostDialogMessage(disasmWindow, WM_DEB_SETDEBUGLPARAM, 0, (LPARAM)Core_IsStepping());
		break;
	}

	case SystemNotification::UI:
	{
		PostMessage(MainWindow::GetHWND(), MainWindow::WM_USER_UPDATE_UI, 0, 0);

		int peers = GetInstancePeerCount();
		if (PPSSPP_ID >= 1 && peers != g_lastNumInstances) {
			g_lastNumInstances = peers;
			PostMessage(MainWindow::GetHWND(), MainWindow::WM_USER_WINDOW_TITLE_CHANGED, 0, 0);
		}
		break;
	}

	case SystemNotification::MEM_VIEW:
		if (memoryWindow)
			PostDialogMessage(memoryWindow, WM_DEB_UPDATE);
		break;

	case SystemNotification::DISASSEMBLY:
		if (disasmWindow)
			PostDialogMessage(disasmWindow, WM_DEB_UPDATE);
		break;

	case SystemNotification::SYMBOL_MAP_UPDATED:
		if (g_symbolMap)
			g_symbolMap->SortSymbols();  // internal locking is performed here
		PostMessage(MainWindow::GetHWND(), WM_USER + 1, 0, 0);
		break;

	case SystemNotification::SWITCH_UMD_UPDATED:
		PostMessage(MainWindow::GetHWND(), MainWindow::WM_USER_SWITCHUMD_UPDATED, 0, 0);
		break;

	case SystemNotification::DEBUG_MODE_CHANGE:
		if (disasmWindow)
			PostDialogMessage(disasmWindow, WM_DEB_SETDEBUGLPARAM, 0, (LPARAM)Core_IsStepping());
		break;

	case SystemNotification::POLL_CONTROLLERS:
		g_inputManager.PollControllers();
		break;

	case SystemNotification::TOGGLE_DEBUG_CONSOLE:
		MainWindow::ToggleDebugConsoleVisibility();
		break;

	case SystemNotification::ACTIVITY:
		g_lastActivity = time_now_d();
		break;

	case SystemNotification::KEEP_SCREEN_AWAKE:
	{
		// Keep the system awake for longer than normal for cutscenes and the like.
		const double now = time_now_d();
		if (now < g_lastActivity + ACTIVITY_IDLE_TIMEOUT) {
			// Only resetting it ever prime number seconds in case the call is expensive.
			// Using a prime number to ensure there's no interaction with other periodic events.
			if (now - g_lastKeepAwake > 89.0 || now < g_lastKeepAwake) {
				// Note that this needs to be called periodically.
				// It's also possible to set ES_CONTINUOUS but let's not, for simplicity.
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
				SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
				g_lastKeepAwake = now;
			}
		}
		break;
	}
	}
}

static std::wstring MakeFilter(std::wstring filter) {
	for (size_t i = 0; i < filter.length(); i++) {
		if (filter[i] == '|')
			filter[i] = '\0';
	}
	return filter;
}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	switch (type) {
	case SystemRequestType::EXIT_APP:
		if (!NativeIsRestarting()) {
			PostMessage(MainWindow::GetHWND(), WM_CLOSE, 0, 0);
		}
		return true;
	case SystemRequestType::RESTART_APP:
	{
		restartArgs = param1;
		if (!restartArgs.empty())
			AddDebugRestartArgs();
		if (System_GetPropertyBool(SYSPROP_DEBUGGER_PRESENT)) {
			PostMessage(MainWindow::GetHWND(), MainWindow::WM_USER_RESTART_EMUTHREAD, 0, 0);
		} else {
			g_Config.bRestartRequired = true;
			PostMessage(MainWindow::GetHWND(), WM_CLOSE, 0, 0);
		}
		return true;
	}
	case SystemRequestType::COPY_TO_CLIPBOARD:
	{
		std::wstring data = ConvertUTF8ToWString(param1);
		W32Util::CopyTextToClipboard(MainWindow::GetDisplayHWND(), data);
		return true;
	}
	case SystemRequestType::SET_WINDOW_TITLE:
	{
		const char *name = System_GetPropertyBool(SYSPROP_APP_GOLD) ? "PPSSPP Gold " : "PPSSPP ";
		std::wstring winTitle = ConvertUTF8ToWString(std::string(name) + PPSSPP_GIT_VERSION);
		if (!param1.empty()) {
			winTitle.append(ConvertUTF8ToWString(" - " + param1));
		}
#ifdef _DEBUG
		winTitle.append(L" (debug)");
#endif
		MainWindow::SetWindowTitle(winTitle.c_str());
		PostMessage(MainWindow::GetHWND(), MainWindow::WM_USER_WINDOW_TITLE_CHANGED, 0, 0);
		return true;
	}
	case SystemRequestType::SET_KEEP_SCREEN_BRIGHT:
	{
		MainWindow::SetKeepScreenBright(param3 != 0);
		return true;
	}
	case SystemRequestType::INPUT_TEXT_MODAL:
		std::thread([=] {
			std::string out;
			if (InputBox_GetString(MainWindow::GetHInstance(), MainWindow::GetHWND(), ConvertUTF8ToWString(param1).c_str(), param2, out)) {
				g_requestManager.PostSystemSuccess(requestId, out.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		}).detach();
		return true;
	case SystemRequestType::ASK_USERNAME_PASSWORD:
		std::thread([=] {
			std::string username;
			std::string password;
			if (UserPasswordBox_GetStrings(MainWindow::GetHInstance(), MainWindow::GetHWND(), ConvertUTF8ToWString(param1).c_str(), &username, &password)) {
				g_requestManager.PostSystemSuccess(requestId, (username + '\n' + password).c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		}).detach();
		return true;
	case SystemRequestType::BROWSE_FOR_IMAGE:
		std::thread([=] {
			std::string out;
			if (W32Util::BrowseForFileName(true, MainWindow::GetHWND(), ConvertUTF8ToWString(param1).c_str(), nullptr,
				MakeFilter(L"All supported images (*.jpg *.jpeg *.png)|*.jpg;*.jpeg;*.png|All files (*.*)|*.*||").c_str(), L"jpg", out)) {
				g_requestManager.PostSystemSuccess(requestId, out.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		}).detach();
		return true;
	case SystemRequestType::BROWSE_FOR_FILE:
	{
		BrowseFileType type = (BrowseFileType)param3;
		std::wstring filter;
		switch (type) {
		case BrowseFileType::BOOTABLE:
			filter = MakeFilter(L"All supported file types (*.iso *.cso *.chd *.pbp *.elf *.prx *.zip *.ppdmp)|*.pbp;*.elf;*.iso;*.cso;*.chd;*.prx;*.zip;*.ppdmp|PSP ROMs (*.iso *.cso *.chd *.pbp *.elf *.prx)|*.pbp;*.elf;*.iso;*.cso;*.chd;*.prx|Homebrew/Demos installers (*.zip)|*.zip|All files (*.*)|*.*||");
			break;
		case BrowseFileType::INI:
			filter = MakeFilter(L"Ini files (*.ini)|*.ini|All files (*.*)|*.*||");
			break;
		case BrowseFileType::ZIP:
			filter = MakeFilter(L"ZIP files (*.zip)|*.zip|All files (*.*)|*.*||");
			break;
		case BrowseFileType::DB:
			filter = MakeFilter(L"Cheat db files (*.db)|*.db|All files (*.*)|*.*||");
			break;
		case BrowseFileType::SOUND_EFFECT:
			filter = MakeFilter(L"Sound effect files (*.wav *.mp3)|*.wav;*.mp3|All files (*.*)|*.*||");
			break;
		case BrowseFileType::ANY:
			filter = MakeFilter(L"All files (*.*)|*.*||");
			break;
		default:
			return false;
		}

		std::thread([=] {
			std::string out;
			if (W32Util::BrowseForFileName(true, MainWindow::GetHWND(), ConvertUTF8ToWString(param1).c_str(), nullptr, filter.c_str(), L"", out)) {
				g_requestManager.PostSystemSuccess(requestId, out.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		}).detach();
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
	{
		std::thread([=] {
			std::string folder = W32Util::BrowseForFolder(MainWindow::GetHWND(), param1, param2);
			if (folder.size()) {
				g_requestManager.PostSystemSuccess(requestId, folder.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		}).detach();
		return true;
	}

	case SystemRequestType::SHOW_FILE_IN_FOLDER:
		W32Util::ShowFileInFolder(param1);
		return true;

	case SystemRequestType::TOGGLE_FULLSCREEN_STATE:
	{
		bool flag = !MainWindow::IsFullscreen();
		if (param1 == "0") {
			flag = false;
		} else if (param1 == "1") {
			flag = true;
		}
		MainWindow::SendToggleFullscreen(flag);
		return true;
	}
	case SystemRequestType::GRAPHICS_BACKEND_FAILED_ALERT:
	{
		auto err = GetI18NCategory(I18NCat::ERRORS);
		std::string_view backendSwitchError = err->T("GenericBackendSwitchCrash", "PPSSPP crashed while starting. This usually means a graphics driver problem. Try upgrading your graphics drivers.\n\nGraphics backend has been switched:");
		std::wstring full_error = ConvertUTF8ToWString(StringFromFormat("%s %s", backendSwitchError, param1.c_str()));
		std::wstring title = ConvertUTF8ToWString(err->T("GenericGraphicsError", "Graphics Error"));
		MessageBox(MainWindow::GetHWND(), full_error.c_str(), title.c_str(), MB_OK);
		return true;
	}
	case SystemRequestType::CREATE_GAME_SHORTCUT:
	{
		// Get the game info to get our hands on the icon png
		Path gamePath(param1);
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath, GameInfoFlags::ICON);
		Path icoPath;
		if (info->icon.dataLoaded) {
			// Write the icon png out as a .ICO file so the shortcut can point to it

			// Savestate seems like a good enough place to put ico files.
			Path iconFolder = GetSysDirectory(PSPDirectories::DIRECTORY_SAVESTATE);

			icoPath = iconFolder / (info->id + ".ico");
			if (!File::Exists(icoPath)) {
				if (!W32Util::CreateICOFromPNGData((const uint8_t *)info->icon.data.data(), info->icon.data.size(), icoPath)) {
					ERROR_LOG(Log::System, "ICO creation failed");
					icoPath.clear();
				}
			}
		}
		return W32Util::CreateDesktopShortcut(param1, param2, icoPath);
	}
	case SystemRequestType::RUN_CALLBACK_IN_WNDPROC:
	{
		auto func = reinterpret_cast<void (*)(void *window, void *userdata)>(param3);
		void *userdata = reinterpret_cast<void *>(param4);
		MainWindow::RunCallbackInWndProc(func, userdata);
		return true;
	}
	default:
		return false;
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

// Don't swallow exceptions.
static void EnableCrashingOnCrashes() {
	typedef BOOL (WINAPI *tGetPolicy)(LPDWORD lpFlags);
	typedef BOOL (WINAPI *tSetPolicy)(DWORD dwFlags);
	const DWORD EXCEPTION_SWALLOWING = 0x1;

	HMODULE kernel32 = LoadLibrary(L"kernel32.dll");
	if (!kernel32)
		return;
	tGetPolicy pGetPolicy = (tGetPolicy)GetProcAddress(kernel32,
		"GetProcessUserModeExceptionPolicy");
	tSetPolicy pSetPolicy = (tSetPolicy)GetProcAddress(kernel32,
		"SetProcessUserModeExceptionPolicy");
	if (pGetPolicy && pSetPolicy) {
		DWORD dwFlags;
		if (pGetPolicy(&dwFlags)) {
			// Turn off the filter.
			pSetPolicy(dwFlags & ~EXCEPTION_SWALLOWING);
		}
	}
	FreeLibrary(kernel32);
}

void System_Toast(std::string_view text) {
	// Not-very-good implementation. Will normally not be used on Windows anyway.
	std::wstring str = ConvertUTF8ToWString(text);
	MessageBox(0, str.c_str(), L"Toast!", MB_ICONINFORMATION);
}

static std::string GetDefaultLangRegion() {
	wchar_t lcLangName[256] = {};

	// LOCALE_SNAME is only available in WinVista+
	if (0 != GetLocaleInfo(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcLangName, ARRAY_SIZE(lcLangName))) {
		std::string result = ConvertWStringToUTF8(lcLangName);
		std::replace(result.begin(), result.end(), '-', '_');
		return result;
	} else {
		// This should work on XP, but we may get numbers for some countries.
		if (0 != GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, lcLangName, ARRAY_SIZE(lcLangName))) {
			wchar_t lcRegion[256] = {};
			if (0 != GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, lcRegion, ARRAY_SIZE(lcRegion))) {
				return ConvertWStringToUTF8(lcLangName) + "_" + ConvertWStringToUTF8(lcRegion);
			}
		}
		// Unfortunate default.  We tried.
		return "en_US";
	}
}

static const int EXIT_CODE_VULKAN_WORKS = 42;

#ifndef _DEBUG
static bool DetectVulkanInExternalProcess() {
	std::wstring workingDirectory;
	std::wstring moduleFilename;
	W32Util::GetSelfExecuteParams(workingDirectory, moduleFilename);

	const wchar_t *cmdline = L"--vulkan-available-check";

	DWORD exitCode = 0;
	if (W32Util::ExecuteAndGetReturnCode(moduleFilename.c_str(), cmdline, workingDirectory.c_str(), &exitCode)) {
		return exitCode == EXIT_CODE_VULKAN_WORKS;
	} else {
		ERROR_LOG(Log::G3D, "Failed to detect Vulkan in external process somehow");
		return false;
	}
}
#endif

std::vector<std::wstring> GetWideCmdLine() {
	wchar_t **wargv;
	int wargc = -1;
	// This is used for the WM_USER_RESTART_EMUTHREAD path.
	if (!restartArgs.empty()) {
		std::wstring wargs = ConvertUTF8ToWString("PPSSPP " + restartArgs);
		wargv = CommandLineToArgvW(wargs.c_str(), &wargc);
		restartArgs.clear();
	} else {
		wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
	}

	std::vector<std::wstring> wideArgs(wargv, wargv + wargc);
	LocalFree(wargv);

	return wideArgs;
}

static void InitMemstickDirectory() {
	if (!g_Config.memStickDirectory.empty() && !g_Config.flash0Directory.empty())
		return;

	const Path &exePath = File::GetExeDirectory();
	// Mount a filesystem
	g_Config.flash0Directory = exePath / "assets/flash0";

	// Caller sets this to the Documents folder.
	const Path rootMyDocsPath = g_Config.internalDataDirectory;
	const Path myDocsPath = rootMyDocsPath / "PPSSPP";
	const Path installedFile = exePath / "installed.txt";
	const bool installed = File::Exists(installedFile);

	// If installed.txt exists(and we can determine the Documents directory)
	if (installed && !rootMyDocsPath.empty()) {
		FILE *fp = File::OpenCFile(installedFile, "rt");
		if (fp) {
			char temp[2048];
			char *tempStr = fgets(temp, sizeof(temp), fp);
			// Skip UTF-8 encoding bytes if there are any. There are 3 of them.
			if (tempStr && strncmp(tempStr, "\xEF\xBB\xBF", 3) == 0) {
				tempStr += 3;
			}
			std::string tempString = tempStr ? tempStr : "";
			if (!tempString.empty() && tempString.back() == '\n')
				tempString.resize(tempString.size() - 1);

			g_Config.memStickDirectory = Path(tempString);
			fclose(fp);
		}

		// Check if the file is empty first, before appending the slash.
		if (g_Config.memStickDirectory.empty())
			g_Config.memStickDirectory = myDocsPath;
	} else {
		g_Config.memStickDirectory = exePath / "memstick";
	}

	// Create the memstickpath before trying to write to it, and fall back on Documents yet again
	// if we can't make it.
	if (!File::Exists(g_Config.memStickDirectory)) {
		if (!File::CreateDir(g_Config.memStickDirectory))
			g_Config.memStickDirectory = myDocsPath;
		INFO_LOG(Log::Common, "Memstick directory not present, creating at '%s'", g_Config.memStickDirectory.c_str());
	}

	Path testFile = g_Config.memStickDirectory / "_writable_test.$$$";

	// If any directory is read-only, fall back to the Documents directory.
	// We're screwed anyway if we can't write to Documents, or can't detect it.
	if (!File::CreateEmptyFile(testFile))
		g_Config.memStickDirectory = myDocsPath;

	// Clean up our mess.
	if (File::Exists(testFile))
		File::Delete(testFile);
}

static void WinMainInit() {
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	net::Init();  // This needs to happen before we load the config. So on Windows we also run it in Main. It's fine to call multiple times.

	// Windows, API init stuff
	INITCOMMONCONTROLSEX comm;
	comm.dwSize = sizeof(comm);
	comm.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
	InitCommonControlsEx(&comm);

	EnableCrashingOnCrashes();

#ifdef _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	PROFILE_INIT();

#if PPSSPP_ARCH(AMD64) && defined(_MSC_VER) && _MSC_VER < 1900
	// FMA3 support in the 2013 CRT is broken on Vista and Windows 7 RTM (fixed in SP1). Just disable it.
	_set_FMA3_enable(0);
#endif

	InitDarkMode();
}

static void WinMainCleanup() {
	// This will ensure no further callbacks are called, which may prevent crashing.
	g_requestManager.Clear();
	net::Shutdown();
	CoUninitialize();

	if (g_Config.bRestartRequired) {
		// TODO: ExitAndRestart prevents the Config::~Config destructor from running,
		// which normally would have done this instance counter update.
		// ExitAndRestart calls ExitProcess which really bad, we should do something better that
		// allows us to fall out of main() properly.
		if (g_Config.bUpdatedInstanceCounter) {
			ShutdownInstanceCounter();
		}
		W32Util::ExitAndRestart(!restartArgs.empty(), restartArgs);
	}
}

int WINAPI WinMain(HINSTANCE _hInstance, HINSTANCE hPrevInstance, LPSTR szCmdLine, int iCmdShow) {
	std::vector<std::wstring> wideArgs = GetWideCmdLine();

	// Check for the Vulkan workaround before any serious init.
	for (size_t i = 1; i < wideArgs.size(); ++i) {
		if (wideArgs[i][0] == L'-') {
			// This should only be called by DetectVulkanInExternalProcess().
			if (wideArgs[i] == L"--vulkan-available-check") {
				// Just call it, this way it will crash here if it doesn't work.
				// (this is an external process.)
				bool result = VulkanMayBeAvailable();

				LogManager::Shutdown();
				WinMainCleanup();
				return result ? EXIT_CODE_VULKAN_WORKS : EXIT_FAILURE;
			}
		}
	}

	SetCurrentThreadName("Main");

	WinMainInit();

#ifndef _DEBUG
	bool showLog = false;
#else
	bool showLog = true;
#endif

	const Path &exePath = File::GetExeDirectory();
	g_VFS.Register("", new DirectoryReader(exePath / "assets"));
	g_VFS.Register("", new DirectoryReader(exePath));

	langRegion = GetDefaultLangRegion();
	osName = GetWindowsVersion() + " " + GetWindowsSystemArchitecture();

	std::string configFilename = "";
	const std::wstring configOption = L"--config=";

	std::string controlsConfigFilename = "";
	const std::wstring controlsOption = L"--controlconfig=";


	for (size_t i = 1; i < wideArgs.size(); ++i) {
		if (wideArgs[i][0] == L'\0')
			continue;
		if (wideArgs[i][0] == L'-') {
			if (wideArgs[i].find(configOption) != std::wstring::npos && wideArgs[i].size() > configOption.size()) {
				const std::wstring tempWide = wideArgs[i].substr(configOption.size());
				configFilename = ConvertWStringToUTF8(tempWide);
			}

			if (wideArgs[i].find(controlsOption) != std::wstring::npos && wideArgs[i].size() > controlsOption.size()) {
				const std::wstring tempWide = wideArgs[i].substr(controlsOption.size());
				controlsConfigFilename = ConvertWStringToUTF8(tempWide);
			}
		}
	}

	LogManager::Init(&g_Config.bEnableLogging);

	// On Win32 it makes more sense to initialize the system directories here
	// because the next place it was called was in the EmuThread, and it's too late by then.
	g_Config.internalDataDirectory = Path(W32Util::UserDocumentsPath());
	InitMemstickDirectory();
	CreateSysDirectories();


	// Load config up here, because those changes below would be overwritten
	// if it's not loaded here first.
	g_Config.SetSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.Load(configFilename.c_str(), controlsConfigFilename.c_str());

	bool debugLogLevel = false;

	const std::wstring gpuBackend = L"--graphics=";

	// The rest is handled in NativeInit().
	for (size_t i = 1; i < wideArgs.size(); ++i) {
		if (wideArgs[i][0] == L'\0')
			continue;

		if (wideArgs[i][0] == L'-') {
			switch (wideArgs[i][1]) {
			case L'l':
				showLog = true;
				g_Config.bEnableLogging = true;
				break;
			case L's':
				g_Config.bAutoRun = false;
				g_Config.bSaveSettings = false;
				break;
			case L'd':
				debugLogLevel = true;
				break;
			}

			if (wideArgs[i].find(gpuBackend) != std::wstring::npos && wideArgs[i].size() > gpuBackend.size()) {
				const std::wstring restOfOption = wideArgs[i].substr(gpuBackend.size());

				// Force software rendering off, as picking directx9 or gles implies HW acceleration.
				// Once software rendering supports Direct3D9/11, we can add more options for software,
				// such as "software-gles", "software-d3d9", and "software-d3d11", or something similar.
				// For now, software rendering force-activates OpenGL.
				if (restOfOption == L"directx9") {
					g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D9;
					g_Config.bSoftwareRendering = false;
				} else if (restOfOption == L"directx11") {
					g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;
					g_Config.bSoftwareRendering = false;
				} else if (restOfOption == L"gles") {
					g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
					g_Config.bSoftwareRendering = false;
				} else if (restOfOption == L"vulkan") {
					g_Config.iGPUBackend = (int)GPUBackend::VULKAN;
					g_Config.bSoftwareRendering = false;
				} else if (restOfOption == L"software") {
					g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
					g_Config.bSoftwareRendering = true;
				}
			}
		}
	}
#ifdef _DEBUG
	g_Config.bEnableLogging = true;
#endif

#ifndef _DEBUG
	// See #11719 - too many Vulkan drivers crash on basic init.
	if (g_Config.IsBackendEnabled(GPUBackend::VULKAN)) {
		VulkanSetAvailable(DetectVulkanInExternalProcess());
	}
#endif

	if (iCmdShow == SW_MAXIMIZE) {
		// Consider this to mean --fullscreen.
		g_Config.iForceFullScreen = 1;
	}

	// Consider at least the following cases before changing this code:
	//   - By default in Release, the console should be hidden by default even if logging is enabled.
	//   - By default in Debug, the console should be shown by default.
	//   - The -l switch is expected to show the log console, REGARDLESS of config settings.
	//   - It should be possible to log to a file without showing the console.
	LogManager::GetInstance()->GetConsoleListener()->Init(showLog, 150, 120, "PPSSPP Debug Console");

	if (debugLogLevel) {
		LogManager::GetInstance()->SetAllLogLevels(LogLevel::LDEBUG);
	}

	// This still seems to improve performance noticeably.
	timeBeginPeriod(1);

	ContextMenuInit(_hInstance);
	MainWindow::Init(_hInstance);
	MainWindow::Show(_hInstance);

	HWND hwndMain = MainWindow::GetHWND();

	//initialize custom controls
	CtrlDisAsmView::init();
	CtrlMemView::init();
	CtrlRegisterList::init();
#if PPSSPP_API(ANY_GL)
	CGEDebugger::Init();
#endif

	if (g_Config.bShowDebuggerOnLoad) {
		MainWindow::CreateDisasmWindow();
		disasmWindow->Show(g_Config.bShowDebuggerOnLoad, false);
	}

	const bool minimized = iCmdShow == SW_MINIMIZE || iCmdShow == SW_SHOWMINIMIZED || iCmdShow == SW_SHOWMINNOACTIVE;
	if (minimized) {
		MainWindow::Minimize();
	}

	g_inputManager.Init();

	// Emu thread (and render thread, if any) is always running!
	// Only OpenGL uses an externally managed render thread (due to GL's single-threaded context design). Vulkan
	// manages its own render thread.
	MainThread_Start(g_Config.iGPUBackend == (int)GPUBackend::OPENGL);
	InputDevice::BeginPolling();

	HACCEL hAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_ACCELS);
	HACCEL hDebugAccelTable = LoadAccelerators(_hInstance, (LPCTSTR)IDR_DEBUGACCELS);

	//so.. we're at the message pump of the GUI thread
	for (MSG msg; GetMessage(&msg, NULL, 0, 0); )	// for no quit
	{
		if (msg.message == WM_KEYDOWN)
		{
			//hack to enable/disable menu command accelerate keys
			MainWindow::UpdateCommands();

			//hack to make it possible to get to main window from floating windows with Esc
			if (msg.hwnd != hwndMain && msg.wParam == VK_ESCAPE)
				BringWindowToTop(hwndMain);
		}

		//Translate accelerators and dialog messages...
		HWND wnd;
		HACCEL accel;
		switch (g_activeWindow)
		{
		case WINDOW_MAINWINDOW:
			wnd = hwndMain;
			accel = g_Config.bSystemControls ? hAccelTable : NULL;
			break;
		case WINDOW_CPUDEBUGGER:
			wnd = disasmWindow ? disasmWindow->GetDlgHandle() : NULL;
			accel = g_Config.bSystemControls ? hDebugAccelTable : NULL;
			break;
		case WINDOW_GEDEBUGGER:
		default:
			wnd = NULL;
			accel = NULL;
			break;
		}

		if (!wnd || !accel || !TranslateAccelerator(wnd, accel, &msg)) {
			if (!DialogManager::IsDialogMessage(&msg)) {
				//and finally translate and dispatch
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	g_VFS.Clear();

	MainWindow::DestroyDebugWindows();
	DialogManager::DestroyAll();
	timeEndPeriod(1);

	LogManager::Shutdown();
	WinMainCleanup();

	return 0;
}

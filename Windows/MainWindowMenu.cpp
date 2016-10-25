#include <map>
#include <string>

#include "CommonWindows.h"
#include <shellapi.h>

#include "resource.h"

#include "i18n/i18n.h"
#include "util/text/utf8.h"
#include "base/NativeApp.h"

#include "gfx_es2/gpu_features.h"
#include "Common/Log.h"
#include "Common/LogManager.h"
#include "Common/ConsoleListener.h"
#include "GPU/GLES/TextureScalerGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "UI/OnScreenDisplay.h"
#include "GPU/Common/PostShader.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#include "Core/Config.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "UI/OnScreenDisplay.h"
#include "Windows/MainWindowMenu.h"
#include "Windows/MainWindow.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/InputBox.h"
#include "Windows/main.h"

#include "Core/HLE/sceUmd.h"
#include "Core/SaveState.h"
#include "Core/Core.h"

extern bool g_TakeScreenshot;

namespace MainWindow {
	extern HINSTANCE hInst;
	static const int numCPUs = 1;  // what?
	extern bool noFocusPause;
	static W32Util::AsyncBrowseDialog *browseDialog;
	static W32Util::AsyncBrowseDialog *browseImageDialog;
	static bool browsePauseAfter;

	static std::map<int, std::string> initialMenuKeys;
	static std::vector<std::string> availableShaders;
	static std::string menuLanguageID = "";
	static bool menuShaderInfoLoaded = false;
	std::vector<ShaderInfo> menuShaderInfo;

	LRESULT CALLBACK About(HWND, UINT, WPARAM, LPARAM);

	void SetIngameMenuItemStates(HMENU menu, const GlobalUIState state) {
		UINT menuEnable = state == UISTATE_INGAME ? MF_ENABLED : MF_GRAYED;
		UINT umdSwitchEnable = state == UISTATE_INGAME && getUMDReplacePermit() ? MF_ENABLED : MF_GRAYED;

		EnableMenuItem(menu, ID_FILE_SAVESTATEFILE, menuEnable);
		EnableMenuItem(menu, ID_FILE_LOADSTATEFILE, menuEnable);
		EnableMenuItem(menu, ID_FILE_QUICKSAVESTATE, menuEnable);
		EnableMenuItem(menu, ID_FILE_QUICKLOADSTATE, menuEnable);
		EnableMenuItem(menu, ID_TOGGLE_PAUSE, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_STOP, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_RESET, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_SWITCH_UMD, umdSwitchEnable);
		EnableMenuItem(menu, ID_DEBUG_LOADMAPFILE, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_SAVEMAPFILE, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_LOADSYMFILE, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_SAVESYMFILE, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_RESETSYMBOLTABLE, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_TAKESCREENSHOT, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_SHOWDEBUGSTATISTICS, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_EXTRACTFILE, menuEnable);

		// While playing, this pop up doesn't work - and probably doesn't make sense.
		EnableMenuItem(menu, ID_OPTIONS_LANGUAGE, state == UISTATE_INGAME ? MF_GRAYED : MF_ENABLED);
	}

	// These are used as an offset
	// to determine which menu item to change.
	// Make sure to count(from 0) the separators too, when dealing with submenus!!
	enum MenuItemPosition {
		// Main menus
		MENU_FILE = 0,
		MENU_EMULATION = 1,
		MENU_DEBUG = 2,
		MENU_OPTIONS = 3,
		MENU_HELP = 4,

		// File submenus
		SUBMENU_FILE_SAVESTATE_SLOT = 6,
		SUBMENU_FILE_RECORD = 11,

		// Emulation submenus
		SUBMENU_DISPLAY_ROTATION = 4,

		// Game Settings submenus
		SUBMENU_DISPLAY_LAYOUT = 7,
		SUBMENU_CUSTOM_SHADERS = 10,
		SUBMENU_RENDERING_RESOLUTION = 11,
		SUBMENU_WINDOW_SIZE = 12,
		SUBMENU_RENDERING_BACKEND = 13,
		SUBMENU_RENDERING_MODE = 14,
		SUBMENU_FRAME_SKIPPING = 15,
		SUBMENU_TEXTURE_FILTERING = 16,
		SUBMENU_BUFFER_FILTER = 17,
		SUBMENU_TEXTURE_SCALING = 18,
	};

	static std::string GetMenuItemText(HMENU menu, int menuID) {
		MENUITEMINFO menuInfo;
		memset(&menuInfo, 0, sizeof(menuInfo));
		menuInfo.cbSize = sizeof(MENUITEMINFO);
		menuInfo.fMask = MIIM_STRING;
		menuInfo.dwTypeData = 0;

		std::string retVal;
		if (GetMenuItemInfo(menu, menuID, MF_BYCOMMAND, &menuInfo) != FALSE) {
			wchar_t *buffer = new wchar_t[++menuInfo.cch];
			menuInfo.dwTypeData = buffer;
			GetMenuItemInfo(menu, menuID, MF_BYCOMMAND, &menuInfo);
			retVal = ConvertWStringToUTF8(menuInfo.dwTypeData);
			delete[] buffer;
		}

		return retVal;
	}

	const std::string &GetMenuItemInitialText(HMENU menu, const int menuID) {
		if (initialMenuKeys.find(menuID) == initialMenuKeys.end()) {
			initialMenuKeys[menuID] = GetMenuItemText(menu, menuID);
		}
		return initialMenuKeys[menuID];
	}

	void CreateHelpMenu(HMENU menu) {
		I18NCategory *des = GetI18NCategory("DesktopUI");

		const std::wstring help = ConvertUTF8ToWString(des->T("Help"));
		const std::wstring visitMainWebsite = ConvertUTF8ToWString(des->T("www.ppsspp.org"));
		const std::wstring visitForum = ConvertUTF8ToWString(des->T("PPSSPP Forums"));
		const std::wstring buyGold = ConvertUTF8ToWString(des->T("Buy Gold"));
		const std::wstring gitHub = ConvertUTF8ToWString(des->T("GitHub"));
		const std::wstring aboutPPSSPP = ConvertUTF8ToWString(des->T("About PPSSPP..."));

		// Simply remove the old help menu and create a new one.
		RemoveMenu(menu, MENU_HELP, MF_BYPOSITION);

		HMENU helpMenu = CreatePopupMenu();
		InsertMenu(menu, MENU_HELP, MF_POPUP | MF_STRING | MF_BYPOSITION, (UINT_PTR)helpMenu, help.c_str());

		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_OPENWEBSITE, visitMainWebsite.c_str());
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_OPENFORUM, visitForum.c_str());
		// Repeat the process for other languages, if necessary.
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_BUYGOLD, buyGold.c_str());
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_GITHUB, gitHub.c_str());
		AppendMenu(helpMenu, MF_SEPARATOR, 0, 0);
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_ABOUT, aboutPPSSPP.c_str());
	}

	void UpdateDynamicMenuCheckmarks(HMENU menu) {
		int item = ID_SHADERS_BASE + 1;

		for (size_t i = 0; i < availableShaders.size(); i++)
			CheckMenuItem(menu, item++, ((g_Config.sPostShaderName == availableShaders[i]) ? MF_CHECKED : MF_UNCHECKED));
	}

	bool CreateShadersSubmenu(HMENU menu) {
		// We only reload this initially and when a menu is actually opened.
		if (!menuShaderInfoLoaded) {
			ReloadAllPostShaderInfo();
			menuShaderInfoLoaded = true;
		}
		std::vector<ShaderInfo> info = GetAllPostShaderInfo();

		if (menuShaderInfo.size() == info.size() && std::equal(info.begin(), info.end(), menuShaderInfo.begin())) {
			return false;
		}

		I18NCategory *des = GetI18NCategory("DesktopUI");
		I18NCategory *ps = GetI18NCategory("PostShaders");
		const std::wstring key = ConvertUTF8ToWString(des->T("Postprocessing Shader"));

		HMENU optionsMenu = GetSubMenu(menu, MENU_OPTIONS);

		HMENU shaderMenu = CreatePopupMenu();

		RemoveMenu(optionsMenu, SUBMENU_CUSTOM_SHADERS, MF_BYPOSITION);
		InsertMenu(optionsMenu, SUBMENU_CUSTOM_SHADERS, MF_POPUP | MF_STRING | MF_BYPOSITION, (UINT_PTR)shaderMenu, key.c_str());

		int item = ID_SHADERS_BASE + 1;
		int checkedStatus = -1;

		const char *translatedShaderName = nullptr;

		availableShaders.clear();
		for (auto i = info.begin(); i != info.end(); ++i) {
			checkedStatus = MF_UNCHECKED;
			availableShaders.push_back(i->section);
			if (g_Config.sPostShaderName == i->section) {
				checkedStatus = MF_CHECKED;
			}

			translatedShaderName = ps->T(i->section.c_str());

			AppendMenu(shaderMenu, MF_STRING | MF_BYPOSITION | checkedStatus, item++, ConvertUTF8ToWString(translatedShaderName).c_str());
		}

		menuShaderInfo = info;
		return true;
	}

	static void _TranslateMenuItem(const HMENU hMenu, const int menuIDOrPosition, const char *key, bool byCommand = false, const std::wstring& accelerator = L"") {
		I18NCategory *des = GetI18NCategory("DesktopUI");

		std::wstring translated = ConvertUTF8ToWString(des->T(key));
		translated.append(accelerator);

		u32 flags = MF_STRING | (byCommand ? MF_BYCOMMAND : MF_BYPOSITION);

		ModifyMenu(hMenu, menuIDOrPosition, flags, menuIDOrPosition, translated.c_str());
	}

	void TranslateMenuItem(HMENU menu, int menuID, const std::wstring& accelerator = L"", const char *key = "") {
		if (key == nullptr || !strcmp(key, ""))
			_TranslateMenuItem(menu, menuID, GetMenuItemInitialText(menu, menuID).c_str(), true, accelerator);
		else
			_TranslateMenuItem(menu, menuID, key, true, accelerator);
	}

	void TranslateMenu(HMENU menu, const char *key, const MenuItemPosition mainMenuPosition, const std::wstring& accelerator = L"") {
		_TranslateMenuItem(menu, mainMenuPosition, key, false, accelerator);
	}

	void TranslateSubMenu(HMENU menu, const char *key, const MenuItemPosition mainMenuItem, const MenuItemPosition subMenuItem, const std::wstring& accelerator = L"") {
		_TranslateMenuItem(GetSubMenu(menu, mainMenuItem), subMenuItem, key, false, accelerator);
	}

	void DoTranslateMenus(HWND hWnd, HMENU menu) {
		// Menu headers and submenu headers don't have resource IDs,
		// So we have to hardcode strings here, unfortunately.
		TranslateMenu(menu, "File", MENU_FILE);
		TranslateMenu(menu, "Emulation", MENU_EMULATION);
		TranslateMenu(menu, "Debugging", MENU_DEBUG);
		TranslateMenu(menu, "Game Settings", MENU_OPTIONS);
		TranslateMenu(menu, "Help", MENU_HELP);

		// File menu
		TranslateMenuItem(menu, ID_FILE_LOAD);
		TranslateMenuItem(menu, ID_FILE_LOAD_DIR);
		TranslateMenuItem(menu, ID_FILE_LOAD_MEMSTICK);
		TranslateMenuItem(menu, ID_FILE_MEMSTICK);
		TranslateSubMenu(menu, "Savestate Slot", MENU_FILE, SUBMENU_FILE_SAVESTATE_SLOT, L"\tF3");
		TranslateMenuItem(menu, ID_FILE_QUICKLOADSTATE, L"\tF4");
		TranslateMenuItem(menu, ID_FILE_QUICKSAVESTATE, L"\tF2");
		TranslateMenuItem(menu, ID_FILE_LOADSTATEFILE);
		TranslateMenuItem(menu, ID_FILE_SAVESTATEFILE);
		TranslateSubMenu(menu, "Record", MENU_FILE, SUBMENU_FILE_RECORD);
		TranslateMenuItem(menu, ID_FILE_EXIT, L"\tAlt+F4");

		// Emulation menu
		TranslateMenuItem(menu, ID_TOGGLE_PAUSE, L"\tF8", "Pause");
		TranslateMenuItem(menu, ID_EMULATION_STOP, L"\tCtrl+W");
		TranslateMenuItem(menu, ID_EMULATION_RESET, L"\tCtrl+B");
		TranslateMenuItem(menu, ID_EMULATION_SWITCH_UMD, L"\tCtrl+U", "Switch UMD");
		TranslateSubMenu(menu, "Display Rotation", MENU_EMULATION, SUBMENU_DISPLAY_ROTATION);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_H);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_V);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_H_R);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_V_R);

		// Debug menu
		TranslateMenuItem(menu, ID_DEBUG_LOADMAPFILE);
		TranslateMenuItem(menu, ID_DEBUG_SAVEMAPFILE);
		TranslateMenuItem(menu, ID_DEBUG_LOADSYMFILE);
		TranslateMenuItem(menu, ID_DEBUG_SAVESYMFILE);
		TranslateMenuItem(menu, ID_DEBUG_RESETSYMBOLTABLE);
		TranslateMenuItem(menu, ID_DEBUG_DUMPNEXTFRAME);
		TranslateMenuItem(menu, ID_DEBUG_TAKESCREENSHOT, L"\tF12");
		TranslateMenuItem(menu, ID_DEBUG_SHOWDEBUGSTATISTICS);
		TranslateMenuItem(menu, ID_DEBUG_IGNOREILLEGALREADS);
		TranslateMenuItem(menu, ID_DEBUG_RUNONLOAD);
		TranslateMenuItem(menu, ID_DEBUG_DISASSEMBLY, L"\tCtrl+D");
		TranslateMenuItem(menu, ID_DEBUG_GEDEBUGGER, L"\tCtrl+G");
		TranslateMenuItem(menu, ID_DEBUG_EXTRACTFILE);
		TranslateMenuItem(menu, ID_DEBUG_LOG, L"\tCtrl+L");
		TranslateMenuItem(menu, ID_DEBUG_MEMORYVIEW, L"\tCtrl+M");

		// Options menu
		TranslateMenuItem(menu, ID_OPTIONS_LANGUAGE);
		TranslateMenuItem(menu, ID_OPTIONS_TOPMOST);
		TranslateMenuItem(menu, ID_OPTIONS_PAUSE_FOCUS);
		TranslateMenuItem(menu, ID_OPTIONS_IGNOREWINKEY);
		TranslateMenuItem(menu, ID_OPTIONS_MORE_SETTINGS);
		TranslateMenuItem(menu, ID_OPTIONS_CONTROLS);
		TranslateMenuItem(menu, ID_OPTIONS_DISPLAY_LAYOUT);

		// Movie menu
		TranslateMenuItem(menu, ID_FILE_DUMPFRAMES);
		TranslateMenuItem(menu, ID_FILE_USEFFV1);
		TranslateMenuItem(menu, ID_FILE_DUMPAUDIO);

		// Skip display multipliers x1-x10
		TranslateMenuItem(menu, ID_OPTIONS_FULLSCREEN, L"\tAlt+Return, F11");
		TranslateMenuItem(menu, ID_OPTIONS_VSYNC);
		TranslateSubMenu(menu, "Postprocessing Shader", MENU_OPTIONS, SUBMENU_CUSTOM_SHADERS);
		TranslateSubMenu(menu, "Rendering Resolution", MENU_OPTIONS, SUBMENU_RENDERING_RESOLUTION, L"\tCtrl+1");
		TranslateMenuItem(menu, ID_OPTIONS_SCREENAUTO);
		// Skip rendering resolution 2x-5x..
		TranslateSubMenu(menu, "Window Size", MENU_OPTIONS, SUBMENU_WINDOW_SIZE);
		// Skip window size 1x-4x..
		TranslateSubMenu(menu, "Backend", MENU_OPTIONS, SUBMENU_RENDERING_BACKEND);
		TranslateMenuItem(menu, ID_OPTIONS_DIRECT3D11);
		TranslateMenuItem(menu, ID_OPTIONS_DIRECT3D9);
		TranslateMenuItem(menu, ID_OPTIONS_OPENGL);
		TranslateSubMenu(menu, "Rendering Mode", MENU_OPTIONS, SUBMENU_RENDERING_MODE);
		TranslateMenuItem(menu, ID_OPTIONS_NONBUFFEREDRENDERING);
		TranslateMenuItem(menu, ID_OPTIONS_BUFFEREDRENDERING);
		TranslateMenuItem(menu, ID_OPTIONS_READFBOTOMEMORYCPU);
		TranslateMenuItem(menu, ID_OPTIONS_READFBOTOMEMORYGPU);
		TranslateSubMenu(menu, "Frame Skipping", MENU_OPTIONS, SUBMENU_FRAME_SKIPPING, L"\tF7");
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIP_AUTO);
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIP_0);
		// Skip frameskipping 1-8..
		TranslateSubMenu(menu, "Texture Filtering", MENU_OPTIONS, SUBMENU_TEXTURE_FILTERING);
		TranslateMenuItem(menu, ID_OPTIONS_TEXTUREFILTERING_AUTO);
		TranslateMenuItem(menu, ID_OPTIONS_NEARESTFILTERING);
		TranslateMenuItem(menu, ID_OPTIONS_LINEARFILTERING);
		TranslateMenuItem(menu, ID_OPTIONS_LINEARFILTERING_CG);
		TranslateSubMenu(menu, "Screen Scaling Filter", MENU_OPTIONS, SUBMENU_BUFFER_FILTER);
		TranslateMenuItem(menu, ID_OPTIONS_BUFLINEARFILTER);
		TranslateMenuItem(menu, ID_OPTIONS_BUFNEARESTFILTER);
		TranslateSubMenu(menu, "Texture Scaling", MENU_OPTIONS, SUBMENU_TEXTURE_SCALING);
		TranslateMenuItem(menu, ID_TEXTURESCALING_OFF);
		// Skip texture scaling 2x-5x...
		TranslateMenuItem(menu, ID_TEXTURESCALING_XBRZ);
		TranslateMenuItem(menu, ID_TEXTURESCALING_HYBRID);
		TranslateMenuItem(menu, ID_TEXTURESCALING_BICUBIC);
		TranslateMenuItem(menu, ID_TEXTURESCALING_HYBRID_BICUBIC);
		TranslateMenuItem(menu, ID_TEXTURESCALING_DEPOSTERIZE);
		TranslateMenuItem(menu, ID_OPTIONS_HARDWARETRANSFORM);
		TranslateMenuItem(menu, ID_OPTIONS_VERTEXCACHE);
		TranslateMenuItem(menu, ID_OPTIONS_SHOWFPS);
		TranslateMenuItem(menu, ID_EMULATION_SOUND);
		TranslateMenuItem(menu, ID_EMULATION_CHEATS, L"\tCtrl+T");
		TranslateMenuItem(menu, ID_EMULATION_CHAT, L"\tCtrl+C");

		// Help menu: it's translated in CreateHelpMenu.
		CreateHelpMenu(menu);
	}

	void TranslateMenus(HWND hWnd, HMENU menu) {
		bool changed = false;

		const std::string curLanguageID = i18nrepo.LanguageID();
		if (curLanguageID != menuLanguageID) {
			DoTranslateMenus(hWnd, menu);
			menuLanguageID = curLanguageID;
			changed = true;
		}

		if (CreateShadersSubmenu(menu)) {
			changed = true;
		}

		if (changed) {
			DrawMenuBar(hWnd);
		}
	}

	void BrowseAndBoot(std::string defaultPath, bool browseDirectory) {
		static std::wstring filter = L"All supported file types (*.iso *.cso *.pbp *.elf *.prx *.zip)|*.pbp;*.elf;*.iso;*.cso;*.prx;*.zip|PSP ROMs (*.iso *.cso *.pbp *.elf *.prx)|*.pbp;*.elf;*.iso;*.cso;*.prx|Homebrew/Demos installers (*.zip)|*.zip|All files (*.*)|*.*||";
		for (int i = 0; i < (int)filter.length(); i++) {
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		browsePauseAfter = false;
		if (GetUIState() == UISTATE_INGAME) {
			browsePauseAfter = Core_IsStepping();
			if (!browsePauseAfter)
				Core_EnableStepping(true);
		}

		W32Util::MakeTopMost(GetHWND(), false);
		if (browseDirectory) {
			browseDialog = new W32Util::AsyncBrowseDialog(GetHWND(), WM_USER_BROWSE_BOOT_DONE, L"Choose directory");
		} else {
			browseDialog = new W32Util::AsyncBrowseDialog(W32Util::AsyncBrowseDialog::OPEN, GetHWND(), WM_USER_BROWSE_BOOT_DONE, L"LoadFile", ConvertUTF8ToWString(defaultPath), filter, L"*.pbp;*.elf;*.iso;*.cso;");
		}
	}

	void BrowseAndBootDone() {
		std::string filename;
		if (!browseDialog->GetResult(filename)) {
			if (!browsePauseAfter) {
				Core_EnableStepping(false);
			}
		} else {
			if (GetUIState() == UISTATE_INGAME || GetUIState() == UISTATE_PAUSEMENU) {
				Core_EnableStepping(false);
			}

			// TODO: What is this for / what does it fix?
			if (browseDialog->GetType() != W32Util::AsyncBrowseDialog::DIR) {
				// Decode the filename with fullpath.
				char drive[MAX_PATH];
				char dir[MAX_PATH];
				char fname[MAX_PATH];
				char ext[MAX_PATH];
				_splitpath(filename.c_str(), drive, dir, fname, ext);

				filename = std::string(drive) + std::string(dir) + std::string(fname) + std::string(ext);
			}

			filename = ReplaceAll(filename, "\\", "/");
			NativeMessageReceived("boot", filename.c_str());
		}

		W32Util::MakeTopMost(GetHWND(), g_Config.bTopMost);

		delete browseDialog;
		browseDialog = 0;
	}

	void BrowseBackground() {
		static std::wstring filter = L"All supported images (*.jpg *.png)|*.jpg;*.png|All files (*.*)|*.*||";
		for (size_t i = 0; i < filter.length(); i++) {
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		W32Util::MakeTopMost(GetHWND(), false);
		browseImageDialog = new W32Util::AsyncBrowseDialog(W32Util::AsyncBrowseDialog::OPEN, GetHWND(), WM_USER_BROWSE_BG_DONE, L"LoadFile", L"", filter, L"*.jpg;*.png;");
	}

	void BrowseBackgroundDone() {
		std::string filename;
		if (browseImageDialog->GetResult(filename)) {
			std::wstring src = ConvertUTF8ToWString(filename);
			std::wstring dest;
			if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".jpg") {
				dest = ConvertUTF8ToWString(GetSysDirectory(DIRECTORY_SYSTEM) + "background.jpg");
			} else {
				dest = ConvertUTF8ToWString(GetSysDirectory(DIRECTORY_SYSTEM) + "background.png");
			}

			CopyFileW(src.c_str(), dest.c_str(), FALSE);
			NativeMessageReceived("bgImage_updated", "");
		}

		W32Util::MakeTopMost(GetHWND(), g_Config.bTopMost);

		delete browseImageDialog;
		browseImageDialog = nullptr;
	}

	static void UmdSwitchAction() {
		std::string fn;
		std::string filter = "PSP ROMs (*.iso *.cso *.pbp *.elf)|*.pbp;*.elf;*.iso;*.cso;*.prx|All files (*.*)|*.*||";

		for (int i = 0; i < (int)filter.length(); i++) {
			if (filter[i] == '|')
				filter[i] = '\0';
		}

		if (W32Util::BrowseForFileName(true, GetHWND(), L"Switch Umd", 0, ConvertUTF8ToWString(filter).c_str(), L"*.pbp;*.elf;*.iso;*.cso;", fn)) {
			fn = ReplaceAll(fn, "\\", "/");
			__UmdReplace(fn);
		}
	}

	static void setScreenRotation(int rotation) {
		g_Config.iInternalScreenRotation = rotation;
	}

	static void SaveStateActionFinished(bool result, const std::string &message, void *userdata) {
		if (!message.empty()) {
			osm.Show(message, 2.0);
		}
		PostMessage(MainWindow::GetHWND(), WM_USER_SAVESTATE_FINISH, 0, 0);
	}

	// not static
	void setTexScalingMultiplier(int level) {
		g_Config.iTexScalingLevel = level;
		NativeMessageReceived("gpu_clearCache", "");
	}

	static void setTexFiltering(int type) {
		g_Config.iTexFiltering = type;
	}

	static void setBufFilter(int type) {
		g_Config.iBufFilter = type;
	}

	static void setTexScalingType(int type) {
		g_Config.iTexScalingType = type;
		NativeMessageReceived("gpu_clearCache", "");
	}

	static void setRenderingMode(int mode) {
		if (mode >= FB_NON_BUFFERED_MODE)
			g_Config.iRenderingMode = mode;
		else {
			if (++g_Config.iRenderingMode > FB_READFBOMEMORY_GPU)
				g_Config.iRenderingMode = FB_NON_BUFFERED_MODE;
		}

		I18NCategory *gr = GetI18NCategory("Graphics");

		switch (g_Config.iRenderingMode) {
		case FB_NON_BUFFERED_MODE:
			osm.Show(gr->T("Non-Buffered Rendering"));
			g_Config.bAutoFrameSkip = false;
			break;

		case FB_BUFFERED_MODE:
			osm.Show(gr->T("Buffered Rendering"));
			break;

		case FB_READFBOMEMORY_CPU:
			osm.Show(gr->T("Read Framebuffers To Memory (CPU)"));
			break;

		case FB_READFBOMEMORY_GPU:
			osm.Show(gr->T("Read Framebuffers To Memory (GPU)"));
			break;
		}

		NativeMessageReceived("gpu_resized", "");
	}

	static void setFpsLimit(int fps) {
		g_Config.iFpsLimit = fps;
	}

	static void setFrameSkipping(int framesToSkip = -1) {
		if (framesToSkip >= FRAMESKIP_OFF)
			g_Config.iFrameSkip = framesToSkip;
		else {
			if (++g_Config.iFrameSkip > FRAMESKIP_MAX)
				g_Config.iFrameSkip = FRAMESKIP_OFF;
		}

		I18NCategory *gr = GetI18NCategory("Graphics");

		std::ostringstream messageStream;
		messageStream << gr->T("Frame Skipping") << ":" << " ";

		if (g_Config.iFrameSkip == FRAMESKIP_OFF)
			messageStream << gr->T("Off");
		else
			messageStream << g_Config.iFrameSkip;

		osm.Show(messageStream.str());
	}

	static void enableCheats(bool cheats) {
		g_Config.bEnableCheats = cheats;
	}

	static void setDisplayOptions(int options) {
		g_Config.iSmallDisplayZoomType = options;
		NativeMessageReceived("gpu_resized", "");
	}

	static void RestartApp() {
		if (IsDebuggerPresent()) {
			PostMessage(MainWindow::GetHWND(), WM_USER_RESTART_EMUTHREAD, 0, 0);
		} else {
			g_Config.bRestartRequired = true;
			PostMessage(MainWindow::GetHWND(), WM_CLOSE, 0, 0);
		}
	}

	void MainWindowMenu_Process(HWND hWnd, WPARAM wParam) {
		std::string fn;

		I18NCategory *gr = GetI18NCategory("Graphics");

		int wmId = LOWORD(wParam);
		int wmEvent = HIWORD(wParam);
		// Parse the menu selections:
		switch (wmId) {
		case ID_FILE_LOAD:
			BrowseAndBoot("");
			break;

		case ID_FILE_LOAD_DIR:
			BrowseAndBoot("", true);
			break;

		case ID_FILE_LOAD_MEMSTICK:
			BrowseAndBoot(GetSysDirectory(DIRECTORY_GAME));
			break;

		case ID_FILE_MEMSTICK:
			ShellExecute(NULL, L"open", ConvertUTF8ToWString(g_Config.memStickDirectory).c_str(), 0, 0, SW_SHOW);
			break;

		case ID_TOGGLE_PAUSE:
			if (GetUIState() == UISTATE_PAUSEMENU) {
				// Causes hang
				//NativeMessageReceived("run", "");

				if (disasmWindow[0])
					SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
			} else if (Core_IsStepping()) { // It is paused, then continue to run.
				if (disasmWindow[0])
					SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
				else
					Core_EnableStepping(false);
			} else {
				if (disasmWindow[0])
					SendMessage(disasmWindow[0]->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
				else
					Core_EnableStepping(true);
			}
			noFocusPause = !noFocusPause;	// If we pause, override pause on lost focus
			break;

		case ID_EMULATION_STOP:
			if (Core_IsStepping())
				Core_EnableStepping(false);

			Core_Stop();
			NativeMessageReceived("stop", "");
			Core_WaitInactive();
			break;

		case ID_EMULATION_RESET:
			NativeMessageReceived("reset", "");
			Core_EnableStepping(false);
			break;

		case ID_EMULATION_SWITCH_UMD:
			UmdSwitchAction();
			break;

		case ID_EMULATION_ROTATION_H:                 setScreenRotation(ROTATION_LOCKED_HORIZONTAL); break;
		case ID_EMULATION_ROTATION_V:                 setScreenRotation(ROTATION_LOCKED_VERTICAL); break;
		case ID_EMULATION_ROTATION_H_R:               setScreenRotation(ROTATION_LOCKED_HORIZONTAL180); break;
		case ID_EMULATION_ROTATION_V_R:               setScreenRotation(ROTATION_LOCKED_VERTICAL180); break;

		case ID_EMULATION_CHEATS:
			g_Config.bEnableCheats = !g_Config.bEnableCheats;
			osm.ShowOnOff(gr->T("Cheats"), g_Config.bEnableCheats);
			break;
		case ID_EMULATION_CHAT:
			if (GetUIState() == UISTATE_INGAME) {
				NativeMessageReceived("chat screen", "");
			}
			break;
		case ID_FILE_LOADSTATEFILE:
			if (W32Util::BrowseForFileName(true, hWnd, L"Load state", 0, L"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0", L"ppst", fn)) {
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::Load(fn, SaveStateActionFinished);
			}
			break;

		case ID_FILE_SAVESTATEFILE:
			if (W32Util::BrowseForFileName(false, hWnd, L"Save state", 0, L"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0", L"ppst", fn)) {
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::Save(fn, SaveStateActionFinished);
			}
			break;

		case ID_FILE_SAVESTATE_NEXT_SLOT:
		{
			SaveState::NextSlot();
			NativeMessageReceived("savestate_displayslot", "");
			break;
		}

		case ID_FILE_SAVESTATE_NEXT_SLOT_HC:
		{
			if (KeyMap::g_controllerMap[VIRTKEY_NEXT_SLOT].empty())
			{
				SaveState::NextSlot();
				NativeMessageReceived("savestate_displayslot", "");
			}
			break;
		}

		case ID_FILE_SAVESTATE_SLOT_1: g_Config.iCurrentStateSlot = 0; break;
		case ID_FILE_SAVESTATE_SLOT_2: g_Config.iCurrentStateSlot = 1; break;
		case ID_FILE_SAVESTATE_SLOT_3: g_Config.iCurrentStateSlot = 2; break;
		case ID_FILE_SAVESTATE_SLOT_4: g_Config.iCurrentStateSlot = 3; break;
		case ID_FILE_SAVESTATE_SLOT_5: g_Config.iCurrentStateSlot = 4; break;

		case ID_FILE_QUICKLOADSTATE:
		{
			SetCursor(LoadCursor(0, IDC_WAIT));
			SaveState::LoadSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
			break;
		}

		case ID_FILE_QUICKLOADSTATE_HC:
		{
			if (KeyMap::g_controllerMap[VIRTKEY_LOAD_STATE].empty())
			{
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::LoadSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
			}
			break;
		}
		case ID_FILE_QUICKSAVESTATE:
		{
			SetCursor(LoadCursor(0, IDC_WAIT));
			SaveState::SaveSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
			break;
		}

		case ID_FILE_QUICKSAVESTATE_HC:
		{
			if (KeyMap::g_controllerMap[VIRTKEY_SAVE_STATE].empty())
			{
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::SaveSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
				break;
			}
		}

		case ID_OPTIONS_LANGUAGE:
			NativeMessageReceived("language screen", "");
			break;

		case ID_OPTIONS_IGNOREWINKEY:
			g_Config.bIgnoreWindowsKey = !g_Config.bIgnoreWindowsKey;
			break;

		case ID_OPTIONS_SCREENAUTO: SetInternalResolution(RESOLUTION_AUTO); break;
		case ID_OPTIONS_SCREEN1X:   SetInternalResolution(RESOLUTION_NATIVE); break;
		case ID_OPTIONS_SCREEN2X:   SetInternalResolution(RESOLUTION_2X); break;
		case ID_OPTIONS_SCREEN3X:   SetInternalResolution(RESOLUTION_3X); break;
		case ID_OPTIONS_SCREEN4X:   SetInternalResolution(RESOLUTION_4X); break;
		case ID_OPTIONS_SCREEN5X:   SetInternalResolution(RESOLUTION_5X); break;
		case ID_OPTIONS_SCREEN6X:   SetInternalResolution(RESOLUTION_6X); break;
		case ID_OPTIONS_SCREEN7X:   SetInternalResolution(RESOLUTION_7X); break;
		case ID_OPTIONS_SCREEN8X:   SetInternalResolution(RESOLUTION_8X); break;
		case ID_OPTIONS_SCREEN9X:   SetInternalResolution(RESOLUTION_9X); break;
		case ID_OPTIONS_SCREEN10X:   SetInternalResolution(RESOLUTION_MAX); break;

		case ID_OPTIONS_WINDOW1X:   SetWindowSize(1); break;
		case ID_OPTIONS_WINDOW2X:   SetWindowSize(2); break;
		case ID_OPTIONS_WINDOW3X:   SetWindowSize(3); break;
		case ID_OPTIONS_WINDOW4X:   SetWindowSize(4); break;

		case ID_OPTIONS_RESOLUTIONDUMMY:
		{
			SetInternalResolution();
			break;
		}

		case ID_OPTIONS_VSYNC:
			g_Config.bVSync = !g_Config.bVSync;
			break;

		case ID_OPTIONS_FRAMESKIP_AUTO:
			g_Config.bAutoFrameSkip = !g_Config.bAutoFrameSkip;
			if (g_Config.bAutoFrameSkip && g_Config.iRenderingMode == FB_NON_BUFFERED_MODE)
				g_Config.iRenderingMode = FB_BUFFERED_MODE;
			break;

		case ID_TEXTURESCALING_AUTO: setTexScalingMultiplier(TEXSCALING_AUTO); break;
		case ID_TEXTURESCALING_OFF:  setTexScalingMultiplier(TEXSCALING_OFF); break;
		case ID_TEXTURESCALING_2X:   setTexScalingMultiplier(TEXSCALING_2X); break;
		case ID_TEXTURESCALING_3X:   setTexScalingMultiplier(TEXSCALING_3X); break;
		case ID_TEXTURESCALING_4X:   setTexScalingMultiplier(TEXSCALING_4X); break;
		case ID_TEXTURESCALING_5X:   setTexScalingMultiplier(TEXSCALING_MAX); break;

		case ID_TEXTURESCALING_XBRZ:            setTexScalingType(TextureScalerCommon::XBRZ); break;
		case ID_TEXTURESCALING_HYBRID:          setTexScalingType(TextureScalerCommon::HYBRID); break;
		case ID_TEXTURESCALING_BICUBIC:         setTexScalingType(TextureScalerCommon::BICUBIC); break;
		case ID_TEXTURESCALING_HYBRID_BICUBIC:  setTexScalingType(TextureScalerCommon::HYBRID_BICUBIC); break;

		case ID_TEXTURESCALING_DEPOSTERIZE:
			g_Config.bTexDeposterize = !g_Config.bTexDeposterize;
			NativeMessageReceived("gpu_clearCache", "");
			break;

		case ID_OPTIONS_DIRECT3D9:
			g_Config.iGPUBackend = GPU_BACKEND_DIRECT3D9;
			RestartApp();
			break;

		case ID_OPTIONS_DIRECT3D11:
			g_Config.iGPUBackend = GPU_BACKEND_DIRECT3D11;
			RestartApp();
			break;

		case ID_OPTIONS_OPENGL:
			g_Config.iGPUBackend = GPU_BACKEND_OPENGL;
			RestartApp();
			break;

		case ID_OPTIONS_VULKAN:
			g_Config.iGPUBackend = GPU_BACKEND_VULKAN;
			RestartApp();
			break;

		case ID_OPTIONS_NONBUFFEREDRENDERING:   setRenderingMode(FB_NON_BUFFERED_MODE); break;
		case ID_OPTIONS_BUFFEREDRENDERING:      setRenderingMode(FB_BUFFERED_MODE); break;
		case ID_OPTIONS_READFBOTOMEMORYCPU:     setRenderingMode(FB_READFBOMEMORY_CPU); break;
		case ID_OPTIONS_READFBOTOMEMORYGPU:     setRenderingMode(FB_READFBOMEMORY_GPU); break;

		case ID_DEBUG_SHOWDEBUGSTATISTICS:
			g_Config.bShowDebugStats = !g_Config.bShowDebugStats;
			NativeMessageReceived("clear jit", "");
			break;

		case ID_OPTIONS_HARDWARETRANSFORM:
			g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
			osm.ShowOnOff(gr->T("Hardware Transform"), g_Config.bHardwareTransform);
			break;

		case ID_OPTIONS_DISPLAY_LAYOUT:
			NativeMessageReceived("display layout editor", "");
			break;


		case ID_OPTIONS_FRAMESKIP_0:    setFrameSkipping(FRAMESKIP_OFF); break;
		case ID_OPTIONS_FRAMESKIP_1:    setFrameSkipping(FRAMESKIP_1); break;
		case ID_OPTIONS_FRAMESKIP_2:    setFrameSkipping(FRAMESKIP_2); break;
		case ID_OPTIONS_FRAMESKIP_3:    setFrameSkipping(FRAMESKIP_3); break;
		case ID_OPTIONS_FRAMESKIP_4:    setFrameSkipping(FRAMESKIP_4); break;
		case ID_OPTIONS_FRAMESKIP_5:    setFrameSkipping(FRAMESKIP_5); break;
		case ID_OPTIONS_FRAMESKIP_6:    setFrameSkipping(FRAMESKIP_6); break;
		case ID_OPTIONS_FRAMESKIP_7:    setFrameSkipping(FRAMESKIP_7); break;
		case ID_OPTIONS_FRAMESKIP_8:    setFrameSkipping(FRAMESKIP_MAX); break;

		case ID_OPTIONS_FRAMESKIPDUMMY:
			setFrameSkipping();
			break;

		case ID_FILE_EXIT:
			PostMessage(hWnd, WM_CLOSE, 0, 0);
			break;

		case ID_DEBUG_RUNONLOAD:
			g_Config.bAutoRun = !g_Config.bAutoRun;
			break;

		case ID_DEBUG_DUMPNEXTFRAME:
			NativeMessageReceived("gpu dump next frame", "");
			break;

		case ID_DEBUG_LOADMAPFILE:
			if (W32Util::BrowseForFileName(true, hWnd, L"Load .ppmap", 0, L"Maps\0*.ppmap\0All files\0*.*\0\0", L"ppmap", fn)) {
				g_symbolMap->LoadSymbolMap(fn.c_str());

				if (disasmWindow[0])
					disasmWindow[0]->NotifyMapLoaded();

				if (memoryWindow[0])
					memoryWindow[0]->NotifyMapLoaded();
			}
			break;

		case ID_DEBUG_SAVEMAPFILE:
			if (W32Util::BrowseForFileName(false, hWnd, L"Save .ppmap", 0, L"Maps\0*.ppmap\0All files\0*.*\0\0", L"ppmap", fn))
				g_symbolMap->SaveSymbolMap(fn.c_str());
			break;

		case ID_DEBUG_LOADSYMFILE:
			if (W32Util::BrowseForFileName(true, hWnd, L"Load .sym", 0, L"Symbols\0*.sym\0All files\0*.*\0\0", L"sym", fn)) {
				g_symbolMap->LoadNocashSym(fn.c_str());

				if (disasmWindow[0])
					disasmWindow[0]->NotifyMapLoaded();

				if (memoryWindow[0])
					memoryWindow[0]->NotifyMapLoaded();
			}
			break;

		case ID_DEBUG_SAVESYMFILE:
			if (W32Util::BrowseForFileName(false, hWnd, L"Save .sym", 0, L"Symbols\0*.sym\0All files\0*.*\0\0", L"sym", fn))
				g_symbolMap->SaveNocashSym(fn.c_str());
			break;

		case ID_DEBUG_RESETSYMBOLTABLE:
			g_symbolMap->Clear();

			for (int i = 0; i < numCPUs; i++)
				if (disasmWindow[i])
					disasmWindow[i]->NotifyMapLoaded();

			for (int i = 0; i < numCPUs; i++)
				if (memoryWindow[i])
					memoryWindow[i]->NotifyMapLoaded();
			break;

		case ID_DEBUG_DISASSEMBLY:
			if (disasmWindow[0])
				disasmWindow[0]->Show(true);
			break;

		case ID_DEBUG_GEDEBUGGER:
			if (geDebuggerWindow)
				geDebuggerWindow->Show(true);
			break;

		case ID_DEBUG_MEMORYVIEW:
			if (memoryWindow[0])
				memoryWindow[0]->Show(true);
			break;

		case ID_DEBUG_EXTRACTFILE:
		{
			std::string filename;
			if (!InputBox_GetString(hInst, hWnd, L"Disc filename", filename, filename)) {
				break;
			}

			const char *lastSlash = strrchr(filename.c_str(), '/');
			if (lastSlash) {
				fn = lastSlash + 1;
			} else {
				fn = "";
			}

			PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
			if (!info.exists) {
				MessageBox(hWnd, L"File does not exist.", L"Sorry", 0);
			} else if (info.type == FILETYPE_DIRECTORY) {
				MessageBox(hWnd, L"Cannot extract directories.", L"Sorry", 0);
			} else if (W32Util::BrowseForFileName(false, hWnd, L"Save file as...", 0, L"All files\0*.*\0\0", L"", fn)) {
				FILE *fp = File::OpenCFile(fn, "wb");
				u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ, "");
				u8 buffer[4096];
				size_t bytes;
				do {
					bytes = pspFileSystem.ReadFile(handle, buffer, sizeof(buffer));
					fwrite(buffer, 1, bytes, fp);
				} while (bytes == sizeof(buffer));
				pspFileSystem.CloseFile(handle);
				fclose(fp);
			}
		}
		break;

		case ID_DEBUG_LOG:
			LogManager::GetInstance()->GetConsoleListener()->Show(LogManager::GetInstance()->GetConsoleListener()->Hidden());
			break;

		case ID_DEBUG_IGNOREILLEGALREADS:
			g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
			break;

		case ID_OPTIONS_FULLSCREEN:
			SendToggleFullscreen(!g_Config.bFullScreen);
			break;

		case ID_OPTIONS_VERTEXCACHE:
			g_Config.bVertexCache = !g_Config.bVertexCache;
			break;

		case ID_OPTIONS_SHOWFPS:
			g_Config.iShowFPSCounter = !g_Config.iShowFPSCounter;
			break;

		case ID_OPTIONS_TEXTUREFILTERING_AUTO: setTexFiltering(TEX_FILTER_AUTO); break;
		case ID_OPTIONS_NEARESTFILTERING:      setTexFiltering(TEX_FILTER_NEAREST); break;
		case ID_OPTIONS_LINEARFILTERING:       setTexFiltering(TEX_FILTER_LINEAR); break;
		case ID_OPTIONS_LINEARFILTERING_CG:    setTexFiltering(TEX_FILTER_LINEAR_VIDEO); break;

		case ID_OPTIONS_BUFLINEARFILTER:       setBufFilter(SCALE_LINEAR); break;
		case ID_OPTIONS_BUFNEARESTFILTER:      setBufFilter(SCALE_NEAREST); break;

		case ID_OPTIONS_TOPMOST:
			g_Config.bTopMost = !g_Config.bTopMost;
			W32Util::MakeTopMost(hWnd, g_Config.bTopMost);
			break;

		case ID_OPTIONS_PAUSE_FOCUS:
			g_Config.bPauseOnLostFocus = !g_Config.bPauseOnLostFocus;
			break;

		case ID_OPTIONS_CONTROLS:
			NativeMessageReceived("control mapping", "");
			break;

		case ID_OPTIONS_MORE_SETTINGS:
			NativeMessageReceived("settings", "");
			break;

		case ID_EMULATION_SOUND:
			g_Config.bEnableSound = !g_Config.bEnableSound;
			if (g_Config.bEnableSound) {
				if (PSP_IsInited() && !IsAudioInitialised())
					Audio_Init();
			}
			break;

		case ID_HELP_OPENWEBSITE:
			ShellExecute(NULL, L"open", L"http://www.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_BUYGOLD:
			ShellExecute(NULL, L"open", L"http://central.ppsspp.org/buygold", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_OPENFORUM:
			ShellExecute(NULL, L"open", L"http://forums.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_GITHUB:
			ShellExecute(NULL, L"open", L"https://github.com/hrydgard/ppsspp/", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_ABOUT:
			DialogManager::EnableAll(FALSE);
			DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
			DialogManager::EnableAll(TRUE);
			break;

		case ID_DEBUG_TAKESCREENSHOT:
			g_TakeScreenshot = true;
			break;

		case ID_FILE_DUMPFRAMES:
			g_Config.bDumpFrames = !g_Config.bDumpFrames;
			break;

		case ID_FILE_USEFFV1:
			g_Config.bUseFFV1 = !g_Config.bUseFFV1;
			break;

		case ID_FILE_DUMPAUDIO:
			g_Config.bDumpAudio = !g_Config.bDumpAudio;
			break;

		default:
		{
			// Handle the dynamic shader switching here.
			// The Menu ID is contained in wParam, so subtract
			// ID_SHADERS_BASE and an additional 1 off it.
			u32 index = (wParam - ID_SHADERS_BASE - 1);
			if (index < availableShaders.size()) {
				g_Config.sPostShaderName = availableShaders[index];

				NativeMessageReceived("gpu_resized", "");
				break;
			}

			MessageBox(hWnd, L"Unimplemented", L"Sorry", 0);
		}
		break;
		}
	}

	void UpdateMenus(bool isMenuSelect) {
		if (isMenuSelect) {
			menuShaderInfoLoaded = false;
		}

		HMENU menu = GetMenu(GetHWND());
#define CHECKITEM(item,value) 	CheckMenuItem(menu,item,MF_BYCOMMAND | ((value) ? MF_CHECKED : MF_UNCHECKED));
		CHECKITEM(ID_DEBUG_IGNOREILLEGALREADS, g_Config.bIgnoreBadMemAccess);
		CHECKITEM(ID_DEBUG_SHOWDEBUGSTATISTICS, g_Config.bShowDebugStats);
		CHECKITEM(ID_OPTIONS_HARDWARETRANSFORM, g_Config.bHardwareTransform);
		CHECKITEM(ID_DEBUG_RUNONLOAD, g_Config.bAutoRun);
		CHECKITEM(ID_OPTIONS_VERTEXCACHE, g_Config.bVertexCache);
		CHECKITEM(ID_OPTIONS_SHOWFPS, g_Config.iShowFPSCounter);
		CHECKITEM(ID_OPTIONS_FRAMESKIP_AUTO, g_Config.bAutoFrameSkip);
		CHECKITEM(ID_OPTIONS_FRAMESKIP, g_Config.iFrameSkip != 0);
		CHECKITEM(ID_OPTIONS_VSYNC, g_Config.bVSync);
		CHECKITEM(ID_OPTIONS_TOPMOST, g_Config.bTopMost);
		CHECKITEM(ID_OPTIONS_PAUSE_FOCUS, g_Config.bPauseOnLostFocus);
		CHECKITEM(ID_EMULATION_SOUND, g_Config.bEnableSound);
		CHECKITEM(ID_TEXTURESCALING_DEPOSTERIZE, g_Config.bTexDeposterize);
		CHECKITEM(ID_EMULATION_CHEATS, g_Config.bEnableCheats);
		CHECKITEM(ID_OPTIONS_IGNOREWINKEY, g_Config.bIgnoreWindowsKey);
		CHECKITEM(ID_FILE_DUMPFRAMES, g_Config.bDumpFrames);
		CHECKITEM(ID_FILE_USEFFV1, g_Config.bUseFFV1);
		CHECKITEM(ID_FILE_DUMPAUDIO, g_Config.bDumpAudio);

		static const int displayrotationitems[] = {
			ID_EMULATION_ROTATION_H,
			ID_EMULATION_ROTATION_V,
			ID_EMULATION_ROTATION_H_R,
			ID_EMULATION_ROTATION_V_R
		};
		if (g_Config.iInternalScreenRotation < ROTATION_LOCKED_HORIZONTAL)
			g_Config.iInternalScreenRotation = ROTATION_LOCKED_HORIZONTAL;

		else if (g_Config.iInternalScreenRotation > ROTATION_LOCKED_VERTICAL180)
			g_Config.iInternalScreenRotation = ROTATION_LOCKED_VERTICAL180;

		for (int i = 0; i < ARRAY_SIZE(displayrotationitems); i++) {
			CheckMenuItem(menu, displayrotationitems[i], MF_BYCOMMAND | ((i + 1) == g_Config.iInternalScreenRotation ? MF_CHECKED : MF_UNCHECKED));
		}

		// Disable Vertex Cache when HW T&L is disabled.
		if (!g_Config.bHardwareTransform) {
			EnableMenuItem(menu, ID_OPTIONS_VERTEXCACHE, MF_GRAYED);
		} else {
			EnableMenuItem(menu, ID_OPTIONS_VERTEXCACHE, MF_ENABLED);
		}

		static const int zoomitems[11] = {
			ID_OPTIONS_SCREENAUTO,
			ID_OPTIONS_SCREEN1X,
			ID_OPTIONS_SCREEN2X,
			ID_OPTIONS_SCREEN3X,
			ID_OPTIONS_SCREEN4X,
			ID_OPTIONS_SCREEN5X,
			ID_OPTIONS_SCREEN6X,
			ID_OPTIONS_SCREEN7X,
			ID_OPTIONS_SCREEN8X,
			ID_OPTIONS_SCREEN9X,
			ID_OPTIONS_SCREEN10X,
		};
		if (g_Config.iInternalResolution < RESOLUTION_AUTO)
			g_Config.iInternalResolution = RESOLUTION_AUTO;

		else if (g_Config.iInternalResolution > RESOLUTION_MAX)
			g_Config.iInternalResolution = RESOLUTION_MAX;

		for (int i = 0; i < ARRAY_SIZE(zoomitems); i++) {
			CheckMenuItem(menu, zoomitems[i], MF_BYCOMMAND | ((i == g_Config.iInternalResolution) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int windowSizeItems[4] = {
			ID_OPTIONS_WINDOW1X,
			ID_OPTIONS_WINDOW2X,
			ID_OPTIONS_WINDOW3X,
			ID_OPTIONS_WINDOW4X,
		};

		RECT rc;
		GetClientRect(GetHWND(), &rc);

		int checkW = g_Config.IsPortrait() ? 272 : 480;
		int checkH = g_Config.IsPortrait() ? 480 : 272;

		for (int i = 0; i < ARRAY_SIZE(windowSizeItems); i++) {
			bool check = (i + 1) * checkW == rc.right - rc.left || (i + 1) * checkH == rc.bottom - rc.top;
			CheckMenuItem(menu, windowSizeItems[i], MF_BYCOMMAND | (check ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texscalingitems[] = {
			ID_TEXTURESCALING_AUTO,
			ID_TEXTURESCALING_OFF,
			ID_TEXTURESCALING_2X,
			ID_TEXTURESCALING_3X,
			ID_TEXTURESCALING_4X,
			ID_TEXTURESCALING_5X,
		};
		if (g_Config.iTexScalingLevel < TEXSCALING_AUTO)
			g_Config.iTexScalingLevel = TEXSCALING_AUTO;

		else if (g_Config.iTexScalingLevel > TEXSCALING_MAX)
			g_Config.iTexScalingLevel = TEXSCALING_MAX;

		for (int i = 0; i < ARRAY_SIZE(texscalingitems); i++) {
			CheckMenuItem(menu, texscalingitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingLevel) ? MF_CHECKED : MF_UNCHECKED));
		}

		if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL && !gl_extensions.OES_texture_npot) {
			EnableMenuItem(menu, ID_TEXTURESCALING_3X, MF_GRAYED);
			EnableMenuItem(menu, ID_TEXTURESCALING_5X, MF_GRAYED);
		} else {
			EnableMenuItem(menu, ID_TEXTURESCALING_3X, MF_ENABLED);
			EnableMenuItem(menu, ID_TEXTURESCALING_5X, MF_ENABLED);
		}

		static const int texscalingtypeitems[] = {
			ID_TEXTURESCALING_XBRZ,
			ID_TEXTURESCALING_HYBRID,
			ID_TEXTURESCALING_BICUBIC,
			ID_TEXTURESCALING_HYBRID_BICUBIC,
		};
		if (g_Config.iTexScalingType < TextureScalerCommon::XBRZ)
			g_Config.iTexScalingType = TextureScalerCommon::XBRZ;

		else if (g_Config.iTexScalingType > TextureScalerCommon::HYBRID_BICUBIC)
			g_Config.iTexScalingType = TextureScalerCommon::HYBRID_BICUBIC;

		for (int i = 0; i < ARRAY_SIZE(texscalingtypeitems); i++) {
			CheckMenuItem(menu, texscalingtypeitems[i], MF_BYCOMMAND | ((i == g_Config.iTexScalingType) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int texfilteringitems[] = {
			ID_OPTIONS_TEXTUREFILTERING_AUTO,
			ID_OPTIONS_NEARESTFILTERING,
			ID_OPTIONS_LINEARFILTERING,
			ID_OPTIONS_LINEARFILTERING_CG,
		};
		if (g_Config.iTexFiltering < TEX_FILTER_AUTO)
			g_Config.iTexFiltering = TEX_FILTER_AUTO;
		else if (g_Config.iTexFiltering > TEX_FILTER_LINEAR_VIDEO)
			g_Config.iTexFiltering = TEX_FILTER_LINEAR_VIDEO;

		for (int i = 0; i < ARRAY_SIZE(texfilteringitems); i++) {
			CheckMenuItem(menu, texfilteringitems[i], MF_BYCOMMAND | ((i + 1) == g_Config.iTexFiltering ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int bufferfilteritems[] = {
			ID_OPTIONS_BUFLINEARFILTER,
			ID_OPTIONS_BUFNEARESTFILTER,
		};
		if (g_Config.iBufFilter < SCALE_LINEAR)
			g_Config.iBufFilter = SCALE_LINEAR;

		else if (g_Config.iBufFilter > SCALE_NEAREST)
			g_Config.iBufFilter = SCALE_NEAREST;

		for (int i = 0; i < ARRAY_SIZE(bufferfilteritems); i++) {
			CheckMenuItem(menu, bufferfilteritems[i], MF_BYCOMMAND | ((i + 1) == g_Config.iBufFilter ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int renderingmode[] = {
			ID_OPTIONS_NONBUFFEREDRENDERING,
			ID_OPTIONS_BUFFEREDRENDERING,
			ID_OPTIONS_READFBOTOMEMORYCPU,
			ID_OPTIONS_READFBOTOMEMORYGPU,
		};
		if (g_Config.iRenderingMode < FB_NON_BUFFERED_MODE)
			g_Config.iRenderingMode = FB_NON_BUFFERED_MODE;

		else if (g_Config.iRenderingMode > FB_READFBOMEMORY_GPU)
			g_Config.iRenderingMode = FB_READFBOMEMORY_GPU;

		for (int i = 0; i < ARRAY_SIZE(renderingmode); i++) {
			CheckMenuItem(menu, renderingmode[i], MF_BYCOMMAND | ((i == g_Config.iRenderingMode) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int frameskipping[] = {
			ID_OPTIONS_FRAMESKIP_0,
			ID_OPTIONS_FRAMESKIP_1,
			ID_OPTIONS_FRAMESKIP_2,
			ID_OPTIONS_FRAMESKIP_3,
			ID_OPTIONS_FRAMESKIP_4,
			ID_OPTIONS_FRAMESKIP_5,
			ID_OPTIONS_FRAMESKIP_6,
			ID_OPTIONS_FRAMESKIP_7,
			ID_OPTIONS_FRAMESKIP_8,
		};
		if (g_Config.iFrameSkip < FRAMESKIP_OFF)
			g_Config.iFrameSkip = FRAMESKIP_OFF;

		else if (g_Config.iFrameSkip > FRAMESKIP_MAX)
			g_Config.iFrameSkip = FRAMESKIP_MAX;

		for (int i = 0; i < ARRAY_SIZE(frameskipping); i++) {
			CheckMenuItem(menu, frameskipping[i], MF_BYCOMMAND | ((i == g_Config.iFrameSkip) ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int savestateSlot[] = {
			ID_FILE_SAVESTATE_SLOT_1,
			ID_FILE_SAVESTATE_SLOT_2,
			ID_FILE_SAVESTATE_SLOT_3,
			ID_FILE_SAVESTATE_SLOT_4,
			ID_FILE_SAVESTATE_SLOT_5,
		};

		if (g_Config.iCurrentStateSlot < 0)
			g_Config.iCurrentStateSlot = 0;

		else if (g_Config.iCurrentStateSlot >= SaveState::NUM_SLOTS)
			g_Config.iCurrentStateSlot = SaveState::NUM_SLOTS - 1;

		for (int i = 0; i < ARRAY_SIZE(savestateSlot); i++) {
			CheckMenuItem(menu, savestateSlot[i], MF_BYCOMMAND | ((i == g_Config.iCurrentStateSlot) ? MF_CHECKED : MF_UNCHECKED));
		}

		switch (g_Config.iGPUBackend) {
		case GPU_BACKEND_DIRECT3D9:
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_OPENGL, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_VULKAN, MF_ENABLED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_CHECKED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_OPENGL, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_VULKAN, MF_UNCHECKED);
			break;
		case GPU_BACKEND_OPENGL:
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_OPENGL, MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_VULKAN, MF_ENABLED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_OPENGL, MF_CHECKED);
			CheckMenuItem(menu, ID_OPTIONS_VULKAN, MF_UNCHECKED);
			break;
		case GPU_BACKEND_VULKAN:
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_OPENGL, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_VULKAN, MF_GRAYED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_OPENGL, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_VULKAN, MF_CHECKED);
			break;
		case GPU_BACKEND_DIRECT3D11:
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_OPENGL, MF_ENABLED);
			EnableMenuItem(menu, ID_OPTIONS_VULKAN, MF_ENABLED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D9, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_CHECKED);
			CheckMenuItem(menu, ID_OPTIONS_OPENGL, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_VULKAN, MF_UNCHECKED);
			break;
		}

		UpdateDynamicMenuCheckmarks(menu);
		UpdateCommands();
	}

	void UpdateCommands() {
		static GlobalUIState lastGlobalUIState = UISTATE_PAUSEMENU;
		static CoreState lastCoreState = CORE_ERROR;

		HMENU menu = GetMenu(GetHWND());
		EnableMenuItem(menu, ID_DEBUG_LOG, !g_Config.bEnableLogging);
		SetIngameMenuItemStates(menu, GetUIState());

		if (lastGlobalUIState == GetUIState() && lastCoreState == coreState)
			return;

		lastCoreState = coreState;
		lastGlobalUIState = GetUIState();

		bool isPaused = Core_IsStepping() && GetUIState() == UISTATE_INGAME;
		TranslateMenuItem(menu, ID_TOGGLE_PAUSE, L"\tF8", isPaused ? "Run" : "Pause");
	}

	// Message handler for about box.
	LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
		switch (message) {
		case WM_INITDIALOG:
		{
			W32Util::CenterWindow(hDlg);
			HWND versionBox = GetDlgItem(hDlg, IDC_VERSION);
			std::string windowText = System_GetPropertyBool(SYSPROP_APP_GOLD) ? "PPSSPP Gold " : "PPSSPP ";
			windowText.append(PPSSPP_GIT_VERSION);
			SetWindowText(versionBox, ConvertUTF8ToWString(windowText).c_str());
		}
		return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}
			break;
		}
		return FALSE;
	}
}

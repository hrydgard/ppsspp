#include "ppsspp_config.h"
#include <algorithm>
#include <string>
#include <sstream>
#include <unordered_map>

#include "CommonWindows.h"
#include <shellapi.h>

#include "resource.h"

#include "Common/GPU/OpenGL/GLFeatures.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/Log/LogManager.h"
#include "Common/Log/ConsoleListener.h"
#include "Common/OSVersion.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/StringUtils.h"
#if PPSSPP_API(ANY_GL)
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/FramebufferManagerGLES.h"
#endif
#include "UI/OnScreenDisplay.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/TextureScalerCommon.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/KeyMap.h"
#include "UI/OnScreenDisplay.h"
#include "Windows/MainWindowMenu.h"
#include "Windows/MainWindow.h"
#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/ShellUtil.h"
#include "Windows/W32Util/Misc.h"
#include "Windows/InputBox.h"
#include "Windows/main.h"
#include "Windows/W32Util/DarkMode.h"

#include "Core/HLE/sceUmd.h"
#include "Core/HLE/sceNet.h"
#include "Core/SaveState.h"
#include "Core/Core.h"
#include "Core/RetroAchievements.h"

#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
#include "ext/rcheevos/include/rc_client_raintegration.h"
#endif

extern bool g_TakeScreenshot;

namespace MainWindow {
	extern HINSTANCE hInst;
	extern bool noFocusPause;
	static bool browsePauseAfter;

	static std::unordered_map<int, std::string> initialMenuKeys;
	static std::vector<std::string> availableShaders;
	static std::string menuLanguageID = "";
	static int menuKeymapGeneration = -1;
	static bool menuShaderInfoLoaded = false;
	std::vector<ShaderInfo> menuShaderInfo;

	LRESULT CALLBACK AboutDlgProc(HWND, UINT, WPARAM, LPARAM);

	void SetIngameMenuItemStates(HMENU menu, const GlobalUIState state) {
		bool menuEnableBool = state == UISTATE_INGAME || state == UISTATE_EXCEPTION;

		bool loadStateEnableBool = menuEnableBool;
		bool saveStateEnableBool = menuEnableBool;
		if (Achievements::HardcoreModeActive()) {
			loadStateEnableBool = false;
			if (!g_Config.bAchievementsSaveStateInHardcoreMode) {
				saveStateEnableBool = false;
			}
		}

		if (!NetworkAllowSaveState()) {
			loadStateEnableBool = false;
			saveStateEnableBool = false;
		}

		const UINT menuEnable = menuEnableBool ? MF_ENABLED : MF_GRAYED;
		const UINT loadStateEnable = loadStateEnableBool ? MF_ENABLED : MF_GRAYED;
		const UINT saveStateEnable = saveStateEnableBool ? MF_ENABLED : MF_GRAYED;
		const UINT menuInGameEnable = state == UISTATE_INGAME ? MF_ENABLED : MF_GRAYED;
		const UINT umdSwitchEnable = state == UISTATE_INGAME && getUMDReplacePermit() ? MF_ENABLED : MF_GRAYED;
		const UINT debugEnable = !Achievements::HardcoreModeActive() ? MF_ENABLED : MF_GRAYED;
		const UINT debugIngameEnable = (state == UISTATE_INGAME && !Achievements::HardcoreModeActive()) ? MF_ENABLED : MF_GRAYED;

		EnableMenuItem(menu, ID_FILE_SAVESTATE_SLOT_MENU, saveStateEnable);
		EnableMenuItem(menu, ID_FILE_SAVESTATEFILE, saveStateEnable);
		EnableMenuItem(menu, ID_FILE_LOADSTATEFILE, loadStateEnable);
		EnableMenuItem(menu, ID_FILE_QUICKSAVESTATE, saveStateEnable);
		EnableMenuItem(menu, ID_FILE_QUICKLOADSTATE, loadStateEnable);
		EnableMenuItem(menu, ID_EMULATION_PAUSE, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_STOP, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_RESET, menuEnable);
		EnableMenuItem(menu, ID_EMULATION_SWITCH_UMD, umdSwitchEnable);
		EnableMenuItem(menu, ID_EMULATION_CHAT, g_Config.bEnableNetworkChat ? menuInGameEnable : MF_GRAYED);
		EnableMenuItem(menu, ID_TOGGLE_BREAK, debugIngameEnable);
		EnableMenuItem(menu, ID_DEBUG_LOADMAPFILE, debugIngameEnable);
		EnableMenuItem(menu, ID_DEBUG_SAVEMAPFILE, debugIngameEnable);
		EnableMenuItem(menu, ID_DEBUG_LOADSYMFILE, debugIngameEnable);
		EnableMenuItem(menu, ID_DEBUG_SAVESYMFILE, debugIngameEnable);
		EnableMenuItem(menu, ID_DEBUG_RESETSYMBOLTABLE, debugIngameEnable);
		EnableMenuItem(menu, ID_DEBUG_SHOWDEBUGSTATISTICS, debugEnable);
		EnableMenuItem(menu, ID_DEBUG_EXTRACTFILE, menuEnable);
		EnableMenuItem(menu, ID_DEBUG_MEMORYBASE, menuInGameEnable);
		EnableMenuItem(menu, ID_DEBUG_DISASSEMBLY, debugEnable);
		EnableMenuItem(menu, ID_DEBUG_MEMORYVIEW, debugEnable);
		EnableMenuItem(menu, ID_DEBUG_GEDEBUGGER, debugEnable);

		// While playing, this pop up doesn't work - and probably doesn't make sense.
		EnableMenuItem(menu, ID_OPTIONS_LANGUAGE, state == UISTATE_INGAME ? MF_GRAYED : MF_ENABLED);
	}

	static HMENU GetSubmenuById(HMENU menu, int menuID) {
		MENUITEMINFO menuInfo{ sizeof(MENUITEMINFO), MIIM_SUBMENU };
		if (GetMenuItemInfo(menu, menuID, MF_BYCOMMAND, &menuInfo) != FALSE) {
			return menuInfo.hSubMenu;
		}
		return nullptr;
	}

	static void EmptySubMenu(HMENU menu) {
		int c = GetMenuItemCount(menu);
		for (int i = 0; i < c; ++i) {
			RemoveMenu(menu, 0, MF_BYPOSITION);
		}
	}

	static std::string GetMenuItemText(HMENU menu, int menuID) {
		MENUITEMINFO menuInfo{ sizeof(menuInfo), MIIM_STRING };
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
		auto des = GetI18NCategory(I18NCat::DESKTOPUI);

		const std::wstring visitMainWebsite = ConvertUTF8ToWString(des->T("www.ppsspp.org"));
		const std::wstring visitForum = ConvertUTF8ToWString(des->T("PPSSPP Forums"));
		const std::wstring buyGold = ConvertUTF8ToWString(des->T("Buy Gold"));
		const std::wstring gitHub = ConvertUTF8ToWString(des->T("GitHub"));
		const std::wstring discord = ConvertUTF8ToWString(des->T("Discord"));
		const std::wstring aboutPPSSPP = ConvertUTF8ToWString(des->T("About PPSSPP..."));

		HMENU helpMenu = GetSubmenuById(menu, ID_HELP_MENU);
		EmptySubMenu(helpMenu);

		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_OPENWEBSITE, visitMainWebsite.c_str());
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_OPENFORUM, visitForum.c_str());
		// Repeat the process for other languages, if necessary.
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_BUYGOLD, buyGold.c_str());
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_GITHUB, gitHub.c_str());
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_DISCORD, discord.c_str());
		AppendMenu(helpMenu, MF_SEPARATOR, 0, 0);
		AppendMenu(helpMenu, MF_STRING | MF_BYCOMMAND, ID_HELP_ABOUT, aboutPPSSPP.c_str());
	}

	static void TranslateMenuItem(const HMENU hMenu, const int menuID, const std::wstring& accelerator = L"", const char *key = nullptr) {
		auto des = GetI18NCategory(I18NCat::DESKTOPUI);

		std::wstring translated;
		if (key == nullptr || !strcmp(key, "")) {
			translated = ConvertUTF8ToWString(des->T(GetMenuItemInitialText(hMenu, menuID)));
		} else {
			translated = ConvertUTF8ToWString(des->T(key));
		}
		translated.append(accelerator);

		ModifyMenu(hMenu, menuID, MF_STRING | MF_BYCOMMAND, menuID, translated.c_str());
	}

	void DoTranslateMenus(HWND hWnd, HMENU menu) {
		auto useDefHotkey = [](int virtKey) {
			return !KeyMap::PspButtonHasMappings(virtKey);
		};

		TranslateMenuItem(menu, ID_FILE_MENU);
		TranslateMenuItem(menu, ID_EMULATION_MENU);
		TranslateMenuItem(menu, ID_DEBUG_MENU);
		TranslateMenuItem(menu, ID_OPTIONS_MENU);
		TranslateMenuItem(menu, ID_HELP_MENU);

		// File menu
		TranslateMenuItem(menu, ID_FILE_LOAD);
		TranslateMenuItem(menu, ID_FILE_LOAD_DIR);
		TranslateMenuItem(menu, ID_FILE_LOAD_MEMSTICK);
		TranslateMenuItem(menu, ID_FILE_OPEN_NEW_INSTANCE);
		TranslateMenuItem(menu, ID_FILE_MEMSTICK);
		TranslateMenuItem(menu, ID_FILE_SAVESTATE_SLOT_MENU, useDefHotkey(VIRTKEY_NEXT_SLOT) ? L"\tF3" : L"");
		TranslateMenuItem(menu, ID_FILE_QUICKLOADSTATE, useDefHotkey(VIRTKEY_LOAD_STATE) ? L"\tF4" : L"");
		TranslateMenuItem(menu, ID_FILE_QUICKSAVESTATE, useDefHotkey(VIRTKEY_SAVE_STATE) ? L"\tF2" : L"");
		TranslateMenuItem(menu, ID_FILE_LOADSTATEFILE);
		TranslateMenuItem(menu, ID_FILE_SAVESTATEFILE);
		TranslateMenuItem(menu, ID_FILE_RECORD_MENU);
		TranslateMenuItem(menu, ID_FILE_EXIT, L"\tAlt+F4");

		// Emulation menu
		TranslateMenuItem(menu, ID_EMULATION_PAUSE);
		TranslateMenuItem(menu, ID_EMULATION_STOP, g_Config.bSystemControls ? L"\tCtrl+W" : L"");
		TranslateMenuItem(menu, ID_EMULATION_RESET, g_Config.bSystemControls ? L"\tCtrl+B" : L"");
		TranslateMenuItem(menu, ID_EMULATION_SWITCH_UMD, g_Config.bSystemControls ? L"\tCtrl+U" : L"");
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_MENU);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_H);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_V);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_H_R);
		TranslateMenuItem(menu, ID_EMULATION_ROTATION_V_R);

		// Debug menu
		TranslateMenuItem(menu, ID_TOGGLE_BREAK, g_Config.bSystemControls ? L"\tF8" : L"", "Break");
		TranslateMenuItem(menu, ID_DEBUG_BREAKONLOAD);
		TranslateMenuItem(menu, ID_DEBUG_IGNOREILLEGALREADS);
		TranslateMenuItem(menu, ID_DEBUG_LOADMAPFILE);
		TranslateMenuItem(menu, ID_DEBUG_SAVEMAPFILE);
		TranslateMenuItem(menu, ID_DEBUG_LOADSYMFILE);
		TranslateMenuItem(menu, ID_DEBUG_SAVESYMFILE);
		TranslateMenuItem(menu, ID_DEBUG_RESETSYMBOLTABLE);
		TranslateMenuItem(menu, ID_DEBUG_TAKESCREENSHOT, g_Config.bSystemControls ? L"\tF12" : L"");
		TranslateMenuItem(menu, ID_DEBUG_DUMPNEXTFRAME);
		TranslateMenuItem(menu, ID_DEBUG_SHOWDEBUGSTATISTICS);
		TranslateMenuItem(menu, ID_DEBUG_RESTARTGRAPHICS);
		TranslateMenuItem(menu, ID_DEBUG_DISASSEMBLY, g_Config.bSystemControls ? L"\tCtrl+D" : L"");
		TranslateMenuItem(menu, ID_DEBUG_GEDEBUGGER, g_Config.bSystemControls ? L"\tCtrl+G" : L"");
		TranslateMenuItem(menu, ID_DEBUG_EXTRACTFILE);
		TranslateMenuItem(menu, ID_DEBUG_LOG, g_Config.bSystemControls ? L"\tCtrl+L" : L"");
		TranslateMenuItem(menu, ID_DEBUG_MEMORYBASE);
		TranslateMenuItem(menu, ID_DEBUG_MEMORYVIEW, g_Config.bSystemControls ? L"\tCtrl+M" : L"");

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
		TranslateMenuItem(menu, ID_FILE_DUMP_VIDEO_OUTPUT);
		TranslateMenuItem(menu, ID_FILE_DUMPAUDIO);

		// Skip display multipliers x1-x10
		TranslateMenuItem(menu, ID_OPTIONS_FULLSCREEN, g_Config.bSystemControls ? L"\tAlt+Return, F11" : L"");
		TranslateMenuItem(menu, ID_OPTIONS_VSYNC);
		TranslateMenuItem(menu, ID_OPTIONS_SCREEN_MENU, g_Config.bSystemControls ? L"\tCtrl+1" : L"");
		TranslateMenuItem(menu, ID_OPTIONS_SCREENAUTO);
		// Skip rendering resolution 2x-5x..
		TranslateMenuItem(menu, ID_OPTIONS_WINDOW_MENU);
		// Skip window size 1x-4x..
		TranslateMenuItem(menu, ID_OPTIONS_BACKEND_MENU);
		TranslateMenuItem(menu, ID_OPTIONS_DIRECT3D11);
		TranslateMenuItem(menu, ID_OPTIONS_OPENGL);
		TranslateMenuItem(menu, ID_OPTIONS_VULKAN);

		TranslateMenuItem(menu, ID_OPTIONS_RENDERMODE_MENU);
		TranslateMenuItem(menu, ID_OPTIONS_SKIP_BUFFER_EFFECTS);
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIP_MENU, g_Config.bSystemControls ? L"\tF7" : L"");
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIP_AUTO);
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIP_0);
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIPTYPE_MENU);
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIPTYPE_COUNT);
		TranslateMenuItem(menu, ID_OPTIONS_FRAMESKIPTYPE_PRCNT);
		// Skip frameskipping 1-8..
		TranslateMenuItem(menu, ID_OPTIONS_TEXTUREFILTERING_MENU);
		TranslateMenuItem(menu, ID_OPTIONS_TEXTUREFILTERING_AUTO);
		TranslateMenuItem(menu, ID_OPTIONS_NEARESTFILTERING);
		TranslateMenuItem(menu, ID_OPTIONS_LINEARFILTERING);
		TranslateMenuItem(menu, ID_OPTIONS_AUTOMAXQUALITYFILTERING);
		TranslateMenuItem(menu, ID_OPTIONS_SMART2DTEXTUREFILTERING);
		TranslateMenuItem(menu, ID_OPTIONS_SCREENFILTER_MENU);
		TranslateMenuItem(menu, ID_OPTIONS_BUFLINEARFILTER);
		TranslateMenuItem(menu, ID_OPTIONS_BUFNEARESTFILTER);
		TranslateMenuItem(menu, ID_OPTIONS_TEXTURESCALING_MENU);
		TranslateMenuItem(menu, ID_TEXTURESCALING_OFF);
		// Skip texture scaling 2x-5x...
		TranslateMenuItem(menu, ID_TEXTURESCALING_XBRZ);
		TranslateMenuItem(menu, ID_TEXTURESCALING_HYBRID);
		TranslateMenuItem(menu, ID_TEXTURESCALING_BICUBIC);
		TranslateMenuItem(menu, ID_TEXTURESCALING_HYBRID_BICUBIC);
		TranslateMenuItem(menu, ID_TEXTURESCALING_DEPOSTERIZE);
		TranslateMenuItem(menu, ID_OPTIONS_HARDWARETRANSFORM);
		TranslateMenuItem(menu, ID_EMULATION_SOUND);
		TranslateMenuItem(menu, ID_EMULATION_CHEATS, g_Config.bSystemControls ? L"\tCtrl+T" : L"");
		TranslateMenuItem(menu, ID_EMULATION_CHAT, g_Config.bSystemControls ? L"\tCtrl+C" : L"");

		// Help menu: it's translated in CreateHelpMenu.
		CreateHelpMenu(menu);
	}

	void TranslateMenus(HWND hWnd, HMENU menu) {
		bool changed = false;

		const std::string curLanguageID = g_i18nrepo.LanguageID();
		if (curLanguageID != menuLanguageID || KeyMap::HasChanged(menuKeymapGeneration)) {
			DoTranslateMenus(hWnd, menu);
			menuLanguageID = curLanguageID;
			changed = true;
		}

		if (changed) {
			DrawMenuBar(hWnd);
		}
	}

	void BrowseAndBootDone(std::string filename);

	void BrowseAndBoot(RequesterToken token, std::string defaultPath, bool browseDirectory) {
		browsePauseAfter = false;
		if (GetUIState() == UISTATE_INGAME) {
			browsePauseAfter = Core_IsStepping();
			if (!browsePauseAfter)
				Core_Break(BreakReason::BreakOnBoot, 0);
		}
		auto mm = GetI18NCategory(I18NCat::MAINMENU);

		W32Util::MakeTopMost(GetHWND(), false);

		if (browseDirectory) {
			System_BrowseForFolder(token, mm->T("Load"), Path(), [](const std::string &value, int) {
				BrowseAndBootDone(value);
			});
		} else {
			System_BrowseForFile(token, mm->T("Load"), BrowseFileType::BOOTABLE, [](const std::string &value, int) {
				BrowseAndBootDone(value);
			});
		}
	}

	void BrowseAndBootDone(std::string filename) {
		if (GetUIState() == UISTATE_INGAME || GetUIState() == UISTATE_EXCEPTION || GetUIState() == UISTATE_PAUSEMENU) {
			Core_Resume();
		}
		filename = ReplaceAll(filename, "\\", "/");
		System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, filename);
		W32Util::MakeTopMost(GetHWND(), g_Config.bTopMost);
	}

	static void UmdSwitchAction(RequesterToken token) {
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		System_BrowseForFile(token, mm->T("Switch UMD"), BrowseFileType::BOOTABLE, [](const std::string &value, int) {
			// This is safe because the callback runs on the emu thread.
			__UmdReplace(Path(value));
		});
	}

	static void SaveStateActionFinished(SaveState::Status status, std::string_view message, void *userdata) {
		if (!message.empty() && (!g_Config.bDumpFrames || !g_Config.bDumpVideoOutput)) {
			g_OSD.Show(status == SaveState::Status::SUCCESS ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_ERROR, message, status == SaveState::Status::SUCCESS ? 2.0 : 5.0);
		}
		PostMessage(MainWindow::GetHWND(), WM_USER_SAVESTATE_FINISH, 0, 0);
	}

	// not static
	void setTexScalingMultiplier(int level) {
		System_RunOnMainThread([level]() {
			g_Config.iTexScalingLevel = level;
		});
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}

	static void setTexScalingType(int type) {
		System_RunOnMainThread([type]() {
			g_Config.iTexScalingType = type;
		});
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}

	static void setSkipBufferEffects(bool skip) {
		System_RunOnMainThread([skip]() {
			g_Config.bSkipBufferEffects = skip;
		});
		System_PostUIMessage(UIMessage::GPU_RENDER_RESIZED);
		System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
	}

	static void setFrameSkipping(int framesToSkip = -1) {
		if (framesToSkip >= FRAMESKIP_OFF)
			g_Config.iFrameSkip = framesToSkip;
		else {
			if (++g_Config.iFrameSkip > FRAMESKIP_MAX)
				g_Config.iFrameSkip = FRAMESKIP_OFF;
		}

		auto gr = GetI18NCategory(I18NCat::GRAPHICS);

		std::ostringstream messageStream;
		messageStream << gr->T("Frame Skipping") << ":" << " ";

		if (g_Config.iFrameSkip == FRAMESKIP_OFF)
			messageStream << gr->T("Off");
		else
			messageStream << g_Config.iFrameSkip;

		g_OSD.Show(OSDType::MESSAGE_INFO, messageStream.str());
	}

	static void setFrameSkippingType(int fskipType = -1) {
		if (fskipType >= 0 && fskipType <= 1) {
			g_Config.iFrameSkipType = fskipType;
		} else {
			g_Config.iFrameSkipType = 0;
		}

		auto gr = GetI18NCategory(I18NCat::GRAPHICS);

		std::ostringstream messageStream;
		messageStream << gr->T("Frame Skipping Type") << ":" << " ";

		if (g_Config.iFrameSkipType == 0)
			messageStream << gr->T("Number of Frames");
		else
			messageStream << gr->T("Percent of FPS");

		g_OSD.Show(OSDType::MESSAGE_INFO, messageStream.str());
	}

	static void RestartApp() {
		if (System_GetPropertyBool(SYSPROP_DEBUGGER_PRESENT)) {
			PostMessage(MainWindow::GetHWND(), WM_USER_RESTART_EMUTHREAD, 0, 0);
		} else {
			g_Config.bRestartRequired = true;
			PostMessage(MainWindow::GetHWND(), WM_USER_DESTROY, 0, 0);
		}
	}

	void MainWindowMenu_Process(HWND hWnd, WPARAM wParam) {
		std::string fn;

		auto gr = GetI18NCategory(I18NCat::GRAPHICS);

		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId) {
		case ID_FILE_LOAD:
			BrowseAndBoot(NON_EPHEMERAL_TOKEN, "", false);
			break;

		case ID_FILE_LOAD_DIR:
			BrowseAndBoot(NON_EPHEMERAL_TOKEN, "", true);
			break;

		case ID_FILE_LOAD_MEMSTICK:
			BrowseAndBoot(NON_EPHEMERAL_TOKEN, GetSysDirectory(DIRECTORY_GAME).ToString());
			break;

		case ID_FILE_OPEN_NEW_INSTANCE:
			W32Util::SpawnNewInstance(false);
			break;

		case ID_FILE_MEMSTICK:
			ShellExecute(NULL, L"open", g_Config.memStickDirectory.ToWString().c_str(), 0, 0, SW_SHOWNORMAL);
			break;

		case ID_TOGGLE_BREAK:
			if (GetUIState() == UISTATE_PAUSEMENU) {
				// Causes hang (outdated comment?)
				// System_PostUIMessage(UIMessage::REQUEST_GAME_RUN, "");

				if (disasmWindow)
					SendMessage(disasmWindow->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
			} else if (Core_IsStepping()) { // It is paused, then continue to run.
				if (disasmWindow)
					SendMessage(disasmWindow->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
				else
					Core_Resume();
			} else {
				if (disasmWindow)
					SendMessage(disasmWindow->GetDlgHandle(), WM_COMMAND, IDC_STOPGO, 0);
				else
					Core_Break(BreakReason::DebugBreak, 0);
			}
			noFocusPause = !noFocusPause;	// If we pause, override pause on lost focus
			break;

		case ID_EMULATION_PAUSE:
			System_PostUIMessage(UIMessage::REQUEST_GAME_PAUSE);
			break;

		case ID_EMULATION_STOP:
			System_PostUIMessage(UIMessage::REQUEST_GAME_STOP);
			break;

		case ID_EMULATION_RESET:
			System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
			break;

		case ID_EMULATION_SWITCH_UMD:
			UmdSwitchAction(NON_EPHEMERAL_TOKEN);
			break;

		case ID_EMULATION_ROTATION_H:   g_Config.iInternalScreenRotation = ROTATION_LOCKED_HORIZONTAL; break;
		case ID_EMULATION_ROTATION_V:   g_Config.iInternalScreenRotation = ROTATION_LOCKED_VERTICAL; break;
		case ID_EMULATION_ROTATION_H_R: g_Config.iInternalScreenRotation = ROTATION_LOCKED_HORIZONTAL180; break;
		case ID_EMULATION_ROTATION_V_R: g_Config.iInternalScreenRotation = ROTATION_LOCKED_VERTICAL180; break;

		case ID_EMULATION_CHEATS:
			g_Config.bEnableCheats = !g_Config.bEnableCheats;
			g_OSD.ShowOnOff(gr->T("Cheats"), g_Config.bEnableCheats);
			break;

		case ID_EMULATION_CHAT:
			if (GetUIState() == UISTATE_INGAME) {
				System_PostUIMessage(UIMessage::SHOW_CHAT_SCREEN);
			}
			break;

		case ID_FILE_LOADSTATEFILE:
			if (!Achievements::WarnUserIfHardcoreModeActive(false) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				if (W32Util::BrowseForFileName(true, hWnd, L"Load state", 0, L"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0", L"ppst", fn)) {
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::Load(Path(fn), -1, SaveStateActionFinished);
				}
			}
			break;
		case ID_FILE_SAVESTATEFILE:
			if (!Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				if (W32Util::BrowseForFileName(false, hWnd, L"Save state", 0, L"Save States (*.ppst)\0*.ppst\0All files\0*.*\0\0", L"ppst", fn)) {
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::Save(Path(fn), -1, SaveStateActionFinished);
				}
			}
			break;

		case ID_FILE_SAVESTATE_NEXT_SLOT:
		{
			if (!Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				SaveState::NextSlot();
				System_PostUIMessage(UIMessage::SAVESTATE_DISPLAY_SLOT);
			}
			break;
		}

		case ID_FILE_SAVESTATE_NEXT_SLOT_HC:
		{
			if (!Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				// We let F3 (search next) in the imdebugger take priority, if active.
				if (!KeyMap::PspButtonHasMappings(VIRTKEY_NEXT_SLOT) && !g_Config.bShowImDebugger) {
					SaveState::NextSlot();
					System_PostUIMessage(UIMessage::SAVESTATE_DISPLAY_SLOT);
				}
			}
			break;
		}

		case ID_FILE_SAVESTATE_SLOT_1:
		case ID_FILE_SAVESTATE_SLOT_2:
		case ID_FILE_SAVESTATE_SLOT_3:
		case ID_FILE_SAVESTATE_SLOT_4:
		case ID_FILE_SAVESTATE_SLOT_5:
			if (!Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				g_Config.iCurrentStateSlot = wmId - ID_FILE_SAVESTATE_SLOT_1;
			}
			break;

		case ID_FILE_QUICKLOADSTATE:
			if (!Achievements::WarnUserIfHardcoreModeActive(false) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::LoadSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
			}
			break;

		case ID_FILE_QUICKLOADSTATE_HC:
		{
			if (!Achievements::WarnUserIfHardcoreModeActive(false) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				if (!KeyMap::PspButtonHasMappings(VIRTKEY_LOAD_STATE)) {
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::LoadSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
				}
			}
			break;
		}
		case ID_FILE_QUICKSAVESTATE:
		{
			if (!Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				SetCursor(LoadCursor(0, IDC_WAIT));
				SaveState::SaveSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
			}
			break;
		}

		case ID_FILE_QUICKSAVESTATE_HC:
		{
			if (!Achievements::WarnUserIfHardcoreModeActive(true) && !NetworkWarnUserIfOnlineAndCantSavestate()) {
				if (!KeyMap::PspButtonHasMappings(VIRTKEY_SAVE_STATE))
				{
					SetCursor(LoadCursor(0, IDC_WAIT));
					SaveState::SaveSlot(PSP_CoreParameter().fileToStart, g_Config.iCurrentStateSlot, SaveStateActionFinished);
					break;
				}
			}
			break;
		}

		case ID_OPTIONS_LANGUAGE:
			System_PostUIMessage(UIMessage::SHOW_LANGUAGE_SCREEN);
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
		case ID_OPTIONS_WINDOW5X:   SetWindowSize(5); break;
		case ID_OPTIONS_WINDOW6X:   SetWindowSize(6); break;
		case ID_OPTIONS_WINDOW7X:   SetWindowSize(7); break;
		case ID_OPTIONS_WINDOW8X:   SetWindowSize(8); break;
		case ID_OPTIONS_WINDOW9X:   SetWindowSize(9); break;
		case ID_OPTIONS_WINDOW10X:   SetWindowSize(10); break;

		case ID_OPTIONS_RESOLUTIONDUMMY:
		{
			SetInternalResolution();
			break;
		}

		case ID_OPTIONS_VSYNC:
			g_Config.bVSync = !g_Config.bVSync;
			System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
			break;

		case ID_OPTIONS_FRAMESKIP_AUTO:
			g_Config.bAutoFrameSkip = !g_Config.bAutoFrameSkip;
			if (g_Config.bAutoFrameSkip && g_Config.bSkipBufferEffects) {
				setSkipBufferEffects(false);
			}
			break;

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
			System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
			break;

		case ID_OPTIONS_DIRECT3D11:
			g_Config.iGPUBackend = (int)GPUBackend::DIRECT3D11;
			g_Config.Save("gpu_choice");
			RestartApp();
			break;

		case ID_OPTIONS_OPENGL:
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			g_Config.Save("gpu_choice");
			RestartApp();
			break;

		case ID_OPTIONS_VULKAN:
			g_Config.iGPUBackend = (int)GPUBackend::VULKAN;
			g_Config.Save("gpu_choice");
			RestartApp();
			break;

		case ID_OPTIONS_SKIP_BUFFER_EFFECTS:
			setSkipBufferEffects(!g_Config.bSkipBufferEffects);
			g_OSD.ShowOnOff(gr->T("Skip Buffer Effects"), g_Config.bSkipBufferEffects);
			break;

		case ID_DEBUG_SHOWDEBUGSTATISTICS:
			// This is still useful as a shortcut to tell users to use.
			// So let's fake the enum.
			if ((DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS) {
				g_Config.iDebugOverlay = (int)DebugOverlay::OFF;
			} else {
				g_Config.iDebugOverlay = (int)DebugOverlay::DEBUG_STATS;
			}
			System_PostUIMessage(UIMessage::REQUEST_CLEAR_JIT);
			break;

		case ID_OPTIONS_HARDWARETRANSFORM:
			System_RunOnMainThread([]() {
				auto gr = GetI18NCategory(I18NCat::GRAPHICS);
				g_Config.bHardwareTransform = !g_Config.bHardwareTransform;
				System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
				g_OSD.ShowOnOff(gr->T("Hardware Transform"), g_Config.bHardwareTransform);
			});
			break;

		case ID_OPTIONS_DISPLAY_LAYOUT:
			System_PostUIMessage(UIMessage::SHOW_DISPLAY_LAYOUT_EDITOR);
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

		case ID_OPTIONS_FRAMESKIPTYPE_COUNT:    setFrameSkippingType(FRAMESKIPTYPE_COUNT); break;
		case ID_OPTIONS_FRAMESKIPTYPE_PRCNT:    setFrameSkippingType(FRAMESKIPTYPE_PRCNT); break;

		case ID_FILE_EXIT:
			if (MainWindow::ConfirmExit(hWnd)) {
				DestroyWindow(hWnd);
			}
			break;

		case ID_DEBUG_BREAKONLOAD:
			g_Config.bAutoRun = !g_Config.bAutoRun;
			break;

		case ID_DEBUG_DUMPNEXTFRAME:
			System_PostUIMessage(UIMessage::REQUEST_GPU_DUMP_NEXT_FRAME);
			break;

		case ID_DEBUG_LOADMAPFILE:
			if (W32Util::BrowseForFileName(true, hWnd, L"Load .ppmap", 0, L"Maps\0*.ppmap\0All files\0*.*\0\0", L"ppmap", fn)) {
				g_symbolMap->LoadSymbolMap(Path(fn));
				NotifyDebuggerMapLoaded();
			}
			break;

		case ID_DEBUG_SAVEMAPFILE:
			if (W32Util::BrowseForFileName(false, hWnd, L"Save .ppmap", 0, L"Maps\0*.ppmap\0All files\0*.*\0\0", L"ppmap", fn))
				g_symbolMap->SaveSymbolMap(Path(fn));
			break;

		case ID_DEBUG_LOADSYMFILE:
			if (W32Util::BrowseForFileName(true, hWnd, L"Load .sym", 0, L"Symbols\0*.sym\0All files\0*.*\0\0", L"sym", fn)) {
				g_symbolMap->LoadNocashSym(Path(fn));
				NotifyDebuggerMapLoaded();
			}
			break;

		case ID_DEBUG_SAVESYMFILE:
			if (W32Util::BrowseForFileName(false, hWnd, L"Save .sym", 0, L"Symbols\0*.sym\0All files\0*.*\0\0", L"sym", fn))
				g_symbolMap->SaveNocashSym(Path(fn));
			break;

		case ID_DEBUG_RESETSYMBOLTABLE:
			g_symbolMap->Clear();
			NotifyDebuggerMapLoaded();
			break;

		case ID_DEBUG_DISASSEMBLY:
			CreateDisasmWindow();
			if (disasmWindow)
				disasmWindow->Show(true);
			break;

		case ID_DEBUG_GEDEBUGGER:
#if PPSSPP_API(ANY_GL)
			CreateGeDebuggerWindow();
			if (geDebuggerWindow)
				geDebuggerWindow->Show(true);
#endif
			break;

		case ID_DEBUG_MEMORYVIEW:
			CreateMemoryWindow();
			if (memoryWindow)
				memoryWindow->Show(true);
			break;

		case ID_DEBUG_MEMORYBASE:
		{
			System_CopyStringToClipboard(StringFromFormat("%016llx", (uint64_t)(uintptr_t)Memory::base));
			break;
		}

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
				fn.clear();
			}

			PSPFileInfo info = pspFileSystem.GetFileInfo(filename);
			if (!info.exists) {
				MessageBox(hWnd, L"File does not exist.", L"Sorry", 0);
			} else if (info.type == FILETYPE_DIRECTORY) {
				MessageBox(hWnd, L"Cannot extract directories.", L"Sorry", 0);
			} else if (W32Util::BrowseForFileName(false, hWnd, L"Save file as...", 0, L"All files\0*.*\0\0", L"", fn)) {
				u32 handle = pspFileSystem.OpenFile(filename, FILEACCESS_READ, "");
				// Note: len may be in blocks.
				size_t len = pspFileSystem.SeekFile(handle, 0, FILEMOVE_END);
				bool isBlockMode = pspFileSystem.DevType(handle) & PSPDevType::BLOCK;

				FILE *fp = File::OpenCFile(Path(fn), "wb");
				pspFileSystem.SeekFile(handle, 0, FILEMOVE_BEGIN);
				u8 buffer[4096];
				size_t bufferSize = isBlockMode ? sizeof(buffer) / 2048 : sizeof(buffer);
				while (len > 0) {
					// This is all in blocks, not bytes, if isBlockMode.
					size_t remain = std::min(len, bufferSize);
					size_t readSize = pspFileSystem.ReadFile(handle, buffer, remain);
					if (readSize == 0)
						break;
					size_t bytes = isBlockMode ? readSize * 2048 : readSize;
					fwrite(buffer, 1, bytes, fp);
					len -= readSize;
				}
				pspFileSystem.CloseFile(handle);
				fclose(fp);
			}
		}
		break;

		case ID_DEBUG_LOG:
			g_logManager.GetConsoleListener()->Show(g_logManager.GetConsoleListener()->Hidden());
			break;

		case ID_DEBUG_IGNOREILLEGALREADS:
			g_Config.bIgnoreBadMemAccess = !g_Config.bIgnoreBadMemAccess;
			break;

		case ID_OPTIONS_FULLSCREEN:
			if (!g_Config.bShowImDebugger) {
				SendToggleFullscreen(!g_Config.UseFullScreen());
			}
			break;

		case ID_OPTIONS_TEXTUREFILTERING_AUTO:   g_Config.iTexFiltering = TEX_FILTER_AUTO; break;
		case ID_OPTIONS_NEARESTFILTERING:        g_Config.iTexFiltering = TEX_FILTER_FORCE_NEAREST; break;
		case ID_OPTIONS_LINEARFILTERING:         g_Config.iTexFiltering = TEX_FILTER_FORCE_LINEAR; break;
		case ID_OPTIONS_AUTOMAXQUALITYFILTERING: g_Config.iTexFiltering = TEX_FILTER_AUTO_MAX_QUALITY; break;

		case ID_OPTIONS_SMART2DTEXTUREFILTERING: g_Config.bSmart2DTexFiltering = !g_Config.bSmart2DTexFiltering; break;

		case ID_OPTIONS_BUFLINEARFILTER:  g_Config.iDisplayFilter = SCALE_LINEAR; break;
		case ID_OPTIONS_BUFNEARESTFILTER: g_Config.iDisplayFilter = SCALE_NEAREST; break;

		case ID_OPTIONS_TOPMOST:
			g_Config.bTopMost = !g_Config.bTopMost;
			W32Util::MakeTopMost(hWnd, g_Config.bTopMost);
			break;

		case ID_OPTIONS_PAUSE_FOCUS:
			g_Config.bPauseOnLostFocus = !g_Config.bPauseOnLostFocus;
			break;

		case ID_OPTIONS_CONTROLS:
			System_PostUIMessage(UIMessage::SHOW_CONTROL_MAPPING);
			break;

		case ID_OPTIONS_MORE_SETTINGS:
			System_PostUIMessage(UIMessage::SHOW_SETTINGS);
			break;

		case ID_EMULATION_SOUND:
			g_Config.bEnableSound = !g_Config.bEnableSound;
			break;

		case ID_HELP_OPENWEBSITE:
			ShellExecute(NULL, L"open", L"https://www.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_BUYGOLD:
			ShellExecute(NULL, L"open", L"https://www.ppsspp.org/buygold", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_OPENFORUM:
			ShellExecute(NULL, L"open", L"https://forums.ppsspp.org/", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_GITHUB:
			ShellExecute(NULL, L"open", L"https://github.com/hrydgard/ppsspp/", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_DISCORD:
			ShellExecute(NULL, L"open", L"https://discord.gg/5NJB6dD", NULL, NULL, SW_SHOWNORMAL);
			break;

		case ID_HELP_ABOUT:
			DialogManager::EnableAll(FALSE);
			DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)AboutDlgProc);
			DialogManager::EnableAll(TRUE);
			break;

		case ID_DEBUG_TAKESCREENSHOT:
			g_TakeScreenshot = true;
			break;

		case ID_DEBUG_RESTARTGRAPHICS:
			System_PostUIMessage(UIMessage::RESTART_GRAPHICS);
			break;

		case ID_FILE_DUMPFRAMES:
			g_Config.bDumpFrames = !g_Config.bDumpFrames;
			break;

		case ID_FILE_USEFFV1:
			g_Config.bUseFFV1 = !g_Config.bUseFFV1;
			break;

		case ID_FILE_DUMP_VIDEO_OUTPUT:
			g_Config.bDumpVideoOutput = !g_Config.bDumpVideoOutput;
			break;

		case ID_FILE_DUMPAUDIO:
			g_Config.bDumpAudio = !g_Config.bDumpAudio;
			break;

		default:
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
			if (rc_client_raintegration_activate_menu_item(Achievements::GetClient(), LOWORD(wParam))) {
				break;
			}
#endif
			MessageBox(hWnd, L"Unhandled menu item", L"Sorry", 0);
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
		CHECKITEM(ID_DEBUG_SHOWDEBUGSTATISTICS, (DebugOverlay)g_Config.iDebugOverlay == DebugOverlay::DEBUG_STATS);
		CHECKITEM(ID_OPTIONS_HARDWARETRANSFORM, g_Config.bHardwareTransform);
		CHECKITEM(ID_DEBUG_BREAKONLOAD, !g_Config.bAutoRun);
		CHECKITEM(ID_OPTIONS_FRAMESKIP_AUTO, g_Config.bAutoFrameSkip);
		CHECKITEM(ID_OPTIONS_FRAMESKIP, g_Config.iFrameSkip != FRAMESKIP_OFF);
		CHECKITEM(ID_OPTIONS_FRAMESKIPTYPE_COUNT, g_Config.iFrameSkipType == FRAMESKIPTYPE_COUNT);
		CHECKITEM(ID_OPTIONS_FRAMESKIPTYPE_PRCNT, g_Config.iFrameSkipType == FRAMESKIPTYPE_PRCNT);
		CHECKITEM(ID_OPTIONS_VSYNC, g_Config.bVSync);
		CHECKITEM(ID_OPTIONS_TOPMOST, g_Config.bTopMost);
		CHECKITEM(ID_OPTIONS_PAUSE_FOCUS, g_Config.bPauseOnLostFocus);
		CHECKITEM(ID_OPTIONS_SMART2DTEXTUREFILTERING, g_Config.bSmart2DTexFiltering);
		CHECKITEM(ID_EMULATION_SOUND, g_Config.bEnableSound);
		CHECKITEM(ID_TEXTURESCALING_DEPOSTERIZE, g_Config.bTexDeposterize);
		CHECKITEM(ID_EMULATION_CHEATS, g_Config.bEnableCheats);
		CHECKITEM(ID_OPTIONS_IGNOREWINKEY, g_Config.bIgnoreWindowsKey);
		CHECKITEM(ID_FILE_DUMPFRAMES, g_Config.bDumpFrames);
		CHECKITEM(ID_FILE_USEFFV1, g_Config.bUseFFV1);
		CHECKITEM(ID_FILE_DUMP_VIDEO_OUTPUT, g_Config.bDumpVideoOutput);
		CHECKITEM(ID_FILE_DUMPAUDIO, g_Config.bDumpAudio);
		CHECKITEM(ID_OPTIONS_SKIP_BUFFER_EFFECTS, g_Config.bSkipBufferEffects);

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

		static const int windowSizeItems[10] = {
			ID_OPTIONS_WINDOW1X,
			ID_OPTIONS_WINDOW2X,
			ID_OPTIONS_WINDOW3X,
			ID_OPTIONS_WINDOW4X,
			ID_OPTIONS_WINDOW5X,
			ID_OPTIONS_WINDOW6X,
			ID_OPTIONS_WINDOW7X,
			ID_OPTIONS_WINDOW8X,
			ID_OPTIONS_WINDOW9X,
			ID_OPTIONS_WINDOW10X,
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
			ID_TEXTURESCALING_OFF,
			ID_TEXTURESCALING_2X,
			ID_TEXTURESCALING_3X,
			ID_TEXTURESCALING_4X,
			ID_TEXTURESCALING_5X,
		};
		if (g_Config.iTexScalingLevel < TEXSCALING_OFF)
			g_Config.iTexScalingLevel = TEXSCALING_OFF;

		else if (g_Config.iTexScalingLevel > TEXSCALING_MAX)
			g_Config.iTexScalingLevel = TEXSCALING_MAX;

		for (int i = 0; i < ARRAY_SIZE(texscalingitems); i++) {
			// OFF is 1, skip 0.
			bool selected = i + 1 == g_Config.iTexScalingLevel;
			CheckMenuItem(menu, texscalingitems[i], MF_BYCOMMAND | (selected ? MF_CHECKED : MF_UNCHECKED));
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
			ID_OPTIONS_AUTOMAXQUALITYFILTERING,
		};
		if (g_Config.iTexFiltering < TEX_FILTER_AUTO)
			g_Config.iTexFiltering = TEX_FILTER_AUTO;
		else if (g_Config.iTexFiltering > TEX_FILTER_AUTO_MAX_QUALITY)
			g_Config.iTexFiltering = TEX_FILTER_AUTO_MAX_QUALITY;

		for (int i = 0; i < ARRAY_SIZE(texfilteringitems); i++) {
			CheckMenuItem(menu, texfilteringitems[i], MF_BYCOMMAND | ((i + 1) == g_Config.iTexFiltering ? MF_CHECKED : MF_UNCHECKED));
		}

		static const int bufferfilteritems[] = {
			ID_OPTIONS_BUFLINEARFILTER,
			ID_OPTIONS_BUFNEARESTFILTER,
		};
		if (g_Config.iDisplayFilter < SCALE_LINEAR)
			g_Config.iDisplayFilter = SCALE_LINEAR;

		else if (g_Config.iDisplayFilter > SCALE_NEAREST)
			g_Config.iDisplayFilter = SCALE_NEAREST;

		for (int i = 0; i < ARRAY_SIZE(bufferfilteritems); i++) {
			CheckMenuItem(menu, bufferfilteritems[i], MF_BYCOMMAND | ((i + 1) == g_Config.iDisplayFilter ? MF_CHECKED : MF_UNCHECKED));
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

		static const int frameskippingType[] = {
			ID_OPTIONS_FRAMESKIPTYPE_COUNT,
			ID_OPTIONS_FRAMESKIPTYPE_PRCNT,
		};

		if (g_Config.iFrameSkip < FRAMESKIP_OFF)
			g_Config.iFrameSkip = FRAMESKIP_OFF;

		else if (g_Config.iFrameSkip > FRAMESKIP_MAX)
			g_Config.iFrameSkip = FRAMESKIP_MAX;

		for (int i = 0; i < ARRAY_SIZE(frameskipping); i++) {
			CheckMenuItem(menu, frameskipping[i], MF_BYCOMMAND | ((i == g_Config.iFrameSkip) ? MF_CHECKED : MF_UNCHECKED));
		}

		for (int i = 0; i < ARRAY_SIZE(frameskippingType); i++) {
			CheckMenuItem(menu, frameskippingType[i], MF_BYCOMMAND | ((i == g_Config.iFrameSkipType) ? MF_CHECKED : MF_UNCHECKED));
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

		bool allowD3D11 = g_Config.IsBackendEnabled(GPUBackend::DIRECT3D11);
		bool allowOpenGL = g_Config.IsBackendEnabled(GPUBackend::OPENGL);
		bool allowVulkan = g_Config.IsBackendEnabled(GPUBackend::VULKAN);

		switch (GetGPUBackend()) {
		case GPUBackend::OPENGL:
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D11, allowD3D11 ? MF_ENABLED : MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_OPENGL, MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_VULKAN, allowVulkan ? MF_ENABLED : MF_GRAYED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_OPENGL, MF_CHECKED);
			CheckMenuItem(menu, ID_OPTIONS_VULKAN, MF_UNCHECKED);
			break;
		case GPUBackend::VULKAN:
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D11, allowD3D11 ? MF_ENABLED : MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_OPENGL, allowOpenGL ? MF_ENABLED : MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_VULKAN, MF_GRAYED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_OPENGL, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_VULKAN, MF_CHECKED);
			break;
		case GPUBackend::DIRECT3D11:
			EnableMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_OPENGL, allowOpenGL ? MF_ENABLED : MF_GRAYED);
			EnableMenuItem(menu, ID_OPTIONS_VULKAN, allowVulkan ? MF_ENABLED : MF_GRAYED);
			CheckMenuItem(menu, ID_OPTIONS_DIRECT3D11, MF_CHECKED);
			CheckMenuItem(menu, ID_OPTIONS_OPENGL, MF_UNCHECKED);
			CheckMenuItem(menu, ID_OPTIONS_VULKAN, MF_UNCHECKED);
			break;
		}

#if !PPSSPP_API(ANY_GL)
		EnableMenuItem(menu, ID_DEBUG_GEDEBUGGER, MF_GRAYED);
#endif

		UpdateCommands();
	}

	void UpdateCommands() {
		static GlobalUIState lastGlobalUIState = UISTATE_PAUSEMENU;
		static CoreState lastCoreState = CORE_BOOT_ERROR;

		HMENU menu = GetMenu(GetHWND());
		EnableMenuItem(menu, ID_DEBUG_LOG, g_Config.bEnableLogging ? MF_ENABLED : MF_GRAYED);
		SetIngameMenuItemStates(menu, GetUIState());

		if (lastGlobalUIState == GetUIState() && lastCoreState == coreState)
			return;

		lastCoreState = coreState;
		lastGlobalUIState = GetUIState();

		bool isPaused = Core_IsStepping() && GetUIState() == UISTATE_INGAME;
		TranslateMenuItem(menu, ID_TOGGLE_BREAK, L"\tF8", isPaused ? "Run" : "Break");
	}

	void UpdateSwitchUMD() {
		HMENU menu = GetMenu(GetHWND());
		GlobalUIState state = GetUIState();
		UINT umdSwitchEnable = state == UISTATE_INGAME && getUMDReplacePermit() ? MF_ENABLED : MF_GRAYED;
		EnableMenuItem(menu, ID_EMULATION_SWITCH_UMD, umdSwitchEnable);
	}

	// Message handler for about box.
	LRESULT CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
		switch (message) {
			case WM_INITDIALOG:
			{
				W32Util::CenterWindow(hDlg);
				HWND versionBox = GetDlgItem(hDlg, IDC_VERSION);
				std::string windowText = System_GetPropertyBool(SYSPROP_APP_GOLD) ? "PPSSPP Gold " : "PPSSPP ";
				windowText.append(PPSSPP_GIT_VERSION);
				SetWindowText(versionBox, ConvertUTF8ToWString(windowText).c_str());
				DarkModeInitDialog(hDlg);
				return TRUE;
			}

			case WM_COMMAND:
			{
				if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
					EndDialog(hDlg, LOWORD(wParam));
					return TRUE;
				}
				break;
				return FALSE;
			}

			default:
				return DarkModeDlgProc(hDlg, message, wParam, lParam);
		}
		return FALSE;
	}
}

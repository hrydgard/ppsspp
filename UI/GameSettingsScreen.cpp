
// Copyright (c) 2013- PPSSPP Project.

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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Common/Common.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/Path.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/ScreenManager.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Instance.h"
#include "Core/KeyMap.h"
#include "Core/Reporting.h"
#include "GPU/Common/PostShader.h"
#include "UI/AdhocServerScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/GamepadEmu.h"
#include "UI/GameSettingsScreen.h"
#include "UI/MiscViews.h"
#include "UI/Settings/MainSettingsPages.h"
#include "UI/Theme.h"

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
#include "Core/Util/DarwinFileSystemServices.h"
#endif

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
void SetMemStickDirDarwin(int requesterToken) {
	auto initialPath = g_Config.memStickDirectory;
	INFO_LOG(Log::System, "Current path: %s", initialPath.c_str());
	System_BrowseForFolder(requesterToken, "", initialPath, [](std::string_view value, int) {
		INFO_LOG(Log::System, "Selected path: %.*s", STR_VIEW(value));
		DarwinFileSystemServices::setUserPreferredMemoryStickDirectory(Path(value));
	});
}
#endif

GameSettingsScreen::GameSettingsScreen(const Path &gamePath, std::string gameID, bool editThenRestore)
	: SettingsPagesDialogScreen(gamePath, &g_Config.iSettingsCurrentTab, TabDialogFlags::HorizontalOnlyIcons | TabDialogFlags::VerticalShowIcons), gameID_(gameID), editGameSpecificThenRestore_(editThenRestore) {
	if (editGameSpecificThenRestore_) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
		g_Config.LoadGameConfig(gameID_);
	}

	ReadSystemPageState(&systemState_);
}

GameSettingsScreen::~GameSettingsScreen() {
	Reporting::Enable(systemState_.enableReports, "report.ppsspp.org");
	Reporting::UpdateConfig();
	if (!g_Config.Save("GameSettingsScreen::onFinish")) {
		std::string message = "Failed to save settings!\n";
#ifdef MOBILE_DEVICE
		message += "Check permissions, or try to restart the device.";
#else
		message += "Make sure this folder isn't read-only:\n";
		message += g_Config.memStickDirectory.ToVisualString();
#endif
		System_Toast(message);
	}

	if (editGameSpecificThenRestore_) {
		// We already saved above. Just unload the game config.
		// Note that we are leaving the screen here in practice, we don't need to reset the bool.
		g_Config.UnloadGameConfig();
	}

	System_Notify(SystemNotification::UI);

	// Wipe some caches after potentially changing settings.
	// Let's not send resize messages here, handled elsewhere.
	System_PostUIMessage(UIMessage::GPU_CONFIG_CHANGED);
}

void GameSettingsScreen::OpenDeveloperTools() {
	OpenPage("GameSettingsTools", "Developer Tools");
}

void GameSettingsScreen::PreCreateViews() {
	if (RecreatingForPageChange()) {
		return;
	}
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	ReloadAllThemeInfo();
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	CheckMemstickInstallState(&systemState_);
#endif
}

// One per tab.
struct PageDef {
	const char *tag;
	const char *title;  // In MAINSETTINGS.
	const char *icon;
	const char *helpTag;  // If null, there's no help button.
	I18NCat cat;
	// Null for the tabs that also take systemState_, given instead.
	void (*build)(SettingsPage *page);
	void (*buildWithState)(SettingsPage *page, SystemPageState *state);
};

static const PageDef pageDefs[] = {
	{ "GameSettingsGeneral", "General", "I_SLIDERS", nullptr, I18NCat::SYSTEM, nullptr, &BuildGeneralSettings },
	{ "GameSettingsControls", "Controls", "I_CONTROLLER", "controls", I18NCat::CONTROLS, &BuildControlsSettings },
	{ "GameSettingsGraphics", "Graphics", "I_DISPLAY", "graphics", I18NCat::GRAPHICS, &BuildGraphicsSettings },
	{ "GameSettingsAudio", "Audio", "I_SPEAKER_MAX", "audio", I18NCat::AUDIO, &BuildAudioSettings },
	{ "GameSettingsNetworking", "Networking", "I_WIFI", "network", I18NCat::NETWORKING, &BuildNetworkingSettings },
	{ "GameSettingsTools", "Tools", "I_TOOLS", "tools", I18NCat::MAINSETTINGS, &BuildToolsSettings },
	{ "GameSettingsSystem", "System", "I_PSP", "system", I18NCat::SYSTEM, nullptr, &BuildSystemSettings },
	{ "GameSettingsVR", "VR", nullptr, nullptr, I18NCat::VR, &BuildVRSettings },
};

void GameSettingsScreen::CreateTabs() {
	using namespace UI;

	const SystemDeviceType deviceType = (SystemDeviceType)System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
	const bool showVR = deviceType == DEVICE_TYPE_VR || g_Config.bForceVR;

	for (const PageDef &def : pageDefs) {
		if (def.build == &BuildVRSettings && !showVR) {
			continue;
		}
		std::function<void(SettingsPage *)> build = def.build;
		if (!build) {
			build = [this, buildWithState = def.buildWithState](SettingsPage *page) {
				buildWithState(page, &systemState_);
			};
		}
		AddSettingsTab(def.tag, T(I18NCat::MAINSETTINGS, def.title), def.icon ? ImageID(def.icon) : ImageID::invalid(), def.cat, build, def.helpTag);
	}
}

void GameSettingsScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	if (equals(dialog->tag(), "NewLanguage") && result == DR_OK) {
		screenManager()->RecreateAllViews();
		return;
	}

	bool recreate = false;

	if (equals(dialog->tag(), "AdhocServer")) {
		const AdhocServerScreen *scr = dynamic_cast<const AdhocServerScreen *>(dialog);
		if (scr && scr->RecreateParent()) {
			recreate = true;
		}
	}

	if (result == DialogResult::DR_OK) {
		if (equals(dialog->tag(), "RestoreSettingsScreen")) {
			ReadSystemPageState(&systemState_);
		}
		recreate = true;
	}

	// The analog alternative speed is only shown when it's mapped.
	if (equals(dialog->tag(), "ControlMapping")) {
		recreate = true;
	}

	if (recreate) {
		RecreateViews();
	}
}

void TriggerRestart(const char *why, bool editThenRestore, const Path &gamePath) {
	// Extra save here to make sure the choice really gets saved even if there are shutdown bugs in
	// the GPU backend code.
	g_Config.Save(why);
	std::string param = "--start-screen=gamesettings";
	if (editThenRestore) {
		// We won't pass the gameID, so don't resume back into settings.
		param.clear();
	} else if (!gamePath.empty()) {
		param += " \"" + ReplaceAll(ReplaceAll(gamePath.ToString(), "\\", "\\\\"), "\"", "\\\"") + "\"";
	}
	// Make sure the new instance is considered the first.
	ShutdownInstanceCounter();
	System_RestartApp(param);
}

// From PauseScreen
std::string GetConfirmExitMessage();

void GameSettingsScreen::RestartOrDo(std::function<void()> callback) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	std::string confirmExitMessage = GetConfirmExitMessage();
	if (!confirmExitMessage.empty()) {
		confirmExitMessage = "\n\n" + confirmExitMessage;
	}
	confirmExitMessage = std::string(di->T("Changing this setting requires PPSSPP to restart.")) + confirmExitMessage;

	screenManager()->push(new UI::MessagePopupScreen(di->T("Restart"), confirmExitMessage, di->T("Restart"), di->T("Cancel"), [=](bool yes) {
		if (yes) {
			TriggerRestart("GameSettingsScreen::RenderingBackendYes", editGameSpecificThenRestore_, gamePath_);
		} else {
			callback();
		}
	}));
}

void GestureMappingScreen::CreateTabs() {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	AddTab("Gesture", di->T("Left side"), [this](UI::LinearLayout *parent) { CreateGestureTab(parent, 0, GetDeviceOrientation() == DeviceOrientation::Portrait); });
	AddTab("Gesture", di->T("Right side"), [this](UI::LinearLayout *parent) { CreateGestureTab(parent, 1, GetDeviceOrientation() == DeviceOrientation::Portrait); });
}

void GestureMappingScreen::CreateGestureTab(UI::LinearLayout *vert, int zoneIndex, bool portrait) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto mc = GetI18NCategory(I18NCat::MAPPABLECONTROLS);

	static const char *gestureButton[ARRAY_SIZE(GestureKey::keyList) + 1];
	gestureButton[0] = "None";
	for (int i = 1; i < ARRAY_SIZE(gestureButton); ++i) {
		gestureButton[i] = KeyMap::GetPspButtonNameCharPointer(GestureKey::keyList[i - 1]);
	}

	GestureControlConfig &zone = g_Config.gestureControls[zoneIndex];

	TopBarFlags flags = TopBarFlags::NoBackButton;
	if (portrait) {
		flags |= TopBarFlags::Portrait;
	}
	vert->Add(new TopBar(*screenManager()->getUIContext(), flags, ApplySafeSubstitutions("%1: %2", co->T("Gesture"), di->T(zoneIndex == 0 ? "Left side" : "Right side"))));

	ChoiceGroupList *list = vert->Add(new ChoiceGroupList());

	list->Add(new CheckBox(&zone.bGestureControlEnabled, co->T("Enable gesture control")))->SetRoundCorners();

	ChoiceGroup *swipeGroup = list->Add(new ChoiceGroup(co->T("Swipe")));

	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeUp, mc->T("Swipe Up"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeDown, mc->T("Swipe Down"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeLeft, mc->T("Swipe Left"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupMultiChoice(&zone.iSwipeRight, mc->T("Swipe Right"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupSliderChoiceFloat(&zone.fSwipeSensitivity, 0.01f, 2.0f, 1.0f, co->T("Swipe sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);
	swipeGroup->Add(new PopupSliderChoiceFloat(&zone.fSwipeSmoothing, 0.0f, 0.95f, 0.3f, co->T("Swipe smoothing"), 0.05f, screenManager(), "x"))->SetEnabledPtr(&zone.bGestureControlEnabled);

	ChoiceGroup *doubleTapGroup = list->Add(new ChoiceGroup(co->T("Double tap")));
	doubleTapGroup->Add(new PopupMultiChoice(&zone.iDoubleTapGesture, mc->T("Double tap button"), gestureButton, 0, ARRAY_SIZE(gestureButton), I18NCat::MAPPABLECONTROLS, screenManager()));

	ChoiceGroup *analogGroup = list->Add(new ChoiceGroup(co->T("Analog Stick")));
	analogGroup->Add(new CheckBox(&zone.bAnalogGesture, co->T("Enable analog stick gesture")));
	analogGroup->Add(new PopupSliderChoiceFloat(&zone.fAnalogGestureSensitivity, 0.01f, 5.0f, 1.0f, co->T("Sensitivity"), 0.01f, screenManager(), "x"))->SetEnabledPtr(&zone.bAnalogGesture);
}

RestoreSettingsScreen::RestoreSettingsScreen(std::string_view title)
	: PopupScreen(title, T(I18NCat::DIALOG, "OK"), T(I18NCat::DIALOG, "Cancel")) {}

void RestoreSettingsScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	// Carefully re-use various translations.
	auto ga = GetI18NCategory(I18NCat::GAME);
	auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	auto dev = GetI18NCategory(I18NCat::DEVELOPER);

	std::string_view text = dev->T(
		"RestoreDefaultSettings",
		"Restore these settings back to their defaults?\nYou can't undo this.\nPlease restart PPSSPP after restoring settings.");

	TextView *textView = parent->Add(new TextView(text, FLAG_WRAP_TEXT, false));
	textView->SetPadding(Margins(10));

	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::SETTINGS, ga->T("Game Settings")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::CONTROLS, ms->T("Controls")));
	parent->Add(new BitCheckBox(&restoreFlags_, (int)RestoreSettingsBits::RECENT, mm->T("Recent")));
}

void RestoreSettingsScreen::OnCompleted(DialogResult result) {
	if (result == DialogResult::DR_OK) {
		g_Config.RestoreDefaults((RestoreSettingsBits)restoreFlags_);
	}
}

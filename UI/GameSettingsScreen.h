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

#pragma once

#include "ppsspp_config.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ui/ui_screen.h"
#include "UI/MiscScreens.h"

class SettingInfoMessage;

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class GameSettingsScreen : public UIDialogScreenWithGameBackground {
public:
	GameSettingsScreen(std::string gamePath, std::string gameID = "", bool editThenRestore = false);

	void update() override;
	void onFinish(DialogResult result) override;
	void sendMessage(const char *message, const char *value) override;
	std::string tag() const override { return "settings"; }

protected:
	void CreateViews() override;
	void CallbackRestoreDefaults(bool yes);
	void CallbackRenderingBackend(bool yes);
	void CallbackRenderingDevice(bool yes);
	void CallbackInflightFrames(bool yes);
#if PPSSPP_PLATFORM(ANDROID)
	void CallbackMemstickFolder(bool yes);
#endif
	bool UseVerticalLayout() const;

private:
	std::string gameID_;
	bool lastVertical_;
	UI::CheckBox *enableReportsCheckbox_;
	UI::Choice *layoutEditorChoice_;
	UI::Choice *postProcChoice_;
	UI::Choice *displayEditor_;
	UI::Choice *backgroundChoice_ = nullptr;
	UI::PopupMultiChoice *resolutionChoice_;
	UI::CheckBox *frameSkipAuto_;
	SettingInfoMessage *settingInfo_;
#ifdef _WIN32
	UI::CheckBox *SavePathInMyDocumentChoice;
	UI::CheckBox *SavePathInOtherChoice;
	// Used to enable/disable the above two options.
	bool installed_;
	bool otherinstalled_;
#endif

	// Event handlers
	UI::EventReturn OnControlMapping(UI::EventParams &e);
	UI::EventReturn OnTouchControlLayout(UI::EventParams &e);
	UI::EventReturn OnDumpNextFrameToLog(UI::EventParams &e);
	UI::EventReturn OnTiltTypeChange(UI::EventParams &e);
	UI::EventReturn OnTiltCustomize(UI::EventParams &e);
	UI::EventReturn OnComboKey(UI::EventParams &e);

	// Global settings handlers
	UI::EventReturn OnLanguage(UI::EventParams &e);
	UI::EventReturn OnLanguageChange(UI::EventParams &e);
	UI::EventReturn OnAutoFrameskip(UI::EventParams &e);
	UI::EventReturn OnPostProcShader(UI::EventParams &e);
	UI::EventReturn OnPostProcShaderChange(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
	UI::EventReturn OnRemoteISO(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat0(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat1(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat2(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat3(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat4(UI::EventParams &e);
	UI::EventReturn OnChangeNickname(UI::EventParams &e);
	UI::EventReturn OnChangeproAdhocServerAddress(UI::EventParams &e);
	UI::EventReturn OnChangeMacAddress(UI::EventParams &e);
	UI::EventReturn OnChangeBackground(UI::EventParams &e);
	UI::EventReturn OnFullscreenChange(UI::EventParams &e);
	UI::EventReturn OnDisplayLayoutEditor(UI::EventParams &e);
	UI::EventReturn OnResolutionChange(UI::EventParams &e);
	UI::EventReturn OnHwScaleChange(UI::EventParams &e);
	UI::EventReturn OnRestoreDefaultSettings(UI::EventParams &e);
	UI::EventReturn OnRenderingMode(UI::EventParams &e);
	UI::EventReturn OnRenderingBackend(UI::EventParams &e);
	UI::EventReturn OnRenderingDevice(UI::EventParams &e);
	UI::EventReturn OnInflightFramesChoice(UI::EventParams &e);
	UI::EventReturn OnCameraDeviceChange(UI::EventParams& e);
	UI::EventReturn OnAudioDevice(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
#if PPSSPP_PLATFORM(ANDROID)
	UI::EventReturn OnChangeMemStickDir(UI::EventParams &e);
#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	UI::EventReturn OnSavePathMydoc(UI::EventParams &e);
	UI::EventReturn OnSavePathOther(UI::EventParams &e);
#endif
	UI::EventReturn OnSoftwareRendering(UI::EventParams &e);
	UI::EventReturn OnHardwareTransform(UI::EventParams &e);

	UI::EventReturn OnScreenRotation(UI::EventParams &e);
	UI::EventReturn OnImmersiveModeChange(UI::EventParams &e);
	UI::EventReturn OnSustainedPerformanceModeChange(UI::EventParams &e);

	UI::EventReturn OnAdhocGuides(UI::EventParams &e);

	UI::EventReturn OnSavedataManager(UI::EventParams &e);
	UI::EventReturn OnSysInfo(UI::EventParams &e);

	// Temporaries to convert setting types.
	int iAlternateSpeedPercent1_;
	int iAlternateSpeedPercent2_;
	bool enableReports_;

	//edit the game-specific settings and restore the global settings after exiting
	bool editThenRestore_;

	// Cached booleans
	bool vtxCacheEnable_;
	bool postProcEnable_;
	bool resolutionEnable_;
	bool bloomHackEnable_;
	bool tessHWEnable_;
	int prevInflightFrames_;

#if PPSSPP_PLATFORM(ANDROID)
	std::string pendingMemstickFolder_;
#endif
};

class SettingInfoMessage : public UI::LinearLayout {
public:
	SettingInfoMessage(int align, UI::AnchorLayoutParams *lp);

	void SetBottomCutoff(float y) {
		cutOffY_ = y;
	}
	void Show(const std::string &text, UI::View *refView = nullptr);

	void Draw(UIContext &dc);

private:
	UI::TextView *text_ = nullptr;
	double timeShown_ = 0.0;
	float cutOffY_;
};

class DeveloperToolsScreen : public UIDialogScreenWithBackground {
public:
	DeveloperToolsScreen() {}
	void update() override;
	void onFinish(DialogResult result) override;

protected:
	void CreateViews() override;

private:
	UI::EventReturn OnRunCPUTests(UI::EventParams &e);
	UI::EventReturn OnLoggingChanged(UI::EventParams &e);
	UI::EventReturn OnLoadLanguageIni(UI::EventParams &e);
	UI::EventReturn OnSaveLanguageIni(UI::EventParams &e);
	UI::EventReturn OnOpenTexturesIniFile(UI::EventParams &e);
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
	UI::EventReturn OnJitDebugTools(UI::EventParams &e);
	UI::EventReturn OnRemoteDebugger(UI::EventParams &e);
	UI::EventReturn OnGPUDriverTest(UI::EventParams &e);
	UI::EventReturn OnTouchscreenTest(UI::EventParams &e);

	bool allowDebugger_ = false;
	bool canAllowDebugger_ = true;
};

class HostnameSelectScreen : public PopupScreen {
public:
	HostnameSelectScreen(std::string *value, const std::string &title)
		: PopupScreen(title, "OK", "Cancel"), value_(value) {
		resolver_ = std::thread([](HostnameSelectScreen *thiz) {
			thiz->ResolverThread();
		}, this);
	}
	~HostnameSelectScreen() {
		resolverState_ = ResolverState::QUIT;
		resolverCond_.notify_one();
		resolver_.join();
	}

	void CreatePopupContents(UI::ViewGroup *parent) override;

protected:
	void OnCompleted(DialogResult result) override;
	bool CanComplete(DialogResult result) override;

private:
	void ResolverThread();
	void SendEditKey(int keyCode, int flags = 0);
	UI::EventReturn OnNumberClick(UI::EventParams &e);
	UI::EventReturn OnPointClick(UI::EventParams &e);
	UI::EventReturn OnDeleteClick(UI::EventParams &e);
	UI::EventReturn OnDeleteAllClick(UI::EventParams &e);

	enum class ResolverState {
		WAITING,
		QUEUED,
		PROGRESS,
		READY,
		QUIT,
	};

	std::string *value_;
	UI::TextEdit *addrView_ = nullptr;
	UI::TextView *errorView_ = nullptr;
	UI::TextView *progressView_ = nullptr;

	std::thread resolver_;
	ResolverState resolverState_ = ResolverState::WAITING;
	std::mutex resolverLock_;
	std::condition_variable resolverCond_;
	std::string toResolve_ = "";
	bool toResolveResult_ = false;
	std::string lastResolved_ = "";
	bool lastResolvedResult_ = false;
};

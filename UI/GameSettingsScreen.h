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

#include "Common/UI/UIScreen.h"
#include "Core/ConfigValues.h"
#include "UI/MiscScreens.h"
#include "UI/TabbedDialogScreen.h"

class Path;

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class GameSettingsScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	GameSettingsScreen(const Path &gamePath, std::string gameID = "", bool editThenRestore = false);

	void onFinish(DialogResult result) override;
	const char *tag() const override { return "GameSettings"; }

protected:
	void CallbackRestoreDefaults(bool yes);
	void CallbackMemstickFolder(bool yes);
	void dialogFinished(const Screen *dialog, DialogResult result) override;

	void CreateTabs() override;
	bool ShowSearchControls() const override { return true; }

private:
	void PreCreateViews() override;

	void CreateGraphicsSettings(UI::ViewGroup *graphicsSettings);
	void CreateControlsSettings(UI::ViewGroup *tools);
	void CreateAudioSettings(UI::ViewGroup *audioSettings);
	void CreateNetworkingSettings(UI::ViewGroup *networkingSettings);
	void CreateToolsSettings(UI::ViewGroup *tools);
	void CreateSystemSettings(UI::ViewGroup *systemSettings);
	void CreateVRSettings(UI::ViewGroup *vrSettings);

	std::string gameID_;
	UI::CheckBox *enableReportsCheckbox_ = nullptr;
	UI::Choice *layoutEditorChoice_ = nullptr;
	UI::Choice *displayEditor_ = nullptr;
	UI::Choice *backgroundChoice_ = nullptr;
	UI::PopupMultiChoice *resolutionChoice_ = nullptr;
	UI::CheckBox *frameSkipAuto_ = nullptr;
#ifdef _WIN32
	UI::CheckBox *SavePathInMyDocumentChoice = nullptr;
	UI::CheckBox *SavePathInOtherChoice = nullptr;
	// Used to enable/disable the above two options.
	bool installed_ = false;
	bool otherinstalled_ = false;
#endif

	std::string memstickDisplay_;

	// Event handlers
	UI::EventReturn OnControlMapping(UI::EventParams &e);
	UI::EventReturn OnCalibrateAnalogs(UI::EventParams &e);
	UI::EventReturn OnTouchControlLayout(UI::EventParams &e);
	UI::EventReturn OnTiltCustomize(UI::EventParams &e);

	// Global settings handlers
	UI::EventReturn OnAutoFrameskip(UI::EventParams &e);
	UI::EventReturn OnTextureShader(UI::EventParams &e);
	UI::EventReturn OnTextureShaderChange(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat0(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat1(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat2(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat3(UI::EventParams &e);
	UI::EventReturn OnChangeQuickChat4(UI::EventParams &e);
	UI::EventReturn OnChangeNickname(UI::EventParams &e);
	UI::EventReturn OnChangeproAdhocServerAddress(UI::EventParams &e);
	UI::EventReturn OnChangeBackground(UI::EventParams &e);
	UI::EventReturn OnFullscreenChange(UI::EventParams &e);
	UI::EventReturn OnFullscreenMultiChange(UI::EventParams &e);
	UI::EventReturn OnResolutionChange(UI::EventParams &e);
	UI::EventReturn OnRestoreDefaultSettings(UI::EventParams &e);
	UI::EventReturn OnRenderingBackend(UI::EventParams &e);
	UI::EventReturn OnRenderingDevice(UI::EventParams &e);
	UI::EventReturn OnInflightFramesChoice(UI::EventParams &e);
	UI::EventReturn OnCameraDeviceChange(UI::EventParams& e);
	UI::EventReturn OnMicDeviceChange(UI::EventParams& e);
	UI::EventReturn OnAudioDevice(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
	UI::EventReturn OnShowMemstickScreen(UI::EventParams &e);
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	UI::EventReturn OnSavePathMydoc(UI::EventParams &e);
	UI::EventReturn OnSavePathOther(UI::EventParams &e);
#endif
	UI::EventReturn OnScreenRotation(UI::EventParams &e);
	UI::EventReturn OnImmersiveModeChange(UI::EventParams &e);
	UI::EventReturn OnSustainedPerformanceModeChange(UI::EventParams &e);

	UI::EventReturn OnAdhocGuides(UI::EventParams &e);

	// Temporaries to convert setting types, cache enabled, etc.
	int iAlternateSpeedPercent1_ = 0;
	int iAlternateSpeedPercent2_ = 0;
	int iAlternateSpeedPercentAnalog_ = 0;
	int prevInflightFrames_ = -1;
	bool enableReports_ = false;
	bool enableReportsSet_ = false;
	bool analogSpeedMapped_ = false;

	// edit the game-specific settings and restore the global settings after exiting
	bool editThenRestore_ = false;

	// Android-only
	std::string pendingMemstickFolder_;
};

class DeveloperToolsScreen : public UIDialogScreenWithGameBackground {
public:
	DeveloperToolsScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}

	void update() override;
	void onFinish(DialogResult result) override;

	const char *tag() const override { return "DeveloperTools"; }

protected:
	void CreateViews() override;

private:
	UI::EventReturn OnRunCPUTests(UI::EventParams &e);
	UI::EventReturn OnLoggingChanged(UI::EventParams &e);
	UI::EventReturn OnOpenTexturesIniFile(UI::EventParams &e);
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
	UI::EventReturn OnJitDebugTools(UI::EventParams &e);
	UI::EventReturn OnRemoteDebugger(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerEnabled(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerPathChanged(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerFlushTrace(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerClearJitCache(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerClearTracer(UI::EventParams &e);
	UI::EventReturn OnGPUDriverTest(UI::EventParams &e);
	UI::EventReturn OnFramedumpTest(UI::EventParams &e);
	UI::EventReturn OnMemstickTest(UI::EventParams &e);
	UI::EventReturn OnTouchscreenTest(UI::EventParams &e);
	UI::EventReturn OnCopyStatesToRoot(UI::EventParams &e);

	bool allowDebugger_ = false;
	bool canAllowDebugger_ = true;
	enum class HasIni {
		NO,
		YES,
		MAYBE,
	};
	HasIni hasTexturesIni_ = HasIni::MAYBE;

	bool MIPSTracerEnabled_ = false;
	std::string MIPSTracerPath_ = "";
	UI::InfoItem* MIPSTracerPath = nullptr;
};

class HostnameSelectScreen : public PopupScreen {
public:
	HostnameSelectScreen(std::string *value, std::string_view title)
		: PopupScreen(title, "OK", "Cancel"), value_(value) {
		resolver_ = std::thread([](HostnameSelectScreen *thiz) {
			thiz->ResolverThread();
		}, this);
	}
	~HostnameSelectScreen() {
		{
			std::unique_lock<std::mutex> guard(resolverLock_);
			resolverState_ = ResolverState::QUIT;
			resolverCond_.notify_one();
		}
		resolver_.join();
	}

	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "HostnameSelect"; }

protected:
	void OnCompleted(DialogResult result) override;
	bool CanComplete(DialogResult result) override;

private:
	void ResolverThread();
	void SendEditKey(InputKeyCode keyCode, int flags = 0);

	UI::EventReturn OnNumberClick(UI::EventParams &e);
	UI::EventReturn OnPointClick(UI::EventParams &e);
	UI::EventReturn OnDeleteClick(UI::EventParams &e);
	UI::EventReturn OnDeleteAllClick(UI::EventParams &e);
	UI::EventReturn OnEditClick(UI::EventParams& e);
	UI::EventReturn OnShowIPListClick(UI::EventParams& e);
	UI::EventReturn OnIPClick(UI::EventParams& e);

	enum class ResolverState {
		WAITING,
		QUEUED,
		PROGRESS,
		READY,
		QUIT,
	};

	std::string *value_;
	UI::TextEdit *addrView_ = nullptr;
	UI::TextView *progressView_ = nullptr;
	UI::LinearLayout *ipRows_ = nullptr;

	std::thread resolver_;
	ResolverState resolverState_ = ResolverState::WAITING;
	std::mutex resolverLock_;
	std::condition_variable resolverCond_;
	std::string toResolve_ = "";
	bool toResolveResult_ = false;
	std::string lastResolved_ = "";
	bool lastResolvedResult_ = false;
};


class GestureMappingScreen : public UIDialogScreenWithGameBackground {
public:
	GestureMappingScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}
	void CreateViews() override;

	const char *tag() const override { return "GestureMapping"; }
};

class RestoreSettingsScreen : public PopupScreen {
public:
	RestoreSettingsScreen(std::string_view title);
	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "RestoreSettingsScreen"; }
private:
	void OnCompleted(DialogResult result) override;
	int restoreFlags_ = (int)(RestoreSettingsBits::SETTINGS);  // RestoreSettingsBits enum
};

void TriggerRestart(const char *why, bool editThenRestore, const Path &gamePath);

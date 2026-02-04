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
#include "Common/UI/PopupScreens.h"
#include "Core/ConfigValues.h"
#include "UI/BaseScreens.h"
#include "UI/TabbedDialogScreen.h"

class Path;

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class GameSettingsScreen : public UITabbedBaseDialogScreen {
public:
	GameSettingsScreen(const Path &gamePath, std::string gameID = "", bool editThenRestore = false);
	~GameSettingsScreen();

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
#ifdef _WIN32
	UI::CheckBox *SavePathInMyDocumentChoice = nullptr;
	UI::CheckBox *SavePathInOtherChoice = nullptr;
	// Used to enable/disable the above two options.
	bool installed_ = false;
	bool otherinstalled_ = false;
#endif

	std::string memstickDisplay_;

	// Global settings handlers
	void OnChangeQuickChat0(UI::EventParams &e);
	void OnChangeQuickChat1(UI::EventParams &e);
	void OnChangeQuickChat2(UI::EventParams &e);
	void OnChangeQuickChat3(UI::EventParams &e);
	void OnChangeQuickChat4(UI::EventParams &e);
	void OnChangeBackground(UI::EventParams &e);
	void OnRestoreDefaultSettings(UI::EventParams &e);
	void OnRenderingBackend(UI::EventParams &e);
	void OnRenderingDevice(UI::EventParams &e);
	void OnInflightFramesChoice(UI::EventParams &e);
	void OnCameraDeviceChange(UI::EventParams& e);
	void OnMicDeviceChange(UI::EventParams& e);
	void OnAudioDevice(UI::EventParams &e);
	void OnJitAffectingSetting(UI::EventParams &e);
	void OnShowMemstickScreen(UI::EventParams &e);
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	void OnMemoryStickMyDoc(UI::EventParams &e);
	void OnMemoryStickOther(UI::EventParams &e);
#endif
	void OnImmersiveModeChange(UI::EventParams &e);
	void OnSustainedPerformanceModeChange(UI::EventParams &e);

	void OnAdhocGuides(UI::EventParams &e);

	void TriggerRestartOrDo(std::function<void()> callback);

	// Temporaries to convert setting types, cache enabled, etc.
	int iAlternateSpeedPercent1_ = 0;
	int iAlternateSpeedPercent2_ = 0;
	int iAlternateSpeedPercentAnalog_ = 0;
	int prevInflightFrames_ = -1;
	bool enableReports_ = false;
	bool enableReportsSet_ = false;
	bool analogSpeedMapped_ = false;

	// edit the game-specific settings and restore the global settings after exiting
	bool editGameSpecificThenRestore_ = false;

	// Android-only
	std::string pendingMemstickFolder_;
};

class HostnameSelectScreen : public UI::PopupScreen {
public:
	HostnameSelectScreen(std::string *value, std::vector<std::string> *listItems, std::string_view title)
		: UI::PopupScreen(title, "OK", "Cancel"), listItems_(listItems), value_(value) {
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
	void SendEditKey(InputKeyCode keyCode, KeyInputFlags flags = (KeyInputFlags)0);

	void OnNumberClick(UI::EventParams &e);
	void OnPointClick(UI::EventParams &e);
	void OnDeleteClick(UI::EventParams &e);
	void OnDeleteAllClick(UI::EventParams &e);
	void OnEditClick(UI::EventParams& e);
	void OnShowIPListClick(UI::EventParams& e);
	void OnIPClick(UI::EventParams& e);

	enum class ResolverState {
		WAITING,
		QUEUED,
		PROGRESS,
		READY,
		QUIT,
	};

	std::string *value_;
	std::vector<std::string> *listItems_;
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

class GestureMappingScreen : public UITabbedBaseDialogScreen {
public:
	GestureMappingScreen(const Path &gamePath) : UITabbedBaseDialogScreen(gamePath) {}

	void CreateTabs() override;
	const char *tag() const override { return "GestureMapping"; }
	bool ShowSearchControls() const override { return false; }
protected:
	void CreateGestureTab(UI::LinearLayout *parent, int zoneIndex, bool portrait);
};

class RestoreSettingsScreen : public UI::PopupScreen {
public:
	RestoreSettingsScreen(std::string_view title);
	void CreatePopupContents(UI::ViewGroup *parent) override;

	const char *tag() const override { return "RestoreSettingsScreen"; }
private:
	void OnCompleted(DialogResult result) override;
	int restoreFlags_ = (int)(RestoreSettingsBits::SETTINGS);  // RestoreSettingsBits enum
};

void TriggerRestart(const char *why, bool editThenRestore, const Path &gamePath);

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
void SetMemStickDirDarwin(int requesterToken);
#endif

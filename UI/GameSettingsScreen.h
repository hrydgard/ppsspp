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

#include "ui/ui_screen.h"
#include "UI/MiscScreens.h"

// Per-game settings screen - enables you to configure graphic options, control options, etc
// per game.
class GameSettingsScreen : public UIDialogScreenWithGameBackground {
public:
	GameSettingsScreen(std::string gamePath, std::string gameID = "", bool editThenRestore = false);

	virtual void update(InputState &input);
	virtual void onFinish(DialogResult result);

	UI::Event OnRecentChanged;

protected:
	virtual void CreateViews();
	virtual void sendMessage(const char *message, const char *value);
	void CallbackRestoreDefaults(bool yes);
	void CallbackRenderingBackend(bool yes);
	bool UseVerticalLayout() const;

private:
	std::string gameID_;
	//edit the game-specific settings and restore the global settings after exiting
	bool bEditThenRestore;
	bool lastVertical_;
	// As we load metadata in the background, we need to be able to update these after the fact.
	UI::TextView *tvTitle_;
	UI::TextView *tvGameSize_;
	UI::CheckBox *enableReportsCheckbox_;
	UI::Choice *layoutEditorChoice_;
	UI::Choice *postProcChoice_;
	UI::PopupMultiChoice *resolutionChoice_;
	UI::CheckBox *frameSkipAuto_;
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
	UI::EventReturn OnReloadCheats(UI::EventParams &e);
	UI::EventReturn OnTiltTypeChange(UI::EventParams &e);
	UI::EventReturn OnTiltCustomize(UI::EventParams &e);

	// Global settings handlers
	UI::EventReturn OnLanguage(UI::EventParams &e);
	UI::EventReturn OnLanguageChange(UI::EventParams &e);
	UI::EventReturn OnAutoFrameskip(UI::EventParams &e);
	UI::EventReturn OnPostProcShader(UI::EventParams &e);
	UI::EventReturn OnPostProcShaderChange(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
	UI::EventReturn OnChangeNickname(UI::EventParams &e);
	UI::EventReturn OnChangeproAdhocServerAddress(UI::EventParams &e);
	UI::EventReturn OnChangeMacAddress(UI::EventParams &e);
	UI::EventReturn OnClearRecents(UI::EventParams &e);
	UI::EventReturn OnFullscreenChange(UI::EventParams &e);
	UI::EventReturn OnResolutionChange(UI::EventParams &e);
	UI::EventReturn OnHwScaleChange(UI::EventParams &e);
	UI::EventReturn OnShaderChange(UI::EventParams &e);
	UI::EventReturn OnRestoreDefaultSettings(UI::EventParams &e);
	UI::EventReturn OnRenderingMode(UI::EventParams &e);
	UI::EventReturn OnRenderingBackend(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
#ifdef _WIN32
	UI::EventReturn OnSavePathMydoc(UI::EventParams &e);
	UI::EventReturn OnSavePathOther(UI::EventParams &e);
#endif
	UI::EventReturn OnSoftwareRendering(UI::EventParams &e);
	UI::EventReturn OnHardwareTransform(UI::EventParams &e);

	UI::EventReturn OnScreenRotation(UI::EventParams &e);
	UI::EventReturn OnImmersiveModeChange(UI::EventParams &e);

	UI::EventReturn OnAdhocGuides(UI::EventParams &e);
	UI::EventReturn OnAudioBackend(UI::EventParams &e);

	// Temporaries to convert bools to int settings
	bool cap60FPS_;
	int iAlternateSpeedPercent_;
	bool enableReports_;

	// Cached booleans
	bool vtxCacheEnable_;
	bool postProcEnable_;
	bool resolutionEnable_;
	bool bloomHackEnable_;
	bool screenRotEnable_;
};

class DeveloperToolsScreen : public UIDialogScreenWithBackground {
public:
	DeveloperToolsScreen() {}
	virtual void onFinish(DialogResult result);

protected:
	virtual void CreateViews();

private:
	UI::EventReturn OnBack(UI::EventParams &e);
	UI::EventReturn OnRunCPUTests(UI::EventParams &e);
	UI::EventReturn OnSysInfo(UI::EventParams &e);
	UI::EventReturn OnLoggingChanged(UI::EventParams &e);
	UI::EventReturn OnLoadLanguageIni(UI::EventParams &e);
	UI::EventReturn OnSaveLanguageIni(UI::EventParams &e);
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
};

class ProAdhocServerScreen : public UIDialogScreenWithBackground {
public:
	ProAdhocServerScreen() {}	

protected:
	virtual void CreateViews();

private:	
	std::string tempProAdhocServer;
	UI::TextView *addrView_;
	UI::EventReturn OnBack(UI::EventParams &e);
	UI::EventReturn On0Click(UI::EventParams &e);
	UI::EventReturn On1Click(UI::EventParams &e);
	UI::EventReturn On2Click(UI::EventParams &e);
	UI::EventReturn On3Click(UI::EventParams &e);
	UI::EventReturn On4Click(UI::EventParams &e);
	UI::EventReturn On5Click(UI::EventParams &e);
	UI::EventReturn On6Click(UI::EventParams &e);
	UI::EventReturn On7Click(UI::EventParams &e);
	UI::EventReturn On8Click(UI::EventParams &e);
	UI::EventReturn On9Click(UI::EventParams &e);
	UI::EventReturn OnPointClick(UI::EventParams &e);
	UI::EventReturn OnDeleteClick(UI::EventParams &e);
	UI::EventReturn OnDeleteAllClick(UI::EventParams &e);
	UI::EventReturn OnOKClick(UI::EventParams &e);
	UI::EventReturn OnCancelClick(UI::EventParams &e);
};

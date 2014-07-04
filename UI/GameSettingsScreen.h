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
	GameSettingsScreen(std::string gamePath, std::string gameID = "")
		: UIDialogScreenWithGameBackground(gamePath), gameID_(gameID), enableReports_(false) {}

	virtual void update(InputState &input);
	virtual void onFinish(DialogResult result);

	UI::Event OnRecentChanged;

protected:
	virtual void CreateViews();
	virtual void sendMessage(const char *message, const char *value);
	void CallbackRestoreDefaults(bool yes);

private:
	std::string gameID_;

	// As we load metadata in the background, we need to be able to update these after the fact.
	UI::TextView *tvTitle_;
	UI::TextView *tvGameSize_;
	UI::CheckBox *enableReportsCheckbox_;
	UI::Choice *layoutEditorChoice_;
	UI::Choice *postProcChoice_;
	UI::PopupMultiChoice *resolutionChoice_;
	UI::CheckBox *frameSkipAuto_;

	// Event handlers
	UI::EventReturn OnControlMapping(UI::EventParams &e);
	UI::EventReturn OnTouchControlLayout(UI::EventParams &e);
	UI::EventReturn OnDumpNextFrameToLog(UI::EventParams &e);
	UI::EventReturn OnReloadCheats(UI::EventParams &e);
	UI::EventReturn OnTiltTypeChange(UI::EventParams &e);
	UI::EventReturn OnTiltCuztomize(UI::EventParams &e);

	// Global settings handlers
	UI::EventReturn OnLanguage(UI::EventParams &e);
	UI::EventReturn OnLanguageChange(UI::EventParams &e);
	UI::EventReturn OnPostProcShader(UI::EventParams &e);
	UI::EventReturn OnPostProcShaderChange(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
	UI::EventReturn OnChangeNickname(UI::EventParams &e);
	UI::EventReturn OnChangeproAdhocServerAddress(UI::EventParams &e);
	UI::EventReturn OnChangeMacAddress(UI::EventParams &e);
	UI::EventReturn OnClearRecents(UI::EventParams &e);
	UI::EventReturn OnFullscreenChange(UI::EventParams &e);
	UI::EventReturn OnResolutionChange(UI::EventParams &e);
	UI::EventReturn OnFrameSkipChange(UI::EventParams &e);
	UI::EventReturn OnShaderChange(UI::EventParams &e);
	UI::EventReturn OnRestoreDefaultSettings(UI::EventParams &e);
	UI::EventReturn OnRenderingMode(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
	UI::EventReturn OnSoftwareRendering(UI::EventParams &e);
	UI::EventReturn OnHardwareTransform(UI::EventParams &e);

	UI::EventReturn OnScreenRotation(UI::EventParams &e);
	UI::EventReturn OnImmersiveModeChange(UI::EventParams &e);

	// Temporaries to convert bools to int settings
	bool cap60FPS_;
	int iAlternateSpeedPercent_;
	bool enableReports_;

	// Cached booleans
	bool hwTransformEnable;
	bool vtxCacheEnable;
	bool renderModeEnable;
	bool blockTransferEnable;
	bool swSkinningEnable;
	bool texBackoffEnable;
	bool mipmapEnable;
	bool texScalingEnable;
	bool texSecondaryEnable;
	bool beziersEnable;
	bool stencilTestEnable;
	bool depthWriteEnable;
	bool prescaleEnable;
	bool texScalingTypeEnable;
	bool desposterizeEnable;
	bool anisotropicEnable;
	bool texFilteringEnable;
	bool postProcEnable;
	bool resolutionEnable;
	bool alphaHackEnable;
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

class MacAddressScreen : public UIDialogScreenWithBackground {
public:
	MacAddressScreen() {}

protected:
	virtual void CreateViews();

private:
	std::string tempMacAddress;
	UI::TextView *addrView_;
	UI::EventReturn OnBack(UI::EventParams &e);
	UI::EventReturn OnAClick(UI::EventParams &e);
	UI::EventReturn OnBClick(UI::EventParams &e);
	UI::EventReturn OnCClick(UI::EventParams &e);
	UI::EventReturn OnDClick(UI::EventParams &e);
	UI::EventReturn OnEClick(UI::EventParams &e);
	UI::EventReturn OnFClick(UI::EventParams &e);
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
	UI::EventReturn OnDeleteClick(UI::EventParams &e);
	UI::EventReturn OnDeleteAllClick(UI::EventParams &e);
	UI::EventReturn OnOKClick(UI::EventParams &e);
	UI::EventReturn OnCancelClick(UI::EventParams &e);
};

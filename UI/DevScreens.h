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

#include <memory>
#include <string>
#include <vector>

#include "Common/Data/Text/I18n.h"
#include "Common/Net/HTTPClient.h"
#include "Common/UI/UIScreen.h"

#include "UI/MiscScreens.h"
#include "GPU/Common/ShaderCommon.h"

class DevMenuScreen : public PopupScreen {
public:
	DevMenuScreen(const Path &gamePath, std::shared_ptr<I18NCategory> i18n) : PopupScreen(i18n->T("Dev Tools")), gamePath_(gamePath) {}

	const char *tag() const override { return "DevMenu"; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

protected:
	UI::EventReturn OnLogView(UI::EventParams &e);
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnJitCompare(UI::EventParams &e);
	UI::EventReturn OnShaderView(UI::EventParams &e);
	UI::EventReturn OnFreezeFrame(UI::EventParams &e);
	UI::EventReturn OnDumpFrame(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
	UI::EventReturn OnToggleAudioDebug(UI::EventParams &e);
	UI::EventReturn OnResetLimitedLogging(UI::EventParams &e);

private:
	Path gamePath_;
};

class JitDebugScreen : public UIDialogScreenWithBackground {
public:
	JitDebugScreen() {}
	void CreateViews() override;

	const char *tag() const override { return "JitDebug"; }

private:
	UI::EventReturn OnEnableAll(UI::EventParams &e);
	UI::EventReturn OnDisableAll(UI::EventParams &e);
};

class LogConfigScreen : public UIDialogScreenWithBackground {
public:
	LogConfigScreen() {}
	void CreateViews() override;

	const char *tag() const override { return "LogConfig"; }

private:
	UI::EventReturn OnToggleAll(UI::EventParams &e);
	UI::EventReturn OnEnableAll(UI::EventParams &e);
	UI::EventReturn OnDisableAll(UI::EventParams &e);
	UI::EventReturn OnLogLevel(UI::EventParams &e);
	UI::EventReturn OnLogLevelChange(UI::EventParams &e);
};

class LogScreen : public UIDialogScreenWithBackground {
public:
	LogScreen() : toBottom_(false) {}
	void CreateViews() override;
	void update() override;

	const char *tag() const override { return "Log"; }

private:
	void UpdateLog();
	UI::EventReturn OnSubmit(UI::EventParams &e);
	UI::TextEdit *cmdLine_;
	UI::LinearLayout *vert_;
	UI::ScrollView *scroll_;
	bool toBottom_;
};

class LogLevelScreen : public ListPopupScreen {
public:
	LogLevelScreen(const std::string &title);

	const char *tag() const override { return "LogLevel"; }

private:
	void OnCompleted(DialogResult result) override;
};

class SystemInfoScreen : public UIDialogScreenWithBackground {
public:
	const char *tag() const override { return "SystemInfo"; }

	void CreateViews() override;
};

class AddressPromptScreen : public PopupScreen {
public:
	AddressPromptScreen(const std::string &title) : PopupScreen(title, "OK", "Cancel"), addrView_(NULL), addr_(0) {
		memset(buttons_, 0, sizeof(buttons_));
	}

	const char *tag() const override { return "AddressPrompt"; }

	bool key(const KeyInput &key) override;

	UI::Event OnChoice;

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void OnCompleted(DialogResult result) override;
	UI::EventReturn OnDigitButton(UI::EventParams &e);
	UI::EventReturn OnBackspace(UI::EventParams &e);

private:
	void AddDigit(int n);
	void BackspaceDigit();
	void UpdatePreviewDigits();

	UI::TextView *addrView_;
	UI::Button *buttons_[16];
	unsigned int addr_;
};

class JitCompareScreen : public UIDialogScreenWithBackground {
public:
	void CreateViews() override;

	const char *tag() const override { return "JitCompare"; }

private:
	void UpdateDisasm();
	UI::EventReturn OnRandomBlock(UI::EventParams &e);
	UI::EventReturn OnRandomFPUBlock(UI::EventParams &e);
	UI::EventReturn OnRandomVFPUBlock(UI::EventParams &e);
	void OnRandomBlock(int flag);

	UI::EventReturn OnCurrentBlock(UI::EventParams &e);
	UI::EventReturn OnSelectBlock(UI::EventParams &e);
	UI::EventReturn OnPrevBlock(UI::EventParams &e);
	UI::EventReturn OnNextBlock(UI::EventParams &e);
	UI::EventReturn OnBlockAddress(UI::EventParams &e);
	UI::EventReturn OnAddressChange(UI::EventParams &e);
	UI::EventReturn OnShowStats(UI::EventParams &e);

	int currentBlock_ = -1;

	UI::TextView *blockName_;
	UI::TextEdit *blockAddr_;
	UI::TextView *blockStats_;

	UI::LinearLayout *leftDisasm_;
	UI::LinearLayout *rightDisasm_;
};

class ShaderListScreen : public UIDialogScreenWithBackground {
public:
	void CreateViews() override;

	const char *tag() const override { return "ShaderList"; }

private:
	int ListShaders(DebugShaderType shaderType, UI::LinearLayout *view);

	UI::EventReturn OnShaderClick(UI::EventParams &e);

	UI::TabHolder *tabs_;
};

class ShaderViewScreen : public UIDialogScreenWithBackground {
public:
	ShaderViewScreen(std::string id, DebugShaderType type)
		: id_(id), type_(type) {}

	void CreateViews() override;

	const char *tag() const override { return "ShaderView"; }

private:
	std::string id_;
	DebugShaderType type_;
};

class FrameDumpTestScreen : public UIDialogScreenWithBackground {
public:
	FrameDumpTestScreen();
	~FrameDumpTestScreen();

	void CreateViews() override;
	void update() override;

	const char *tag() const override { return "FrameDumpTest"; }

private:
	UI::EventReturn OnLoadDump(UI::EventParams &e);

	std::vector<std::string> files_;
	std::shared_ptr<http::Download> listing_;
	std::shared_ptr<http::Download> dumpDownload_;
};

void DrawProfile(UIContext &ui);
const char *GetCompilerABI();

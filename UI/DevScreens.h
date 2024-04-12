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
#include "UI/TabbedDialogScreen.h"
#include "UI/MiscScreens.h"
#include "GPU/Common/ShaderCommon.h"

class DevMenuScreen : public PopupScreen {
public:
	DevMenuScreen(const Path &gamePath, I18NCat cat) : PopupScreen(T(cat, "Dev Tools")), gamePath_(gamePath) {}

	const char *tag() const override { return "DevMenu"; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

protected:
	UI::EventReturn OnLogView(UI::EventParams &e);
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnJitCompare(UI::EventParams &e);
	UI::EventReturn OnShaderView(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
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
	void CreateViews() override;
	void update() override;

	const char *tag() const override { return "Log"; }

private:
	void UpdateLog();
	UI::EventReturn OnSubmit(UI::EventParams &e);

	UI::TextEdit *cmdLine_ = nullptr;
	UI::LinearLayout *vert_ = nullptr;
	UI::ScrollView *scroll_ = nullptr;
	bool toBottom_ = false;
};

class LogLevelScreen : public UI::ListPopupScreen {
public:
	LogLevelScreen(std::string_view title);

	const char *tag() const override { return "LogLevel"; }

private:
	void OnCompleted(DialogResult result) override;
};

class SystemInfoScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	SystemInfoScreen(const Path &filename) : TabbedUIDialogScreenWithGameBackground(filename) {}

	const char *tag() const override { return "SystemInfo"; }

	void CreateTabs() override;
	void update() override;

protected:
	bool ShowSearchControls() const override { return false; }
	void CreateInternalsTab(UI::ViewGroup *internals);
};

class AddressPromptScreen : public PopupScreen {
public:
	AddressPromptScreen(std::string_view title) : PopupScreen(title, "OK", "Cancel"), addrView_(NULL), addr_(0) {
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
	std::shared_ptr<http::Request> listing_;
	std::shared_ptr<http::Request> dumpDownload_;
};

class TouchTestScreen : public UIDialogScreenWithGameBackground {
public:
	TouchTestScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			touches_[i].id = -1;
		}
	}

	void touch(const TouchInput &touch) override;
	void DrawForeground(UIContext &dc) override;

	bool key(const KeyInput &key) override;
	void axis(const AxisInput &axis) override;

	const char *tag() const override { return "TouchTest"; }

protected:
	struct TrackedTouch {
		int id;
		float x;
		float y;
	};
	enum {
		MAX_TOUCH_POINTS = 10,
	};
	TrackedTouch touches_[MAX_TOUCH_POINTS]{};

	std::vector<std::string> keyEventLog_;

	UI::TextView *lastKeyEvents_ = nullptr;

	double lastFrameTime_ = 0.0;

	void CreateViews() override;
	void UpdateLogView();

	UI::EventReturn OnImmersiveModeChange(UI::EventParams &e);
	UI::EventReturn OnRenderingBackend(UI::EventParams &e);
	UI::EventReturn OnRecreateActivity(UI::EventParams &e);
};

void DrawProfile(UIContext &ui);
const char *GetCompilerABI();

void AddOverlayList(UI::ViewGroup *items, ScreenManager *screenManager);

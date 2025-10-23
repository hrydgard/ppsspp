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
	void OnJitCompare(UI::EventParams &e);
	void OnShaderView(UI::EventParams &e);
	void OnDeveloperTools(UI::EventParams &e);
	void OnResetLimitedLogging(UI::EventParams &e);

private:
	Path gamePath_;
};

class JitDebugScreen : public UIDialogScreenWithBackground {
public:
	JitDebugScreen() {}
	void CreateViews() override;

	const char *tag() const override { return "JitDebug"; }

private:
	void OnEnableAll(UI::EventParams &e);
	void OnDisableAll(UI::EventParams &e);
};

class LogConfigScreen : public UIDialogScreenWithBackground {
public:
	LogConfigScreen() {}
	void CreateViews() override;

	const char *tag() const override { return "LogConfig"; }

private:
	void OnToggleAll(UI::EventParams &e);
	void OnEnableAll(UI::EventParams &e);
	void OnDisableAll(UI::EventParams &e);
	void OnLogLevel(UI::EventParams &e);
	void OnLogLevelChange(UI::EventParams &e);
};

class LogViewScreen : public UIDialogScreenWithBackground {
public:
	void CreateViews() override;
	void update() override;

	const char *tag() const override { return "Log"; }

private:
	void UpdateLog();

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

class GPIGPOScreen : public PopupScreen {
public:
	GPIGPOScreen(std::string_view title) : PopupScreen(title, "OK") {}
	const char *tag() const override { return "GPIGPO"; }

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
};

class ShaderListScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	ShaderListScreen() : TabbedUIDialogScreenWithGameBackground(Path()) {}
	void CreateTabs() override;

	const char *tag() const override { return "ShaderList"; }

private:
	bool ForceHorizontalTabs() const override {return true; }
	int ListShaders(DebugShaderType shaderType, UI::LinearLayout *view);

	void OnShaderClick(UI::EventParams &e);
};

class ShaderViewScreen : public UIDialogScreenWithBackground {
public:
	ShaderViewScreen(std::string id, DebugShaderType type)
		: id_(id), type_(type) {}

	void CreateViews() override;
	bool key(const KeyInput &ki) override;

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
	void OnLoadDump(UI::EventParams &e);

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

	void OnImmersiveModeChange(UI::EventParams &e);
	void OnRenderingBackend(UI::EventParams &e);
	void OnRecreateActivity(UI::EventParams &e);
};

void DrawProfile(UIContext &ui);

void AddOverlayList(UI::ViewGroup *items, ScreenManager *screenManager);

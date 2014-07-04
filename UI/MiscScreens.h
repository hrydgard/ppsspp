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

#include <vector>
#include <map>
#include <string>

#include "base/functional.h"
#include "file/file_util.h"
#include "ui/ui_screen.h"
#include "GPU/Common/PostShader.h"

extern std::string boot_filename;

inline void NoOpVoidBool(bool) {}

class UIScreenWithBackground : public UIScreen {
public:
	UIScreenWithBackground() : UIScreen() {}
protected:
	virtual void DrawBackground(UIContext &dc);
	virtual void sendMessage(const char *message, const char *value) override;
	virtual UI::EventReturn OnLanguageChange(UI::EventParams &e);
};

class UIScreenWithGameBackground : public UIScreenWithBackground {
public:
	UIScreenWithGameBackground(const std::string &gamePath)
		: UIScreenWithBackground(), gamePath_(gamePath) {}
	virtual void DrawBackground(UIContext &dc);
protected:
	std::string gamePath_;
};

class UIDialogScreenWithBackground : public UIDialogScreen {
public:
	UIDialogScreenWithBackground() : UIDialogScreen() {}
protected:
	virtual void DrawBackground(UIContext &dc);
	virtual void sendMessage(const char *message, const char *value) override;
	virtual UI::EventReturn OnLanguageChange(UI::EventParams &e);
};

class UIDialogScreenWithGameBackground : public UIDialogScreenWithBackground {
public:
	UIDialogScreenWithGameBackground(const std::string &gamePath)
		: UIDialogScreenWithBackground(), gamePath_(gamePath) {}
	virtual void DrawBackground(UIContext &dc);
protected:
	std::string gamePath_;
};

class PromptScreen : public UIDialogScreenWithBackground {
public:
	PromptScreen(std::string message, std::string yesButtonText, std::string noButtonText, 
		std::function<void(bool)> callback = &NoOpVoidBool);

	virtual void CreateViews() override;

private:
	UI::EventReturn OnYes(UI::EventParams &e);
	UI::EventReturn OnNo(UI::EventParams &e);

	std::string message_;
	std::string yesButtonText_;
	std::string noButtonText_;
	std::function<void(bool)> callback_;
};

class NewLanguageScreen : public ListPopupScreen {
public:
	NewLanguageScreen(const std::string &title);

private:
	virtual void OnCompleted(DialogResult result);
	virtual bool ShowButtons() const override { return true; }
	std::map<std::string, std::pair<std::string, int>> langValuesMapping;
	std::map<std::string, std::string> titleCodeMapping;
	std::vector<FileInfo> langs_;
};

class PostProcScreen : public ListPopupScreen {
public:
	PostProcScreen(const std::string &title);

private:
	virtual void OnCompleted(DialogResult result);
	virtual bool ShowButtons() const override { return true; }
	std::vector<ShaderInfo> shaders_;
};

class LogoScreen : public UIScreen {
public:
	LogoScreen()
		: frames_(0), switched_(false) {}
	bool key(const KeyInput &key) override;
	virtual void update(InputState &input) override;
	virtual void render() override;
	virtual void sendMessage(const char *message, const char *value) override;
	virtual void CreateViews() {}

private:
	void Next();
	int frames_;
	bool switched_;
};

class CreditsScreen : public UIDialogScreenWithBackground {
public:
	CreditsScreen() : frames_(0) {}
	virtual void update(InputState &input) override;
	virtual void render() override;

	virtual void CreateViews();

private:
	UI::EventReturn OnOK(UI::EventParams &e);

	UI::EventReturn OnSupport(UI::EventParams &e);
	UI::EventReturn OnPPSSPPOrg(UI::EventParams &e);
	UI::EventReturn OnForums(UI::EventParams &e);
	UI::EventReturn OnChineseForum(UI::EventParams &e);
	UI::EventReturn OnShare(UI::EventParams &e);
	UI::EventReturn OnTwitter(UI::EventParams &e);

	int frames_;
};


// Utility functions that create various popup screens
ListPopupScreen *CreateLanguageScreen();

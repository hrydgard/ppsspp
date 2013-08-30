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

inline void NoOpVoidBool(bool) {}

class UIScreenWithBackground : public UIScreen {
public:
	UIScreenWithBackground() : UIScreen() {}
protected:
	virtual void DrawBackground(UIContext &dc);
};

class UIDialogScreenWithBackground : public UIDialogScreen {
public:
	UIDialogScreenWithBackground() : UIDialogScreen() {}
protected:
	virtual void DrawBackground(UIContext &dc);
};

class PromptScreen : public UIDialogScreenWithBackground {
public:
	PromptScreen(std::string message, std::string yesButtonText, std::string noButtonText)
		: message_(message), yesButtonText_(yesButtonText), noButtonText_(noButtonText) {
		callback_ = &NoOpVoidBool;
	}
	PromptScreen(std::string message, std::string yesButtonText, std::string noButtonText, std::function<void(bool)> callback)
		: message_(message), yesButtonText_(yesButtonText), noButtonText_(noButtonText), callback_(callback) {}

	virtual void CreateViews();

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
	virtual bool ShowButtons() const { return true; }
	std::map<std::string, std::pair<std::string, int>> langValuesMapping;
	std::map<std::string, std::string> titleCodeMapping;
	std::vector<FileInfo> langs_;
};

class LogoScreen : public UIScreen {
public:
	LogoScreen(const std::string &bootFilename)
		: bootFilename_(bootFilename), frames_(0) {}
	void key(const KeyInput &key);
	void update(InputState &input);
	void render();
	void sendMessage(const char *message, const char *value);
	virtual void CreateViews() {}

private:
	void Next();
	std::string bootFilename_;
	int frames_;
};

class CreditsScreen : public UIDialogScreenWithBackground {
public:
	CreditsScreen() : frames_(0) {}
	void update(InputState &input);
	void render();
	virtual void CreateViews();

private:
	UI::EventReturn OnOK(UI::EventParams &e);

	UI::EventReturn OnSupport(UI::EventParams &e);
	UI::EventReturn OnPPSSPPOrg(UI::EventParams &e);
	UI::EventReturn OnForums(UI::EventParams &e);
	UI::EventReturn OnChineseForum(UI::EventParams &e);

	int frames_;
};

class SystemInfoScreen : public UIDialogScreenWithBackground {
public:
	SystemInfoScreen() {}
	virtual void CreateViews();
};

// Utility functions that create various popup screens
ListPopupScreen *CreateLanguageScreen();
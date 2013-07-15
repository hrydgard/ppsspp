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

class PromptScreen : public UIScreen {
public:
	PromptScreen(std::string message, std::string yesButtonText, std::string noButtonText)
		: message_(message), yesButtonText_(yesButtonText), noButtonText_(noButtonText) {
		callback_ = &NoOpVoidBool;
	}
	PromptScreen(std::string message, std::string yesButtonText, std::string noButtonText, std::function<void(bool)> callback)
		: message_(message), yesButtonText_(yesButtonText), noButtonText_(noButtonText), callback_(callback) {}

	virtual void CreateViews();
protected:
	virtual void DrawBackground(UIContext &dc);

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
	NewLanguageScreen();

private:
	virtual void OnCompleted();

	std::map<std::string, std::pair<std::string, int>> langValuesMapping;
	std::map<std::string, std::string> titleCodeMapping;
	std::vector<FileInfo> langs_;
};


// Utility functions that create various popup screens
ListPopupScreen *CreateLanguageScreen();
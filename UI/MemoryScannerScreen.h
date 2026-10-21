// Copyright (c) 2026- PPSSPP Project.

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

#include <deque>
#include <string>
#include <vector>

#include "Common/Input/InputState.h"
#include "Common/UI/PopupScreens.h"
#include "Core/MemoryScanner.h"

namespace UI {
class ChoiceStrip;
class Choice;
class TextView;
class PopupMultiChoice;
}

// An on-screen number pad that edits one of up to two value fields. Used by both screens,
// so values can be typed without a soft keyboard (which covers half the screen on phones).
class ScanValueEditor {
public:
	// Adds the value field(s) and the keypad.
	void CreateViews(UI::ViewGroup *parent, ScreenManager *screenManager, RequesterToken token, bool twoFields);
	void Key(const char *key);
	bool HandleKey(const KeyInput &input);
	void UpdateViews();
	// Forget the views and fields, when the value fields aren't shown.
	void Detach();

	std::string *fields[2]{};
	const char *fieldLabels[2]{};
	int activeField = 0;

private:
	UI::Choice *fieldViews_[2]{};
	int fieldCount_ = 1;
	ScreenManager *screenManager_ = nullptr;
	RequesterToken token_{};
};

class MemoryScannerScreen : public UI::PopupScreen {
public:
	MemoryScannerScreen();

	const char *tag() const override { return "MemoryScanner"; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;
	bool key(const KeyInput &input) override;
	bool WantsTextInput() const override;

protected:
	bool FillVertical() const override { return true; }
	UI::Size PopupWidth() const override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;

private:
	void CreateControls(UI::ViewGroup *parent);
	void CreateResults(UI::ViewGroup *parent);
	void DoScan(bool first);
	void RefreshResultValues();
	// A result line: freeze checkbox and the address/value, which opens the edit popup.
	void AddResultRow(UI::ViewGroup *parent, u32 addr, ScanValueType type, bool showPrevious);
	std::string ResultText(u32 addr, ScanValueType type, bool showPrevious) const;

	ScanValueEditor editor_;
	UI::TextView *status_ = nullptr;
	struct ResultView {
		u32 addr;
		ScanValueType type;
		bool showPrevious;
		UI::Choice *view;
	};
	std::vector<ResultView> resultViews_;
	// Storage for the freeze checkboxes. A deque, so the pointers stay valid as it grows.
	std::deque<bool> freezeFlags_;
	double lastRefresh_ = 0.0;
	std::string statusText_;
	bool statusIsError_ = false;
};

class MemoryScannerEditScreen : public UI::PopupScreen {
public:
	MemoryScannerEditScreen(u32 addr, ScanValueType type);

	const char *tag() const override { return "MemoryScannerEdit"; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;
	bool key(const KeyInput &input) override;
	bool WantsTextInput() const override;

protected:
	UI::Size PopupWidth() const override { return 600; }

private:
	void UpdateTexts();
	bool GetValueToUse(u32 *bits);
	void OnSet();
	void OnAdd();

	u32 addr_;
	ScanValueType type_;
	bool frozen_ = false;
	std::string value_;
	std::string name_;
	ScanValueEditor editor_;

	UI::TextView *currentText_ = nullptr;
	UI::TextView *cheatText_ = nullptr;
	UI::TextView *status_ = nullptr;
	double lastRefresh_ = 0.0;
};

// Appends a cheat with these lines to the running game's cheat file (PSP/Cheats/<DISC_ID>.ini),
// adding the _S/_G header if the file doesn't have one for this game. The cheat is added disabled.
bool AppendCheatToGameIni(const std::string &name, const std::vector<std::string> &lines, std::string *error);

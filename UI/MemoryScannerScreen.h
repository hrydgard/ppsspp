// Copyright (c) 2012- PPSSPP Project.

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
class ViewGroup;
class Choice;
class TextView;
}

// An on-screen number pad that edits one of up to two value fields. Used by all the scanner
// screens, so values can be typed without a soft keyboard (which covers half the screen on phones).
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
	bool ActiveFieldIsHex() const;

	UI::Choice *fieldViews_[2]{};
	// The A-F keys, shown while the active field is in hex.
	UI::ViewGroup *hexKeys_ = nullptr;
	UI::Choice *hexToggle_ = nullptr;
	UI::Choice *dotKey_ = nullptr;
	int fieldCount_ = 1;
	ScreenManager *screenManager_ = nullptr;
	RequesterToken token_{};
};

// Rows of watched values, each with a Freeze checkbox, showing the live value. Tapping a row
// opens the edit popup. Used for search results, frozen values and pointer chains.
class WatchList {
public:
	enum class Style {
		SearchResult,  // Shows the value at the last scan, when it changed.
		Frozen,        // Shows the value type, it may not be the current search's.
		Pointer,       // Shows the chain and where it leads now.
	};

	void Clear();
	// owner gets RecreateViews() after a freeze changes, so lists of frozen values update.
	void AddRow(UI::ViewGroup *parent, Screen *owner, const PointerChain &where, ScanValueType type, Style style);
	void Refresh();
	bool Empty() const { return rows_.empty(); }

	static std::string RowText(const PointerChain &where, ScanValueType type, Style style);

private:
	struct Row {
		PointerChain where;
		ScanValueType type;
		Style style;
		UI::Choice *view;
	};
	std::vector<Row> rows_;
	// Storage for the freeze checkboxes. A deque, so the pointers stay valid as it grows.
	std::deque<bool> flags_;
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
	// What the current search covers, or a new one would.
	std::string AreaText() const;

	ScanValueEditor editor_;
	WatchList watches_;
	UI::TextView *status_ = nullptr;
	double lastRefresh_ = 0.0;
	std::string statusText_;
	bool statusIsError_ = false;
};

// Pausing, and where scans look.
class ScanOptionsScreen : public UI::PopupScreen {
public:
	ScanOptionsScreen();

	const char *tag() const override { return "MemoryScannerOptions"; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
};

// Changes the value at an address or at the end of a pointer chain, freezes it, or saves it as a
// cheat. For a plain address it also starts a pointer scan.
class MemoryScannerEditScreen : public UI::PopupScreen {
public:
	MemoryScannerEditScreen(const PointerChain &where, ScanValueType type);

	const char *tag() const override { return "MemoryScannerEdit"; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;
	bool key(const KeyInput &input) override;
	bool WantsTextInput() const override;

protected:
	UI::Size PopupWidth() const override { return 600; }
	void dialogFinished(const Screen *dialog, DialogResult result) override;

private:
	void UpdateTexts();
	bool GetValueToUse(u32 *bits);
	bool Resolve(u32 *addr) const { return where_.Resolve(addr); }
	void OnSet();
	void OnAdd();

	PointerChain where_;
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

// Finds pointer chains to an address, and narrows them down with the value's address after it
// moved. Its chains can be frozen, edited and added as pointer cheats like any address.
class PointerScanScreen : public UI::PopupScreen {
public:
	// target is the address scanned for, or filtered with.
	PointerScanScreen(u32 target, ScanValueType type);

	const char *tag() const override { return "PointerScan"; }

	void CreatePopupContents(UI::ViewGroup *parent) override;
	void update() override;

protected:
	bool FillVertical() const override { return true; }
	UI::Size PopupWidth() const override { return 820; }
	void dialogFinished(const Screen *dialog, DialogResult result) override;

private:
	void CreateControls(UI::ViewGroup *parent);
	void CreateResults(UI::ViewGroup *parent);
	void DoScan();
	void DoFilter();

	u32 target_;
	ScanValueType type_;
	WatchList watches_;
	double lastRefresh_ = 0.0;
	std::string statusText_;
	bool statusIsError_ = false;
};

// Appends a cheat with these lines to the running game's cheat file (PSP/Cheats/<DISC_ID>.ini),
// adding the _S/_G header if the file doesn't have one for this game. The cheat is added disabled.
bool AppendCheatToGameIni(const std::string &name, const std::vector<std::string> &lines, std::string *error);

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

#include "UI/MiscScreens.h"

class DevMenu : public PopupScreen {
public:
	DevMenu() : PopupScreen("Dev Tools") {}

	virtual void CreatePopupContents(UI::ViewGroup *parent);

	virtual void dialogFinished(const Screen *dialog, DialogResult result);

protected:
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnJitCompare(UI::EventParams &e);
	UI::EventReturn OnFreezeFrame(UI::EventParams &e);
	UI::EventReturn OnDumpFrame(UI::EventParams &e);
	UI::EventReturn OnDeveloperTools(UI::EventParams &e);
};

class LogConfigScreen : public UIDialogScreenWithBackground {
public:
	LogConfigScreen() {}
	virtual void CreateViews();

private:
	UI::EventReturn OnToggleAll(UI::EventParams &e);
	UI::EventReturn OnLogLevel(UI::EventParams &e);
	UI::EventReturn OnLogLevelChange(UI::EventParams &e);
};

class LogLevelScreen : public ListPopupScreen {
public:
	LogLevelScreen(const std::string &title);

private:
	virtual void OnCompleted(DialogResult result);

};

class SystemInfoScreen : public UIDialogScreenWithBackground {
public:
	SystemInfoScreen() {}
	virtual void CreateViews();
};

class AddressPromptScreen : public PopupScreen {
public:
	AddressPromptScreen(const std::string &title) : PopupScreen(title, "OK", "Cancel"), addrView_(NULL), addr_(0) {
		memset(buttons_, 0, sizeof(buttons_));
	}

	virtual bool key(const KeyInput &key) override;

	UI::Event OnChoice;

protected:
	virtual void CreatePopupContents(UI::ViewGroup *parent) override;
	virtual void OnCompleted(DialogResult result);
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
	JitCompareScreen() : currentBlock_(-1) {}
	virtual void CreateViews();

private:
	void UpdateDisasm();
	UI::EventReturn OnRandomBlock(UI::EventParams &e);
	UI::EventReturn OnRandomVFPUBlock(UI::EventParams &e);
	UI::EventReturn OnCurrentBlock(UI::EventParams &e);
	UI::EventReturn OnSelectBlock(UI::EventParams &e);
	UI::EventReturn OnBlockAddress(UI::EventParams &e);

	int currentBlock_;

	UI::TextView *blockName_;	

	UI::LinearLayout *leftDisasm_;	
	UI::LinearLayout *rightDisasm_;	
};

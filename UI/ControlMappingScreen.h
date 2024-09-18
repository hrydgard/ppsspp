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

#include <functional>
#include <memory>
#include <set>
#include <mutex>
#include <vector>
#include <string>

#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/Data/Text/I18n.h"

#include "Core/ControlMapper.h"

#include "UI/MiscScreens.h"

class SingleControlMapper;

class ControlMappingScreen : public UIDialogScreenWithGameBackground {
public:
	explicit ControlMappingScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {
		categoryToggles_[0] = true;
		categoryToggles_[1] = true;
		categoryToggles_[2] = true;
		categoryToggles_[3] = false;
	}
	const char *tag() const override { return "ControlMapping"; }

protected:
	void CreateViews() override;
	void update() override;

private:
	UI::EventReturn OnAutoConfigure(UI::EventParams &params);

	void dialogFinished(const Screen *dialog, DialogResult result) override;

	UI::ScrollView *rightScroll_ = nullptr;
	std::vector<SingleControlMapper *> mappers_;
	int keyMapGeneration_ = -1;

	bool categoryToggles_[10]{};
};

class KeyMappingNewKeyDialog : public PopupScreen {
public:
	explicit KeyMappingNewKeyDialog(int btn, bool replace, std::function<void(KeyMap::MultiInputMapping)> callback, I18NCat i18n)
		: PopupScreen(T(i18n, "Map Key"), "Cancel", ""), pspBtn_(btn), callback_(callback) {}

	const char *tag() const override { return "KeyMappingNewKey"; }

	bool key(const KeyInput &key) override;
	void axis(const AxisInput &axis) override;

	void SetDelay(float t);

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;

	bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return true; }
	void OnCompleted(DialogResult result) override {}

private:
	int pspBtn_;
	std::function<void(KeyMap::MultiInputMapping)> callback_;

	KeyMap::MultiInputMapping mapping_;

	UI::View *comboMappingsNotEnabled_ = nullptr;

	// We need to do our own detection for axis "keyup" here.
	std::set<InputMapping> triggeredAxes_;

	double delayUntil_ = 0.0f;
};

class KeyMappingNewMouseKeyDialog : public PopupScreen {
public:
	KeyMappingNewMouseKeyDialog(int btn, bool replace, std::function<void(KeyMap::MultiInputMapping)> callback, I18NCat i18n)
		: PopupScreen(T(i18n, "Map Mouse"), "", ""), pspBtn_(btn), callback_(callback) {}

	const char *tag() const override { return "KeyMappingNewMouseKey"; }

	bool key(const KeyInput &key) override;
	void axis(const AxisInput &axis) override;

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;

	bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return true; }
	void OnCompleted(DialogResult result) override {}

private:
	int pspBtn_;
	std::function<void(KeyMap::MultiInputMapping)> callback_;
	bool mapped_ = false;  // Prevent double registrations
};

class JoystickHistoryView;

class AnalogSetupScreen : public UIDialogScreenWithGameBackground {
public:
	AnalogSetupScreen(const Path &gamePath);

	bool key(const KeyInput &key) override;
	void axis(const AxisInput &axis) override;

	void update() override;

	const char *tag() const override { return "AnalogSetup"; }

protected:
	void CreateViews() override;

private:
	UI::EventReturn OnResetToDefaults(UI::EventParams &e);

	ControlMapper mapper_;

	float analogX_[2]{};
	float analogY_[2]{};
	float rawX_[2]{};
	float rawY_[2]{};

	JoystickHistoryView *stickView_[2]{};
};

class MockPSP;

class VisualMappingScreen : public UIDialogScreenWithGameBackground {
public:
	VisualMappingScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}

	const char *tag() const override { return "VisualMapping"; }

	bool key(const KeyInput &key) override;
	void axis(const AxisInput &axis) override;

protected:
	void CreateViews() override;

	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void resized() override;

private:
	UI::EventReturn OnMapButton(UI::EventParams &e);
	UI::EventReturn OnBindAll(UI::EventParams &e);
	void HandleKeyMapping(const KeyMap::MultiInputMapping &key);
	void MapNext(bool successive);

	MockPSP *psp_ = nullptr;
	int nextKey_ = 0;
	int bindAll_ = -1;
	bool replace_ = false;
};

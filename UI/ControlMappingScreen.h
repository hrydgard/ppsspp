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
#include <mutex>
#include <vector>

#include "Common/UI/View.h"
#include "Common/UI/UIScreen.h"
#include "Common/Data/Text/I18n.h"

#include "Core/ControlMapper.h"

#include "UI/MiscScreens.h"

class SingleControlMapper;

class ControlMappingScreen : public UIDialogScreenWithGameBackground {
public:
	ControlMappingScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}
	const char *tag() const override { return "ControlMapping"; }

protected:
	void CreateViews() override;
	void update() override;

private:
	UI::EventReturn OnDefaultMapping(UI::EventParams &params);
	UI::EventReturn OnClearMapping(UI::EventParams &params);
	UI::EventReturn OnAutoConfigure(UI::EventParams &params);
	UI::EventReturn OnVisualizeMapping(UI::EventParams &params);

	void dialogFinished(const Screen *dialog, DialogResult result) override;

	UI::ScrollView *rightScroll_;
	std::vector<SingleControlMapper *> mappers_;
	int keyMapGeneration_ = -1;
};

class KeyMappingNewKeyDialog : public PopupScreen {
public:
	explicit KeyMappingNewKeyDialog(int btn, bool replace, std::function<void(KeyDef)> callback, std::shared_ptr<I18NCategory> i18n)
		: PopupScreen(i18n->T("Map Key"), "Cancel", ""), callback_(callback) {
		pspBtn_ = btn;
	}

	const char *tag() const override { return "KeyMappingNewKey"; }

	bool key(const KeyInput &key) override;
	bool axis(const AxisInput &axis) override;

	void SetDelay(float t);

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;

	bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return true; }
	void OnCompleted(DialogResult result) override {}

private:
	int pspBtn_;
	std::function<void(KeyDef)> callback_;
	bool mapped_ = false;  // Prevent double registrations
	double delayUntil_ = 0.0f;
};

class KeyMappingNewMouseKeyDialog : public PopupScreen {
public:
	KeyMappingNewMouseKeyDialog(int btn, bool replace, std::function<void(KeyDef)> callback, std::shared_ptr<I18NCategory> i18n)
		: PopupScreen(i18n->T("Map Mouse"), "", ""), callback_(callback), mapped_(false) {
		pspBtn_ = btn;
	}

	const char *tag() const override { return "KeyMappingNewMouseKey"; }

	bool key(const KeyInput &key) override;
	bool axis(const AxisInput &axis) override;

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;

	bool FillVertical() const override { return false; }
	bool ShowButtons() const override { return true; }
	void OnCompleted(DialogResult result) override {}

private:
	int pspBtn_;
	std::function<void(KeyDef)> callback_;
	bool mapped_;  // Prevent double registrations
};

class JoystickHistoryView;

class AnalogSetupScreen : public UIDialogScreenWithGameBackground {
public:
	AnalogSetupScreen(const Path &gamePath);

	bool key(const KeyInput &key) override;
	bool axis(const AxisInput &axis) override;

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

class TouchTestScreen : public UIDialogScreenWithGameBackground {
public:
	TouchTestScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {
		for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
			touches_[i].id = -1;
		}
	}

	bool touch(const TouchInput &touch) override;
	void render() override;

	bool key(const KeyInput &key) override;
	bool axis(const AxisInput &axis) override;

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

	UI::TextView *lastKeyEvent_ = nullptr;
	UI::TextView *lastLastKeyEvent_ = nullptr;

	void CreateViews() override;

	UI::EventReturn OnImmersiveModeChange(UI::EventParams &e);
	UI::EventReturn OnRenderingBackend(UI::EventParams &e);
	UI::EventReturn OnRecreateActivity(UI::EventParams &e);
};

class MockPSP;

class VisualMappingScreen : public UIDialogScreenWithGameBackground {
public:
	VisualMappingScreen(const Path &gamePath) : UIDialogScreenWithGameBackground(gamePath) {}

	const char *tag() const override { return "VisualMapping"; }

protected:
	void CreateViews() override;

	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void resized() override;

private:
	UI::EventReturn OnMapButton(UI::EventParams &e);
	UI::EventReturn OnBindAll(UI::EventParams &e);
	void HandleKeyMapping(KeyDef key);
	void MapNext(bool successive);

	MockPSP *psp_ = nullptr;
	int nextKey_ = 0;
	int bindAll_ = -1;
	bool replace_ = false;
};

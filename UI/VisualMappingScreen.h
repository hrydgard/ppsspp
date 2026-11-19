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

#include "Common/UI/View.h"
#include "Core/KeyMap.h"
#include "UI/SimpleDialogScreen.h"

class MockPSP;

class VisualMappingScreen : public UITwoPaneBaseDialogScreen {
public:
	VisualMappingScreen(const Path &gamePath);

	const char *tag() const override { return "VisualMapping"; }

	bool key(const KeyInput &key) override;
	void axis(const AxisInput &axis) override;

protected:
	void CreateSettingsViews(UI::ViewGroup *parent, bool inPopup) override;
	void CreateContentViews(UI::ViewGroup *parent) override;
	std::string_view GetTitle() const override;

	void dialogFinished(const Screen *dialog, DialogResult result) override;

private:
	void OnMapButton(UI::EventParams &e);
	void OnBindAll(UI::EventParams &e);
	void HandleKeyMapping(const KeyMap::MultiInputMapping &key);
	void MapNext(bool successive);

	MockPSP *psp_ = nullptr;
	int nextKey_ = 0;
	int bindAll_ = -1;
	bool replace_ = false;
};

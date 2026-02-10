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

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/TabHolder.h"
#include "BaseScreens.h"

class ControlLayoutView;

class TouchControlLayoutScreen : public UIBaseDialogScreen {
public:
	TouchControlLayoutScreen(const Path &gamePath) : UIBaseDialogScreen(gamePath) {}

	void CreateViews() override;
	void dialogFinished(const Screen *dialog, DialogResult result) override;
	void onFinish(DialogResult reason) override;
	void update() override;
	void resized() override;

	const char *tag() const override { return "TouchControlLayout"; }

protected:
	void OnReset(UI::EventParams &e);
	void OnMode(UI::EventParams &e);
	void OnLayoutSelection(UI::EventParams &e);


private:
	UI::ChoiceStrip *mode_ = nullptr;
	ControlLayoutView *layoutView_ = nullptr;
};

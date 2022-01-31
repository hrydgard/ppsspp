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
#include "MiscScreens.h"

class ControlLayoutView;

class TouchControlLayoutScreen : public UIDialogScreenWithBackground {
public:
	TouchControlLayoutScreen();

	virtual void CreateViews() override;
	virtual void dialogFinished(const Screen *dialog, DialogResult result) override;
	virtual void onFinish(DialogResult reason) override;
	virtual void update() override;
	virtual void resized() override;

protected:
	virtual UI::EventReturn OnReset(UI::EventParams &e);
	virtual UI::EventReturn OnVisibility(UI::EventParams &e);
	virtual UI::EventReturn OnMode(UI::EventParams &e);

private:
	UI::ChoiceStrip *mode_ = nullptr;
	ControlLayoutView *layoutView_ = nullptr;
};

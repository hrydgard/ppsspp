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

#include "base/functional.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "MiscScreens.h"
#include <vector>

class DragDropButton;

class TouchControlLayoutScreen : public UIDialogScreenWithBackground {
public:
	TouchControlLayoutScreen();

	virtual void CreateViews() override;
	virtual bool touch(const TouchInput &touch) override;
	virtual void dialogFinished(const Screen *dialog, DialogResult result) override;
	virtual void onFinish(DialogResult reason) override;

protected:
	virtual UI::EventReturn OnReset(UI::EventParams &e);
	virtual UI::EventReturn OnVisibility(UI::EventParams &e);

private:
	DragDropButton *pickedControl_;
	std::vector<DragDropButton *> controls_;
	UI::ChoiceStrip *mode_;
	DragDropButton *getPickedControl(const int x, const int y);

	// Touch down state for drag to resize etc
	float startX_;
	float startY_;
	float startScale_;
	float startSpacing_;
};

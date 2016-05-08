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

#include "ui/view.h"
#include "ui/viewgroup.h"
#include "MiscScreens.h"
#include <vector>

class DragDropDisplay;

class DisplayLayoutScreen : public UIDialogScreenWithBackground {
public:
	DisplayLayoutScreen();
	virtual void CreateViews() override;
	virtual bool touch(const TouchInput &touch) override;
	virtual void dialogFinished(const Screen *dialog, DialogResult result) override;
	virtual void onFinish(DialogResult reason) override;
	
protected:
	virtual UI::EventReturn OnCenter(UI::EventParams &e);
	virtual UI::EventReturn OnZoomTypeChange(UI::EventParams &e);

private:
	DragDropDisplay *picked_;
	DragDropDisplay *displayRepresentation_;
	UI::ChoiceStrip *mode_;
	UI::PopupMultiChoice *zoom_;
	UI::PopupMultiChoice *rotation_;
	bool displayRotEnable_;
	// Touch down state for drag to resize etc
	float startX_;
	float startY_;
	float startScale_, scaleUpdate_;
	float displayRepresentationScale_;
	int offsetTouchX, offsetTouchY;
	
};

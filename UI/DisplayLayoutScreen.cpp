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

#include <vector>

#include "base/colorutil.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui_atlas.h"

#include "DisplayLayoutScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "DisplayLayoutEditor.h"

static const int leftColumnWidth = 200;

// Ugly hackery, need to rework some stuff to get around this
static float local_dp_xres;
static float local_dp_yres;

class DragDropDisplay : public MultiTouchDisplay {
public:
	DragDropDisplay(float &x, float &y, int img, float &scale)
		: MultiTouchDisplay(img, scale, new UI::AnchorLayoutParams(x*local_dp_xres, y*local_dp_yres, UI::NONE, UI::NONE, true)),
		x_(x), y_(y), theScale_(scale) {
		scale_ = theScale_;
	}	

	virtual void SaveDisplayPosition() {
		x_ = bounds_.centerX() / local_dp_xres;
		y_ = bounds_.centerY() / local_dp_yres;
		scale_ = theScale_;
	}

	virtual float GetScale() const { return theScale_; }
	virtual void SetScale(float s) { theScale_ = s; scale_ = s; }

	private:

	float &x_, &y_;
	float &theScale_;
};

DisplayLayoutScreen::DisplayLayoutScreen() {
	picked_ = 0;
};


bool DisplayLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	int mode = mode_->GetSelection();
	if (g_Config.iSmallDisplayZoom == 0) { mode = -1; }

	const Bounds &screen_bounds = screenManager()->getUIContext()->GetBounds();

	if ((touch.flags & TOUCH_MOVE) && picked_ != 0) {
		if (mode == 0) {
			const Bounds &bounds = picked_->GetBounds();

			int mintouchX = screen_bounds.w / 4;
			int maxTouchX = screen_bounds.w - screen_bounds.w / 4;

			int minTouchY = screen_bounds.h / 4;
			int maxTouchY = screen_bounds.h - screen_bounds.h / 4;

			int newX = bounds.centerX(), newY = bounds.centerY();
			// we have to handle x and y separately since even if x is blocked, y may not be.
			if (touch.x > mintouchX && touch.x < maxTouchX) {
				// if the leftmost point of the control is ahead of the margin,
				// move it. Otherwise, don't.
				newX = touch.x;
			}
			if (touch.y > minTouchY && touch.y < maxTouchY) {
				newY = touch.y;
			}
			picked_->ReplaceLayoutParams(new UI::AnchorLayoutParams(newX, newY, NONE, NONE, true));
		}
		else if (mode == 1) {
			// Resize. Vertical = scaling, horizontal = spacing;
			// Up should be bigger so let's negate in that direction
			float diffX = (touch.x - startX_);
			float diffY = -(touch.y - startY_);

			float movementScale = 0.5f;
			float newScale = startScale_ + diffY * movementScale;
			if (newScale > 100.0f) newScale = 100.0f;
			if (newScale < 1.0f) newScale = 1.0f;
			picked_->SetScale(newScale);
		}
	}
	if ((touch.flags & TOUCH_DOWN) && picked_ == 0) {
		picked_ = displayRepresentation_;
		if (picked_) {
			startX_ = touch.x;
			startY_ = touch.y;
			startScale_ = picked_->GetScale();
		}
	}
	if ((touch.flags & TOUCH_UP) && picked_ != 0) {
		const Bounds &bounds = picked_->GetBounds();
		float saveX_ = touch.x;
		float saveY_ = touch.y;
		startScale_ = picked_->GetScale();
		picked_->SaveDisplayPosition();
		picked_ = 0;
	}
	return true;
};

void DisplayLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save();
}

UI::EventReturn DisplayLayoutScreen::OnCenter(UI::EventParams &e) {
	g_Config.fSmallDisplayOffsetX = 0.5f;
	g_Config.fSmallDisplayOffsetY = 0.5f;
	RecreateViews();
	return UI::EVENT_DONE;
};

UI::EventReturn DisplayLayoutScreen::OnZoomChange(UI::EventParams &e) {
	if (g_Config.iSmallDisplayZoom > 0) {
		g_Config.fSmallDisplayCustomZoom = (float)(g_Config.iSmallDisplayZoom * 8);
	} else {
		const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
		float autoBound = bounds.w / 480.0f * 8.0f;
		g_Config.fSmallDisplayCustomZoom = autoBound;
		g_Config.fSmallDisplayOffsetX = 0.5f;
		g_Config.fSmallDisplayOffsetY = 0.5f;
	}
	RecreateViews();
	return UI::EVENT_DONE;
};


void DisplayLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

void DisplayLayoutScreen::CreateViews() {
	if (g_Config.bStretchToDisplay) {
		// Shouldn't even be able to get here as the way into this dialog should be closed.
		return;
	}
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();

	local_dp_xres = bounds.w;
	local_dp_yres = bounds.h;

	using namespace UI;

	I18NCategory *di = GetI18NCategory("Dialog");
	I18NCategory *gr = GetI18NCategory("Graphics");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	// Just visual boundaries of the screen, should be easier to use than imagination
	float verticalBoundaryPositionL = local_dp_xres / 4.0f;
	float verticalBoundaryPositionR = local_dp_xres - verticalBoundaryPositionL;
	float horizontalBoundaryPositionL = local_dp_yres / 4.0f;
	float horizontalBoundaryPositionR = local_dp_yres - horizontalBoundaryPositionL;
	TabHolder *verticalBoundaryL = new TabHolder(ORIENT_VERTICAL, verticalBoundaryPositionL, new AnchorLayoutParams(0, 0, 0, 0, false));
	TabHolder *verticalBoundaryR = new TabHolder(ORIENT_VERTICAL, verticalBoundaryPositionR, new AnchorLayoutParams(0, 0, 0, 0, false));
	TabHolder *horizontalBoundaryL = new TabHolder(ORIENT_VERTICAL, verticalBoundaryPositionL * 2.0f, new AnchorLayoutParams(verticalBoundaryPositionL * 2.0f, horizontalBoundaryPositionL - 31.0f, 0, 0, true));
	TabHolder *horizontalBoundaryR = new TabHolder(ORIENT_VERTICAL, verticalBoundaryPositionL * 2.0f, new AnchorLayoutParams(verticalBoundaryPositionL * 2.0f, horizontalBoundaryPositionR + 31.0f, 0, 0, true));
	AnchorLayout *topBoundary = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	AnchorLayout *bottomBoundary = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	root_->Add(verticalBoundaryL);
	root_->Add(verticalBoundaryR);
	root_->Add(horizontalBoundaryL);
	root_->Add(horizontalBoundaryR);
	horizontalBoundaryL->AddTab("", topBoundary);
	horizontalBoundaryR->AddTab("", bottomBoundary);

	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10));
	static const char *zoomLevels[] = { "Auto", "1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x", "9x", "10x" };
	zoom_ = new PopupMultiChoice(&g_Config.iSmallDisplayZoom, gr->T("Zoom settings"), zoomLevels, 0, ARRAY_SIZE(zoomLevels), gr->GetName(), screenManager(), new AnchorLayoutParams(300, WRAP_CONTENT, verticalBoundaryPositionL * 2 - 150, NONE, NONE, 10));
	zoom_->OnChoice.Handle(this, &DisplayLayoutScreen::OnZoomChange);

	

	mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 158 + 64 + 10));
	if (g_Config.iSmallDisplayZoom == 0) {
		mode_->AddChoice(gr->T("Active (Auto)"));
		float autoBound = bounds.w / 480.0f * 8.0f;
		g_Config.fSmallDisplayCustomZoom = autoBound;
		g_Config.fSmallDisplayOffsetX = 0.5f;
		g_Config.fSmallDisplayOffsetY = 0.5f;
	} else {
		Choice *center = new Choice(di->T("Center"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 84));
		center->OnClick.Handle(this, &DisplayLayoutScreen::OnCenter);
		root_->Add(center);
		mode_->AddChoice(di->T("Move"));
		mode_->AddChoice(di->T("Resize"));
		mode_->SetSelection(0);
	}

	
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	root_->Add(mode_);
	root_->Add(zoom_);
	root_->Add(back);

	displayRepresentation_ = new DragDropDisplay(g_Config.fSmallDisplayOffsetX, g_Config.fSmallDisplayOffsetY, I_PSP_DISPLAY, g_Config.fSmallDisplayCustomZoom);
	if (g_Config.iInternalScreenRotation == 2 || g_Config.iInternalScreenRotation == 4) {
		displayRepresentation_->SetAngle(90.0f);
	}
	root_->Add(displayRepresentation_);
}

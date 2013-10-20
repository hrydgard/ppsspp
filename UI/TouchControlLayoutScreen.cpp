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

#include "TouchControlLayoutScreen.h"
#include "TouchControlVisibilityScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "base/colorutil.h"
#include "ui/ui_context.h"
#include "ui_atlas.h"
#include "gfx_es2/draw_buffer.h"
#include "GamepadEmu.h"
#include "i18n/i18n.h"

static const int leftMargin = 140; 

// convert from screen coordinates (leftMargin to dp_xres) to actual fullscreen coordinates (0 to dp_xres)
static inline int toFullscreenCoord(int screenx) {
	return  ((float)dp_xres / (dp_xres - leftMargin)) * (screenx - leftMargin);
}

// convert from external fullscreen  coordinates(0 to dp_xres)  to the current partial coordinates (leftMargin to dp_xres)
static inline int fromFullscreenCoord(int controllerX) {
	return leftMargin + ((dp_xres - leftMargin) / (float)dp_xres) * controllerX;
};

class DragDropButton : public MultiTouchButton {
public:
	DragDropButton(int &x, int &y, int bgImg, int img, float scale) 
	: MultiTouchButton(bgImg, img, scale, new UI::AnchorLayoutParams(fromFullscreenCoord(x), y, UI::NONE, UI::NONE, true)),
    x_(x), y_(y) {
		scale_ = scale;
	}

	virtual bool IsDown() {
		//don't want the button to enlarge and throw the user's perspective
		//of button size off whack.
		return false;
	};

	void SavePosition() {
		x_ = toFullscreenCoord(bounds_.centerX());
		y_ = bounds_.centerY();
	}

private:
	int &x_, &y_;
};

class PSPActionButtons : public DragDropButton {
public:
	PSPActionButtons(int &x, int &y, int actionButtonSpacing, float scale) 
	: DragDropButton(x, y, -1, -1, scale), actionButtonSpacing_(actionButtonSpacing) {
		using namespace UI;
		roundId_ = I_ROUND;

		circleId_ = I_CIRCLE;
		crossId_ = I_CROSS;
		triangleId_ = I_TRIANGLE;
		squareId_ = I_SQUARE;

		circleVisible_ = triangleVisible_ = squareVisible_ = crossVisible_ = true;
	};

	void setCircleVisibility(bool visible){
		circleVisible_ = visible;
	}

	void setCrossVisibility(bool visible){
		crossVisible_ = visible;
	}

	void setTriangleVisibility(bool visible){
		triangleVisible_ = visible;
	}

	void setSquareVisibility(bool visible){
		squareVisible_ = visible;
	}

	void Draw(UIContext &dc) {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		int centerX = bounds_.centerX();
		int centerY = bounds_.centerY();

		if (circleVisible_) {
			dc.Draw()->DrawImageRotated(roundId_, centerX + actionButtonSpacing_, centerY, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(circleId_,  centerX + actionButtonSpacing_, centerY, scale_, 0, color, false);
		}

		if (crossVisible_) {
			dc.Draw()->DrawImageRotated(roundId_, centerX, centerY + actionButtonSpacing_, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(crossId_, centerX, centerY + actionButtonSpacing_, scale_, 0, color, false);
		}

		if (triangleVisible_) {
			dc.Draw()->DrawImageRotated(roundId_, centerX, centerY - actionButtonSpacing_, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(triangleId_, centerX, centerY - actionButtonSpacing_, scale_, 0, color, false);
		}
		
		if (squareVisible_){
			dc.Draw()->DrawImageRotated(roundId_, centerX -  actionButtonSpacing_, centerY, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(squareId_, centerX -  actionButtonSpacing_, centerY, scale_, 0, color, false);
		}
	};

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const{
		const AtlasImage &image = dc.Draw()->GetAtlas()->images[roundId_];
		w = 2 * actionButtonSpacing_ + image.w * scale_;
		h = 2 * actionButtonSpacing_ + image.h * scale_;

		//w += 2 * actionButtonSpacing_;
		//h += 2 * actionButtonSpacing_;
	};

private:

	bool circleVisible_, crossVisible_, triangleVisible_, squareVisible_;

	int roundId_;
	int circleId_, crossId_, triangleId_, squareId_;

	int actionButtonSpacing_;
};

class PSPDPadButtons : public DragDropButton {
public:
	PSPDPadButtons(int &x, int &y, int DpadRadius, float scale) 
		: DragDropButton(x, y, -1, -1, scale), DpadRadius_(DpadRadius) {
	}

	void Draw(UIContext &dc) {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		static const float xoff[4] = {1, 0, -1, 0};
		static const float yoff[4] = {0, 1, 0, -1};

		for (int i = 0; i < 4; i++) {
			float x = bounds_.centerX() + xoff[i] * DpadRadius_;
			float y  = bounds_.centerY() + yoff[i] * DpadRadius_;
			float angle = i * M_PI / 2;

			dc.Draw()->DrawImageRotated(I_DIR, x, y, scale_, angle + PI, colorBg, false);
			dc.Draw()->DrawImageRotated(I_ARROW, x, y, scale_, angle + PI, color);
		}
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const{
		const AtlasImage &image = dc.Draw()->GetAtlas()->images[I_DIR];
		w =  2 * DpadRadius_ + image.w * scale_;
		h =  2 * DpadRadius_ + image.h * scale_;

		//w += 2 * DpadRadius_;
		//h += 2 * DpadRadius_;
	};

private:
	int DpadRadius_;
}; 

TouchControlLayoutScreen::TouchControlLayoutScreen() {
	pickedControl_ = 0;
};

void TouchControlLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	if ((touch.flags & TOUCH_MOVE) && pickedControl_ != 0) {
		const Bounds &bounds = pickedControl_->GetBounds();
		
		int mintouchX = leftMargin + bounds.w * 0.5;
		int maxTouchX = dp_xres - bounds.w * 0.5;

		int minTouchY = bounds.h * 0.5;
		int maxTouchY = dp_yres - bounds.h * 0.5;

		int newX = bounds.centerX(), newY = bounds.centerY();

		//we have to handle x and y separately since even if x is blocked, y may not be.
		if (touch.x > mintouchX && touch.x < maxTouchX) {
			//if the leftmost point of the control is ahead of the margin,
			//move it. Otherwise, don't.
			newX = touch.x;
		}
		if (touch.y > minTouchY && touch.y < maxTouchY) {
			newY = touch.y;
		}

		// ILOG("position: x = %d; y = %d", newX, newY);
		pickedControl_->ReplaceLayoutParams(new UI::AnchorLayoutParams(newX, newY, NONE, NONE, true));
	}
	if ((touch.flags & TOUCH_DOWN) && pickedControl_ == 0) {
		ILOG("->->->picked up")
		pickedControl_ = getPickedControl(touch.x, touch.y);
	}
	if ((touch.flags & TOUCH_UP) && pickedControl_ != 0) {
		pickedControl_->SavePosition();
		ILOG("->->->dropped down")
		pickedControl_ = 0;
	}
};

UI::EventReturn TouchControlLayoutScreen::OnBack(UI::EventParams &e) {
	g_Config.Save();

	if (PSP_IsInited()) {
		screenManager()->finishDialog(this, DR_CANCEL);
	} else {
		screenManager()->finishDialog(this, DR_OK);
	}

	return UI::EVENT_DONE;
};

UI::EventReturn TouchControlLayoutScreen::OnVisibility(UI::EventParams &e) {
	screenManager()->push(new TouchControlVisibilityScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlLayoutScreen::OnReset(UI::EventParams &e) {
	g_Config.iActionButtonSpacing = -1;
	g_Config.iActionButtonCenterX = -1;
	g_Config.iActionButtonCenterY = -1;
	g_Config.iDpadRadius = -1;
	g_Config.iDpadX = -1;
	g_Config.iDpadY = -1;
	g_Config.iStartKeyX = -1;
	g_Config.iStartKeyY = -1;
	g_Config.iSelectKeyX = -1;
	g_Config.iSelectKeyY = -1;
	g_Config.iUnthrottleKeyX = -1;
	g_Config.iUnthrottleKeyY = -1;
	g_Config.iLKeyX = -1;
	g_Config.iLKeyY = -1;
	g_Config.iRKeyX = -1;
	g_Config.iRKeyY = -1;
	g_Config.iAnalogStickX = -1;
	g_Config.iAnalogStickY = -1;
	InitPadLayout();
	RecreateViews();
	return UI::EVENT_DONE;
};

void TouchControlLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

void TouchControlLayoutScreen::CreateViews() {
	// setup g_Config for button layout
	InitPadLayout();

	using namespace UI;

	I18NCategory *c = GetI18NCategory("Controls");
	I18NCategory *d = GetI18NCategory("Dialog");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	Choice *reset = new Choice(d->T("Reset"), "", false, new AnchorLayoutParams(leftMargin, WRAP_CONTENT, 10, NONE, NONE, 84));
	Choice *back = new Choice(d->T("Back"), "", false, new AnchorLayoutParams(leftMargin, WRAP_CONTENT, 10, NONE, NONE, 10));
	Choice *visibility = new Choice(c->T("Visibility"), "", false, new AnchorLayoutParams(leftMargin, WRAP_CONTENT, 10, NONE, NONE, 158));
	reset->OnClick.Handle(this, &TouchControlLayoutScreen::OnReset);
	back->OnClick.Handle(this, &TouchControlLayoutScreen::OnBack);
	visibility->OnClick.Handle(this, &TouchControlLayoutScreen::OnVisibility);
	root_->Add(visibility);
	root_->Add(reset);
	root_->Add(back);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, leftMargin, new AnchorLayoutParams(10, 0, 10, 0, false));
	root_->Add(tabHolder);

	//this is more for show than anything else. It's used to provide a boundary 
	//so that buttons like back can be placed within the boundary.
	//serves no other purpose.
	AnchorLayout *controlsHolder = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	I18NCategory *ms = GetI18NCategory("MainSettings");

	tabHolder->AddTab(ms->T("Controls"), controlsHolder);

	if (!g_Config.bShowTouchControls){
		return;
	}

	float scale = g_Config.fButtonScale;
	controls_.clear();

	PSPActionButtons *actionButtons = new PSPActionButtons(g_Config.iActionButtonCenterX, g_Config.iActionButtonCenterY, g_Config.iActionButtonSpacing, scale);
	actionButtons->setCircleVisibility(g_Config.bShowTouchCircle);
	actionButtons->setCrossVisibility(g_Config.bShowTouchCross);
	actionButtons->setTriangleVisibility(g_Config.bShowTouchTriangle);
	actionButtons->setSquareVisibility(g_Config.bShowTouchSquare);

	controls_.push_back(actionButtons);

	if (g_Config.bShowTouchCross) {
		controls_.push_back(new PSPDPadButtons(g_Config.iDpadX, g_Config.iDpadY, g_Config.iDpadRadius, scale));
	}

	if (g_Config.bShowTouchSelect) {
		controls_.push_back(new DragDropButton(g_Config.iSelectKeyX, g_Config.iSelectKeyY, I_RECT, I_SELECT, scale));
	}

	if (g_Config.bShowTouchStart) {
		controls_.push_back(new DragDropButton(g_Config.iStartKeyX, g_Config.iStartKeyY, I_RECT, I_START, scale));
	}

	if (g_Config.bShowTouchUnthrottle) {
		DragDropButton *unthrottle = new DragDropButton(g_Config.iUnthrottleKeyX, g_Config.iUnthrottleKeyY, I_RECT, I_ARROW, scale);
		unthrottle->SetAngle(180.0f);
		controls_.push_back(unthrottle);
	}

	if (g_Config.bShowTouchLTrigger) {
		controls_.push_back(new DragDropButton(g_Config.iLKeyX, g_Config.iLKeyY, I_SHOULDER, I_L, scale));
	}

	if (g_Config.bShowTouchRTrigger) {
		DragDropButton *rbutton = new DragDropButton(g_Config.iRKeyX, g_Config.iRKeyY, I_SHOULDER, I_R, scale);
		rbutton->FlipImageH(true);
		controls_.push_back(rbutton);
	}

	if (g_Config.bShowTouchAnalogStick) {
		controls_.push_back(new DragDropButton(g_Config.iAnalogStickX, g_Config.iAnalogStickY, I_STICKBG, I_STICK, scale));
	};

	for (size_t i = 0; i < controls_.size(); i++) {
		root_->Add(controls_[i]);
	}
}

// return the control which was picked up by the touchEvent. If a control
// was already picked up, then it's being dragged around, so just return that instead
DragDropButton *TouchControlLayoutScreen::getPickedControl(const int x, const int y) {
	if (pickedControl_ != 0) {
		return pickedControl_;
	}

	for (size_t i = 0; i < controls_.size(); i++) {
		DragDropButton *control = controls_[i];
		const Bounds &bounds = control->GetBounds();
		static const int thresholdFactor = 1.5;

		Bounds tolerantBounds(bounds.x, bounds.y, bounds.w * thresholdFactor, bounds.h * thresholdFactor);
		if (tolerantBounds.Contains(x, y)) {
			return control;
		}
	}

	return 0;
}

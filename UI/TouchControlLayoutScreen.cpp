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

#include "TouchControlLayoutScreen.h"
#include "TouchControlVisibilityScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GamepadEmu.h"

static const int leftColumnWidth = 140;

// Ugly hackery, need to rework some stuff to get around this
static float local_dp_xres;
static float local_dp_yres;

static u32 GetButtonColor() {
	int theme_botton = g_Config.iR + g_Config.iG * 16 * 16 + g_Config.iB * 16 * 16 * 16 * 16;
	if (!g_Config.iTheme_botton)
		return g_Config.iTouchButtonStyle == 1 ? 0xFFFFFF : 0xc0b080;
	else return theme_botton;
}

class DragDropButton : public MultiTouchButton {
public:
	DragDropButton(float &x, float &y, int bgImg, int img, float &scale)
	: MultiTouchButton(bgImg, img, scale, new UI::AnchorLayoutParams(fromFullscreenCoord(x), y*local_dp_yres, UI::NONE, UI::NONE, true)),
		x_(x), y_(y), theScale_(scale) {
		scale_ = theScale_;
	}

	virtual bool IsDown() {
		// Don't want the button to enlarge and throw the user's perspective
		// of button size off whack.
		return false;
	};

	virtual void SavePosition() {
		x_ = toFullscreenCoord(bounds_.centerX());
		y_ = bounds_.centerY() / local_dp_yres;
		scale_ = theScale_;
	}

	virtual float GetScale() const { return theScale_; }
	virtual void SetScale(float s) { theScale_ = s; scale_ = s; }

	virtual float GetSpacing() const { return 1.0f; }
	virtual void SetSpacing(float s) { }

private:
	// convert from screen coordinates (leftColumnWidth to dp_xres) to actual fullscreen coordinates (0 to 1.0)
	inline float toFullscreenCoord(int screenx) {
		return  (float)(screenx - leftColumnWidth) / (local_dp_xres - leftColumnWidth);
	}

	// convert from external fullscreen  coordinates(0 to 1.0)  to the current partial coordinates (leftColumnWidth to dp_xres)
	inline int fromFullscreenCoord(float controllerX) {
		return leftColumnWidth + (local_dp_xres - leftColumnWidth) * controllerX;
	};

	float &x_, &y_;
	float &theScale_;
};

class PSPActionButtons : public DragDropButton {
public:
	PSPActionButtons(float &x, float &y, float &scale, float &spacing)
	: DragDropButton(x, y, -1, -1, scale), spacing_(spacing) {
		using namespace UI;
		roundId_ = g_Config.iTouchButtonStyle ? I_ROUND_LINE : I_ROUND;

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

		uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
		uint32_t color = colorAlpha(GetButtonColor(), opacity);

		int centerX = bounds_.centerX();
		int centerY = bounds_.centerY();

		float spacing = spacing_ * baseActionButtonSpacing;
		if (circleVisible_) {
			dc.Draw()->DrawImageRotated(roundId_, centerX + spacing, centerY, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(circleId_,  centerX + spacing, centerY, scale_, 0, color, false);
		}

		if (crossVisible_) {
			dc.Draw()->DrawImageRotated(roundId_, centerX, centerY + spacing, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(crossId_, centerX, centerY + spacing, scale_, 0, color, false);
		}

		if (triangleVisible_) {
			float y = centerY - spacing;
			y -= 2.8f * scale_;
			dc.Draw()->DrawImageRotated(roundId_, centerX, centerY - spacing, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(triangleId_, centerX, y, scale_, 0, color, false);
		}

		if (squareVisible_) {
			dc.Draw()->DrawImageRotated(roundId_, centerX - spacing, centerY, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(squareId_, centerX - spacing, centerY, scale_, 0, color, false);
		}
	};

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const{
		const AtlasImage &image = dc.Draw()->GetAtlas()->images[roundId_];

		w = (2 * baseActionButtonSpacing * spacing_) + image.w * scale_;
		h = (2 * baseActionButtonSpacing * spacing_) + image.h * scale_;
	}

	virtual float GetSpacing() const { return spacing_; }
	virtual void SetSpacing(float s) { spacing_ = s; }
	
	virtual void SavePosition() {
		DragDropButton::SavePosition();
	}

private:
	bool circleVisible_, crossVisible_, triangleVisible_, squareVisible_;

	int roundId_;
	int circleId_, crossId_, triangleId_, squareId_;

	float &spacing_;
};

class PSPDPadButtons : public DragDropButton {
public:
	PSPDPadButtons(float &x, float &y, float &scale, float &spacing)
		: DragDropButton(x, y, -1, -1, scale), spacing_(spacing) {
	}

	void Draw(UIContext &dc) {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
		uint32_t color = colorAlpha(GetButtonColor(), opacity);

		static const float xoff[4] = {1, 0, -1, 0};
		static const float yoff[4] = {0, 1, 0, -1};

		int dirImage = g_Config.iTouchButtonStyle ? I_DIR_LINE : I_DIR;

		for (int i = 0; i < 4; i++) {
			float r = D_pad_Radius * spacing_;
			float x = bounds_.centerX() + xoff[i] * r;
			float y = bounds_.centerY() + yoff[i] * r;
			float x2 = bounds_.centerX() + xoff[i] * (r + 10.f * scale_);
			float y2 = bounds_.centerY() + yoff[i] * (r + 10.f * scale_);
			float angle = i * M_PI / 2;

			dc.Draw()->DrawImageRotated(dirImage, x, y, scale_, angle + PI, colorBg, false);
			dc.Draw()->DrawImageRotated(I_ARROW, x2, y2, scale_, angle + PI, color);
		}
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const{
		const AtlasImage &image = dc.Draw()->GetAtlas()->images[I_DIR];
		w = 2 * D_pad_Radius * spacing_ + image.w * scale_;
		h = 2 * D_pad_Radius * spacing_ + image.h * scale_;
	};

	float GetSpacing() const { return spacing_; }
	virtual void SetSpacing(float s) { spacing_ = s; }

private:
	float &spacing_;
};

TouchControlLayoutScreen::TouchControlLayoutScreen() {
	pickedControl_ = 0;
};

bool TouchControlLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	int mode = mode_->GetSelection();

	const Bounds &screen_bounds = screenManager()->getUIContext()->GetBounds();

	if ((touch.flags & TOUCH_MOVE) && pickedControl_ != 0) {
		if (mode == 0) {
			const Bounds &bounds = pickedControl_->GetBounds();

			int mintouchX = leftColumnWidth + bounds.w * 0.5;
			int maxTouchX = screen_bounds.w - bounds.w * 0.5;

			int minTouchY = bounds.h * 0.5;
			int maxTouchY = screen_bounds.h - bounds.h * 0.5;

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
			pickedControl_->ReplaceLayoutParams(new UI::AnchorLayoutParams(newX, newY, NONE, NONE, true));
		} else if (mode == 1) {
			// Resize. Vertical = scaling, horizontal = spacing;
			// Up should be bigger so let's negate in that direction
			float diffX = (touch.x - startX_);
			float diffY = -(touch.y - startY_);

			float movementScale = 0.02f;
			float newScale = startScale_ + diffY * movementScale; 
			float newSpacing = startSpacing_ + diffX * movementScale;
			if (newScale > 3.0f) newScale = 3.0f;
			if (newScale < 0.5f) newScale = 0.5f;
			if (newSpacing > 3.0f) newSpacing = 3.0f;
			if (newSpacing < 0.5f) newSpacing = 0.5f;
			pickedControl_->SetSpacing(newSpacing);
			pickedControl_->SetScale(newScale);
		}
	}
	if ((touch.flags & TOUCH_DOWN) && pickedControl_ == 0) {
		pickedControl_ = getPickedControl(touch.x, touch.y);
		if (pickedControl_) {
			startX_ = touch.x;
			startY_ = touch.y;
			startSpacing_ = pickedControl_->GetSpacing();
			startScale_ = pickedControl_->GetScale();
		}
	}
	if ((touch.flags & TOUCH_UP) && pickedControl_ != 0) {
		pickedControl_->SavePosition();
		pickedControl_ = 0;
	}
	return true;
};

void TouchControlLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save();
}

UI::EventReturn TouchControlLayoutScreen::OnVisibility(UI::EventParams &e) {
	screenManager()->push(new TouchControlVisibilityScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlLayoutScreen::OnCombo_key(UI::EventParams &e) {
	screenManager()->push(new Combo_keyScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlLayoutScreen::OnCombo_key1(UI::EventParams &e) {
	screenManager()->push(new Combo_keyScreen1());
	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlLayoutScreen::OnCombo_key2(UI::EventParams &e) {
	screenManager()->push(new Combo_keyScreen2());
	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlLayoutScreen::OnReset(UI::EventParams &e) {
	ILOG("Resetting touch control layout");
	g_Config.ResetControlLayout();
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	InitPadLayout(bounds.w, bounds.h);
	RecreateViews();
	return UI::EVENT_DONE;
};

void TouchControlLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

void TouchControlLayoutScreen::CreateViews() {
	// setup g_Config for button layout
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	InitPadLayout(bounds.w, bounds.h);

	local_dp_xres = bounds.w;
	local_dp_yres = bounds.h;

	using namespace UI;

	I18NCategory *c = GetI18NCategory("Controls");
	I18NCategory *d = GetI18NCategory("Dialog");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	Choice *reset = new Choice(d->T("Reset"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 64));
	Choice *back = new Choice(d->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10));
	Choice *visibility = new Choice(c->T("Visibility"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 118));
	Choice *combo_key2 = new Choice(c->T("CBK2"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 304));
	Choice *combo_key1 = new Choice(c->T("CBK1"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 358));
	Choice *combo_key = new Choice(c->T("CBK"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 412));


	mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 118 + 44 + 10));
	mode_->AddChoice(d->T("Move"));
	mode_->AddChoice(d->T("Resize"));
	mode_->SetSelection(0);

	reset->OnClick.Handle(this, &TouchControlLayoutScreen::OnReset);
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	visibility->OnClick.Handle(this, &TouchControlLayoutScreen::OnVisibility);
	combo_key->OnClick.Handle(this, &TouchControlLayoutScreen::OnCombo_key);
	combo_key1->OnClick.Handle(this, &TouchControlLayoutScreen::OnCombo_key1);
	combo_key2->OnClick.Handle(this, &TouchControlLayoutScreen::OnCombo_key2);
	root_->Add(mode_);
	root_->Add(visibility);
	root_->Add(combo_key2);
	root_->Add(combo_key1);
	root_->Add(combo_key);
	root_->Add(reset);
	root_->Add(back);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, leftColumnWidth, new AnchorLayoutParams(10, 0, 10, 0, false));
	root_->Add(tabHolder);

	// this is more for show than anything else. It's used to provide a boundary
	// so that buttons like back can be placed within the boundary.
	// serves no other purpose.
	AnchorLayout *controlsHolder = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	I18NCategory *ms = GetI18NCategory("MainSettings");

	tabHolder->AddTab(ms->T("Controls"), controlsHolder);

	if (!g_Config.bShowTouchControls) {
		// Shouldn't even be able to get here as the way into this dialog should be closed.
		return;
	}

	controls_.clear();
	if (!g_Config.bActionButtonseparation)
	{
		
		PSPActionButtons *actionButtons = new PSPActionButtons(g_Config.fActionButtonCenterX, g_Config.fActionButtonCenterY, g_Config.fActionButtonScale, g_Config.fActionButtonSpacing);
		actionButtons->setCircleVisibility(g_Config.bShowTouchCircle);
		actionButtons->setCrossVisibility(g_Config.bShowTouchCross);
		actionButtons->setTriangleVisibility(g_Config.bShowTouchTriangle);
		actionButtons->setSquareVisibility(g_Config.bShowTouchSquare);

		controls_.push_back(actionButtons);
	}
	int rectImage = g_Config.iTouchButtonStyle ? I_RECT_LINE : I_RECT;
	int roundImage = g_Config.iTouchButtonStyle ? I_ROUND_LINE : I_ROUND;
	int shoulderImage = g_Config.iTouchButtonStyle ? I_SHOULDER_LINE : I_SHOULDER;
	int dirImage = g_Config.iTouchButtonStyle ? I_DIR_LINE : I_DIR;
	int dirImage_UP = g_Config.iTouchButtonStyle ? I_DIR_LINE_UP : I_DIR_UP;
	int dirImage_RIGHT = g_Config.iTouchButtonStyle ? I_DIR_LINE_RIGHT : I_DIR_RIGHT;
	int dirImage_DOWN = g_Config.iTouchButtonStyle ? I_DIR_LINE_DOWN : I_DIR_DOWN;
	int stickImage = g_Config.iTouchButtonStyle ? I_STICK_LINE : I_STICK;
	int stickBg = g_Config.iTouchButtonStyle ? I_STICK_BG_LINE : I_STICK_BG;
	int CombintionButton1 = g_Config.iComboButtonStyle ? I_SQUARE1 : I_STAR;
	int CombintionButton2 = g_Config.iComboButtonStyle ? I_TRIANGLE1 : I_EYE;
	int CombintionButton3 = g_Config.iComboButtonStyle ? I_CROSS1 : I_GC;


	if (g_Config.bActionButtonseparation)
	{
		if (g_Config.bShowTouchCircle) 
			controls_.push_back(new DragDropButton(g_Config.CircleX, g_Config.CircleY, roundImage, I_CIRCLE, g_Config.CircleScale));
		if (g_Config.bShowTouchCross)
			controls_.push_back(new DragDropButton(g_Config.CrossX, g_Config.CrossY, roundImage, I_CROSS, g_Config.CrossScale));
		if (g_Config.bShowTouchTriangle)
			controls_.push_back(new DragDropButton(g_Config.TriangleX, g_Config.TriangleY, roundImage, I_TRIANGLE, g_Config.TriangleScale));
		if (g_Config.bShowTouchSquare)
			controls_.push_back(new DragDropButton(g_Config.SquareX, g_Config.SquareY, roundImage, I_SQUARE, g_Config.SquareScale));

	}
	
	if (g_Config.bActionButtonseparation)
		{
			if (g_Config.bShowTouchDpad_LEFT)
				controls_.push_back(new DragDropButton(g_Config.fDpad_LEFTX, g_Config.fDpad_LEFTY, dirImage, I_ARROW, g_Config.fDpad_LEFTScale));
			if (g_Config.bShowTouchDpad_UP)
				controls_.push_back(new DragDropButton(g_Config.fDpad_UPX, g_Config.fDpad_UPY, dirImage_UP, I_ARROW1, g_Config.fDpad_UPScale));
			if (g_Config.bShowTouchDpad_RIGHT)
				controls_.push_back(new DragDropButton(g_Config.fDpad_RIGHTX, g_Config.fDpad_RIGHTY, dirImage_RIGHT, I_ARROW2, g_Config.fDpad_RIGHTScale));
			if (g_Config.bShowTouchDpad_DOWN)
				controls_.push_back(new DragDropButton(g_Config.fDpad_DOWNX, g_Config.fDpad_DOWNY, dirImage_DOWN, I_ARROW3, g_Config.fDpad_DOWNScale));
		}
	else
		{
			if (g_Config.bShowTouchDpad) {
			controls_.push_back(new PSPDPadButtons(g_Config.fDpadX, g_Config.fDpadY, g_Config.fDpadScale, g_Config.fDpadSpacing));
			}
		}
	if (g_Config.bShowTouchSelect) {
		controls_.push_back(new DragDropButton(g_Config.fSelectKeyX, g_Config.fSelectKeyY, rectImage, I_SELECT, g_Config.fSelectKeyScale));
	}

	if (g_Config.bShowTouchStart) {
		controls_.push_back(new DragDropButton(g_Config.fStartKeyX, g_Config.fStartKeyY, rectImage, I_START, g_Config.fStartKeyScale));
	}

	if (g_Config.bShowTouchUnthrottle) {
		DragDropButton *unthrottle = new DragDropButton(g_Config.fUnthrottleKeyX, g_Config.fUnthrottleKeyY, rectImage, I_ARROW, g_Config.fUnthrottleKeyScale);
		unthrottle->SetAngle(180.0f);
		controls_.push_back(unthrottle);
	}

	if (g_Config.bShowTouchLTrigger) {
		controls_.push_back(new DragDropButton(g_Config.fLKeyX, g_Config.fLKeyY, shoulderImage, I_L, g_Config.fLKeyScale));
	}

	if (g_Config.bShowTouchRTrigger) {
		DragDropButton *rbutton = new DragDropButton(g_Config.fRKeyX, g_Config.fRKeyY, shoulderImage, I_R, g_Config.fRKeyScale);
		rbutton->FlipImageH(true);
		controls_.push_back(rbutton);
	}

	if (g_Config.bShowTouchAnalogStick) {
		controls_.push_back(new DragDropButton(g_Config.fAnalogStickX, g_Config.fAnalogStickY, stickBg, stickImage, g_Config.fAnalogStickScale));
	}
	if (g_Config.bShowComboKey) {
		controls_.push_back(new DragDropButton(g_Config.fcomboX, g_Config.fcomboY, roundImage, CombintionButton1, g_Config.fcomboScale));
	}

	if (g_Config.bShowComboKey1) {
		controls_.push_back(new DragDropButton(g_Config.fcombo1X, g_Config.fcombo1Y, roundImage, CombintionButton2, g_Config.fcomboScale1));
	}
	if (g_Config.bShowComboKey2) {
		controls_.push_back(new DragDropButton(g_Config.fcombo2X, g_Config.fcombo2Y, roundImage, CombintionButton3, g_Config.fcomboScale2));
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
		const float thresholdFactor = 1.5f;

		Bounds tolerantBounds(bounds.x, bounds.y, bounds.w * thresholdFactor, bounds.h * thresholdFactor);
		if (tolerantBounds.Contains(x, y)) {
			return control;
		}
	}

	return 0;
}

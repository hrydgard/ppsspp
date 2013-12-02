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

static const int leftColumnWidth = 140;

// convert from screen coordinates (leftColumnWidth to dp_xres) to actual fullscreen coordinates (0 to 1.0)
static inline float toFullscreenCoord(int screenx) {
	return  (float)(screenx - leftColumnWidth) / (dp_xres - leftColumnWidth);
}

// convert from external fullscreen  coordinates(0 to 1.0)  to the current partial coordinates (leftColumnWidth to dp_xres)
static inline int fromFullscreenCoord(float controllerX) {
	return leftColumnWidth + (dp_xres - leftColumnWidth) * controllerX;
};

class DragDropButton : public MultiTouchButton {
public:
	DragDropButton(float &x, float &y, int bgImg, int img, float &scale)
	: MultiTouchButton(bgImg, img, scale, new UI::AnchorLayoutParams(fromFullscreenCoord(x), y*dp_yres, UI::NONE, UI::NONE, true)),
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
		y_ = bounds_.centerY() / dp_yres;
		scale_ = theScale_;
	}

	virtual float GetScale() const { return theScale_; }
	virtual void SetScale(float s) { theScale_ = s; scale_ = s; }

	virtual float GetSpacing() const { return 1.0f; }
	virtual void SetSpacing(float s) { }

private:
	float &x_, &y_;
	float &theScale_;
};

class PSPActionButtons : public DragDropButton {
public:
	PSPActionButtons(float &x, float &y, float &scale, float &spacing)
	: DragDropButton(x, y, -1, -1, scale), spacing_(spacing) {
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
			dc.Draw()->DrawImageRotated(roundId_, centerX, centerY - spacing, scale_, 0, colorBg, false);
			dc.Draw()->DrawImageRotated(triangleId_, centerX, centerY - spacing, scale_, 0, color, false);
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

		uint32_t colorBg = colorAlpha(0xc0b080, opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		static const float xoff[4] = {1, 0, -1, 0};
		static const float yoff[4] = {0, 1, 0, -1};

		for (int i = 0; i < 4; i++) {
			float x = bounds_.centerX() + xoff[i] * D_pad_Radius * spacing_;
			float y = bounds_.centerY() + yoff[i] * D_pad_Radius * spacing_;
			float angle = i * M_PI / 2;

			dc.Draw()->DrawImageRotated(I_DIR, x, y, scale_, angle + PI, colorBg, false);
			dc.Draw()->DrawImageRotated(I_ARROW, x, y, scale_, angle + PI, color);
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

void TouchControlLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	int mode = mode_->GetSelection();

	if ((touch.flags & TOUCH_MOVE) && pickedControl_ != 0) {
		if (mode == 0) {
			const Bounds &bounds = pickedControl_->GetBounds();

			int mintouchX = leftColumnWidth + bounds.w * 0.5;
			int maxTouchX = dp_xres - bounds.w * 0.5;

			int minTouchY = bounds.h * 0.5;
			int maxTouchY = dp_yres - bounds.h * 0.5;

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
};

void TouchControlLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save();
}

UI::EventReturn TouchControlLayoutScreen::OnVisibility(UI::EventParams &e) {
	screenManager()->push(new TouchControlVisibilityScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn TouchControlLayoutScreen::OnReset(UI::EventParams &e) {
	ILOG("Resetting touch control layout");
	float defaultScale = 1.15;
	g_Config.fActionButtonScale = defaultScale; 
	g_Config.fActionButtonSpacing = 1.0f;
	g_Config.fActionButtonCenterX = -1.0;
	g_Config.fActionButtonCenterY = -1.0;
	g_Config.fDpadScale = defaultScale;
	g_Config.fDpadSpacing = 1.0f;
	g_Config.fDpadX = -1.0;
	g_Config.fDpadY = -1.0;
	g_Config.fStartKeyX = -1.0;
	g_Config.fStartKeyY = -1.0;
	g_Config.fStartKeyScale = defaultScale; 
	g_Config.fSelectKeyX = -1.0;
	g_Config.fSelectKeyY = -1.0;
	g_Config.fSelectKeyScale = defaultScale; 
	g_Config.fUnthrottleKeyX = -1.0;
	g_Config.fUnthrottleKeyY = -1.0;
	g_Config.fUnthrottleKeyScale = defaultScale; 
	g_Config.fLKeyX = -1.0;
	g_Config.fLKeyY = -1.0;
	g_Config.fLKeyScale = defaultScale; 
	g_Config.fRKeyX = -1.0;
	g_Config.fRKeyY = -1.0;
	g_Config.fRKeyScale = defaultScale; 
	g_Config.fAnalogStickX = -1.0;
	g_Config.fAnalogStickY = -1.0;
	g_Config.fAnalogStickScale = defaultScale;
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

	Choice *reset = new Choice(d->T("Reset"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 84));
	Choice *back = new Choice(d->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10));
	Choice *visibility = new Choice(c->T("Visibility"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 158));
	// controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fButtonScale, 0.80, 2.0, c->T("Button Scaling"), screenManager()))
	// 	->OnChange.Handle(this, &GameSettingsScreen::OnChangeControlScaling);

	mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 158 + 64 + 10));
	mode_->AddChoice(d->T("Move"));
	mode_->AddChoice(d->T("Resize"));
	mode_->SetSelection(0);

	reset->OnClick.Handle(this, &TouchControlLayoutScreen::OnReset);
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	visibility->OnClick.Handle(this, &TouchControlLayoutScreen::OnVisibility);
	root_->Add(mode_);
	root_->Add(visibility);
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

	PSPActionButtons *actionButtons = new PSPActionButtons(g_Config.fActionButtonCenterX, g_Config.fActionButtonCenterY, g_Config.fActionButtonScale, g_Config.fActionButtonSpacing);
	actionButtons->setCircleVisibility(g_Config.bShowTouchCircle);
	actionButtons->setCrossVisibility(g_Config.bShowTouchCross);
	actionButtons->setTriangleVisibility(g_Config.bShowTouchTriangle);
	actionButtons->setSquareVisibility(g_Config.bShowTouchSquare);

	controls_.push_back(actionButtons);

	if (g_Config.bShowTouchDpad) {
		controls_.push_back(new PSPDPadButtons(g_Config.fDpadX, g_Config.fDpadY, g_Config.fDpadScale, g_Config.fDpadSpacing));
	}

	if (g_Config.bShowTouchSelect) {
		controls_.push_back(new DragDropButton(g_Config.fSelectKeyX, g_Config.fSelectKeyY, I_RECT, I_SELECT, g_Config.fSelectKeyScale));
	}

	if (g_Config.bShowTouchStart) {
		controls_.push_back(new DragDropButton(g_Config.fStartKeyX, g_Config.fStartKeyY, I_RECT, I_START, g_Config.fStartKeyScale));
	}

	if (g_Config.bShowTouchUnthrottle) {
		DragDropButton *unthrottle = new DragDropButton(g_Config.fUnthrottleKeyX, g_Config.fUnthrottleKeyY, I_RECT, I_ARROW, g_Config.fUnthrottleKeyScale);
		unthrottle->SetAngle(180.0f);
		controls_.push_back(unthrottle);
	}

	if (g_Config.bShowTouchLTrigger) {
		controls_.push_back(new DragDropButton(g_Config.fLKeyX, g_Config.fLKeyY, I_SHOULDER, I_L, g_Config.fLKeyScale));
	}

	if (g_Config.bShowTouchRTrigger) {
		DragDropButton *rbutton = new DragDropButton(g_Config.fRKeyX, g_Config.fRKeyY, I_SHOULDER, I_R, g_Config.fRKeyScale);
		rbutton->FlipImageH(true);
		controls_.push_back(rbutton);
	}

	if (g_Config.bShowTouchAnalogStick) {
		controls_.push_back(new DragDropButton(g_Config.fAnalogStickX, g_Config.fAnalogStickY, I_STICKBG, I_STICK, g_Config.fAnalogStickScale));
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

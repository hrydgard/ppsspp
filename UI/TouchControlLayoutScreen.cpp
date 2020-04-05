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

#include <algorithm>
#include <vector>

#include "base/colorutil.h"
#include "gfx_es2/draw_buffer.h"
#include "i18n/i18n.h"
#include "math/math_util.h"
#include "ui/ui_context.h"

#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "UI/GamepadEmu.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"

static const int leftColumnWidth = 140;

static u32 GetButtonColor() {
	return g_Config.iTouchButtonStyle != 0 ? 0xFFFFFF : 0xc0b080;
}

class DragDropButton : public MultiTouchButton {
public:
	DragDropButton(ConfigTouchPos &pos, ImageID bgImg, ImageID img, const Bounds &screenBounds)
	: MultiTouchButton(bgImg, bgImg, img, pos.scale, new UI::AnchorLayoutParams(fromFullscreenCoord(pos.x, screenBounds), pos.y * screenBounds.h, UI::NONE, UI::NONE, true)),
		x_(pos.x), y_(pos.y), theScale_(pos.scale), screenBounds_(screenBounds) {
		scale_ = theScale_;
	}

	bool IsDown() override {
		// Don't want the button to enlarge and throw the user's perspective
		// of button size off whack.
		return false;
	};

	virtual void SavePosition() {
		x_ = toFullscreenCoord(bounds_.centerX());
		y_ = bounds_.centerY() / screenBounds_.h;
		scale_ = theScale_;
	}

	virtual float GetScale() const { return theScale_; }
	virtual void SetScale(float s) { theScale_ = s; scale_ = s; }

	virtual float GetSpacing() const { return 1.0f; }
	virtual void SetSpacing(float s) { }

protected:
	float GetButtonOpacity() override {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;
		return std::max(0.5f, opacity);
	}

private:
	// convert from screen coordinates (leftColumnWidth to dp_xres) to actual fullscreen coordinates (0 to 1.0)
	inline float toFullscreenCoord(int screenx) {
		return  (float)(screenx - leftColumnWidth) / (screenBounds_.w - leftColumnWidth);
	}

	// convert from external fullscreen  coordinates(0 to 1.0)  to the current partial coordinates (leftColumnWidth to dp_xres)
	inline int fromFullscreenCoord(float controllerX, const Bounds &screenBounds) {
		return leftColumnWidth + (screenBounds.w - leftColumnWidth) * controllerX;
	};

	float &x_, &y_;
	float &theScale_;
	const Bounds &screenBounds_;
};

class PSPActionButtons : public DragDropButton {
public:
	PSPActionButtons(ConfigTouchPos &pos, float &spacing, const Bounds &screenBounds)
		: DragDropButton(pos, ImageID::invalid(), ImageID::invalid(), screenBounds), spacing_(spacing) {
		using namespace UI;
		roundId_ = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");
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

	void Draw(UIContext &dc) override {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
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

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(roundId_);

		w = (2.0f * baseActionButtonSpacing * spacing_) + image->w * scale_;
		h = (2.0f * baseActionButtonSpacing * spacing_) + image->h * scale_;
	}

	float GetSpacing() const override { return spacing_; }
	void SetSpacing(float s) override { spacing_ = s; }

private:
	bool circleVisible_ = true, crossVisible_ = true, triangleVisible_ = true, squareVisible_ = true;

	ImageID roundId_ = ImageID::invalid();
	ImageID circleId_ = ImageID("I_CIRCLE");
	ImageID crossId_ = ImageID("I_CROSS");
	ImageID triangleId_ = ImageID("I_TRIANGLE");
	ImageID squareId_ = ImageID("I_SQUARE");

	float &spacing_;
};

class PSPDPadButtons : public DragDropButton {
public:
	PSPDPadButtons(ConfigTouchPos &pos, float &spacing, const Bounds &screenBounds)
		: DragDropButton(pos, ImageID::invalid(), ImageID::invalid(), screenBounds), spacing_(spacing) {
	}

	void Draw(UIContext &dc) override {
		float opacity = g_Config.iTouchButtonOpacity / 100.0f;

		uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		static const float xoff[4] = {1, 0, -1, 0};
		static const float yoff[4] = {0, 1, 0, -1};

		ImageID dirImage = g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR");

		for (int i = 0; i < 4; i++) {
			float r = D_pad_Radius * spacing_;
			float x = bounds_.centerX() + xoff[i] * r;
			float y = bounds_.centerY() + yoff[i] * r;
			float x2 = bounds_.centerX() + xoff[i] * (r + 10.f * scale_);
			float y2 = bounds_.centerY() + yoff[i] * (r + 10.f * scale_);
			float angle = i * M_PI / 2;

			dc.Draw()->DrawImageRotated(dirImage, x, y, scale_, angle + PI, colorBg, false);
			dc.Draw()->DrawImageRotated(ImageID("I_ARROW"), x2, y2, scale_, angle + PI, color);
		}
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(ImageID("I_DIR"));
		w = 2 * D_pad_Radius * spacing_ + image->w * scale_;
		h = 2 * D_pad_Radius * spacing_ + image->h * scale_;
	};

	float GetSpacing() const override { return spacing_; }
	void SetSpacing(float s) override { spacing_ = s; }

private:
	float &spacing_;
};

class SnapGrid : public UI::View {
public:
	SnapGrid(int leftMargin, int rightMargin, int topMargin, int bottomMargin, u32 color) {
		x1 = leftMargin;
		x2 = rightMargin;
		y1 = topMargin;
		y2 = bottomMargin;
		col = color;
	}

	void Draw(UIContext &dc) override {
		if (g_Config.bTouchSnapToGrid) {
			dc.Flush();
			dc.BeginNoTex();
			for (int x = x1; x < x2; x += g_Config.iTouchSnapGridSize)
				dc.Draw()->vLine(x, y1, y2, col);
			for (int y = y1; y < y2; y += g_Config.iTouchSnapGridSize)
				dc.Draw()->hLine(x1, y, x2, col);
			dc.Flush();
			dc.Begin();
		}
	}

private:
	int x1, x2, y1, y2;
	u32 col;
};

TouchControlLayoutScreen::TouchControlLayoutScreen() {
	pickedControl_ = 0;
};

static Point ClampTo(const Point &p, const Bounds &b) {
	return Point(clamp_value(p.x, b.x, b.x + b.w), clamp_value(p.y, b.y, b.y + b.h));
}

bool TouchControlLayoutScreen::touch(const TouchInput &touch) {
	UIScreen::touch(touch);

	using namespace UI;

	if ((touch.flags & TOUCH_MOVE) && pickedControl_ != nullptr) {
		int mode = mode_->GetSelection();
		if (mode == 0) {
			const Bounds &bounds = pickedControl_->GetBounds();
			const auto &prevParams = pickedControl_->GetLayoutParams()->As<AnchorLayoutParams>();
			Point newPos(prevParams->left, prevParams->top);

			Bounds validRange = screenManager()->getUIContext()->GetBounds();
			validRange.x += leftColumnWidth + bounds.w * 0.5f;
			validRange.w -= leftColumnWidth + bounds.w;
			validRange.y += bounds.h * 0.5f;
			validRange.h -= bounds.h;

			newPos.x = touch.x;
			newPos.y = touch.y;
			if (g_Config.bTouchSnapToGrid) {
				newPos.x -= (int)(newPos.x - bounds.w) % g_Config.iTouchSnapGridSize;
				newPos.y -= (int)(newPos.y - bounds.h) % g_Config.iTouchSnapGridSize;
			}

			newPos = ClampTo(newPos, validRange);
			pickedControl_->ReplaceLayoutParams(new AnchorLayoutParams(newPos.x, newPos.y, NONE, NONE, true));
		} else if (mode == 1) {
			// Resize. Vertical = scaling, horizontal = spacing;
			// Up should be bigger so let's negate in that direction
			float diffX = (touch.x - startX_);
			float diffY = -(touch.y - startY_);

			// Snap to grid
			if (g_Config.bTouchSnapToGrid) {
					diffX -= (int)(touch.x - startX_) % (g_Config.iTouchSnapGridSize/2);
					diffY += (int)(touch.y - startY_) % (g_Config.iTouchSnapGridSize/2);
			}
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
}

void TouchControlLayoutScreen::resized() {
	RecreateViews();
}

void TouchControlLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save("TouchControlLayoutScreen::onFinish");
}

UI::EventReturn TouchControlLayoutScreen::OnVisibility(UI::EventParams &e) {
	screenManager()->push(new TouchControlVisibilityScreen());
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

	using namespace UI;

	auto co = GetI18NCategory("Controls");
	auto di = GetI18NCategory("Dialog");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	Choice *reset = new Choice(di->T("Reset"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 84));
	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10));
	Choice *visibility = new Choice(co->T("Visibility"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 298));
	// controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fButtonScale, 0.80, 2.0, co->T("Button Scaling"), screenManager()))
	// 	->OnChange.Handle(this, &GameSettingsScreen::OnChangeControlScaling);

	CheckBox *snap = new CheckBox(&g_Config.bTouchSnapToGrid, di->T("Snap"), "", new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 228));
	PopupSliderChoice *gridSize = new PopupSliderChoice(&g_Config.iTouchSnapGridSize, 2, 256, di->T("Grid"), screenManager(), "", new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 158));
	gridSize->SetEnabledPtr(&g_Config.bTouchSnapToGrid);

	mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 140 + 158 + 64 + 10));
	mode_->AddChoice(di->T("Move"));
	mode_->AddChoice(di->T("Resize"));
	mode_->SetSelection(0);

	reset->OnClick.Handle(this, &TouchControlLayoutScreen::OnReset);
	back->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	visibility->OnClick.Handle(this, &TouchControlLayoutScreen::OnVisibility);
	root_->Add(mode_);
	root_->Add(visibility);
	root_->Add(snap);
	root_->Add(gridSize);
	root_->Add(reset);
	root_->Add(back);

	TabHolder *tabHolder = new TabHolder(ORIENT_VERTICAL, leftColumnWidth, new AnchorLayoutParams(10, 0, 10, 0, false));
	tabHolder->SetTag("TouchControlLayout");
	root_->Add(tabHolder);

	// this is more for show than anything else. It's used to provide a boundary
	// so that buttons like back can be placed within the boundary.
	// serves no other purpose.
	AnchorLayout *controlsHolder = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	auto ms = GetI18NCategory("MainSettings");

	//tabHolder->AddTab(ms->T("Controls"), controlsHolder);

	if (!g_Config.bShowTouchControls) {
		// Shouldn't even be able to get here as the way into this dialog should be closed.
		return;
	}

	controls_.clear();

	PSPActionButtons *actionButtons = new PSPActionButtons(g_Config.touchActionButtonCenter, g_Config.fActionButtonSpacing, bounds);
	actionButtons->setCircleVisibility(g_Config.bShowTouchCircle);
	actionButtons->setCrossVisibility(g_Config.bShowTouchCross);
	actionButtons->setTriangleVisibility(g_Config.bShowTouchTriangle);
	actionButtons->setSquareVisibility(g_Config.bShowTouchSquare);

	controls_.push_back(actionButtons);

	ImageID rectImage = g_Config.iTouchButtonStyle ? ImageID("I_RECT_LINE") : ImageID("I_RECT");
	ImageID shoulderImage = g_Config.iTouchButtonStyle ? ImageID("I_SHOULDER_LINE") : ImageID("I_SHOULDER");
	ImageID dirImage = g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR");
	ImageID stickImage = g_Config.iTouchButtonStyle ? ImageID("I_STICK_LINE") : ImageID("I_STICK");
	ImageID stickBg = g_Config.iTouchButtonStyle ? ImageID("I_STICK_BG_LINE") : ImageID("I_STICK_BG");
	ImageID roundImage = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");

	const ImageID comboKeyImages[5] = { ImageID("I_1"), ImageID("I_2"), ImageID("I_3"), ImageID("I_4"), ImageID("I_5") };

	auto addDragDropButton = [&](ConfigTouchPos &pos, ImageID bgImg, ImageID img) {
		DragDropButton *b = nullptr;
		if (pos.show) {
			b = new DragDropButton(pos, bgImg, img, bounds);
			controls_.push_back(b);
		}
		return b;
	};

	if (g_Config.touchDpad.show) {
		controls_.push_back(new PSPDPadButtons(g_Config.touchDpad, g_Config.fDpadSpacing, bounds));
	}

	addDragDropButton(g_Config.touchSelectKey, rectImage, ImageID("I_SELECT"));
	addDragDropButton(g_Config.touchStartKey, rectImage, ImageID("I_START"));

	if (auto *unthrottle = addDragDropButton(g_Config.touchUnthrottleKey, rectImage, ImageID("I_ARROW"))) {
		unthrottle->SetAngle(180.0f);
	}
	if (auto *speed1 = addDragDropButton(g_Config.touchSpeed1Key, rectImage, ImageID("I_ARROW"))) {
		speed1->SetAngle(170.0f, 180.0f);
	}
	if (auto *speed2 = addDragDropButton(g_Config.touchSpeed2Key, rectImage, ImageID("I_ARROW"))) {
		speed2->SetAngle(190.0f, 180.0f);
	}
	if (auto *rapidFire = addDragDropButton(g_Config.touchRapidFireKey, rectImage, ImageID("I_ARROW"))) {
		rapidFire->SetAngle(90.0f, 180.0f);
	}
	if (auto *analogRotationCW = addDragDropButton(g_Config.touchAnalogRotationCWKey, rectImage, ImageID("I_ARROW"))) {
		analogRotationCW->SetAngle(190.0f, 180.0f);
	}
	if (auto *analogRotationCCW = addDragDropButton(g_Config.touchAnalogRotationCCWKey, rectImage, ImageID("I_ARROW"))) {
		analogRotationCCW->SetAngle(350.0f, 180.0f);
	}

	addDragDropButton(g_Config.touchLKey, shoulderImage, ImageID("I_L"));
	if (auto *rbutton = addDragDropButton(g_Config.touchRKey, shoulderImage, ImageID("I_R"))) {
		rbutton->FlipImageH(true);
	}

	addDragDropButton(g_Config.touchAnalogStick, stickBg, stickImage);
	addDragDropButton(g_Config.touchRightAnalogStick, stickBg, stickImage);
	addDragDropButton(g_Config.touchCombo0, roundImage, comboKeyImages[0]);
	addDragDropButton(g_Config.touchCombo1, roundImage, comboKeyImages[1]);
	addDragDropButton(g_Config.touchCombo2, roundImage, comboKeyImages[2]);
	addDragDropButton(g_Config.touchCombo3, roundImage, comboKeyImages[3]);
	addDragDropButton(g_Config.touchCombo4, roundImage, comboKeyImages[4]);

	for (size_t i = 0; i < controls_.size(); i++) {
		root_->Add(controls_[i]);
	}

	root_->Add(new SnapGrid(leftColumnWidth+10, bounds.w, 0, bounds.h, 0x3FFFFFFF));
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

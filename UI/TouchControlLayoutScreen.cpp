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

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Math/math_util.h"
#include "Common/UI/Context.h"

#include "Common/Common.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "UI/GamepadEmu.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"

static u32 GetButtonColor() {
	return g_Config.iTouchButtonStyle != 0 ? 0xFFFFFF : 0xc0b080;
}

class DragDropButton : public MultiTouchButton {
public:
	DragDropButton(ConfigTouchPos &pos, const char *key, ImageID bgImg, ImageID img, const Bounds &screenBounds)
	: MultiTouchButton(key, bgImg, bgImg, img, pos.scale, new UI::AnchorLayoutParams(pos.x * screenBounds.w, pos.y * screenBounds.h, UI::NONE, UI::NONE, true)),
		x_(pos.x), y_(pos.y), theScale_(pos.scale), screenBounds_(screenBounds) {
		scale_ = theScale_;
	}

	bool IsDown() override {
		// Don't want the button to enlarge and throw the user's perspective
		// of button size off whack.
		return false;
	};

	virtual void SavePosition() {
		x_ = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		y_ = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
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
	float &x_, &y_;
	float &theScale_;
	const Bounds &screenBounds_;
};

class PSPActionButtons : public DragDropButton {
public:
	PSPActionButtons(ConfigTouchPos &pos, const char *key, float &spacing, const Bounds &screenBounds)
		: DragDropButton(pos, key, ImageID::invalid(), ImageID::invalid(), screenBounds), spacing_(spacing) {
		using namespace UI;
		roundId_ = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");
	};

	void setCircleVisibility(bool visible) {
		circleVisible_ = visible;
	}

	void setCrossVisibility(bool visible) {
		crossVisible_ = visible;
	}

	void setTriangleVisibility(bool visible) {
		triangleVisible_ = visible;
	}

	void setSquareVisibility(bool visible) {
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
	PSPDPadButtons(ConfigTouchPos &pos, const char *key, float &spacing, const Bounds &screenBounds)
		: DragDropButton(pos, key, ImageID::invalid(), ImageID::invalid(), screenBounds), spacing_(spacing) {
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
			float xOffset = bounds_.x;
			float yOffset = bounds_.y;
			for (int x = x1; x < x2; x += g_Config.iTouchSnapGridSize)
				dc.Draw()->vLine(x + xOffset, y1 + yOffset, y2 + yOffset, col);
			for (int y = y1; y < y2; y += g_Config.iTouchSnapGridSize)
				dc.Draw()->hLine(x1 + xOffset, y + yOffset, x2 + xOffset, col);
			dc.Flush();
			dc.Begin();
		}
	}

	std::string DescribeText() const override {
		return "";
	}

private:
	int x1, x2, y1, y2;
	u32 col;
};

class DragDropButton;

class ControlLayoutView : public UI::AnchorLayout {
public:
	explicit ControlLayoutView(UI::LayoutParams *layoutParams)
		: UI::AnchorLayout(layoutParams) {
	}

	void Touch(const TouchInput &input) override;
	void CreateViews();
	bool HasCreatedViews() const {
		return !controls_.empty();
	}

	DragDropButton *pickedControl_ = nullptr;
	DragDropButton *getPickedControl(const int x, const int y);
	std::vector<DragDropButton *> controls_;

	// Touch down state for dragging
	float startObjectX_ = -1.0f;
	float startObjectY_ = -1.0f;
	float startDragX_ = -1.0f;
	float startDragY_ = -1.0f;
	float startScale_ = -1.0f;
	float startSpacing_ = -1.0f;

	int mode_ = 0;
};

static Point ClampTo(const Point &p, const Bounds &b) {
	return Point(clamp_value(p.x, b.x, b.x + b.w), clamp_value(p.y, b.y, b.y + b.h));
}

void ControlLayoutView::Touch(const TouchInput &touch) {
	using namespace UI;

	if ((touch.flags & TOUCH_MOVE) && pickedControl_ != nullptr) {
		if (mode_ == 0) {
			const Bounds &controlBounds = pickedControl_->GetBounds();

			// Allow placing the control halfway outside the play area.
			Bounds validRange = this->GetBounds();
			// Control coordinates are relative inside the bounds.
			validRange.x = 0.0f;
			validRange.y = 0.0f;

			validRange.x += controlBounds.w * 0.5f;
			validRange.w -= controlBounds.w;
			validRange.y += controlBounds.h * 0.5f;
			validRange.h -= controlBounds.h;

			Point newPos;
			newPos.x = startObjectX_ + (touch.x - startDragX_);
			newPos.y = startObjectY_ + (touch.y - startDragY_);
			if (g_Config.bTouchSnapToGrid) {
				newPos.x -= fmod(newPos.x - controlBounds.w, g_Config.iTouchSnapGridSize);
				newPos.y -= fmod(newPos.y - controlBounds.h, g_Config.iTouchSnapGridSize);
			}

			newPos = ClampTo(newPos, validRange);
			pickedControl_->ReplaceLayoutParams(new AnchorLayoutParams(newPos.x, newPos.y, NONE, NONE, true));
		} else if (mode_ == 1) {
			// Resize. Vertical = scaling, horizontal = spacing;
			// Up should be bigger so let's negate in that direction
			float diffX = (touch.x - startDragX_);
			float diffY = -(touch.y - startDragY_);

			// Snap to grid
			if (g_Config.bTouchSnapToGrid) {
				diffX -= fmod(touch.x - startDragX_, g_Config.iTouchSnapGridSize/2);
				diffY += fmod(touch.y - startDragY_, g_Config.iTouchSnapGridSize/2);
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
			startDragX_ = touch.x;
			startDragY_ = touch.y;
			const auto &prevParams = pickedControl_->GetLayoutParams()->As<AnchorLayoutParams>();
			startObjectX_ = prevParams->left;
			startObjectY_ = prevParams->top;

			startSpacing_ = pickedControl_->GetSpacing();
			startScale_ = pickedControl_->GetScale();
		}
	}
	if ((touch.flags & TOUCH_UP) && pickedControl_ != 0) {
		pickedControl_->SavePosition();
		pickedControl_ = 0;
	}
}

void ControlLayoutView::CreateViews() {
	using namespace CustomKey;
	const Bounds &bounds = GetBounds();
	if (bounds.w == 0.0f || bounds.h == 0.0f) {
		// Layout hasn't happened yet, return.
		// See comment in TouchControlLayoutScreen::update().
		return;
	}

	// Create all the views.

	PSPActionButtons *actionButtons = new PSPActionButtons(g_Config.touchActionButtonCenter, "Action buttons", g_Config.fActionButtonSpacing, bounds);
	actionButtons->setCircleVisibility(g_Config.bShowTouchCircle);
	actionButtons->setCrossVisibility(g_Config.bShowTouchCross);
	actionButtons->setTriangleVisibility(g_Config.bShowTouchTriangle);
	actionButtons->setSquareVisibility(g_Config.bShowTouchSquare);

	controls_.push_back(actionButtons);

	ImageID rectImage = g_Config.iTouchButtonStyle ? ImageID("I_RECT_LINE") : ImageID("I_RECT");
	ImageID shoulderImage = g_Config.iTouchButtonStyle ? ImageID("I_SHOULDER_LINE") : ImageID("I_SHOULDER");
	ImageID stickImage = g_Config.iTouchButtonStyle ? ImageID("I_STICK_LINE") : ImageID("I_STICK");
	ImageID stickBg = g_Config.iTouchButtonStyle ? ImageID("I_STICK_BG_LINE") : ImageID("I_STICK_BG");
	ImageID roundImage = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");

	auto addDragDropButton = [&](ConfigTouchPos &pos, const char *key, ImageID bgImg, ImageID img) {
		DragDropButton *b = nullptr;
		if (pos.show) {
			b = new DragDropButton(pos, key, bgImg, img, bounds);
			controls_.push_back(b);
		}
		return b;
	};

	if (g_Config.touchDpad.show) {
		controls_.push_back(new PSPDPadButtons(g_Config.touchDpad, "D-pad", g_Config.fDpadSpacing, bounds));
	}

	addDragDropButton(g_Config.touchSelectKey, "Select button", rectImage, ImageID("I_SELECT"));
	addDragDropButton(g_Config.touchStartKey, "Start button", rectImage, ImageID("I_START"));

	if (auto *unthrottle = addDragDropButton(g_Config.touchUnthrottleKey, "Unthrottle button", rectImage, ImageID("I_ARROW"))) {
		unthrottle->SetAngle(180.0f);
	}
	addDragDropButton(g_Config.touchLKey, "Left shoulder button", shoulderImage, ImageID("I_L"));
	if (auto *rbutton = addDragDropButton(g_Config.touchRKey, "Right shoulder button", shoulderImage, ImageID("I_R"))) {
		rbutton->FlipImageH(true);
	}

	addDragDropButton(g_Config.touchAnalogStick, "Left analog stick", stickBg, stickImage);
	addDragDropButton(g_Config.touchRightAnalogStick, "Right analog stick", stickBg, stickImage);

	auto addDragComboKey = [&](ConfigTouchPos &pos, const char *key, const ConfigCustomButton& cfg) {
		DragDropButton *b = nullptr;
		if (pos.show) {
			b = new DragDropButton(pos, key, g_Config.iTouchButtonStyle == 0 ? comboKeyShapes[cfg.shape].i : comboKeyShapes[cfg.shape].l, comboKeyImages[cfg.image].i, bounds);
			b->FlipImageH(comboKeyShapes[cfg.shape].f);
			b->SetAngle(comboKeyImages[cfg.image].r, comboKeyShapes[cfg.shape].r);
			controls_.push_back(b);
		}
		return b;
	};
	addDragComboKey(g_Config.touchCombo0, "Custom 1 button", g_Config.CustomKey0);
	addDragComboKey(g_Config.touchCombo1, "Custom 2 button", g_Config.CustomKey1);
	addDragComboKey(g_Config.touchCombo2, "Custom 3 button", g_Config.CustomKey2);
	addDragComboKey(g_Config.touchCombo3, "Custom 4 button", g_Config.CustomKey3);
	addDragComboKey(g_Config.touchCombo4, "Custom 5 button", g_Config.CustomKey4);
	addDragComboKey(g_Config.touchCombo5, "Custom 6 button", g_Config.CustomKey5);
	addDragComboKey(g_Config.touchCombo6, "Custom 7 button", g_Config.CustomKey6);
	addDragComboKey(g_Config.touchCombo7, "Custom 8 button", g_Config.CustomKey7);
	addDragComboKey(g_Config.touchCombo8, "Custom 9 button", g_Config.CustomKey8);
	addDragComboKey(g_Config.touchCombo9, "Custom 10 button", g_Config.CustomKey9);

	for (size_t i = 0; i < controls_.size(); i++) {
		Add(controls_[i]);
	}

	Add(new SnapGrid(0, bounds.w, 0, bounds.h, 0x3FFFFFFF));
}

// return the control which was picked up by the touchEvent. If a control
// was already picked up, then it's being dragged around, so just return that instead
DragDropButton *ControlLayoutView::getPickedControl(const int x, const int y) {
	if (pickedControl_ != 0) {
		return pickedControl_;
	}

	for (size_t i = 0; i < controls_.size(); i++) {
		DragDropButton *control = controls_[i];
		const Bounds &bounds = control->GetBounds();
		const float thresholdFactor = 0.25f;
		const float thresholdW = thresholdFactor * bounds.w;
		const float thresholdH = thresholdFactor * bounds.h;

		Bounds tolerantBounds(bounds.x - thresholdW * 0.5, bounds.y - thresholdH * 0.5 , bounds.w + thresholdW, bounds.h + thresholdH);
		if (tolerantBounds.Contains(x, y)) {
			return control;
		}
	}

	return 0;
}

TouchControlLayoutScreen::TouchControlLayoutScreen() {}

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
	INFO_LOG(G3D, "Resetting touch control layout");
	g_Config.ResetControlLayout();
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	InitPadLayout(bounds.w, bounds.h);
	RecreateViews();
	return UI::EVENT_DONE;
};

void TouchControlLayoutScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	RecreateViews();
}

UI::EventReturn TouchControlLayoutScreen::OnMode(UI::EventParams &e) {
	int mode = mode_->GetSelection();
	if (layoutView_) {
		layoutView_->mode_ = mode;
	}
	return UI::EVENT_DONE;
}

void TouchControlLayoutScreen::update() {
	UIDialogScreenWithBackground::update();

	// TODO: We really, really need a cleaner solution for creating sub-views
	// of custom compound controls.
	if (layoutView_) {
		if (!layoutView_->HasCreatedViews()) {
			layoutView_->CreateViews();
		}
	}
}

void TouchControlLayoutScreen::CreateViews() {
	// setup g_Config for button layout
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	InitPadLayout(bounds.w, bounds.h);

	const float leftColumnWidth = 140.0f;

	using namespace UI;

	auto co = GetI18NCategory("Controls");
	auto di = GetI18NCategory("Dialog");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	Choice *reset = new Choice(di->T("Reset"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 84));
	Choice *back = new Choice(di->T("Back"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 10));
	Choice *visibility = new Choice(co->T("Customize"), "", false, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 298));
	// controlsSettings->Add(new PopupSliderChoiceFloat(&g_Config.fButtonScale, 0.80, 2.0, co->T("Button Scaling"), screenManager()))
	// 	->OnChange.Handle(this, &GameSettingsScreen::OnChangeControlScaling);

	CheckBox *snap = new CheckBox(&g_Config.bTouchSnapToGrid, di->T("Snap"), "", new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 228));
	PopupSliderChoice *gridSize = new PopupSliderChoice(&g_Config.iTouchSnapGridSize, 2, 256, di->T("Grid"), screenManager(), "", new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 158));
	gridSize->SetEnabledPtr(&g_Config.bTouchSnapToGrid);

	mode_ = new ChoiceStrip(ORIENT_VERTICAL, new AnchorLayoutParams(leftColumnWidth, WRAP_CONTENT, 10, NONE, NONE, 140 + 158 + 64 + 10));
	mode_->AddChoice(di->T("Move"));
	mode_->AddChoice(di->T("Resize"));
	mode_->SetSelection(0, false);
	mode_->OnChoice.Handle(this, &TouchControlLayoutScreen::OnMode);

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

	layoutView_ = root_->Add(new ControlLayoutView(new AnchorLayoutParams(leftColumnWidth + 10, 0.0f, 0.0f, 0.0f, false)));

	// this is more for show than anything else. It's used to provide a boundary
	// so that buttons like back can be placed within the boundary.
	// serves no other purpose.
	// AnchorLayout *controlsHolder = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	auto ms = GetI18NCategory("MainSettings");

	//tabHolder->AddTab(ms->T("Controls"), controlsHolder);

	if (!g_Config.bShowTouchControls) {
		// Shouldn't even be able to get here as the way into this dialog should be closed.
		return;
	}
}

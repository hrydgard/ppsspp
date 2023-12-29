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
#include "Common/System/Display.h"
#include "Common/UI/Context.h"

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "UI/GamepadEmu.h"
#include "UI/TouchControlLayoutScreen.h"
#include "UI/TouchControlVisibilityScreen.h"

static float layoutAreaScale = 1.0f;

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

	void Draw(UIContext &dc) override {
		scale_ = theScale_*layoutAreaScale; // Scale down just for rendering
		MultiTouchButton::Draw(dc);
		scale_ = theScale_/layoutAreaScale; // is this is needed?
	}

	virtual void SavePosition() {
		x_ = (bounds_.centerX() - screenBounds_.x) / screenBounds_.w;
		y_ = (bounds_.centerY() - screenBounds_.y) / screenBounds_.h;
		scale_ = theScale_;
	}

	virtual float GetScale() const { return theScale_; }
	virtual void SetScale(float s) { theScale_ = s; scale_ = s; }

	virtual float GetSpacing() const { return 1.0f; }
	virtual void SetSpacing(float s) { }

	virtual bool Contains(float x, float y) {
		const float thresholdFactor = 0.25f;
		const float thresholdW = thresholdFactor * bounds_.w;
		const float thresholdH = thresholdFactor * bounds_.h;

		Bounds tolerantBounds(bounds_.x - thresholdW * 0.5, bounds_.y - thresholdH * 0.5 , bounds_.w + thresholdW, bounds_.h + thresholdH);
		return tolerantBounds.Contains(x, y);
	}

protected:
	const Bounds &screenBounds_;
	float &theScale_;
	float &x_, &y_;
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
		scale_ = theScale_*layoutAreaScale;
		float opacity = GamepadGetOpacity();
		uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		int centerX = bounds_.centerX();
		int centerY = bounds_.centerY();

		float spacing = spacing_ * baseActionButtonSpacing * layoutAreaScale;
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
		scale_ = theScale_/layoutAreaScale;
	};

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(roundId_);

		w = (2.0f * baseActionButtonSpacing * spacing_) + image->w * scale_;
		h = (2.0f * baseActionButtonSpacing * spacing_) + image->h * scale_;
	}

	float GetSpacing() const override { return spacing_; }
	void SetSpacing(float s) override { spacing_ = s; }

	bool Contains(float x, float y) override {
		float xFac = 0.0f;
		float wFac = 0.0f;
		float yFac = 0.0f;
		float hFac = 0.0f;

		// Many cases sadly...
		if (circleVisible_ && !squareVisible_) {
			if (crossVisible_ || triangleVisible_) {
				xFac = 1.0f;
				wFac = -1.0f;
			} else {
				xFac = 2.0f;
				wFac = -2.0f;
				yFac = 1.0f;
				hFac = -2.0f;
			}
		} else if (!circleVisible_ && squareVisible_) {
			if (crossVisible_ || triangleVisible_) {
				wFac = -1.0f;
			} else {
				wFac = -2.0f;
				yFac = 1.0f;
				hFac = -2.0f;
			}
		} else if (circleVisible_ && squareVisible_ && !crossVisible_ && !triangleVisible_) {
			yFac = 1.0f;
			hFac = -2.0f;
		}

		// No else here is intentional
		if (crossVisible_ && !triangleVisible_) {
			if (circleVisible_ || squareVisible_) {
				yFac = 1.0f;
				hFac = -1.0f;
			} else {
				yFac = 2.0f;
				hFac = -2.0f;
				xFac = 1.0f;
				wFac = -2.0f;
			}
		} else if (!crossVisible_ && triangleVisible_) {
			if (circleVisible_ || squareVisible_) {
				hFac = -1.0f;
			} else {
				hFac = -2.0f;
				xFac = 1.0f;
				wFac = -2.0f;
			}
		} else if (!circleVisible_ && !squareVisible_ && crossVisible_ && triangleVisible_) {
			xFac = 1.0f;
			wFac = -2.0f;
		}

		const float thresholdFactor = 0.25f;
		const float thresholdW = thresholdFactor * bounds_.w;
		const float thresholdH = thresholdFactor * bounds_.h;

		float tolerantX = bounds_.x - thresholdW*0.5 + xFac*baseActionButtonSpacing*spacing_;
		float tolerantY = bounds_.y - thresholdH*0.5 + yFac*baseActionButtonSpacing*spacing_;
		float tolerantW = bounds_.w + thresholdW + wFac*baseActionButtonSpacing*spacing_;
		float tolerantH = bounds_.h + thresholdH + hFac*baseActionButtonSpacing*spacing_;

		Bounds tolerantBounds(tolerantX, tolerantY, tolerantW, tolerantH);
		return tolerantBounds.Contains(x, y);
	}

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
		scale_ = theScale_*layoutAreaScale;
		float opacity = GamepadGetOpacity();
		uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
		uint32_t color = colorAlpha(0xFFFFFF, opacity);

		static const float xoff[4] = {1, 0, -1, 0};
		static const float yoff[4] = {0, 1, 0, -1};

		ImageID dirImage = g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR");

		for (int i = 0; i < 4; i++) {
			float r = D_pad_Radius * spacing_ * layoutAreaScale;
			float x = bounds_.centerX() + xoff[i] * r;
			float y = bounds_.centerY() + yoff[i] * r;
			float x2 = bounds_.centerX() + xoff[i] * (r + 10.f * scale_);
			float y2 = bounds_.centerY() + yoff[i] * (r + 10.f * scale_);
			float angle = i * M_PI / 2;

			dc.Draw()->DrawImageRotated(dirImage, x, y, scale_, angle + PI, colorBg, false);
			dc.Draw()->DrawImageRotated(ImageID("I_ARROW"), x2, y2, scale_, angle + PI, color);
		}
		scale_ = theScale_/layoutAreaScale;
	}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(ImageID("I_DIR"));
		w = 2.0f * D_pad_Radius * spacing_ + image->w * scale_;
		h = w;
	};

	float GetSpacing() const override { return spacing_; }
	void SetSpacing(float s) override { spacing_ = s; }

private:
	float &spacing_;
};

class PSPStickDragDrop : public DragDropButton {
public:
	PSPStickDragDrop(ConfigTouchPos &pos, const char *key, ImageID bgImg, ImageID img, const Bounds &screenBounds, float &spacing)
		: DragDropButton(pos, key, bgImg, img, screenBounds), spacing_(spacing) {
	}

	void Draw(UIContext &dc) override {
		float opacity = GamepadGetOpacity();
		uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
		uint32_t downBg = colorAlpha(0x00FFFFFF, opacity * 0.5f);

		const ImageID stickImage = g_Config.iTouchButtonStyle ? ImageID("I_STICK_LINE") : ImageID("I_STICK");
		const ImageID stickBg = g_Config.iTouchButtonStyle ? ImageID("I_STICK_BG_LINE") : ImageID("I_STICK_BG");

		dc.Draw()->DrawImage(stickBg, bounds_.centerX(), bounds_.centerY(), scale_, colorBg, ALIGN_CENTER);
		dc.Draw()->DrawImage(stickImage, bounds_.centerX(), bounds_.centerY(), scale_ * spacing_, colorBg, ALIGN_CENTER);
	}

	float GetSpacing() const override { return spacing_ * 3; }
	void SetSpacing(float s) override {
		// In mapping spacing is clamped between 0.5 and 3.0 and passed to this method
		spacing_ = s/3;
	}

private:
	float &spacing_;
};

class SnapGrid : public UI::View {
public:
	SnapGrid(int leftMargin, int rightMargin, int topMargin, int bottomMargin, u32 color)
		: UI::View(), x1(leftMargin), x2(rightMargin), y1(topMargin), y2(bottomMargin), col(color) {}

	void Draw(UIContext &dc) override {
		if (g_Config.bTouchSnapToGrid) {
			dc.Flush();
			dc.BeginNoTex();
			float xOffset = bounds_.x;
			float yOffset = bounds_.y;

			dc.Draw()->Rect((x1+x2)/2 + xOffset - g_display.pixel_in_dps_x, y1 + yOffset, 3.0f * g_display.pixel_in_dps_x, y2-y1, col);
			dc.Draw()->Rect(x1 + xOffset, (y1+y2)/2 + yOffset - g_display.pixel_in_dps_y, x2-x1, 3.0f * g_display.pixel_in_dps_y, col);

			for (int x = x1 + (x1+x2)/2 % g_Config.iTouchSnapGridSize; x < x2; x += g_Config.iTouchSnapGridSize)
				dc.Draw()->vLine(x + xOffset, y1 + yOffset, y2 + yOffset, col);
			for (int y = y1 + (y1+y2)/2 % g_Config.iTouchSnapGridSize; y < y2; y += g_Config.iTouchSnapGridSize)
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
		SetClip(true);
	}

	bool Touch(const TouchInput &input) override;
	void CreateViews();
	void Draw(UIContext& ui) override;
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

bool ControlLayoutView::Touch(const TouchInput &touch) {
	using namespace UI;

	if ((touch.flags & TOUCH_MOVE) && pickedControl_ != nullptr) {
		if (mode_ == 0) {
			const Bounds &controlBounds = pickedControl_->GetBounds();

			// Allow placing the control halfway outside the play area.
			Bounds validRange = this->GetBounds();
			// Control coordinates are relative inside the bounds.
			validRange.x = 0.0f;
			validRange.y = 0.0f;

			// This make cure the controll is all inside the screen (commended out only half)
			//validRange.x += controlBounds.w * 0.5f;
			//validRange.w -= controlBounds.w;
			//validRange.y += controlBounds.h * 0.5f;
			//validRange.h -= controlBounds.h;

			Point newPos;
			newPos.x = startObjectX_ + (touch.x - startDragX_);
			newPos.y = startObjectY_ + (touch.y - startDragY_);
			if (g_Config.bTouchSnapToGrid) {
				newPos.x -= fmod(newPos.x - validRange.w/2, g_Config.iTouchSnapGridSize);
				newPos.y -= fmod(newPos.y - validRange.h/2, g_Config.iTouchSnapGridSize);
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
	return true;
}

void ControlLayoutView::Draw(UIContext& dc) {
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;
	GamepadUpdateOpacity(std::max(0.5f, opacity));
	using namespace UI;
	dc.FillRect(Drawable(0x80000000), bounds_);
	dc.Flush();
	UI::AnchorLayout::Draw(dc);
}

void ControlLayoutView::CreateViews() {
	using namespace CustomKeyData;
	const Bounds &bounds = GetBounds();
	if (bounds.w == 0.0f || bounds.h == 0.0f) {
		// Layout hasn't happened yet, return.
		// See comment in TouchControlLayoutScreen::update().
		return;
	}

	// Create all the views.

	if (g_Config.bShowTouchCircle || g_Config.bShowTouchCross || g_Config.bShowTouchTriangle || g_Config.bShowTouchSquare) {
		PSPActionButtons *actionButtons = new PSPActionButtons(g_Config.touchActionButtonCenter, "Action buttons", g_Config.fActionButtonSpacing, bounds);
		actionButtons->setCircleVisibility(g_Config.bShowTouchCircle);
		actionButtons->setCrossVisibility(g_Config.bShowTouchCross);
		actionButtons->setTriangleVisibility(g_Config.bShowTouchTriangle);
		actionButtons->setSquareVisibility(g_Config.bShowTouchSquare);
		controls_.push_back(actionButtons);
	}

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

	if (auto *fastForward = addDragDropButton(g_Config.touchFastForwardKey, "Fast-forward button", rectImage, ImageID("I_ARROW"))) {
		fastForward->SetAngle(180.0f);
	}
	addDragDropButton(g_Config.touchLKey, "Left shoulder button", shoulderImage, ImageID("I_L"));
	if (auto *rbutton = addDragDropButton(g_Config.touchRKey, "Right shoulder button", shoulderImage, ImageID("I_R"))) {
		rbutton->FlipImageH(true);
	}

	if (g_Config.touchAnalogStick.show) {
		controls_.push_back(new PSPStickDragDrop(g_Config.touchAnalogStick, "Left analog stick", stickBg, stickImage, bounds, g_Config.fLeftStickHeadScale));
	}
	if (g_Config.touchRightAnalogStick.show) {
		controls_.push_back(new PSPStickDragDrop(g_Config.touchRightAnalogStick, "Right analog stick", stickBg, stickImage, bounds, g_Config.fRightStickHeadScale));
	}

	auto addDragCustomKey = [&](ConfigTouchPos &pos, const char *key, const ConfigCustomButton& cfg) {
		DragDropButton *b = nullptr;
		if (pos.show) {
			b = new DragDropButton(pos, key, g_Config.iTouchButtonStyle == 0 ? customKeyShapes[cfg.shape].i : customKeyShapes[cfg.shape].l, customKeyImages[cfg.image].i, bounds);
			b->FlipImageH(customKeyShapes[cfg.shape].f);
			b->SetAngle(customKeyImages[cfg.image].r, customKeyShapes[cfg.shape].r);
			controls_.push_back(b);
		}
		return b;
	};

	for (int i = 0; i < Config::CUSTOM_BUTTON_COUNT; i++) {
		char temp[64];
		snprintf(temp, sizeof(temp), "Custom %d button", i);
		addDragCustomKey(g_Config.touchCustom[i], temp, g_Config.CustomButton[i]);
	}

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

	DragDropButton *bestMatch = nullptr;
	float bestDistance;
	for (size_t i = 0; i < controls_.size(); i++) {
		DragDropButton *control = controls_[i];
		if (control->Contains(x, y)) {
			const Bounds &bounds = control->GetBounds();
			float distance = (bounds.centerX()-x)*(bounds.centerX()-x)+(bounds.centerY()-y)*(bounds.centerY()-y);
			if (!bestMatch || distance < bestDistance) {
				bestDistance = distance;
				bestMatch = control;
			}
		}
	}

	return bestMatch;
}

void TouchControlLayoutScreen::resized() {
	RecreateViews();
}

void TouchControlLayoutScreen::onFinish(DialogResult reason) {
	g_Config.Save("TouchControlLayoutScreen::onFinish");
}

UI::EventReturn TouchControlLayoutScreen::OnVisibility(UI::EventParams &e) {
	screenManager()->push(new TouchControlVisibilityScreen(gamePath_));
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
	UIDialogScreenWithGameBackground::update();

	// TODO: We really, really need a cleaner solution for creating sub-views
	// of custom compound controls.
	if (layoutView_) {
		if (!layoutView_->HasCreatedViews()) {
			layoutView_->CreateViews();
		}
	}
}

void TouchControlLayoutScreen::CreateViews() {
	using namespace UI;

	// setup g_Config for button layout
	const Bounds &bounds = screenManager()->getUIContext()->GetBounds();
	InitPadLayout(bounds.w, bounds.h);

	const float leftColumnWidth = 200.0f;
	layoutAreaScale = 1.0f - (leftColumnWidth + 10.0f) / std::max(bounds.w, 1.0f);

	auto co = GetI18NCategory(I18NCat::CONTROLS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	auto rootLayout = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	rootLayout->SetSpacing(0.0f);
	root_ = rootLayout;

	ScrollView *leftColumnScroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(leftColumnWidth, FILL_PARENT)));
	leftColumnScroll->SetAlignOpposite(true);
	LinearLayout *leftColumn = leftColumnScroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(Margins(12.0f, 0.0f))));

	mode_ = new ChoiceStrip(ORIENT_VERTICAL);
	mode_->AddChoice(di->T("Move"));
	mode_->AddChoice(di->T("Resize"));
	mode_->SetSelection(0, false);
	mode_->OnChoice.Handle(this, &TouchControlLayoutScreen::OnMode);

	CheckBox *snap = new CheckBox(&g_Config.bTouchSnapToGrid, di->T("Snap"));
	PopupSliderChoice *gridSize = new PopupSliderChoice(&g_Config.iTouchSnapGridSize, 2, 256, 64, di->T("Grid"), screenManager(), "");
	gridSize->SetEnabledPtr(&g_Config.bTouchSnapToGrid);

	leftColumn->Add(mode_);
	leftColumn->Add(new Choice(co->T("Customize")))->OnClick.Handle(this, &TouchControlLayoutScreen::OnVisibility);
	leftColumn->Add(snap);
	leftColumn->Add(gridSize);
	leftColumn->Add(new Choice(di->T("Reset")))->OnClick.Handle(this, &TouchControlLayoutScreen::OnReset);
	leftColumn->Add(new Spacer(12.0f));
	leftColumn->Add(new Choice(di->T("Back"), "", false))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	leftColumn->Add(new Spacer(0.0f));

	LinearLayout* rightColumn = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, Margins(0.0f, 12.0f, 12.0f, 12.0f))));
	rightColumn->Add(new Spacer(new LinearLayoutParams(1.0)));
	float previewHeight = bounds.h * layoutAreaScale;
	layoutView_ = rightColumn->Add(new ControlLayoutView(new LinearLayoutParams(FILL_PARENT, previewHeight)));
}

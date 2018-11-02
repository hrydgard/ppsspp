// Copyright (c) 2012- PPSSPP Project.

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

#include "input/input_state.h"
#include "gfx_es2/draw_buffer.h"

#include "ui/view.h"
#include "ui/viewgroup.h"
#include "Core/CoreParameter.h"

class GamepadView : public UI::View {
public:
	GamepadView(UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	bool Key(const KeyInput &input) override {
		return false;
	}
	void Update() override;

protected:
	virtual float GetButtonOpacity();

	float lastFrameTime_;
	float secondsWithoutTouch_;
};

class MultiTouchButton : public GamepadView {
public:
	MultiTouchButton(int bgImg, int bgDownImg, int img, float scale, UI::LayoutParams *layoutParams)
		: GamepadView(layoutParams), scale_(scale), bgImg_(bgImg), bgDownImg_(bgDownImg), img_(img) {
	}

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	virtual bool IsDown() { return pointerDownMask_ != 0; }
	// chainable
	MultiTouchButton *FlipImageH(bool flip) { flipImageH_ = flip; return this; }
	MultiTouchButton *SetAngle(float angle) { angle_ = angle; bgAngle_ = angle; return this; }
	MultiTouchButton *SetAngle(float angle, float bgAngle) { angle_ = angle; bgAngle_ = bgAngle; return this; }

protected:
	uint32_t pointerDownMask_ = 0;
	float scale_;

private:
	int bgImg_;
	int bgDownImg_;
	int img_;
	float bgAngle_ = 0.0f;
	float angle_ = 0.0f;
	bool flipImageH_ = false;
};

class BoolButton : public MultiTouchButton {
public:
	BoolButton(bool *value, int bgImg, int bgDownImg, int img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), value_(value) {

	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override { return *value_; }

	UI::Event OnChange;

private:
	bool *value_;
};

class FPSLimitButton : public MultiTouchButton {
public:
	FPSLimitButton(FPSLimit limit, int bgImg, int bgDownImg, int img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), limit_(limit) {

	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override;

private:
	FPSLimit limit_;
};

class PSPButton : public MultiTouchButton {
public:
	PSPButton(int pspButtonBit, int bgImg, int bgDownImg, int img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit) {
	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override;

private:
	int pspButtonBit_;
};

class PSPDpad : public GamepadView {
public:
	PSPDpad(int arrowIndex, int arrowDownIndex, int overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	void ProcessTouch(float x, float y, bool down);
	int arrowIndex_;
	int arrowDownIndex_;
	int overlayIndex_;

	float scale_;
	float spacing_;

	int dragPointerId_;
	int down_;
};

class PSPStick : public GamepadView {
public:
	PSPStick(int bgImg, int stickImg, int stickDownImg, int stick, float scale, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	void ProcessTouch(float x, float y, bool down);

	int dragPointerId_;
	int bgImg_;
	int stickImageIndex_;
	int stickDownImg_;
	int stick_;
	float stick_size_;
	float scale_;

	float centerX_;
	float centerY_;
};

//initializes the layout from Config. if a default layout does not exist,
//it sets up default values
void InitPadLayout(float xres, float yres, float globalScale = 1.15f);
UI::ViewGroup *CreatePadLayout(float xres, float yres, bool *pause);

const int D_pad_Radius = 50;
const int baseActionButtonSpacing = 60;

class ComboKey : public MultiTouchButton {
public:
	ComboKey(int pspButtonBit, int bgImg, int bgDownImg, int img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, bgDownImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit)  {
	}
	void Touch(const TouchInput &input) override;
private:
	int pspButtonBit_;
};

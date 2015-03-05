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

class MultiTouchButton : public UI::View {
public:
	MultiTouchButton(int bgImg, int img, float scale, UI::LayoutParams *layoutParams)
		: UI::View(layoutParams), pointerDownMask_(0), scale_(scale), bgImg_(bgImg), img_(img), angle_(0.0f), flipImageH_(false) {
	}

	virtual bool Key(const KeyInput &input) { return false; }
	virtual void Update(const InputState &input) {}
	virtual void Touch(const TouchInput &input);
	virtual void Draw(UIContext &dc);
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const;
	virtual bool IsDown() { return pointerDownMask_ != 0; }
	// chainable
	MultiTouchButton *FlipImageH(bool flip) { flipImageH_ = flip; return this; }
	MultiTouchButton *SetAngle(float angle) { angle_ = angle; return this; }

protected:
	uint32_t pointerDownMask_;
	float scale_;

private:
	int bgImg_;
	int img_;
	float angle_;
	bool flipImageH_;
};

class BoolButton : public MultiTouchButton {
public:
	BoolButton(bool *value, int bgImg, int img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, img, scale, layoutParams), value_(value) {

	}
	virtual void Touch(const TouchInput &input) override;
	virtual bool IsDown() { return *value_; }

private:
	bool *value_;
};

class PSPButton : public MultiTouchButton {
public:
	PSPButton(int pspButtonBit, int bgImg, int img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit) {
	}
	void Touch(const TouchInput &input) override;
	virtual bool IsDown();

private:
	int pspButtonBit_;
};

class PSPDpad : public UI::View {
public:
	PSPDpad(int arrowIndex, int overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams);

	bool Key(const KeyInput &input) override { return false; }
	void Update(const InputState &input) override {}
	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	void ProcessTouch(float x, float y, bool down);
	int arrowIndex_;
	int overlayIndex_;

	float scale_;
	float spacing_;

	int dragPointerId_;
	int down_;
};

class PSPStick : public UI::View {
public:
	PSPStick(int bgImg, int stickImg, int stick, float scale, UI::LayoutParams *layoutParams);

	bool Key(const KeyInput &input) override { return false; }
	void Update(const InputState &input) override {}
	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	void ProcessTouch(float x, float y, bool down);

	int dragPointerId_;
	int bgImg_;
	int stickImageIndex_;
	int stick_;
	float stick_size_;
	float scale_;
	bool dragging_[MAX_POINTERS];
	bool lastPointerDown_[MAX_POINTERS];

	float centerX_;
	float centerY_;
};

//initializes the layout from Config. if a default layout does not exist,
//it sets up default values
void InitPadLayout(float xres, float yres, float globalScale = 1.15f);
UI::ViewGroup *CreatePadLayout(float xres, float yres, bool *pause);

const int D_pad_Radius = 50;
const int baseActionButtonSpacing = 60;


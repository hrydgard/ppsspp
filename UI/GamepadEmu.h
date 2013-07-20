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
		: UI::View(layoutParams), pointerDownMask_(0), bgImg_(bgImg), img_(img), scale_(scale), flipImageH_(false) {
	}

	virtual void Key(const KeyInput &input) {}
	virtual void Update(const InputState &input) {}
	virtual void Touch(const TouchInput &input);
	virtual void Draw(UIContext &dc);
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const;
	virtual bool IsDown() { return pointerDownMask_ != NULL; }
	void FlipImageH(bool flip) { flipImageH_ = flip; }

protected:
	uint32_t pointerDownMask_;

private:
	int bgImg_;
	int img_;
	float scale_;
	bool flipImageH_;
};

class PSPButton : public MultiTouchButton {
public:
	PSPButton(int pspButtonBit, int bgImg, int img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(bgImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit) {
		
	}
	virtual void Touch(const TouchInput &input);
	virtual bool IsDown();

private:
	int pspButtonBit_;
};

class PSPCross : public UI::View {
public:
	PSPCross(int arrowIndex, int overlayIndex, float scale, float radius, UI::LayoutParams *layoutParams);

	virtual void Key(const KeyInput &input) {}
	virtual void Update(const InputState &input) {}
	virtual void Touch(const TouchInput &input);
	virtual void Draw(UIContext &dc);
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const;

private:
	void ProcessTouch(float x, float y, bool down);
	float radius_;
	float scale_;

	int arrowIndex_;
	int overlayIndex_;

	int dragPointerId_;
	int down_;
};

class PSPStick : public UI::View {
public:
	virtual void Touch(const TouchInput &input);
	virtual void Draw(UIContext &dc);
	virtual void GetContentDimensions(const UIContext &dc, float &w, float &h) const;

private:
	int bgImageIndex_;
	int stickImageIndex_;
	int stick_;
	int stick_size_;
	float stick_x_;
	float stick_y_;
	float scale_;
	bool dragging_[MAX_POINTERS];
	bool lastPointerDown_[MAX_POINTERS];

	// maintained for drawing only
	float stick_delta_x_;
	float stick_delta_y_;
};


void LayoutGamepad(int w, int h);
void UpdateGamepad(InputState &input_state);
void DrawGamepad(DrawBuffer &db, float opacity);

UI::ViewGroup *CreatePadLayout();

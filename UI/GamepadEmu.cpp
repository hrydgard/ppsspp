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

#include "GamepadEmu.h"
#include "base/colorutil.h"
#include "ui/virtual_input.h"
#include "ui/ui_context.h"
#include "Core/Config.h"
#include "ui_atlas.h"
#include "Core/HLE/sceCtrl.h"

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
#define USE_PAUSE_BUTTON 1
#else
#define USE_PAUSE_BUTTON 0
#endif

void MultiTouchButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &image = dc.Draw()->GetAtlas()->images[bgImg_];
	w = image.w * scale_;
	h = image.h * scale_;
}

void MultiTouchButton::Touch(const TouchInput &input) {
	if ((input.flags & TOUCH_DOWN) && bounds_.Contains(input.x, input.y)) {
		pointerDownMask_ |= 1 << input.id;
	}
	if (input.flags & TOUCH_MOVE) {
		if (bounds_.Contains(input.x, input.y))
			pointerDownMask_ |= 1 << input.id;
		else
			pointerDownMask_ &= ~(1 << input.id);
	}
	if (input.flags & TOUCH_UP) {
		pointerDownMask_ &= ~(1 << input.id);
	}
}

void MultiTouchButton::Draw(UIContext &dc) {
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;
	
	float scale = scale_;
	if (IsDown()) {
		scale *= 2.0f;
		opacity = 100.0f;
	}
	uint32_t colorBg = colorAlpha(0xc0b080, opacity);
	uint32_t color = colorAlpha(0xFFFFFF, opacity);

	dc.Draw()->DrawImageRotated(bgImg_, bounds_.centerX(), bounds_.centerY(), scale, 0.0f, colorBg, flipImageH_);
	dc.Draw()->DrawImageRotated(img_, bounds_.centerX(), bounds_.centerY(), scale, 0.0f, color, flipImageH_);
}

void PSPButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	if (down && !lastDown) {
		__CtrlButtonDown(pspButtonBit_);
	} else if (lastDown && !down) {
		__CtrlButtonUp(pspButtonBit_);
	}
}

bool PSPButton::IsDown() {
	return __CtrlPeekButtons() & pspButtonBit_;
}

UI::ViewGroup *CreatePadLayout() {
	using namespace UI;

	AnchorLayout *root = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	// TODO: See if we can make some kind of global scaling for views instead of this hackery.
	float scale = g_Config.fButtonScale;

	// const int button_spacing = 50 * controlScale;
	const int button_spacing = 50 * scale;
	const int arrow_spacing = 40 * scale;

	const int circleX = 40 * scale;
	const int circleY = 120 * scale;

	const int startX = 170 * scale;
	const int leftX = 40 * scale;
	const int bottomStride = 120 * scale;

	root->Add(new PSPButton(CTRL_CIRCLE, I_ROUND, I_CIRCLE, scale, new AnchorLayoutParams(NONE, NONE, circleX, circleY, true)));
	root->Add(new PSPButton(CTRL_CROSS, I_ROUND, I_CROSS, scale, new AnchorLayoutParams(NONE, NONE, circleX + button_spacing, circleY - button_spacing, true)));
	root->Add(new PSPButton(CTRL_TRIANGLE, I_ROUND, I_TRIANGLE, scale, new AnchorLayoutParams(NONE, NONE, circleX + button_spacing, circleY + button_spacing, true)));
	root->Add(new PSPButton(CTRL_SQUARE, I_ROUND, I_SQUARE, scale, new AnchorLayoutParams(NONE, NONE, circleX + button_spacing * 2, circleY, true)));
	
	root->Add(new PSPButton(CTRL_START, I_RECT, I_START, scale, new AnchorLayoutParams(NONE, NONE, startX, 30, true)));
	root->Add(new PSPButton(CTRL_SELECT, I_RECT, I_SELECT, scale, new AnchorLayoutParams(NONE, NONE, startX + bottomStride, 30, true)));

	root->Add(new PSPButton(CTRL_LTRIGGER, I_RECT, I_L, scale, new AnchorLayoutParams(10, 10, NONE, NONE, false)));
	root->Add(new PSPButton(CTRL_RTRIGGER, I_RECT, I_R, scale, new AnchorLayoutParams(NONE, 10, 10, NONE, false)))->FlipImageH(true);

	return root;
}

TouchButton buttonTurbo(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_UNTHROTTLE, 180);
TouchCrossPad crossPad(&ui_atlas, I_DIR, I_ARROW);
#if USE_PAUSE_BUTTON
TouchButton buttonPause(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_BACK, 90);
#endif

TouchStick leftStick(&ui_atlas, I_STICKBG, I_STICK, 0);

void LayoutGamepad(int w, int h)
{
	float controlScale = g_Config.bLargeControls ? g_Config.fButtonScale : 1.15;

	const int button_spacing = 50 * controlScale;
	const int arrow_spacing = 40 * controlScale;

	const int circleX = w - 40 * controlScale;
	const int circleY = h - 120 * controlScale;

	const int leftX = 40 * controlScale;
	int leftY = h - 120 * controlScale;

	if (g_Config.bShowAnalogStick) {
		leftY = h - 250 * controlScale;
	}

	const int stickX = leftX + arrow_spacing;
	const int stickY = h - 80 * controlScale;

	const int halfW = w / 2;

	crossPad.setPos(leftX + arrow_spacing, leftY, 40, controlScale);

	//if (g_Config.iFpsLimit)
	//	buttonVPS.setPos(halfW - button_spacing * 2, h - 20 * controlScale, controlScale);
	//else
		buttonTurbo.setPos(halfW - button_spacing * 2, h - 20 * controlScale, controlScale);

#if USE_PAUSE_BUTTON
	buttonPause.setPos(halfW, 15 * controlScale, controlScale);
#endif

	leftStick.setPos(stickX, stickY, controlScale);
}

void UpdateGamepad(InputState &input_state) {
	LayoutGamepad(dp_xres, dp_yres);

	crossPad.update(input_state);

	//if (g_Config.iFpsLimit)
	//	buttonVPS.update(input_state);
	//else 
		buttonTurbo.update(input_state);

#if USE_PAUSE_BUTTON
	buttonPause.update(input_state);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.update(input_state);
}

void DrawGamepad(DrawBuffer &db, float opacity) {
	uint32_t color = colorAlpha(0xc0b080, opacity);
	uint32_t colorOverlay = colorAlpha(0xFFFFFF, opacity);

	crossPad.draw(db, color, colorOverlay);

	//if (g_Config.iFpsLimit)
	//	buttonVPS.draw(db, color, colorOverlay);
	//else
		buttonTurbo.draw(db, color, colorOverlay);

#if USE_PAUSE_BUTTON
	buttonPause.draw(db, color, colorOverlay);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.draw(db, color);
}


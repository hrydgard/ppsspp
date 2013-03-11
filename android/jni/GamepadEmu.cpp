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
#include "ui/virtual_input.h"
#include "../../Core/Config.h"
#include "ui_atlas.h"

TouchButton buttonX(&ui_atlas, I_ROUND, I_CROSS, PAD_BUTTON_A);
TouchButton buttonO(&ui_atlas, I_ROUND, I_CIRCLE, PAD_BUTTON_B);
TouchButton buttonSq(&ui_atlas, I_ROUND, I_SQUARE, PAD_BUTTON_X);
TouchButton buttonTri(&ui_atlas, I_ROUND, I_TRIANGLE, PAD_BUTTON_Y);
TouchButton buttonSelect(&ui_atlas, I_RECT, I_SELECT, PAD_BUTTON_SELECT);
TouchButton buttonStart(&ui_atlas, I_RECT, I_START, PAD_BUTTON_START);
TouchButton buttonLShoulder(&ui_atlas, I_SHOULDER, I_L, PAD_BUTTON_LBUMPER);
TouchButton buttonRShoulder(&ui_atlas, I_SHOULDER, I_R, PAD_BUTTON_RBUMPER, 0, true);
TouchButton buttonLeft(&ui_atlas, I_DIR, I_ARROW, PAD_BUTTON_LEFT, 0);
TouchButton buttonUp(&ui_atlas, I_DIR, I_ARROW, PAD_BUTTON_UP, 90);
TouchButton buttonRight(&ui_atlas, I_DIR, I_ARROW, PAD_BUTTON_RIGHT, 180);
TouchButton buttonDown(&ui_atlas, I_DIR, I_ARROW, PAD_BUTTON_DOWN, 270);
#if defined(__SYMBIAN32__) || defined(IOS)
TouchButton buttonPause(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_BACK, 90);
#endif

TouchStick leftStick(&ui_atlas, I_STICKBG, I_STICK, 0);

void LayoutGamepad(int w, int h)
{
	float controlScale = g_Config.bLargeControls ? 1.6 : 1.15;

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

	buttonO.setPos(circleX, circleY, controlScale);
	buttonX.setPos(circleX - button_spacing, circleY + button_spacing, controlScale);
	buttonTri.setPos(circleX - button_spacing, circleY - button_spacing, controlScale);
	buttonSq.setPos(circleX - button_spacing * 2, circleY, controlScale);

	buttonLeft.setPos(leftX, leftY, controlScale);
	buttonUp.setPos(leftX + arrow_spacing, leftY - arrow_spacing, controlScale);
	buttonDown.setPos(leftX + arrow_spacing, leftY + arrow_spacing, controlScale);
	buttonRight.setPos(leftX + arrow_spacing * 2, leftY, controlScale);

	buttonSelect.setPos(halfW - button_spacing, h - 20 * controlScale, controlScale);
	buttonStart.setPos(halfW + button_spacing, h - 20 * controlScale, controlScale);
	buttonLShoulder.setPos(button_spacing + 10 * controlScale, 15 * controlScale, controlScale);
	buttonRShoulder.setPos(w - button_spacing - 10 * controlScale, 15 * controlScale, controlScale);

#if defined(__SYMBIAN32__) || defined(IOS)
	buttonPause.setPos(halfW, 15 * controlScale, controlScale);
#endif

	leftStick.setPos(stickX, stickY, controlScale);
}

void UpdateGamepad(InputState &input_state)
{
	LayoutGamepad(dp_xres, dp_yres);

	buttonO.update(input_state);
	buttonX.update(input_state);
	buttonTri.update(input_state);
	buttonSq.update(input_state);

	buttonLeft.update(input_state);
	buttonUp.update(input_state);
	buttonDown.update(input_state);
	buttonRight.update(input_state);

	buttonSelect.update(input_state);
	buttonStart.update(input_state);
	buttonLShoulder.update(input_state);
	buttonRShoulder.update(input_state);

#if defined(__SYMBIAN32__) || defined(IOS)
	buttonPause.update(input_state);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.update(input_state);
}

void DrawGamepad(DrawBuffer &db)
{
	uint32_t color = 0xa0c0b080;
	uint32_t colorOverlay = 0xa0FFFFFF;
	buttonO.draw(db, color, colorOverlay);
	buttonX.draw(db, color, colorOverlay);
	buttonTri.draw(db, color, colorOverlay);
	buttonSq.draw(db, color, colorOverlay);

	buttonLeft.draw(db, color, colorOverlay);
	buttonUp.draw(db, color, colorOverlay);
	buttonDown.draw(db, color, colorOverlay);
	buttonRight.draw(db, color, colorOverlay);

	buttonSelect.draw(db, color, colorOverlay);
	buttonStart.draw(db, color, colorOverlay);
	buttonLShoulder.draw(db, color, colorOverlay);
	buttonRShoulder.draw(db, color, colorOverlay);

#if defined(__SYMBIAN32__) || defined(IOS)
	buttonPause.draw(db, color, colorOverlay);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.draw(db, color);
}


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
#include "Core/Config.h"
#include "ui_atlas.h"

TouchButton buttonX(&ui_atlas, I_ROUND, I_CROSS, PAD_BUTTON_A);
TouchButton buttonO(&ui_atlas, I_ROUND, I_CIRCLE, PAD_BUTTON_B);
TouchButton buttonSq(&ui_atlas, I_ROUND, I_SQUARE, PAD_BUTTON_X);
TouchButton buttonTri(&ui_atlas, I_ROUND, I_TRIANGLE, PAD_BUTTON_Y);
TouchButton buttonSelect(&ui_atlas, I_RECT, I_SELECT, PAD_BUTTON_SELECT);
TouchButton buttonStart(&ui_atlas, I_RECT, I_START, PAD_BUTTON_START);
TouchButton buttonLShoulder(&ui_atlas, I_SHOULDER, I_L, PAD_BUTTON_LBUMPER);
TouchButton buttonRShoulder(&ui_atlas, I_SHOULDER, I_R, PAD_BUTTON_RBUMPER, 0, true);
TouchButton buttonTurbo(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_UNTHROTTLE, 180);
TouchButton buttonVPS(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_LEFT_THUMB, 180);
TouchCrossPad crossPad(&ui_atlas, I_DIR, I_ARROW);
TouchStick leftStick(&ui_atlas, I_STICKBG, I_STICK, 0);

TouchButton buttonTriFlip(&ui_atlas, I_ROUND, I_TRIANGLE, PAD_BUTTON_Y,180);
TouchButton buttonSelectFlip(&ui_atlas, I_RECT, I_SELECT, PAD_BUTTON_SELECT,180);
TouchButton buttonStartFlip(&ui_atlas, I_RECT, I_START, PAD_BUTTON_START,180);
TouchButton buttonLShoulderFlip(&ui_atlas, I_SHOULDER, I_L, PAD_BUTTON_LBUMPER, 180);
TouchButton buttonRShoulderFlip(&ui_atlas, I_SHOULDER, I_R, PAD_BUTTON_RBUMPER, 180, true);
TouchButton buttonTurboFlip(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_UNTHROTTLE, 180);
TouchButton buttonVPSFlip(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_LEFT_THUMB, 180);

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
TouchButton buttonPause(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_BACK, 90);
TouchButton buttonPauseFlip(&ui_atlas, I_RECT, I_ARROW, PAD_BUTTON_BACK, 270);
#endif

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

	buttonO.setPos(circleX, circleY, controlScale);
	buttonX.setPos(circleX - button_spacing, circleY + button_spacing, controlScale);
	buttonTri.setPos(circleX - button_spacing, circleY - button_spacing, controlScale);
	buttonSq.setPos(circleX - button_spacing * 2, circleY, controlScale);

	crossPad.setPos(leftX + arrow_spacing, leftY, 40, controlScale);

	if (g_Config.iFpsLimit)
		buttonVPS.setPos(halfW - button_spacing * 2, h - 20 * controlScale, controlScale);
	else
		buttonTurbo.setPos(halfW - button_spacing * 2, h - 20 * controlScale, controlScale);
	buttonSelect.setPos(halfW , h - 20 * controlScale, controlScale);
	buttonStart.setPos(halfW + button_spacing * 2 , h - 20 * controlScale, controlScale);
	buttonLShoulder.setPos(button_spacing + 10 * controlScale, 30 * controlScale, controlScale);
	buttonRShoulder.setPos(w - button_spacing - 10 * controlScale, 30 * controlScale, controlScale);


#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
	buttonPause.setPos(halfW, 15 * controlScale, controlScale);
#endif

	leftStick.setPos(stickX, stickY, controlScale);
}

void LayoutGamepadFlip(int w, int h)
{
	float controlScale = g_Config.bLargeControls ? g_Config.fButtonScale : 1.15;
	
	const int button_spacing = 50 * controlScale;
	const int arrow_spacing = 40 * controlScale;

	const int circleX = 140 * controlScale;
	const int circleY = 140 * controlScale;

	const int leftX = w - 120 * controlScale;
	int leftY = 120 * controlScale;

	if (g_Config.bShowAnalogStick) {
		leftY = h - 250 * controlScale;
	}

	const int stickX = leftX + arrow_spacing;
	const int stickY = h - 80 * controlScale;

	const int halfW = w / 2;

	buttonO.setPos(circleX - button_spacing * 2, circleY , controlScale);
	buttonX.setPos(circleX - button_spacing, circleY - button_spacing, controlScale);
	buttonTriFlip.setPos(circleX - button_spacing, circleY + button_spacing, controlScale);
	buttonSq.setPos(circleX, circleY, controlScale);

	crossPad.setPos(leftX + arrow_spacing, leftY, 40, controlScale);

	if (g_Config.iFpsLimit)
		buttonVPSFlip.setPos(halfW + button_spacing * 2, 20 * controlScale, controlScale);
	else
		buttonTurboFlip.setPos(halfW + button_spacing * 2, 20 * controlScale, controlScale);
	buttonSelectFlip.setPos(halfW ,  20 * controlScale, controlScale);
	buttonStartFlip.setPos(halfW - button_spacing * 2 , 20 * controlScale, controlScale);
	buttonLShoulderFlip.setPos(w - button_spacing - 20 + 10 * controlScale, h - 30 * controlScale, controlScale);
	buttonRShoulderFlip.setPos(button_spacing + 10 * controlScale, h - 30 * controlScale, controlScale);


#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
	buttonPauseFlip.setPos(halfW, h - 15 * controlScale, controlScale);
#endif

	leftStick.setPos(stickX, h - stickY, controlScale);
}


void UpdateGamepad(InputState &input_state)
{
	LayoutGamepad(dp_xres, dp_yres);

	buttonO.update(input_state);
	buttonX.update(input_state);
	buttonTri.update(input_state);
	buttonSq.update(input_state);

	crossPad.update(input_state);

	buttonSelect.update(input_state);
	buttonStart.update(input_state);
	buttonLShoulder.update(input_state);
	buttonRShoulder.update(input_state);

	if (g_Config.iFpsLimit)
		buttonVPS.update(input_state);
	else 
		buttonTurbo.update(input_state);

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
	buttonPause.update(input_state);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.update(input_state);
}

void UpdateGamepadFlip(InputState &input_state)
{
	LayoutGamepadFlip(dp_xres, dp_yres);

	buttonO.update(input_state);
	buttonX.update(input_state);
	buttonTriFlip.update(input_state);
	buttonSq.update(input_state);

	crossPad.update(input_state);

	buttonSelectFlip.update(input_state);
	buttonStartFlip.update(input_state);
	buttonLShoulderFlip.update(input_state);
	buttonRShoulderFlip.update(input_state);

	if (g_Config.iFpsLimit) { 
		buttonVPSFlip.update(input_state);
	} else {
		buttonTurboFlip.update(input_state);
	}

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
	buttonPauseFlip.update(input_state);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.update(input_state);
}

void DrawGamepad(DrawBuffer &db, float opacity)
{
	uint32_t color = colorAlpha(0xc0b080, opacity);
	uint32_t colorOverlay = colorAlpha(0xFFFFFF, opacity);

	buttonO.draw(db, color, colorOverlay);
	buttonX.draw(db, color, colorOverlay);
	buttonTri.draw(db, color, colorOverlay);
	buttonSq.draw(db, color, colorOverlay);

	crossPad.draw(db, color, colorOverlay);

	buttonSelect.draw(db, color, colorOverlay);
	buttonStart.draw(db, color, colorOverlay);
	buttonLShoulder.draw(db, color, colorOverlay);
	buttonRShoulder.draw(db, color, colorOverlay);

	if (g_Config.iFpsLimit)
		buttonVPS.draw(db, color, colorOverlay);
	else
		buttonTurbo.draw(db, color, colorOverlay);

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
	buttonPause.draw(db, color, colorOverlay);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.draw(db, color);
}

void DrawGamepadFlip(DrawBuffer &db, float opacity)
{
	uint32_t color = colorAlpha(0xc0b080, opacity);
	uint32_t colorOverlay = colorAlpha(0xFFFFFF, opacity);

	buttonO.draw(db, color, colorOverlay);
	buttonX.draw(db, color, colorOverlay);
	buttonSq.draw(db, color, colorOverlay);
	buttonTriFlip.draw(db, color, colorOverlay);
	buttonSelectFlip.draw(db, color, colorOverlay);
	buttonStartFlip.draw(db, color, colorOverlay);
	buttonLShoulderFlip.draw(db, color, colorOverlay);
	buttonRShoulderFlip.draw(db, color, colorOverlay);
	crossPad.drawFlip(db, color, colorOverlay);

	if (g_Config.iFpsLimit) {
			buttonVPSFlip.draw(db, color, colorOverlay);
	} else {
			buttonTurboFlip.draw(db, color, colorOverlay);
	}

#if defined(__SYMBIAN32__) || defined(IOS) || defined(MEEGO_EDITION_HARMATTAN)
	buttonPauseFlip.draw(db, color, colorOverlay);
#endif

	if (g_Config.bShowAnalogStick)
		leftStick.draw(db, color);
}


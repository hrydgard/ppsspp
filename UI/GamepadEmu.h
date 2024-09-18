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

#include "Common/Input/InputState.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Core/CoreParameter.h"
#include "Core/HLE/sceCtrl.h"
#include "UI/EmuScreen.h"

class GamepadView : public UI::View {
public:
	GamepadView(const char *key, UI::LayoutParams *layoutParams);

	bool Key(const KeyInput &input) override {
		return false;
	}
	std::string DescribeText() const override;

protected:
	std::string key_;
};

class MultiTouchButton : public GamepadView {
public:
	MultiTouchButton(const char *key, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: GamepadView(key, layoutParams), scale_(scale), bgImg_(bgImg), bgDownImg_(bgDownImg), img_(img) {
	}

	bool Touch(const TouchInput &input) override;
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
	ImageID bgImg_;
	ImageID bgDownImg_;
	ImageID img_;
	float bgAngle_ = 0.0f;
	float angle_ = 0.0f;
	bool flipImageH_ = false;
};

class BoolButton : public MultiTouchButton {
public:
	BoolButton(bool *value, const char *key, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(key, bgImg, bgDownImg, img, scale, layoutParams), value_(value) {

	}
	bool Touch(const TouchInput &input) override;
	bool IsDown() override { return *value_; }

	UI::Event OnChange;

private:
	bool *value_;
};

class PSPButton : public MultiTouchButton {
public:
	PSPButton(int pspButtonBit, const char *key, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(key, bgImg, bgDownImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit) {
	}
	bool Touch(const TouchInput &input) override;
	bool IsDown() override;

private:
	int pspButtonBit_;
};

class PSPDpad : public GamepadView {
public:
	PSPDpad(ImageID arrowIndex, const char *key, ImageID arrowDownIndex, ImageID overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams);

	bool Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

private:
	void ProcessTouch(float x, float y, bool down);
	ImageID arrowIndex_;
	ImageID arrowDownIndex_;
	ImageID overlayIndex_;

	float scale_;
	float spacing_;

	int dragPointerId_;
	int down_;
};

class PSPStick : public GamepadView {
public:
	PSPStick(ImageID bgImg, const char *key, ImageID stickImg, ImageID stickDownImg, int stick, float scale, UI::LayoutParams *layoutParams);

	bool Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;

protected:
	int dragPointerId_;
	ImageID bgImg_;
	ImageID stickImageIndex_;
	ImageID stickDownImg_;

	int stick_;
	float stick_size_;
	float scale_;

	float centerX_;
	float centerY_;

private:
	void ProcessTouch(float x, float y, bool down);
};

class PSPCustomStick : public PSPStick {
public:
	PSPCustomStick(ImageID bgImg, const char *key, ImageID stickImg, ImageID stickDownImg, int stick, float scale, UI::LayoutParams *layoutParams);

	bool Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;

private:
	void ProcessTouch(float x, float y, bool down);

	float posX_ = 0.0f;
	float posY_ = 0.0f;
};

//initializes the layout from Config. if a default layout does not exist,
//it sets up default values
void InitPadLayout(float xres, float yres, float globalScale = 1.15f);
UI::ViewGroup *CreatePadLayout(float xres, float yres, bool *pause, bool showPauseButton, ControlMapper* controllMapper);

const int D_pad_Radius = 50;
const int baseActionButtonSpacing = 60;

class CustomButton : public MultiTouchButton {
public:
	CustomButton(uint64_t pspButtonBit, const char *key, bool toggle, bool repeat, ControlMapper* controllMapper, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, bool invertedContextDimension, UI::LayoutParams *layoutParams)
		: MultiTouchButton(key, bgImg, bgDownImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit), toggle_(toggle), repeat_(repeat), controlMapper_(controllMapper), on_(false), invertedContextDimension_(invertedContextDimension) {
	}
	bool Touch(const TouchInput &input) override;
	void Update() override;
	bool IsDown() override;

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
private:
	uint64_t pspButtonBit_;
	bool toggle_;
	bool repeat_;
	int pressedFrames_ = 0;
	ControlMapper* controlMapper_;
	bool on_;
	bool invertedContextDimension_; // Swap width and height
};

class GestureGamepad : public UI::View {
public:
	GestureGamepad(ControlMapper* controllMapper) : controlMapper_(controllMapper) {};

	bool Touch(const TouchInput &input) override;
	void Update() override;
	void Draw(UIContext &dc) override;

protected:

	float lastX_ = 0.0f;
	float lastY_ = 0.0f;
	float deltaX_ = 0.0f;
	float deltaY_ = 0.0f;
	float downX_;
	float downY_;
	float lastTapRelease_ = 0.0f;
	float lastTouchDown_ = 0.0f;
	int dragPointerId_ = -1;
	bool swipeLeftReleased_ = true;
	bool swipeRightReleased_ = true;
	bool swipeUpReleased_ = true;
	bool swipeDownReleased_ = true;
	bool haveDoubleTapped_ = false;
	ControlMapper* controlMapper_;
};

// Just edit this to add new image, shape or button function
namespace CustomKeyData {
	// Image list
	struct keyImage {
		ImageID i; // ImageID
		float r; // Rotation angle in degree
	};
	static const keyImage customKeyImages[] = {
		{ ImageID("I_1"), 0.0f },
		{ ImageID("I_2"), 0.0f },
		{ ImageID("I_3"), 0.0f },
		{ ImageID("I_4"), 0.0f },
		{ ImageID("I_5"), 0.0f },
		{ ImageID("I_6"), 0.0f },
		{ ImageID("I_A"), 0.0f },
		{ ImageID("I_B"), 0.0f },
		{ ImageID("I_C"), 0.0f },
		{ ImageID("I_D"), 0.0f },
		{ ImageID("I_E"), 0.0f },
		{ ImageID("I_F"), 0.0f },
		{ ImageID("I_CIRCLE"), 0.0f },
		{ ImageID("I_CROSS"), 0.0f },
		{ ImageID("I_SQUARE"), 0.0f },
		{ ImageID("I_TRIANGLE"), 0.0f },
		{ ImageID("I_L"), 0.0f },
		{ ImageID("I_R"),  0.0f },
		{ ImageID("I_START"), 0.0f },
		{ ImageID("I_SELECT"), 0.0f },
		{ ImageID("I_CROSS"), 45.0f },
		{ ImageID("I_SQUARE"), 45.0f },
		{ ImageID("I_TRIANGLE"), 180.0f },
		{ ImageID("I_ARROW"), 90.0f},
		{ ImageID("I_ARROW"), 270.0f},
		{ ImageID("I_ARROW"), 0.0f},
		{ ImageID("I_ARROW"), 180.0f},
		{ ImageID("I_GEAR"), 0.0f},
		{ ImageID("I_ROTATE_LEFT"), 0.0f},
		{ ImageID("I_ROTATE_RIGHT"), 0.0f},
		{ ImageID("I_ARROW_LEFT"), 0.0f},
		{ ImageID("I_ARROW_RIGHT"), 0.0f},
		{ ImageID("I_ARROW_UP"), 0.0f},
		{ ImageID("I_ARROW_DOWN"), 0.0f},
		{ ImageID("I_THREE_DOTS"), 0.0f},
		{ ImageID("I_EMPTY"), 0.0f},
	};

	// Shape list
	struct keyShape {
		ImageID i; // ImageID
		ImageID l; // ImageID line version
		float r; // Rotation angle in dregree
		bool f; // Flip Horizontally
		bool d; // Invert height and width for context dimension (for example for 90 degree rot)
	};
	static const keyShape customKeyShapes[] = {
		{ ImageID("I_ROUND"), ImageID("I_ROUND_LINE"), 0.0f, false, false },
		{ ImageID("I_RECT"), ImageID("I_RECT_LINE"), 0.0f, false, false },
		{ ImageID("I_RECT"), ImageID("I_RECT_LINE"), 90.0f, false, true },
		{ ImageID("I_SHOULDER"), ImageID("I_SHOULDER_LINE"), 0.0f, false, false },
		{ ImageID("I_SHOULDER"), ImageID("I_SHOULDER_LINE"), 0.0f, true, false },
		{ ImageID("I_DIR"), ImageID("I_DIR_LINE"), 270.0f, false, true },
		{ ImageID("I_DIR"), ImageID("I_DIR_LINE"), 90.0f, false, true },
		{ ImageID("I_DIR"), ImageID("I_DIR_LINE"), 180.0f, false, false },
		{ ImageID("I_DIR"), ImageID("I_DIR_LINE"), 0.0f, false, false },
		{ ImageID("I_SQUARE_SHAPE"), ImageID("I_SQUARE_SHAPE_LINE"), 0.0f, false, false },
		{ ImageID("I_EMPTY"), ImageID("I_EMPTY"), 0.0f, false, false },
	};

	// Button list
	struct keyList {
		ImageID i; // UI ImageID
		uint32_t c; // Key code
	};
	static const keyList customKeyList[] = {
		{ ImageID("I_SQUARE"), CTRL_SQUARE },
		{ ImageID("I_TRIANGLE"), CTRL_TRIANGLE },
		{ ImageID("I_CIRCLE"), CTRL_CIRCLE },
		{ ImageID("I_CROSS"), CTRL_CROSS },
		{ ImageID::invalid(), CTRL_UP },
		{ ImageID::invalid(), CTRL_DOWN },
		{ ImageID::invalid(), CTRL_LEFT },
		{ ImageID::invalid(), CTRL_RIGHT },
		{ ImageID("I_START"), CTRL_START },
		{ ImageID("I_SELECT"), CTRL_SELECT },
		{ ImageID("I_L"), CTRL_LTRIGGER },
		{ ImageID("I_R"), CTRL_RTRIGGER },
		{ ImageID::invalid(), VIRTKEY_RAPID_FIRE },
		{ ImageID::invalid(), VIRTKEY_FASTFORWARD },
		{ ImageID::invalid(), VIRTKEY_SPEED_TOGGLE },
		{ ImageID::invalid(), VIRTKEY_REWIND },
		{ ImageID::invalid(), VIRTKEY_SAVE_STATE },
		{ ImageID::invalid(), VIRTKEY_LOAD_STATE },
		{ ImageID::invalid(), VIRTKEY_NEXT_SLOT },
#if !defined(MOBILE_DEVICE)
		{ ImageID::invalid(), VIRTKEY_TOGGLE_FULLSCREEN },
#endif
		{ ImageID::invalid(), VIRTKEY_SPEED_CUSTOM1 },
		{ ImageID::invalid(), VIRTKEY_SPEED_CUSTOM2 },
		{ ImageID::invalid(), VIRTKEY_TEXTURE_DUMP },
		{ ImageID::invalid(), VIRTKEY_TEXTURE_REPLACE },
		{ ImageID::invalid(), VIRTKEY_SCREENSHOT },
		{ ImageID::invalid(), VIRTKEY_MUTE_TOGGLE },
		{ ImageID::invalid(), VIRTKEY_OPENCHAT },
		{ ImageID::invalid(), VIRTKEY_ANALOG_ROTATE_CW },
		{ ImageID::invalid(), VIRTKEY_ANALOG_ROTATE_CCW },
		{ ImageID::invalid(), VIRTKEY_PAUSE },
		{ ImageID::invalid(), VIRTKEY_RESET_EMULATION },
		{ ImageID::invalid(), VIRTKEY_DEVMENU },
#ifndef MOBILE_DEVICE
		{ ImageID::invalid(), VIRTKEY_RECORD },
#endif
		{ ImageID::invalid(), VIRTKEY_AXIS_X_MIN },
		{ ImageID::invalid(), VIRTKEY_AXIS_Y_MIN },
		{ ImageID::invalid(), VIRTKEY_AXIS_X_MAX },
		{ ImageID::invalid(), VIRTKEY_AXIS_Y_MAX },
		{ ImageID::invalid(), VIRTKEY_PREVIOUS_SLOT },
	};
	static_assert(ARRAY_SIZE(customKeyList) <= 64, "Too many key for a uint64_t bit mask");
};

// Gesture key only have virtual button that can work without constant press
namespace GestureKey {
	static const uint32_t keyList[] = {
		CTRL_SQUARE,
		CTRL_TRIANGLE,
		CTRL_CIRCLE,
		CTRL_CROSS,
		CTRL_UP,
		CTRL_DOWN,
		CTRL_LEFT,
		CTRL_RIGHT,
		CTRL_START,
		CTRL_SELECT,
		CTRL_LTRIGGER,
		CTRL_RTRIGGER,
		VIRTKEY_AXIS_Y_MAX,
		VIRTKEY_AXIS_Y_MIN,
		VIRTKEY_AXIS_X_MIN,
		VIRTKEY_AXIS_X_MAX, 
		VIRTKEY_SPEED_TOGGLE,
		VIRTKEY_REWIND, 
		VIRTKEY_SAVE_STATE,
		VIRTKEY_LOAD_STATE,
		VIRTKEY_PREVIOUS_SLOT,
		VIRTKEY_NEXT_SLOT,
		VIRTKEY_TEXTURE_DUMP, 
		VIRTKEY_TEXTURE_REPLACE,
		VIRTKEY_SCREENSHOT,
		VIRTKEY_MUTE_TOGGLE,
		VIRTKEY_OPENCHAT,
		VIRTKEY_PAUSE,
		VIRTKEY_DEVMENU,
#ifndef MOBILE_DEVICE
		VIRTKEY_RECORD,
#endif
		VIRTKEY_AXIS_X_MIN,
		VIRTKEY_AXIS_Y_MIN,
		VIRTKEY_AXIS_X_MAX,
		VIRTKEY_AXIS_Y_MAX,
	};
}

void GamepadTouch(bool reset = false);
void GamepadUpdateOpacity(float force = -1.0f);
float GamepadGetOpacity();

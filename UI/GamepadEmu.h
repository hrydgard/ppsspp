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
#include "UI/EmuScreen.h"

class GamepadView : public UI::View {
public:
	GamepadView(const char *key, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	bool Key(const KeyInput &input) override {
		return false;
	}
	void Update() override;
	std::string DescribeText() const override;

protected:
	virtual float GetButtonOpacity();

	const char *key_;
	double lastFrameTime_;
	float secondsWithoutTouch_ = 0.0;
};

class MultiTouchButton : public GamepadView {
public:
	MultiTouchButton(const char *key, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: GamepadView(key, layoutParams), scale_(scale), bgImg_(bgImg), bgDownImg_(bgDownImg), img_(img) {
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
	void Touch(const TouchInput &input) override;
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
	void Touch(const TouchInput &input) override;
	bool IsDown() override;

private:
	int pspButtonBit_;
};

class PSPDpad : public GamepadView {
public:
	PSPDpad(ImageID arrowIndex, const char *key, ImageID arrowDownIndex, ImageID overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
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

	void Touch(const TouchInput &input) override;
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
	PSPCustomStick(ImageID bgImg, const char *key, ImageID stickImg, ImageID stickDownImg, float scale, UI::LayoutParams *layoutParams);

	void Touch(const TouchInput &input) override;
	void Draw(UIContext &dc) override;

private:
	void ProcessTouch(float x, float y, bool down);

	float posX_ = 0.0f;
	float posY_ = 0.0f;
};

//initializes the layout from Config. if a default layout does not exist,
//it sets up default values
void InitPadLayout(float xres, float yres, float globalScale = 1.15f);
UI::ViewGroup *CreatePadLayout(float xres, float yres, bool *pause, ControlMapper* controllMapper);

const int D_pad_Radius = 50;
const int baseActionButtonSpacing = 60;

class ComboKey : public MultiTouchButton {
public:
	ComboKey(uint64_t pspButtonBit, const char *key, bool toggle, ControlMapper* controllMapper, ImageID bgImg, ImageID bgDownImg, ImageID img, float scale, UI::LayoutParams *layoutParams)
		: MultiTouchButton(key, bgImg, bgDownImg, img, scale, layoutParams), pspButtonBit_(pspButtonBit), toggle_(toggle), controllMapper_(controllMapper), on_(false) {
	}
	void Touch(const TouchInput &input) override;
	bool IsDown() override;
private:
	uint64_t pspButtonBit_;
	bool toggle_;
	ControlMapper* controllMapper_;
	bool on_;
};

// Just edit this to add new image, shape or button function
namespace CustomKey {
	// Image list
	struct keyImage {
		const char* n; // UI name
		ImageID i; // ImageID
		float r; // Rotation angle in degree
	};
	static const keyImage comboKeyImages[] = {
		{ "1", ImageID("I_1"), 0.0f },
		{ "2", ImageID("I_2"), 0.0f },
		{ "3", ImageID("I_3"), 0.0f },
		{ "4", ImageID("I_4"), 0.0f },
		{ "5", ImageID("I_5"), 0.0f },
		{ "6", ImageID("I_6"), 0.0f },
		{ "A", ImageID("I_A"), 0.0f },
		{ "B", ImageID("I_B"), 0.0f },
		{ "C", ImageID("I_C"), 0.0f },
		{ "D", ImageID("I_D"), 0.0f },
		{ "E", ImageID("I_E"), 0.0f },
		{ "F", ImageID("I_F"), 0.0f },
		{ "Circle", ImageID("I_CIRCLE"), 0.0f },
		{ "Cross", ImageID("I_CROSS"), 0.0f },
		{ "Square", ImageID("I_SQUARE"), 0.0f },
		{ "Triangle", ImageID("I_TRIANGLE"), 0.0f },
		{ "L", ImageID("I_L"), 0.0f },
		{ "R", ImageID("I_R"),  0.0f },
		{ "Start", ImageID("I_START"), 0.0f },
		{ "Select", ImageID("I_SELECT"), 0.0f },
		{ "Plus", ImageID("I_CROSS"), 45.0f },
		{ "Rhombus", ImageID("I_SQUARE"), 45.0f },
		{ "Down Triangle", ImageID("I_TRIANGLE"), 180.0f },
		{ "Arrow up", ImageID("I_ARROW"), 90.0f},
		{ "Arrow down", ImageID("I_ARROW"), 270.0f},
		{ "Arrow left", ImageID("I_ARROW"), 0.0f},
		{ "Arrow right", ImageID("I_ARROW"), 180.0f},
		{ "Gear", ImageID("I_GEAR"), 0.0f},
	};

	// Shape list
	struct keyShape {
		const char* n; // UI name
		ImageID i; // ImageID
		ImageID l; // ImageID line version
		float r; // Rotation angle in dregree
		bool f; // Flip Horizontally
	};
	static const keyShape comboKeyShapes[] = {
		{ "Circle", ImageID("I_ROUND"), ImageID("I_ROUND_LINE"), 0.0f, false },
		{ "Rectangle", ImageID("I_RECT"), ImageID("I_RECT_LINE"), 0.0f, false },
		{ "Vertical Rectangle", ImageID("I_RECT"), ImageID("I_RECT_LINE"), 90.0f, false },
		{ "L button", ImageID("I_SHOULDER"), ImageID("I_SHOULDER_LINE"), 0.0f, false },
		{ "R button", ImageID("I_SHOULDER"), ImageID("I_SHOULDER_LINE"), 0.0f, true },
		{ "Arrow up", ImageID("I_DIR"), ImageID("I_DIR_LINE"), 270.0f, false },
		{ "Arrow down", ImageID("I_DIR"), ImageID("I_DIR_LINE"), 90.0f, false },
		{ "Arrow left", ImageID("I_DIR"), ImageID("I_DIR_LINE"), 180.0f, false },
		{ "Arrow right", ImageID("I_DIR"), ImageID("I_DIR_LINE"), 0.0f, false },
	};

	// Button list
	struct keyList {
		const char* n; // UI name
		ImageID i; // UI ImageID
		uint32_t c; // Key code
	};
	static const keyList comboKeyList[] = {
		{ "Square", ImageID("I_SQUARE"), CTRL_SQUARE },
		{ "Triangle", ImageID("I_TRIANGLE"), CTRL_TRIANGLE },
		{ "Circle", ImageID("I_CIRCLE"), CTRL_CIRCLE },
		{ "Cross", ImageID("I_CROSS"), CTRL_CROSS },
		{ "Up", ImageID::invalid(), CTRL_UP },
		{ "Down", ImageID::invalid(), CTRL_DOWN },
		{ "Left", ImageID::invalid(), CTRL_LEFT },
		{ "Right", ImageID::invalid(), CTRL_RIGHT },
		{ "Start", ImageID("I_START"), CTRL_START },
		{ "Select", ImageID("I_SELECT"), CTRL_SELECT },
		{ "L", ImageID("I_L"), CTRL_LTRIGGER },
		{ "R", ImageID("I_R"), CTRL_RTRIGGER },
		{ "RapidFire", ImageID::invalid(), VIRTKEY_RAPID_FIRE },
		{ "Unthrottle", ImageID::invalid(), VIRTKEY_UNTHROTTLE },
		{ "SpeedToggle", ImageID::invalid(), VIRTKEY_SPEED_TOGGLE },
		{ "Rewind", ImageID::invalid(), VIRTKEY_REWIND },
		{ "Save State", ImageID::invalid(), VIRTKEY_SAVE_STATE },
		{ "Load State", ImageID::invalid(), VIRTKEY_LOAD_STATE },
		{ "Next Slot", ImageID::invalid(), VIRTKEY_NEXT_SLOT },
		{ "Toggle Fullscreen", ImageID::invalid(), VIRTKEY_TOGGLE_FULLSCREEN },
		{ "Alt speed 1", ImageID::invalid(), VIRTKEY_SPEED_CUSTOM1 },
		{ "Alt speed 2", ImageID::invalid(), VIRTKEY_SPEED_CUSTOM2 },
		{ "Texture Dumping", ImageID::invalid(), VIRTKEY_TEXTURE_DUMP },
		{ "Texture Replacement", ImageID::invalid(), VIRTKEY_TEXTURE_REPLACE },
		{ "Screenshot", ImageID::invalid(), VIRTKEY_SCREENSHOT },
		{ "Mute toggle", ImageID::invalid(), VIRTKEY_MUTE_TOGGLE },
		{ "OpenChat", ImageID::invalid(), VIRTKEY_OPENCHAT },
		{ "Auto Analog Rotation (CW)", ImageID::invalid(), VIRTKEY_ANALOG_ROTATE_CW },
		{ "Auto Analog Rotation (CCW)", ImageID::invalid(), VIRTKEY_ANALOG_ROTATE_CCW },
		{ "Pause", ImageID::invalid(), VIRTKEY_PAUSE },
		{ "DevMenu", ImageID::invalid(), VIRTKEY_DEVMENU },
#ifndef MOBILE_DEVICE
		{ "Record", ImageID::invalid(), VIRTKEY_RECORD },
#endif
	};
	static_assert(ARRAY_SIZE(comboKeyList) <= 64, "Too many key for a uint64_t bit mask");
};

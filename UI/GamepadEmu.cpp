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

#include <algorithm>

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Math/math_util.h"
#include "Common/UI/Context.h"

#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/ControlMapper.h"
#include "UI/GamepadEmu.h"

const float TOUCH_SCALE_FACTOR = 1.5f;

static uint32_t usedPointerMask = 0;
static uint32_t analogPointerMask = 0;

static float g_gamepadOpacity;
static double g_lastTouch;

MultiTouchButton *primaryButton[TOUCH_MAX_POINTERS]{};
std::set<int> g_activeGesturePointers;

void GamepadUpdateOpacity(float force) {
	if (force >= 0.0f) {
		g_gamepadOpacity = force;
		return;
	}
	if (coreState == CORE_RUNTIME_ERROR || coreState == CORE_POWERDOWN || coreState == CORE_STEPPING_GE) {
		// No need to show the controls.
		g_gamepadOpacity = 0.0f;
		return;
	}

	float fadeAfterSeconds = g_Config.iTouchButtonHideSeconds;
	float fadeTransitionSeconds = std::min(fadeAfterSeconds, 0.5f);
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;

	float multiplier = 1.0f;
	float secondsWithoutTouch = time_now_d() - g_lastTouch;
	if (secondsWithoutTouch >= fadeAfterSeconds && fadeAfterSeconds > 0.0f) {
		if (secondsWithoutTouch >= fadeAfterSeconds + fadeTransitionSeconds) {
			multiplier = 0.0f;
		} else {
			float secondsIntoFade = secondsWithoutTouch - fadeAfterSeconds;
			multiplier = 1.0f - (secondsIntoFade / fadeTransitionSeconds);
		}
	}

	g_gamepadOpacity = opacity * multiplier;
}

void GamepadResetTouch() {
	g_lastTouch = 0.0f;
}

void GamepadTouch() {
	g_lastTouch = time_now_d();
}

float GamepadGetOpacity() {
	return g_gamepadOpacity;
}

static u32 GetButtonColor() {
	return g_Config.iTouchButtonStyle != 0 ? 0xFFFFFF : 0xc0b080;
}

GamepadComponent::GamepadComponent(std::string_view key, UI::LayoutParams *layoutParams) : UI::View(layoutParams), key_(key) {}

std::string GamepadComponent::DescribeText() const {
	return key_;
}

void MultiTouchButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(bgImg_);
	if (image) {
		w = image->w * scale_;
		h = image->h * scale_;
	} else {
		w = 0.0f;
		h = 0.0f;
	}
}

bool MultiTouchButton::CanGlide() const {
	return g_Config.bTouchGliding;
}

bool MultiTouchButton::Touch(const TouchInput &input) {
	_dbg_assert_(input.id >= 0 && input.id < TOUCH_MAX_POINTERS);

	bool retval = GamepadComponent::Touch(input);
	if ((input.flags & TouchInputFlags::DOWN) && bounds_.Contains(input.x, input.y)) {
		pointerDownMask_ |= 1 << input.id;
		usedPointerMask |= 1 << input.id;
		if (CanGlide() && !primaryButton[input.id])
			primaryButton[input.id] = this;
	}
	if (input.flags & TouchInputFlags::MOVE) {
		if (!(input.flags & TouchInputFlags::MOUSE) || input.buttons) {
			bool ignorePress = false;
			if (g_activeGesturePointers.count(input.id)) {
				// Don't start a button touch if dragging and this pointer is already being used for a gesture.
				ignorePress = true;
			}
			if (bounds_.Contains(input.x, input.y) && !(analogPointerMask & (1 << input.id))) {
				if (!ignorePress) {
					if (CanGlide() && !primaryButton[input.id]) {
						primaryButton[input.id] = this;
					}
					pointerDownMask_ |= 1 << input.id;
				}
			} else if (primaryButton[input.id] != this) {
				pointerDownMask_ &= ~(1 << input.id);
			}
		}
	}
	if (input.flags & TouchInputFlags::UP) {
		pointerDownMask_ &= ~(1 << input.id);
		usedPointerMask &= ~(1 << input.id);
		primaryButton[input.id] = nullptr;
	}
	if (input.flags & TouchInputFlags::RELEASE_ALL) {
		pointerDownMask_ = 0;
		usedPointerMask = 0;
	}
	return retval;
}

void MultiTouchButton::Draw(UIContext &dc) {
	float opacity = std::max(g_gamepadOpacity, minimumAlpha_);
	if (opacity <= 0.0f)
		return;

	float scale = scale_;
	if (IsDown()) {
		if (g_Config.iTouchButtonStyle == 2) {
			opacity *= 1.35f;
		} else {
			scale *= TOUCH_SCALE_FACTOR;
			opacity *= 1.15f;
		}
	}

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t downBg = colorAlpha(0xFFFFFF, opacity * 0.5f);
	uint32_t color = colorAlpha(0xFFFFFF, opacity);

	if (IsDown() && g_Config.iTouchButtonStyle == 2) {
		if (bgImg_ != bgDownImg_)
			dc.Draw()->DrawImageRotated(bgDownImg_, bounds_.centerX(), bounds_.centerY(), scale, bgAngle_ * (M_PI * 2 / 360.0f), downBg, flipImageH_);
	}

	dc.Draw()->DrawImageRotated(bgImg_, bounds_.centerX(), bounds_.centerY(), scale, bgAngle_ * (M_PI * 2 / 360.0f), colorBg, flipImageH_);

	float x = bounds_.centerX();
	float y = bounds_.centerY();
	// Hack round the fact that the center of the rectangular picture the triangle is contained in
	// is not at the "weight center" of the triangle. Same for fast-forward.
	if (img_ == ImageID("I_TRIANGLE"))
		y -= 2.8f * scale;
	if (img_ == ImageID("I_FAST_FORWARD_LINE"))
		x += 2.0f * scale;
	dc.Draw()->DrawImageRotated(img_, x, y, scale, angle_ * (M_PI * 2 / 360.0f), color);
}

bool BoolButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	bool retval = MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;

	if (down != lastDown) {
		*value_ = down;
		UI::EventParams params{ this };
		params.a = down;
		OnChange.Trigger(params);
	}
	return retval;
}

bool PSPButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	bool retval = MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	if (down && !lastDown) {
		if (g_Config.bHapticFeedback) {
			System_Vibrate(HAPTIC_VIRTUAL_KEY);
		}
		__CtrlUpdateButtons(pspButtonBit_, 0);
	} else if (lastDown && !down) {
		__CtrlUpdateButtons(0, pspButtonBit_);
	}
	return retval;
}

bool CustomButton::IsDown() const {
	return (toggle_ && on_) || (!toggle_ && pointerDownMask_ != 0);
}

bool CustomButton::IsDownForFadeoutCheck() const {
	// This check is due to a stupid mistake, see the header.
#ifdef MOBILE_DEVICE
	constexpr int bitNumber = 36;
#else
	constexpr int bitNumber = 38;
#endif
	const bool down = IsDown() && !(pspButtonBit_ & (1ULL << bitNumber));  // VIRTKEY_TOGGLE_TOUCH_CONTROLS from g_customKeyList
	return down;
}

void CustomButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MultiTouchButton::GetContentDimensions(dc, w, h);
	if (invertedContentDimension_) {
		float tmp = w;
		w = h;
		h = tmp;
	}
}

bool CustomButton::Touch(const TouchInput &input) {
	using namespace CustomKeyData;
	bool lastDown = pointerDownMask_ != 0;
	bool retval = MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;

	if (down && !lastDown) {
		if (g_Config.bHapticFeedback)
			System_Vibrate(HAPTIC_VIRTUAL_KEY);

		if (!repeat_) {
			for (int i = 0; i < ARRAY_SIZE(g_customKeyList); i++) {
				if (pspButtonBit_ & (1ULL << i)) {
					controlMapper_->PSPKey(DEVICE_ID_TOUCH, g_customKeyList[i].c, (on_ && toggle_) ? KeyInputFlags::UP : KeyInputFlags::DOWN);
				}
			}
		}
		on_ = toggle_ ? !on_ : true;
	} else if (!toggle_ && lastDown && !down) {
		if (!repeat_) {
			for (int i = 0; i < ARRAY_SIZE(g_customKeyList); i++) {
				if (pspButtonBit_ & (1ULL << i)) {
					controlMapper_->PSPKey(DEVICE_ID_TOUCH, g_customKeyList[i].c, KeyInputFlags::UP);
				}
			}
		}
		on_ = false;
	}
	return retval;
}

void CustomButton::Update() {
	MultiTouchButton::Update();
	using namespace CustomKeyData;

	if (repeat_) {
		// Give the game some time to process the input, frame based so it's faster when fast-forwarding.
		static constexpr int DOWN_FRAME = 5;

		if (pressedFrames_ == 2*DOWN_FRAME) {
			pressedFrames_ = 0;
		} else if (pressedFrames_ == DOWN_FRAME) {
			for (int i = 0; i < ARRAY_SIZE(g_customKeyList); i++) {
				if (pspButtonBit_ & (1ULL << i)) {
					controlMapper_->PSPKey(DEVICE_ID_TOUCH, g_customKeyList[i].c, KeyInputFlags::UP);
				}
			}
		} else if (on_ && pressedFrames_ == 0) {
			for (int i = 0; i < ARRAY_SIZE(g_customKeyList); i++) {
				if (pspButtonBit_ & (1ULL << i)) {
					controlMapper_->PSPKey(DEVICE_ID_TOUCH, g_customKeyList[i].c, KeyInputFlags::DOWN);
				}
			}
			pressedFrames_ = 1;
		}

		if (pressedFrames_ > 0)
			pressedFrames_++;
	}
}

bool PSPButton::IsDown() const {
	return (__CtrlPeekButtonsVisual() & pspButtonBit_) != 0;
}

PSPDpad::PSPDpad(ImageID arrowIndex, std::string_view key, ImageID arrowDownIndex, ImageID overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams)
	: GamepadComponent(key, layoutParams), arrowIndex_(arrowIndex), arrowDownIndex_(arrowDownIndex), overlayIndex_(overlayIndex),
		scale_(scale), spacing_(spacing), dragPointerId_(-1), down_(0) {
}

void PSPDpad::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(arrowIndex_);
	if (image) {
		w = 2.0f * D_pad_Radius * spacing_ + image->w * scale_;
		h = w;
	} else {
		w = 0.0f;
		h = 0.0f;
	}
}

bool PSPDpad::Touch(const TouchInput &input) {
	bool retval = GamepadComponent::Touch(input);

	if (input.flags & TouchInputFlags::DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			dragPointerId_ = input.id;
			usedPointerMask |= 1 << input.id;
			ProcessTouch(input.x, input.y, true, false);
		}
	}
	if (input.flags & TouchInputFlags::MOVE) {
		if (!(input.flags & TouchInputFlags::MOUSE) || input.buttons) {
			bool ignorePress = false;
			if (g_activeGesturePointers.count(input.id)) {
				// Don't start a dpad touch if dragging and this pointer is already being used for a gesture.
				ignorePress = true;
			}

			if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y) && !(analogPointerMask & (1 << input.id))) {
				dragPointerId_ = input.id;
			}

			if (input.id == dragPointerId_) {
				ProcessTouch(input.x, input.y, true, ignorePress);
			}
		}
	}
	if (input.flags & TouchInputFlags::UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			usedPointerMask &= ~(1 << input.id);
			ProcessTouch(input.x, input.y, false, false);
		}
	}
	return retval;
}

void PSPDpad::ProcessTouch(float x, float y, bool down, bool ignorePress) {
	float stick_size = bounds_.w;
	float inv_stick_size = 1.0f / stick_size;
	const float deadzone = 0.05f;

	float dx = (x - bounds_.centerX()) * inv_stick_size;
	float dy = (y - bounds_.centerY()) * inv_stick_size;
	float rad = sqrtf(dx * dx + dy * dy);
	if (!g_Config.bStickyTouchDPad && (rad < deadzone || fabs(dx) > 0.5f || fabs(dy) > 0.5))
		down = false;

	int ctrlMask = 0;
	bool fourWay = g_Config.bDisableDpadDiagonals || rad < 0.2f;
	if (down) {
		if (fourWay) {
			int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 4) + 0.5f)) & 3;
			switch (direction) {
			case 0: ctrlMask = CTRL_RIGHT; break;
			case 1: ctrlMask = CTRL_DOWN; break;
			case 2: ctrlMask = CTRL_LEFT; break;
			case 3: ctrlMask = CTRL_UP; break;
			}
			// 4 way pad
		} else {
			// 8 way pad
			int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 8) + 0.5f)) & 7;
			switch (direction) {
			case 0: ctrlMask = CTRL_RIGHT; break;
			case 1: ctrlMask = CTRL_RIGHT | CTRL_DOWN; break;
			case 2: ctrlMask = CTRL_DOWN; break;
			case 3: ctrlMask = CTRL_DOWN | CTRL_LEFT; break;
			case 4: ctrlMask = CTRL_LEFT; break;
			case 5: ctrlMask = CTRL_UP | CTRL_LEFT; break;
			case 6: ctrlMask = CTRL_UP; break;
			case 7: ctrlMask = CTRL_UP | CTRL_RIGHT; break;
			}
		}
	}

	int lastDown = down_;
	int pressed = ctrlMask & ~lastDown;
	int released = (~ctrlMask) & lastDown;
	down_ = ctrlMask;
	bool vibrate = false;
	static const int dir[4] = { CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP };
	for (int i = 0; i < 4; i++) {
		if (!ignorePress && (pressed & dir[i])) {
			vibrate = true;
			__CtrlUpdateButtons(dir[i], 0);
		}
		if (released & dir[i]) {
			__CtrlUpdateButtons(0, dir[i]);
		}
	}
	if (vibrate && g_Config.bHapticFeedback) {
		System_Vibrate(HAPTIC_VIRTUAL_KEY);
	}
}

void PSPDpad::Draw(UIContext &dc) {
	float opacity = g_gamepadOpacity;
	if (opacity <= 0.0f)
		return;

	static const float xoff[4] = {1, 0, -1, 0};
	static const float yoff[4] = {0, 1, 0, -1};
	static const int dir[4] = {CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP};
	int buttons = __CtrlPeekButtons();
	float r = D_pad_Radius * spacing_;
	for (int i = 0; i < 4; i++) {
		bool isDown = (buttons & dir[i]) != 0;

		float x = bounds_.centerX() + xoff[i] * r;
		float y = bounds_.centerY() + yoff[i] * r;
		float x2 = bounds_.centerX() + xoff[i] * (r + 10.f * scale_);
		float y2 = bounds_.centerY() + yoff[i] * (r + 10.f * scale_);
		float angle = i * (M_PI / 2.0f);
		float imgScale = isDown ? scale_ * TOUCH_SCALE_FACTOR : scale_;
		float imgOpacity = opacity;

		if (isDown && g_Config.iTouchButtonStyle == 2) {
			imgScale = scale_;
			imgOpacity *= 1.35f;

			uint32_t downBg = colorAlpha(0x00FFFFFF, imgOpacity * 0.5f);
			if (arrowIndex_ != arrowDownIndex_)
				dc.Draw()->DrawImageRotated(arrowDownIndex_, x, y, imgScale, angle + PI, downBg, false);
		}

		uint32_t colorBg = colorAlpha(GetButtonColor(), imgOpacity);
		uint32_t color = colorAlpha(0xFFFFFF, imgOpacity);

		dc.Draw()->DrawImageRotated(arrowIndex_, x, y, imgScale, angle + PI, colorBg, false);
		if (overlayIndex_.isValid())
			dc.Draw()->DrawImageRotated(overlayIndex_, x2, y2, imgScale, angle + PI, color);
	}
}

PSPStick::PSPStick(ImageID bgImg, std::string_view key, ImageID stickImg, ImageID stickDownImg, int stick, float scale, UI::LayoutParams *layoutParams)
	: GamepadComponent(key, layoutParams), bgImg_(bgImg), stickImageIndex_(stickImg), stickDownImg_(stickDownImg), stick_(stick), scale_(scale) {
	stick_size_ = 50;
}

void PSPStick::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	dc.Draw()->GetAtlas()->measureImage(bgImg_, &w, &h);
	w *= scale_;
	h *= scale_;
}

void PSPStick::Draw(UIContext &dc) {
	float opacity = g_gamepadOpacity;
	if (opacity <= 0.0f)
		return;

	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2) {
		opacity *= 1.35f;
	}

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t downBg = colorAlpha(0x00FFFFFF, opacity * 0.5f);

	if (centerX_ < 0.0f) {
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
	}

	float stickX = centerX_;
	float stickY = centerY_;

	float dx, dy;
	__CtrlPeekAnalog(stick_, &dx, &dy);

	const TouchControlConfig &config = g_Config.GetTouchControlsConfig(g_display.GetDeviceOrientation());

	if (!config.bHideStickBackground)
		dc.Draw()->DrawImage(bgImg_, stickX, stickY, 1.0f * scale_, colorBg, ALIGN_CENTER);
	float headScale = stick_ ? config.fRightStickHeadScale : config.fLeftStickHeadScale;
	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2 && stickDownImg_ != stickImageIndex_)
		dc.Draw()->DrawImage(stickDownImg_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_ * headScale, downBg, ALIGN_CENTER);
	dc.Draw()->DrawImage(stickImageIndex_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_ * headScale, colorBg, ALIGN_CENTER);
}

bool PSPStick::Touch(const TouchInput &input) {
	bool retval = GamepadComponent::Touch(input);
	if (input.flags & TouchInputFlags::RELEASE_ALL) {
		dragPointerId_ = -1;
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
		__CtrlSetAnalogXY(stick_, 0.0f, 0.0f);
		usedPointerMask = 0;
		analogPointerMask = 0;
		return retval;
	}
	if (input.flags & TouchInputFlags::DOWN) {
		const TouchControlConfig &config = g_Config.GetTouchControlsConfig(g_display.GetDeviceOrientation());
		float fac = 0.5f * (stick_ ? config.fRightStickHeadScale : config.fLeftStickHeadScale)-0.5f;
		if (dragPointerId_ == -1 && bounds_.Expand(bounds_.w*fac, bounds_.h*fac).Contains(input.x, input.y)) {
			if (g_Config.bAutoCenterTouchAnalog) {
				centerX_ = input.x;
				centerY_ = input.y;
			} else {
				centerX_ = bounds_.centerX();
				centerY_ = bounds_.centerY();
			}
			dragPointerId_ = input.id;
			usedPointerMask |= 1 << input.id;
			analogPointerMask |= 1 << input.id;
			ProcessTouch(input.x, input.y, true);
			retval = true;
		}
	}
	if (input.flags & TouchInputFlags::MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
			retval = true;
		}
	}
	if (input.flags & TouchInputFlags::UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			centerX_ = bounds_.centerX();
			centerY_ = bounds_.centerY();
			usedPointerMask &= ~(1 << input.id);
			analogPointerMask &= ~(1 << input.id);
			ProcessTouch(input.x, input.y, false);
			retval = true;
		}
	}
	return retval;
}

void PSPStick::ProcessTouch(float x, float y, bool down) {
	if (down && centerX_ >= 0.0f) {
		float inv_stick_size = 1.0f / (stick_size_ * scale_);

		float dx = (x - centerX_) * inv_stick_size;
		float dy = (y - centerY_) * inv_stick_size;
		// Do not clamp to a circle! The PSP has nearly square range!

		// Old code to clamp to a circle
		// float len = sqrtf(dx * dx + dy * dy);
		// if (len > 1.0f) {
		//	dx /= len;
		//	dy /= len;
		//}

		// Still need to clamp to a square
		dx = std::min(1.0f, std::max(-1.0f, dx));
		dy = std::min(1.0f, std::max(-1.0f, dy));

		__CtrlSetAnalogXY(stick_, dx, -dy);
	} else {
		__CtrlSetAnalogXY(stick_, 0.0f, 0.0f);
	}
}

PSPCustomStick::PSPCustomStick(ImageID bgImg, const char *key, ImageID stickImg, ImageID stickDownImg, int stick, float scale, UI::LayoutParams *layoutParams)
	: PSPStick(bgImg, key, stickImg, stickDownImg, stick, scale, layoutParams) {
}

void PSPCustomStick::Draw(UIContext &dc) {
	float opacity = g_gamepadOpacity;
	if (opacity <= 0.0f)
		return;

	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2) {
		opacity *= 1.35f;
	}

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t downBg = colorAlpha(0x00FFFFFF, opacity * 0.5f);

	if (centerX_ < 0.0f) {
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
	}

	float stickX = centerX_;
	float stickY = centerY_;

	float dx, dy;
	dx = posX_;
	dy = -posY_;

	const TouchControlConfig &config = g_Config.GetTouchControlsConfig(g_display.GetDeviceOrientation());
	const float headScale = config.fRightStickHeadScale;
	if (!config.bHideStickBackground)
		dc.Draw()->DrawImage(bgImg_, stickX, stickY, 1.0f * scale_, colorBg, ALIGN_CENTER);
	if (dragPointerId_ != -1 && g_Config.iTouchButtonStyle == 2 && stickDownImg_ != stickImageIndex_)
		dc.Draw()->DrawImage(stickDownImg_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_ * headScale, downBg, ALIGN_CENTER);
	dc.Draw()->DrawImage(stickImageIndex_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_ * headScale, colorBg, ALIGN_CENTER);
}

bool PSPCustomStick::Touch(const TouchInput &input) {
	bool retval = GamepadComponent::Touch(input);
	if (input.flags & TouchInputFlags::RELEASE_ALL) {
		dragPointerId_ = -1;
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
		posX_ = 0.0f;
		posY_ = 0.0f;
		usedPointerMask = 0;
		analogPointerMask = 0;
		return false;
	}
	if (input.flags & TouchInputFlags::DOWN) {
		const TouchControlConfig &config = g_Config.GetTouchControlsConfig(g_display.GetDeviceOrientation());
		float fac = 0.5f * config.fRightStickHeadScale - 0.5f;
		if (dragPointerId_ == -1 && bounds_.Expand(bounds_.w*fac, bounds_.h*fac).Contains(input.x, input.y)) {
			if (g_Config.bAutoCenterTouchAnalog) {
				centerX_ = input.x;
				centerY_ = input.y;
			} else {
				centerX_ = bounds_.centerX();
				centerY_ = bounds_.centerY();
			}
			dragPointerId_ = input.id;
			usedPointerMask |= 1 << input.id;
			analogPointerMask |= 1 << input.id;
			ProcessTouch(input.x, input.y, true);
			retval = true;
		}
	}
	if (input.flags & TouchInputFlags::MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
			retval = true;
		}
	}
	if (input.flags & TouchInputFlags::UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			centerX_ = bounds_.centerX();
			centerY_ = bounds_.centerY();
			usedPointerMask &= ~(1 << input.id);
			analogPointerMask &= ~(1 << input.id);
			ProcessTouch(input.x, input.y, false);
			retval = true;
		}
	}
	return retval;
}

void PSPCustomStick::ProcessTouch(float x, float y, bool down) {
	static const int buttons[] = {0, CTRL_LTRIGGER, CTRL_RTRIGGER, CTRL_SQUARE, CTRL_TRIANGLE, CTRL_CIRCLE, CTRL_CROSS, CTRL_UP, CTRL_DOWN, CTRL_LEFT, CTRL_RIGHT, CTRL_START, CTRL_SELECT};
	static const int analogs[] = {VIRTKEY_AXIS_RIGHT_Y_MAX, VIRTKEY_AXIS_RIGHT_Y_MIN, VIRTKEY_AXIS_RIGHT_X_MIN, VIRTKEY_AXIS_RIGHT_X_MAX, VIRTKEY_AXIS_Y_MAX, VIRTKEY_AXIS_Y_MIN, VIRTKEY_AXIS_X_MIN, VIRTKEY_AXIS_X_MAX};
	u32 press = 0;
	u32 release = 0;

	auto toggle = [&](int config, bool simpleCheck, bool diagCheck = true) {
		if (config <= 0 || (size_t)config >= ARRAY_SIZE(buttons))
			return;

		if (simpleCheck && (!g_Config.bRightAnalogDisableDiagonal || diagCheck))
			press |= buttons[config];
		else
			release |= buttons[config];
	};

	auto analog = [&](float dx, float dy) {
		if (g_Config.bRightAnalogDisableDiagonal) {
			if (fabs(dx) > fabs(dy)) {
				dy = 0.0f;
			} else {
				dx = 0.0f;
			}
		}

		auto assign = [&](int config, float value) {
			if (config < ARRAY_SIZE(buttons) || config >= ARRAY_SIZE(buttons) + ARRAY_SIZE(analogs)) {
				return;
			}
			switch(analogs[config - ARRAY_SIZE(buttons)]) {
			case VIRTKEY_AXIS_Y_MAX:
				__CtrlSetAnalogY(0, value);
				break;
			case VIRTKEY_AXIS_Y_MIN:
				__CtrlSetAnalogY(0, value * -1.0f);
				break;
			case VIRTKEY_AXIS_X_MIN:
				__CtrlSetAnalogX(0, value * -1.0f);
				break;
			case VIRTKEY_AXIS_X_MAX:
				__CtrlSetAnalogX(0, value);
				break;
			case VIRTKEY_AXIS_RIGHT_Y_MAX:
				__CtrlSetAnalogY(1, value);
				break;
			case VIRTKEY_AXIS_RIGHT_Y_MIN:
				__CtrlSetAnalogY(1, value * -1.0f);
				break;
			case VIRTKEY_AXIS_RIGHT_X_MIN:
				__CtrlSetAnalogX(1, value * -1.0f);
				break;
			case VIRTKEY_AXIS_RIGHT_X_MAX:
				__CtrlSetAnalogX(1, value);
				break;
			}
		};

		// if/when we ever get iLeftAnalog settings, check stick_ for the config to use
		// let 0.0f through during centering
		if (dy >= 0.0f) {
			// down
			assign(g_Config.iRightAnalogUp, 0.0f);
			assign(g_Config.iRightAnalogDown, dy);
		}
		if (dy <= 0.0f) {
			// up
			assign(g_Config.iRightAnalogDown, 0.0f);
			assign(g_Config.iRightAnalogUp, dy * -1.0f);
		}
		if (dx <= 0.0f) {
			// left
			assign(g_Config.iRightAnalogRight, 0.0f);
			assign(g_Config.iRightAnalogLeft, dx * -1.0f);
		}
		if (dx >= 0.0f) {
			// right
			assign(g_Config.iRightAnalogLeft, 0.0f);
			assign(g_Config.iRightAnalogRight, dx);
		}
	};

	if (down && centerX_ >= 0.0f) {
		float inv_stick_size = 1.0f / (stick_size_ * scale_);

		float dx = (x - centerX_) * inv_stick_size;
		float dy = (y - centerY_) * inv_stick_size;

		dx = std::min(1.0f, std::max(-1.0f, dx));
		dy = std::min(1.0f, std::max(-1.0f, dy));

		toggle(g_Config.iRightAnalogRight, dx > 0.5f, fabs(dx) > fabs(dy));
		toggle(g_Config.iRightAnalogLeft, dx < -0.5f, fabs(dx) > fabs(dy));
		toggle(g_Config.iRightAnalogUp, dy < -0.5f, fabs(dx) <= fabs(dy));
		toggle(g_Config.iRightAnalogDown, dy > 0.5f, fabs(dx) <= fabs(dy));
		toggle(g_Config.iRightAnalogPress, true);

		analog(dx, dy);

		posX_ = dx;
		posY_ = dy;

	} else {
		toggle(g_Config.iRightAnalogRight, false);
		toggle(g_Config.iRightAnalogLeft, false);
		toggle(g_Config.iRightAnalogUp, false);
		toggle(g_Config.iRightAnalogDown, false);
		toggle(g_Config.iRightAnalogPress, false);

		analog(0.0f, 0.0f);

		posX_ = 0.0f;
		posY_ = 0.0f;
	}

	if (release || press) {
		__CtrlUpdateButtons(press, release);
	}
}

void InitPadLayout(TouchControlConfig *config, DeviceOrientation orientation, float xres, float yres, float globalScale) {
	const float scale = globalScale;
	const int halfW = xres / 2;

	const float screenBottom = orientation == DeviceOrientation::Portrait ? (yres - yres * 0.13f) : yres;

	auto initTouchPos = [=](ConfigTouchPos *touch, float x, float y, float extraScale = 1.0f) {
		if (touch->x == -1.0f || touch->y == -1.0f) {
			touch->x = x / xres;
			touch->y = std::max(y, 20.0f * globalScale) / yres;
			touch->scale = scale * extraScale;
		}
	};

	// Pause button. Has some special handling, it MUST be visible on some platforms.
	float Pause_button_center_X = halfW;
	float Pause_button_center_Y = 28.0f;

	if (!System_GetPropertyBool(SYSPROP_HAS_BACK_BUTTON)) {
		// Make really sure the pause button will be visible. Setting to -1 ensures reinit.
		if (config->touchPauseKey.x < 0.0f || config->touchPauseKey.x > 1.0f) {
			config->touchPauseKey.x = -1;
		}
		if (config->touchPauseKey.y < 0.0f || config->touchPauseKey.y > 1.0f) {
			config->touchPauseKey.y = -1;
		}
		config->touchPauseKey.show = true;
	}

	initTouchPos(&config->touchPauseKey, Pause_button_center_X, Pause_button_center_Y, 0.8f);

	// PSP buttons (triangle, circle, square, cross)---------------------
	// space between the PSP buttons (triangle, circle, square and cross)
	if (config->fActionButtonSpacing < 0) {
		config->fActionButtonSpacing = 1.0f;
	}

	// Position of the circle button (the PSP circle button). It is the farthest to the left
	float Action_button_spacing = config->fActionButtonSpacing * baseActionButtonSpacing;
	int Action_button_center_X = xres - Action_button_spacing * 2;
	int Action_button_center_Y = screenBottom - Action_button_spacing * 2;
	if (config->touchRightAnalogStick.show) {
		Action_button_center_Y -= 150 * scale;
	}
	initTouchPos(&config->touchActionButtonCenter, Action_button_center_X, Action_button_center_Y);

	//D-PAD (up down left right) (aka PSP cross)----------------------------
	//radius to the D-pad
	// TODO: Make configurable

	int D_pad_X = 2.5 * D_pad_Radius * scale;
	int D_pad_Y = screenBottom - D_pad_Radius * scale;
	if (config->touchAnalogStick.show) {
		D_pad_Y -= 200 * scale;
	}
	initTouchPos(&config->touchDpad, D_pad_X, D_pad_Y);

	//analog stick-------------------------------------------------------
	//keep the analog stick right below the D pad
	int analog_stick_X = D_pad_X;
	int analog_stick_Y = screenBottom - 80 * scale;
	initTouchPos(&config->touchAnalogStick, analog_stick_X, analog_stick_Y);

	//right analog stick-------------------------------------------------
	//keep the right analog stick right below the face buttons
	int right_analog_stick_X = Action_button_center_X;
	int right_analog_stick_Y = screenBottom - 80 * scale;
	initTouchPos(&config->touchRightAnalogStick, right_analog_stick_X, right_analog_stick_Y);

	//select, start, throttle--------------------------------------------
	//space between the bottom keys (space between select, start and un-throttle)
	float bottom_key_spacing = 100;
	if (g_display.dp_xres < 750) {
		bottom_key_spacing *= 0.8f;
	}

	// On IOS, nudge the bottom button up a little to avoid the task switcher.
	// Additionally, in portrait mode on any platform, move the defaults up by quite a bit - much
	// more comfortable to reach.
#if PPSSPP_PLATFORM(IOS)
	constexpr float bottom_button_Y = 80.0f;
#else
	constexpr float bottom_button_Y = 60.0f;
#endif
	if (orientation == DeviceOrientation::Portrait) {
		int start_key_X = halfW;
		int start_key_Y = screenBottom - bottom_button_Y * scale;
		initTouchPos(&config->touchStartKey, start_key_X, start_key_Y);

		int select_key_X = halfW;
		int select_key_Y = screenBottom - bottom_button_Y * scale - 50;
		initTouchPos(&config->touchSelectKey, select_key_X, select_key_Y);

		int fast_forward_key_X = halfW;
		int fast_forward_key_Y = screenBottom - bottom_button_Y * scale - 100;
		initTouchPos(&config->touchFastForwardKey, fast_forward_key_X, fast_forward_key_Y);
	} else {
		int start_key_X = halfW + bottom_key_spacing * scale;
		int start_key_Y = screenBottom - bottom_button_Y * scale;
		initTouchPos(&config->touchStartKey, start_key_X, start_key_Y);

		int select_key_X = halfW;
		int select_key_Y = screenBottom - bottom_button_Y * scale;
		initTouchPos(&config->touchSelectKey, select_key_X, select_key_Y);

		int fast_forward_key_X = halfW - bottom_key_spacing * scale;
		int fast_forward_key_Y = screenBottom - bottom_button_Y * scale;
		initTouchPos(&config->touchFastForwardKey, fast_forward_key_X, fast_forward_key_Y);
	}

	// L and R------------------------------------------------------------
	// Put them above the analog stick / above the buttons to the right.
	// The corners were very hard to reach..

	int l_key_X = 60 * scale;
	int l_key_Y = screenBottom - 380 * scale;
	initTouchPos(&config->touchLKey, l_key_X, l_key_Y);

	int r_key_X = xres - 60 * scale;
	int r_key_Y = l_key_Y;
	initTouchPos(&config->touchRKey, r_key_X, r_key_Y);

	struct { float x; float y; } customButtonPositions[10] = {
		{ 1.2f, 0.5f },
		{ 2.2f, 0.5f },
		{ 3.2f, 0.5f },
		{ 1.2f, 0.333f },
		{ 2.2f, 0.333f },
		{ -1.2f, 0.5f },
		{ -2.2f, 0.5f },
		{ -3.2f, 0.5f },
		{ -1.2f, 0.333f },
		{ -2.2f, 0.333f },
	};

	for (int i = 0; i < TouchControlConfig::CUSTOM_BUTTON_COUNT; i++) {
		const float y_offset = (float)(i / 10) * 0.08333f;

		int combo_key_X = halfW + bottom_key_spacing * scale * customButtonPositions[i % 10].x;
		int combo_key_Y = screenBottom * (y_offset + customButtonPositions[i % 10].y);

		initTouchPos(&config->touchCustom[i], combo_key_X, combo_key_Y);
	}
}

UI::ViewGroup *CreatePadLayout(const TouchControlConfig &config, float xres, float yres, bool *pause, ControlMapper *controlMapper) {
	using namespace UI;
	return new GamepadEmuView(config, xres, yres, pause, controlMapper, new UI::LayoutParams(UI::FILL_PARENT, UI::FILL_PARENT));
}

GamepadEmuView::GamepadEmuView(const TouchControlConfig &config, float xres, float yres, bool *pause, ControlMapper *controlMapper, UI::LayoutParams *layoutParams)
	: UI::AnchorLayout(layoutParams)
{
	if (!g_Config.bShowTouchControls) {
		return;
	}

	using namespace UI;

	struct ButtonOffset {
		float x;
		float y;
	};
	auto buttonLayoutParams = [=](const ConfigTouchPos &touch, ButtonOffset off = { 0, 0 }) {
		return new AnchorLayoutParams(touch.x * xres + off.x, touch.y * yres + off.y, NONE, NONE, Centering::Both);
	};

	// Space between the PSP buttons (triangle, circle, square and cross)
	const float actionButtonSpacing = config.fActionButtonSpacing * baseActionButtonSpacing;
	// Position of the circle button (the PSP circle button).  It is the farthest to the right.
	ButtonOffset circleOffset{ actionButtonSpacing, 0.0f };
	ButtonOffset crossOffset{ 0.0f, actionButtonSpacing };
	ButtonOffset triangleOffset{ 0.0f, -actionButtonSpacing };
	ButtonOffset squareOffset{ -actionButtonSpacing, 0.0f };

	const int halfW = xres / 2;

	const ImageID roundImage = g_Config.iTouchButtonStyle ? ImageID("I_ROUND_LINE") : ImageID("I_ROUND");
	const ImageID rectImage = g_Config.iTouchButtonStyle ? ImageID("I_RECT_LINE") : ImageID("I_RECT");
	const ImageID shoulderImage = g_Config.iTouchButtonStyle ? ImageID("I_SHOULDER_LINE") : ImageID("I_SHOULDER");
	const ImageID stickImage = g_Config.iTouchButtonStyle ? ImageID("I_STICK_LINE") : ImageID("I_STICK");
	const ImageID stickBg = g_Config.iTouchButtonStyle ? ImageID("I_STICK_BG_LINE") : ImageID("I_STICK_BG");

	auto addPSPButton = [this, buttonLayoutParams](int buttonBit, const char *key, ImageID bgImg, ImageID bgDownImg, ImageID img, const ConfigTouchPos &touch, ButtonOffset off = { 0, 0 }) -> PSPButton * {
		if (touch.show) {
			return Add(new PSPButton(buttonBit, key, bgImg, bgDownImg, img, touch.scale, buttonLayoutParams(touch, off)));
		}
		return nullptr;
	};
	auto addBoolButton = [this, buttonLayoutParams](bool *value, const char *key, ImageID bgImg, ImageID bgDownImg, ImageID img, const ConfigTouchPos &touch) -> BoolButton * {
		if (touch.show) {
			return Add(new BoolButton(value, key, bgImg, bgDownImg, img, touch.scale, buttonLayoutParams(touch)));
		}
		return nullptr;
	};
	auto addCustomButton = [this, buttonLayoutParams, controlMapper](const ConfigCustomButton& cfg, const char *key, const ConfigTouchPos &touch) -> CustomButton * {
		using namespace CustomKeyData;
		if (touch.show) {
			_dbg_assert_(cfg.shape < ARRAY_SIZE(customKeyShapes));
			_dbg_assert_(cfg.image < ARRAY_SIZE(customKeyImages));

			// Note: cfg.shape and cfg.image are bounds-checked elsewhere.
			auto aux = Add(new CustomButton(cfg.key, key, cfg.toggle, cfg.repeat, controlMapper,
					g_Config.iTouchButtonStyle == 0 ? customKeyShapes[cfg.shape].i : customKeyShapes[cfg.shape].l, customKeyShapes[cfg.shape].i,
					customKeyImages[cfg.image].i, touch.scale, customKeyShapes[cfg.shape].d, buttonLayoutParams(touch)));
			aux->SetAngle(customKeyImages[cfg.image].r, customKeyShapes[cfg.shape].r);
			aux->FlipImageH(customKeyShapes[cfg.shape].f);
			return aux;
		}
		return nullptr;
	};

	if (config.touchPauseKey.show) {
		auto button = addBoolButton(pause, "Pause button", roundImage, ImageID("I_ROUND"), ImageID("I_HAMBURGER"), config.touchPauseKey);
		if (button) {
			// The user is not allowed to hide this completely on some platforms - it must be findable.
			button->SetMinimumAlpha(0.1f);
		}
	}

	// touchActionButtonCenter.show will always be true, since that's the default.
	if (config.bShowTouchCircle)
		addPSPButton(CTRL_CIRCLE, "Circle button", roundImage, ImageID("I_ROUND"), ImageID("I_CIRCLE"), config.touchActionButtonCenter, circleOffset);
	if (config.bShowTouchCross)
		addPSPButton(CTRL_CROSS, "Cross button", roundImage, ImageID("I_ROUND"), ImageID("I_CROSS"), config.touchActionButtonCenter, crossOffset);
	if (config.bShowTouchTriangle)
		addPSPButton(CTRL_TRIANGLE, "Triangle button", roundImage, ImageID("I_ROUND"), ImageID("I_TRIANGLE"), config.touchActionButtonCenter, triangleOffset);
	if (config.bShowTouchSquare)
		addPSPButton(CTRL_SQUARE, "Square button", roundImage, ImageID("I_ROUND"), ImageID("I_SQUARE"), config.touchActionButtonCenter, squareOffset);

	addPSPButton(CTRL_START, "Start button", rectImage, ImageID("I_RECT"), ImageID("I_START"), config.touchStartKey);
	addPSPButton(CTRL_SELECT, "Select button", rectImage, ImageID("I_RECT"), ImageID("I_SELECT"), config.touchSelectKey);

	BoolButton *fastForward = addBoolButton(&PSP_CoreParameter().fastForward, "Fast-forward button", rectImage, ImageID("I_RECT"), ImageID("I_FAST_FORWARD_LINE"), config.touchFastForwardKey);
	if (fastForward) {
		fastForward->OnChange.Add([](UI::EventParams &e) {
			if (e.a && coreState == CORE_STEPPING_CPU) {
				Core_Resume();
			}
		});
	}

	addPSPButton(CTRL_LTRIGGER, "Left shoulder button", shoulderImage, ImageID("I_SHOULDER"), ImageID("I_L"), config.touchLKey);
	PSPButton *rTrigger = addPSPButton(CTRL_RTRIGGER, "Right shoulder button", shoulderImage, ImageID("I_SHOULDER"), ImageID("I_R"), config.touchRKey);
	if (rTrigger)
		rTrigger->FlipImageH(true);

	if (config.touchDpad.show) {
		const ImageID dirImage = g_Config.iTouchButtonStyle ? ImageID("I_DIR_LINE") : ImageID("I_DIR");
		Add(new PSPDpad(dirImage, "D-pad", ImageID("I_DIR"), ImageID("I_ARROW"), config.touchDpad.scale, config.fDpadSpacing, buttonLayoutParams(config.touchDpad)));
	}

	if (config.touchAnalogStick.show)
		Add(new PSPStick(stickBg, "Left analog stick", stickImage, ImageID("I_STICK"), 0, config.touchAnalogStick.scale, buttonLayoutParams(config.touchAnalogStick)));

	if (config.touchRightAnalogStick.show) {
		if (g_Config.bRightAnalogCustom)
			Add(new PSPCustomStick(stickBg, "Right analog stick", stickImage, ImageID("I_STICK"), 1, config.touchRightAnalogStick.scale, buttonLayoutParams(config.touchRightAnalogStick)));
		else
			Add(new PSPStick(stickBg, "Right analog stick", stickImage, ImageID("I_STICK"), 1, config.touchRightAnalogStick.scale, buttonLayoutParams(config.touchRightAnalogStick)));
	}

	// Sanitize custom button images, while adding them.
	for (int i = 0; i < TouchControlConfig::CUSTOM_BUTTON_COUNT; i++) {
		if (g_Config.CustomButton[i].shape >= ARRAY_SIZE(CustomKeyData::customKeyShapes)) {
			g_Config.CustomButton[i].shape = 0;
		}
		if (g_Config.CustomButton[i].image >= ARRAY_SIZE(CustomKeyData::customKeyImages)) {
			g_Config.CustomButton[i].image = 0;
		}

		char temp[64];
		snprintf(temp, sizeof(temp), "Custom %d button", i + 1);
		addCustomButton(g_Config.CustomButton[i], temp, config.touchCustom[i]);
	}

	// Add the two gesture zones.
	for (int i = 0; i < 2; i++) {
		if (g_Config.gestureControls[i].bGestureControlEnabled) {
			// We have them both cover the whole surface, then limit in the touch handler.
			// This is because there's no easy way to do "half the screen" in AnchorLayout.
			// We can do more complex layout combinations, but meh.
			Add(new GestureGamepad(controlMapper, i, new AnchorLayoutParams(FILL_PARENT, FILL_PARENT, 0.0f, 0.0f, 0.0f, 0.0f)));
		}
	}
}

void GamepadEmuView::Update() {
	AnchorLayout::Update();
	GamepadUpdateOpacity();

	bool anyDown = false;
	for (auto view : views_) {
		GamepadComponent *component = dynamic_cast<GamepadComponent *>(view);
		if (component && component->IsDownForFadeoutCheck()) {
			// INFO_LOG(Log::System, "GamepadEmuView::Update: component is down for fadeout check: %s", component->DescribeText().c_str());
			anyDown = true;
		}
	}

	if (anyDown) {
		// INFO_LOG(Log::System, "last touch in update");
		g_lastTouch = time_now_d();
	}
}

const GestureControlConfig &GestureGamepad::GetZone() {
	// TODO: Add support for multiple zones.
	return g_Config.gestureControls[zoneIndex_];
}

GestureGamepad::~GestureGamepad() {
	if (dragPointerId_ != -1) {
		g_activeGesturePointers.erase(dragPointerId_);
	}
}

bool GestureGamepad::Touch(const TouchInput &input) {
	const GestureControlConfig &zone = GetZone();

	if (usedPointerMask & (1 << input.id)) {
		if (input.id == dragPointerId_)
			dragPointerId_ = -1;
		return false;
	}

	if (input.flags & TouchInputFlags::RELEASE_ALL) {
		dragPointerId_ = -1;
		g_activeGesturePointers.clear();
		return false;
	}

	if (input.flags & TouchInputFlags::DOWN) {
		// Handle the zones here, easier than with AnchorLayout.
		const float minX = zoneIndex_ == 0 ? 0.0f : bounds_.w * 0.5f;
		const float maxX = zoneIndex_ == 0 ? bounds_.w * 0.5f : bounds_.w;

		if (dragPointerId_ == -1 && input.x >= minX && input.x < maxX) {
			dragPointerId_ = input.id;
			lastX_ = input.x;
			lastY_ = input.y;
			downX_ = input.x;
			downY_ = input.y;

			const float now = time_now_d();
			if (now - lastTapRelease_ < 0.3f && !haveDoubleTapped_) {
				if (zone.iDoubleTapGesture != 0 )
					controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[zone.iDoubleTapGesture - 1], KeyInputFlags::DOWN);
				haveDoubleTapped_ = true;
			}

			lastTouchDown_ = now;
		}
	}
	if (input.flags & TouchInputFlags::MOVE) {
		if (input.id == dragPointerId_) {
			deltaX_ += input.x - lastX_;
			deltaY_ += input.y - lastY_;
			lastX_ = input.x;
			lastY_ = input.y;

			const float distance = sqrtf((input.x - downX_) * (input.x - downX_) + (input.y - downY_) * (input.y - downY_));
			// TODO: Configurable distance?
			if (distance > 50.0f * g_display.dpi_scale_x) {
				// The user has dragged some distance from the initial touch. Start ignoring button presses.
				g_activeGesturePointers.insert(input.id);
			} else {
				g_activeGesturePointers.erase(input.id);
			}

			if (zone.bAnalogGesture) {
				const float k = zone.fAnalogGestureSensitivity * 0.02;
				float dx = (input.x - downX_) * g_display.dpi_scale_x * k;
				float dy = (input.y - downY_) * g_display.dpi_scale_y * k;
				dx = std::clamp(dx, -1.0f, 1.0f);
				dy = std::clamp(dy, -1.0f, 1.0f);
				__CtrlSetAnalogXY(0, dx, -dy);
			}
		}
	}
	if (input.flags & TouchInputFlags::UP) {
		g_activeGesturePointers.erase(input.id);
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			if (time_now_d() - lastTouchDown_ < 0.3f)
				lastTapRelease_ = time_now_d();

			if (haveDoubleTapped_) {
				if (zone.iDoubleTapGesture != 0)
					controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[zone.iDoubleTapGesture - 1], KeyInputFlags::UP);
				haveDoubleTapped_ = false;
			}

			if (zone.bAnalogGesture)
				__CtrlSetAnalogXY(0, 0, 0);
		}
	}
	return true;
}

void GestureGamepad::Draw(UIContext &dc) {
	float opacity = g_Config.iTouchButtonOpacity / 100.0;
	if (opacity <= 0.0f)
		return;

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);

	if (GetZone().bAnalogGesture && dragPointerId_ != -1) {
		dc.Draw()->DrawImage(ImageID("I_CIRCLE"), downX_, downY_, 0.7f, colorBg, ALIGN_CENTER);
	}
}

void GestureGamepad::Update() {
	const float th = 1.0f;
	float dx = deltaX_ * g_display.dpi_scale_x * GetZone().fSwipeSensitivity;
	float dy = deltaY_ * g_display.dpi_scale_y * GetZone().fSwipeSensitivity;
	if (GetZone().iSwipeRight != 0) {
		if (dx > th) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeRight - 1], KeyInputFlags::DOWN);
			swipeRightReleased_ = false;
		} else if (!swipeRightReleased_) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeRight -1], KeyInputFlags::UP);
			swipeRightReleased_ = true;
		}
	}
	if (GetZone().iSwipeLeft != 0) {
		if (dx < -th) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeLeft - 1], KeyInputFlags::DOWN);
			swipeLeftReleased_ = false;
		} else if (!swipeLeftReleased_) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeLeft - 1], KeyInputFlags::UP);
			swipeLeftReleased_ = true;
		}
	}
	if (GetZone().iSwipeUp != 0) {
		if (dy < -th) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeUp - 1], KeyInputFlags::DOWN);
			swipeUpReleased_ = false;
		} else if (!swipeUpReleased_) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeUp - 1], KeyInputFlags::UP);
			swipeUpReleased_ = true;
		}
	}
	if (GetZone().iSwipeDown != 0) {
		if (dy > th) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeDown - 1], KeyInputFlags::DOWN);
			swipeDownReleased_ = false;
		} else if (!swipeDownReleased_) {
			controlMapper_->PSPKey(DEVICE_ID_TOUCH, GestureKey::keyList[GetZone().iSwipeDown - 1], KeyInputFlags::UP);
			swipeDownReleased_ = true;
		}
	}
	const float smoothing = GetZone().fSwipeSmoothing;
	deltaX_ *= smoothing;
	deltaY_ *= smoothing;
}

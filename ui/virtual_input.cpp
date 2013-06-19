#include <stdio.h>
#include <algorithm>

#include "base/logging.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx/texture_atlas.h"
#include "input/input_state.h"
#include "virtual_input.h"

TouchButton::TouchButton(const Atlas *atlas, int imageIndex, int overlayImageIndex, int button, int rotationAngle, bool mirror_h)
	: atlas_(atlas), imageIndex_(imageIndex), overlayImageIndex_(overlayImageIndex), button_(button), mirror_h_(mirror_h)
{
	memset(pointerDown, 0, sizeof(pointerDown));
	w_ = atlas_->images[imageIndex_].w;
	h_ = atlas_->images[imageIndex_].h;
	rotationAngle_ = (float)rotationAngle * 3.1415927 / 180.0f;
	isDown_ = false;
}

void TouchButton::update(InputState &input_state)
{
	bool down = false;
	for (int i = 0; i < MAX_POINTERS; i++) {
		if (input_state.pointer_down[i] && isInside(input_state.pointer_x[i], input_state.pointer_y[i]))
			down = true;
	}

	if (down)
		input_state.pad_buttons |= button_;

	isDown_ = (input_state.pad_buttons & button_) != 0;
}

void TouchButton::draw(DrawBuffer &db, uint32_t color, uint32_t colorOverlay)
{
	float scale = 1.0f;
	if (isDown_) {
		color |= 0xFF000000;
		colorOverlay |= 0xFF000000;
		scale = 2.0f;
	}
	scale *= scale_;
	// We only mirror background
	db.DrawImageRotated(imageIndex_, x_ + w_*scale_/2, y_ + h_*scale_/2, scale, rotationAngle_, color, mirror_h_);
	if (overlayImageIndex_ != -1)
		db.DrawImageRotated(overlayImageIndex_, x_ + w_*scale_/2, y_ + h_*scale_/2, scale, rotationAngle_, colorOverlay);
}

TouchCrossPad::TouchCrossPad(const Atlas *atlas, int arrowIndex, int overlayIndex)
	: atlas_(atlas), arrowIndex_(arrowIndex), overlayIndex_(overlayIndex)
{

}

void TouchCrossPad::update(InputState &input_state)
{
	float stick_size_ = radius_ * 2;
	float inv_stick_size = 1.0f / (stick_size_ * scale_);
	const float deadzone = 0.17f;
	bool all_up = true;

	for (int i = 0; i < MAX_POINTERS; i++) {
		if (input_state.pointer_down[i]) {
			float dx = (input_state.pointer_x[i] - x_) * inv_stick_size;
			float dy = (input_state.pointer_y[i] - y_) * inv_stick_size;
			float rad = sqrtf(dx*dx+dy*dy);
			if (rad < deadzone || rad > 1.0f)
				continue;

			all_up = false;

			if (dx == 0 && dy == 0)
				continue;

			int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 8) + 0.5f)) & 7;
	
			input_state.pad_buttons &= ~(PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN);
			switch (direction) {
			case 0: input_state.pad_buttons |= PAD_BUTTON_RIGHT; break;
			case 1: input_state.pad_buttons |= PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN; break;
			case 2: input_state.pad_buttons |= PAD_BUTTON_DOWN; break;
			case 3: input_state.pad_buttons |= PAD_BUTTON_DOWN | PAD_BUTTON_LEFT; break;
			case 4: input_state.pad_buttons |= PAD_BUTTON_LEFT; break;
			case 5: input_state.pad_buttons |= PAD_BUTTON_UP | PAD_BUTTON_LEFT; break;
			case 6: input_state.pad_buttons |= PAD_BUTTON_UP; break;
			case 7: input_state.pad_buttons |= PAD_BUTTON_UP | PAD_BUTTON_RIGHT; break;
			}
		}
	}
	down_ = input_state.pad_buttons & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN);
}

void TouchCrossPad::draw(DrawBuffer &db, uint32_t color, uint32_t colorOverlay)
{
	static const float xoff[4] = {1, 0, -1, 0};
	static const float yoff[4] = {0, 1, 0, -1};
	static const int dir[4] = {PAD_BUTTON_RIGHT, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_UP};
	for (int i = 0; i < 4; i++) {
		float x = x_ + xoff[i] * scale_ * radius_;
		float y = y_ + yoff[i] * scale_ * radius_;
		float angle = i * M_PI / 2;
		float imgScale = (down_ & dir[i]) ? scale_ * 2 : scale_;
		db.DrawImageRotated(arrowIndex_, x, y, imgScale, angle + PI, color, false);
		if (overlayIndex_ != -1)
			db.DrawImageRotated(overlayIndex_, x, y, imgScale, angle + PI, colorOverlay);
	}
}

TouchStick::TouchStick(const Atlas *atlas, int bgImageIndex, int stickImageIndex, int stick)
	: atlas_(atlas), bgImageIndex_(bgImageIndex), stickImageIndex_(stickImageIndex), stick_(stick)
{
	stick_size_ = atlas_->images[bgImageIndex].w / 3.5f;
	memset(dragging_, 0, sizeof(dragging_));
	memset(lastPointerDown_, 0, sizeof(lastPointerDown_));
}

void TouchStick::update(InputState &input_state)
{
	float inv_stick_size = 1.0f / (stick_size_ * scale_);
	bool all_up = true;
	for (int i = 0; i < MAX_POINTERS; i++) {
		if (input_state.pointer_down[i]) {
			all_up = false;
			float dx = (input_state.pointer_x[i] - stick_x_) * inv_stick_size;
			float dy = (input_state.pointer_y[i] - stick_y_) * inv_stick_size;
			// Ignore outside box
			if (!dragging_[i] && (fabsf(dx) > 1.4f || fabsf(dy) > 1.4f))
				goto skip;
			if (!lastPointerDown_[i] && (fabsf(dx) < 1.4f && fabsf(dy) < 1.4f)) {
				dragging_[i] = true;
			}
			if (!dragging_[i])
				goto skip;

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

			if (stick_ == 0) {
				input_state.pad_lstick_x = dx;
				input_state.pad_lstick_y = -dy;
			} else if (stick_ == 1) {
				input_state.pad_rstick_x = dx;
				input_state.pad_rstick_y = -dy;
			}

		} else {
			dragging_[i] = false;
		}
skip:
		lastPointerDown_[i] = input_state.pointer_down[i];
	}

	stick_delta_x_ = input_state.pad_lstick_x;
	stick_delta_y_ = -input_state.pad_lstick_y;
}

void TouchStick::draw(DrawBuffer &db, uint32_t color)
{
	if (bgImageIndex_ != -1)
		db.DrawImage(bgImageIndex_, stick_x_, stick_y_, 1.0f * scale_, color, ALIGN_CENTER);
	db.DrawImage(stickImageIndex_, stick_x_ + stick_delta_x_ * stick_size_ * scale_, stick_y_ + stick_delta_y_ * stick_size_ * scale_, 1.0f * scale_, color, ALIGN_CENTER);
}

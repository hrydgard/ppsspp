#include "gfx_es2/draw_buffer.h"
#include "gfx/texture_atlas.h"
#include "input/input_state.h"
#include "virtual_input.h"

TouchButton::TouchButton(const Atlas *atlas, int imageIndex, int overlayImageIndex, int button, int rotationAngle)
	: atlas_(atlas), imageIndex_(imageIndex), overlayImageIndex_(overlayImageIndex), button_(button), rotationAngle_(rotationAngle)
{
	memset(pointerDown, 0, sizeof(pointerDown));
	w_ = atlas->images[imageIndex].w;
	h_ = atlas->images[imageIndex].h;
}

void TouchButton::update(InputState &input_state)
{
	bool isDown = false;
	for (int i = 0; i < MAX_POINTERS; i++) {
		if (input_state.pointer_down[i] && isInside(input_state.pointer_x[i], input_state.pointer_y[i]))
			isDown = true;
	}

	if (isDown) {
		int prev_buttons = input_state.pad_buttons;
		input_state.pad_buttons |= button_;
		input_state.pad_buttons_down |= button_ & (~prev_buttons);
	} else {
		input_state.pad_buttons_up &= ~(button_ & input_state.pad_buttons);
		input_state.pad_buttons &= ~button_;
	}
}

void TouchButton::draw(DrawBuffer &db)
{
	db.DrawImageRotated(imageIndex_, x_ + w_/2, y_ + h_/2, 1.0f, rotationAngle_, 0xFFFFFFFF);
	if (overlayImageIndex_ != -1)
		db.DrawImageRotated(overlayImageIndex_, x_ + w_/2, y_ + h_/2, 1.0f, rotationAngle_, 0xFFFFFFFF);
}


TouchStick::TouchStick(const Atlas *atlas, int bgImageIndex, int stickImageIndex, int stick)
	: atlas_(atlas), bgImageIndex_(bgImageIndex), stickImageIndex_(stickImageIndex), stick_(stick)
{
	
}

void TouchStick::update(InputState &input_state)
{
	float inv_stick_size = 1.0f / stick_size_;
	for (int i = 0; i < MAX_POINTERS; i++) {
		if (input_state.pointer_down[i]) {
			float dx = (input_state.pointer_x[i] - stick_x_) * inv_stick_size;
			float dy = (input_state.pointer_y[i] - stick_y_) * inv_stick_size;
			// Ignore outside box
			if (fabsf(dx) > 1.4f || fabsf(dy) > 1.4f)
				continue;
			// Clamp to a circle
			float len = sqrtf(dx * dx + dy * dy);
			if (len > 1.0f) {
				dx /= len;
				dy /= len;
			}
			stick_delta_x_ = dx;
			stick_delta_y_ = dy;
			if (stick_ == 0) {
				input_state.pad_lstick_x = dx;
				input_state.pad_lstick_y = -dy;
			} else if (stick_ == 1) {
				input_state.pad_rstick_x = dx;
				input_state.pad_rstick_y = -dy;
			}
		}
	}
}

void TouchStick::draw(DrawBuffer &db)
{
	if (bgImageIndex_ != -1)
		db.DrawImage(bgImageIndex_, stick_x_, stick_y_, 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
	db.DrawImage(stickImageIndex_, stick_x_ + stick_delta_x_, stick_y_ + stick_delta_y_, 1.0f, 0xFFFFFFFF, ALIGN_CENTER);
}
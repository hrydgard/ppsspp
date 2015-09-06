#pragma once

#include "math/math_util.h"
#include "gfx/texture_atlas.h"

class DrawBuffer;

// Multitouch-enabled emulation of a hardware button.
// Many of these can be pressed simultaneously with multitouch.
// (any finger will work, simultaneously with other virtual button/stick actions).
class TouchButton
{
public:
	TouchButton(const Atlas *atlas, int imageIndex, int overlayImageIndex, int button, int rotationAngle = 0, bool mirror_h = false);

	void update(InputState &input_state);
	void draw(DrawBuffer &db, uint32_t color, uint32_t colorOverlay);

	void setPos(float x, float y, float scale = 1.0f) {
		scale_ = scale;
		x_ = x - w_ * scale / 2;
		y_ = y - h_ * scale / 2;
	}

private:
	virtual bool isInside(float px, float py) const
	{
		float margin = 5.0f;
		return px >= x_ - margin * scale_ && py >= y_ - margin * scale_ && px <= x_ + (w_ + margin) * scale_ && py <= y_ + (h_ + margin) * scale_;
	}

	const Atlas *atlas_;

	int imageIndex_;
	int overlayImageIndex_;
	int button_;
	float rotationAngle_;
	bool mirror_h_;

	float x_, y_;
	float w_;
	float h_;
	float scale_;

	bool isDown_;

	// TODO: simplify into flags.
	bool pointerDown[MAX_POINTERS];
};

// 4-in-one directional pad, with support for single touch diagonals.
class TouchCrossPad
{
public:
	TouchCrossPad(const Atlas *atlas, int arrowIndex, int overlayIndex);

	void update(InputState &input_state);
	void draw(DrawBuffer &db, uint32_t color, uint32_t colorOverlay);

	void setPos(float x, float y, float radius, float scale = 1.0f) {
		x_ = x;
		y_ = y;
		radius_ = radius;
		scale_ = scale;
	}

private:
	const Atlas *atlas_;

	float x_;
	float y_;
	float radius_;
	float scale_;

	int arrowIndex_;
	int overlayIndex_;
	int down_;
};


// Multi-touch enabled virtual joystick
// Many of these can be used simultaneously with multitouch.
// (any finger will work, simultaneously with other virtual button/stick actions).
class TouchStick
{
public:
	TouchStick(const Atlas *atlas, int bgImageIndex, int stickImageIndex, int stick);

	void update(InputState &input_state);
	void draw(DrawBuffer &db, uint32_t color);

	void setPos(float x, float y, float scale = 1.0f) {
		stick_x_ = x;
		stick_y_ = y;
		scale_ = scale;
	}

private:
	const Atlas *atlas_;
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


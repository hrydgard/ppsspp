#pragma once

#include "gfx/texture_atlas.h"

class DrawBuffer;

// Multitouch-enabled emulation of a hardware button.
// (any finger will work, simultaneously with other virtual button/stick actions).
class TouchButton
{
public:
	TouchButton(const Atlas *atlas, int imageIndex, int overlayImageIndex, int button, int rotationAngle = 0);

	void update(InputState &input_state);
	void draw(DrawBuffer &db);

	void setPos(float x, float y) {
		x_ = x - w_ / 2;
		y_ = y - h_ / 2;
	}

private:
	virtual bool isInside(float px, float py) const
	{
		float margin = 5.0f;
		return px >= x_ - margin && py >= y_ - margin && px <= x_ + w_ + margin && py <= y_ + h_ + margin;
	}

	const Atlas *atlas_;

	int imageIndex_;
	int overlayImageIndex_;
	int button_;
	float rotationAngle_;

	float x_, y_;
	float w_;
	float h_;

  bool isDown_;

	// TODO: simplify into flags.
	bool pointerDown[MAX_POINTERS];
};


// Multi-touch enabled virtual joystick 
// (any finger will work, simultaneously with other virtual button/stick actions).
class TouchStick 
{
public:
	TouchStick(const Atlas *atlas, int bgImageIndex, int stickImageIndex, int stick);

	void update(InputState &input_state);
	void draw(DrawBuffer &db);

	void setPos(float x, float y) {
		stick_x_ = x;
		stick_y_ = y;
	}

private:
	const Atlas *atlas_;
	int bgImageIndex_;
	int stickImageIndex_;
	int stick_;
	int stick_size_;
	float stick_x_;
	float stick_y_;

	// maintained for drawing only
	float stick_delta_x_;
	float stick_delta_y_;
};


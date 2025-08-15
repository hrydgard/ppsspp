#include <algorithm>

#include "UI/JoystickHistoryView.h"

#include "Common/UI/Context.h"
#include "Common/UI/UI.h"

#include "Core/ControlMapper.h"

void JoystickHistoryView::Draw(UIContext &dc) {
	const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(ImageID("I_CROSS"));
	if (!image) {
		return;
	}
	float minRadius = std::min(bounds_.w, bounds_.h) * 0.5f - image->w;
	dc.Begin();
	Bounds textBounds(bounds_.x, bounds_.centerY() + minRadius + 5.0, bounds_.w, bounds_.h / 2 - minRadius - 5.0);
	dc.DrawTextShadowRect(title_, textBounds, 0xFFFFFFFF, ALIGN_TOP | ALIGN_HCENTER | FLAG_WRAP_TEXT);
	dc.Flush();
	dc.BeginNoTex();
	dc.Draw()->RectOutline(bounds_.centerX() - minRadius, bounds_.centerY() - minRadius, minRadius * 2.0f, minRadius * 2.0f, 0x80FFFFFF);
	dc.Flush();
	dc.Begin();

	// First draw a grid.
	float dx = 1.0f / 10.0f;
	for (int ix = -10; ix <= 10; ix++) {
		// First draw vertical lines.
		float fx = ix * dx;
		for (int iy = -10; iy < 10; iy++) {
			float ax = fx;
			float ay = iy * dx;
			float bx = fx;
			float by = (iy + 1) * dx;

			if (type_ == StickHistoryViewType::OUTPUT) {
				ConvertAnalogStick(ax, ay, &ax, &ay);
				ConvertAnalogStick(bx, by, &bx, &by);
			}

			ax = ax * minRadius + bounds_.centerX();
			ay = ay * minRadius + bounds_.centerY();

			bx = bx * minRadius + bounds_.centerX();
			by = by * minRadius + bounds_.centerY();

			dc.Draw()->Line(dc.theme->whiteImage, ax, ay, bx, by, 1.0, 0x70FFFFFF);
		}
	}

	for (int iy = -10; iy <= 10; iy++) {
		// Then horizontal.
		float fy = iy * dx;
		for (int ix = -10; ix < 10; ix++) {
			float ax = ix * dx;
			float ay = fy;
			float bx = (ix + 1) * dx;
			float by = fy;

			if (type_ == StickHistoryViewType::OUTPUT) {
				ConvertAnalogStick(ax, ay, &ax, &ay);
				ConvertAnalogStick(bx, by, &bx, &by);
			}

			ax = ax * minRadius + bounds_.centerX();
			ay = ay * minRadius + bounds_.centerY();

			bx = bx * minRadius + bounds_.centerX();
			by = by * minRadius + bounds_.centerY();

			dc.Draw()->Line(dc.theme->whiteImage, ax, ay, bx, by, 1.0, 0x70FFFFFF);
		}
	}


	int a = maxCount_ - (int)locations_.size();
	for (auto iter = locations_.begin(); iter != locations_.end(); ++iter) {
		float x = bounds_.centerX() + minRadius * iter->x;
		float y = bounds_.centerY() - minRadius * iter->y;
		float alpha = (float)a / (float)(maxCount_ - 1);
		if (alpha < 0.0f) {
			alpha = 0.0f;
		}
		// Emphasize the newest (higher) ones.
		alpha = powf(alpha, 3.7f);
		// Highlight the output (and OTHER)
		if (alpha >= 1.0f && type_ != StickHistoryViewType::INPUT) {
			dc.Draw()->DrawImage(ImageID("I_CIRCLE"), x, y, 1.0f, colorAlpha(0xFFFFFF, 1.0), ALIGN_CENTER);
		} else {
			dc.Draw()->DrawImage(ImageID("I_CIRCLE"), x, y, 0.8f, colorAlpha(0xC0C0C0, alpha * 0.5f), ALIGN_CENTER);
		}
		a++;
	}
	dc.Flush();
}

void JoystickHistoryView::Update() {
	locations_.push_back(Location{ curX_, curY_ });
	if ((int)locations_.size() > maxCount_) {
		locations_.pop_front();
	}
}

#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"

#include "base/colorutil.h"
#include "base/display.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"

OnScreenMessages osm;

void OnScreenMessages::Draw(DrawBuffer &draw) {
	// First, clean out old messages.
	std::lock_guard<std::recursive_mutex> guard(mutex_);

restart:
	double now = time_now_d();
	for (auto iter = messages_.begin(); iter != messages_.end(); iter++) {
		if (iter->endTime < now) {
			messages_.erase(iter);
			goto restart;
		}
	}

	// Get height
	float w, h;
	draw.MeasureText(UBUNTU24, "Wg", &w, &h);

	float y = 10.0f;
	// Then draw them all. 
	for (auto iter = messages_.begin(); iter != messages_.end(); ++iter) {
		float alpha = (iter->endTime - time_now_d()) * 4;
		if (alpha > 1.0) alpha = 1.0;
		if (alpha < 0.0) alpha = 0.0;
		draw.DrawTextShadow(UBUNTU24, iter->text.c_str(), dp_xres / 2, y, colorAlpha(iter->color, alpha), ALIGN_TOP | ALIGN_HCENTER);
		y += h;
	}
}

void OnScreenMessages::Show(const std::string &message, float duration_s, uint32_t color, int icon, bool checkUnique) {
	std::lock_guard<std::recursive_mutex> guard(mutex_);
	if (checkUnique) {
		for (auto iter = messages_.begin(); iter != messages_.end(); ++iter) {
			if (iter->text == message)
				return;
		}
	}
	double now = time_now_d();
	Message msg;
	msg.text = message;
	msg.color = color;
	msg.endTime = now + duration_s;
	msg.icon = icon;
	messages_.insert(messages_.begin(), msg);
}

void OnScreenMessages::ShowOnOff(const std::string &message, bool b, float duration_s, uint32_t color, int icon) {
	Show(message + (b ? ": on" : ": off"), duration_s, color, icon);
}

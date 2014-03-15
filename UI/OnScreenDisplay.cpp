#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"

OnScreenMessages osm;

void OnScreenMessages::Draw(DrawBuffer &draw, const Bounds &bounds) {
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
		float alpha = (iter->endTime - time_now_d()) * 4.0f;
		if (alpha > 1.0) alpha = 1.0f;
		if (alpha < 0.0) alpha = 0.0f;
		// Messages that are wider than the screen are left-aligned instead of centered.
		float tw, th;
		draw.MeasureText(UBUNTU24, iter->text.c_str(), &tw, &th);
		float x = bounds.centerX();
		int align = ALIGN_TOP | ALIGN_HCENTER;
		if (tw > bounds.w) {
			align = ALIGN_TOP | ALIGN_LEFT;
			x = 2;
		}
		draw.DrawTextShadow(UBUNTU24, iter->text.c_str(), x, y, colorAlpha(iter->color, alpha), align);
		y += h;
	}
}

void OnScreenMessages::Show(const std::string &message, float duration_s, uint32_t color, int icon, bool checkUnique) {
	double now = time_now_d();
	std::lock_guard<std::recursive_mutex> guard(mutex_);
	if (checkUnique) {
		for (auto iter = messages_.begin(); iter != messages_.end(); ++iter) {
			if (iter->text == message) {
				Message msg = *iter;
				msg.endTime = now + duration_s;
				messages_.erase(iter);
				messages_.insert(messages_.begin(), msg);
				return;
			}
		}
	}
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

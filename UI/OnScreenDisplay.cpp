#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"

#include "base/colorutil.h"
#include "base/timeutil.h"
#include "gfx_es2/draw_buffer.h"

#include "ui/ui_context.h"

OnScreenMessages osm;

void OnScreenMessagesView::Draw(UIContext &dc) {
	// First, clean out old messages.
	osm.Lock();
	osm.Clean();

	// Get height
	float w, h;
	dc.MeasureText(dc.theme->uiFont, "Wg", &w, &h);

	float y = 10.0f;
	// Then draw them all. 
	const std::list<OnScreenMessages::Message> &messages = osm.Messages();
	for (auto iter = messages.begin(); iter != messages.end(); ++iter) {
		float alpha = (iter->endTime - time_now_d()) * 4.0f;
		if (alpha > 1.0) alpha = 1.0f;
		if (alpha < 0.0) alpha = 0.0f;
		// Messages that are wider than the screen are left-aligned instead of centered.
		float tw, th;
		dc.MeasureText(dc.theme->uiFont, iter->text.c_str(), &tw, &th);
		float x = bounds_.centerX();
		int align = ALIGN_TOP | ALIGN_HCENTER;
		if (tw > bounds_.w) {
			align = ALIGN_TOP | ALIGN_LEFT;
			x = 2;
		}
		dc.SetFontStyle(dc.theme->uiFont);
		dc.DrawTextShadow(iter->text.c_str(), x, y, colorAlpha(iter->color, alpha), align);
		y += h;
	}

	osm.Unlock();
}

void OnScreenMessages::Clean() {
restart:
	double now = time_now_d();
	for (auto iter = messages_.begin(); iter != messages_.end(); iter++) {
		if (iter->endTime < now) {
			messages_.erase(iter);
			goto restart;
		}
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

#include <algorithm>
#include <sstream>
#include "UI/OnScreenDisplay.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/UI/Context.h"

#include "Common/TimeUtil.h"

OnScreenMessages osm;

void OnScreenMessagesView::Draw(UIContext &dc) {
	// First, clean out old messages.
	osm.Lock();
	osm.Clean();

	// Get height
	float w, h;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, "Wg", &w, &h);

	float y = 10.0f;
	// Then draw them all. 
	const std::list<OnScreenMessages::Message> &messages = osm.Messages();
	double now = time_now_d();
	for (auto iter = messages.begin(); iter != messages.end(); ++iter) {
		float alpha = (iter->endTime - now) * 4.0f;
		if (alpha > 1.0) alpha = 1.0f;
		if (alpha < 0.0) alpha = 0.0f;
		dc.SetFontScale(1.0f, 1.0f);
		// Messages that are wider than the screen are left-aligned instead of centered.

		int align = 0;
		// If we have newlines, we may be looking at ASCII debug output.  But let's verify.
		if (iter->text.find('\n') != 0) {
			if (!UTF8StringHasNonASCII(iter->text.c_str()))
				align |= FLAG_DYNAMIC_ASCII;
		}

		float tw, th;
		dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, iter->text.c_str(), &tw, &th, align);
		float x = bounds_.centerX();
		if (tw > bounds_.w) {
			align |= ALIGN_TOP | ALIGN_LEFT;
			x = 2;
		} else {
			align |= ALIGN_TOP | ALIGN_HCENTER;
		}
		float scale = 1.0f;
		if (th > bounds_.h - y) {
			// Scale down!
			scale = std::max(0.15f, (bounds_.h - y) / th);
			dc.SetFontScale(scale, scale);
		}
		dc.SetFontStyle(dc.theme->uiFont);
		dc.DrawTextShadow(iter->text.c_str(), x, y, colorAlpha(iter->color, alpha), align);
		y += th * scale;
	}

	osm.Unlock();
}

std::string OnScreenMessagesView::DescribeText() const {
	std::stringstream ss;
	const auto &messages = osm.Messages();
	for (auto iter = messages.begin(); iter != messages.end(); ++iter) {
		if (iter != messages.begin()) {
			ss << "\n";
		}
		ss << iter->text;
	}
	return ss.str();
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

void OnScreenMessages::Show(const std::string &text, float duration_s, uint32_t color, int icon, bool checkUnique, const char *id) {
	double now = time_now_d();
	std::lock_guard<std::mutex> guard(mutex_);
	if (checkUnique) {
		for (auto iter = messages_.begin(); iter != messages_.end(); ++iter) {
			if (iter->text == text || (id && iter->id && !strcmp(iter->id, id))) {
				Message msg = *iter;
				msg.endTime = now + duration_s;
				msg.text = text;
				msg.color = color;
				messages_.erase(iter);
				messages_.insert(messages_.begin(), msg);
				return;
			}
		}
	}
	Message msg;
	msg.text = text;
	msg.color = color;
	msg.endTime = now + duration_s;
	msg.icon = icon;
	msg.id = id;
	messages_.insert(messages_.begin(), msg);
}

void OnScreenMessages::ShowOnOff(const std::string &message, bool b, float duration_s, uint32_t color, int icon) {
	Show(message + (b ? ": on" : ": off"), duration_s, color, icon);
}

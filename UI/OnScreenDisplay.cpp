#include <algorithm>
#include <sstream>

#include "UI/OnScreenDisplay.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Render/DrawBuffer.h"

#include "Common/UI/Context.h"
#include "Common/System/System.h"

#include "Common/TimeUtil.h"
#include "Common/Net/HTTPClient.h"
#include "Core/Config.h"

void OnScreenMessagesView::Draw(UIContext &dc) {
	if (!g_Config.bShowOnScreenMessages) {
		return;
	}

	// Get height
	float w, h;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, "Wg", &w, &h);

	float y = 10.0f;
	// Then draw them all. 
	const std::vector<OnScreenDisplay::Message> messages = g_OSD.Messages();
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
		dc.DrawTextShadow(iter->text.c_str(), x, y, colorAlpha(0xFFFFFFFF, alpha), align);
		y += th * scale;
	}

	// Thin bar at the top of the screen.
	std::vector<float> progress = g_DownloadManager.GetCurrentProgress();
	if (!progress.empty()) {
		static const uint32_t colors[4] = {
			0xFFFFFFFF,
			0xFFCCCCCC,
			0xFFAAAAAA,
			0xFF777777,
		};

		dc.Begin();
		int h = 5;
		for (size_t i = 0; i < progress.size(); i++) {
			float barWidth = 10 + (dc.GetBounds().w - 10) * progress[i];
			Bounds bounds(0, h * i, barWidth, h);
			UI::Drawable solid(colors[i & 3]);
			dc.FillRect(solid, bounds);
		}
		dc.Flush();
	}
}

std::string OnScreenMessagesView::DescribeText() const {
	std::stringstream ss;
	const auto &messages = g_OSD.Messages();
	for (auto iter = messages.begin(); iter != messages.end(); ++iter) {
		if (iter != messages.begin()) {
			ss << "\n";
		}
		ss << iter->text;
	}
	return ss.str();
}

void OSDOverlayScreen::CreateViews() {
	root_ = new UI::AnchorLayout();
	root_->Add(new OnScreenMessagesView(new UI::AnchorLayoutParams(0.0f, 0.0f, 0.0f, 0.0f)));
}

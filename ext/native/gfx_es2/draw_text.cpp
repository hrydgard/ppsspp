#include "base/display.h"
#include "base/logging.h"
#include "base/stringutil.h"
#include "thin3d/thin3d.h"
#include "util/hash/hash.h"
#include "util/text/wrap_text.h"
#include "util/text/utf8.h"
#include "gfx_es2/draw_text.h"
#include "gfx_es2/draw_text_win.h"
#include "gfx_es2/draw_text_qt.h"
#include "gfx_es2/draw_text_android.h"

TextDrawer::TextDrawer(Draw::DrawContext *draw) : draw_(draw) {
	// These probably shouldn't be state.
	dpiScale_ = CalculateDPIScale();
}
TextDrawer::~TextDrawer() {
}

float TextDrawerWordWrapper::MeasureWidth(const char *str, size_t bytes) {
	float w, h;
	drawer_->MeasureString(str, bytes, &w, &h);
	return w;
}

void TextDrawer::WrapString(std::string &out, const char *str, float maxW) {
	TextDrawerWordWrapper wrapper(this, str, maxW);
	out = wrapper.Wrapped();
}

void TextDrawer::SetFontScale(float xscale, float yscale) {
	fontScaleX_ = xscale;
	fontScaleY_ = yscale;
}

float TextDrawer::CalculateDPIScale() {
	float scale = g_dpi_scale_y;
	if (scale >= 1.0f) {
		scale = 1.0f;
	}
	return scale;
}

TextDrawer *TextDrawer::Create(Draw::DrawContext *draw) {
	TextDrawer *drawer = nullptr;
#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
	drawer = new TextDrawerWin32(draw);
#elif defined(USING_QT_UI)
	drawer = new TextDrawerQt(draw);
#elif PPSSPP_PLATFORM(ANDROID)
	drawer = new TextDrawerAndroid(draw);
#endif
	if (drawer && !drawer->IsReady()) {
		delete drawer;
		drawer = nullptr;
	}
	return drawer;
}

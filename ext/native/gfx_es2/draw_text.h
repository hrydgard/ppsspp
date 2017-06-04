// draw_text

// Uses system fonts to draw text. 
// Platform support will be added over time, initially just Win32.

// Caches strings in individual textures. Might later combine them into a big one
// with dynamically allocated space but too much trouble for now.

#pragma once

#include "ppsspp_config.h"

#include <map>
#include <memory>

#include "base/basictypes.h"
#include "gfx_es2/draw_buffer.h"
#include "util/text/wrap_text.h"

namespace Draw {
	class DrawContext;
	class Texture;
}

#ifdef USING_QT_UI
#include <QtGui/QFont>
#endif

struct TextStringEntry {
	Draw::Texture *texture;
	int width;
	int height;
	int bmWidth;
	int bmHeight;
	int lastUsedFrame;
};

struct TextMeasureEntry {
	int width;
	int height;
	int lastUsedFrame;
};

// Not yet functional
enum {
	FONTSTYLE_BOLD = 1,
	FONTSTYLE_ITALIC = 2,
};

class TextDrawer {
public:
	TextDrawer(Draw::DrawContext *draw);
	virtual ~TextDrawer();

	virtual uint32_t SetFont(const char *fontName, int size, int flags) = 0;
	virtual void SetFont(uint32_t fontHandle) = 0;  // Shortcut once you've set the font once.
	void SetFontScale(float xscale, float yscale);
	virtual void MeasureString(const char *str, size_t len, float *w, float *h) = 0;
	virtual void MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align = ALIGN_TOPLEFT) = 0;
	virtual void DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align = ALIGN_TOPLEFT) = 0;
	virtual void DrawStringRect(DrawBuffer &target, const char *str, const Bounds &bounds, uint32_t color, int align) = 0;
	// Use for housekeeping like throwing out old strings.
	virtual void OncePerFrame() = 0;

	float CalculateDPIScale();

protected:
	Draw::DrawContext *draw_;
	virtual void ClearCache() = 0;
	virtual void RecreateFonts() = 0;  // On DPI change
	void WrapString(std::string &out, const char *str, float maxWidth);

	int frameCount_;
	float fontScaleX_;
	float fontScaleY_;
	float dpiScale_;
};


class TextDrawerWordWrapper : public WordWrapper {
public:
	TextDrawerWordWrapper(TextDrawer *drawer, const char *str, float maxW) : WordWrapper(str, maxW), drawer_(drawer) {
	}

protected:
	float MeasureWidth(const char *str, size_t bytes) override;

	TextDrawer *drawer_;
};


TextDrawer *CreateTextDrawer(Draw::DrawContext *draw);
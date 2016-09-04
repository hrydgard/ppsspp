// draw_text

// Uses system fonts to draw text. 
// Platform support will be added over time, initially just Win32.

// Caches strings in individual textures. Might later combine them into a big one
// with dynamically allocated space but too much trouble for now.

#pragma once

#include <map>

#include "base/basictypes.h"
#include "gfx_es2/draw_buffer.h"

class Thin3DContext;
class Thin3DTexture;

#ifdef USING_QT_UI
#include <QtGui/QFont>
#endif

struct TextStringEntry {
	Thin3DTexture *texture;
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

// Internal struct but all details in .cpp file (pimpl to avoid pulling in excessive headers here)
struct TextDrawerContext;
struct TextDrawerFontContext;

class TextDrawer {
public:
	TextDrawer(Thin3DContext *thin3d);
	~TextDrawer();

	uint32_t SetFont(const char *fontName, int size, int flags);
	void SetFont(uint32_t fontHandle);  // Shortcut once you've set the font once.

	void SetFontScale(float xscale, float yscale);
	void MeasureString(const char *str, float *w, float *h);
	void MeasureString(const char *str, size_t len, float *w, float *h);
	void MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align = ALIGN_TOPLEFT);
	void DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align = ALIGN_TOPLEFT);
	void DrawStringRect(DrawBuffer &target, const char *str, const Bounds &bounds, uint32_t color, int align);
	// Use for housekeeping like throwing out old strings.
	void OncePerFrame();

private:
	Thin3DContext *thin3d_;

	void WrapString(std::string &out, const char *str, float maxWidth);

	int frameCount_;
	float fontScaleX_;
	float fontScaleY_;

	TextDrawerContext *ctx_;
#ifdef USING_QT_UI
	std::map<uint32_t, QFont *> fontMap_;
#else
	std::map<uint32_t, TextDrawerFontContext *> fontMap_;
#endif

	uint32_t fontHash_;
	// The key is the CityHash of the string xor the fontHash_.
	std::map<uint32_t, TextStringEntry *> cache_;
	std::map<uint32_t, TextMeasureEntry *> sizeCache_;
};

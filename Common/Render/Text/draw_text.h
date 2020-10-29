// draw_text

// Uses system fonts to draw text. 
// Platform support will be added over time, initially just Win32.

// Caches strings in individual textures. Might later combine them into a big one
// with dynamically allocated space but too much trouble for now.

#pragma once

#include "ppsspp_config.h"

#include <memory>
#include <cstdint>

#include "Common/Data/Text/WrapText.h"
#include "Common/Render/DrawBuffer.h"

namespace Draw {
	class DrawContext;
	class Texture;
}

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

class TextDrawer {
public:
	virtual ~TextDrawer();

	virtual bool IsReady() const { return true; }
	virtual uint32_t SetFont(const char *fontName, int size, int flags) = 0;
	virtual void SetFont(uint32_t fontHandle) = 0;  // Shortcut once you've set the font once.
	void SetFontScale(float xscale, float yscale);
	virtual void MeasureString(const char *str, size_t len, float *w, float *h) = 0;
	virtual void MeasureStringRect(const char *str, size_t len, const Bounds &bounds, float *w, float *h, int align = ALIGN_TOPLEFT) = 0;
	virtual void DrawString(DrawBuffer &target, const char *str, float x, float y, uint32_t color, int align = ALIGN_TOPLEFT) = 0;
	void DrawStringRect(DrawBuffer &target, const char *str, const Bounds &bounds, uint32_t color, int align);
	virtual void DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, const char *str, int align = ALIGN_TOPLEFT) = 0;
	void DrawStringBitmapRect(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, const char *str, const Bounds &bounds, int align);
	// Use for housekeeping like throwing out old strings.
	virtual void OncePerFrame() = 0;

	float CalculateDPIScale();
	void SetForcedDPIScale(float dpi) {
		dpiScale_ = dpi;
		ignoreGlobalDpi_ = true;
	}

	// Factory function that selects implementation.
	static TextDrawer *Create(Draw::DrawContext *draw);

protected:
	TextDrawer(Draw::DrawContext *draw);

	Draw::DrawContext *draw_;
	virtual void ClearCache() = 0;
	void WrapString(std::string &out, const char *str, float maxWidth, int flags);

	struct CacheKey {
		bool operator < (const CacheKey &other) const {
			if (fontHash < other.fontHash)
				return true;
			if (fontHash > other.fontHash)
				return false;
			return text < other.text;
		}
		std::string text;
		uint32_t fontHash;
	};

	int frameCount_ = 0;
	float fontScaleX_ = 1.0f;
	float fontScaleY_ = 1.0f;
	float dpiScale_ = 1.0f;
	bool ignoreGlobalDpi_ = false;
};

class TextDrawerWordWrapper : public WordWrapper {
public:
	TextDrawerWordWrapper(TextDrawer *drawer, const char *str, float maxW, int flags)
		: WordWrapper(str, maxW, flags), drawer_(drawer) {}

protected:
	float MeasureWidth(const char *str, size_t bytes) override;

	TextDrawer *drawer_;
};

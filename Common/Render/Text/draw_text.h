// draw_text

// Uses system fonts to draw text. 
// Platform support will be added over time, initially just Win32.

// Caches strings in individual textures. Might later combine them into a big one
// with dynamically allocated space but too much trouble for now.

#pragma once

#include "ppsspp_config.h"

#include <memory>
#include <cstdint>
#include <map>

#include "Common/Data/Text/WrapText.h"
#include "Common/Render/DrawBuffer.h"

namespace Draw {
	class DrawContext;
	class Texture;
}

struct TextStringEntry {
	TextStringEntry(int frameCount) : lastUsedFrame(frameCount) {}

	Draw::Texture *texture = nullptr;
	int width = 0;
	int height = 0;
	int bmWidth = 0;
	int bmHeight = 0;
	int lastUsedFrame;
};

struct TextMeasureEntry {
	int width;
	int height;
	int leading;  // only used with Cocoa
	int lastUsedFrame;
};

class TextDrawer {
public:
	virtual ~TextDrawer();

	virtual bool IsReady() const { return true; }
	virtual uint32_t SetFont(const char *fontName, int size, int flags) = 0;
	virtual void SetFont(uint32_t fontHandle) = 0;  // Shortcut once you've set the font once.
	void SetFontScale(float xscale, float yscale);
	virtual void MeasureString(std::string_view str, float *w, float *h) = 0;
	virtual void MeasureStringRect(std::string_view str, const Bounds &bounds, float *w, float *h, int align = ALIGN_TOPLEFT);

	void DrawString(DrawBuffer &target, std::string_view str, float x, float y, uint32_t color, int align = ALIGN_TOPLEFT);
	void DrawStringRect(DrawBuffer &target, std::string_view str, const Bounds &bounds, uint32_t color, int align);
	virtual bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) = 0;
	bool DrawStringBitmapRect(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, const Bounds &bounds, int align, bool fullColor);
	// Use for housekeeping like throwing out old strings.
	void OncePerFrame();

	float CalculateDPIScale() const;
	void SetForcedDPIScale(float dpi) {
		dpiScale_ = dpi;
		ignoreGlobalDpi_ = true;
	}

	// Factory function that selects implementation.
	static TextDrawer *Create(Draw::DrawContext *draw);

protected:
	TextDrawer(Draw::DrawContext *draw);
	void ClearCache();

	virtual bool SupportsColorEmoji() const = 0;
	virtual void ClearFonts() = 0;

	void WrapString(std::string &out, std::string_view str, float maxWidth, int flags);

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

	Draw::DrawContext *draw_;

	int frameCount_ = 0;
	float fontScaleX_ = 1.0f;
	float fontScaleY_ = 1.0f;
	float dpiScale_ = 1.0f;
	bool ignoreGlobalDpi_ = false;

	uint32_t fontHash_ = 0;

	std::map<CacheKey, std::unique_ptr<TextStringEntry>> cache_;
	std::map<CacheKey, std::unique_ptr<TextMeasureEntry>> sizeCache_;
};

class TextDrawerWordWrapper : public WordWrapper {
public:
	TextDrawerWordWrapper(TextDrawer *drawer, std::string_view str, float maxW, int flags)
		: WordWrapper(str, maxW, flags), drawer_(drawer) {}

protected:
	float MeasureWidth(std::string_view str) override;

	TextDrawer *drawer_;
};

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
#include <unordered_map>

#include "Common/Data/Text/WrapText.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/Render/Text/Font.h"

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
	// Padding in bitmap pixels around the text, on each side, to fit a drop shadow.
	int shadowPad = 0;
	int lastUsedFrame;
};

struct TextMeasureEntry {
	int width;
	int height;
	int leading;  // only used with Cocoa
	int lastUsedFrame;
};

struct TextWrapEntry {
	std::string wrapped;
	int lastUsedFrame;
};

struct TextCacheKey {
	std::string text;
	FontStyle style;
	int flags = 0;
	// Wrapping only.
	float maxWidth = 0.0f;
	float scale = 0.0f;

	bool operator==(const TextCacheKey &other) const {
		return text == other.text && style == other.style && flags == other.flags && maxWidth == other.maxWidth && scale == other.scale;
	}
};

struct TextCacheKeyHash {
	size_t operator()(const TextCacheKey &key) const;
};

class TextDrawer {
public:
	virtual ~TextDrawer() = default;

	virtual bool IsReady() const { return true; }

	// The font and scale are passed to each call. The fonts are created as they're first used.
	void MeasureString(const FontStyle &style, float scaleX, float scaleY, std::string_view str, float *w, float *h);
	void MeasureStringRect(const FontStyle &style, float scaleX, float scaleY, std::string_view str, float maxWidth, float *w, float *h, int align = ALIGN_TOPLEFT);
	void DrawString(DrawBuffer &target, const FontStyle &style, float scaleX, float scaleY, std::string_view str, float x, float y, uint32_t color, int align = ALIGN_TOPLEFT);
	void DrawStringRect(DrawBuffer &target, const FontStyle &style, float scaleX, float scaleY, std::string_view str, const Bounds &bounds, uint32_t color, int align);
	// Rasterizes at scale 1, wrapping to bounds as if drawn at scale.
	bool DrawStringBitmapRect(const FontStyle &style, float scale, std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, const Bounds &bounds, int align, bool fullColor);
	// Use for housekeeping like throwing out old strings.
	void OncePerFrame();

	float CalculateDPIScale() const;
	// This is used by PPGe, since we run at the PSP's DPI inside of there.
	void SetForcedDPIScale(float dpi) {
		dpiScale_ = dpi;
		ignoreGlobalDpi_ = true;
	}

	// Factory function that selects implementation.
	static TextDrawer *Create(Draw::DrawContext *draw);

	size_t GetStringCacheSize() const {
		return cache_.size();
	}
	size_t GetCacheDataSize() const;

protected:
	TextDrawer(Draw::DrawContext *draw);

	// Creates the font for style, if it isn't already.
	virtual void EnsureFont(const FontStyle &style) = 0;
	// Implementations of these must handle multi-line strings. EnsureFont has been called for style.
	virtual void MeasureStringInternal(const FontStyle &style, std::string_view str, float *w, float *h) = 0;
	virtual bool DrawStringBitmap(const FontStyle &style, std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) = 0;

	void ClearCache();

	virtual bool SupportsColorEmoji() const = 0;
	virtual void ClearFonts() = 0;

	// Wraps and/or ellipsizes, caching the result. The reference stays valid until the next OncePerFrame.
	const std::string &WrapString(const FontStyle &style, float scale, std::string_view str, float maxWidth, int flags);

	// v has the 8-bit alpha.
	static u16 AlphaToPremul4444(u32 v) {
		v = (v >> 4) & 0x0F;
		v |= v << 4;
		v |= v << 8;
		return v;
	}
	static u32 AlphaToPremul8888(u32 v) {
		v |= v << 8;
		v |= v << 16;
		return v;
	}
	static u32 RGBAToPremul8888(u32 v) {
		u32 a = (v >> 24) & 0xFF;
		if (a == 0xFF)
			return v;
		if (a == 0)
			return 0;
		u32 r = (v >> 16) & 0xFF;
		u32 g = (v >> 8) & 0xFF;
		u32 b = v & 0xFF;
		r = (r * a + 127) / 255;
		g = (g * a + 127) / 255;
		b = (b * a + 127) / 255;
		return (a << 24) | (r << 16) | (g << 8) | b;
	}

	Draw::DrawContext *draw_;

	int frameCount_ = 0;
	float dpiScale_ = 1.0f;
	bool ignoreGlobalDpi_ = false;

	// We will clamp strings to this length to avoid various degenenerate behaviors.k
	static constexpr size_t MAX_TEXT_LENGTH = 16384;

	// Fills in and returns lookupKey_, which reuses its string buffer, so a cache hit doesn't allocate.
	const TextCacheKey &LookupKey(const FontStyle &style, std::string_view str, int flags = 0, float maxWidth = 0.0f, float scale = 0.0f);
	void AddDropShadow(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat srcFormat) const;

	TextCacheKey lookupKey_;

	std::unordered_map<TextCacheKey, std::unique_ptr<TextStringEntry>, TextCacheKeyHash> cache_;
	std::unordered_map<TextCacheKey, TextMeasureEntry, TextCacheKeyHash> sizeCache_;
	std::unordered_map<TextCacheKey, TextWrapEntry, TextCacheKeyHash> wrapCache_;
};

class TextDrawerWordWrapper : public WordWrapper {
public:
	TextDrawerWordWrapper(TextDrawer *drawer, const FontStyle &style, float scale, std::string_view str, float maxW, int flags)
		: WordWrapper(str, maxW, flags), drawer_(drawer), style_(style), scale_(scale) {}

protected:
	float MeasureWidth(std::string_view str) override;

	TextDrawer *drawer_;
	FontStyle style_;
	float scale_;
};

// The backends can use this to query the filenames of the fonts.
// Some backends want to just load all the fonts, just pass all.
// Note that the ttf file extension is included in the output.
std::vector<std::string> GetAllFontFilenames();
std::string GetFilenameForFontStyle(const FontStyle &font);
std::string GetFontNameForFontStyle(const FontStyle &font, FontStyleFlags *outFlags);

// Some languages use an override font, set via ini file.
void SetFontNameOverride(FontFamily family, std::string_view overrideFont);

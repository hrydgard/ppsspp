#pragma once

#include "ppsspp_config.h"

#include <map>
#include <set>
#include "Common/Render/Text/draw_text.h"

#if defined(USE_SDL3_TTF)

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#if defined(USE_SDL3_TTF_FONTCONFIG)
#include <fontconfig/fontconfig.h>
#endif

class TextDrawerSDL : public TextDrawer {
public:
	TextDrawerSDL(Draw::DrawContext *draw);
	~TextDrawerSDL();

	bool SupportsColorEmoji() const override { return true; }

protected:
	void EnsureFont(const FontStyle &style) override;
	void MeasureStringInternal(const FontStyle &style, std::string_view str, float *w, float *h) override;
	bool DrawStringBitmap(const FontStyle &style, std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

	void ClearFonts() override;

private:
	struct TextRun {
		std::string_view text;
		TTF_Font *emojiFont;  // nullptr for text, drawn with the base font.
		int width;
	};

	struct FallbackFontPath {
		std::string path;
		int faceIndex;
		bool emoji;
	};

	void PrepareFallbackFonts(std::string_view locale);
	bool FallbackHasGlyph(int index, uint32_t glyph, bool attachable, int ptSize);
	int FindFallbackFont(uint32_t glyph, bool emoji, int ptSize);
	TTF_Font *GetFallbackFont(int index, int ptSize);
	TTF_Font *FindEmojiFont(uint32_t emojiGlyph, int ptSize);
	void AttachFallbackFontsForString(TTF_Font *font, std::string_view text);
	std::vector<TextRun> LayoutLine(TTF_Font *font, std::string_view line, int *lineWidth);

	std::map<FontStyle, std::pair<TTF_Font *, uint8_t *>> fontMap_;

	std::vector<FallbackFontPath> fallbackFontPaths_;
	// Opened fallback fonts, by index in fallbackFontPaths_ and point size. Each size needs its own copy,
	// since fallbacks are attached to base fonts of different sizes.
	std::map<std::pair<int, int>, TTF_Font *> fallbackFonts_;

	// Keyed by glyph * 2 + emoji, see FindFallbackFont.
	std::map<uint32_t, int> glyphFallbackFontIndex_;
	std::map<TTF_Font *, std::set<int>> attachedFallbackIndices_;

	#if defined(USE_SDL3_TTF_FONTCONFIG)
	FcConfig *config;
	#endif
};

#endif

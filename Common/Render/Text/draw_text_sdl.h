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
	void PrepareFallbackFonts(std::string_view locale);
	uint32_t CheckMissingGlyph(TTF_Font *font, std::string_view text);
	int FindFallbackFonts(uint32_t missingGlyph, int ptSize);
	void AttachFallbackFontsForString(TTF_Font *font, std::string_view text, int ptSize);
	TTF_Font *FindEmojiFont(uint32_t emojiGlyph, int ptSize);

	std::map<FontStyle, std::pair<TTF_Font *, uint8_t *>> fontMap_;

	std::vector<TTF_Font *> fallbackFonts_;
	std::vector<std::pair<std::string, int>> fallbackFontPaths_; // path and font face index

	std::map<uint32_t, int> glyphFallbackFontIndex_;
	std::map<TTF_Font *, std::set<int>> attachedFallbackIndices_;

	#if defined(USE_SDL3_TTF_FONTCONFIG)
	FcConfig *config;
	#endif
};

#endif

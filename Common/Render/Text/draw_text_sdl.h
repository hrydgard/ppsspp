#pragma once

#include "ppsspp_config.h"

#include <map>
#include "Common/Render/Text/draw_text.h"

#if defined(USE_SDL2_TTF_FONTCONFIG)
#include <fontconfig/fontconfig.h>
#endif

// SDL2_ttf's TTF_Font is a typedef of _TTF_Font.
struct _TTF_Font;

class TextDrawerSDL : public TextDrawer {
public:
	TextDrawerSDL(Draw::DrawContext *draw);
	~TextDrawerSDL();

	uint32_t SetFont(const char *fontName, int size, int flags) override;
	void SetFont(uint32_t fontHandle) override;  // Shortcut once you've set the font once.
	void MeasureString(std::string_view str, float *w, float *h) override;
	bool DrawStringBitmap(std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;

protected:
	bool SupportsColorEmoji() const override { return false; }
	void ClearFonts() override;

private:
	void PrepareFallbackFonts(std::string_view locale);
	uint32_t CheckMissingGlyph(const std::string& text);
	int FindFallbackFonts(uint32_t missingGlyph, int ptSize);

	std::map<uint32_t, _TTF_Font *> fontMap_;

	std::vector<_TTF_Font *> fallbackFonts_;
	std::vector<std::pair<std::string, int>> fallbackFontPaths_; // path and font face index

	std::map<int, int> glyphFallbackFontIndex_;

#if defined(USE_SDL2_TTF_FONTCONFIG)
	FcConfig *config;
#endif
};

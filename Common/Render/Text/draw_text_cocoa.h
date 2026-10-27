#pragma once

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)

#include <map>
#include "Common/Render/Text/draw_text.h"

struct TextDrawerContext;
// Internal struct but all details in .cpp file (pimpl to avoid pulling in excessive headers here)
class TextDrawerFontContext;

class TextDrawerCocoa : public TextDrawer {
public:
	TextDrawerCocoa(Draw::DrawContext *draw);
	~TextDrawerCocoa();


protected:
	void EnsureFont(const FontStyle &style) override;
	void MeasureStringInternal(const FontStyle &style, std::string_view str, float *w, float *h) override;
	bool DrawStringBitmap(const FontStyle &style, std::vector<uint8_t> &bitmapData, TextStringEntry &entry, Draw::DataFormat texFormat, std::string_view str, int align, bool fullColor) override;
	bool SupportsColorEmoji() const override { return true; }

	void ClearFonts() override;

	TextDrawerContext *ctx_;
	std::map<FontStyle, std::unique_ptr<TextDrawerFontContext>> fontMap_;
};

#endif
